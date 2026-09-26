/*
 * Session — one protoScala evaluation session: a ProtoSpace, its Runtime,
 * the globals, the engine and every compiled module (retained for the whole
 * session because function objects point into them; protoClojure
 * src/repl/Repl.cpp retainedModules). Shared by the script runner and the
 * REPL. Construct it on the evaluator thread (StackGuard.h). Phase 1 starts
 * no threads; a later phase that does must join them in ~Session before the
 * ProtoSpace member is destroyed.
 */
#pragma once
#include "compiler/BytecodeModule.h"
#include "compiler/Compiler.h"
#include "compiler/GlobalTable.h"
#include "compiler/ModuleLoader.h"
#include "repl/ModuleTable.h"
#include "runtime/ExecutionEngine.h"
#include "umd/ScalaModuleProvider.h"
#include "runtime/Runtime.h"
#include "protoCore.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace protoScala {

enum class EvalStatus { Ok, Incomplete, Error };

struct EvalOutcome {
    EvalStatus status = EvalStatus::Ok;
    std::vector<std::string> echo;  // REPL lines to print on stdout
};

// Session is the ModuleLoader the Compiler consults (Phase 6 A0-1) and the
// ModuleHost ScalaModuleProvider resolves through (A0-5), because it is what owns
// the ProtoSpace, the engine and the globals.
class Session : public ModuleLoader, public ModuleHost {
public:
    Session();
    ~Session() override;
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    // Runs a script file; returns the process exit code (0 or 1).
    int runScript(const std::string& path, const std::vector<std::string>& args);
    // Evaluates one REPL input. Incomplete: the input ended inside a
    // construct (only possible when forceComplete is false). With
    // forceComplete, an input that would otherwise be Incomplete is reported
    // as a parse error instead (the REPL uses this to force evaluation on a
    // blank continuation line).
    EvalOutcome evalReplInput(const std::string& source, bool forceComplete = false);
    // True when `source` ends inside a construct (an unclosed bracket, a
    // missing body, ...): parsing fails at end of input. Parses only.
    bool needsMoreInput(const std::string& source) const;
    // :load — runs a file in this session (its definitions stay visible).
    bool loadFile(const std::string& path);
    // --disassemble: prints the compiled bytecode of a file; exit code. Because
    // an import is resolved by loading (D90), this RUNS the top level of every
    // module the file imports (D95).
    int disassemble(const std::string& path);
    // --run-module: loads a COMPILED module (.so) and runs it as a program --
    // proto_module_init, then proto_module_main when it exports one (Phase 7
    // D8). Returns the process exit code. Without proto_module_main it returns 0
    // and prints nothing: a module has no output of its own.
    int runModule(const std::string& soPath, const std::vector<std::string>& args);

    // --- ModuleLoader (compiler side; no ProtoObject* crosses it) ----------
    const ModuleExports& load(const std::string& providerSpec, const std::string& logicalPath,
                              const std::string& importerDir, SourcePos pos) override;
    std::string bindForeignMember(const ModuleExports& mod, const std::string& name,
                                  SourcePos pos) override;

    // --- ModuleHost (provider side) ---------------------------------------
    std::string findModuleFile(const std::string& logicalPath,
                               const std::string& importerDir) override;
    std::string triedPathsOf(const std::string& logicalPath,
                             const std::string& importerDir) override;
    const LoadedModule& loadModuleFile(proto::ProtoContext* ctx, const std::string& absPath,
                                       const std::string& logicalPath) override;

    // The provider plug-ins this session loaded, in load order (--version).
    const std::vector<std::string>& providerPlugins() const { return pluginPaths_; }
    // The space this session owns, for the UMD tests: the provider resolves its
    // host through it, so a test has to be able to name it.
    proto::ProtoSpace& space() { return space_; }
    // The table a unit is compiled against, after the prelude. `protoscalac` is
    // the second consumer of a Session (Phase 7 D7): it needs the same table the
    // interpreter would compile against, so that an import resolves identically
    // and a transpiled unit binds the same globals.
    GlobalTable& globals() { return globals_; }
    // The prelude-only copy a MODULE is compiled against, which shares the key
    // counters (A0-16). A transpiled module must use this one for the same reason
    // an interpreted one does: it sees the prelude and nothing of any importer.
    GlobalTable& moduleGlobals() { return preludeGlobals_; }
    // The loader a unit's imports resolve through. D90 resolves an import by
    // LOADING, which is why protoscalac owns a Session at all.
    ModuleLoader& moduleLoader() { return *this; }

private:
    proto::ProtoSpace space_;  // first member: destroyed last
    Runtime runtime_;
    ExecutionEngine engine_;
    GlobalTable globals_;
    std::vector<std::unique_ptr<BytecodeModule>> modules_;
    int resultCounter_ = 0;

    // --- Phase 6: modules -------------------------------------------------
    // The table a module is compiled against: the session's, right after the
    // prelude. The module sees the prelude (it needs List, Option, println) and
    // nothing of the importer, and the COPY shares the key counters, so a
    // module's `trim` becomes `trim` or `trim#1` and can never collide with a
    // session global of the same name (A0-16, D25's machinery).
    GlobalTable preludeGlobals_;
    ModuleTable modulesByPath_;
    std::map<std::string, ModuleExports> foreignModules_;  // spec/path -> exports
    int moduleCounter_ = 0;               // fresh keys for foreign module globals
    std::vector<std::string> pluginPaths_;
    // a.b.C -> the files findModuleFile will try, in order.
    std::vector<std::string> candidatePathsOf(const std::string& logicalPath,
                                              const std::string& importerDir) const;
    // A prefixed import. It calls the named provider's tryLoad DIRECTLY and does
    // NOT go through ProtoSpace::getImportModule: protoCore's SharedModuleCache
    // is keyed by logical path with no ProtoSpace component
    // (core/ModuleCache.cpp), so in a two-runtime process a cached module could
    // be handed to whichever runtime asked second (A0-4).
    const ModuleExports& loadForeign(const std::string& providerSpec,
                                     const std::string& logicalPath, SourcePos pos);
    ModuleExports collectExports(const GlobalTable& table, const std::string& objectName) const;
    const proto::ProtoObject* forceGlobal(proto::ProtoContext* ctx, const std::string& key);

    // Parses, compiles and runs one unit. Reports errors on stderr.
    // allowIncomplete: a parse error at end-of-input in Repl mode is
    // reported as EvalStatus::Incomplete instead of an error (the REPL asks
    // for more input); runScript and loadFile always pass false.
    EvalStatus evaluate(const std::string& source, const std::string& sourceName,
                        UnitMode mode, const std::vector<std::string>* mainArgs,
                        EvalOutcome* outcome, bool allowIncomplete);
    void callMain(proto::ProtoContext* ctx, const std::string& mainKey, bool takesArgs,
                  const std::vector<std::string>& args);
    // A value as the REPL echoes it: Scala's toString (through the engine, so
    // a user-defined toString runs), with a String in quotes (the Scala 3
    // REPL shows `val res0: String = "hi"`).
    std::string showResult(proto::ProtoContext* ctx, const proto::ProtoObject* v);
    // An uncaught exception value as its own toString renders it, so a
    // user-defined class reports what its class says (DESIGN §7). A toString
    // that itself fails is reported rather than allowed to mask the original.
    std::string showThrown(proto::ProtoContext* ctx, const proto::ProtoObject* v);
};

} // namespace protoScala
