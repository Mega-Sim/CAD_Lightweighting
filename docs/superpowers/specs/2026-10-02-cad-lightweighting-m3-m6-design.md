# CAD Lightweighting M3-M6 Design

## Product goal

Input is always DXF. Output is DWG. The customer should experience the output as the same CAD document in practical use while the actual final DWG byte size is minimized.

## Binding constraints

- Core languages: C++20 and Python 3.
- Immutable Source Truth is never mutated.
- Intermediate representation is unrestricted: translation, XYZ rotation, coordinate frame changes, temporary grouping, compression, tensor/wavelet/spectral/grammar/latent representations, and lossy approximation are allowed internally.
- Final output must restore customer-visible CAD behavior, not only visual appearance.
- If source entities are individually selectable/editable, final output must retain equivalent selection/edit semantics. Temporary grouping/block-like representations are allowed only inside the optimizer.
- Geometry, semantic, and interaction verification must independently gate every accepted candidate.
- Absolute coordinate values are low priority during optimization; relative physical geometry and final reconstruction are authoritative.
- Optimization runtime is secondary during the research phase. Broad search is preferred over premature speed optimization.
- Final objective is actual bytes of a successfully written DWG, never an intermediate compression estimate.
- Every transformation and every verification failure must be traceable back to source entities and algorithm parameters.
- UI remains the existing minimal file picker; no UI feature work is part of M3-M6.

## M3: Reversible canonical transform layer

Introduce a representation-independent geometry view with explicit reversible transforms. Supported entities produce canonical geometric data without modifying source DXF records. Translation, XYZ rotation, axis permutation, and uniform scale normalization are represented as invertible matrices/metadata. Every forward transform has an explicit inverse, and reconstruction drift is measured before a candidate can proceed.

## M4: Representation discovery and candidate engine

Analyze the drawing at global, structural, and local scopes without privileging any one scope. Discover exact and near repeated geometry using transform-invariant canonical signatures. Represent candidates as recipes, never as destructive edits to Source Truth. Candidate families include raw, reference+transform, reference+residual, symmetry/grid, repeated sequence/grammar, primitive reduction hooks, and research hooks for tensor/wavelet/spectral methods.

Candidate cost has two levels: a cheap estimated description length for pruning, and later an exact DWG byte cost. Every candidate carries provenance, source coverage, dependencies/conflicts, reconstruction recipe, estimated cost, and expected loss risk.

## M5: Multi-method search

Search over candidate selection, algorithm parameters, and composition order. Implement deterministic baseline search first, then Python research searchers (beam/exhaustive where tractable, simulated annealing, genetic search). Export QUBO-compatible binary models without requiring a proprietary or cloud quantum solver. Search results are reproducible through deterministic seeds and persisted experiment records.

A plan is eligible for exact evaluation only when coverage/conflict constraints are satisfied. Cheap cost is only a proxy. The exact winner is selected from candidates that pass independent verification after DWG serialization.

## M6: DWG backend and end-to-end selection

Provide production backend abstractions with capability metadata. Do not bundle proprietary SDK binaries. Support explicit native/SDK command adapters and keep the generic external command adapter as a fail-closed fallback. A backend must be able to serialize DXF/CAD data to DWG and convert DWG back to DXF for independent verification before a result can be accepted.

The end-to-end runner reconstructs source-equivalent CAD entities, writes candidate DWGs, measures actual file bytes, converts them back to DXF, performs geometry/semantic/interaction verification, rejects invalid candidates, and chooses the smallest valid output.

## Licensing boundary

The repository does not redistribute proprietary ODA or Autodesk SDK binaries. ODA File Converter may be used for research/non-commercial evaluation under its terms; commercial product integration requires a properly licensed backend. RealDWG/ODA SDK integration remains behind the backend interface so licensing does not contaminate optimizer logic.

## Success criteria

A run succeeds only if:

1. output DWG exists and is non-empty;
2. independent DWG->DXF verification succeeds;
3. geometry verification passes configured strict tolerances;
4. semantic verification passes;
5. interaction/selection semantics pass;
6. loss trace contains no unresolved fatal issue;
7. among all valid evaluated candidates, the returned DWG has the smallest measured byte size.
