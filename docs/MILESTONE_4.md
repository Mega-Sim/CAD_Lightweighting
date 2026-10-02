# Milestone 4 — Representation Discovery and Candidate Engine

Milestone 4 changes the optimizer from a single-method compressor into a representation search system.

## Scope model

No scope is privileged. Candidate discovery may operate on the whole drawing, regions, layers, networks, components, objects, sub-objects, or repeated patterns. A candidate always records exactly which original source entities it covers.

## Implemented candidate families

- raw source fallback
- bounded whole-drawing numeric quantization probes
- exact reference + transform
- near reference + residual
- reflection-symmetry family probes
- repeated transform family / grid probe
- repeated sequence / grammar probe
- LWPOLYLINE primitive-fit + residual hook
- whole-drawing structural composite estimate
- research-side tensor, wavelet, and spectral probes

## Ordered transform recipes

Every candidate carries an ordered `transform_chain`. Examples include:

- `canonicalize → reference_transform → restore_entity_units`
- `canonicalize → reflection_symmetry → placement_stream → restore_entity_units`
- `canonicalize → primitive_fit → capture_residual → restore_entity_units`
- `numeric_quantization`

The order is part of the candidate representation. Different orderings must be represented as distinct candidates so M5 can compare them instead of treating a set of transforms as unordered.

## Canonical signatures

Supported geometry is transformed into a temporary canonical frame before hashing so translation and XY rotation do not prevent equal shapes from matching. Physical scale is preserved by default. Reflection-family signatures additionally compare a canonical Y-reflected representation. Unsupported/incomplete geometry and TEXT are deliberately isolated to prevent unsafe false deduplication.

## Primitive reduction hook

The first primitive-reduction hook targets supported LWPOLYLINE geometry with at least three points. It is intentionally analysis-only in the generic materializer: the candidate records a primitive fit plus residual reconstruction recipe but M6 will not serialize it until a backend-specific materializer can prove the same final polyline selection/edit unit and pass independent verification.

## First materializable lossy family

`NumericQuantization` is the first candidate family that can reach the generic M6 DXF materializer without changing entity count/type/layer/block/group structure. The candidate stores its quantization step explicitly and is marked potentially lossy.

The first implementation only quantizes coordinate/distance-like geometry fields for supported baseline entities. Angle/bulge/dimensionless fields remain exact so different error units are not mixed into one loss source. M6 independently round-trips the resulting DWG and rejects the candidate when geometry tolerance is exceeded.

## Cost policy

`estimated_bytes` and `residual_estimated_bytes` are only cheap search proxies. They are never reported as final DWG savings. M6 must serialize a reconstructed candidate through a DWG backend and measure real filesystem bytes.

## Customer semantics

Every representation candidate has `preserves_selection_cardinality=true` as a hard requirement. Internal reference/grid/grammar/symmetry/primitive representations must expand back into the original customer-visible entity units before final DWG serialization.

The generic materializer currently refuses structural/tensor/wavelet/spectral/primitive candidates that cannot yet be encoded into a smaller standard CAD representation without changing selection/edit semantics. A licensed native backend/materializer can later implement those recipes while keeping the same verifier contract.

## Safety

`validate_candidate_set()` rejects duplicate/unknown source IDs, duplicate candidate IDs, missing ordered transform chains, invalid conflict references, self-conflicts, and candidates that declare loss of selection cardinality. Candidate JSON includes `conflicts` and `transform_chain` so the Python research search layer sees the same constraint graph as C++.

Geometry/semantic/interaction validation remains an independent M6 gate after actual DWG serialization.

## Verification status

Regression test sources cover translation/rotation-invariant matching, reference candidate discovery, near-repeat residual candidates, reflection symmetry, primitive-reduction hook generation, ordered transform-chain serialization, numeric quantization candidate generation, and invalid coverage. Per project workflow, tests are not executed until a PR is requested.
