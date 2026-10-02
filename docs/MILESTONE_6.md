# Milestone 6 — Exact DWG Evaluation and End-to-End Selection

Milestone 6 closes the research loop by making the real serialized DWG file size the final objective.

## Backend boundary

The repository does not bundle proprietary Autodesk RealDWG or ODA SDK binaries. The optimizer talks to a `DwgBackend` interface and currently uses explicit external command templates as the host boundary. Profiles identify intended backend families (`external`, `oda-file-converter`, `oda-sdk`, `realdwg-host`) and report capabilities/licensing expectations, but command syntax remains supplied by the installed backend.

Every command template must contain both `{input}` and `{output}`. Missing, failed, stale, empty, or unmeasurable outputs fail closed.

## Exact evaluator

`evaluate_candidate_plans()` performs, for every evaluated plan:

1. validate exact source coverage / selection cardinality;
2. materialize a final source-semantic DXF;
3. convert it to DWG through the configured backend;
4. measure the actual DWG filesystem byte size;
5. independently convert that DWG back to DXF;
6. parse the reverse DXF;
7. run geometry + semantic + interaction verification against immutable Source Truth;
8. reject any failed candidate;
9. select the smallest exact byte count among valid candidates.

Intermediate `estimated_bytes` never overrides actual `exact_dwg_bytes`.

## Current conservative materializer

The first materializer is intentionally `StrictSourceMaterializer`. It reconstructs the original source entity units exactly before serialization. This proves the safety/evaluation pipeline but means structural candidate estimates (reference/grid/grammar/tensor probes) do not yet automatically produce a smaller serialized DXF representation.

This limitation is deliberate. Later materializers may rewrite representation details only when they can reconstruct the same customer-visible CAD selection/edit semantics and pass the independent verifier.

Therefore **M6 source implementation is not a claim that the 42 MB reference drawing has already been reduced.** Actual reduction measurements require:

- the reference drawing in DXF input form;
- an installed/licensed DWG backend;
- a materializer that converts a proven candidate recipe into a source-equivalent but smaller native CAD representation;
- PR-time build/test and real round-trip verification.

## CLI research mode

`--research` performs canonical probing, candidate discovery, deterministic C++ search, exact DWG evaluation, independent reverse verification, and winning DWG selection.

Example shape (backend-specific command syntax is intentionally not invented here):

```bash
./build/cadopt \
  --input drawing.dxf \
  --output build/optimized.dwg \
  --research \
  --backend-profile external \
  --dxf-to-dwg 'YOUR_ENCODER {input} {output}' \
  --dwg-to-dxf 'YOUR_DECODER {input} {output}' \
  --max-plans 16 \
  --beam-width 64 \
  --work-dir build/research \
  --report build/research_report.json
```

The process returns no winner when the backend is unavailable, reverse conversion fails, or the reconstructed document fails independent verification.

## Traceability

M6 preserves the M2 trace model. Canonicalization drift, selected representation candidate, verification failure category, and exact output size are retained in the machine-readable report so later lossy materializers can be traced back to the algorithm and parameters that caused a difference.

## Verification status

Regression source is included with a fake backend to exercise a valid round trip and a deliberately corrupted reverse conversion. Per project workflow these tests are not executed until a PR is requested.
