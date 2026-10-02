#pragma once

#include <cadopt/candidate.hpp>
#include <cadopt/dwg_backend.hpp>
#include <cadopt/dxf.hpp>
#include <cadopt/search.hpp>
#include <cadopt/trace.hpp>
#include <cadopt/verifier.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace cadopt {

struct MaterializationResult {
    bool ok{};
    std::string message;
    std::filesystem::path dxf_path;
};

class CandidateMaterializer {
public:
    virtual ~CandidateMaterializer() = default;
    virtual std::string name() const = 0;
    virtual MaterializationResult materialize(const DxfDocument& source,
                                              const CandidateSet& candidate_set,
                                              const CandidatePlan& plan,
                                              const std::filesystem::path& output_dxf) const = 0;
};

class StrictSourceMaterializer final : public CandidateMaterializer {
public:
    std::string name() const override { return "strict-source-equivalent"; }
    MaterializationResult materialize(const DxfDocument& source,
                                      const CandidateSet& candidate_set,
                                      const CandidatePlan& plan,
                                      const std::filesystem::path& output_dxf) const override;
};

struct EvaluationOptions {
    std::filesystem::path work_directory;
    std::size_t max_plans{16};
    VerificationOptions verification{};
    bool keep_artifacts{true};
};

struct PlanEvaluation {
    std::string plan_id;
    double estimated_bytes{};
    std::optional<std::uintmax_t> exact_dwg_bytes;
    bool valid{};
    std::string materializer;
    std::string failure_stage;
    std::string failure_reason;
    std::filesystem::path staged_dxf;
    std::filesystem::path dwg_path;
    std::filesystem::path verification_dxf;
    VerificationReport verification;
};

struct EvaluationReport {
    std::uintmax_t source_dxf_bytes{};
    DwgBackendCapabilities backend;
    std::vector<PlanEvaluation> evaluations;
    std::optional<std::size_t> winner_index;

    const PlanEvaluation* winner() const;
    std::string to_json() const;
};

EvaluationReport evaluate_candidate_plans(const DxfDocument& source,
                                          const std::filesystem::path& source_path,
                                          const CandidateSet& candidate_set,
                                          std::vector<CandidatePlan>& plans,
                                          const DwgBackend& backend,
                                          const CandidateMaterializer& materializer,
                                          const EvaluationOptions& options,
                                          TraceLedger* trace = nullptr);

bool copy_winner_dwg(const EvaluationReport& report,
                     const std::filesystem::path& output,
                     std::string* error = nullptr);

} // namespace cadopt
