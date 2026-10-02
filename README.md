# GigaRoute CAD Optimizer

Research-first DXF → DWG minimization engine. The customer-facing objective is simple: **minimize the final DWG byte size while preserving the drawing as the same CAD document in practical use**.

## Fixed product constraints

- Input: DXF.
- Output: DWG.
- Core languages: C++ + Python.
- Intermediate representation is unrestricted: rotation/translation/coordinate normalization, temporary grouping, tensor/wavelet/graph/latent/compressed representations, and lossy candidates are allowed internally.
- Final output must restore CAD behavior, not only appearance. If source lines are individually selectable, the output must not silently turn them into a newly created block/group.
- Intermediate merging/blocking is allowed only if final reconstruction restores the original customer-visible selection/edit semantics.
- Optimization time is secondary to finding the smallest valid final DWG during the research phase.
- Final objective: actual output DWG bytes. Intermediate compression ratios are diagnostics only.
- Loss/difference traceability is a first-class requirement from the beginning.

## Milestone 1 implemented scope

Milestone 1 establishes the zero-optimization safety baseline:

1. ASCII DXF is ingested as an immutable lexical source plus an entity semantic index.
2. The source can be emitted byte-for-byte in `PreserveLexical` mode.
3. ENTITIES selection units are indexed with stable source IDs, type, handle, layer, and source record spans.
4. The CLI supports a strict zero-optimization DXF round trip.
5. DWG conversion is isolated behind a backend boundary. In this repository the production backend is an external-command adapter so RealDWG/ODA or another licensed writer can be connected without contaminating geometry truth or search logic.
6. For a real DXF→DWG run, the CLI requires both DXF→DWG and DWG→DXF commands. The generated DWG is converted back to DXF and independently compared before the run can pass.

## Milestone 2 branch scope

Development branch: `feature/milestone-2-loss-trace`  
Issue: #1

Milestone 2 deepens the comparison/trace foundation before lossy optimization is enabled:

1. DXF indexing now includes BLOCK definitions, OBJECTS records, INSERT targets, GROUP member references, and XDATA application ownership.
2. Verification separates geometry, semantic, interaction, reference, block/object, and XDATA loss categories.
3. Geometry comparison for LINE/ARC/CIRCLE/LWPOLYLINE/TEXT normalizes numeric representation and applies explicit absolute/relative tolerances instead of treating harmless decimal formatting changes as geometry loss.
4. Selection/edit behavior is treated as a hard constraint: entity count/type, INSERT target, BLOCK definition, and GROUP membership changes fail verification.
5. Every verification issue carries severity/category/detail and optional numeric metrics.
6. Verification failures are correlated back into the source trace ledger so later optimization stages can identify which operation caused a loss.
7. The CLI emits detailed Milestone 2 indexing/verification diagnostics while retaining fail-closed DWG backend behavior.
8. Source Truth remains immutable; no optimizer owns or mutates the source document.

## Current deliberate limitation

This repository does **not** ship Autodesk RealDWG or ODA binaries/licenses. Therefore true DWG round-trip verification becomes active only when a licensed/installed backend is supplied through the CLI command templates.

Milestone 2 is currently a development-branch implementation. Per the project workflow, build/regression execution is deferred until a PR is requested; the branch is not claimed as build-verified yet.

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
  --report build/m2_report.json
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

A run is rejected if the independent geometry/semantic/interaction verifier fails.

## Repository layout

```text
include/cadopt/       C++ public interfaces
src/                  source truth, DXF semantic indexes, verifier, trace, DWG backend, CLI
app/minimal_qt/       optional file-picker-only Qt shell
python/cadopt_lab/    research/search layer; source geometry remains C++ owned
tests/                regression fixtures/tests
docs/                 decisions and milestone notes
```

## Active development

- Issue #1: Milestone 2 loss/difference trace and richer semantic verification.
- Branch: `feature/milestone-2-loss-trace`.
- Branch was created from the latest `main`; previous commits were not used as a development base or UI reference.

## Following milestone

After Milestone 2 is verified, Milestone 3 can begin reversible optimization experiments (global/local transforms, coordinate normalization/canonicalization, and other transformations that can be reconstructed exactly) while the M2 verifier/trace remains the safety gate.
