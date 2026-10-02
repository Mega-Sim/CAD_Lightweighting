# Milestone 5 — Multi-method Search and Black-box Cost Selection

Milestone 5 introduces competing search strategies over representation candidates.

## Search contract

A valid plan is an exact cover of Source Truth: every source entity must be represented exactly once and no candidate may silently merge away customer-visible selection units.

## C++ baseline search

- deterministic greedy exact-cover construction
- bounded exhaustive search with overlap pruning
- beam search fallback for larger non-raw candidate sets
- deterministic tie-breaking
- explicit separation of estimated bytes and exact DWG bytes
- exact DWG bytes always outrank estimated cost when selecting the final verified winner

## Python research search

- simulated annealing with deterministic seed
- genetic search with overlap repair and deterministic seed
- append-only JSONL experiment records
- QUBO exporter for optional quantum-inspired solvers

Quantum-inspired/QUBO is treated as one competitor. It is not assumed to outperform classical search.

## Cost hierarchy

1. `estimated_bytes`: cheap candidate-selection proxy only.
2. `exact_dwg_bytes`: actual serialized DWG filesystem bytes measured by M6.
3. A plan can win only if it passes geometry, semantic, interaction, trace, and backend validity checks.

## Verification status

Regression sources cover overlap rejection, exact coverage, deterministic search, raw fallback, and exact-byte precedence. Per project workflow, tests are not executed until a PR is requested.
