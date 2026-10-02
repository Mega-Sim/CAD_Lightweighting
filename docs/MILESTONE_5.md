# Milestone 5 — Multi-method Search and Black-box Cost Selection

Milestone 5 introduces competing search strategies over representation candidates.

## Search contract

A valid plan is an exact cover of Source Truth: every source entity must be represented exactly once, declared candidate conflicts must not coexist, and no candidate may silently merge away customer-visible selection units.

Candidate transformation order is explicit in each M4 `transform_chain`. Different orderings are represented as distinct candidates, making transform order part of the search space rather than an implicit unordered set.

## C++ baseline search

- deterministic greedy exact-cover construction
- bounded exhaustive search with overlap and declared-conflict pruning
- beam search fallback for larger non-raw candidate sets
- safe all-raw fallback if greedy completion would become structurally invalid
- deterministic tie-breaking
- explicit separation of estimated bytes and exact DWG bytes
- exact DWG bytes always outrank estimated cost when selecting the final verified winner

The search does not assume that a candidate conflicting with a raw fallback is globally invalid: exhaustive/beam paths can still select another non-raw candidate that covers the conflicted source and produce a valid exact cover.

## Python research search

- bounded exhaustive search when the non-raw search space is tractable
- beam search with partial-state proxy cost
- simulated annealing with deterministic seed and byte-scale geometric temperature schedule
- genetic search with partial-state conflict/overlap repair and deterministic seed
- explicit declared-conflict handling matching C++ candidate JSON
- append-only JSONL experiment records
- QUBO exporter for optional quantum-inspired solvers

Python distinguishes a **valid partial state** from a **completed exact cover**. This prevents premature pruning when a partial candidate conflicts with a raw fallback that a later non-raw candidate will replace.

## QUBO safety

The default exact-cover penalty is greater than the sum of all positive estimated candidate costs, so violating a hard coverage constraint cannot become artificially attractive merely because proxy candidate cost is low. User-supplied unsafe penalties are rejected.

Quantum-inspired/QUBO is treated as one competitor. It is not assumed to outperform classical search.

## Cost hierarchy

1. `estimated_bytes`: cheap candidate-selection proxy only.
2. `exact_dwg_bytes`: actual serialized DWG filesystem bytes measured by M6.
3. A plan can win only if it passes geometry, semantic, interaction, trace, and backend validity checks.

## Verification status

C++ regression sources cover overlap rejection, declared-conflict pruning, safe raw fallback, valid non-raw replacement of a conflicting raw fallback, deterministic exhaustive search, and exact-byte precedence.

Dependency-free Python unittest sources cover bounded exhaustive, beam, deterministic SA/GA, partial-state replacement of conflicting raw fallback, exact-cover validity, QUBO penalty safety, and unsafe penalty rejection.

Per project workflow, tests are not executed until a PR is requested.
