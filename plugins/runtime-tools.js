// Plugin: runtime convenience commands

ted.registerCommand("pluginscan", `
  ted.message("Use :plugins to reload ~/.ted/plugins");
`);

ted.registerCommand("edstate", `
  const text = ted.getText();
  const lines = text.split("\\n").length;
  ted.message("buffer lines=" + lines);
`);
