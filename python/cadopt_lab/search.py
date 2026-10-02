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


def _declared_conflict(candidate_set: dict, indices: Sequence[int]) -> bool:
    selected_ids = {
        candidate_set["candidates"][index].get("id", str(index))
        for index in indices
    }
    for index in indices:
        candidate = candidate_set["candidates"][index]
        if any(conflict in selected_ids for conflict in candidate.get("conflicts", [])):
            return True
    return False


def _partial_valid(candidate_set: dict, indices: Iterable[int]) -> bool:
    selected = sorted(set(indices))
    candidates = candidate_set.get("candidates", [])
    if any(index < 0 or index >= len(candidates) for index in selected):
        return False
    if any(candidates[index].get("preserves_selection_cardinality", True) is False for index in selected):
        return False
    return not _has_overlap(candidate_set, selected) and not _declared_conflict(candidate_set, selected)


def complete_with_raw(candidate_set: dict, non_raw_indices: Iterable[int]) -> SearchPlan | None:
    selected = sorted(set(non_raw_indices))
    if not _partial_valid(candidate_set, selected):
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
    if not _partial_valid(candidate_set, selected):
        return None
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


def _plan_order(plan: SearchPlan) -> tuple[float, tuple[int, ...]]:
    return plan.estimated_bytes, plan.candidate_indices


def _partial_proxy_cost(candidate_set: dict, indices: Iterable[int]) -> float:
    selected = sorted(set(indices))
    selected_cost = sum(
        float(candidate_set["candidates"][index].get("estimated_bytes", 0.0))
        for index in selected
    )
    raw = _raw_by_source(candidate_set)
    covered = _covered_sources(candidate_set, selected)
    fallback = 0.0
    for source_id in candidate_set.get("source_universe", []):
        if source_id in covered:
            continue
        raw_index = raw.get(source_id)
        if raw_index is not None:
            fallback += float(candidate_set["candidates"][raw_index].get("estimated_bytes", 0.0))
    return selected_cost + fallback


def bounded_exhaustive_search(
    candidate_set: dict,
    *,
    max_non_raw: int = 20,
    max_results: int = 128,
) -> list[SearchPlan]:
    """Enumerate non-overlapping/conflict-free non-raw subsets when tractable."""
    available = _non_raw_indices(candidate_set)
    if len(available) > max_non_raw:
        return beam_search(candidate_set, beam_width=max(16, max_results), max_results=max_results)

    results: dict[tuple[int, ...], SearchPlan] = {}

    def visit(position: int, selected: tuple[int, ...]) -> None:
        if position == len(available):
            plan = complete_with_raw(candidate_set, selected)
            if plan is not None:
                results[plan.candidate_indices] = plan
            return
        visit(position + 1, selected)
        index = available[position]
        proposal = selected + (index,)
        if _partial_valid(candidate_set, proposal):
            visit(position + 1, proposal)

    visit(0, ())
    ordered = sorted(results.values(), key=_plan_order)
    return ordered[: max(1, max_results)]


def beam_search(
    candidate_set: dict,
    *,
    beam_width: int = 64,
    max_results: int = 128,
) -> list[SearchPlan]:
    """Keep the lowest proxy-cost valid partial states, then exact-cover them."""
    available = _non_raw_indices(candidate_set)
    beam: list[tuple[int, ...]] = [()]
    width = max(1, beam_width)

    for index in available:
        proposals: set[tuple[int, ...]] = set(beam)
        for state in beam:
            candidate = tuple(sorted(state + (index,)))
            if _partial_valid(candidate_set, candidate):
                proposals.add(candidate)
        scored = sorted(
            ((_partial_proxy_cost(candidate_set, state), state) for state in proposals),
            key=lambda item: (item[0], item[1]),
        )
        beam = [state for _, state in scored[:width]]

    results: dict[tuple[int, ...], SearchPlan] = {}
    for state in beam:
        plan = complete_with_raw(candidate_set, state)
        if plan is not None:
            results[plan.candidate_indices] = plan
    return sorted(results.values(), key=_plan_order)[: max(1, max_results)]


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
        chosen_id = candidate_set["candidates"][chosen].get("id", str(chosen))
        chosen_conflicts = set(candidate_set["candidates"][chosen].get("conflicts", []))
        conflicts = set()
        for index in result:
            existing = candidate_set["candidates"][index]
            existing_sources = set(existing.get("source_ids", []))
            existing_id = existing.get("id", str(index))
            existing_conflicts = set(existing.get("conflicts", []))
            if candidate_sources.intersection(existing_sources):
                conflicts.add(index)
            elif existing_id in chosen_conflicts or chosen_id in existing_conflicts:
                conflicts.add(index)
        result.difference_update(conflicts)
        result.add(chosen)
    return result if _partial_valid(candidate_set, result) else set(state)


def simulated_annealing(
    candidate_set: dict,
    *,
    iterations: int = 5000,
    seed: int = 0,
    initial_temperature_fraction: float = 0.05,
    final_temperature_fraction: float = 1.0e-6,
) -> SearchPlan:
    """Search estimated-byte space with a cost-scaled annealing schedule."""
    if initial_temperature_fraction <= 0.0 or final_temperature_fraction <= 0.0:
        raise ValueError("temperature fractions must be positive")

    rng = random.Random(seed)
    state: set[int] = set()
    current = complete_with_raw(candidate_set, state)
    if current is None:
        raise ValueError("candidate set has no raw exact-cover fallback")
    best = current

    raw_scale = max(1.0, current.estimated_bytes)
    start_temperature = max(1.0e-12, raw_scale * initial_temperature_fraction)
    end_temperature = max(1.0e-12, raw_scale * final_temperature_fraction)
    steps = max(1, iterations)

    for step in range(steps):
        proposal_state = _mutate(candidate_set, state, rng)
        proposal = complete_with_raw(candidate_set, proposal_state)
        if proposal is None:
            # Partial state may only become final-valid after adding another
            # candidate. Score it with the optimistic raw proxy but do not make
            # it the final best plan yet.
            continue

        progress = step / max(1, steps - 1)
        temperature = start_temperature * ((end_temperature / start_temperature) ** progress)
        delta = proposal.estimated_bytes - current.estimated_bytes
        if delta <= 0.0 or rng.random() < math.exp(-delta / max(temperature, 1.0e-12)):
            state = proposal_state
            current = proposal
        if _plan_order(current) < _plan_order(best):
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
                proposal = set(state)
                proposal.add(index)
                if _partial_valid(candidate_set, proposal):
                    state = proposal
        return state

    population = [random_state() for _ in range(max(2, population_size))]
    best = fallback
    for _ in range(max(1, generations)):
        scored: list[tuple[float, set[int], SearchPlan | None]] = []
        for state in population:
            plan = complete_with_raw(candidate_set, state)
            score = plan.estimated_bytes if plan is not None else _partial_proxy_cost(candidate_set, state)
            scored.append((score, state, plan))
            if plan is not None and _plan_order(plan) < _plan_order(best):
                best = plan
        scored.sort(key=lambda item: (item[0], tuple(sorted(item[1]))))
        elite_count = max(2, min(len(scored), max(2, population_size // 4)))
        elites = [set(item[1]) for item in scored[:elite_count]]
        next_population = elites[:]
        while len(next_population) < max(2, population_size):
            a = rng.choice(elites)
            b = rng.choice(elites)
            child = set(index for index in a.union(b) if rng.random() < 0.5)
            ordered = sorted(
                child,
                key=lambda idx: (
                    float(candidate_set["candidates"][idx].get("estimated_bytes", 0.0))
                    / max(1, len(candidate_set["candidates"][idx].get("source_ids", []))),
                    idx,
                ),
            )
            repaired: set[int] = set()
            for index in ordered:
                proposal = set(repaired)
                proposal.add(index)
                if _partial_valid(candidate_set, proposal):
                    repaired = proposal
            if rng.random() < 0.35:
                repaired = _mutate(candidate_set, repaired, rng)
            next_population.append(repaired)
        population = next_population
    return best
