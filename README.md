# GigaRoute CAD Optimizer

Research-first **DXF/DWG → DWG lightweighting engine**. The product objective is intentionally simple: **minimize the actual final DWG byte size while making the output behave like the same CAD document in customer use**.

## Fixed product constraints

- Input: **DXF or DWG**.
- DWG input is decoded by the configured DWG backend into a temporary normalized DXF before the existing Source Truth / optimizer pipeline starts.
- Output: **DWG**.
- Core languages: **C++20 + Python 3**.
- Intermediate representation is unrestricted: coordinate translation, XYZ rotation, axis changes, uniform normalization, temporary grouping, reference/residual representations, grammar/grid forms, tensor/wavelet/spectral probes, compression, and lossy candidates are allowed internally.
- Final output must preserve more than appearance. Geometry, CAD semantics, selection units, block/group/reference relationships, and practical edit behavior are verification constraints.
- Temporary merging/blocking is allowed only inside the optimizer; if source lines were individually selectable, the final output must restore equivalent individual selection/edit units.
- Absolute coordinates are low-priority inside the optimizer; physical shape/relative relationships and correct final reconstruction are authoritative.
- Research runtime is secondary to finding the smallest valid result.
- **Final objective = actual serialized DWG filesystem bytes.** Intermediate compression/description-length estimates are search hints only.
- Loss/difference traceability is a first-class requirement.
- UI stays intentionally minimal. Current development is core/CLI first.

## Development branch

Current branch: `feature/milestone-2-loss-trace`

Tracked issues:

- #1 — M2 loss/difference trace and richer semantic verification
- #2 — M3 reversible canonical transforms and exact reconstruction
- #3 — M4 representation discovery and lightweighting candidate engine
- #4 — M5 multi-method search and black-box size optimization
- #5 — M6 production DWG backend, exact byte objective, and end-to-end validation
- #6 — direct DWG input normalization through the configured backend

The branch was created from the then-current `main`. Development is based on the current repository code, not historical UI/code snapshots.

## Milestone status

### M1 — zero-optimization safety baseline

Implemented and user-verified on Linux:

- lexical-preserving ASCII DXF parser
- immutable Source Truth
- stable ENTITIES selection-unit index
- byte-preserving DXF round trip
- fail-closed external DWG backend boundary
- minimal Qt file-picker shell

### M2 — loss/difference trace and semantic verification

Implemented:

- BLOCK definitions, OBJECTS, INSERT targets, GROUP references, XDATA application ownership
- geometry/semantic/interaction/reference loss categories
- normalized numeric geometry comparison for LINE/ARC/CIRCLE/LWPOLYLINE/TEXT
- hard selection/edit constraints for entity count/type and structure
- order-independent entity matching for backend round trips that legitimately reorder entities
- source-correlated trace records with severity, category, detail, and numeric metrics

### M3 — reversible canonical transform core

Implemented:

- `Vec3` / affine `Mat4`
- reversible translation, XYZ rotation, signed axis permutation, uniform scale
- explicit affine inverse and round-trip drift measurement
- non-destructive geometry views
- canonical recenter/alignment for representation discovery
- unsupported/incomplete entities remain explicit and fail safe

See `docs/MILESTONE_3.md`.

### M4 — representation discovery and candidate engine

Implemented:

- global/structural/local scopes are all allowed; no single scope is privileged
- raw source fallback
- bounded whole-drawing numeric quantization probes
- transform-invariant exact-repeat signatures
- reference + transform candidates
- near-repeat reference + residual candidates
- grid / repeated sequence / grammar probes
- whole-drawing structural estimate
- dependency-free Python tensor/wavelet/spectral suitability probes
- candidate provenance, exact source coverage, reconstruction recipe, estimated bytes, residual estimate, and loss risk

`NumericQuantization` is the first candidate family that can alter serialized numeric values while preserving entity count/type/layer/block/group structure. Angle/bulge/dimensionless fields remain exact in the first lossy path so different error units are not mixed.

See `docs/MILESTONE_4.md`.

### M5 — multi-method search

Implemented:

- exact-cover structural validity: each source entity must be represented exactly once
- deterministic greedy search
- bounded exhaustive search
- beam search fallback
- Python deterministic-seed simulated annealing
- Python deterministic-seed genetic search
- QUBO exact-cover export for optional quantum-inspired solvers
- append-only JSONL experiment records
- strict separation between `estimated_bytes` and `exact_dwg_bytes`

**Actual DWG bytes always outrank estimated cost.** Quantum-inspired search is one competitor, not a hard-coded winner.

See `docs/MILESTONE_5.md`.

### M6 — exact DWG evaluator and end-to-end selection

Implemented:

- backend capability/profile model: external command / ODA File Converter / ODA SDK host / RealDWG host
- proprietary backend binaries are **not** bundled
- external command adapter with strict `{input}` / `{output}` validation
- runtime native DLL/SO adapter for separately licensed ODA/RealDWG hosts
- stable C ABI in `include/cadopt/native_backend_api.h`
- fail-closed ABI version/symbol/output validation
- direct `.dxf` and `.dwg` input detection
- DWG input normalization to `<work-dir>/input/normalized_source.dxf`
- source DWG overwrite protection
- `StrictSourceMaterializer`, `NumericQuantizationMaterializer`, and `PlanAwareMaterializer`
- real DWG filesystem byte measurement
- independent DWG → DXF reverse conversion
- independent geometry + semantic + interaction verification
- smallest verified DWG winner selection
- `--research` CLI path: input normalization → canonical probe → candidate discovery → search → materialization → exact DWG evaluation → reverse verification → winner copy
- machine-readable candidate/search/evaluation/trace report
- fake native DLL/SO test backend using the same ABI for loader verification

See `docs/MILESTONE_6.md` and `docs/NATIVE_DWG_BACKEND.md`.

## Verification status

Windows MinGW 13.1 verification before the direct-DWG-input follow-up:

- clean build completed
- CTest **7/7 PASS**
- CLI `minimal.dxf` dry run PASS
- selection units preserved

Issue #6 adds an eighth regression target, `cadopt_input_tests`, covering direct DWG input normalization. Per project workflow, the new change is source-complete but its new build/CTest execution is deferred until a PR is requested.

Structural reference/grid/grammar/tensor/wavelet/spectral candidates are still **analysis/search representations** in the generic DXF materializer. They are deliberately rejected instead of being serialized in a way that could alter customer-visible selection/edit behavior. A licensed backend-specific materializer may implement those recipes later under the same independent verifier.

Therefore **no compression percentage is claimed from source code alone**. A reduction is valid only after a real customer drawing is serialized to DWG, actual output bytes are measured, the DWG is independently read back, and all hard equivalence checks pass.

For direct DWG input, the immutable Source Truth is the DXF representation decoded by the configured backend. The core does not claim to parse proprietary DWG bytes itself.

This repository does not ship Autodesk RealDWG or ODA SDK/converter binaries. A production backend must be installed/licensed separately and connected through the supported backend boundary.

## Build

Linux:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
```

Windows Git Bash with MinGW:

```bash
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

## DXF dry run

```bash
./build/cadopt \
  --input tests/fixtures/minimal.dxf \
  --dry-run \
  --report build/dry_run_report.json
```

## DWG direct-input dry run

A DWG reader is required even for dry run because the optimizer first normalizes the source to DXF. Only `--dwg-to-dxf` is needed when no DWG output is produced.

```bash
./build/cadopt \
  --input drawing.dwg \
  --dry-run \
  --backend-profile external \
  --dwg-to-dxf 'INSTALLED_DECODER_COMMAND {input} {output}' \
  --work-dir build/dwg_input \
  --report build/dwg_input_report.json
```

Backend command syntax depends on the separately installed backend; do not substitute guessed syntax.

## One-shot real DWG round trip

DXF input:

```bash
./build/cadopt \
  --input drawing.dxf \
  --output build/roundtrip.dwg \
  --backend-profile external \
  --dxf-to-dwg 'INSTALLED_ENCODER_COMMAND {input} {output}' \
  --dwg-to-dxf 'INSTALLED_DECODER_COMMAND {input} {output}' \
  --report build/roundtrip_report.json
```

Direct DWG input:

```bash
./build/cadopt \
  --input drawing.dwg \
  --output build/roundtrip.dwg \
  --backend-profile external \
  --dxf-to-dwg 'INSTALLED_ENCODER_COMMAND {input} {output}' \
  --dwg-to-dxf 'INSTALLED_DECODER_COMMAND {input} {output}' \
  --work-dir build/dwg_roundtrip \
  --report build/roundtrip_report.json
```

The output path must not be the same as the source drawing.

## M3-M6 research mode

```bash
./build/cadopt \
  --input drawing.dwg \
  --output build/optimized.dwg \
  --research \
  --backend-profile external \
  --dxf-to-dwg 'INSTALLED_ENCODER_COMMAND {input} {output}' \
  --dwg-to-dxf 'INSTALLED_DECODER_COMMAND {input} {output}' \
  --quantize-steps 1e-10,5e-10,1e-9 \
  --geometry-abs-tol 1e-9 \
  --geometry-rel-tol 1e-9 \
  --max-plans 16 \
  --beam-width 64 \
  --work-dir build/research \
  --report build/research_report.json
```

The same research command also accepts a `.dxf` input. No winner is accepted if input normalization fails, backend conversion fails, reverse conversion fails, geometry/semantic/interaction verification fails, or trace contains an unresolved fatal issue.

## Native backend integration

Production ODA/RealDWG hosts implement the ABI in:

```text
include/cadopt/native_backend_api.h
```

The optimizer's `NativeLibraryDwgBackend` loads the host DLL/SO at runtime. Proprietary headers/libraries remain outside this repository. See `docs/NATIVE_DWG_BACKEND.md`.

## Repository layout

```text
include/cadopt/       C++ public interfaces, input normalization, and native backend ABI
src/                  Source Truth, input normalization, geometry, candidate/search/evaluator, verifier, trace, DWG backend, CLI
python/cadopt_lab/    experimental probes/search/QUBO/experiment records
app/minimal_qt/       optional file-picker-only shell
tests/                regression test sources, fake native backend, and fixtures
docs/                 decisions, milestone notes, architecture/implementation plan, backend contract, PR verification checklist
```

## Verification checklist

See `docs/M3_M6_VERIFICATION.md` for the PR-time build/test/dry-run/fail-closed/native-loader/real-backend verification sequence.

## Reference drawing

The repository includes `7F.dwg` as a reference drawing. With a configured DWG backend it can now be supplied directly to `--input`; a manual DXF export is no longer required. Renaming a DWG or archive as DXF/DWG is never treated as a valid conversion or product output.
