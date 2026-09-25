#include "repl/Session.h"
#include "compiler/Compiler.h"
#include "frontend/Desugar.h"
#include "frontend/Parser.h"
#include "runtime/ActorScheduler.h"
#include "runtime/Errors.h"
#include "runtime/Prelude.h"
#include "runtime/Primitives.h"
#include "runtime/Values.h"
#include "umd/ForeignBoundary.h"
#include "umd/Prefixes.h"
#include "umd/ProviderPlugins.h"
#include "umd/ScalaModuleProvider.h"

#include <algorithm>
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

// The directory a file's imports are resolved relative to (INTEROP §2). Empty
// for "<console>" and for any name that is not a path.
std::string directoryOf(const std::string& sourceName) {
    if (sourceName.empty() || sourceName.front() == '<') return {};
    std::error_code ec;
    std::filesystem::path p(sourceName);
    std::filesystem::path dir = std::filesystem::absolute(p, ec).parent_path();
    return ec ? std::string{} : dir.lexically_normal().string();
}

// `util/Shapes.scala` -> `Shapes`: a module's name is its FILE name, not
// anything written inside it (D91).
std::string moduleObjectName(const std::string& logicalPath) {
    const auto dot = logicalPath.rfind('.');
    return dot == std::string::npos ? logicalPath : logicalPath.substr(dot + 1);
}

} // namespace

Session::Session() : runtime_(space_), engine_(runtime_.layout()) {
    installPrimitives(runtime_.rootContext(), runtime_.layout());
    for (const auto& n : builtinGlobalNames()) globals_.declare(n, BindingKind::Builtin);
    // The runtime's own by-name signatures (D47), declared next to the natives
    // that need them: nothing here names a particular global.
    for (const BuiltinByNameSignature& sig : builtinByNameSignatures())
        globals_.setByNameMasks(sig.global, sig.applyMasks);
    for (ClassInfo& t : builtinTypes()) globals_.defineBuiltinType(std::move(t));
    proto::ProtoContext ctx(&space_, runtime_.rootContext());
    loadPrelude(&ctx, engine_, globals_, modules_);
    bindPreludeHooks(&ctx, runtime_.mutableLayout(), globals_);
    // The table every module is compiled from: a COPY, so it shares the key
    // counters and a module's names can never collide with a session global
    // (A0-16). Taken here, after the prelude and before any user unit.
    preludeGlobals_ = globals_;
    // Registered before any thread that can import is started: the provider
    // resolves its host from ctx->space, so an actor worker reaches it too. A
    // thread-local would answer "module not found" on every worker.
    registerModuleHost(&space_, this);
    installScalaProvider(&ctx);
    pluginPaths_ = loadProviderPlugins(&ctx);
}

Session::~Session() {
    // Every exit path (normal end, uncaught error, :quit in the REPL) passes
    // here before the ProtoSpace member is destroyed (DESIGN §8.2).
    proto::ProtoContext ctx(&space_, runtime_.rootContext());
    ActorScheduler::instance().shutdown(&ctx);
    // AFTER the workers are joined by shutdown(): a worker can still be
    // importing until then, and the provider would find a dangling host.
    unregisterModuleHost(&space_, this);
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
        compiler.setModuleLoader(this);
        compiler.setSourceDir(directoryOf(sourceName));
        cu = compiler.compileUnit(*unit, mode, resultCounter_);
    } catch (const CompileError& e) {
        reportAt(sourceName, e.pos, e.what());
        return EvalStatus::Error;
    } catch (const ParseError& e) {
        // The desugarer rejects a few forms the parser cannot judge -- an
        // unknown string interpolator is the one (D56) -- and reports them as a
        // ParseError with a position. Without this arm it escaped as an
        // "internal error", which reads as a crash.
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
        // `object Main extends App`: initialising the object IS running the
        // program, as in Scala (D104). Only in script mode -- the REPL defines an
        // object without running it, which is what Scala's REPL does too.
        else if (!cu.appKey.empty() && mainArgs)
            forceGlobal(&ctx, cu.appKey);
    } catch (const ScalaThrow& t) {
        // An uncaught Scala exception, reported through the VALUE's own
        // toString, so a user-defined class says what its class says. The engine
        // must be active for that, and it is: this is the thread that ran it.
        std::fflush(stdout);
        std::fprintf(stderr, "%s:%d: error: %s\n", sourceName.c_str(), t.line,
                     showThrown(&ctx, t.value).c_str());
        return EvalStatus::Error;
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
    // A module this unit imported has already RUN, and stays loaded even when
    // the unit fails to compile or throws: its values live in the globals object
    // under keys that are never reused, so the leftover is unreachable and
    // harmless. Python behaves the same way (D90).
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
    } catch (const ScalaThrow& t) {  // a toString that throws
        std::fflush(stdout);
        std::fprintf(stderr, "%s: error: %s\n", sourceName.c_str(),
                     showThrown(&ctx, t.value).c_str());
        return EvalStatus::Error;
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

std::string Session::showThrown(proto::ProtoContext* ctx, const proto::ProtoObject* v) {
    try {
        return engine_.showTopLevel(ctx, v);
    } catch (...) {
        return "<exception whose toString failed>";
    }
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

// ---------------------------------------------------------------------------
// Phase 6: modules (DESIGN §9)
// ---------------------------------------------------------------------------

// a.b.C -> a/b/C.scala, searched in the importing file's directory, then each
// colon-separated entry of PROTOSCALA_PATH, then the working directory (A0-5).
std::vector<std::string> Session::candidatePathsOf(const std::string& logicalPath,
                                                   const std::string& importerDir) const {
    std::string rel = logicalPath;
    std::replace(rel.begin(), rel.end(), '.', '/');
    rel += ".scala";
    std::vector<std::string> out;
    if (!importerDir.empty()) out.push_back(importerDir + "/" + rel);
    if (const char* p = std::getenv("PROTOSCALA_PATH")) {
        std::stringstream ss(p);
        std::string dir;
        while (std::getline(ss, dir, ':'))
            if (!dir.empty()) out.push_back(dir + "/" + rel);
    }
    out.push_back(rel);
    return out;
}

std::string Session::findModuleFile(const std::string& logicalPath,
                                    const std::string& importerDir) {
    for (const std::string& cand : candidatePathsOf(logicalPath, importerDir)) {
        std::error_code ec;
        if (std::filesystem::is_regular_file(cand, ec)) {
            std::filesystem::path abs = std::filesystem::absolute(cand, ec);
            if (ec) return cand;
            return abs.lexically_normal().string();
        }
    }
    return "";  // a MISS, never an error: the resolver walks on
}

std::string Session::triedPathsOf(const std::string& logicalPath,
                                  const std::string& importerDir) {
    std::string out;
    for (const std::string& cand : candidatePathsOf(logicalPath, importerDir)) {
        if (!out.empty()) out += ", ";
        out += cand;
    }
    return out;
}

// The module's exports: the difference between its own table and the prelude
// snapshot it was copied from.
ModuleExports Session::collectExports(const GlobalTable& table,
                                      const std::string& objectName) const {
    ModuleExports ex;
    ex.moduleName = objectName;
    ex.moduleKind = BindingKind::Object;
    for (const std::string& name : table.typesDeclaredInUnit()) {
        const ClassInfo* info = table.findType(name);
        if (!info) continue;
        if (name == objectName + ".type") ex.moduleTypeKey = info->key;
        // The module's own class is exported too: the importing unit reads its
        // member list to resolve a selector import (`import M.{f}`).
        ex.types.emplace_back(name, *info);
    }
    for (const std::string& name : table.declaredInUnit()) {
        const GlobalBinding* b = table.binding(name);
        if (!b) continue;
        if (name == objectName) {
            ex.moduleKey = b->key;
            continue;
        }
        ex.terms.emplace_back(name, *b);
    }
    // The module's own by-name selector index, so a call to an imported def with
    // a by-name parameter is boxed in the importing unit too (D47).
    for (const auto& kv : table.byNameSelectors()) {
        if (preludeGlobals_.anyByNameMaskOf(kv.first) == kv.second) continue;
        ex.byNameSelectors.emplace_back(kv.first, kv.second);
    }
    return ex;
}

// Reads a global and FORCES it, so an `object` singleton is materialised: a
// module's top level runs when it is imported (A0-3, D90).
const proto::ProtoObject* Session::forceGlobal(proto::ProtoContext* ctx, const std::string& key) {
    const auto* sym = proto::ProtoString::createSymbol(ctx, key.c_str());
    const proto::ProtoObject* v = runtime_.layout().globals->getOwnAttributeDirect(ctx, sym);
    if (!v) return PROTO_NONE;
    // Forcing a module's singleton RUNS its top level, which can call a native
    // (`println` is the obvious one). This is reached from the compiler, not
    // from run(), so nothing has installed this thread's active call context and
    // the native would read a null layout.
    ExecutionEngine::ActiveCallGuard guard(&engine_, &runtime_.layout());
    return engine_.force(ctx, v);
}

const LoadedModule& Session::loadModuleFile(proto::ProtoContext* ctx, const std::string& absPath,
                                            const std::string& logicalPath) {
    if (const LoadedModule* hit = modulesByPath_.find(absPath)) return *hit;
    if (!modulesByPath_.claim(absPath, logicalPath)) {
        // Another thread is loading it. Park OUTSIDE the collector's view so a
        // waiting importer never holds up a collection (P6).
        proto::ProtoContext::UnmanagedScope unmanaged(ctx);
        if (const LoadedModule* m = modulesByPath_.awaitLoaded(absPath)) return *m;
        throw std::runtime_error("module failed to load: " + logicalPath);
    }
    // A failed load is NOT published, so a fixed file can be imported again in
    // the same session (protoST's LoadingEntryGuard).
    struct Guard {
        ModuleTable& t;
        const std::string& p;
        bool done = false;
        ~Guard() { if (!done) t.abandon(p); }
    } guard{modulesByPath_, absPath, false};

    std::string source;
    if (!readFile(absPath, &source))
        throw std::runtime_error("cannot read module file: " + absPath);

    const std::string objectName = moduleObjectName(logicalPath);
    std::unique_ptr<CompilationUnit> unit;
    CompiledUnit cu;
    try {
        unit = parseSource(source);
        desugarModule(*unit, objectName);
        GlobalTable table = preludeGlobals_;  // shares the key counters (A0-16)
        Compiler compiler(table);
        compiler.setModuleLoader(this);
        compiler.setSourceDir(std::filesystem::path(absPath).parent_path().string());
        cu = compiler.compileUnit(*unit, UnitMode::Script, 0);
        cu.module->linkSymbols(ctx);
        const BytecodeModule& mod = *cu.module;
        modules_.push_back(std::move(cu.module));  // the session keeps it alive
        engine_.run(ctx, mod);

        LoadedModule m;
        m.exports = collectExports(table, objectName);
        if (m.exports.moduleKey.empty())
            throw std::runtime_error("module " + logicalPath + " defines nothing to import");
        // Root it through the ONE pinned structure before it enters the map (P1):
        // the module object is the value of its own session global, and the
        // globals object is pinned in a root-context slot. Forcing it here is
        // what runs the module's top level (D90).
        m.object = forceGlobal(ctx, m.exports.moduleKey);
        modulesByPath_.publish(absPath, std::move(m));
    } catch (const ParseError& e) {
        throw std::runtime_error(absPath + ":" + std::to_string(e.pos.line) + ":" +
                                 std::to_string(e.pos.column) + ": " + e.what());
    } catch (const CompileError& e) {
        throw std::runtime_error(absPath + ":" + std::to_string(e.pos.line) + ":" +
                                 std::to_string(e.pos.column) + ": " + e.what());
    }
    guard.done = true;
    return *modulesByPath_.find(absPath);
}

const ModuleExports& Session::load(const std::string& providerSpec, const std::string& logicalPath,
                                   const std::string& importerDir, SourcePos pos) {
    if (!providerSpec.empty()) return loadForeign(providerSpec, logicalPath, pos);
    proto::ProtoContext ctx(&space_, runtime_.rootContext());
    const std::string abs = findModuleFile(logicalPath, importerDir);
    if (abs.empty())
        throw CompileError("ImportError: no module found for '" + logicalPath + "' (tried " +
                               triedPathsOf(logicalPath, importerDir) + ")",
                           pos);
    try {
        return loadModuleFile(&ctx, abs, logicalPath).exports;
    } catch (const ScalaThrow& t) {
        // The module's own top level threw. Report it as an import failure with
        // the thrown value's own toString, so a reader sees what actually failed.
        throw CompileError("ImportError: " + logicalPath + " failed to initialise: " +
                               showThrown(&ctx, t.value),
                           pos);
    } catch (const ScalaError& e) {
        throw CompileError(std::string("ImportError: ") + e.what(), pos);
    } catch (const std::runtime_error& e) {
        throw CompileError(std::string("ImportError: ") + e.what(), pos);
    }
}

const ModuleExports& Session::loadForeign(const std::string& providerSpec,
                                          const std::string& logicalPath, SourcePos pos) {
    // The provider is resolved FIRST, because a module's identity is provider +
    // path + version (protoCore 2.2.0, P3 D11) and `providerSpec` is an
    // alias-or-GUID spec, not the provider's stable identity: two specs can name
    // one provider, and an alias can be re-pointed. Resolving first is safe —
    // ProviderRegistry is a process singleton with no unregister, so a provider
    // that answered once still answers.
    proto::ModuleProvider* provider =
        proto::ProviderRegistry::instance().getProviderForSpec(providerSpec);
    if (!provider)
        throw CompileError("ImportError: no provider registered for '" + aliasOfSpec(providerSpec) +
                               "'. Install the runtime that provides it, or point "
                               "PROTOSCALA_PROVIDERS at its plug-in",
                           pos);

    // P3 D13: this Session's local cache is keyed by the KERNEL's identity, so
    // the two cannot disagree. The old key was `providerSpec + "/" + logicalPath`
    // — half the answer, and alias-dependent.
    const proto::ModuleIdentity id =
        proto::ModuleIdentity::unversioned(provider->getGUID(), logicalPath);
    const std::string cacheKey = id.asKey();
    if (auto it = foreignModules_.find(cacheKey); it != foreignModules_.end()) return it->second;

    proto::ProtoContext ctx(&space_, runtime_.rootContext());
    const proto::ProtoObject* mod = nullptr;
    try {
        // A provider is foreign code, possibly from a dlopen'd plug-in: a C++
        // exception escaping it into this frame would cross an ABI this binary
        // did not compile (A0-7).
        mod = translateForeignException([&] { return provider->tryLoad(logicalPath, &ctx); });
    } catch (const ScalaError& e) {
        throw CompileError(std::string("ImportError: ") + e.what(), pos);
    }
    if (!mod || mod == PROTO_NONE)
        throw CompileError("ImportError: provider '" + aliasOfSpec(providerSpec) +
                               "' has no module '" + logicalPath + "'",
                           pos);

    // P3 D13: publish through the kernel so the module is in the process-global
    // module list under the ruled identity (provider GUID + path + version) and
    // is rooted in THIS space — the space whose lifetime this importer controls.
    //
    // Before P3 this path called provider->tryLoad directly and reached neither
    // SharedModuleCache nor any moduleRoots, so the module's only anchor was
    // inside the PROVIDING runtime (protoST's liveRegistry root set and its
    // globals). A host that destroyed that runtime while this Session still held
    // the module's values dropped the only anchor; the tests survived only
    // because construction order happened to make the runtime destroyed last,
    // and nothing enforced that.
    //
    // registerModule is publish-or-adopt: if this identity is already served, it
    // returns the module already published, so two importers of one identity
    // share one module.
    mod = space_.registerModule(id, mod);
    if (!mod || mod == PROTO_NONE)
        throw CompileError("ImportError: provider '" + aliasOfSpec(providerSpec) +
                               "' module '" + logicalPath + "' could not be published",
                           pos);

    ModuleExports ex;
    ex.foreign = true;
    ex.moduleKind = BindingKind::Val;
    ex.moduleName = moduleObjectName(logicalPath);
    // `$` cannot occur in a protoScala identifier, so this key is outside every
    // Scala namespace, exactly as `@` and `#` already are.
    ex.moduleKey = "__module" + std::to_string(++moduleCounter_) + "$" + ex.moduleName;
    // The value goes into the pinned globals object; nothing else holds it (P1).
    runtime_.layout().globals->setAttribute(
        &ctx, proto::ProtoString::createSymbol(&ctx, ex.moduleKey.c_str()), mod);
    return foreignModules_.emplace(cacheKey, std::move(ex)).first->second;
}

std::string Session::bindForeignMember(const ModuleExports& mod, const std::string& name,
                                       SourcePos pos) {
    proto::ProtoContext ctx(&space_, runtime_.rootContext());
    const auto* modKey = proto::ProtoString::createSymbol(&ctx, mod.moduleKey.c_str());
    const proto::ProtoObject* obj =
        runtime_.layout().globals->getOwnAttributeDirect(&ctx, modKey);
    const auto* memberKey = proto::ProtoString::createSymbol(&ctx, name.c_str());
    // PROTO_NONE is both Scala `null` and "attribute missing": probe with
    // hasAttribute, never by comparing the value, or a foreign member that
    // legitimately holds null would be reported as absent.
    if (!obj || obj->hasAttribute(&ctx, memberKey) != PROTO_TRUE)
        throw CompileError("ImportError: " + mod.moduleName + " has no member named '" + name + "'",
                           pos);
    const proto::ProtoObject* v = nullptr;
    try {
        v = translateForeignException(
            [&]() -> const proto::ProtoObject* { return obj->getAttribute(&ctx, memberKey); });
    } catch (const ScalaError& e) {
        throw CompileError(std::string("ImportError: ") + e.what(), pos);
    }
    const std::string key = mod.moduleKey + "$" + name;
    runtime_.layout().globals->setAttribute(
        &ctx, proto::ProtoString::createSymbol(&ctx, key.c_str()), v ? v : PROTO_NONE);
    return key;
}

} // namespace protoScala
