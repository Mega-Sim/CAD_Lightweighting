# GigaRoute CAD Optimizer

Research-first DXF → DWG minimization engine. The customer-facing objective is simple: **minimize the final DWG byte size while preserving the drawing as the same CAD document in practical use**.

## Fixed product constraints

- Input: DXF.
- Output: DWG.
- Core languages: C++ + Python.
- Intermediate representation is unrestricted: rotation/translation/coordinate normalization, temporary grouping, tensor/wavelet/graph/latent/compressed representations, and lossy candidates are allowed internally.
- Final output must restore CAD behavior, not only appearance. If source lines are individually selectable, the output must not silently turn them into a newly created block/group.
- Optimization time is secondary to finding the smallest valid final DWG during the research phase.
- Final objective: actual output DWG bytes. Intermediate compression ratios are diagnostics only.
- Loss/difference traceability is a first-class requirement from the beginning.

## Milestone 1 implemented scope

Milestone 1 establishes the zero-optimization safety baseline:

1. ASCII DXF is ingested as an immutable lexical source plus an entity semantic index.
2. The source can be emitted byte-for-byte in `PreserveLexical` mode.
3. ENTITIES selection units are indexed with stable source IDs, type, handle, layer, and source record spans.
4. Semantic verification checks entity count, entity type, layer, and group-code/value fingerprints.
5. A trace ledger records each source entity and every future transformation step, including whether the step can be lossy.
6. The CLI supports a strict zero-optimization DXF round trip.
7. DWG conversion is isolated behind a backend boundary. In this repository the production backend is an external-command adapter so RealDWG/ODA or another licensed writer can be connected without contaminating geometry truth or search logic.
8. For a real DXF→DWG run, the CLI requires both DXF→DWG and DWG→DXF commands. The generated DWG is converted back to DXF and independently compared before the run can pass.
9. Python exists only as a research-side package at this milestone; it does not own source geometry.
10. An optional Qt6 Widgets shell contains only an `Open DXF` button and selected-path display. The optimizer remains CLI/core-first.

## Current deliberate limitation

This package does **not** ship Autodesk RealDWG or ODA binaries/licenses. Therefore this environment verifies the complete DXF→IR→DXF safety path, while true DWG round-trip verification becomes active when a licensed/installed backend is supplied through the CLI command templates.

This is deliberate: Milestone 1 must never fake a `.dwg` by renaming or copying DXF bytes.

## Build

Linux/macOS:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

Windows Git Bash with MinGW:

```bash
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

## Zero-optimization dry run

```bash
./build/cadopt \
  --input tests/fixtures/minimal.dxf \
  --dry-run \
  --report build/m1_report.json
```

## Real DWG round trip

After installing/licensing a DWG converter/writer, provide commands containing both `{input}` and `{output}` placeholders:

```bash
./build/cadopt \
  --input drawing.dxf \
  --output drawing.dwg \
  --dxf-to-dwg 'YOUR_DXF_TO_DWG_COMMAND {input} {output}' \
  --dwg-to-dxf 'YOUR_DWG_TO_DXF_COMMAND {input} {output}' \
  --report cadopt_report.json
```

A run is rejected if the independent round-trip semantic verifier fails.

## Repository layout

```text
include/cadopt/       C++ public interfaces
src/                  source truth, DXF IR, verifier, trace, DWG backend, CLI
app/minimal_qt/       optional file-picker-only Qt shell
python/cadopt_lab/    later research/search layer; report reader only in M1
tests/                TDD regression fixtures/tests
docs/                 decisions and milestone notes
```

## Next milestone

Milestone 2 should deepen the independent comparison/trace foundation before any lossy optimization is allowed: richer BLOCK/INSERT/GROUP/OBJECT/XDATA semantics, geometry-normalized comparison, and entity-level cause tracing across real DWG round trips.
