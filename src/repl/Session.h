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
};

} // namespace protoScala
