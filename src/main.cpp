#include <cadopt/candidate.hpp>
#include <cadopt/dwg_backend.hpp>
#include <cadopt/dxf.hpp>
#include <cadopt/evaluator.hpp>
#include <cadopt/geometry.hpp>
#include <cadopt/search.hpp>
#include <cadopt/trace.hpp>
#include <cadopt/verifier.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
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
    std::size_t max_plans{16};
    std::size_t beam_width{64};
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
        else if (a == "--beam-width") o.beam_width = parse_size(value(a.c_str()), "--beam-width");
        else if (a == "--cleanup-artifacts") o.keep_artifacts = false;
        else if (a == "--dry-run") o.dry_run = true;
        else if (a == "--research" || a == "--optimize") o.research = true;
        else if (a == "--help" || a == "-h") {
            std::cout
                << "cadopt --input input.dxf --output output.dwg [options]\n"
                << "  --dry-run                 Strict DXF -> IR -> DXF safety check only\n"
                << "  --research                Discover/search/evaluate multiple representation plans\n"
                << "  --backend-profile NAME    external|oda-file-converter|oda-sdk|realdwg-host\n"
                << "  --dxf-to-dwg CMD          Converter command template with {input} and {output}\n"
                << "  --dwg-to-dxf CMD          Independent reverse command with {input} and {output}\n"
                << "  --max-plans N             Maximum exact DWG candidate evaluations (default 16)\n"
                << "  --beam-width N            Beam width for broad candidate search (default 64)\n"
                << "  --work-dir DIR            Candidate artifact directory\n"
                << "  --cleanup-artifacts       Remove non-winning candidate artifacts\n"
                << "  --report FILE             Machine-readable JSON report\n";
            std::exit(0);
        } else {
            throw std::runtime_error("unknown argument: " + a);
        }
    }
    if (o.input.empty()) throw std::runtime_error("--input is required");
    if (!o.dry_run && o.output.empty()) throw std::runtime_error("--output is required unless --dry-run");
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
                                  const cadopt::CandidateSet& candidates,
                                  const std::vector<cadopt::CandidatePlan>& plans,
                                  const cadopt::EvaluationReport& evaluation,
                                  const cadopt::TraceLedger& trace) {
    std::ofstream out(path, std::ios::trunc);
    if (!out) throw std::runtime_error("cannot write report: " + path.string());
    out << "{\n  \"milestone\": 6,\n"
        << "  \"phase\": \"research_exact_dwg_search\",\n"
        << "  \"candidate_set\": " << cadopt::candidate_set_to_json(candidates) << ",\n"
        << "  \"search_plans\": [";
    for (std::size_t i = 0; i < plans.size(); ++i) {
        if (i) out << ',';
        out << "\n    " << cadopt::candidate_plan_to_json(plans[i], candidates);
    }
    if (!plans.empty()) out << '\n';
    out << "  ],\n  \"evaluation\": " << evaluation.to_json() << ",\n"
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

static std::vector<cadopt::CandidatePlan> build_research_plans(
    const cadopt::CandidateSet& candidates,
    std::size_t beam_width,
    std::size_t max_plans) {

    cadopt::SearchOptions options;
    options.beam_width = std::max<std::size_t>(1, beam_width);
    options.max_results = std::max<std::size_t>(max_plans, 16);

    std::vector<cadopt::CandidatePlan> plans;
    std::set<std::string> seen;
    auto add = [&](cadopt::CandidatePlan plan) {
        const auto key = plan_key(plan);
        if (seen.insert(key).second) plans.push_back(std::move(plan));
    };

    add(cadopt::greedy_search(candidates));
    for (auto& plan : cadopt::beam_search(candidates, options)) add(std::move(plan));
    for (auto& plan : cadopt::exhaustive_search(candidates, options)) add(std::move(plan));

    std::sort(plans.begin(), plans.end(), [](const auto& a, const auto& b) {
        if (a.estimated_bytes != b.estimated_bytes) return a.estimated_bytes < b.estimated_bytes;
        return a.id < b.id;
    });
    if (max_plans != 0 && plans.size() > max_plans) plans.resize(max_plans);
    return plans;
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
                        cadopt::TraceLedger& trace) {
    if (opt.dxf_to_dwg.empty() || opt.dwg_to_dxf.empty()) {
        cadopt::VerificationReport empty;
        empty.pass = false;
        write_basic_report(opt.report, empty, trace, "research_exact_dwg_search",
                           "converter_not_configured");
        std::cerr << "[CADOPT][M6] research mode requires both converter commands\n";
        return 4;
    }

    const auto views = cadopt::extract_geometry_views(source);
    trace_canonical_views(views, trace);
    const auto candidates = cadopt::discover_representation_candidates(views);
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

    auto plans = build_research_plans(candidates, opt.beam_width, opt.max_plans);
    std::cout << "[CADOPT][M4] source_entities=" << candidates.source_universe.size()
              << " candidates=" << candidates.candidates.size() << '\n';
    std::cout << "[CADOPT][M5] exact-evaluation plans=" << plans.size() << '\n';

    const auto profile = cadopt::parse_dwg_backend_profile(opt.backend_profile);
    cadopt::ExternalCommandDwgBackend backend(opt.dxf_to_dwg, opt.dwg_to_dxf, profile);
    cadopt::StrictSourceMaterializer materializer;
    cadopt::EvaluationOptions evaluation_options;
    evaluation_options.work_directory = opt.work_directory.empty()
        ? fs::temp_directory_path() / "cadopt_m6_research"
        : opt.work_directory;
    evaluation_options.max_plans = opt.max_plans;
    evaluation_options.keep_artifacts = opt.keep_artifacts;

    auto evaluation = cadopt::evaluate_candidate_plans(source, opt.input, candidates, plans,
                                                        backend, materializer,
                                                        evaluation_options, &trace);
    trace_winner(evaluation, plans, candidates, trace);
    write_research_report(opt.report, candidates, plans, evaluation, trace);

    std::string copy_error;
    if (!cadopt::copy_winner_dwg(evaluation, opt.output, &copy_error)) {
        std::cerr << "[CADOPT][M6] no valid DWG winner: " << copy_error
                  << ". Report: " << opt.report << '\n';
        return 9;
    }

    const auto* winner = evaluation.winner();
    std::cout << "[CADOPT][M6] winner=" << winner->plan_id
              << " exact_dwg_bytes=" << *winner->exact_dwg_bytes
              << " output=" << opt.output << '\n';
    return 0;
}

int main(int argc, char** argv) {
    try {
        const auto opt = parse_args(argc, argv);
        const auto source = cadopt::DxfDocument::read(opt.input);
        std::cout << "[CADOPT] source indexed: entities=" << source.entities().size()
                  << " blocks=" << source.blocks().size()
                  << " objects=" << source.objects().size()
                  << " xdata_owners=" << count_xdata_owners(source) << "\n";

        cadopt::TraceLedger trace;
        seed_trace(source, trace);

        const fs::path staged = fs::temp_directory_path() / "cadopt_safety_staged.dxf";
        source.write(staged, cadopt::DxfWriteMode::PreserveLexical);
        const auto staged_doc = cadopt::DxfDocument::read(staged);
        auto report = cadopt::verify_semantic_equivalence(source, staged_doc);
        if (!report.pass) {
            trace_verification_failures(trace, report);
            write_basic_report(opt.report, report, trace, "dxf_roundtrip", "not_started");
            std::cerr << "[CADOPT] zero-optimization DXF round-trip FAILED. See " << opt.report << '\n';
            return 3;
        }
        std::cout << "[CADOPT] zero-optimization DXF round-trip verified\n";

        if (opt.dry_run) {
            write_basic_report(opt.report, report, trace, "dxf_roundtrip", "dry_run");
            std::cout << "Dry-run PASS: " << source.entities().size()
                      << " selection units preserved. Report: " << opt.report << '\n';
            return 0;
        }

        if (opt.research) return run_research(opt, source, trace);

        if (opt.dxf_to_dwg.empty() || opt.dwg_to_dxf.empty()) {
            write_basic_report(opt.report, report, trace, "dwg_roundtrip", "converter_not_configured");
            std::cerr << "DWG backend is not configured. Both converter commands are required\n";
            return 4;
        }

        const auto profile = cadopt::parse_dwg_backend_profile(opt.backend_profile);
        cadopt::ExternalCommandDwgBackend backend(opt.dxf_to_dwg, opt.dwg_to_dxf, profile);
        const auto encode = backend.dxf_to_dwg(staged, opt.output);
        if (!encode.ok) {
            write_basic_report(opt.report, report, trace, "dwg_roundtrip", encode.message);
            std::cerr << "DXF->DWG failed: " << encode.message << '\n';
            return 5;
        }

        const fs::path verify_dxf = fs::temp_directory_path() / "cadopt_verify.dxf";
        const auto decode = backend.dwg_to_dxf(opt.output, verify_dxf);
        if (!decode.ok) {
            write_basic_report(opt.report, report, trace, "dwg_roundtrip", decode.message);
            std::cerr << "DWG->DXF verification conversion failed: " << decode.message << '\n';
            return 6;
        }

        const auto roundtrip = cadopt::DxfDocument::read(verify_dxf);
        report = cadopt::verify_semantic_equivalence(source, roundtrip);
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
