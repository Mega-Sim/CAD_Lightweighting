#include <cadopt/candidate.hpp>
#include <cadopt/dwg_backend.hpp>
#include <cadopt/dxf.hpp>
#include <cadopt/evaluator.hpp>
#include <cadopt/geometry.hpp>
#include <cadopt/input.hpp>
#include <cadopt/search.hpp>
#include <cadopt/trace.hpp>
#include <cadopt/verifier.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

struct Options {
    fs::path input;
    fs::path output;
    fs::path report{"cadopt_report.json"};
    fs::path work_directory;
    std::string dxf_to_dwg;
    std::string dwg_to_dxf;
    std::string backend_profile{"external"};
    bool dry_run{};
    bool research{};
    bool keep_artifacts{true};
    std::size_t max_plans{4};
    std::size_t max_search_plans{128};
    std::size_t beam_width{64};
    double geometry_absolute_tolerance{1.0e-9};
    double geometry_relative_tolerance{1.0e-9};
    std::vector<double> quantization_steps{1.0e-10, 5.0e-10, 1.0e-9};
};

struct ExactQueueDecision {
    std::string plan_id;
    std::string status;
    std::string materialization_key;
    std::string reason;
    double estimated_bytes{};
};

struct ExactEvaluationQueue {
    std::size_t exact_budget{};
    std::size_t supported_unique{};
    std::vector<std::size_t> selected_plan_indices;
    std::vector<ExactQueueDecision> decisions;
};

static std::string escape_json(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else out += c;
    }
    return out;
}

static std::size_t parse_size(const std::string& text, const char* name) {
    std::size_t pos = 0;
    const auto value = std::stoull(text, &pos);
    if (pos != text.size()) throw std::runtime_error(std::string("invalid value for ") + name);
    return static_cast<std::size_t>(value);
}

static double parse_nonnegative_double(const std::string& text, const char* name) {
    std::size_t pos = 0;
    const double value = std::stod(text, &pos);
    if (pos != text.size() || !std::isfinite(value) || value < 0.0) {
        throw std::runtime_error(std::string("invalid non-negative value for ") + name);
    }
    return value;
}

static std::vector<double> parse_positive_double_list(const std::string& text, const char* name) {
    std::vector<double> values;
    std::stringstream stream(text);
    std::string token;
    while (std::getline(stream, token, ',')) {
        if (token.empty()) continue;
        std::size_t pos = 0;
        const double value = std::stod(token, &pos);
        if (pos != token.size() || !std::isfinite(value) || value <= 0.0) {
            throw std::runtime_error(std::string("invalid positive value in ") + name + ": " + token);
        }
        values.push_back(value);
    }
    if (values.empty()) {
        throw std::runtime_error(std::string(name) + " must contain at least one positive value");
    }
    return values;
}

static cadopt::VerificationOptions verification_options(const Options& options) {
    cadopt::VerificationOptions result;
    result.absolute_geometry_tolerance = options.geometry_absolute_tolerance;
    result.relative_geometry_tolerance = options.geometry_relative_tolerance;
    return result;
}

static fs::path effective_work_directory(const Options& options) {
    return options.work_directory.empty()
        ? fs::temp_directory_path() / "cadopt_m6"
        : options.work_directory;
}

static bool same_target_path(const fs::path& a, const fs::path& b) {
    if (a.empty() || b.empty()) return false;
    std::error_code error;
    const bool a_exists = fs::exists(a, error) && !error;
    error.clear();
    const bool b_exists = fs::exists(b, error) && !error;
    if (a_exists && b_exists) {
        error.clear();
        if (fs::equivalent(a, b, error) && !error) return true;
    }
    error.clear();
    const auto aa = fs::absolute(a, error).lexically_normal();
    if (error) return false;
    error.clear();
    const auto bb = fs::absolute(b, error).lexically_normal();
    return !error && aa == bb;
}

static std::unique_ptr<cadopt::DwgBackend> configure_backend(const Options& options,
                                                             bool need_read_dwg,
                                                             bool need_write_dwg,
                                                             std::string& error) {
    if (!need_read_dwg && !need_write_dwg) return {};
    if (need_read_dwg && options.dwg_to_dxf.empty()) {
        error = "DWG input/verification requires --dwg-to-dxf";
        return {};
    }
    if (need_write_dwg && options.dxf_to_dwg.empty()) {
        error = "DWG output requires --dxf-to-dwg";
        return {};
    }

    const auto profile = cadopt::parse_dwg_backend_profile(options.backend_profile);
    auto backend = std::make_unique<cadopt::ExternalCommandDwgBackend>(
        options.dxf_to_dwg, options.dwg_to_dxf, profile);
    const auto caps = backend->capabilities();
    if (need_read_dwg && !caps.can_read_dwg) {
        error = "configured backend cannot read DWG: " + caps.name;
        return {};
    }
    if (need_write_dwg && !caps.can_write_dwg) {
        error = "configured backend cannot write DWG: " + caps.name;
        return {};
    }
    return backend;
}

static Options parse_args(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto value = [&](const char* name) -> std::string {
            if (++i >= argc) throw std::runtime_error(std::string("missing value for ") + name);
            return argv[i];
        };
        if (a == "--input" || a == "-i") o.input = value(a.c_str());
        else if (a == "--output" || a == "-o") o.output = value(a.c_str());
        else if (a == "--report") o.report = value(a.c_str());
        else if (a == "--work-dir") o.work_directory = value(a.c_str());
        else if (a == "--dxf-to-dwg") o.dxf_to_dwg = value(a.c_str());
        else if (a == "--dwg-to-dxf") o.dwg_to_dxf = value(a.c_str());
        else if (a == "--backend-profile") o.backend_profile = value(a.c_str());
        else if (a == "--max-plans") o.max_plans = parse_size(value(a.c_str()), "--max-plans");
        else if (a == "--max-search-plans") {
            o.max_search_plans = parse_size(value(a.c_str()), "--max-search-plans");
        }
        else if (a == "--beam-width") o.beam_width = parse_size(value(a.c_str()), "--beam-width");
        else if (a == "--geometry-abs-tol") {
            o.geometry_absolute_tolerance = parse_nonnegative_double(value(a.c_str()), "--geometry-abs-tol");
        }
        else if (a == "--geometry-rel-tol") {
            o.geometry_relative_tolerance = parse_nonnegative_double(value(a.c_str()), "--geometry-rel-tol");
        }
        else if (a == "--quantize-steps") {
            o.quantization_steps = parse_positive_double_list(value(a.c_str()), "--quantize-steps");
        }
        else if (a == "--cleanup-artifacts") o.keep_artifacts = false;
        else if (a == "--dry-run") o.dry_run = true;
        else if (a == "--research" || a == "--optimize") o.research = true;
        else if (a == "--help" || a == "-h") {
            std::cout
                << "cadopt --input input.dxf|input.dwg --output output.dwg [options]\n"
                << "  --dry-run                 Strict source -> DXF IR -> DXF safety check; DWG input is decoded first\n"
                << "  --research                Discover/search/materialize/evaluate representation plans\n"
                << "  --backend-profile NAME    external|oda-file-converter|oda-sdk|realdwg-host\n"
                << "  --dxf-to-dwg CMD          Converter command template with {input} and {output}\n"
                << "  --dwg-to-dxf CMD          DWG input/reverse command with {input} and {output}\n"
                << "  --max-plans N             Expensive exact DWG evaluations after preflight (default 4; 0=all)\n"
                << "  --max-search-plans N      Cheap M5 plan pool retained before exact preflight (default 128)\n"
                << "  --beam-width N            Beam width for broad candidate search (default 64)\n"
                << "  --geometry-abs-tol V      Absolute geometry verification tolerance (default 1e-9)\n"
                << "  --geometry-rel-tol V      Relative geometry verification tolerance (default 1e-9)\n"
                << "  --quantize-steps CSV      Whole-drawing numeric quantization probes\n"
                << "                           (default 1e-10,5e-10,1e-9)\n"
                << "  --work-dir DIR            Input/candidate artifact directory\n"
                << "  --cleanup-artifacts       Remove non-winning candidate artifacts\n"
                << "  --report FILE             Machine-readable JSON report\n";
            std::exit(0);
        } else {
            throw std::runtime_error("unknown argument: " + a);
        }
    }
    if (o.input.empty()) throw std::runtime_error("--input is required");
    if (!o.dry_run && o.output.empty()) throw std::runtime_error("--output is required unless --dry-run");
    if (!o.dry_run && cadopt::detect_cad_input_kind(o.output) != cadopt::CadInputKind::Dwg) {
        throw std::runtime_error("--output must use the .dwg extension");
    }
    return o;
}

static void write_basic_report(const fs::path& path,
                               const cadopt::VerificationReport& report,
                               const cadopt::TraceLedger& trace,
                               const std::string& phase,
                               const std::string& backend_message) {
    std::ofstream out(path, std::ios::trunc);
    if (!out) throw std::runtime_error("cannot write report: " + path.string());
    out << "{\n  \"milestone\": 6,\n"
        << "  \"phase\": \"" << escape_json(phase) << "\",\n"
        << "  \"backend\": \"" << escape_json(backend_message) << "\",\n"
        << "  \"verification\": " << report.to_json() << ",\n"
        << "  \"trace\": " << trace.to_json() << "\n}\n";
}

static void write_research_report(const fs::path& path,
                                  const Options& options,
                                  const cadopt::CandidateSet& candidates,
                                  const std::vector<cadopt::CandidatePlan>& plans,
                                  const ExactEvaluationQueue& queue,
                                  const cadopt::EvaluationReport& evaluation,
                                  const cadopt::TraceLedger& trace) {
    std::ofstream out(path, std::ios::trunc);
    if (!out) throw std::runtime_error("cannot write report: " + path.string());
    out << "{\n  \"milestone\": 6,\n"
        << "  \"phase\": \"research_exact_dwg_search\",\n"
        << "  \"geometry_tolerance\": {\"absolute\": " << options.geometry_absolute_tolerance
        << ", \"relative\": " << options.geometry_relative_tolerance << "},\n"
        << "  \"candidate_set\": " << cadopt::candidate_set_to_json(candidates) << ",\n"
        << "  \"search_plans\": [";
    for (std::size_t i = 0; i < plans.size(); ++i) {
        if (i) out << ',';
        out << "\n    " << cadopt::candidate_plan_to_json(plans[i], candidates);
    }
    if (!plans.empty()) out << '\n';
    out << "  ],\n  \"evaluation_queue\": {\"exact_budget\": " << queue.exact_budget
        << ", \"supported_unique\": " << queue.supported_unique
        << ", \"selected_count\": " << queue.selected_plan_indices.size()
        << ", \"decisions\": [";
    for (std::size_t i = 0; i < queue.decisions.size(); ++i) {
        if (i) out << ',';
        const auto& decision = queue.decisions[i];
        out << "\n    {\"plan_id\": \"" << escape_json(decision.plan_id)
            << "\", \"estimated_bytes\": " << decision.estimated_bytes
            << ", \"status\": \"" << escape_json(decision.status)
            << "\", \"materialization_key\": \""
            << escape_json(decision.materialization_key)
            << "\", \"reason\": \"" << escape_json(decision.reason) << "\"}";
    }
    if (!queue.decisions.empty()) out << '\n';
    out << "  ]},\n  \"evaluation\": " << evaluation.to_json() << ",\n"
        << "  \"trace\": " << trace.to_json() << "\n}\n";
}

static std::size_t count_xdata_owners(const cadopt::DxfDocument& doc) {
    std::size_t count = 0;
    for (const auto& entity : doc.entities()) if (!entity.xdata_apps.empty()) ++count;
    for (const auto& object : doc.objects()) if (!object.xdata_apps.empty()) ++count;
    return count;
}

static void seed_trace(const cadopt::DxfDocument& source, cadopt::TraceLedger& trace) {
    const auto document = trace.begin_entity("document", "DOCUMENT");
    trace.record(document, "source_ingest", "immutable_source_truth=true", false);

    const auto blocks = trace.begin_entity("blocks", "BLOCK_COLLECTION");
    trace.record(blocks, "source_ingest", "collection=true", false);
    const auto objects = trace.begin_entity("objects", "OBJECT_COLLECTION");
    trace.record(objects, "source_ingest", "collection=true", false);

    for (const auto& entity : source.entities()) {
        const auto id = trace.begin_entity(entity.source_id, entity.type);
        trace.record(id, "source_ingest", "selection_unit=true;immutable=true", false);
    }
    for (const auto& block : source.blocks()) {
        const auto id = trace.begin_entity(block.source_id, "BLOCK:" + block.name);
        trace.record(id, "source_ingest", "block_definition=true", false);
    }
    for (const auto& object : source.objects()) {
        const auto id = trace.begin_entity(object.source_id, "OBJECT:" + object.type);
        trace.record(id, "source_ingest", "object_semantics=true", false);
    }
}

static void trace_verification_failures(cadopt::TraceLedger& trace,
                                        const cadopt::VerificationReport& report) {
    cadopt::append_verification_issues_to_trace(trace, report);
    if (!report.issues.empty()) {
        std::cerr << "[CADOPT] verification issues=" << report.issues.size()
                  << " geometry=" << report.geometry_mismatches
                  << " interaction=" << report.interaction_mismatches
                  << " references=" << report.reference_mismatches
                  << " xdata=" << report.xdata_mismatches << "\n";
    }
}

static void trace_canonical_views(const std::vector<cadopt::GeometryView>& views,
                                  cadopt::TraceLedger& trace) {
    for (const auto& view : views) {
        if (!view.supported || !view.complete) {
            trace.record_for_source(view.source_id,
                                    "canonical_probe_skipped",
                                    "supported=false_or_incomplete=true",
                                    false,
                                    "none",
                                    "info");
            continue;
        }
        const auto transform = cadopt::make_canonical_transform(view, false);
        const auto drift = cadopt::max_roundtrip_drift(view, transform);
        trace.record_for_source(view.source_id,
                                "canonical_probe",
                                transform.description,
                                false,
                                "none",
                                "info",
                                "reversible transform round-trip drift",
                                "roundtrip_drift",
                                drift);
    }
}

static std::string plan_key(const cadopt::CandidatePlan& plan) {
    std::ostringstream out;
    for (const auto index : plan.candidate_indices) out << index << ',';
    return out.str();
}

static std::vector<std::size_t> raw_candidate_indices(const cadopt::CandidateSet& candidates) {
    std::vector<std::size_t> indices;
    for (std::size_t index = 0; index < candidates.candidates.size(); ++index) {
        if (candidates.candidates[index].kind == cadopt::CandidateKind::Raw) indices.push_back(index);
    }
    return indices;
}

static std::vector<cadopt::CandidatePlan> build_research_plans(
    const cadopt::CandidateSet& candidates,
    std::size_t beam_width,
    std::size_t max_search_plans) {

    cadopt::SearchOptions options;
    options.beam_width = std::max<std::size_t>(1, beam_width);
    options.max_results = std::max<std::size_t>(max_search_plans, 16);

    std::vector<cadopt::CandidatePlan> mandatory;
    std::vector<cadopt::CandidatePlan> optional;
    std::set<std::string> seen;

    auto add_mandatory = [&](cadopt::CandidatePlan plan) {
        const auto key = plan_key(plan);
        if (seen.insert(key).second) mandatory.push_back(std::move(plan));
    };
    auto add_optional = [&](cadopt::CandidatePlan plan) {
        const auto key = plan_key(plan);
        if (seen.insert(key).second) optional.push_back(std::move(plan));
    };

    const auto raw = raw_candidate_indices(candidates);
    if (!raw.empty()) add_mandatory(cadopt::build_plan(candidates, raw, "m6:raw-baseline"));

    for (std::size_t index = 0; index < candidates.candidates.size(); ++index) {
        const auto& candidate = candidates.candidates[index];
        if (candidate.kind != cadopt::CandidateKind::NumericQuantization) continue;
        add_mandatory(cadopt::build_plan(candidates, {index}, "m6:" + candidate.id));
    }

    add_optional(cadopt::greedy_search(candidates));
    for (auto& plan : cadopt::beam_search(candidates, options)) add_optional(std::move(plan));
    for (auto& plan : cadopt::exhaustive_search(candidates, options)) add_optional(std::move(plan));

    std::sort(mandatory.begin(), mandatory.end(), [](const auto& a, const auto& b) {
        if (a.id == "m6:raw-baseline") return true;
        if (b.id == "m6:raw-baseline") return false;
        if (a.estimated_bytes != b.estimated_bytes) return a.estimated_bytes < b.estimated_bytes;
        return a.id < b.id;
    });
    std::sort(optional.begin(), optional.end(), [](const auto& a, const auto& b) {
        if (a.estimated_bytes != b.estimated_bytes) return a.estimated_bytes < b.estimated_bytes;
        return a.id < b.id;
    });

    std::vector<cadopt::CandidatePlan> plans;
    plans.reserve(mandatory.size() + optional.size());
    for (auto& plan : mandatory) plans.push_back(std::move(plan));
    for (auto& plan : optional) plans.push_back(std::move(plan));

    if (max_search_plans != 0 && plans.size() > max_search_plans) plans.resize(max_search_plans);
    return plans;
}

static bool plan_aware_preflight(const cadopt::CandidateSet& candidates,
                                 const cadopt::CandidatePlan& plan,
                                 std::string& materialization_key,
                                 std::string& reason) {
    if (!plan.structurally_valid) {
        reason = "candidate plan is not a valid exact cover";
        return false;
    }

    bool has_quantization = false;
    double quantization_step = 0.0;
    std::vector<std::string> quantization_candidate_ids;

    for (const auto index : plan.candidate_indices) {
        if (index >= candidates.candidates.size()) {
            reason = "candidate plan contains an out-of-range candidate index";
            return false;
        }
        const auto& candidate = candidates.candidates[index];
        if (!candidate.preserves_selection_cardinality) {
            reason = "candidate plan would change customer-visible selection cardinality";
            return false;
        }
        switch (candidate.kind) {
        case cadopt::CandidateKind::Raw:
            break;
        case cadopt::CandidateKind::NumericQuantization:
            if (!std::isfinite(candidate.numeric_parameter) || candidate.numeric_parameter <= 0.0) {
                reason = candidate.id + " has an invalid numeric quantization step";
                return false;
            }
            if (has_quantization
                && std::abs(candidate.numeric_parameter - quantization_step)
                    > std::max(1.0e-18, std::abs(quantization_step) * 1.0e-12)) {
                reason = "one generic DXF materialization cannot mix different whole-drawing quantization steps";
                return false;
            }
            has_quantization = true;
            quantization_step = candidate.numeric_parameter;
            quantization_candidate_ids.push_back(candidate.id);
            break;
        case cadopt::CandidateKind::ReferenceTransform:
        case cadopt::CandidateKind::ReferenceResidual:
        case cadopt::CandidateKind::Symmetry:
        case cadopt::CandidateKind::Grid:
        case cadopt::CandidateKind::SequenceGrammar:
        case cadopt::CandidateKind::PrimitiveReductionProbe:
        case cadopt::CandidateKind::TensorProbe:
        case cadopt::CandidateKind::WaveletProbe:
        case cadopt::CandidateKind::SpectralProbe:
            reason = "candidate kind " + cadopt::candidate_kind_name(candidate.kind)
                + " is analysis-only for the generic DXF materializer; native backend reconstruction is required";
            return false;
        }
    }

    if (!has_quantization) {
        materialization_key = "plan-aware:raw";
        reason = "generic DXF materializer can reproduce this plan exactly";
        return true;
    }

    std::sort(quantization_candidate_ids.begin(), quantization_candidate_ids.end());
    std::ostringstream key;
    key << "plan-aware:quant:" << std::setprecision(17) << quantization_step;
    for (const auto& id : quantization_candidate_ids) key << ':' << id;
    materialization_key = key.str();
    reason = "generic DXF materializer can emit numeric quantization plan";
    return true;
}

static ExactEvaluationQueue build_exact_evaluation_queue(
    const cadopt::CandidateSet& candidates,
    const std::vector<cadopt::CandidatePlan>& plans,
    std::size_t exact_budget) {

    ExactEvaluationQueue queue;
    queue.exact_budget = exact_budget;
    queue.decisions.resize(plans.size());

    std::unordered_map<std::string, std::size_t> representative_by_key;

    auto better_representative = [&](std::size_t candidate_index, std::size_t current_index) {
        const auto& candidate = plans[candidate_index];
        const auto& current = plans[current_index];
        const bool candidate_baseline = candidate.id == "m6:raw-baseline";
        const bool current_baseline = current.id == "m6:raw-baseline";
        if (candidate_baseline != current_baseline) return candidate_baseline;
        if (candidate.estimated_bytes != current.estimated_bytes) {
            return candidate.estimated_bytes < current.estimated_bytes;
        }
        return candidate.id < current.id;
    };

    for (std::size_t i = 0; i < plans.size(); ++i) {
        auto& decision = queue.decisions[i];
        decision.plan_id = plans[i].id;
        decision.estimated_bytes = plans[i].estimated_bytes;

        std::string key;
        std::string reason;
        if (!plan_aware_preflight(candidates, plans[i], key, reason)) {
            decision.status = "unsupported";
            decision.reason = std::move(reason);
            continue;
        }

        decision.materialization_key = key;
        decision.status = "materializable";
        decision.reason = std::move(reason);

        const auto found = representative_by_key.find(key);
        if (found == representative_by_key.end()) {
            representative_by_key.emplace(std::move(key), i);
            continue;
        }

        const auto previous = found->second;
        if (better_representative(i, previous)) {
            auto& old_decision = queue.decisions[previous];
            old_decision.status = "duplicate_materialization";
            old_decision.reason = "same materialization output as representative plan " + plans[i].id;
            found->second = i;
        } else {
            decision.status = "duplicate_materialization";
            decision.reason = "same materialization output as representative plan " + plans[previous].id;
        }
    }

    std::vector<std::size_t> representatives;
    representatives.reserve(representative_by_key.size());
    for (const auto& [key, index] : representative_by_key) {
        (void)key;
        representatives.push_back(index);
    }
    queue.supported_unique = representatives.size();

    std::sort(representatives.begin(), representatives.end(), [&](std::size_t a, std::size_t b) {
        const bool a_baseline = plans[a].id == "m6:raw-baseline";
        const bool b_baseline = plans[b].id == "m6:raw-baseline";
        if (a_baseline != b_baseline) return a_baseline;
        if (plans[a].estimated_bytes != plans[b].estimated_bytes) {
            return plans[a].estimated_bytes < plans[b].estimated_bytes;
        }
        return plans[a].id < plans[b].id;
    });

    const std::size_t selected_count = exact_budget == 0
        ? representatives.size()
        : std::min(exact_budget, representatives.size());
    queue.selected_plan_indices.reserve(selected_count);

    for (std::size_t position = 0; position < representatives.size(); ++position) {
        const auto index = representatives[position];
        auto& decision = queue.decisions[index];
        if (position < selected_count) {
            decision.status = "selected";
            decision.reason = plans[index].id == "m6:raw-baseline"
                ? "raw baseline retained before expensive exact DWG evaluation"
                : "selected by estimated byte ranking within exact DWG budget";
            queue.selected_plan_indices.push_back(index);
        } else {
            decision.status = "budget_skipped";
            decision.reason = "materializable but outside expensive exact DWG evaluation budget";
        }
    }

    return queue;
}

static void trace_winner(const cadopt::EvaluationReport& evaluation,
                         const std::vector<cadopt::CandidatePlan>& plans,
                         const cadopt::CandidateSet& candidates,
                         cadopt::TraceLedger& trace) {
    const auto* winner = evaluation.winner();
    if (!winner) return;
    const auto plan_it = std::find_if(plans.begin(), plans.end(), [&](const auto& plan) {
        return plan.id == winner->plan_id;
    });
    if (plan_it == plans.end()) return;

    for (const auto index : plan_it->candidate_indices) {
        if (index >= candidates.candidates.size()) continue;
        const auto& candidate = candidates.candidates[index];
        for (const auto& source_id : candidate.source_ids) {
            trace.record_for_source(source_id,
                                    "winner_representation",
                                    "candidate=" + candidate.id
                                        + ";kind=" + cadopt::candidate_kind_name(candidate.kind)
                                        + ";materializer=" + winner->materializer
                                        + ";estimated_bytes=" + std::to_string(candidate.estimated_bytes),
                                    candidate.loss_risk > 0.0,
                                    candidate.loss_risk > 0.0 ? "representation_candidate" : "none",
                                    "info",
                                    "selected plan passed exact DWG round-trip verification");
        }
    }
}

static int run_research(const Options& opt,
                        const cadopt::DxfDocument& source,
                        const fs::path& source_dxf_path,
                        cadopt::DwgBackend& backend,
                        cadopt::TraceLedger& trace) {
    const auto views = cadopt::extract_geometry_views(source);
    trace_canonical_views(views, trace);

    cadopt::CandidateDiscoveryOptions discovery_options;
    discovery_options.numeric_quantization_steps = opt.quantization_steps;
    const auto candidates = cadopt::discover_representation_candidates(views, discovery_options);
    const auto candidate_validation = cadopt::validate_candidate_set(candidates);
    if (!candidate_validation.pass) {
        cadopt::VerificationReport empty;
        empty.pass = false;
        write_basic_report(opt.report, empty, trace, "candidate_discovery",
                           "candidate_set_invalid");
        for (const auto& issue : candidate_validation.issues) {
            std::cerr << "[CADOPT][M4] candidate validation: " << issue << '\n';
        }
        return 8;
    }

    auto plans = build_research_plans(candidates, opt.beam_width, opt.max_search_plans);
    const auto queue = build_exact_evaluation_queue(candidates, plans, opt.max_plans);
    std::cout << "[CADOPT][M4] source_entities=" << candidates.source_universe.size()
              << " candidates=" << candidates.candidates.size() << '\n';
    std::cout << "[CADOPT][M5] search_plans=" << plans.size()
              << " max_search_plans=" << opt.max_search_plans << '\n';
    std::cout << "[CADOPT][M6][PREFLIGHT] supported_unique=" << queue.supported_unique
              << " exact_budget=" << opt.max_plans
              << " selected=" << queue.selected_plan_indices.size() << '\n';
    for (const auto& decision : queue.decisions) {
        std::cout << "[CADOPT][M6][QUEUE] plan=" << decision.plan_id
                  << " status=" << decision.status
                  << " estimated_bytes=" << decision.estimated_bytes;
        if (!decision.materialization_key.empty()) {
            std::cout << " key=" << decision.materialization_key;
        }
        if (!decision.reason.empty()) std::cout << " reason=" << decision.reason;
        std::cout << '\n';
    }

    std::vector<cadopt::CandidatePlan> exact_plans;
    exact_plans.reserve(queue.selected_plan_indices.size());
    for (const auto index : queue.selected_plan_indices) exact_plans.push_back(plans[index]);

    cadopt::PlanAwareMaterializer materializer;
    cadopt::EvaluationOptions evaluation_options;
    evaluation_options.work_directory = effective_work_directory(opt) / "research";
    evaluation_options.max_plans = 0;
    evaluation_options.keep_artifacts = opt.keep_artifacts;
    evaluation_options.verification = verification_options(opt);

    std::cout << "[CADOPT][M6] exact backend round-trips start count=" << exact_plans.size()
              << " source_dxf_bytes=" << fs::file_size(source_dxf_path) << '\n';
    auto evaluation = cadopt::evaluate_candidate_plans(source, source_dxf_path, candidates, exact_plans,
                                                        backend, materializer,
                                                        evaluation_options, &trace);
    trace_winner(evaluation, plans, candidates, trace);
    write_research_report(opt.report, opt, candidates, plans, queue, evaluation, trace);

    std::string copy_error;
    if (!cadopt::copy_winner_dwg(evaluation, opt.output, &copy_error)) {
        std::cerr << "[CADOPT][M6] no valid DWG winner: " << copy_error
                  << ". Report: " << opt.report << '\n';
        return 9;
    }

    const auto* winner = evaluation.winner();
    std::cout << "[CADOPT][M6] winner=" << winner->plan_id
              << " materializer=" << winner->materializer
              << " exact_dwg_bytes=" << *winner->exact_dwg_bytes
              << " output=" << opt.output << '\n';
    return 0;
}

int main(int argc, char** argv) {
    try {
        const auto opt = parse_args(argc, argv);
        if (!opt.dry_run && same_target_path(opt.input, opt.output)) {
            throw std::runtime_error("refusing to overwrite the source CAD input; choose a different --output path");
        }

        const auto input_kind = cadopt::detect_cad_input_kind(opt.input);
        const bool need_read_dwg = input_kind == cadopt::CadInputKind::Dwg || !opt.dry_run;
        const bool need_write_dwg = !opt.dry_run;
        std::string backend_error;
        auto backend = configure_backend(opt, need_read_dwg, need_write_dwg, backend_error);
        if ((need_read_dwg || need_write_dwg) && !backend) {
            std::cerr << "[CADOPT][BACKEND] " << backend_error << '\n';
            return 4;
        }

        const auto prepared = cadopt::prepare_input_as_dxf(
            opt.input, effective_work_directory(opt), backend.get());
        if (!prepared.ok) {
            std::cerr << "[CADOPT][INPUT] " << prepared.message << '\n';
            return 4;
        }
        if (prepared.converted_from_dwg) {
            std::cout << "[CADOPT][INPUT] DWG normalized to " << prepared.dxf_path
                      << " bytes=" << prepared.conversion.output_bytes << '\n';
        } else {
            std::cout << "[CADOPT][INPUT] DXF source=" << prepared.dxf_path << '\n';
        }

        const auto source = cadopt::DxfDocument::read(prepared.dxf_path);
        std::cout << "[CADOPT] source indexed: entities=" << source.entities().size()
                  << " blocks=" << source.blocks().size()
                  << " objects=" << source.objects().size()
                  << " xdata_owners=" << count_xdata_owners(source) << "\n";

        cadopt::TraceLedger trace;
        seed_trace(source, trace);
        trace.record_for_source("document",
                                "input_normalization",
                                "input_kind=" + cadopt::cad_input_kind_name(prepared.kind)
                                    + ";converted_from_dwg="
                                    + (prepared.converted_from_dwg ? "true" : "false"),
                                false,
                                "none",
                                "info",
                                prepared.message);

        const fs::path staged = effective_work_directory(opt) / "safety" / "staged_source.dxf";
        fs::create_directories(staged.parent_path());
        source.write(staged, cadopt::DxfWriteMode::PreserveLexical);
        const auto staged_doc = cadopt::DxfDocument::read(staged);
        auto report = cadopt::verify_semantic_equivalence(source, staged_doc,
                                                          verification_options(opt));
        if (!report.pass) {
            trace_verification_failures(trace, report);
            write_basic_report(opt.report, report, trace, "dxf_roundtrip", "not_started");
            std::cerr << "[CADOPT] zero-optimization DXF round-trip FAILED. See " << opt.report << '\n';
            return 3;
        }
        std::cout << "[CADOPT] zero-optimization DXF round-trip verified\n";

        if (opt.dry_run) {
            write_basic_report(opt.report, report, trace, "dxf_roundtrip",
                               prepared.converted_from_dwg ? "dry_run_from_dwg" : "dry_run_from_dxf");
            std::cout << "Dry-run PASS: " << source.entities().size()
                      << " selection units preserved. Report: " << opt.report << '\n';
            return 0;
        }

        if (opt.research) return run_research(opt, source, prepared.dxf_path, *backend, trace);

        const auto encode = backend->dxf_to_dwg(staged, opt.output);
        if (!encode.ok) {
            write_basic_report(opt.report, report, trace, "dwg_roundtrip", encode.message);
            std::cerr << "DXF->DWG failed: " << encode.message << '\n';
            return 5;
        }

        const fs::path verify_dxf = effective_work_directory(opt) / "verify" / "roundtrip.dxf";
        fs::create_directories(verify_dxf.parent_path());
        const auto decode = backend->dwg_to_dxf(opt.output, verify_dxf);
        if (!decode.ok) {
            write_basic_report(opt.report, report, trace, "dwg_roundtrip", decode.message);
            std::cerr << "DWG->DXF verification conversion failed: " << decode.message << '\n';
            return 6;
        }

        const auto roundtrip = cadopt::DxfDocument::read(verify_dxf);
        report = cadopt::verify_semantic_equivalence(source, roundtrip,
                                                     verification_options(opt));
        trace_verification_failures(trace, report);
        write_basic_report(opt.report, report, trace, "dwg_roundtrip", "ok");
        if (!report.pass) {
            std::cerr << "DWG geometry/semantic/interaction round-trip FAILED. See "
                      << opt.report << '\n';
            return 7;
        }
        std::cout << "DWG round-trip PASS. bytes=" << encode.output_bytes
                  << " output=" << opt.output << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "cadopt: " << error.what() << '\n';
        return 2;
    }
}
