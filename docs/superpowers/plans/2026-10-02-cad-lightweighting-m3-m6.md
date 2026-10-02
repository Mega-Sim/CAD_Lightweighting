# CAD Lightweighting M3-M6 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Build the M3-M6 research pipeline that explores multiple reversible/lossy representations and selects the smallest final DWG that still behaves like the source CAD document.

**Architecture:** C++ owns Source Truth, geometry normalization, reconstruction, verification, tracing, candidate bookkeeping, and DWG backend orchestration. Python owns long-running research search strategies and experimental cost models. Cheap description-length estimates prune the search, but actual serialized DWG bytes plus independent round-trip verification choose the winner.

**Tech Stack:** C++20, CMake, Python 3 standard library first, optional external DWG SDK/converter backends.

**Spec:** `docs/superpowers/specs/2026-10-02-cad-lightweighting-m3-m6-design.md`

## Global Constraints

- Input DXF, output DWG.
- Immutable Source Truth.
- Final CAD geometry + semantic + interaction behavior must remain equivalent.
- Intermediate representation is unrestricted.
- Final objective is actual output DWG bytes.
- No UI expansion.
- Do not bundle proprietary DWG SDK binaries.
- Build/test execution is deferred until PR request per project workflow; test sources are still added with each task.

## Review Focus

- DXF entities with incomplete/unsupported numeric geometry must never be silently treated as equivalent.
- Rotation/translation normalization must round-trip without changing selection units or entity types.
- Candidate coverage must not drop or duplicate source entities.
- A cheap estimated cost must never be reported as final DWG savings.
- Backend failures or incomplete reverse conversion must fail closed and never produce a winner.

---

### Task 1: M3 reversible geometry transform core

**Files:**
- Create: `include/cadopt/geometry.hpp`
- Create: `src/geometry.cpp`
- Create: `tests/test_geometry.cpp`
- Modify: `CMakeLists.txt`
- Modify: `README.md`
- Create: `docs/MILESTONE_3.md`

**Interfaces:**
- Produces `Vec3`, `Mat4`, `ReversibleTransform`, `GeometryView`, and extraction/canonicalization helpers.
- Geometry views are derived from `DxfDocument`; no Source Truth mutation.

- [ ] Add tests for identity, translation, XYZ rotation, axis permutation, uniform-scale normalization, inverse round trip, and 0/360 angle normalization.
- [ ] Implement matrix composition/inversion for the supported affine subset.
- [ ] Implement geometry extraction for LINE/ARC/CIRCLE/LWPOLYLINE/TEXT baseline types.
- [ ] Implement canonical frame generation and explicit inverse metadata.
- [ ] Add reconstruction-drift metrics and trace-friendly transform descriptions.
- [ ] Document M3 behavior and limitations.

### Task 2: M4 candidate representation engine

**Files:**
- Create: `include/cadopt/candidate.hpp`
- Create: `src/candidate.cpp`
- Create: `tests/test_candidate.cpp`
- Create: `python/cadopt_lab/representations.py`
- Modify: `CMakeLists.txt`
- Modify: `README.md`
- Create: `docs/MILESTONE_4.md`

**Interfaces:**
- Consumes canonical `GeometryView` objects from Task 1.
- Produces `CandidateRecipe`, `CandidateSet`, transform-invariant signatures, source coverage/conflict metadata, residual estimates, and description-length estimates.

- [ ] Add tests for translation/rotation-invariant signatures and exact-repeat grouping.
- [ ] Add tests that candidate coverage never changes final entity selection cardinality.
- [ ] Implement raw/reference/reference+residual candidate families.
- [ ] Implement global, structural, and local scope descriptors without prioritizing one scope.
- [ ] Implement simple entropy/description-length estimators and provenance.
- [ ] Add Python research hooks for tensor/wavelet/spectral candidate scoring without required third-party dependencies.
- [ ] Document M4 candidate semantics and strict reconstruction rule.

### Task 3: M5 search engine

**Files:**
- Create: `include/cadopt/search.hpp`
- Create: `src/search.cpp`
- Create: `tests/test_search.cpp`
- Create: `python/cadopt_lab/search.py`
- Create: `python/cadopt_lab/qubo.py`
- Create: `python/cadopt_lab/experiment.py`
- Modify: `CMakeLists.txt`
- Modify: `README.md`
- Create: `docs/MILESTONE_5.md`

**Interfaces:**
- Consumes `CandidateSet` from Task 2.
- Produces valid candidate plans and persisted experiment records.

- [ ] Add tests for overlap/conflict rejection, complete coverage, deterministic tie-breaking, and exact-cost precedence over estimated cost.
- [ ] Implement deterministic greedy/beam/exhaustive baselines for bounded candidate sets.
- [ ] Implement Python simulated annealing and genetic search with deterministic seeds.
- [ ] Implement QUBO model export with binary variables, coverage penalties, conflict penalties, and estimated-byte objective.
- [ ] Persist JSONL experiment records separating estimated bytes, exact bytes, validity, trace summary, runtime, and seed.
- [ ] Document that quantum-inspired solvers are optional competitors, not hard-coded winners.

### Task 4: M6 backend capability and exact DWG evaluator

**Files:**
- Modify: `include/cadopt/dwg_backend.hpp`
- Modify: `src/dwg_backend.cpp`
- Create: `include/cadopt/evaluator.hpp`
- Create: `src/evaluator.cpp`
- Create: `tests/test_evaluator.cpp`
- Modify: `src/main.cpp`
- Modify: `CMakeLists.txt`
- Modify: `README.md`
- Create: `docs/MILESTONE_6.md`

**Interfaces:**
- Consumes candidate plans from Task 3 and reconstruction recipes from Task 2.
- Produces exact DWG byte measurements and verified winner selection.

- [ ] Add backend capability metadata and fail-closed validation.
- [ ] Keep generic command backend; add named ODA/RealDWG-compatible command profiles without shipping proprietary binaries.
- [ ] Reconstruct final candidate to source-equivalent DXF entity semantics before serialization.
- [ ] Serialize candidate to DWG, measure actual filesystem bytes, reverse-convert DWG to DXF, and run independent verification.
- [ ] Reject any candidate with backend failure, missing output, unresolved fatal trace, or geometry/semantic/interaction mismatch.
- [ ] Add research-run CLI mode that evaluates multiple candidate plans and copies/selects the smallest valid DWG.
- [ ] Emit a machine-readable final report containing source bytes, each evaluated candidate, exact DWG bytes, validity, failure cause, and chosen winner.
- [ ] Document licensed-backend expectations and Linux usage.

### Task 5: Integration documentation and PR-time verification checklist

**Files:**
- Modify: `README.md`
- Create: `docs/M3_M6_VERIFICATION.md`

- [ ] Document end-to-end commands for Linux.
- [ ] Document the strict success contract and known backend licensing boundary.
- [ ] Record PR-time commands: configure, build, CTest, dry-run, candidate search smoke test, and real DWG round trip when a backend is installed.
- [ ] Record expected fail-closed behavior when no backend is configured.
