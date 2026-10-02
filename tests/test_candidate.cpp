#include <cadopt/candidate.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

static void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

static cadopt::GeometryView make_line(std::string id,
                                     cadopt::Vec3 a,
                                     cadopt::Vec3 b) {
    cadopt::GeometryView view;
    view.source_id = std::move(id);
    view.type = "LINE";
    view.points = {a, b};
    view.supported = true;
    view.complete = true;
    return view;
}

static cadopt::GeometryView make_polyline(std::string id,
                                         std::vector<cadopt::Vec3> points) {
    cadopt::GeometryView view;
    view.source_id = std::move(id);
    view.type = "LWPOLYLINE";
    view.points = std::move(points);
    view.supported = true;
    view.complete = true;
    return view;
}

static void test_signature_is_translation_rotation_invariant() {
    const auto a = make_line("a", {0.0, 0.0, 0.0}, {10.0, 0.0, 0.0});
    const auto b = make_line("b", {100.0, 200.0, 0.0}, {100.0, 210.0, 0.0});
    require(cadopt::canonical_signature(a) == cadopt::canonical_signature(b),
            "translated/rotated equal line should have the same canonical signature");
}

static void test_exact_repeat_creates_reference_candidate() {
    std::vector<cadopt::GeometryView> views{
        make_line("a", {0.0, 0.0, 0.0}, {10.0, 0.0, 0.0}),
        make_line("b", {50.0, 50.0, 0.0}, {50.0, 60.0, 0.0})
    };
    const auto set = cadopt::discover_representation_candidates(views);
    const auto found = std::find_if(set.candidates.begin(), set.candidates.end(), [](const auto& c) {
        return c.kind == cadopt::CandidateKind::ReferenceTransform && c.source_ids.size() == 2;
    });
    require(found != set.candidates.end(), "exact repeat reference candidate was not discovered");
    require(found->preserves_selection_cardinality,
            "reference candidate must restore original selection units");
    require(!found->transform_chain.empty(), "reference candidate must carry an ordered transform chain");
}

static void test_near_repeat_creates_residual_candidate() {
    std::vector<cadopt::GeometryView> views{
        make_line("a", {0.0, 0.0, 0.0}, {10.0, 0.0, 0.0}),
        make_line("b", {100.0, 0.0, 0.0}, {110.0004, 0.0, 0.0})
    };
    const auto set = cadopt::discover_representation_candidates(views);
    const auto found = std::find_if(set.candidates.begin(), set.candidates.end(), [](const auto& c) {
        return c.kind == cadopt::CandidateKind::ReferenceResidual;
    });
    require(found != set.candidates.end(), "near repeat residual candidate was not discovered");
    require(found->residual_estimated_bytes > 0.0,
            "near repeat candidate must account for residual bytes");
}

static void test_reflection_family_creates_symmetry_candidate() {
    std::vector<cadopt::GeometryView> views{
        make_polyline("left", {{0.0, 0.0, 0.0}, {2.0, 0.0, 0.0}, {2.0, 1.0, 0.0}}),
        make_polyline("right", {{10.0, 0.0, 0.0}, {12.0, 0.0, 0.0}, {12.0, -1.0, 0.0}})
    };
    const auto set = cadopt::discover_representation_candidates(views);
    const auto found = std::find_if(set.candidates.begin(), set.candidates.end(), [](const auto& c) {
        return c.kind == cadopt::CandidateKind::Symmetry && c.source_ids.size() == 2;
    });
    require(found != set.candidates.end(), "reflection-related geometry should create a symmetry candidate");
    require(std::find(found->transform_chain.begin(), found->transform_chain.end(), "reflection_symmetry")
                != found->transform_chain.end(),
            "symmetry candidate must record reflection in its ordered transform chain");
}

static void test_polyline_creates_primitive_reduction_probe() {
    std::vector<cadopt::GeometryView> views{
        make_polyline("poly", {{0.0, 0.0, 0.0}, {1.0, 0.1, 0.0}, {2.0, 0.0, 0.0}})
    };
    const auto set = cadopt::discover_representation_candidates(views);
    const auto found = std::find_if(set.candidates.begin(), set.candidates.end(), [](const auto& c) {
        return c.kind == cadopt::CandidateKind::PrimitiveReductionProbe;
    });
    require(found != set.candidates.end(), "polyline primitive reduction hook was not generated");
    require(found->source_ids.size() == 1 && found->source_ids.front() == "poly",
            "primitive probe must preserve original source-unit coverage");
    require(std::find(found->transform_chain.begin(), found->transform_chain.end(), "capture_residual")
                != found->transform_chain.end(),
            "primitive probe must include residual capture before reconstruction");
}

static void test_numeric_quantization_candidate_is_whole_drawing_and_explicit() {
    std::vector<cadopt::GeometryView> views{
        make_line("a", {0.123456789, 0.0, 0.0}, {10.123456789, 0.0, 0.0}),
        make_line("b", {20.123456789, 0.0, 0.0}, {30.123456789, 0.0, 0.0})
    };
    cadopt::CandidateDiscoveryOptions options;
    options.numeric_quantization_steps = {1.0e-9, 5.0e-9};
    const auto set = cadopt::discover_representation_candidates(views, options);

    const auto found = std::find_if(set.candidates.begin(), set.candidates.end(), [](const auto& c) {
        return c.kind == cadopt::CandidateKind::NumericQuantization
            && std::abs(c.numeric_parameter - 5.0e-9) < 1.0e-18;
    });
    require(found != set.candidates.end(), "numeric quantization candidate was not discovered");
    require(found->scope.kind == cadopt::CandidateScopeKind::WholeDrawing,
            "numeric quantization must be represented as an explicit whole-drawing candidate");
    require(found->source_ids == set.source_universe,
            "whole-drawing quantization candidate must cover the complete source universe");
    require(found->preserves_selection_cardinality,
            "numeric quantization must never change customer-visible selection cardinality");
    require(found->loss_risk > 0.0,
            "numeric quantization must be marked as potentially lossy");
}

static void test_candidate_json_exposes_conflicts_and_transform_order() {
    std::vector<cadopt::GeometryView> views{
        make_line("a", {0.0, 0.0, 0.0}, {10.0, 0.0, 0.0})
    };
    auto set = cadopt::discover_representation_candidates(views);
    set.candidates.front().conflicts = {"quantize:1.000000e-10"};
    const auto json = cadopt::candidate_set_to_json(set);
    require(json.find("\"conflicts\"") != std::string::npos,
            "candidate JSON must expose explicit conflicts to Python search");
    require(json.find("\"transform_chain\"") != std::string::npos,
            "candidate JSON must expose ordered transform chain");
}

static void test_candidate_validation_rejects_unknown_source() {
    cadopt::CandidateSet set;
    set.source_universe = {"a"};
    cadopt::CandidateRecipe candidate;
    candidate.id = "bad";
    candidate.source_ids = {"missing"};
    candidate.transform_chain = {"test"};
    candidate.preserves_selection_cardinality = true;
    set.candidates.push_back(candidate);
    const auto validation = cadopt::validate_candidate_set(set);
    require(!validation.pass, "unknown source id must fail candidate validation");
}

int main() {
    try {
        test_signature_is_translation_rotation_invariant();
        test_exact_repeat_creates_reference_candidate();
        test_near_repeat_creates_residual_candidate();
        test_reflection_family_creates_symmetry_candidate();
        test_polyline_creates_primitive_reduction_probe();
        test_numeric_quantization_candidate_is_whole_drawing_and_explicit();
        test_candidate_json_exposes_conflicts_and_transform_order();
        test_candidate_validation_rejects_unknown_source();
        std::cout << "cadopt_candidate_tests: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "cadopt_candidate_tests: FAIL: " << error.what() << '\n';
        return 1;
    }
}
