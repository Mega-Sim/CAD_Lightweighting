# Milestone 1 — Zero-optimization round-trip foundation

## Goal

Establish a safety baseline before any compression/optimization algorithm is allowed to modify geometry.

## Acceptance gates

- ASCII DXF source can be parsed without discarding unknown group codes.
- PreserveLexical DXF round trip is byte-identical.
- Model-space ENTITIES retain one-to-one source selection units in the semantic index.
- Entity type/layer/content changes are rejected by the verifier.
- Every source entity has a trace ID.
- Missing production DWG backend fails closed; no fake DWG is emitted.
- When a production DWG backend is configured, output DWG must be converted back and independently verified before PASS.
- CMake build and CTest must pass.

## Not claimed in Milestone 1

- Full semantic comparison of every possible DXF/DWG custom/proxy object.
- Autodesk/ODA SDK redistribution.
- File-size optimization.
- Lossy transform implementation.
- Performance/lag optimization.

Those are intentionally gated behind this baseline.
