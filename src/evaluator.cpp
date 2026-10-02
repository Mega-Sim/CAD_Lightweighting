#include <cadopt/evaluator.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <unordered_set>
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

bool plan_is_materializable(const CandidateSet& candidate_set,
                            const CandidatePlan& plan,
                            std::string& error) {
    if (!plan.structurally_valid) {
        error = "candidate plan is not a valid exact cover";
        return false;
    }
    for (const auto index : plan.candidate_indices) {
        if (index >= candidate_set.candidates.size()) {
            error = "candidate plan contains an out-of-range candidate index";
            return false;
        }
        if (!candidate_set.candidates[index].preserves_selection_cardinality) {
            error = "candidate plan would change customer-visible selection cardinality";
            return false;
        }
    }
    return true;
}

bool parse_double(const std::string& text, double& value) {
    char* end = nullptr;
    value = std::strtod(text.c_str(), &end);
    if (end == text.c_str()) return false;
    while (end && *end != '\0') {
        if (*end != ' ' && *end != '\t' && *end != '\r' && *end != '\n') return false;
        ++end;
    }
    return std::isfinite(value);
}

bool quantizable_geometry_code(const std::string& type, int code) {
    if (type == "LINE") {
        return code == 10 || code == 20 || code == 30
            || code == 11 || code == 21 || code == 31;
    }
    if (type == "ARC" || type == "CIRCLE") {
        // Start/end angles remain exact in the first lossy path.
        return code == 10 || code == 20 || code == 30 || code == 40;
    }
    if (type == "LWPOLYLINE") {
        // Keep bulge (42) and angular/dimensionless fields exact.
        return code == 10 || code == 20 || code == 38 || code == 39
            || code == 40 || code == 41 || code == 43;
    }
    if (type == "TEXT") {
        // Keep rotation/oblique/width-factor values exact. Quantize only
        // coordinates and text height.
        return code == 10 || code == 20 || code == 30
            || code == 11 || code == 21 || code == 31 || code == 40;
    }
    return false;
}

std::string format_quantized(double value) {
    if (value == 0.0) value = 0.0; // canonicalize negative zero
    std::ostringstream out;
    out << std::setprecision(17) << std::defaultfloat << value;
    return out.str();
}

MaterializationResult write_quantized_dxf(
    const DxfDocument& source,
    const std::unordered_set<std::string>& source_ids,
    double step,
    const std::filesystem::path& output_dxf,
    const std::string& label) {

    if (!std::isfinite(step) || step <= 0.0) {
        return {false, "numeric quantization step must be finite and positive", {}};
    }

    std::unordered_set<std::size_t> quantizable_records;
    for (const auto& entity : source.entities()) {
        if (!source_ids.empty() && !source_ids.contains(entity.source_id)) continue;
        for (std::size_t index = entity.first_record;
             index < entity.last_record_exclusive && index < source.records().size(); ++index) {
            if (quantizable_geometry_code(entity.type, source.records()[index].code)) {
                quantizable_records.insert(index);
            }
        }
    }

    std::ofstream out(output_dxf, std::ios::binary | std::ios::trunc);
    if (!out) return {false, "unable to create quantized DXF: " + output_dxf.string(), {}};

    const auto& records = source.records();
    for (std::size_t index = 0; index < records.size(); ++index) {
        const auto& record = records[index];
        out << record.raw_code_line << '\n';
        if (!quantizable_records.contains(index)) {
            out << record.raw_value_line << '\n';
            continue;
        }

        double value = 0.0;
        if (!parse_double(record.value, value)) {
            out << record.raw_value_line << '\n';
            continue;
        }
        const double quantized = std::round(value / step) * step;
        out << format_quantized(quantized) << '\n';
    }
    out.flush();
    if (!out) return {false, "failed while writing quantized DXF", {}};
    return {true, label, output_dxf};
}

void trace_materializer_issues(TraceLedger& trace,
                               const VerificationReport& verification,
                               const std::string& materializer_name) {
    for (const auto& issue : verification.issues) {
        trace.record_for_source(issue.source_id,
                                "materializer_verification_issue",
                                "materializer=" + materializer_name,
                                true,
                                issue.category,
                                issue.severity,
                                issue.detail,
                                issue.metric_name,
                                issue.metric_value);
    }
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

    std::string validation_error;
    if (!plan_is_materializable(candidate_set, plan, validation_error)) {
        return {false, validation_error, {}};
    }

    try {
        source.write(output_dxf, DxfWriteMode::PreserveLexical);
    } catch (const std::exception& error) {
        return {false, error.what(), {}};
    }
    return {true, "strict source-equivalent reconstruction", output_dxf};
}

NumericQuantizationMaterializer::NumericQuantizationMaterializer(double step)
    : step_(step) {
    if (!std::isfinite(step_) || step_ <= 0.0) {
        throw std::invalid_argument("numeric quantization step must be finite and positive");
    }
}

std::string NumericQuantizationMaterializer::name() const {
    std::ostringstream out;
    out << "numeric-quantization(step=" << std::setprecision(17) << step_ << ')';
    return out.str();
}

MaterializationResult NumericQuantizationMaterializer::materialize(
    const DxfDocument& source,
    const CandidateSet& candidate_set,
    const CandidatePlan& plan,
    const std::filesystem::path& output_dxf) const {

    std::string validation_error;
    if (!plan_is_materializable(candidate_set, plan, validation_error)) {
        return {false, validation_error, {}};
    }

    return write_quantized_dxf(source, {}, step_, output_dxf, name());
}

MaterializationResult PlanAwareMaterializer::materialize(
    const DxfDocument& source,
    const CandidateSet& candidate_set,
    const CandidatePlan& plan,
    const std::filesystem::path& output_dxf) const {

    std::string validation_error;
    if (!plan_is_materializable(candidate_set, plan, validation_error)) {
        return {false, validation_error, {}};
    }

    std::unordered_set<std::string> quantized_sources;
    double quantization_step = 0.0;
    bool has_quantization = false;

    for (const auto index : plan.candidate_indices) {
        const auto& candidate = candidate_set.candidates[index];
        switch (candidate.kind) {
        case CandidateKind::Raw:
            break;
        case CandidateKind::NumericQuantization:
            if (!std::isfinite(candidate.numeric_parameter) || candidate.numeric_parameter <= 0.0) {
                return {false, candidate.id + " has an invalid numeric quantization step", {}};
            }
            if (has_quantization
                && std::abs(candidate.numeric_parameter - quantization_step)
                    > std::max(1.0e-18, std::abs(quantization_step) * 1.0e-12)) {
                return {false, "one generic DXF materialization cannot mix different whole-drawing quantization steps", {}};
            }
            has_quantization = true;
            quantization_step = candidate.numeric_parameter;
            quantized_sources.insert(candidate.source_ids.begin(), candidate.source_ids.end());
            break;
        case CandidateKind::ReferenceTransform:
        case CandidateKind::ReferenceResidual:
        case CandidateKind::Symmetry:
        case CandidateKind::Grid:
        case CandidateKind::SequenceGrammar:
        case CandidateKind::TensorProbe:
        case CandidateKind::WaveletProbe:
        case CandidateKind::SpectralProbe:
            // These representations are real research/search candidates, but a
            // standards-compliant generic DXF writer cannot encode them into a
            // smaller final DWG without changing CAD selection semantics. A
            // native licensed backend may supply such a materializer later.
            return {false,
                    "candidate kind " + candidate_kind_name(candidate.kind)
                        + " is analysis-only for the generic DXF materializer; native backend reconstruction is required",
                    {}};
        }
    }

    if (!has_quantization) {
        try {
            source.write(output_dxf, DxfWriteMode::PreserveLexical);
        } catch (const std::exception& error) {
            return {false, error.what(), {}};
        }
        return {true, "plan-aware raw reconstruction", output_dxf};
    }

    return write_quantized_dxf(source,
                               quantized_sources,
                               quantization_step,
                               output_dxf,
                               "plan-aware numeric quantization");
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

        if (trace) trace_materializer_issues(*trace, evaluation.verification, materializer.name());
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

    recompute_winner(report);

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

void recompute_winner(EvaluationReport& report) {
    report.winner_index.reset();
    for (std::size_t i = 0; i < report.evaluations.size(); ++i) {
        const auto& evaluation = report.evaluations[i];
        if (!evaluation.valid || !evaluation.exact_dwg_bytes) continue;
        if (!report.winner_index) {
            report.winner_index = i;
            continue;
        }
        const auto& best = report.evaluations[*report.winner_index];
        if (*evaluation.exact_dwg_bytes < *best.exact_dwg_bytes
            || (*evaluation.exact_dwg_bytes == *best.exact_dwg_bytes
                && (evaluation.estimated_bytes < best.estimated_bytes
                    || (evaluation.estimated_bytes == best.estimated_bytes
                        && evaluation.materializer < best.materializer)))) {
            report.winner_index = i;
        }
    }
}

bool append_evaluation_report(EvaluationReport& destination,
                              EvaluationReport source,
                              std::string* error_message) {
    if (!destination.evaluations.empty()) {
        if (destination.backend.profile != source.backend.profile
            || destination.backend.name != source.backend.name) {
            if (error_message) *error_message = "cannot merge evaluation reports from different backends";
            return false;
        }
        if (destination.source_dxf_bytes != source.source_dxf_bytes) {
            if (error_message) *error_message = "cannot merge evaluation reports from different source sizes";
            return false;
        }
    } else {
        destination.source_dxf_bytes = source.source_dxf_bytes;
        destination.backend = source.backend;
    }

    destination.evaluations.insert(destination.evaluations.end(),
                                   std::make_move_iterator(source.evaluations.begin()),
                                   std::make_move_iterator(source.evaluations.end()));
    recompute_winner(destination);
    return true;
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
