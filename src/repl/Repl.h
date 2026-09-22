// protoScala interactive REPL (readline): see the Phase 1 plan, Task 11.
#pragma once

namespace protoScala {

// Runs the REPL on the calling thread (call it on the evaluator thread).
// Returns the process exit code.
int runRepl();

} // namespace protoScala
