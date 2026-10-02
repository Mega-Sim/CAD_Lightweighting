# Milestone 2 — Loss / Difference Trace Foundation

Issue: #1  
Branch: `feature/milestone-2-loss-trace`

## Purpose

Milestone 2 is the safety gate that must exist before reversible or lossy optimization experiments begin. The optimizer may later merge, rotate, compress, tensorize, approximate, or otherwise rewrite intermediate data, but a final DWG is valid only if the customer-visible CAD document behaves like the source.

The verifier therefore distinguishes three hard requirements:

1. **Geometry** — measurable geometry remains equivalent within explicit tolerances.
2. **Semantic structure** — layers, object records, XDATA, block definitions and references remain equivalent.
3. **Interaction / selection semantics** — source selection units and grouping/block relationships are not silently changed in the final document.

Intermediate representation is unrestricted; final reconstruction is not.

## Implemented M2 data indexing

The immutable DXF source index now records:

- ENTITIES selection units: type, handle, layer, INSERT target, XDATA applications and record span.
- BLOCK definitions: stable source ID, name, handle and complete record span including contained block geometry.
- OBJECTS records: type, handle, owner, GROUP member handles, XDATA applications and record span.

No optimizer mutation API is introduced in M2.

## Geometry-normalized comparison

LINE, ARC, CIRCLE, LWPOLYLINE and TEXT geometry fields are parsed numerically for verification. Numeric text representation differences such as `1.0` versus `1.000000` do not by themselves count as geometry loss.

Comparison uses:

- absolute tolerance,
- relative tolerance,
- angle wrap handling for angular group codes,
- maximum absolute deviation metrics when a mismatch occurs.

Non-geometry group-code/value content is compared separately so formatting normalization cannot hide semantic changes.

## Interaction checks

The M2 verifier rejects changes to:

- ENTITIES count,
- entity type / selection granularity,
- INSERT target block,
- BLOCK definition presence/content,
- GROUP member selection set.

This means a later optimizer may temporarily merge twenty independent LINE entities internally, but final reconstruction must restore twenty independent LINE selection units if that was the source behavior.

## XDATA / OBJECTS checks

Entity and object XDATA streams are compared independently from normal geometry records. OBJECTS are indexed and compared, with GROUP reference membership resolved back to source entity signatures where possible.

## Loss trace

Every source entity/block/object has a trace identity. Verification issues are written back into the trace ledger with:

- category,
- severity,
- detail,
- optional metric name/value,
- source ID correlation.

Later optimization milestones must append their transform operation and parameters before reconstruction so a detected loss can be traced to the exact candidate operation that introduced it.

## Fail-closed behavior

- A geometry, semantic, interaction, reference, block/object, or XDATA error fails the candidate.
- DWG output is not considered valid without the independent DWG→DXF reverse verification path.
- Missing production DWG backend still fails closed; M2 does not fake DWG output.

## Development verification policy

This branch was implemented directly from the latest `main`. No older commit was used as a development base or reference.

Per current project workflow, build/test execution is intentionally deferred until a PR is requested. Until then, this branch is implementation-complete for the M2 code scope but is **not** claimed as compiled/tested.
