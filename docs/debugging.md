# Debugging protoScala scripts (VS Code and the Debug Adapter Protocol)

`protoscala --dap` is a debug adapter: it speaks the
[Debug Adapter Protocol](https://microsoft.github.io/debug-adapter-protocol/)
(DAP) on its standard input and output and runs the script a client names, in
the same process, under the interpreter. Any DAP client can drive it; the
extension in [`editor-integration/vscode/`](../editor-integration/vscode/)
connects it to VS Code and adds Scala syntax highlighting.

Only **interpreted** code is debugged: what `protoscala file.scala` runs. A
module transpiled by `protoscalac` (and anything run with `--run-module`) is
native code with no line table at run time; the debugger does not see its
frames, and a breakpoint in its source never stops.

## Install

1. Build and install protoScala ([INSTALLATION.md](INSTALLATION.md)). The
   extension looks for the executable in this order:
   1. the `protoscala.path` setting, if set;
   2. the default install location, if it exists:
      `%LOCALAPPDATA%\Programs\proto\bin\protoscala.exe` on Windows,
      `~/.local/bin/protoscala` elsewhere;
   3. `protoscala` on `PATH`.
2. Package and install the extension (Node.js is needed for `npx`):

   ```bash
   cd editor-integration/vscode
   npx @vscode/vsce@3.2.1 package --skip-license
   code --install-extension protoscala-0.1.0.vsix
   ```

   Then reload VS Code (**Developer: Reload Window**). On Linux and macOS a
   symbolic link works too, and picks up edits after a reload:
   `ln -s "$PWD/editor-integration/vscode" ~/.vscode/extensions/protoscala-0.1.0`.

VS Code has no built-in Scala support, so the extension contributes the
`scala` language (`.scala`, `.sc`, and files whose first line is a
`#!... protoscala` line), a TextMate grammar and a language configuration
(comments, brackets, auto-closing, indentation, folding by indentation). If the
Scala (Metals) extension is installed it claims the same language id; both
work, and VS Code uses one of the two grammars.

## Start a session

- Open a `.scala` file, click in the gutter to set breakpoints, press **F5**.
  Without a `launch.json` the extension debugs the active file.
- Or use a launch configuration. [`examples/.vscode/launch.json`](../examples/.vscode/launch.json)
  has ready-made ones (open the `examples/` folder); **Add Configuration... →
  protoScala** writes one:

  ```json
  {
    "type": "protoscala",
    "request": "launch",
    "name": "Debug current protoScala script",
    "program": "${file}",
    "args": [],
    "stopOnEntry": false
  }
  ```

| Attribute     | Meaning |
|---------------|---------|
| `program`     | The script to run (required). |
| `args`        | Arguments for the script's `@main` method. |
| `cwd`         | Working directory of the script (imports are found relative to the importing file, as always). |
| `stopOnEntry` | Stop at the first line that runs. |

**Ctrl+F5** (Run Without Debugging) runs the script through the adapter with
the debugger detached.

## What works

- **Line breakpoints**, in the script and in any `.scala` module it imports.
  A breakpoint on a line with no code (a blank line, a comment) moves to the
  next line that has code once the file is compiled;
  until then VS Code shows it unverified.
- **Continue, Step Over, Step Into, Step Out, Pause.** Steps are by source
  line. Step Over runs calls made on the current line to completion; Step Out
  and a Step Over off the last line of a function stop in the caller at the
  call site (for `n * fact(n - 1)`, back on that line before the
  multiplication). A loop whose body is on one line stops once per iteration.
- **Call stack** with function names (`fact`, `<lambda>`, `Point.<init>`, the
  script's `<top>`), files and lines. Frames of the prelude (`List.map` running
  a lambda) are not shown.
- **Variables**: a frame's **Locals** — parameters, `val`s and `var`s in
  scope, captured variables, `this` in a method — and **Globals**: the
  top-level `val`s, `var`s and objects of the script being run (a `lazy val` not
  forced yet is left out: forcing it would run code). Values are shown as the
  REPL shows them: their `toString`, Strings in quotes.
- **Evaluate** (Watch, Debug Console, hover) in the selected frame: any
  expression over that frame's locals and the script's globals and functions,
  for example `n * 10`, `xs.filter(_ > 1)`, `fact(n + 1)`.
- **Output**: what the script prints to standard output and standard error
  appears in the Debug Console as it is printed. Standard input is at end of
  file under the debugger (`StdIn.readLine()` answers `null`): the adapter's
  own standard input carries the protocol.

## Limitations

- Transpiled modules are not debugged (see the top of this page). Neither is
  the REPL.
- No conditional breakpoints, hit counts, logpoints, function breakpoints or
  exception breakpoints. An uncaught exception ends the script and is reported
  in the Debug Console, as on the command line.
- Variables are flat: an object shows its `toString` and does not expand into
  fields. Use Evaluate (`p.x`) to look inside.
- Evaluate runs the expression as code: a `toString`, a call or a `println` in
  it runs for real (breakpoints do not stop inside it). It cannot assign to the
  frame's locals (they are passed by value), does not see `this` implicitly (a
  method's fields are not names in scope; `this` itself is listed in Locals),
  and does not see names bound by the script's `import`s.
- `stopOnEntry` stops at the first line that **runs**. An `import` loads and
  runs its module while the script is compiled, so with imports that first line
  is in the first imported module.
- One thread is presented. An actor's handler can stop at a breakpoint; while
  one thread is stopped, another that reaches a breakpoint waits until the first
  resumes, and steps follow the thread that was stopped.
- Stop (or closing the session) ends the script: the adapter lets it run to its
  end for half a second, then ends the process.
- Attached, the interpreter checks every instruction for a new line, which
  slows interpreted code down. Detached (`protoscala file.scala`, or Ctrl+F5),
  the cost is one branch per call.

## How it works

- `src/runtime/Debugger.{h,cpp}` is the interpreter's hook. While a debugger is
  attached, every interpreted frame (`ExecutionEngine::runLoop`) keeps a record
  on a per-thread chain; the first instruction of each new source line is
  checked against the breakpoints (an array of flags per source file) and the
  stepping state (a mode, a thread and a frame depth).
- The compiler records the source name of every frame slot
  (`BytecodeModule::slotName`); the session registers each module it compiles
  from a file under that file's path.
- `src/dap/DapServer.cpp` is the adapter. The protocol loop runs on the main
  thread, which is not a protoCore thread. The script runs on the evaluator
  thread, in a `Session` created after `configurationDone`. A stopped thread
  waits in an unmanaged region (a collection never waits for it) and runs the
  requests that need the language — rendering values, evaluating — itself, on
  its own context, so a value is only ever read by the thread whose stopped
  frames hold it.
- `src/dap/DapStdio.cpp` gives the protocol private copies of the standard
  descriptors, points the script's stdout and stderr at pipes forwarded as
  `output` events (never split inside a UTF-8 sequence), and its stdin at the
  null device. Without it the first line a script printed would corrupt the
  protocol stream.

The tests are `ctest -R cli/dap` (`tests/cli/dap_session_check.py`, a Python
DAP client with a watchdog on every wait; on Windows it passes paths with a
lower-case drive letter, as VS Code does).
