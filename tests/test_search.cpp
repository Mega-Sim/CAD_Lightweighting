#include <cadopt/search.hpp>

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
        test_exhaustive_search_is_deterministic();
        test_exact_bytes_override_estimated_bytes();
        std::cout << "cadopt_search_tests: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "cadopt_search_tests: FAIL: " << error.what() << '\n';
        return 1;
    }
}
