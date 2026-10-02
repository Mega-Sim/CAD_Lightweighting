# Milestone 4 — Representation Discovery and Candidate Engine

Milestone 4 changes the optimizer from a single-method compressor into a representation search system.

## Scope model

No scope is privileged. Candidate discovery may operate on the whole drawing, regions, layers, networks, components, objects, sub-objects, or repeated patterns. A candidate always records exactly which original source entities it covers.

## Implemented candidate families

- raw source fallback
- exact reference + transform
- near reference + residual
- repeated transform family / grid probe
- repeated sequence / grammar probe
- whole-drawing structural composite estimate
- research-side tensor, wavelet, and spectral probes

## Canonical signatures

Supported geometry is transformed into a temporary canonical frame before hashing so translation and XY rotation do not prevent equal shapes from matching. Physical scale is preserved by default. Unsupported/incomplete geometry and TEXT are deliberately isolated to prevent unsafe false deduplication.

## Cost policy

`estimated_bytes` and `residual_estimated_bytes` are only cheap search proxies. They are never reported as final DWG savings. M6 must serialize a reconstructed candidate through a DWG backend and measure real filesystem bytes.

## Customer semantics

Every representation candidate has `preserves_selection_cardinality=true` as a hard requirement. Internal reference/grid/grammar representations must expand back into the original customer-visible entity units before final DWG serialization.

## Safety

`validate_candidate_set()` rejects duplicate/unknown source IDs, duplicate candidate IDs, and candidates that declare loss of selection cardinality. Geometry/semantic/interaction validation remains a later independent gate.

## Verification status

Regression test sources are included for translation/rotation-invariant matching, reference candidate discovery, near-repeat residual candidates, and invalid coverage. Per project workflow, tests are not executed until a PR is requested.
