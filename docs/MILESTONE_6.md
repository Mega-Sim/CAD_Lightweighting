# Milestone 6 — Exact DWG Evaluation and End-to-End Selection

Milestone 6 closes the research loop by making the real serialized DWG file size the final objective.

## Backend boundary

The repository does not bundle proprietary Autodesk RealDWG or ODA SDK binaries. `DwgBackend` has two host paths:

1. `ExternalCommandDwgBackend` for explicit DXF→DWG / DWG→DXF command templates.
2. `NativeLibraryDwgBackend` for a separately built/licensed ODA Drawings SDK or RealDWG host loaded at runtime through the stable `cadopt/native_backend_api.h` C ABI.

Backend profiles identify intended families (`external`, `oda-file-converter`, `oda-sdk`, `realdwg-host`) and expose capability/licensing metadata. Missing commands, missing plugin symbols, ABI mismatch, failed conversion, stale/missing output, empty output, or unmeasurable output all fail closed.

The native ABI version is currently `1` and requires both conversion directions so independent round-trip verification cannot be bypassed.

## Exact evaluator

`evaluate_candidate_plans()` performs, for every evaluated plan:

1. validate exact source coverage / selection cardinality;
2. materialize a final source-semantic DXF according to the selected plan;
3. convert it to DWG through the configured backend;
4. measure the actual DWG filesystem byte size;
5. independently convert that DWG back to DXF;
6. parse the reverse DXF;
7. run geometry + semantic + interaction verification against immutable Source Truth;
8. reject any failed candidate;
9. select the smallest exact byte count among valid candidates.

Intermediate `estimated_bytes` never overrides actual `exact_dwg_bytes`.

## Materialization policy

Three materializer classes exist:

- `StrictSourceMaterializer`: byte-preserving source-semantic baseline.
- `NumericQuantizationMaterializer`: isolated global numeric quantization experiment helper.
- `PlanAwareMaterializer`: production research path; reads each `CandidatePlan` and materializes only candidate kinds it can safely express in generic DXF.

`PlanAwareMaterializer` currently supports:

- `Raw`: exact lexical/source reconstruction.
- `NumericQuantization`: coordinate/distance-like fields are rounded to the candidate step while entity count/type/layer/block/group ownership stays unchanged.

It deliberately rejects reference/grid/grammar/tensor/wavelet/spectral recipes when using the generic DXF materializer because emitting those directly could change customer-visible CAD selection/edit semantics. Those recipes remain valid search/research representations and may be implemented by a native licensed backend-specific materializer later.

This means M6 now has a real path where candidate values can change serialized DWG bytes, but **it still does not claim a particular compression percentage until a real DWG backend and real DXF input are evaluated**.

## CLI research mode

`--research` performs canonical probing, candidate discovery, deterministic C++ search, plan-aware materialization, exact DWG evaluation, independent reverse verification, and winning DWG selection.

External-command example shape:

```bash
./build/cadopt \
  --input drawing.dxf \
  --output build/optimized.dwg \
  --research \
  --backend-profile external \
  --dxf-to-dwg 'YOUR_ENCODER {input} {output}' \
  --dwg-to-dxf 'YOUR_DECODER {input} {output}' \
  --quantize-steps 1e-10,5e-10,1e-9 \
  --geometry-abs-tol 1e-9 \
  --geometry-rel-tol 1e-9 \
  --max-plans 16 \
  --beam-width 64 \
  --work-dir build/research \
  --report build/research_report.json
```

The process returns no winner when the backend is unavailable, reverse conversion fails, or the reconstructed document fails independent verification.

## Native SDK host ABI

A separately licensed host library exports:

```c
int cadopt_backend_api_version(void);
const char* cadopt_backend_name(void);
int cadopt_backend_dxf_to_dwg(const char* input_utf8,
                              const char* output_utf8,
                              char* diagnostic_utf8,
                              size_t diagnostic_capacity);
int cadopt_backend_dwg_to_dxf(const char* input_utf8,
                              const char* output_utf8,
                              char* diagnostic_utf8,
                              size_t diagnostic_capacity);
```

The optimizer loads those symbols dynamically and still performs its own output existence/byte measurement and independent semantic verification. The proprietary SDK remains outside this repository.

## Traceability

M6 preserves the M2 trace model. Canonicalization drift, selected representation candidate, materializer verification failure category, and exact output size are retained in the machine-readable report so later lossy algorithms can be traced back to the exact operation and parameters that caused a difference.

## What M6 does and does not prove

M6 source implementation provides the end-to-end architecture and the first actually materializable bounded-loss candidate family. It does **not** prove the target drawing is already smaller because this branch has not yet been PR-time built/tested and no licensed DWG backend has been used on the real DXF drawing in this task.

A compression result is valid only after:

- a real DXF input is supplied;
- a real backend serializes each candidate to DWG;
- actual DWG filesystem bytes are measured;
- the DWG is independently read back;
- geometry + semantic + interaction checks pass.

## Verification status

Test sources cover:

- valid and corrupted independent round trips;
- plan-aware quantization accepted/rejected according to geometry tolerance;
- exact DWG byte precedence over estimated cost;
- fail-closed missing native backend loading;
- dynamic loading and two-way conversion through a fake native DLL/SO implementing the same ABI.

Per project workflow these tests are not executed until a PR is requested.
