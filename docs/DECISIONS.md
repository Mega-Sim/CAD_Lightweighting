# Architecture decisions carried from the research discussion

1. Input is fixed to DXF; output is DWG.
2. C++ owns source truth, geometry semantics, reconstruction, output, and independent verification.
3. Python owns later experimental representations and search/orchestration (tensor, wavelet, spectral, neural, Bayesian/GA/QUBO/quantum-inspired, etc.).
4. The optimizer may use any intermediate form and may temporarily merge/group/rotate/translate/normalize data, provided the final CAD document restores the original practical editing/selection behavior.
5. Visual similarity alone is insufficient. Geometry, CAD semantics, selection/editing granularity, layers/blocks/groups/attributes/references and other practical behavior must be preserved according to the source.
6. Whole-drawing, structural, repeated-pattern, and partial/local scopes are peers. No one scope is architecturally privileged.
7. Lossy methods are not excluded in research. A candidate must expose its residual/error and must be independently validated after reconstruction.
8. The optimization target is the number of bytes in the actual generated DWG, not the size of a temporary tensor/archive/IR.
9. Processing time is not a primary constraint in the research phase; broad multi-method search is allowed.
10. Every transform must be traceable back to source entities so the exact cause of loss can be identified.
11. Display lag/performance optimization is a later, separate objective. Performance metrics may be recorded but do not drive the current file-size objective.
12. Patented techniques are not automatically excluded from research; IP/licensing is treated separately from technical evaluation.
