# TED Autoresearch Workflows

## improve

Use for normal overnight self-optimization.

- Goal: raise the mechanical metric
- Bias: visible product wins
- Output: one keep/discard recommendation

## debug

Use when there is a fresh regression or user-reported breakage.

- Goal: remove one reproduced failure
- Bias: root cause over workaround
- Output: concise bug summary with evidence

## fix

Use when build, smoke, or other mechanical checks are red.

- Goal: restore a healthy baseline
- Bias: smallest safe repair
- Output: restored verify/guard status

## security

Use when auditing plugin loading, LLM integration, or extension boundaries.

- Goal: find one real security issue or hardening gap
- Bias: code evidence, not theory
- Output: severity, location, mitigation

## ship

Use when the loop should package work for human review or release.

- Goal: make readiness explicit
- Bias: checklist discipline
- Output: readiness summary and blockers
