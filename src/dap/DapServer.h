#pragma once

namespace protoScala {

// Runs the Debug Adapter Protocol server over the process's stdin/stdout
// (`protoscala --dap`, docs/debugging.md). VS Code starts it as a subprocess;
// the script named by `launch` runs in this process under the interpreter's
// debugging hook. Returns the process exit code.
int runDapServer();

} // namespace protoScala
