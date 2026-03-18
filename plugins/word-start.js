// Plugin: operator target "B" (word start)
// Usage: dB / yB / cB / 3dB

ted.registerOperatorTarget("B", `
  const text = ted.getText().split("\\n");
  const row = Math.max(0, Math.min(__ted_row, text.length - 1));
  const line = text[row] || "";
  let i = Math.max(0, Math.min(__ted_col, line.length));
  const count = Math.max(1, __ted_count | 0);

  for (let step = 0; step < count; step++) {
    while (i > 0 && /[^A-Za-z0-9_]/.test(line[i - 1])) i--;
    while (i > 0 && /[A-Za-z0-9_]/.test(line[i - 1])) i--;
  }

  "range:" + i + ":" + __ted_col;
`);
