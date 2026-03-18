// Plugin: runtime clock command

ted.registerCommand("now", `
  const d = new Date();
  ted.message("now: " + d.toISOString());
`);
