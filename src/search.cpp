#include <cadopt/search.hpp>

#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace cadopt {
namespace {

std::string escape_json(const std::string& text) {
    std::ostringstream out;
    for (char c : text) {
        if (c == '"') out << "\\\"";
        else if (c == '\\') out << "\\\\";
        else if (c == '\n') out << "\\n";
        else out << c;
    }
    return out.str();
}

std::unordered_map<std::string, std::size_t> raw_candidates(const CandidateSet& set) {
    std::unordered_map<std::string, std::size_t> result;
    for (std::size_t i = 0; i < set.candidates.size(); ++i) {
        const auto& candidate = set.candidates[i];
        if (candidate.kind != CandidateKind::Raw || candidate.source_ids.size() != 1) continue;
        result.emplace(candidate.source_ids.front(), i);
    }
    return result;
}

double raw_cost_for(const CandidateSet& set,
                    const std::unordered_map<std::string, std::size_t>& raw,
                    const std::vector<std::string>& sources) {
    double total = 0.0;
    for (const auto& source : sources) {
        const auto it = raw.find(source);
        if (it == raw.end()) return 0.0;
        total += set.candidates[it->second].estimated_bytes;
    }
    return total;
}

bool overlaps(const CandidateRecipe& candidate, const std::unordered_set<std::string>& covered) {
    return std::any_of(candidate.source_ids.begin(), candidate.source_ids.end(), [&](const auto& id) {
        return covered.contains(id);
    });
}

void fill_raw_fallback(const CandidateSet& set,
                       const std::unordered_map<std::string, std::size_t>& raw,
                       std::vector<std::size_t>& selected,
                       const std::unordered_set<std::string>& covered) {
    for (const auto& source : set.source_universe) {
        if (covered.contains(source)) continue;
        const auto it = raw.find(source);
        if (it != raw.end()) selected.push_back(it->second);
    }
}

std::string key_for_indices(std::vector<std::size_t> indices) {
    std::sort(indices.begin(), indices.end());
    std::ostringstream out;
    for (auto value : indices) out << value << ',';
    return out.str();
}

double provisional_cost(const CandidateSet& set,
                        const std::vector<std::size_t>& selected,
                        const std::unordered_set<std::string>& covered,
                        const std::unordered_map<std::string, std::size_t>& raw) {
    double cost = 0.0;
    for (const auto index : selected) {
        if (index < set.candidates.size()) cost += set.candidates[index].estimated_bytes;
    }
    for (const auto& source : set.source_universe) {
        if (covered.contains(source)) continue;
        const auto it = raw.find(source);
        if (it != raw.end()) cost += set.candidates[it->second].estimated_bytes;
    }
    return cost;
}

} // namespace

CandidatePlan build_plan(const CandidateSet& set,
                         std::vector<std::size_t> candidate_indices,
                         std::string id) {
    CandidatePlan plan;
    plan.id = std::move(id);
    std::sort(candidate_indices.begin(), candidate_indices.end());
    candidate_indices.erase(std::unique(candidate_indices.begin(), candidate_indices.end()),
                            candidate_indices.end());
    plan.candidate_indices = std::move(candidate_indices);

    std::unordered_map<std::string, std::size_t> counts;
    std::unordered_set<std::string> selected_ids;
    bool valid = true;
    for (const auto index : plan.candidate_indices) {
        if (index >= set.candidates.size()) {
            valid = false;
            plan.issues.push_back("candidate index out of range: " + std::to_string(index));
            continue;
        }
        const auto& candidate = set.candidates[index];
        plan.estimated_bytes += candidate.estimated_bytes;
        selected_ids.insert(candidate.id);
        if (!candidate.preserves_selection_cardinality) {
            valid = false;
            plan.issues.push_back(candidate.id + " does not preserve selection cardinality");
        }
        for (const auto& source : candidate.source_ids) ++counts[source];
    }

    std::unordered_set<std::string> universe(set.source_universe.begin(), set.source_universe.end());
    for (const auto& [source, count] : counts) {
        if (!universe.contains(source)) {
            valid = false;
            plan.issues.push_back("plan covers unknown source: " + source);
        }
        if (count != 1) {
            valid = false;
            plan.issues.push_back("source coverage must equal one: " + source
                                  + " count=" + std::to_string(count));
        }
    }
    for (const auto& source : set.source_universe) {
        const auto it = counts.find(source);
        if (it == counts.end() || it->second != 1) {
            valid = false;
            plan.issues.push_back("source is not covered exactly once: " + source);
        }
    }

    for (const auto index : plan.candidate_indices) {
        if (index >= set.candidates.size()) continue;
        const auto& candidate = set.candidates[index];
        for (const auto& conflict : candidate.conflicts) {
            if (selected_ids.contains(conflict)) {
                valid = false;
                plan.issues.push_back(candidate.id + " conflicts with " + conflict);
            }
        }
    }

    plan.structurally_valid = valid;
    return plan;
}

CandidatePlan greedy_search(const CandidateSet& set) {
    const auto raw = raw_candidates(set);
    struct Ranked {
        std::size_t index{};
        double saving{};
        double saving_per_source{};
    };
    std::vector<Ranked> ranked;
    for (std::size_t i = 0; i < set.candidates.size(); ++i) {
        const auto& candidate = set.candidates[i];
        if (candidate.kind == CandidateKind::Raw || candidate.source_ids.empty()
            || !candidate.preserves_selection_cardinality) continue;
        const double raw_cost = raw_cost_for(set, raw, candidate.source_ids);
        if (raw_cost <= 0.0) continue;
        const double saving = raw_cost - candidate.estimated_bytes;
        if (saving <= 0.0) continue;
        ranked.push_back({i, saving, saving / static_cast<double>(candidate.source_ids.size())});
    }
    std::sort(ranked.begin(), ranked.end(), [](const Ranked& a, const Ranked& b) {
        if (a.saving_per_source != b.saving_per_source) return a.saving_per_source > b.saving_per_source;
        if (a.saving != b.saving) return a.saving > b.saving;
        return a.index < b.index;
    });

    std::vector<std::size_t> selected;
    std::unordered_set<std::string> covered;
    for (const auto& item : ranked) {
        const auto& candidate = set.candidates[item.index];
        if (overlaps(candidate, covered)) continue;
        selected.push_back(item.index);
        covered.insert(candidate.source_ids.begin(), candidate.source_ids.end());
    }
    fill_raw_fallback(set, raw, selected, covered);
    return build_plan(set, std::move(selected), "greedy");
}

std::vector<CandidatePlan> exhaustive_search(const CandidateSet& set,
                                             const SearchOptions& options) {
    const auto raw = raw_candidates(set);
    std::vector<std::size_t> non_raw;
    for (std::size_t i = 0; i < set.candidates.size(); ++i) {
        const auto& candidate = set.candidates[i];
        if (candidate.kind != CandidateKind::Raw && !candidate.source_ids.empty()
            && candidate.preserves_selection_cardinality) {
            non_raw.push_back(i);
        }
    }
    if (non_raw.size() > options.max_exhaustive_non_raw) {
        return beam_search(set, options);
    }

    std::vector<CandidatePlan> best;
    std::set<std::string> seen;
    std::vector<std::size_t> selected;
    std::unordered_set<std::string> covered;

    std::function<void(std::size_t)> visit = [&](std::size_t pos) {
        if (pos == non_raw.size()) {
            auto complete = selected;
            fill_raw_fallback(set, raw, complete, covered);
            const auto key = key_for_indices(complete);
            if (!seen.insert(key).second) return;
            auto plan = build_plan(set, std::move(complete), "exhaustive:" + std::to_string(seen.size()));
            if (!plan.structurally_valid) return;
            best.push_back(std::move(plan));
            std::sort(best.begin(), best.end(), [](const auto& a, const auto& b) {
                if (a.estimated_bytes != b.estimated_bytes) return a.estimated_bytes < b.estimated_bytes;
                return a.id < b.id;
            });
            if (best.size() > options.max_results) best.resize(options.max_results);
            return;
        }

        visit(pos + 1);

        const auto index = non_raw[pos];
        const auto& candidate = set.candidates[index];
        if (overlaps(candidate, covered)) return;
        selected.push_back(index);
        for (const auto& source : candidate.source_ids) covered.insert(source);
        visit(pos + 1);
        for (const auto& source : candidate.source_ids) covered.erase(source);
        selected.pop_back();
    };
    visit(0);

    if (best.empty()) best.push_back(greedy_search(set));
    return best;
}

std::vector<CandidatePlan> beam_search(const CandidateSet& set,
                                       const SearchOptions& options) {
    const auto raw = raw_candidates(set);
    std::vector<std::size_t> non_raw;
    for (std::size_t i = 0; i < set.candidates.size(); ++i) {
        if (set.candidates[i].kind != CandidateKind::Raw
            && !set.candidates[i].source_ids.empty()
            && set.candidates[i].preserves_selection_cardinality) {
            non_raw.push_back(i);
        }
    }
    std::sort(non_raw.begin(), non_raw.end(), [&](std::size_t a, std::size_t b) {
        const auto& ca = set.candidates[a];
        const auto& cb = set.candidates[b];
        const double aa = ca.estimated_bytes / static_cast<double>(ca.source_ids.size());
        const double bb = cb.estimated_bytes / static_cast<double>(cb.source_ids.size());
        if (aa != bb) return aa < bb;
        return a < b;
    });

    struct State {
        std::vector<std::size_t> selected;
        std::unordered_set<std::string> covered;
    };
    std::vector<State> beam(1);
    for (const auto index : non_raw) {
        std::vector<State> next;
        next.reserve(beam.size() * 2);
        for (const auto& state : beam) {
            next.push_back(state);
            if (!overlaps(set.candidates[index], state.covered)) {
                auto take = state;
                take.selected.push_back(index);
                take.covered.insert(set.candidates[index].source_ids.begin(),
                                    set.candidates[index].source_ids.end());
                next.push_back(std::move(take));
            }
        }
        std::sort(next.begin(), next.end(), [&](const State& a, const State& b) {
            const auto ca = provisional_cost(set, a.selected, a.covered, raw);
            const auto cb = provisional_cost(set, b.selected, b.covered, raw);
            if (ca != cb) return ca < cb;
            return a.selected < b.selected;
        });
        if (next.size() > options.beam_width) next.resize(options.beam_width);
        beam = std::move(next);
    }

    std::vector<CandidatePlan> result;
    std::set<std::string> seen;
    for (auto& state : beam) {
        fill_raw_fallback(set, raw, state.selected, state.covered);
        const auto key = key_for_indices(state.selected);
        if (!seen.insert(key).second) continue;
        auto plan = build_plan(set, std::move(state.selected), "beam:" + std::to_string(result.size()));
        if (plan.structurally_valid) result.push_back(std::move(plan));
    }
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        if (a.estimated_bytes != b.estimated_bytes) return a.estimated_bytes < b.estimated_bytes;
        return a.id < b.id;
    });
    if (result.size() > options.max_results) result.resize(options.max_results);
    if (result.empty()) result.push_back(greedy_search(set));
    return result;
}

void set_exact_evaluation(CandidatePlan& plan,
                          std::uintmax_t exact_dwg_bytes,
                          bool valid,
                          std::string issue) {
    plan.exact_dwg_bytes = exact_dwg_bytes;
    plan.exact_valid = valid && plan.structurally_valid;
    if (!issue.empty()) plan.issues.push_back(std::move(issue));
    if (!plan.structurally_valid) plan.exact_valid = false;
}

const CandidatePlan* choose_best_exact_plan(const std::vector<CandidatePlan>& plans) {
    const CandidatePlan* best = nullptr;
    for (const auto& plan : plans) {
        if (!plan.structurally_valid || !plan.exact_valid || !plan.exact_dwg_bytes) continue;
        if (!best
            || *plan.exact_dwg_bytes < *best->exact_dwg_bytes
            || (*plan.exact_dwg_bytes == *best->exact_dwg_bytes
                && (plan.estimated_bytes < best->estimated_bytes
                    || (plan.estimated_bytes == best->estimated_bytes && plan.id < best->id)))) {
            best = &plan;
        }
    }
    return best;
}

std::string candidate_plan_to_json(const CandidatePlan& plan,
                                   const CandidateSet& set) {
    std::ostringstream out;
    out << "{\"id\":\"" << escape_json(plan.id)
        << "\",\"estimated_bytes\":" << plan.estimated_bytes
        << ",\"structurally_valid\":" << (plan.structurally_valid ? "true" : "false")
        << ",\"exact_valid\":" << (plan.exact_valid ? "true" : "false")
        << ",\"exact_dwg_bytes\":";
    if (plan.exact_dwg_bytes) out << *plan.exact_dwg_bytes;
    else out << "null";
    out << ",\"candidates\":[";
    for (std::size_t i = 0; i < plan.candidate_indices.size(); ++i) {
        if (i) out << ',';
        const auto index = plan.candidate_indices[i];
        if (index < set.candidates.size()) out << '"' << escape_json(set.candidates[index].id) << '"';
        else out << "null";
    }
    out << "],\"issues\":[";
    for (std::size_t i = 0; i < plan.issues.size(); ++i) {
        if (i) out << ',';
        out << '"' << escape_json(plan.issues[i]) << '"';
    }
    out << "]}";
    return out.str();
}

} // namespace cadopt
