"""Long-running research search strategies for candidate plans.

The C++ layer is authoritative for final validity and DWG byte measurement. This
module only explores candidate combinations using the cheap estimated cost.
"""

from __future__ import annotations

from dataclasses import dataclass
import math
import random
from typing import Iterable, Sequence


@dataclass(frozen=True)
class SearchPlan:
    candidate_indices: tuple[int, ...]
    estimated_bytes: float


def _raw_by_source(candidate_set: dict) -> dict[str, int]:
    raw: dict[str, int] = {}
    for index, candidate in enumerate(candidate_set.get("candidates", [])):
        source_ids = candidate.get("source_ids", [])
        if candidate.get("kind") == "raw" and len(source_ids) == 1:
            raw[source_ids[0]] = index
    return raw


def _covered_sources(candidate_set: dict, indices: Iterable[int]) -> set[str]:
    covered: set[str] = set()
    for index in indices:
        covered.update(candidate_set["candidates"][index].get("source_ids", []))
    return covered


def _has_overlap(candidate_set: dict, indices: Sequence[int]) -> bool:
    seen: set[str] = set()
    for index in indices:
        for source_id in candidate_set["candidates"][index].get("source_ids", []):
            if source_id in seen:
                return True
            seen.add(source_id)
    return False


def complete_with_raw(candidate_set: dict, non_raw_indices: Iterable[int]) -> SearchPlan | None:
    selected = sorted(set(non_raw_indices))
    if _has_overlap(candidate_set, selected):
        return None
    universe = list(candidate_set.get("source_universe", []))
    raw = _raw_by_source(candidate_set)
    covered = _covered_sources(candidate_set, selected)
    for source_id in universe:
        if source_id in covered:
            continue
        raw_index = raw.get(source_id)
        if raw_index is None:
            return None
        selected.append(raw_index)
        covered.add(source_id)
    if covered != set(universe):
        return None
    selected = sorted(set(selected))
    cost = sum(float(candidate_set["candidates"][i].get("estimated_bytes", 0.0)) for i in selected)
    return SearchPlan(tuple(selected), cost)


def _non_raw_indices(candidate_set: dict) -> list[int]:
    result: list[int] = []
    for index, candidate in enumerate(candidate_set.get("candidates", [])):
        if candidate.get("kind") == "raw":
            continue
        if not candidate.get("source_ids"):
            continue
        if candidate.get("preserves_selection_cardinality", True) is False:
            continue
        result.append(index)
    return result


def _mutate(candidate_set: dict, state: set[int], rng: random.Random) -> set[int]:
    available = _non_raw_indices(candidate_set)
    if not available:
        return set()
    result = set(state)
    chosen = rng.choice(available)
    if chosen in result:
        result.remove(chosen)
    else:
        candidate_sources = set(candidate_set["candidates"][chosen].get("source_ids", []))
        conflicts = {
            index
            for index in result
            if candidate_sources.intersection(candidate_set["candidates"][index].get("source_ids", []))
        }
        result.difference_update(conflicts)
        result.add(chosen)
    return result


def simulated_annealing(candidate_set: dict, *, iterations: int = 5000, seed: int = 0) -> SearchPlan:
    rng = random.Random(seed)
    state: set[int] = set()
    current = complete_with_raw(candidate_set, state)
    if current is None:
        raise ValueError("candidate set has no raw exact-cover fallback")
    best = current

    for step in range(max(1, iterations)):
        proposal_state = _mutate(candidate_set, state, rng)
        proposal = complete_with_raw(candidate_set, proposal_state)
        if proposal is None:
            continue
        temperature = max(1e-9, 1.0 - step / max(1, iterations))
        delta = proposal.estimated_bytes - current.estimated_bytes
        if delta <= 0.0 or rng.random() < math.exp(-delta / max(temperature, 1e-9)):
            state = proposal_state
            current = proposal
        if current.estimated_bytes < best.estimated_bytes:
            best = current
    return best


def genetic_search(
    candidate_set: dict,
    *,
    generations: int = 250,
    population_size: int = 64,
    seed: int = 0,
) -> SearchPlan:
    rng = random.Random(seed)
    available = _non_raw_indices(candidate_set)
    fallback = complete_with_raw(candidate_set, [])
    if fallback is None:
        raise ValueError("candidate set has no raw exact-cover fallback")
    if not available:
        return fallback

    def random_state() -> set[int]:
        state: set[int] = set()
        shuffled = available[:]
        rng.shuffle(shuffled)
        for index in shuffled:
            if rng.random() < 0.35:
                sources = set(candidate_set["candidates"][index].get("source_ids", []))
                if any(sources.intersection(candidate_set["candidates"][other].get("source_ids", [])) for other in state):
                    continue
                state.add(index)
        return state

    population = [random_state() for _ in range(max(2, population_size))]
    best = fallback
    for _ in range(max(1, generations)):
        scored: list[tuple[float, set[int], SearchPlan]] = []
        for state in population:
            plan = complete_with_raw(candidate_set, state)
            if plan is None:
                continue
            scored.append((plan.estimated_bytes, state, plan))
            if plan.estimated_bytes < best.estimated_bytes:
                best = plan
        if not scored:
            population = [random_state() for _ in range(max(2, population_size))]
            continue
        scored.sort(key=lambda item: (item[0], tuple(sorted(item[1]))))
        elite_count = max(2, min(len(scored), population_size // 4))
        elites = [set(item[1]) for item in scored[:elite_count]]
        next_population = elites[:]
        while len(next_population) < population_size:
            a = rng.choice(elites)
            b = rng.choice(elites)
            child = set(index for index in a.union(b) if rng.random() < 0.5)
            # Repair overlap deterministically by lower estimated cost per covered source.
            ordered = sorted(
                child,
                key=lambda idx: (
                    float(candidate_set["candidates"][idx].get("estimated_bytes", 0.0))
                    / max(1, len(candidate_set["candidates"][idx].get("source_ids", []))),
                    idx,
                ),
            )
            repaired: set[int] = set()
            used: set[str] = set()
            for index in ordered:
                sources = set(candidate_set["candidates"][index].get("source_ids", []))
                if used.intersection(sources):
                    continue
                repaired.add(index)
                used.update(sources)
            if rng.random() < 0.35:
                repaired = _mutate(candidate_set, repaired, rng)
            next_population.append(repaired)
        population = next_population
    return best
