// Plugin language pack: dockerfile

ted.registerLanguage("dockerfile", {
  extensions: "Dockerfile,.dockerfile",
  keywords: "FROM RUN CMD COPY ADD WORKDIR EXPOSE ENV ARG ENTRYPOINT USER LABEL VOLUME STOPSIGNAL",
  types: "",
  singleComments: "#",
  multiCommentPairs: "",
  stringDelims: "\"'",
  identifierExtras: "-_./",
  numberMode: "strict",
  escapeChar: "\\",
  multiLineStrings: false,
  onConflict: "override"
});
