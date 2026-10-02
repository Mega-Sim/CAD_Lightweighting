# Milestone 6 — Exact DWG Evaluation and End-to-End Selection

Milestone 6 closes the research loop by making the real serialized DWG file size the final objective.

## Backend boundary

The repository does not bundle proprietary Autodesk RealDWG or ODA SDK binaries. `DwgBackend` has two host paths:

1. `ExternalCommandDwgBackend` for explicit DXF→DWG / DWG→DXF command templates.
2. `NativeLibraryDwgBackend` for a separately built/licensed ODA Drawings SDK or RealDWG host loaded at runtime through the stable `cadopt/native_backend_api.h` C ABI.

Backend profiles identify intended families (`external`, `oda-file-converter`, `oda-sdk`, `realdwg-host`) and expose capability/licensing metadata. Missing commands, missing plugin symbols, ABI mismatch, failed conversion, stale/missing output, empty output, or unmeasurable output all fail closed.

The native ABI version is currently `1` and requires both conversion directions so independent round-trip verification cannot be bypassed.

## Direct DXF and DWG input

The CLI accepts both `.dxf` and `.dwg` source files.

- DXF input enters Source Truth directly.
- DWG input is first decoded through the configured `DwgBackend::dwg_to_dxf()` path into `<work-dir>/input/normalized_source.dxf`.
- The customer source DWG is never overwritten.
- Unsupported extensions, a missing DWG reader, failed conversion, missing normalized output, or an empty normalized output fail closed before optimization begins.
- For DWG input, the immutable Source Truth used by the optimizer/verifier is the backend-decoded DXF representation. The core does not pretend to parse proprietary DWG bytes itself.

This removes the manual "export to DXF first" step while retaining the same internal verification boundary.

## Exact evaluator

`evaluate_candidate_plans()` performs, for every evaluated plan:

1. validate exact source coverage / selection cardinality;
2. reject evaluation immediately when an unresolved fatal trace already exists;
3. materialize a final source-semantic DXF according to the selected plan;
4. convert it to DWG through the configured backend;
5. measure the actual DWG filesystem byte size;
6. independently convert that DWG back to DXF;
7. parse the reverse DXF;
8. run geometry + semantic + interaction verification against immutable Source Truth;
9. correlate verifier loss back into trace;
10. reject the candidate if verification fails or any fatal trace remains;
11. select the smallest exact byte count among valid candidates.

Intermediate `estimated_bytes` never overrides actual `exact_dwg_bytes`.

## Materialization policy

Three materializer classes exist:

- `StrictSourceMaterializer`: byte-preserving source-semantic baseline.
- `NumericQuantizationMaterializer`: isolated global numeric quantization experiment helper.
- `PlanAwareMaterializer`: production research path; reads each `CandidatePlan` and materializes only candidate kinds it can safely express in generic DXF.

`PlanAwareMaterializer` currently supports:

- `Raw`: exact lexical/source reconstruction.
- `NumericQuantization`: coordinate/distance-like fields are rounded to the candidate step while entity count/type/layer/block/group ownership stays unchanged.

It deliberately rejects reference/residual/symmetry/grid/grammar/primitive/tensor/wavelet/spectral recipes when using the generic DXF materializer because emitting those directly could change customer-visible CAD selection/edit semantics. Those recipes remain valid search/research representations and may be implemented by a native licensed backend-specific materializer later.

This means M6 now has a real path where candidate values can change serialized DWG bytes, but **it still does not claim a particular compression percentage until a real DWG backend and real customer drawing are evaluated**.

## CLI research mode

`--research` performs input normalization when required, canonical probing, candidate discovery, deterministic C++ search, plan-aware materialization, exact DWG evaluation, independent reverse verification, fatal-trace gate, and winning DWG selection.

DXF input example:

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

Direct DWG input uses the same command shape; only `--input` changes:

```bash
./build/cadopt \
  --input drawing.dwg \
  --output build/optimized.dwg \
  --research \
  --backend-profile external \
  --dxf-to-dwg 'YOUR_ENCODER {input} {output}' \
  --dwg-to-dxf 'YOUR_DECODER {input} {output}' \
  --work-dir build/research \
  --report build/research_report.json
```

For a DWG-only dry run, `--dwg-to-dxf` is sufficient because no DWG output is created. Any optimization/round-trip run still requires both conversion directions.

The process returns no winner when the backend is unavailable, input normalization fails, reverse conversion fails, the reconstructed document fails independent verification, or trace contains an unresolved fatal issue.

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

M6 preserves the M2 trace model. Input kind/normalization, canonicalization drift, selected representation candidate, materializer verification failure category, and exact output size are retained in the machine-readable report so later lossy algorithms can be traced back to the exact operation and parameters that caused a difference.

`TraceLedger::has_fatal_issue()` is a hard acceptance gate. Fatal means the run has a condition that cannot be resolved by candidate scoring; no candidate from that evaluation may become a winner until the fatal condition is removed.

## What M6 does and does not prove

M6 source implementation provides the end-to-end architecture and the first actually materializable bounded-loss candidate family. Direct DWG input removes the manual pre-export step, but still depends on a real DWG backend.

A compression result is valid only after:

- a real DXF or DWG input is supplied;
- DWG input is successfully normalized by the configured backend;
- a real backend serializes each candidate to DWG;
- actual DWG filesystem bytes are measured;
- the DWG is independently read back;
- geometry + semantic + interaction checks pass;
- trace contains no unresolved fatal issue.

## Verification status

The M1-M6 branch was built on Windows with MinGW 13.1 and the pre-direct-DWG-input regression set passed 7/7, followed by a successful CLI dry run on `minimal.dxf`.

The direct-DWG-input follow-up adds regression source for:

- case-insensitive `.dxf` / `.dwg` detection;
- DXF input bypassing the backend;
- DWG input failing closed without a reader;
- backend-driven DWG→DXF normalization inside the work directory;
- preservation of the original DWG bytes during normalization;
- unsupported input extensions failing closed.

Per project workflow, the new follow-up test is not executed until a PR is requested.
