// Place this file under ~/.ted/plugins/lang/toml.js to autoload it at startup.
// It registers TOML syntax metadata from JS while highlight execution stays in C.

ted.registerLanguage("toml", {
  extensions: ".toml",
  keywords: "true false",
  types: "",
  singleComments: "#",
  multiCommentPairs: "",
  stringDelims: "\"'",
  identifierExtras: "-",
  numberMode: "strict",
  escapeChar: "\\",
  multiLineStrings: false,
  onConflict: "override"
});

ted.registerCommand("langinfo", `
  ted.message("Syntax plugin loaded: TOML");
`);
