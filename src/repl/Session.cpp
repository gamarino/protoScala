#include "repl/Session.h"
#include "compiler/Compiler.h"
#include "frontend/Desugar.h"
#include "frontend/Parser.h"
#include "runtime/Errors.h"
#include "runtime/Primitives.h"
#include "runtime/Values.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace protoScala {

namespace {

bool readFile(const std::string& path, std::string* out) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) return false;  // missing, a directory, ...
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    *out = ss.str();
    return true;
}

void reportAt(const std::string& name, SourcePos pos, const std::string& msg) {
    std::fflush(stdout);
    std::fprintf(stderr, "%s:%d:%d: error: %s\n", name.c_str(), pos.line, pos.column, msg.c_str());
}

} // namespace

Session::Session() : runtime_(space_), engine_(runtime_.layout()) {
    installPrimitives(runtime_.rootContext(), runtime_.layout());
    for (const auto& n : builtinGlobalNames()) globals_.declare(n, BindingKind::Builtin);
}

Session::~Session() { std::fflush(stdout); }

EvalStatus Session::evaluate(const std::string& source, const std::string& sourceName,
                             UnitMode mode, const std::vector<std::string>* mainArgs,
                             EvalOutcome* outcome) {
    std::unique_ptr<CompilationUnit> unit;
    try {
        unit = parseSource(source);
    } catch (const ParseError& e) {
        if (mode == UnitMode::Repl && e.atEof) return EvalStatus::Incomplete;
        reportAt(sourceName, e.pos, e.what());
        return EvalStatus::Error;
    }
    GlobalTable trial = globals_;  // committed only if compilation succeeds
    CompiledUnit cu;
    try {
        desugar(*unit);
        Compiler compiler(trial);
        cu = compiler.compileUnit(*unit, mode, resultCounter_);
    } catch (const CompileError& e) {
        reportAt(sourceName, e.pos, e.what());
        return EvalStatus::Error;
    } catch (const std::length_error& e) {  // bytecode limits
        reportAt(sourceName, SourcePos{}, e.what());
        return EvalStatus::Error;
    }
    globals_ = std::move(trial);
    if (!cu.resultName.empty()) ++resultCounter_;

    proto::ProtoContext ctx(&space_, runtime_.rootContext());
    cu.module->linkSymbols(&ctx);
    const BytecodeModule& mod = *cu.module;
    modules_.push_back(std::move(cu.module));
    try {
        engine_.run(&ctx, mod);
        if (!cu.mainName.empty() && mainArgs)
            callMain(&ctx, cu.mainName, cu.mainTakesArgs, *mainArgs);
    } catch (const ScalaError& e) {
        std::fflush(stdout);
        std::fprintf(stderr, "%s:%d: error: %s\n", sourceName.c_str(), e.line, e.what());
        return EvalStatus::Error;
    }
    if (outcome) {
        const RuntimeLayout& L = runtime_.layout();
        auto valueOf = [&](const std::string& name) {
            const auto* key = proto::ProtoString::createSymbol(&ctx, name.c_str());
            const proto::ProtoObject* v = L.globals->getOwnAttributeDirect(&ctx, key);
            return show(&ctx, L, v ? v : PROTO_NONE);
        };
        for (const std::string& d : cu.definitions) {
            if (d.rfind("val ", 0) == 0 || d.rfind("var ", 0) == 0)
                outcome->echo.push_back(d + " = " + valueOf(d.substr(4)));
            else
                outcome->echo.push_back(d);  // "def f", "lazy val x"
        }
        if (!cu.resultName.empty()) {
            const std::string shown = valueOf(cu.resultName);
            if (shown != "()") outcome->echo.push_back("val " + cu.resultName + " = " + shown);
        }
    }
    return EvalStatus::Ok;
}

void Session::callMain(proto::ProtoContext* ctx, const std::string& name, bool takesArgs,
                       const std::vector<std::string>& args) {
    const auto* key = proto::ProtoString::createSymbol(ctx, name.c_str());
    const unsigned n = takesArgs ? static_cast<unsigned>(args.size()) : 0;
    // One scope for the call: slots [0, n) hold the argument Strings, slot n the
    // @main function, so every value stays rooted while the next one is allocated.
    proto::ProtoContext scope(ctx->space, ctx);
    scope.resizeAutomaticLocals(n + 1);
    scope.setAutomaticLocal(n, runtime_.layout().globals->getOwnAttributeDirect(&scope, key));
    for (unsigned k = 0; k < n; ++k)
        scope.setAutomaticLocal(k, scope.fromUTF8String(args[k].c_str()));
    engine_.callTopLevel(&scope, scope.getAutomaticLocal(n), scope.getAutomaticLocals(), n);
}

int Session::runScript(const std::string& path, const std::vector<std::string>& args) {
    std::string source;
    if (!readFile(path, &source)) {
        std::fprintf(stderr, "protoscala: cannot open '%s'\n", path.c_str());
        return 1;
    }
    const EvalStatus s = evaluate(source, path, UnitMode::Script, &args, nullptr);
    std::fflush(stdout);
    return s == EvalStatus::Ok ? 0 : 1;
}

EvalOutcome Session::evalReplInput(const std::string& source) {
    EvalOutcome out;
    out.status = evaluate(source, "<console>", UnitMode::Repl, nullptr, &out);
    return out;
}

bool Session::loadFile(const std::string& path) {
    std::string source;
    if (!readFile(path, &source)) {
        std::fprintf(stderr, "cannot open '%s'\n", path.c_str());
        return false;
    }
    static const std::vector<std::string> noArgs;
    return evaluate(source, path, UnitMode::Script, &noArgs, nullptr) == EvalStatus::Ok;
}

int Session::disassemble(const std::string& path) {
    std::string source;
    if (!readFile(path, &source)) {
        std::fprintf(stderr, "protoscala: cannot open '%s'\n", path.c_str());
        return 1;
    }
    try {
        auto unit = parseSource(source);
        desugar(*unit);
        GlobalTable trial = globals_;
        Compiler compiler(trial);
        const CompiledUnit cu = compiler.compileUnit(*unit, UnitMode::Script, 0);
        std::fputs(cu.module->disassemble().c_str(), stdout);
        return 0;
    } catch (const ParseError& e) {
        reportAt(path, e.pos, e.what());
    } catch (const CompileError& e) {
        reportAt(path, e.pos, e.what());
    } catch (const std::length_error& e) {  // bytecode limits
        reportAt(path, SourcePos{}, e.what());
    }
    return 1;
}

} // namespace protoScala
