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

## Large-input exact-evaluation funnel

Large production drawings make an all-candidates exact round trip impractical because a compact binary DWG can expand into a much larger normalized ASCII DXF. The research pipeline therefore separates **cheap search breadth** from **expensive backend conversion depth**.

The M6 funnel is:

1. M4 discovers representation candidates.
2. M5 retains a broad plan pool, bounded by `--max-search-plans` (default `128`).
3. A plan-aware in-memory preflight checks structural validity, source coverage constraints, selection cardinality, supported generic-DXF candidate kinds, and numeric-quantization consistency without writing candidate files or invoking the DWG backend.
4. Plans that the generic `PlanAwareMaterializer` cannot safely express are marked `unsupported` before any external converter process is started.
5. Plans expected to produce the same materialized generic DXF are collapsed by a deterministic materialization key.
6. The raw source baseline is retained as the first exact candidate whenever it exists.
7. Remaining unique materializable plans are ranked by `estimated_bytes` only to decide queue priority.
8. At most `--max-plans` plans (default `4`, `0=all`) enter the expensive exact DWG queue.
9. Only queued plans perform DXF→DWG serialization, actual filesystem byte measurement, DWG→DXF reverse conversion, and independent equivalence verification.
10. Among exact-evaluated valid plans, **actual `exact_dwg_bytes` remains the final winner criterion**. Estimated bytes never override an exact result.

This design prevents a large M5 search space from automatically multiplying expensive backend conversions. It also preserves search diversity: lowering `--max-plans` does not reduce the M5 search pool.

### Preflight states

Each search plan receives one queue decision that is emitted both to stdout and the research JSON report:

- `selected` — unique materializable plan admitted to the exact DWG budget.
- `unsupported` — cannot be represented safely by the current generic DXF materializer; no converter call is made.
- `duplicate_materialization` — another plan is expected to produce the same materialized DXF; only one representative is evaluated exactly.
- `budget_skipped` — materializable and unique, but outside the configured exact conversion budget.

The logs use `[CADOPT][M6][PREFLIGHT]` for the aggregate queue summary and `[CADOPT][M6][QUEUE]` for per-plan decisions. The JSON report exposes the same data under `evaluation_queue`.

The current materialization-key policy is intentionally narrow and deterministic:

- raw-only generic plans → `plan-aware:raw`;
- numeric quantization → quantization step plus the sorted numeric candidate identities.

The key is a **preflight equivalence key for the current generic DXF materializer**, not a claim that arbitrary future native-backend recipes are equivalent. When a native backend-specific materializer gains support for reference/grid/grammar/etc. recipes, its preflight/materialization key must reflect those additional reconstruction semantics.

## Exact evaluator

`evaluate_candidate_plans()` performs, for every plan admitted by the exact queue:

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

Intermediate `estimated_bytes` only schedules which unique materializable plans reach this stage. Once exact evaluation starts, actual `exact_dwg_bytes` outranks estimated cost.

## Materialization policy

Three materializer classes exist:

- `StrictSourceMaterializer`: byte-preserving source-semantic baseline.
- `NumericQuantizationMaterializer`: isolated global numeric quantization experiment helper.
- `PlanAwareMaterializer`: production research path; reads each `CandidatePlan` and materializes only candidate kinds it can safely express in generic DXF.

`PlanAwareMaterializer` currently supports:

- `Raw`: exact lexical/source reconstruction.
- `NumericQuantization`: coordinate/distance-like fields are rounded to the candidate step while entity count/type/layer/block/group ownership stays unchanged.

It deliberately rejects reference/residual/symmetry/grid/grammar/primitive/tensor/wavelet/spectral recipes when using the generic DXF materializer because emitting those directly could change customer-visible CAD selection/edit semantics. Those recipes remain valid search/research representations and may be implemented by a native licensed backend-specific materializer later.

The new preflight mirrors this fail-closed policy so unsupported plans are rejected **before** expensive external conversion rather than after writing a large staged DXF.

This means M6 now has a real path where candidate values can change serialized DWG bytes, but **it still does not claim a particular compression percentage until a real DWG backend and real customer drawing are evaluated**.

## CLI research mode

`--research` performs input normalization when required, canonical probing, candidate discovery, deterministic C++ search, plan-aware preflight/deduplication, bounded exact DWG evaluation, independent reverse verification, fatal-trace gate, and winning DWG selection.

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
  --max-search-plans 128 \
  --max-plans 4 \
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
  --max-search-plans 128 \
  --max-plans 4 \
  --work-dir build/research \
  --report build/research_report.json
```

`--max-search-plans` controls the cheap M5 plan pool retained for preflight. `--max-plans` controls only the expensive exact DWG round trips after unsupported and duplicate plans are removed. `--max-plans 0` evaluates every unique materializable plan.

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

The research report now also preserves the full search-plan pool and the exact-evaluation queue decision for each plan. This makes it possible to distinguish "not searched", "searched but unsupported", "duplicate materialization", "skipped by exact budget", and "actually serialized and verified" without inferring state from missing DWG artifacts.

`TraceLedger::has_fatal_issue()` is a hard acceptance gate. Fatal means the run has a condition that cannot be resolved by candidate scoring; no candidate from that evaluation may become a winner until the fatal condition is removed.

## What M6 does and does not prove

M6 source implementation provides the end-to-end architecture, the first actually materializable bounded-loss candidate family, and a bounded exact-evaluation funnel for large normalized inputs. Direct DWG input removes the manual pre-export step, but still depends on a real DWG backend.

A compression result is valid only after:

- a real DXF or DWG input is supplied;
- DWG input is successfully normalized by the configured backend;
- a real backend serializes the selected exact-queue candidates to DWG;
- actual DWG filesystem bytes are measured;
- the DWG is independently read back;
- geometry + semantic + interaction checks pass;
- trace contains no unresolved fatal issue.

A plan being skipped by preflight or the exact-evaluation budget is not proof that it would have produced a larger DWG. Queue ranking is a runtime-cost control; the final byte claim applies only to candidates actually evaluated exactly.

## Verification status

The M1-M6 branch was built on Windows with MinGW 13.1 and the pre-direct-DWG-input regression set passed 7/7, followed by a successful CLI dry run on `minimal.dxf`.

The direct-DWG-input follow-up adds regression source for:

- case-insensitive `.dxf` / `.dwg` detection;
- DXF input bypassing the backend;
- DWG input failing closed without a reader;
- backend-driven DWG→DXF normalization inside the work directory;
- preservation of the original DWG bytes during normalization;
- unsupported input extensions failing closed.

Issue #7 adds the large-input exact-evaluation funnel, preflight state reporting, materialization-key deduplication, and separate search/exact budgets. Per project workflow, these latest source changes are not claimed as newly build/CTest-verified until PR-time verification is requested.
