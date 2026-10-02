from __future__ import annotations

from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "python"))

from cadopt_lab.qubo import build_exact_cover_qubo
from cadopt_lab.search import (
    beam_search,
    bounded_exhaustive_search,
    complete_with_raw,
    genetic_search,
    simulated_annealing,
)


def candidate_set() -> dict:
    return {
        "source_universe": ["a", "b"],
        "candidates": [
            {
                "id": "raw-a",
                "kind": "raw",
                "source_ids": ["a"],
                "estimated_bytes": 100.0,
                "preserves_selection_cardinality": True,
                "conflicts": [],
            },
            {
                "id": "raw-b",
                "kind": "raw",
                "source_ids": ["b"],
                "estimated_bytes": 100.0,
                "preserves_selection_cardinality": True,
                "conflicts": [],
            },
            {
                "id": "pair",
                "kind": "reference_transform",
                "source_ids": ["a", "b"],
                "estimated_bytes": 40.0,
                "preserves_selection_cardinality": True,
                "conflicts": [],
            },
            {
                "id": "bad-a",
                "kind": "reference_transform",
                "source_ids": ["a"],
                "estimated_bytes": 1.0,
                "preserves_selection_cardinality": True,
                "conflicts": ["raw-b"],
            },
        ],
    }


class SearchTests(unittest.TestCase):
    def test_complete_with_raw_rejects_declared_conflict(self) -> None:
        data = candidate_set()
        plan = complete_with_raw(data, [3])
        self.assertIsNone(plan)

    def test_bounded_exhaustive_finds_lowest_proxy_exact_cover(self) -> None:
        plans = bounded_exhaustive_search(candidate_set(), max_non_raw=8, max_results=8)
        self.assertTrue(plans)
        self.assertEqual(plans[0].estimated_bytes, 40.0)
        self.assertEqual(plans[0].candidate_indices, (2,))

    def test_beam_search_keeps_valid_exact_covers(self) -> None:
        plans = beam_search(candidate_set(), beam_width=4, max_results=4)
        self.assertTrue(plans)
        self.assertEqual(plans[0].estimated_bytes, 40.0)
        for plan in plans:
            self.assertIsNotNone(complete_with_raw(candidate_set(), plan.candidate_indices))

    def test_simulated_annealing_is_deterministic_and_finds_compact_exact_cover(self) -> None:
        data = candidate_set()
        first = simulated_annealing(data, iterations=1000, seed=17)
        second = simulated_annealing(data, iterations=1000, seed=17)
        self.assertEqual(first, second)
        self.assertLessEqual(first.estimated_bytes, 200.0)
        self.assertIsNotNone(complete_with_raw(data, first.candidate_indices))

    def test_genetic_search_is_deterministic_and_keeps_exact_cover(self) -> None:
        data = candidate_set()
        first = genetic_search(data, generations=80, population_size=24, seed=9)
        second = genetic_search(data, generations=80, population_size=24, seed=9)
        self.assertEqual(first, second)
        self.assertLessEqual(first.estimated_bytes, 200.0)
        self.assertIsNotNone(complete_with_raw(data, first.candidate_indices))

    def test_qubo_default_penalty_dominates_all_positive_costs(self) -> None:
        data = candidate_set()
        model = build_exact_cover_qubo(data)
        total_positive = sum(max(0.0, c["estimated_bytes"]) for c in data["candidates"])
        self.assertGreater(model.penalty, total_positive)

    def test_qubo_rejects_unsafe_user_penalty(self) -> None:
        with self.assertRaises(ValueError):
            build_exact_cover_qubo(candidate_set(), penalty=1.0)


if __name__ == "__main__":
    unittest.main()
