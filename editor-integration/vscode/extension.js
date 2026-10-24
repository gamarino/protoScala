// protoScala for VS Code: the debug adapter glue.
//
// The protoscala executable itself IS the debug adapter: `protoscala --dap`
// speaks the Debug Adapter Protocol over stdin/stdout. This extension
// registers the `protoscala` debug type, tells VS Code to start that adapter,
// and lets F5 on an open .scala file debug it without a launch.json.
//
// protoscala is taken from the `protoscala.path` setting if set, else from the
// default install location (when present), else from PATH.
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

function protoscalaCommand() {
  const configured = vscode.workspace.getConfiguration("protoscala").get("path");
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
      return vscode.window
        .showInformationMessage("protoScala: open a .scala file or set 'program' in launch.json.")
        .then(() => undefined);
    }
    return config;
  },
};

function activate(context) {
  const factory = {
    createDebugAdapterDescriptor(/* session */) {
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
