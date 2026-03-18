// Plugin: trim trailing spaces in current buffer

ted.registerCommand("trimtrail", `
  const lines = ted.getText().split("\\n");
  for (let i = 0; i < lines.length; i++) {
    lines[i] = lines[i].replace(/[ \t]+$/g, "");
  }
  ted.setText(lines.join("\\n"));
  ted.message("trimtrail: done");
`);
