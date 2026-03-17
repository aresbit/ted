# TED Autoresearch Module

This directory turns TED's local autoresearch loop into a repository-owned
self-optimization module instead of a loose external skill bundle.

## What lives here

- `protocol.md`
  - The core TED-specific self-optimization contract.
- `workflows.md`
  - Named loop modes the editor can reuse: improve, debug, fix, security, ship.

## Why it exists

`AutoConvexOptimization-skill/` contains useful prompt engineering and workflow
structure, but TED should not depend on a nested standalone repo just to improve
itself. This module keeps the useful parts in-tree and lets
`scripts/autoresearch-loop.sh` consume a single local protocol source.

## Integration points

- `scripts/autoresearch-module.sh`
  - Emits protocol text for the loop prompt or prints a summary for humans.
- `scripts/autoresearch-loop.sh`
  - Reads the module prompt block before launching the agent.
- `make autoresearch-module`
  - Quick way to inspect the active self-optimization contract.
