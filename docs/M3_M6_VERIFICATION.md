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

Expected: configure/build exit code 0.

## 3. Full CTest suite

```bash
ctest --test-dir build --output-on-failure
```

Expected test targets:

- `cadopt_tests`
- `cadopt_geometry_tests`
- `cadopt_candidate_tests`
- `cadopt_search_tests`
- `cadopt_evaluator_tests`

All must pass before opening/merging the PR.

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

## 6. Real DWG round-trip when a licensed/installed backend is available

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

## 7. End-to-end research search with a real backend

```bash
./build/cadopt \
  --input /path/to/reference.dxf \
  --output build/optimized.dwg \
  --research \
  --backend-profile external \
  --dxf-to-dwg 'INSTALLED_ENCODER_COMMAND {input} {output}' \
  --dwg-to-dxf 'INSTALLED_DECODER_COMMAND {input} {output}' \
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

## 8. Reference FAB drawing

The repository contains `7F.dwg` only as a reference DWG. Product input remains DXF. A corresponding DXF must be supplied/exported before it can be used as an optimizer input. Never rename DWG bytes to `.dxf` or treat a compressed/archive intermediate as the product output.

## Known research limitation

The current strict materializer reconstructs original source entity units before DWG serialization. It is the safety baseline for M6. Candidate estimates from reference/grid/grammar/tensor/wavelet/spectral analysis are not yet equivalent to measured savings until a source-equivalent optimized materializer actually changes native serialization and passes the full round-trip gate.
