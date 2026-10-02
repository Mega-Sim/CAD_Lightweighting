#pragma once

#include <cadopt/candidate.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace cadopt {

struct CandidatePlan {
    std::string id;
    std::vector<std::size_t> candidate_indices;
    double estimated_bytes{};
    std::optional<std::uintmax_t> exact_dwg_bytes;
    bool structurally_valid{};
    bool exact_valid{};
    std::vector<std::string> issues;
};

struct SearchOptions {
    std::size_t beam_width{64};
    std::size_t max_exhaustive_non_raw{20};
    std::size_t max_results{128};
};

CandidatePlan build_plan(const CandidateSet& set,
                         std::vector<std::size_t> candidate_indices,
                         std::string id);
CandidatePlan greedy_search(const CandidateSet& set);
std::vector<CandidatePlan> exhaustive_search(const CandidateSet& set,
                                             const SearchOptions& options = {});
std::vector<CandidatePlan> beam_search(const CandidateSet& set,
                                       const SearchOptions& options = {});
void set_exact_evaluation(CandidatePlan& plan,
                          std::uintmax_t exact_dwg_bytes,
                          bool valid,
                          std::string issue = {});
const CandidatePlan* choose_best_exact_plan(const std::vector<CandidatePlan>& plans);
std::string candidate_plan_to_json(const CandidatePlan& plan,
                                   const CandidateSet& set);

} // namespace cadopt
