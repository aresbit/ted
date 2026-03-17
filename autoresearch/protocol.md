# TED Self-Optimization Protocol

This protocol adapts the external autoresearch skill into a TED-native module.

## Core contract

1. Work inside the TED repository only.
2. Make one focused change per iteration.
3. Measure improvement mechanically with `sh scripts/autoresearch-metric.sh`.
4. Protect the editor with `make smoke`.
5. Prefer shipping concrete product capability over adding planning text.
6. Treat git history and `.autoresearch/results.tsv` as the loop's memory.

## Scope priorities

Prioritize work in this order unless the live focus helper says otherwise:

1. Editor-visible improvements
2. Sketch correctness and usability
3. Runtime/plugin extensibility
4. Loop automation and self-driving behavior
5. Documentation only when it unlocks one of the above

## Workflow modes

- `improve`
  - Default mode. Raise the metric with one product-facing change.
- `debug`
  - Investigate a concrete regression with evidence and a small repair.
- `fix`
  - Clear one failing build/test/guard issue without broad refactors.
- `security`
  - Audit attack surfaces, but still obey the same verify + guard contract.
- `ship`
  - Prepare a clean handoff by verifying readiness and logging the result.

## Iteration shape

1. Review repo state, recent results, and current focus.
2. Pick one small change that fits the active mode.
3. Implement it.
4. Run verify and guard.
5. Summarize baseline, result, keep/discard recommendation, and next likely move.

## Anti-patterns

- Do not make unrelated multi-file experiments in one iteration.
- Do not keep changes that fail the guard.
- Do not claim UX improvements without touching the visible editor behavior.
- Do not depend on the external skill directory for runtime decisions.
