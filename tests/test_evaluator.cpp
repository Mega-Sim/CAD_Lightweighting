#include <cadopt/candidate.hpp>
#include <cadopt/dwg_backend.hpp>
#include <cadopt/dxf.hpp>
#include <cadopt/evaluator.hpp>
#include <cadopt/geometry.hpp>
#include <cadopt/search.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

class FakeRoundTripBackend final : public cadopt::DwgBackend {
public:
    explicit FakeRoundTripBackend(bool corrupt_decode = false)
        : corrupt_decode_(corrupt_decode) {}

    cadopt::DwgBackendCapabilities capabilities() const override {
        return {"fake-roundtrip", cadopt::DwgBackendProfile::ExternalCommand,
                true, true, true, false, "test backend"};
    }

    cadopt::ConversionResult dxf_to_dwg(const fs::path& input,
                                        const fs::path& output) const override {
        std::error_code error;
        fs::copy_file(input, output, fs::copy_options::overwrite_existing, error);
        if (error) return {false, 1, error.message(), 0};
        return {true, 0, "ok", fs::file_size(output)};
    }

    cadopt::ConversionResult dwg_to_dxf(const fs::path& input,
                                        const fs::path& output) const override {
        std::ifstream in(input, std::ios::binary);
        std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
        if (corrupt_decode_) {
            const auto pos = text.find("LINE");
            if (pos != std::string::npos) text.replace(pos, 4, "ARC ");
        }
        std::ofstream out(output, std::ios::binary | std::ios::trunc);
        out << text;
        out.close();
        if (!out) return {false, 1, "failed to write fake reverse DXF", 0};
        return {true, 0, "ok", fs::file_size(output)};
    }

private:
    bool corrupt_decode_{};
};

static std::vector<cadopt::CandidatePlan> one_plan(const cadopt::CandidateSet& set) {
    std::vector<cadopt::CandidatePlan> plans;
    plans.push_back(cadopt::greedy_search(set));
    return plans;
}

static fs::path write_quantization_fixture(const fs::path& directory) {
    fs::create_directories(directory);
    const auto path = directory / "quantization_input.dxf";
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out
        << "0\nSECTION\n"
        << "2\nENTITIES\n"
        << "0\nLINE\n"
        << "5\n10\n"
        << "8\n0\n"
        << "10\n0.123456789\n"
        << "20\n0.987654321\n"
        << "30\n0.0\n"
        << "11\n10.123456789\n"
        << "21\n10.987654321\n"
        << "31\n0.0\n"
        << "0\nENDSEC\n"
        << "0\nEOF\n";
    out.close();
    if (!out) throw std::runtime_error("failed to create quantization fixture");
    return path;
}

static std::size_t find_quantization_candidate(const cadopt::CandidateSet& set, double step) {
    const auto it = std::find_if(set.candidates.begin(), set.candidates.end(), [&](const auto& candidate) {
        return candidate.kind == cadopt::CandidateKind::NumericQuantization
            && candidate.numeric_parameter == step;
    });
    if (it == set.candidates.end()) throw std::runtime_error("quantization candidate missing");
    return static_cast<std::size_t>(std::distance(set.candidates.begin(), it));
}

static void test_exact_evaluator_accepts_verified_round_trip() {
    const fs::path input = fs::path(CADOPT_TEST_FIXTURE_DIR) / "minimal.dxf";
    const auto source = cadopt::DxfDocument::read(input);
    const auto candidates = cadopt::discover_representation_candidates(
        cadopt::extract_geometry_views(source));
    auto plans = one_plan(candidates);

    FakeRoundTripBackend backend;
    cadopt::StrictSourceMaterializer materializer;
    cadopt::EvaluationOptions options;
    options.work_directory = fs::temp_directory_path() / "cadopt_evaluator_test_valid";
    options.max_plans = 1;
    options.keep_artifacts = true;

    const auto report = cadopt::evaluate_candidate_plans(
        source, input, candidates, plans, backend, materializer, options);
    const auto* winner = report.winner();
    require(winner != nullptr, "verified candidate should produce a winner");
    require(winner->valid, "winner must be valid");
    require(winner->exact_dwg_bytes.has_value() && *winner->exact_dwg_bytes > 0,
            "winner must carry exact DWG bytes");

    const fs::path copied = options.work_directory / "selected.dwg";
    std::string error;
    require(cadopt::copy_winner_dwg(report, copied, &error),
            "winner copy failed: " + error);
    require(fs::exists(copied), "selected DWG was not copied");
    fs::remove_all(options.work_directory);
}

static void test_exact_evaluator_rejects_round_trip_semantic_loss() {
    const fs::path input = fs::path(CADOPT_TEST_FIXTURE_DIR) / "minimal.dxf";
    const auto source = cadopt::DxfDocument::read(input);
    const auto candidates = cadopt::discover_representation_candidates(
        cadopt::extract_geometry_views(source));
    auto plans = one_plan(candidates);

    FakeRoundTripBackend backend(true);
    cadopt::StrictSourceMaterializer materializer;
    cadopt::EvaluationOptions options;
    options.work_directory = fs::temp_directory_path() / "cadopt_evaluator_test_invalid";
    options.max_plans = 1;

    const auto report = cadopt::evaluate_candidate_plans(
        source, input, candidates, plans, backend, materializer, options);
    require(report.winner() == nullptr, "semantic loss must not produce a winning DWG");
    require(!report.evaluations.empty() && !report.evaluations.front().valid,
            "corrupt reverse conversion must be invalid");
    require(report.evaluations.front().failure_stage == "verification",
            "failure must be attributed to verification");
    fs::remove_all(options.work_directory);
}

static void test_plan_aware_quantization_passes_only_inside_geometry_tolerance() {
    const auto root = fs::temp_directory_path() / "cadopt_evaluator_quantized";
    const auto input = write_quantization_fixture(root);
    const auto source = cadopt::DxfDocument::read(input);

    cadopt::CandidateDiscoveryOptions discovery;
    discovery.numeric_quantization_steps = {1.0e-3};
    const auto candidates = cadopt::discover_representation_candidates(
        cadopt::extract_geometry_views(source), discovery);
    const auto quant_index = find_quantization_candidate(candidates, 1.0e-3);
    std::vector<cadopt::CandidatePlan> plans{
        cadopt::build_plan(candidates, {quant_index}, "quantized")
    };
    require(plans.front().structurally_valid,
            "whole-drawing quantization candidate must form an exact-cover plan");

    FakeRoundTripBackend backend;
    cadopt::PlanAwareMaterializer materializer;
    cadopt::EvaluationOptions options;
    options.work_directory = root / "allowed";
    options.max_plans = 1;
    options.verification.absolute_geometry_tolerance = 1.0e-3;
    options.verification.relative_geometry_tolerance = 0.0;

    const auto allowed = cadopt::evaluate_candidate_plans(
        source, input, candidates, plans, backend, materializer, options);
    require(allowed.winner() != nullptr,
            "quantization inside the configured geometry tolerance should remain eligible");
    require(allowed.winner()->materializer == "plan-aware",
            "plan-aware evaluator must report its materializer identity");

    auto strict_plans = std::vector<cadopt::CandidatePlan>{
        cadopt::build_plan(candidates, {quant_index}, "quantized-too-coarse")
    };
    options.work_directory = root / "rejected";
    options.verification.absolute_geometry_tolerance = 1.0e-6;
    const auto rejected = cadopt::evaluate_candidate_plans(
        source, input, candidates, strict_plans, backend, materializer, options);
    require(rejected.winner() == nullptr,
            "quantization outside the configured geometry tolerance must be rejected");
    require(!rejected.evaluations.empty()
            && rejected.evaluations.front().failure_stage == "verification",
            "out-of-tolerance quantization must fail at independent verification");

    fs::remove_all(root);
}

static void test_recompute_winner_uses_actual_dwg_bytes_across_materializers() {
    cadopt::EvaluationReport report;
    cadopt::PlanEvaluation estimated_favorite;
    estimated_favorite.plan_id = "estimated-favorite";
    estimated_favorite.estimated_bytes = 10.0;
    estimated_favorite.exact_dwg_bytes = 500;
    estimated_favorite.valid = true;
    estimated_favorite.materializer = "strict-source-equivalent";

    cadopt::PlanEvaluation actual_favorite;
    actual_favorite.plan_id = "actual-favorite";
    actual_favorite.estimated_bytes = 9999.0;
    actual_favorite.exact_dwg_bytes = 300;
    actual_favorite.valid = true;
    actual_favorite.materializer = "plan-aware";

    report.evaluations = {estimated_favorite, actual_favorite};
    cadopt::recompute_winner(report);
    require(report.winner() != nullptr && report.winner()->plan_id == "actual-favorite",
            "actual serialized DWG bytes must outrank all estimated costs");
}

int main() {
    try {
        test_exact_evaluator_accepts_verified_round_trip();
        test_exact_evaluator_rejects_round_trip_semantic_loss();
        test_plan_aware_quantization_passes_only_inside_geometry_tolerance();
        test_recompute_winner_uses_actual_dwg_bytes_across_materializers();
        std::cout << "cadopt_evaluator_tests: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "cadopt_evaluator_tests: FAIL: " << error.what() << '\n';
        return 1;
    }
}
