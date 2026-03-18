// Plugin: sketch helper commands

ted.registerCommand("sketchauto", `
  ted.sketchMode("auto");
  ted.message("Sketch mode: auto");
`);

ted.registerCommand("sketchinfo", `
  const s = ted.sketchStatus();
  ted.message(s);
`);

ted.registerCommand("sketchshapes", `
  const shapes = ted.sketchShapes();
  ted.message("shapes=" + shapes);
`);
