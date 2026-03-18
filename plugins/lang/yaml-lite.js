// Plugin language pack: yaml-lite

ted.registerLanguage("yaml", {
  extensions: ".yaml,.yml",
  keywords: "true false null yes no on off",
  types: "",
  singleComments: "#",
  multiCommentPairs: "",
  stringDelims: "\"'",
  identifierExtras: "-",
  numberMode: "strict",
  escapeChar: "\\",
  multiLineStrings: true,
  onConflict: "override"
});
