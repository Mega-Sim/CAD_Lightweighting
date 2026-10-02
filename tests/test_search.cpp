#include <cadopt/search.hpp>

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

static void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

static cadopt::CandidateSet fixture() {
    cadopt::CandidateSet set;
    set.source_universe = {"a", "b", "c"};

    for (const auto& id : set.source_universe) {
        cadopt::CandidateRecipe raw;
        raw.id = "raw:" + id;
        raw.kind = cadopt::CandidateKind::Raw;
        raw.source_ids = {id};
        raw.estimated_bytes = 100.0;
        raw.preserves_selection_cardinality = true;
        set.candidates.push_back(raw);
    }

    cadopt::CandidateRecipe ab;
    ab.id = "ref:ab";
    ab.kind = cadopt::CandidateKind::ReferenceTransform;
    ab.source_ids = {"a", "b"};
    ab.estimated_bytes = 120.0;
    ab.preserves_selection_cardinality = true;
    set.candidates.push_back(ab);

    cadopt::CandidateRecipe bc;
    bc.id = "ref:bc";
    bc.kind = cadopt::CandidateKind::ReferenceTransform;
    bc.source_ids = {"b", "c"};
    bc.estimated_bytes = 130.0;
    bc.preserves_selection_cardinality = true;
    set.candidates.push_back(bc);
    return set;
}

static cadopt::CandidateSet conflict_fixture(bool include_nonraw_b) {
    cadopt::CandidateSet set;
    set.source_universe = {"a", "b"};

    cadopt::CandidateRecipe raw_a;
    raw_a.id = "raw:a";
    raw_a.kind = cadopt::CandidateKind::Raw;
    raw_a.source_ids = {"a"};
    raw_a.estimated_bytes = 100.0;
    raw_a.preserves_selection_cardinality = true;
    set.candidates.push_back(raw_a);

    cadopt::CandidateRecipe raw_b = raw_a;
    raw_b.id = "raw:b";
    raw_b.source_ids = {"b"};
    set.candidates.push_back(raw_b);

    cadopt::CandidateRecipe compact_a;
    compact_a.id = "compact:a";
    compact_a.kind = cadopt::CandidateKind::PrimitiveReductionProbe;
    compact_a.source_ids = {"a"};
    compact_a.estimated_bytes = 1.0;
    compact_a.conflicts = {"raw:b"};
    compact_a.preserves_selection_cardinality = true;
    set.candidates.push_back(compact_a);

    if (include_nonraw_b) {
        cadopt::CandidateRecipe compact_b;
        compact_b.id = "compact:b";
        compact_b.kind = cadopt::CandidateKind::PrimitiveReductionProbe;
        compact_b.source_ids = {"b"};
        compact_b.estimated_bytes = 5.0;
        compact_b.preserves_selection_cardinality = true;
        set.candidates.push_back(compact_b);
    }
    return set;
}

static void test_build_plan_rejects_overlap() {
    const auto set = fixture();
    const auto plan = cadopt::build_plan(set, {3, 4}, "overlap");
    require(!plan.structurally_valid, "overlapping candidate coverage must fail exact-cover validation");
}

static void test_greedy_prefers_beneficial_reference_and_fills_raw() {
    const auto set = fixture();
    const auto plan = cadopt::greedy_search(set);
    require(plan.structurally_valid, "greedy result must be an exact cover");
    require(plan.estimated_bytes < 300.0, "greedy should improve on all-raw estimate");
    require(plan.candidate_indices.size() == 2,
            "greedy should select one pair representation plus one raw fallback");
}

static void test_greedy_falls_back_to_valid_all_raw_when_raw_completion_conflicts() {
    const auto set = conflict_fixture(false);
    const auto plan = cadopt::greedy_search(set);
    require(plan.structurally_valid, "greedy must never return an invalid conflict plan");
    require(plan.candidate_indices.size() == 2,
            "safe fallback must restore one raw candidate per source");
    require(plan.estimated_bytes == 200.0,
            "conflicting compact candidate must not beat a valid exact-cover fallback");
}

static void test_exhaustive_can_replace_conflicting_raw_with_nonraw_candidate() {
    const auto set = conflict_fixture(true);
    cadopt::SearchOptions options;
    options.max_exhaustive_non_raw = 8;
    options.max_results = 16;
    const auto plans = cadopt::exhaustive_search(set, options);
    require(!plans.empty(), "exhaustive conflict fixture returned no valid plans");
    require(plans.front().structurally_valid, "best exhaustive plan must be valid");
    require(plans.front().estimated_bytes == 6.0,
            "exhaustive search must preserve a valid nonraw replacement for the conflicting raw fallback");
}

static void test_exhaustive_search_is_deterministic() {
    const auto set = fixture();
    cadopt::SearchOptions options;
    options.max_exhaustive_non_raw = 8;
    options.max_results = 16;
    const auto first = cadopt::exhaustive_search(set, options);
    const auto second = cadopt::exhaustive_search(set, options);
    require(!first.empty() && first.size() == second.size(), "exhaustive search result count changed");
    require(first.front().estimated_bytes == second.front().estimated_bytes,
            "exhaustive search must be deterministic");
}

static void test_exact_bytes_override_estimated_bytes() {
    auto set = fixture();
    auto plans = cadopt::exhaustive_search(set);
    require(plans.size() >= 2, "fixture needs at least two search plans");

    plans[0].estimated_bytes = 1.0;
    plans[1].estimated_bytes = 1000.0;
    cadopt::set_exact_evaluation(plans[0], 500, true);
    cadopt::set_exact_evaluation(plans[1], 300, true);
    const auto* best = cadopt::choose_best_exact_plan(plans);
    require(best == &plans[1], "actual DWG bytes must outrank estimated cost");
}

int main() {
    try {
        test_build_plan_rejects_overlap();
        test_greedy_prefers_beneficial_reference_and_fills_raw();
        test_greedy_falls_back_to_valid_all_raw_when_raw_completion_conflicts();
        test_exhaustive_can_replace_conflicting_raw_with_nonraw_candidate();
        test_exhaustive_search_is_deterministic();
        test_exact_bytes_override_estimated_bytes();
        std::cout << "cadopt_search_tests: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "cadopt_search_tests: FAIL: " << error.what() << '\n';
        return 1;
    }
}
