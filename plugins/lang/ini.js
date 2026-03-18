// Plugin language pack: ini

ted.registerLanguage("ini", {
  extensions: ".ini,.cfg,.conf",
  keywords: "true false on off yes no",
  types: "",
  singleComments: ";#",
  multiCommentPairs: "",
  stringDelims: "\"'",
  identifierExtras: "-.",
  numberMode: "strict",
  escapeChar: "\\",
  multiLineStrings: false,
  onConflict: "override"
});
