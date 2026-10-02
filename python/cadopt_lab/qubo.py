"""QUBO export for optional quantum-inspired solvers.

The optimizer does not require a quantum backend. This module exports a plain QUBO
coefficient dictionary so external solvers can compete with classical search.
"""

from __future__ import annotations

from dataclasses import dataclass
import json


@dataclass(frozen=True)
class QuboModel:
    linear: dict[int, float]
    quadratic: dict[tuple[int, int], float]
    constant: float
    penalty: float

    def to_json(self) -> str:
        payload = {
            "linear": {str(k): v for k, v in self.linear.items()},
            "quadratic": {f"{a},{b}": value for (a, b), value in self.quadratic.items()},
            "constant": self.constant,
            "penalty": self.penalty,
        }
        return json.dumps(payload, sort_keys=True)


def build_exact_cover_qubo(candidate_set: dict, *, penalty: float | None = None) -> QuboModel:
    candidates = candidate_set.get("candidates", [])
    universe = list(candidate_set.get("source_universe", []))

    positive_cost_sum = sum(
        max(0.0, float(candidate.get("estimated_bytes", 0.0)))
        for candidate in candidates
    )
    # One violated exact-cover constraint must cost more than every positive
    # estimated candidate cost combined. This makes feasibility dominate the
    # cheap search proxy. Final validity is still rechecked by the C++ layer.
    minimum_safe_penalty = max(1.0, positive_cost_sum + 1.0)
    p = float(penalty if penalty is not None else minimum_safe_penalty)
    if p <= positive_cost_sum:
        raise ValueError(
            "QUBO penalty must exceed the sum of positive estimated candidate costs"
        )

    linear: dict[int, float] = {}
    quadratic: dict[tuple[int, int], float] = {}
    constant = 0.0

    for index, candidate in enumerate(candidates):
        linear[index] = float(candidate.get("estimated_bytes", 0.0))
        if candidate.get("preserves_selection_cardinality", True) is False:
            # Invalid candidates remain visible in exported variable numbering,
            # but selecting one incurs a dominating hard-constraint penalty.
            linear[index] += p

    for source_id in universe:
        covering = [
            index
            for index, candidate in enumerate(candidates)
            if source_id in candidate.get("source_ids", [])
        ]
        if not covering:
            raise ValueError(f"source {source_id!r} has no covering candidate")
        # P * (sum(x_i) - 1)^2
        #   = P * (1 - sum(x_i) + 2 * sum_{i<j}(x_i*x_j))
        constant += p
        for index in covering:
            linear[index] = linear.get(index, 0.0) - p
        for pos, a in enumerate(covering):
            for b in covering[pos + 1 :]:
                key = (min(a, b), max(a, b))
                quadratic[key] = quadratic.get(key, 0.0) + 2.0 * p

    id_to_index = {
        candidate.get("id", str(index)): index
        for index, candidate in enumerate(candidates)
    }
    applied_conflicts: set[tuple[int, int]] = set()
    for index, candidate in enumerate(candidates):
        for conflict_id in candidate.get("conflicts", []):
            other = id_to_index.get(conflict_id)
            if other is None or other == index:
                continue
            key = (min(index, other), max(index, other))
            if key in applied_conflicts:
                continue
            applied_conflicts.add(key)
            quadratic[key] = quadratic.get(key, 0.0) + p

    return QuboModel(linear=linear, quadratic=quadratic, constant=constant, penalty=p)
