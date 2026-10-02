#include <cadopt/candidate.hpp>
#include <cadopt/dwg_backend.hpp>
#include <cadopt/dxf.hpp>
#include <cadopt/evaluator.hpp>
#include <cadopt/geometry.hpp>
#include <cadopt/search.hpp>

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

int main() {
    try {
        test_exact_evaluator_accepts_verified_round_trip();
        test_exact_evaluator_rejects_round_trip_semantic_loss();
        std::cout << "cadopt_evaluator_tests: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "cadopt_evaluator_tests: FAIL: " << error.what() << '\n';
        return 1;
    }
}
