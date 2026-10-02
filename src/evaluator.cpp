#include <cadopt/evaluator.hpp>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <system_error>
#include <utility>

namespace cadopt {
namespace {

std::string escape_json(const std::string& text) {
    std::ostringstream out;
    for (char c : text) {
        if (c == '"') out << "\\\"";
        else if (c == '\\') out << "\\\\";
        else if (c == '\n') out << "\\n";
        else if (c == '\r') out << "\\r";
        else out << c;
    }
    return out.str();
}

std::string path_json(const std::filesystem::path& path) {
    return escape_json(path.generic_string());
}

std::filesystem::path effective_work_directory(const EvaluationOptions& options) {
    if (!options.work_directory.empty()) return options.work_directory;
    return std::filesystem::temp_directory_path() / "cadopt_m6_eval";
}

void mark_failure(PlanEvaluation& evaluation,
                  CandidatePlan& plan,
                  std::string stage,
                  std::string reason) {
    evaluation.valid = false;
    evaluation.verification.pass = false;
    evaluation.failure_stage = std::move(stage);
    evaluation.failure_reason = std::move(reason);
    plan.exact_valid = false;
    if (evaluation.exact_dwg_bytes) plan.exact_dwg_bytes = evaluation.exact_dwg_bytes;
    plan.issues.push_back(evaluation.failure_stage + ": " + evaluation.failure_reason);
}

} // namespace

MaterializationResult StrictSourceMaterializer::materialize(
    const DxfDocument& source,
    const CandidateSet& candidate_set,
    const CandidatePlan& plan,
    const std::filesystem::path& output_dxf) const {

    if (!plan.structurally_valid) {
        return {false, "candidate plan is not a valid exact cover", {}};
    }
    for (const auto index : plan.candidate_indices) {
        if (index >= candidate_set.candidates.size()) {
            return {false, "candidate plan contains an out-of-range candidate index", {}};
        }
        if (!candidate_set.candidates[index].preserves_selection_cardinality) {
            return {false, "candidate plan would change customer-visible selection cardinality", {}};
        }
    }

    try {
        source.write(output_dxf, DxfWriteMode::PreserveLexical);
    } catch (const std::exception& error) {
        return {false, error.what(), {}};
    }
    return {true, "strict source-equivalent reconstruction", output_dxf};
}

const PlanEvaluation* EvaluationReport::winner() const {
    if (!winner_index || *winner_index >= evaluations.size()) return nullptr;
    return &evaluations[*winner_index];
}

std::string EvaluationReport::to_json() const {
    std::ostringstream out;
    out << "{\n"
        << "  \"source_dxf_bytes\": " << source_dxf_bytes << ",\n"
        << "  \"backend\": {\"name\": \"" << escape_json(backend.name)
        << "\", \"profile\": \"" << dwg_backend_profile_name(backend.profile)
        << "\", \"can_write_dwg\": " << (backend.can_write_dwg ? "true" : "false")
        << ", \"can_read_dwg\": " << (backend.can_read_dwg ? "true" : "false")
        << ", \"supports_independent_roundtrip\": "
        << (backend.supports_independent_roundtrip ? "true" : "false")
        << ", \"requires_external_license\": "
        << (backend.requires_external_license ? "true" : "false")
        << ", \"notes\": \"" << escape_json(backend.notes) << "\"},\n"
        << "  \"winner_index\": ";
    if (winner_index) out << *winner_index;
    else out << "null";
    out << ",\n  \"evaluations\": [";
    for (std::size_t i = 0; i < evaluations.size(); ++i) {
        if (i) out << ',';
        const auto& evaluation = evaluations[i];
        out << "\n    {\"plan_id\": \"" << escape_json(evaluation.plan_id)
            << "\", \"estimated_bytes\": " << evaluation.estimated_bytes
            << ", \"exact_dwg_bytes\": ";
        if (evaluation.exact_dwg_bytes) out << *evaluation.exact_dwg_bytes;
        else out << "null";
        out << ", \"valid\": " << (evaluation.valid ? "true" : "false")
            << ", \"materializer\": \"" << escape_json(evaluation.materializer)
            << "\", \"failure_stage\": \"" << escape_json(evaluation.failure_stage)
            << "\", \"failure_reason\": \"" << escape_json(evaluation.failure_reason)
            << "\", \"staged_dxf\": \"" << path_json(evaluation.staged_dxf)
            << "\", \"dwg_path\": \"" << path_json(evaluation.dwg_path)
            << "\", \"verification_dxf\": \"" << path_json(evaluation.verification_dxf)
            << "\", \"verification\": " << evaluation.verification.to_json() << '}';
    }
    if (!evaluations.empty()) out << '\n';
    out << "  ]\n}";
    return out.str();
}

EvaluationReport evaluate_candidate_plans(const DxfDocument& source,
                                          const std::filesystem::path& source_path,
                                          const CandidateSet& candidate_set,
                                          std::vector<CandidatePlan>& plans,
                                          const DwgBackend& backend,
                                          const CandidateMaterializer& materializer,
                                          const EvaluationOptions& options,
                                          TraceLedger* trace) {
    EvaluationReport report;
    report.backend = backend.capabilities();

    std::error_code error;
    report.source_dxf_bytes = std::filesystem::file_size(source_path, error);
    if (error) report.source_dxf_bytes = 0;

    const auto work = effective_work_directory(options);
    error.clear();
    std::filesystem::create_directories(work, error);
    if (error) {
        PlanEvaluation failure;
        failure.plan_id = "document";
        failure.materializer = materializer.name();
        failure.failure_stage = "work_directory";
        failure.failure_reason = error.message();
        failure.verification.pass = false;
        report.evaluations.push_back(std::move(failure));
        return report;
    }

    if (!report.backend.can_write_dwg || !report.backend.can_read_dwg
        || !report.backend.supports_independent_roundtrip) {
        PlanEvaluation failure;
        failure.plan_id = "document";
        failure.materializer = materializer.name();
        failure.failure_stage = "backend_capability";
        failure.failure_reason = "backend must support DWG write, DWG read, and independent round trip";
        failure.verification.pass = false;
        report.evaluations.push_back(std::move(failure));
        return report;
    }

    const std::size_t limit = options.max_plans == 0
        ? plans.size()
        : std::min(options.max_plans, plans.size());

    for (std::size_t i = 0; i < limit; ++i) {
        auto& plan = plans[i];
        PlanEvaluation evaluation;
        evaluation.plan_id = plan.id;
        evaluation.estimated_bytes = plan.estimated_bytes;
        evaluation.materializer = materializer.name();
        evaluation.staged_dxf = work / ("candidate_" + std::to_string(i) + ".dxf");
        evaluation.dwg_path = work / ("candidate_" + std::to_string(i) + ".dwg");
        evaluation.verification_dxf = work / ("candidate_" + std::to_string(i) + "_verify.dxf");

        if (!plan.structurally_valid) {
            mark_failure(evaluation, plan, "plan_structure", "candidate plan is not an exact cover");
            report.evaluations.push_back(std::move(evaluation));
            continue;
        }

        const auto materialized = materializer.materialize(source, candidate_set, plan,
                                                            evaluation.staged_dxf);
        if (!materialized.ok) {
            mark_failure(evaluation, plan, "materialize", materialized.message);
            report.evaluations.push_back(std::move(evaluation));
            continue;
        }

        const auto encoded = backend.dxf_to_dwg(evaluation.staged_dxf, evaluation.dwg_path);
        if (!encoded.ok) {
            mark_failure(evaluation, plan, "dxf_to_dwg", encoded.message);
            report.evaluations.push_back(std::move(evaluation));
            continue;
        }

        error.clear();
        const auto exact_bytes = std::filesystem::file_size(evaluation.dwg_path, error);
        if (error || exact_bytes == 0) {
            mark_failure(evaluation, plan, "measure_dwg",
                         error ? error.message() : "DWG output is empty");
            report.evaluations.push_back(std::move(evaluation));
            continue;
        }
        evaluation.exact_dwg_bytes = exact_bytes;

        const auto decoded = backend.dwg_to_dxf(evaluation.dwg_path, evaluation.verification_dxf);
        if (!decoded.ok) {
            mark_failure(evaluation, plan, "dwg_to_dxf", decoded.message);
            report.evaluations.push_back(std::move(evaluation));
            continue;
        }

        try {
            const auto roundtrip = DxfDocument::read(evaluation.verification_dxf);
            evaluation.verification = verify_semantic_equivalence(source, roundtrip,
                                                                  options.verification);
        } catch (const std::exception& exception) {
            mark_failure(evaluation, plan, "verification_parse", exception.what());
            report.evaluations.push_back(std::move(evaluation));
            continue;
        }

        if (trace) append_verification_issues_to_trace(*trace, evaluation.verification);
        if (!evaluation.verification.pass) {
            mark_failure(evaluation, plan, "verification",
                         "geometry/semantic/interaction round trip failed");
            report.evaluations.push_back(std::move(evaluation));
            continue;
        }

        evaluation.valid = true;
        set_exact_evaluation(plan, exact_bytes, true);
        report.evaluations.push_back(std::move(evaluation));
    }

    const auto* best_plan = choose_best_exact_plan(plans);
    if (best_plan) {
        for (std::size_t i = 0; i < report.evaluations.size(); ++i) {
            if (report.evaluations[i].plan_id == best_plan->id && report.evaluations[i].valid) {
                report.winner_index = i;
                break;
            }
        }
    }

    if (!options.keep_artifacts && report.winner_index) {
        for (std::size_t i = 0; i < report.evaluations.size(); ++i) {
            if (i == *report.winner_index) continue;
            std::error_code ignored;
            std::filesystem::remove(report.evaluations[i].staged_dxf, ignored);
            std::filesystem::remove(report.evaluations[i].dwg_path, ignored);
            std::filesystem::remove(report.evaluations[i].verification_dxf, ignored);
        }
    }

    return report;
}

bool copy_winner_dwg(const EvaluationReport& report,
                     const std::filesystem::path& output,
                     std::string* error_message) {
    const auto* selected = report.winner();
    if (!selected || !selected->valid || !selected->exact_dwg_bytes) {
        if (error_message) *error_message = "no valid evaluated DWG candidate exists";
        return false;
    }

    std::error_code error;
    if (!output.parent_path().empty()) std::filesystem::create_directories(output.parent_path(), error);
    if (error) {
        if (error_message) *error_message = "cannot create output directory: " + error.message();
        return false;
    }
    error.clear();
    std::filesystem::copy_file(selected->dwg_path, output,
                               std::filesystem::copy_options::overwrite_existing, error);
    if (error) {
        if (error_message) *error_message = "cannot copy winning DWG: " + error.message();
        return false;
    }
    return true;
}

} // namespace cadopt
