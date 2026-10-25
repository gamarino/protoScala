# protoScala — VS Code extension

A minimal VS Code extension for protoScala scripts:

- **Scala language support** — VS Code ships none, so this extension
  contributes the `scala` language (`.scala`, `.sc`): a TextMate grammar
  (keywords and Scala 3 soft keywords, string interpolators `s""` / `f""` /
  `raw""` with `$x` and `${...}`, triple-quoted strings, characters, numbers,
  annotations, types after `:` and in `[...]`, names of `def` / `val` / `var` /
  `class` / `object` / `trait` / `enum` / `given`), and a language
  configuration: `//` and `/* */` comments, brackets, auto-closing,
  indentation, folding by indentation.
- **Debugging** of scripts run by the protoScala interpreter — breakpoints,
  stepping, the call stack, Locals and Globals, evaluation in a frame, the
  script's output in the Debug Console, exception breakpoints (*All
  Exceptions*, *Uncaught Exceptions*) and threads (`main`, `Thread-N`, the
  actor workers: only the thread that stops is stopped, the others keep
  running), conditional / hit-count breakpoints and logpoints, and
  conditions on the exception filters (the exception is `e`), the prelude's
  embedded source, and remote debugging (`remote` + `pathMappings`). The debug
  adapter is the `protoscala` executable itself, started as
  `protoscala --dap`; the exception filters come from it, so the BREAKPOINTS
  view lists them without any setting here.

Plain JavaScript; there is no build step and no bundler.

## Requirements

`protoscala` installed or built ([docs/INSTALLATION.md](../../docs/INSTALLATION.md)).
The extension looks for it in this order:

1. the `protoscala.path` setting (Settings → Extensions → protoScala), if set;
2. the default install location — `%LOCALAPPDATA%\Programs\proto\bin\protoscala.exe`
   on Windows, `~/.local/bin/protoscala` elsewhere — if it exists;
3. `protoscala` on `PATH`.

## Install

```bash
cd editor-integration/vscode
npx @vscode/vsce@3.2.1 package --skip-license
code --install-extension protoscala-0.4.0.vsix
```

On Linux and macOS a symbolic link works too (edits are picked up after
**Developer: Reload Window**):

```bash
ln -s "$(pwd)/editor-integration/vscode" ~/.vscode/extensions/protoscala-0.4.0
```

If the Scala (Metals) extension is installed it also claims the `scala`
language; the debugger works the same with either grammar.

## Use

1. Open a `.scala` file and click in the gutter to set breakpoints.
2. Press **F5**. Without a `launch.json` the active file is debugged;
   `examples/.vscode/launch.json` in the repository has ready-made
   configurations, and **Add Configuration… → protoScala** writes one.

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

| Attribute     | Type     | Meaning |
|---------------|----------|---------|
| `program`     | string   | The script to debug (required). |
| `args`        | string[] | Arguments for the script's `@main`. |
| `cwd`         | string   | Working directory of the script. |
| `stopOnEntry` | boolean  | Stop at the first line that runs. |
| `justMyCode`  | boolean  | Steps never stop in the prelude. |
| `pathMappings`| object[] | `[{localRoot, remoteRoot}]`: this machine's path and the adapter's. |
| `remote`      | object   | `{command: ["ssh", "-T", "-o", "BatchMode=yes", "u@h"], path: "/remote/protoscala"}`: run the adapter behind that command. Needs `pathMappings`. |

**Remote**: with VS Code's Remote - SSH / WSL / Dev Containers, install this
`.vsix` and protoScala on the remote side (the extension runs where the
workspace is). Or keep VS Code local and use the "protoScala: Launch over SSH"
/ "Launch in WSL" snippets (`remote` + `pathMappings`); the remote shell must
print nothing on a non-interactive start. Details: [docs/debugging.md](../../docs/debugging.md),
"Remote debugging"; JetBrains IDEs: the same page.

Only interpreted code is debugged; modules transpiled by `protoscalac` are
not. See [docs/debugging.md](../../docs/debugging.md) for what works and the
limitations.
