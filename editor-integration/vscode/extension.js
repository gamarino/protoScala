// protoScala for VS Code: the debug adapter glue.
//
// The protoscala executable itself IS the debug adapter: `protoscala --dap`
// speaks the Debug Adapter Protocol over stdin/stdout. This extension
// registers the `protoscala` debug type, tells VS Code to start that adapter,
// and lets F5 on an open .scala file debug it without a launch.json.
//
// protoscala is taken from the `protoscala.path` setting if set, else from the
// default install location (when present), else from PATH. A launch with
// `remote.command` runs `<command...> <remote.path or protoscala> --dap`
// instead (ssh, wsl, docker exec ...); pathMappings go to the adapter.
const vscode = require("vscode");
const fs = require("fs");
const path = require("path");
const os = require("os");

function defaultInstallPath() {
  if (process.platform === "win32") {
    const base = process.env.LOCALAPPDATA || path.join(os.homedir(), "AppData", "Local");
    return path.join(base, "Programs", "proto", "bin", "protoscala.exe");
  }
  return path.join(os.homedir(), ".local", "bin", "protoscala");
}

function configuredPath() {
  return vscode.workspace.getConfiguration("protoscala").get("path");
}

// What is wrong with the `protoscala.path` setting, or null: it names a file
// (not a bare command looked up in PATH) that does not exist. Checked before
// the launch, so the user reads which setting is wrong instead of a spawn
// error.
function configuredPathProblem() {
  const configured = configuredPath();
  if (!configured || !/[\\/]/.test(configured) || fs.existsSync(configured)) return null;
  return `protoScala: the protoscala.path setting is "${configured}", which does not exist. ` +
    "Fix it, or clear it to use the default install location or protoscala from PATH.";
}

// Shows a message and aborts the launch, without waiting for the user to
// close the message: a launch that waited would stay pending for as long as
// the notification is not dismissed.
function refuse(kind, message) {
  vscode.window[kind](message);
  return undefined;
}

function protoscalaCommand() {
  const configured = configuredPath();
  if (configured) return configured;
  const installed = defaultInstallPath();
  return fs.existsSync(installed) ? installed : "protoscala";
}

const configurationProvider = {
  resolveDebugConfiguration(folder, config) {
    // F5 with no launch.json (or an empty configuration): debug the active file.
    if (!config.type && !config.request && !config.name) {
      const editor = vscode.window.activeTextEditor;
      if (editor && editor.document.languageId === "scala") {
        config.type = "protoscala";
        config.name = "Debug current protoScala script";
        config.request = "launch";
        config.program = "${file}";
      }
    }
    if (!config.program) {
      return refuse("showInformationMessage", "protoScala: open a .scala file or set 'program' in launch.json.");
    }
    if (config.remote) {
      const cmd = config.remote.command;
      if (!Array.isArray(cmd) || cmd.length === 0 || !cmd.every((a) => typeof a === "string")) {
        return refuse("showErrorMessage",
                      "protoScala: 'remote.command' must be a non-empty list of strings, " +
                      'e.g. ["ssh", "-T", "-o", "BatchMode=yes", "user@host"].');
      }
      if (!Array.isArray(config.pathMappings) || config.pathMappings.length === 0) {
        return refuse("showErrorMessage",
                      "protoScala: a 'remote' launch needs 'pathMappings': " +
                      '[{"localRoot": "${workspaceFolder}", "remoteRoot": "<the same ' +
                      'folder on the remote machine>"}], so that breakpoints and frames ' +
                      "can be translated between the two machines.");
      }
    } else {
      const problem = configuredPathProblem();
      if (problem) return refuse("showErrorMessage", problem);
    }
    return config;
  },
};

function activate(context) {
  const factory = {
    createDebugAdapterDescriptor(session) {
      // `remote`: the adapter runs on another machine (ssh), in WSL or in a
      // container, behind any command that relays stdin/stdout. Paths are
      // translated by the adapter itself (pathMappings), not here.
      const remote = session.configuration.remote;
      if (remote && Array.isArray(remote.command) && remote.command.length > 0) {
        const exe = remote.path || "protoscala";
        return new vscode.DebugAdapterExecutable(remote.command[0],
                                                 [...remote.command.slice(1), exe, "--dap"]);
      }
      return new vscode.DebugAdapterExecutable(protoscalaCommand(), ["--dap"]);
    },
  };
  context.subscriptions.push(
    vscode.debug.registerDebugAdapterDescriptorFactory("protoscala", factory),
    vscode.debug.registerDebugConfigurationProvider("protoscala", configurationProvider)
  );
}

function deactivate() {}

module.exports = { activate, deactivate };
