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

## Milestone 2 implemented branch scope

Development branch: `feature/milestone-2-loss-trace`  
Issue: #1

1. DXF indexing includes BLOCK definitions, OBJECTS records, INSERT targets, GROUP member references, and XDATA application ownership.
2. Verification separates geometry, semantic, interaction, reference, block/object, and XDATA loss categories.
3. Geometry comparison for LINE/ARC/CIRCLE/LWPOLYLINE/TEXT normalizes numeric representation and applies explicit tolerances.
4. Selection/edit behavior is a hard constraint: entity count/type, INSERT target, BLOCK definition, and GROUP membership changes fail verification.
5. Every verification issue carries severity/category/detail and optional numeric metrics.
6. Verification failures are correlated back into the source trace ledger.
7. Source Truth remains immutable.

## Milestone 3 source implementation

Issue: #2

Milestone 3 introduces the reversible optimization-space math layer:

1. `Vec3` / affine `Mat4` geometry primitives.
2. Reversible translation, XYZ rotation, signed axis permutation, and uniform scale.
3. Explicit affine inverse calculation and round-trip drift measurement.
4. Non-destructive geometry views for LINE, ARC, CIRCLE, LWPOLYLINE, and TEXT.
5. Unsupported/incomplete entity geometry remains explicit and cannot silently pass as supported geometry.
6. Canonicalization recenters and aligns geometry for later representation discovery while preserving an inverse transform back to the source coordinate frame.
7. Canonical views are intermediate-only; customer-facing entity units are reconstructed before final DWG serialization.

M3 test source is included but, per project workflow, build/regression execution is deferred until a PR is requested.

## Current deliberate limitation

This repository does **not** ship Autodesk RealDWG or ODA binaries/licenses. True DWG round-trip verification becomes active only when a licensed/installed backend is supplied through the backend interface/CLI command templates.

The current development branch contains M2/M3 source implementations but is not claimed as build-verified until PR-time verification is requested.

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
src/                  source truth, geometry, verifier, trace, DWG backend, CLI
app/minimal_qt/       optional file-picker-only Qt shell
python/cadopt_lab/    research/search layer; source geometry remains C++ owned
tests/                regression fixtures/tests
docs/                 decisions, milestone notes, design and implementation plans
```

## Active development

- #1 M2 loss/difference trace and richer semantic verification.
- #2 M3 reversible canonical transforms and exact reconstruction.
- #3 M4 representation discovery and lightweighting candidate engine.
- #4 M5 multi-method search and black-box size optimization.
- #5 M6 production DWG backend, exact byte objective, and end-to-end validation.
- Branch: `feature/milestone-2-loss-trace`.

## Roadmap

M4 adds global/structural/local representation discovery and candidate recipes. M5 adds competing long-running search methods and experiment persistence. M6 serializes candidates through a real DWG backend, independently round-trips them back to DXF, and chooses the smallest candidate that passes geometry + semantic + interaction verification.
