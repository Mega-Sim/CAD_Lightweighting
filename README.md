# GigaRoute CAD Optimizer

Research-first **DXF → DWG lightweighting engine**. The product objective is intentionally simple: **minimize the actual final DWG byte size while making the output behave like the same CAD document in customer use**.

## Fixed product constraints

- Input: **DXF**.
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

The initial user run passed CTest and the three-entity LINE/ARC/TEXT dry-run.

### M2 — loss/difference trace and semantic verification

Source implementation on the development branch:

- BLOCK definitions, OBJECTS, INSERT targets, GROUP references, XDATA application ownership
- geometry/semantic/interaction/reference loss categories
- normalized numeric geometry comparison for LINE/ARC/CIRCLE/LWPOLYLINE/TEXT
- hard selection/edit constraints for entity count/type and structure
- source-correlated trace records with severity, category, detail, and numeric metrics

### M3 — reversible canonical transform core

Source implementation:

- `Vec3` / affine `Mat4`
- reversible translation, XYZ rotation, signed axis permutation, uniform scale
- explicit affine inverse and round-trip drift measurement
- non-destructive geometry views
- canonical recenter/alignment for representation discovery
- unsupported/incomplete entities remain explicit and fail safe

See `docs/MILESTONE_3.md`.

### M4 — representation discovery and candidate engine

Source implementation:

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

Source implementation:

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

Source implementation:

- backend capability/profile model: external command / ODA File Converter / ODA SDK host / RealDWG host
- proprietary backend binaries are **not** bundled
- external command adapter with strict `{input}` / `{output}` validation
- runtime native DLL/SO adapter for separately licensed ODA/RealDWG hosts
- stable C ABI in `include/cadopt/native_backend_api.h`
- fail-closed ABI version/symbol/output validation
- `StrictSourceMaterializer`, `NumericQuantizationMaterializer`, and `PlanAwareMaterializer`
- real DWG filesystem byte measurement
- independent DWG → DXF reverse conversion
- independent geometry + semantic + interaction verification
- smallest verified DWG winner selection
- `--research` CLI path: canonical probe → candidate discovery → search → materialization → exact DWG evaluation → reverse verification → winner copy
- machine-readable candidate/search/evaluation/trace report
- fake native DLL/SO test backend using the same ABI for PR-time loader verification

See `docs/MILESTONE_6.md` and `docs/NATIVE_DWG_BACKEND.md`.

## Important verification boundary

M2-M6 are currently **source implementations, not yet PR-time build/regression verified**. Per project workflow, build/test execution is deferred until a PR is requested. Test sources and the verification checklist are included now.

M6 does have an actual materializable lightweighting path: `PlanAwareMaterializer` can apply bounded numeric quantization to supported coordinate/distance fields, serialize the candidate through a real DWG backend, reverse-convert it, and reject it when geometry/semantic/interaction verification fails.

Structural reference/grid/grammar/tensor/wavelet/spectral candidates are still **analysis/search representations** in the generic DXF materializer. They are deliberately rejected instead of being serialized in a way that could alter customer-visible selection/edit behavior. A licensed backend-specific materializer may implement those recipes later under the same independent verifier.

Therefore **no compression percentage is claimed from source code alone**. A reduction is valid only after a real DXF input is serialized to DWG, actual output bytes are measured, the DWG is independently read back, and all hard equivalence checks pass.

This repository also does not ship Autodesk RealDWG or ODA SDK/converter binaries. A production backend must be installed/licensed separately and connected either through explicit command templates or the native plugin ABI.

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

## Strict dry run

```bash
./build/cadopt \
  --input tests/fixtures/minimal.dxf \
  --dry-run \
  --report build/dry_run_report.json
```

## One-shot real DWG round trip

Backend command syntax depends on the separately installed backend; do not substitute guessed syntax.

```bash
./build/cadopt \
  --input drawing.dxf \
  --output build/roundtrip.dwg \
  --backend-profile external \
  --dxf-to-dwg 'INSTALLED_ENCODER_COMMAND {input} {output}' \
  --dwg-to-dxf 'INSTALLED_DECODER_COMMAND {input} {output}' \
  --report build/roundtrip_report.json
```

## M3-M6 research mode

```bash
./build/cadopt \
  --input drawing.dxf \
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

No winner is accepted if backend conversion fails, reverse conversion fails, or geometry/semantic/interaction verification fails.

## Native backend integration

Production ODA/RealDWG hosts implement the ABI in:

```text
include/cadopt/native_backend_api.h
```

The optimizer's `NativeLibraryDwgBackend` loads the host DLL/SO at runtime. Proprietary headers/libraries remain outside this repository. See `docs/NATIVE_DWG_BACKEND.md`.

## Repository layout

```text
include/cadopt/       C++ public interfaces and native backend ABI
src/                  Source Truth, geometry, candidate/search/evaluator, verifier, trace, DWG backend, CLI
python/cadopt_lab/    experimental probes/search/QUBO/experiment records
app/minimal_qt/       optional file-picker-only shell
tests/                regression test sources, fake native backend, and fixtures
docs/                 decisions, milestone notes, architecture/implementation plan, backend contract, PR verification checklist
```

## Verification checklist

See `docs/M3_M6_VERIFICATION.md` for the PR-time Linux build/test/dry-run/fail-closed/native-loader/real-backend verification sequence.

## Reference drawing

The repository includes `7F.dwg` as a reference drawing. Product input remains DXF, so a corresponding DXF export is required before end-to-end optimizer evaluation. Renaming a DWG or archive as DXF/DWG is never treated as a valid conversion or product output.
