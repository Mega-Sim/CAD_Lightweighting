# Milestone 3 — Reversible Canonical Transform Core

Milestone 3 adds the first optimization-space transformation layer while keeping Source Truth immutable.

## Implemented design

- `Vec3` and row-major affine `Mat4` primitives.
- Translation, X/Y/Z rotation, uniform scaling, and signed axis permutation.
- General inverse for the supported affine subset.
- `ReversibleTransform` stores forward/inverse matrices plus a trace-friendly description.
- Geometry views are extracted from DXF entities without changing source records.
- Baseline geometry extraction covers LINE, ARC, CIRCLE, LWPOLYLINE, and TEXT.
- Unsupported or incomplete entities remain explicit (`supported=false` / `complete=false`) and are never silently accepted as geometry-equivalent.
- Canonicalization recenters geometry, aligns a dominant first edge in the XY plane, and can optionally normalize uniform scale.
- `max_roundtrip_drift()` measures forward/inverse reconstruction drift before later optimization stages may use the transformed view.

## Important semantic rule

Canonical views are temporary optimization representations only. They are not emitted directly as the customer DWG. The final document must reconstruct the source entity units and selection/edit behavior before serialization.

## Test source added

`tests/test_geometry.cpp` covers reversible affine transforms, axis permutation, angle wrapping, canonical line alignment, inverse drift, and non-destructive fixture extraction.

Per project workflow these tests are added now but are not executed until a PR is requested.
