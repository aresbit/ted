#!/usr/bin/env sh
set -eu

ROOT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
cd "$ROOT_DIR"

RESULTS_DIR=".autoresearch"
RESULTS_FILE="$RESULTS_DIR/results.tsv"

if [ ! -f "$RESULTS_FILE" ]; then
  cat <<'EOF'
Autoresearch history:
Recent trend: no iterations recorded yet
EOF
  exit 0
fi

awk -F '\t' '
  NR == 1 { next }
  {
    rows[count++] = $0
    metric = $4 + 0
    if (count == 1) first_metric = metric
    last_metric = metric
    if ($5 == "keep") keeps++
    else if ($5 == "discard") discards++
    else if ($5 == "observe") observes++
  }
  END {
    if (count == 0) {
      print "Autoresearch history:"
      print "Recent trend: no iterations recorded yet"
      exit
    }

    start = count - 3
    if (start < 0) start = 0
    print "Autoresearch history:"
    printf "Recent trend: keeps=%d discards=%d observes=%d net=%+d\n",
      keeps, discards, observes, (last_metric - first_metric)
    printf "Last window:"
    for (i = start; i < count; i++) {
      split(rows[i], f, "\t")
      printf " [%s %s→%s]", f[5], f[3], f[4]
    }
    printf "\n"
  }
' "$RESULTS_FILE"
