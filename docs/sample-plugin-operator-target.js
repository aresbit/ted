// Place this file under ~/.ted/plugins/operator-word-end.js
// Example: register "e" as an operator target.
// Usage in normal mode: de / ye / ce / 2de

ted.registerOperatorTarget("e", `
  const text = ted.getText().split("\\n");
  const row = Math.max(0, Math.min(__ted_row, text.length - 1));
  const line = text[row] || "";
  let i = Math.max(0, Math.min(__ted_col, line.length));
  const count = Math.max(1, __ted_count | 0);
  for (let step = 0; step < count; step++) {
    while (i < line.length && /[^A-Za-z0-9_]/.test(line[i])) i++;
    while (i < line.length && /[A-Za-z0-9_]/.test(line[i])) i++;
  }
  "range:" + __ted_col + ":" + i;
`);
