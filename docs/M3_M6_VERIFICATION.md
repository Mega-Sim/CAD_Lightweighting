# M3-M6 PR-time Verification Checklist

Build/regression execution is intentionally deferred until a PR is requested. At that point run the following from Linux on the development branch.

## 1. Synchronize branch

```bash
git fetch origin
git switch feature/milestone-2-loss-trace
git pull --ff-only origin feature/milestone-2-loss-trace
```

## 2. Clean Release configure/build

```bash
rm -rf build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
```

Expected: configure/build exit code 0. The fake native backend shared library must also build successfully.

## 3. Full CTest suite

```bash
ctest --test-dir build --output-on-failure
```

Expected tests when Python 3 is available:

- `cadopt_tests`
- `cadopt_geometry_tests`
- `cadopt_candidate_tests`
- `cadopt_search_tests`
- `cadopt_evaluator_tests`
- `cadopt_native_backend_tests`
- `cadopt_python_search_tests`

All registered tests must pass before opening/merging the PR.

The native backend test uses `tests/fake_native_backend.cpp`. It validates ABI loading/symbol resolution/two-way calls only; it is not a DWG implementation.

## 4. Zero-optimization safety smoke test

```bash
./build/cadopt \
  --input tests/fixtures/minimal.dxf \
  --dry-run \
  --report build/dry_run_report.json

cat build/dry_run_report.json
```

Expected: geometry/semantic/interaction round trip passes and source selection-unit cardinality is unchanged.

## 5. Fail-closed backend smoke test

```bash
set +e
./build/cadopt \
  --input tests/fixtures/minimal.dxf \
  --output build/should_not_exist.dwg \
  --research \
  --report build/no_backend_report.json
status=$?
set -e

test "$status" -ne 0
test ! -s build/should_not_exist.dwg
```

Expected: non-zero exit and no accepted DWG when no encoder/decoder backend is configured.

## 6. Numeric-quantization safety smoke test with fake copy backend logic

The C++ evaluator regression already exercises this path. Confirm its report/test assertions show:

- a quantization step inside configured geometry tolerance remains eligible;
- the same step outside tolerance is rejected at independent verification;
- entity count/type/layer/selection cardinality are unchanged;
- exact serialized byte count, not estimated cost, chooses the winner.

Do not interpret the fake backend byte count as real DWG compression.

## 7. Native backend ABI test

CTest must load the generated fake DLL/SO through `NativeLibraryDwgBackend` and complete two-way conversion.

Also confirm the fail-closed test rejects a missing library path before evaluation.

For a real licensed native host, verify its exports against `include/cadopt/native_backend_api.h` before using the reference drawing.

## 8. Real DWG round-trip when a licensed/installed backend is available

Do not guess backend command-line syntax. Use the exact command documented by the locally installed licensed backend and pass it through the placeholders below.

```bash
./build/cadopt \
  --input /path/to/reference.dxf \
  --output build/reference_roundtrip.dwg \
  --backend-profile external \
  --dxf-to-dwg 'INSTALLED_ENCODER_COMMAND {input} {output}' \
  --dwg-to-dxf 'INSTALLED_DECODER_COMMAND {input} {output}' \
  --report build/reference_roundtrip_report.json
```

Expected: emitted DWG is non-empty, reverse conversion succeeds, and independent geometry + semantic + interaction verification passes.

A production ODA/RealDWG DLL/SO may instead use the native API described in `docs/NATIVE_DWG_BACKEND.md`; proprietary SDK files remain outside this repository.

## 9. End-to-end research search with a real backend

Start with conservative tolerances:

```bash
./build/cadopt \
  --input /path/to/reference.dxf \
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

The selected winner is valid only when the report confirms:

1. source coverage is exact;
2. actual DWG byte size is measured;
3. reverse conversion succeeds;
4. geometry verification passes;
5. semantic verification passes;
6. interaction/selection semantics pass;
7. no unresolved fatal trace exists.

If a quantized candidate wins, record its exact quantization step and maximum measured geometry deviation from the report/trace.

## 10. Python research search checks

The registered Python test verifies deterministic seeded SA/GA exact-cover behavior and QUBO penalty safety. For a long research run, keep seeds and experiment JSONL records so the result is reproducible.

Python estimated cost is never a shipping metric. Any Python-generated candidate combination must still be reconstructed/evaluated by the C++ M6 exact DWG pipeline.

## 11. Reference FAB drawing

The repository contains `7F.dwg` only as a reference DWG. Product input remains DXF. A corresponding DXF must be supplied/exported before it can be used as an optimizer input. Never rename DWG bytes to `.dxf` or treat a compressed/archive intermediate as the product output.

## Known research boundary

`PlanAwareMaterializer` can currently materialize `Raw` and bounded `NumericQuantization` candidates in generic DXF without changing entity-unit structure. Reference/grid/grammar/tensor/wavelet/spectral representations remain analysis-only for the generic materializer and are rejected rather than emitted unsafely.

Those advanced representations become eligible for real byte competition only after a backend-specific materializer can reconstruct standard CAD entities with equivalent geometry + semantics + selection/edit behavior and pass this same independent round-trip gate.
