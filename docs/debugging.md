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
   code --install-extension protoscala-0.4.1.vsix
   ```

   Then reload VS Code (**Developer: Reload Window**). On Linux and macOS a
   symbolic link works too, and picks up edits after a reload:
   `ln -s "$PWD/editor-integration/vscode" ~/.vscode/extensions/protoscala-0.4.1`.

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
| `justMyCode`  | Steps never stop in the prelude (breakpoints there still do). Default `false`. |
| `pathMappings`| `[{localRoot, remoteRoot}]`: translate paths between the client's machine and the adapter's ("Remote debugging"). |
| `remote`      | Extension only: `{command: [...], path}` runs the adapter behind that command ("Remote debugging"). |

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
  script's `<top>`), files and lines, prelude frames included (shown dimmed,
  with the prelude's embedded source: see "Sources without a file").
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
- **Exception breakpoints** (the BREAKPOINTS view, or `setExceptionBreakpoints`):
  - **All Exceptions** (filter `raised`, off by default): stop wherever an
    exception is thrown, caught or not.
  - **Uncaught Exceptions** (filter `userUnhandled`, on by default): stop
    where an exception is thrown that no `try`/`catch` of the program will
    catch.

  The thread stops at the line that threw, before anything unwinds: the call
  stack, Locals and Evaluate show the state at the `throw`. The stop says the
  exception's class and message (`IllegalStateException: too big: 6`), and the
  exception widget shows them with the stack (`exceptionInfo`). An error the
  runtime raises (`10 / 0`, a `null` dereference, a failed cast) stops at the
  line whose operation failed, marked "raised by the runtime on this line";
  one raised inside the prelude (`None.get`, `List().head`) stops at the line
  of the program that called it, marked "raised in `get`, called from this
  line". One exception stops once: a `catch` that does not match and
  re-raises, a `finally` that re-raises after its cleanup, or `throw e` of a
  caught `e` does not stop again.
- **Conditional breakpoints, hit counts and logpoints** (right-click a
  breakpoint → Edit Condition / Hit Count / Log Message):
  - **Condition**: a Scala expression evaluated in the frame at the
    breakpoint's line, over its locals and the script's globals, as Evaluate
    does (`i == 3`, `xs.length > 2 && name.startsWith("a")`). The thread stops
    when it is `true`. A condition that does not compile, throws, or is not a
    Boolean **stops** the thread anyway, and the stop (and the Debug Console)
    says `breakpoint condition `...` failed: <error>`: a broken condition is
    never skipped silently.
  - **Hit count**: `N` or `== N` (stop on the Nth hit only), `>= N`, `> N`,
    `< N`, `<= N`, or `% N` (every Nth hit). Hits are counted per breakpoint,
    and only the hits whose condition held count. Anything else is answered
    as an unverified breakpoint with a message. Setting a file's breakpoints
    again (VS Code does it on every edit) starts the counts again.
  - **Log message**: printed to the Debug Console instead of stopping, with
    each `{expr}` replaced by the expression's value as a String (its
    `toString`, `null` for null) evaluated in the frame; `{{` and `}}` are
    literal braces. An expression that fails prints `<error: message>`. A
    logpoint with a condition or a hit count logs only when they pass.
  - On several threads, each evaluates the condition in its own frame (a
    condition on `tag` stops only the thread whose `tag` matches).
- **Conditions on exception filters** (the pencil next to *All Exceptions*
  / *Uncaught Exceptions*): a Scala expression with the exception bound as
  **`e`**, e.g. `e.isInstanceOf[IllegalStateException]` or
  `e.getMessage.contains("timeout")`. It is evaluated in the throwing frame
  (its locals and the globals are in scope too; `e` hides a local named `e`).
  The filter stops only when it is `true`; one that fails stops with the
  failure in the stop's description, as above. A native error the runtime
  raises (`10 / 0`) is bound as the Scala exception it becomes
  (`ArithmeticException`).
- While a condition, a log message, Watch, hover or the Debug Console
  evaluates, that thread never stops: a breakpoint in a function the
  condition calls, or an exception the condition throws, does not re-enter
  the debugger.
- **Threads.** Every thread that runs interpreted code is listed: `main` (the
  script), `Thread-1`, `Thread-2`... (`Thread.start` bodies, in start order)
  and `actor worker 1`, `actor worker 2`... (the actor pool's workers; while a
  worker runs an actor's handler or a `Future`'s body its name says which:
  `actor worker 2 (actor#3 at chat.scala:12)`, `(future#4 at main.scala:7)` --
  the number counts spawns, the place is where `Actor.spawn` or `Future`
  was called). VS Code gets `thread` events as they start and end.

  When a thread reaches a breakpoint, a step's end, an exception stop or a
  pause, **only that thread stops**; the others keep running. Several threads
  can be stopped at once, each with its own call stack, Locals and Evaluate.
  Continue, Step Over/Into/Out and Pause act on the thread they name (the one
  selected in the CALL STACK view); a step in one thread never stops another
  one. Breakpoints apply to every thread. Pause on a thread that is waiting in
  native code (`t.join()`, an `await` that blocks, I/O) takes effect when it
  is back in interpreted code.

## Sources without a file: the prelude, Loaded Scripts

The prelude (`lib/prelude.scala`: `Option`, `List`'s Scala-written methods,
`require`, `Try`, the I/O classes...) is installed only inside the executable,
as a precompiled image plus its source text. Under the debugger its frames
show that text: VS Code opens it read-only as `prelude.scala` (the adapter
serves it through the DAP `source` request, by `sourceReference`), dimmed as
library code. Step Into enters prelude methods, breakpoints set in that
read-only editor stop, and its frames are in the call stack. Its frames have
no Locals names beyond the parameters (the image carries no slot names).

- `"justMyCode": true` keeps steps out of the prelude (Step Into on
  `require(...)` goes to the next line of the program). Breakpoints in the
  prelude still stop.
- Exception stops always show the program's frame that called into the
  prelude ("raised in `require`, called from this line"), and stopOnEntry
  stops at the script's first line, not in the prelude's initialisation.
- **Loaded Scripts** (the Run and Debug view's LOADED SCRIPTS section, DAP
  `loadedSources` and `loadedSource` events) lists the prelude, the script
  and every module it imported, as they load.
- Code compiled for Watch, the Debug Console and conditions is never shown:
  nothing stops inside it.

## Remote debugging

The script runs where `protoscala --dap` runs. Two ways to put it on another
machine (a Linux server, WSL, a container):

1. **VS Code's own remoting (Remote - SSH, WSL, Dev Containers).** Install
   protoScala and the extension's `.vsix` on the remote side and open the
   folder there. The extension is a workspace extension
   (`"extensionKind": ["workspace"]`), so it runs where the files are, and
   nothing else is needed: paths are the remote machine's on both ends.
2. **The adapter behind a command** (`remote` + `pathMappings`): VS Code and
   your files stay local; the extension starts
   `<remote.command...> <remote.path> --dap`, and the adapter translates every
   path itself, so this works with any command that relays stdin/stdout:

   ```json
   {
     "type": "protoscala", "request": "launch", "name": "Debug over SSH",
     "program": "${file}",
     "remote": { "command": ["ssh", "-T", "-o", "BatchMode=yes", "me@build-host"],
                 "path": "/home/me/.local/bin/protoscala" },
     "pathMappings": [ { "localRoot": "${workspaceFolder}",
                         "remoteRoot": "/home/me/src/project" } ]
   }
   ```

   In WSL: `"command": ["wsl", "-d", "Ubuntu-24.04", "--"]` and a
   `remoteRoot` that is a Linux clone (or `/mnt/c/...` for the Windows folder
   itself). In a container: `["docker", "exec", "-i", "my-container"]`.
   **The remote shell must print nothing** when it starts non-interactively:
   a `motd`, an `echo` in `.bashrc`, or a password prompt would land in the
   protocol stream and VS Code would drop the session. `ssh -T -o
   BatchMode=yes` (no terminal, no prompts; use a key) avoids the last two.
   The same files must exist on both sides (a clone, a synced folder).

`pathMappings` (also usable without `remote`, by any DAP client) is a list of
`{localRoot, remoteRoot}`: local is the client's machine, remote the
adapter's. The script's `program` and `cwd`, breakpoint paths and `source`
requests are translated local -> remote; frames, Loaded Scripts and the
exception stack local <- remote. The longest matching root wins; `\` and `/`
are converted; a root that looks like a Windows path (`c:\...`) matches
regardless of case. A file under no mapping (a module found through
`PROTOSCALA_PATH` elsewhere on the remote machine) is shown read-only by
`sourceReference`, with its remote path as the origin, and takes breakpoints
like the prelude. An entry without both roots fails the launch.

## JetBrains IDEs (IntelliJ IDEA, PyCharm, CLion, ...)

*This section was written from the LSP4IJ plugin's documentation and the
adapter's protocol behaviour; it has not been clicked through in a JetBrains
IDE, and menu names may differ between versions.*

JetBrains IDEs speak DAP through the [LSP4IJ](https://github.com/redhat-developer/lsp4ij)
plugin (Settings -> Plugins -> Marketplace -> "LSP4IJ"):

1. **Run -> Edit Configurations... -> + -> Debug Adapter Protocol.**
2. **Server** tab: the command `protoscala --dap` (the full path to the
   executable if it is not on `PATH`; for a remote machine, the same command
   line as `remote` above: `ssh -T -o BatchMode=yes me@host /home/me/.local/bin/protoscala --dap`).
3. **Mappings** tab: map the `*.scala` files (or the Scala language) to this
   server, so the IDE lets you set breakpoints in them and sends them.
4. **Configuration** tab: the working directory and file, and the launch
   JSON, with the same attributes as VS Code's `launch.json`:
   `{"type": "protoscala", "request": "launch", "program": "${file}",
   "cwd": "${workspaceFolder}", "pathMappings": [...]}` (`stopOnEntry`,
   `args`, `justMyCode` too). Everything about paths and sources is in the
   adapter, so pathMappings, the prelude's source, exception filters,
   conditions and threads behave as in VS Code.

Syntax: an IDE with a Scala plugin already highlights `.scala`. Without one,
import the extension's TextMate grammar: **Settings -> Editor -> TextMate
Bundles -> +**, and choose the `editor-integration/vscode` folder of this
repository (it reads `package.json`'s grammar contribution).

## Limitations

- Transpiled modules are not debugged (see the top of this page). Neither is
  the REPL.
- No function or data breakpoints.
- A condition, a hit condition's count or a log message is evaluated by
  compiling it as a function of the frame's locals; the compiled function is
  cached per expression and set of local names, so a condition in a loop is
  compiled once, but every evaluation is a call (a conditional breakpoint in a
  hot loop slows it down far more than a plain one). A syntax error is
  reported when the breakpoint is set and again where it stops.
- After an uncaught-exception stop, Continue lets the exception end the script
  (or the thread, or fail the actor's message) and be reported in the Debug
  Console, as on the command line.
- "Uncaught" is decided at the `throw`, from the handler tables of the
  thread's interpreted frames and what each `catch` selects: `case e: T`
  patterns are matched against the exception's class (with `Throwable` and
  `Exception` selecting foreign values as a `catch` does); a guard, an
  extractor (`case NonFatal(e)`), or a pattern on a type the compiler cannot
  name counts as **caught**, because only running it would tell. The search
  stops at native code that catches:
  - **counts as caught** (no stop): `Try { ... }`, a `Future`'s body, and the
    functions given to a future's `map`, `flatMap` and `recover` -- the
    exception becomes a `Failure` the program may still look at;
  - **counts as uncaught** (stop): the top of the script, of a `Thread.start`
    body and of an actor's handler, and a future's `onComplete` callback: each
    reports the exception and drops it;
  - native code that only passes exceptions through (`List.map` calling a
    lambda, a call into another runtime) is transparent. A native that might
    catch and is not in the lists above makes the filter say "uncaught" where
    it is caught.
- Exception stops ignore what the runtime throws for its own control flow:
  the cooperative suspension of an actor's `await` is not an exception and
  never stops; an exception the prelude raises and catches itself before any
  frame of the program sees it never stops either (none does today). An
  exception thrown while the debugger evaluates (Watch, hover, `toString`)
  never stops.
- Recognising "the same exception" is by its value, per thread: a value that
  is thrown, caught and later thrown **again on purpose** by the same thread,
  while that thread still runs, stops only the first time. An `await` that
  re-raises a future's failure on the waiting thread is a new raise there.
- A failed `await` resumed in an actor (the value injected at the `await`
  after a suspension) is not seen by the filters; its original throw, on the
  thread that ran the future, is.
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
- A thread is listed once it has run interpreted code; one that has not yet
  (a worker that has not taken an actor's message) is not. Only a stopped
  thread has a call stack to show: a running one, or one blocked in native
  code (`join`, a blocking `await`, I/O), is listed without frames until it
  stops.
- An actor worker is a thread, not an actor: stepping off the end of an
  actor's handler continues on that worker, in whatever message it runs next
  (possibly another actor's).
- Evaluate runs on the stopped thread. An expression that waits for another
  thread (`f.await` on a future whose actor is stopped, `t.join()` of a
  stopped thread) blocks that request until the other thread is resumed.
- Pausing every thread at once is not offered (VS Code's Pause acts on the
  selected thread); a `pause` with no thread id stops the first thread that
  reaches a new line.
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
  (`BytecodeModule::slotName`) and, for each `catch`, what its cases select
  (`BytecodeModule::CatchFilter`: everything, a list of class markers, or
  "unknown"); the session registers each module it compiles from a file under
  that file's path.
- Stepping state (mode and frame depth), pause requests and the last reported
  exception are per thread. Each thread is registered with an id and a name
  the first time it enters an interpreted frame while attached; its exit is
  reported from the thread's own end.
- Exceptions: `ExecutionEngine::runLoop` already catches every C++ exception
  that leaves an instruction (a Scala `throw`, a native's `ScalaError`,
  protoCore's errors from the error factory, the translated `std::` errors)
  to attach the source line. Right after that catch clause, while the frame
  and its callers are still intact, the first frame an exception passes
  through calls `ExecutionEngine::debugRaised`, which decides from the
  filters, the frames' handler tables and `debug::NativeBoundary` marks
  (placed in `Try`, the future callbacks, the `Future` body runner) whether
  to stop. `FutureYield` (an actor's `await` suspending) has its own clause
  and is never reported. Detached, or with no exception filter, the cost is
  one test of a local flag on the exception path only.
- `src/dap/DapServer.cpp` is the adapter. The protocol loop runs on the main
  thread, which is not a protoCore thread. The script runs on the evaluator
  thread, in a `Session` created after `configurationDone`. Each stopped
  thread waits in an unmanaged region (a collection never waits for it, so
  the threads that keep running keep allocating and collecting) and runs the
  requests that need the language — rendering values, evaluating — itself, on
  its own context, from its own queue, so a value is only ever read by the
  thread whose stopped frames hold it. Frame ids come from one counter for the
  session, so they are unique across threads.
- `src/dap/DapStdio.cpp` gives the protocol private copies of the standard
  descriptors, points the script's stdout and stderr at pipes forwarded as
  `output` events (never split inside a UTF-8 sequence), and its stdin at the
  null device. Without it the first line a script printed would corrupt the
  protocol stream.

The tests are `ctest -R cli/dap` (`tests/cli/dap_session_check.py`, a Python
DAP client with a watchdog on every wait; on Windows it passes paths with a
lower-case drive letter, as VS Code does): `cli/dap-session` and
`cli/dap-output` for the basics, `cli/dap-exceptions` for the filters
(a throw with its locals, a re-raise that does not stop twice, a runtime
error, internal control flow, filters off, uncaught on the main thread, in a
thread body and in an actor's handler but not in a Future's body), and
`cli/dap-threads` for multi-thread stops (two threads at one breakpoint while
a third prints, a step in one while the other stays stopped, pause of one
busy thread, several collections while a thread is stopped).
