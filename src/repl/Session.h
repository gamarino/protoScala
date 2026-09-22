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
#include "runtime/ExecutionEngine.h"
#include "runtime/Runtime.h"
#include "protoCore.h"

#include <memory>
#include <string>
#include <vector>

namespace protoScala {

enum class EvalStatus { Ok, Incomplete, Error };

struct EvalOutcome {
    EvalStatus status = EvalStatus::Ok;
    std::vector<std::string> echo;  // REPL lines to print on stdout
};

class Session {
public:
    Session();
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    // Runs a script file; returns the process exit code (0 or 1).
    int runScript(const std::string& path, const std::vector<std::string>& args);
    // Evaluates one REPL input. Incomplete: the input ended inside a construct.
    EvalOutcome evalReplInput(const std::string& source);
    // :load — runs a file in this session (its definitions stay visible).
    bool loadFile(const std::string& path);
    // --disassemble: prints the compiled bytecode of a file; exit code.
    int disassemble(const std::string& path);

private:
    proto::ProtoSpace space_;  // first member: destroyed last
    Runtime runtime_;
    ExecutionEngine engine_;
    GlobalTable globals_;
    std::vector<std::unique_ptr<BytecodeModule>> modules_;
    int resultCounter_ = 0;

    // Parses, compiles and runs one unit. Reports errors on stderr.
    EvalStatus evaluate(const std::string& source, const std::string& sourceName,
                        UnitMode mode, const std::vector<std::string>* mainArgs,
                        EvalOutcome* outcome);
    void callMain(proto::ProtoContext* ctx, const std::string& name, bool takesArgs,
                  const std::vector<std::string>& args);
};

} // namespace protoScala
