#include <cadopt/dxf.hpp>
#include <cadopt/geometry.hpp>

#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

static void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

static bool near(double a, double b, double eps = 1e-9) {
    return std::abs(a - b) <= eps;
}

static void test_reversible_affine_round_trip() {
    constexpr double pi = 3.14159265358979323846;
    const auto forward = cadopt::Mat4::uniform_scale(2.5)
                       * cadopt::Mat4::rotation_z(pi / 3.0)
                       * cadopt::Mat4::rotation_y(pi / 7.0)
                       * cadopt::Mat4::translation(-1200.0, 450.0, 12.0);
    const auto transform = cadopt::make_reversible_transform(forward, "test");
    const cadopt::Vec3 source{1450.25, -88.5, 33.0};
    const auto restored = transform.restore(transform.apply(source));
    require(cadopt::distance(source, restored) < 1e-9,
            "affine forward/inverse round trip drifted");
}

static void test_axis_permutation_is_reversible() {
    const auto matrix = cadopt::Mat4::axis_permutation({1, 0, 2}, {1.0, -1.0, 1.0});
    const auto transform = cadopt::make_reversible_transform(matrix, "swap_xy");
    const cadopt::Vec3 source{2.0, 3.0, 4.0};
    const auto mapped = transform.apply(source);
    require(near(mapped.x, 3.0) && near(mapped.y, -2.0) && near(mapped.z, 4.0),
            "axis permutation produced unexpected coordinates");
    require(cadopt::distance(source, transform.restore(mapped)) < 1e-12,
            "axis permutation did not restore source coordinates");
}

static void test_angle_normalization() {
    require(near(cadopt::normalize_angle_deg(360.0), 0.0), "360 must normalize to zero");
    require(near(cadopt::normalize_angle_deg(-360.0), 0.0), "-360 must normalize to zero");
    require(near(cadopt::normalize_angle_deg(450.0), 90.0), "450 must normalize to 90");
    require(near(cadopt::normalize_angle_deg(-90.0), 270.0), "-90 must normalize to 270");
}

static void test_canonical_line_preserves_shape_and_is_reversible() {
    cadopt::GeometryView view;
    view.source_id = "line-1";
    view.type = "LINE";
    view.points = {{100.0, 200.0, 0.0}, {110.0, 210.0, 0.0}};
    view.supported = true;
    view.complete = true;

    const auto transform = cadopt::make_canonical_transform(view, true);
    const auto canonical = cadopt::transform_geometry_view(view, transform.forward);
    require(canonical.points.size() == 2, "canonical line lost points");
    require(std::abs(canonical.points[0].y) < 1e-9
            && std::abs(canonical.points[1].y) < 1e-9,
            "canonical line was not aligned to the X axis");
    require(cadopt::max_roundtrip_drift(view, transform) < 1e-9,
            "canonical line round trip drifted");
}

static void test_fixture_geometry_extraction_is_non_destructive() {
    const fs::path input = fs::path(CADOPT_TEST_FIXTURE_DIR) / "minimal.dxf";
    const auto document = cadopt::DxfDocument::read(input);
    const auto views = cadopt::extract_geometry_views(document);
    require(views.size() == document.entities().size(),
            "geometry extraction changed entity cardinality");
    require(views.size() == 3, "minimal fixture should contain three entities");
    require(views[0].type == "LINE" && views[0].supported && views[0].complete,
            "LINE geometry extraction failed");
    require(views[1].type == "ARC" && views[1].supported && views[1].complete,
            "ARC geometry extraction failed");
    require(views[2].type == "TEXT" && views[2].supported && views[2].complete,
            "TEXT geometry extraction failed");
}

int main() {
    try {
        test_reversible_affine_round_trip();
        test_axis_permutation_is_reversible();
        test_angle_normalization();
        test_canonical_line_preserves_shape_and_is_reversible();
        test_fixture_geometry_extraction_is_non_destructive();
        std::cout << "cadopt_geometry_tests: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "cadopt_geometry_tests: FAIL: " << error.what() << '\n';
        return 1;
    }
}
