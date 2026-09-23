#include "repl/Session.h"
#include "compiler/Compiler.h"
#include "frontend/Desugar.h"
#include "frontend/Parser.h"
#include "runtime/ActorScheduler.h"
#include "runtime/Errors.h"
#include "runtime/Prelude.h"
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
    for (ClassInfo& t : builtinTypes()) globals_.defineBuiltinType(std::move(t));
    proto::ProtoContext ctx(&space_, runtime_.rootContext());
    loadPrelude(&ctx, engine_, globals_, modules_);
    bindPreludeHooks(&ctx, runtime_.mutableLayout(), globals_);
}

Session::~Session() {
    // Every exit path (normal end, uncaught error, :quit in the REPL) passes
    // here before the ProtoSpace member is destroyed (DESIGN §8.2).
    proto::ProtoContext ctx(&space_, runtime_.rootContext());
    ActorScheduler::instance().shutdown(&ctx);
    std::fflush(stdout);
}

EvalStatus Session::evaluate(const std::string& source, const std::string& sourceName,
                             UnitMode mode, const std::vector<std::string>* mainArgs,
                             EvalOutcome* outcome, bool allowIncomplete) {
    std::unique_ptr<CompilationUnit> unit;
    try {
        unit = parseSource(source);
    } catch (const ParseError& e) {
        if (mode == UnitMode::Repl && e.atEof && allowIncomplete) return EvalStatus::Incomplete;
        reportAt(sourceName, e.pos, e.what());
        return EvalStatus::Error;
    } catch (const ScalaError& e) {  // StackOverflowError: source nested too deeply
        std::fflush(stdout);
        std::fprintf(stderr, "%s: error: %s\n", sourceName.c_str(), e.what());
        return EvalStatus::Error;
    }
    // Globals are committed only when the unit compiles and runs without
    // error: a REPL input that fails defines nothing, and earlier bindings
    // stay as they were (GlobalTable.h: every definition has its own key).
    GlobalTable trial = globals_;
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
    } catch (const ScalaError& e) {  // StackOverflowError: source nested too deeply
        std::fflush(stdout);
        std::fprintf(stderr, "%s: error: %s\n", sourceName.c_str(), e.what());
        return EvalStatus::Error;
    }

    proto::ProtoContext ctx(&space_, runtime_.rootContext());
    cu.module->linkSymbols(&ctx);
    const BytecodeModule& mod = *cu.module;
    modules_.push_back(std::move(cu.module));
    try {
        engine_.run(&ctx, mod);
        if (!cu.mainName.empty() && mainArgs)
            callMain(&ctx, cu.mainKey, cu.mainTakesArgs, *mainArgs);
    } catch (const ScalaError& e) {
        std::fflush(stdout);
        std::fprintf(stderr, "%s:%d: error: %s\n", sourceName.c_str(), e.line, e.what());
        return EvalStatus::Error;
    }
    const RuntimeLayout& L = runtime_.layout();
    auto global = [&](const std::string& key) {
        const auto* sym = proto::ProtoString::createSymbol(&ctx, key.c_str());
        const proto::ProtoObject* v = L.globals->getOwnAttributeDirect(&ctx, sym);
        return v ? v : PROTO_NONE;
    };
    // A Unit-valued REPL expression binds no resN (Scala REPL): its name
    // and number stay free for the next result.
    bool bindsResult = !cu.resultName.empty();
    if (bindsResult && global(cu.resultKey) == L.unit) {
        trial.restore(cu.resultName, globals_);
        bindsResult = false;
    }
    if (bindsResult) ++resultCounter_;
    globals_ = std::move(trial);
    try {
        if (outcome) {
            for (const ReplDefinition& d : cu.definitions) {
                if (d.text.rfind("val ", 0) == 0 || d.text.rfind("var ", 0) == 0)
                    outcome->echo.push_back(d.text + " = " + showResult(&ctx, global(d.key)));
                else
                    outcome->echo.push_back(d.text);  // "def f", "lazy val x", "// defined class C"
            }
            if (bindsResult)
                outcome->echo.push_back("val " + cu.resultName + " = " +
                                        showResult(&ctx, global(cu.resultKey)));
        }
    } catch (const ScalaError& e) {  // a toString that throws
        std::fflush(stdout);
        std::fprintf(stderr, "%s: error: %s\n", sourceName.c_str(), e.what());
        return EvalStatus::Error;
    }
    return EvalStatus::Ok;
}

std::string Session::showResult(proto::ProtoContext* ctx, const proto::ProtoObject* v) {
    const std::string shown = engine_.showTopLevel(ctx, v);
    return proto::ProtoObject::isStringTagFast(v) ? "\"" + shown + "\"" : shown;
}

void Session::callMain(proto::ProtoContext* ctx, const std::string& mainKey, bool takesArgs,
                       const std::vector<std::string>& args) {
    const auto* key = proto::ProtoString::createSymbol(ctx, mainKey.c_str());
    const unsigned n = takesArgs ? static_cast<unsigned>(args.size()) : 0;
    // One scope for the call: slots [0, n) hold the argument Strings, slot n the
    // @main function, so every value stays rooted while the next one is allocated.
    proto::ProtoContext scope(ctx->space, ctx);
    scope.resizeAutomaticLocals(n + 1);
    scope.setAutomaticLocal(n, runtime_.layout().globals->getOwnAttributeDirect(&scope, key));
    for (unsigned k = 0; k < n; ++k)
        scope.setAutomaticLocal(k, makeString(&scope, args[k]));
    engine_.callTopLevel(&scope, scope.getAutomaticLocal(n), scope.getAutomaticLocals(), n);
}

int Session::runScript(const std::string& path, const std::vector<std::string>& args) {
    std::string source;
    if (!readFile(path, &source)) {
        std::fprintf(stderr, "protoscala: cannot open '%s'\n", path.c_str());
        return 1;
    }
    const EvalStatus s = evaluate(source, path, UnitMode::Script, &args, nullptr, false);
    std::fflush(stdout);
    return s == EvalStatus::Ok ? 0 : 1;
}

EvalOutcome Session::evalReplInput(const std::string& source, bool forceComplete) {
    EvalOutcome out;
    out.status = evaluate(source, "<console>", UnitMode::Repl, nullptr, &out, !forceComplete);
    return out;
}

bool Session::needsMoreInput(const std::string& source) const {
    try {
        parseSource(source);
    } catch (const ParseError& e) {
        return e.atEof;
    } catch (const ScalaError&) {  // StackOverflowError: reported when evaluated
    }
    return false;
}

bool Session::loadFile(const std::string& path) {
    std::string source;
    if (!readFile(path, &source)) {
        std::fprintf(stderr, "cannot open '%s'\n", path.c_str());
        return false;
    }
    static const std::vector<std::string> noArgs;
    return evaluate(source, path, UnitMode::Script, &noArgs, nullptr, false) == EvalStatus::Ok;
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
    } catch (const ScalaError& e) {  // StackOverflowError: source nested too deeply
        std::fprintf(stderr, "%s: error: %s\n", path.c_str(), e.what());
    }
    return 1;
}

} // namespace protoScala
