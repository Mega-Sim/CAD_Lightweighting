#pragma once

#include <cadopt/dxf.hpp>

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace cadopt {

struct Vec3 {
    double x{};
    double y{};
    double z{};
};

struct Mat4 {
    std::array<double, 16> v{};

    static Mat4 identity();
    static Mat4 translation(double tx, double ty, double tz);
    static Mat4 rotation_x(double radians);
    static Mat4 rotation_y(double radians);
    static Mat4 rotation_z(double radians);
    static Mat4 uniform_scale(double scale);
    static Mat4 axis_permutation(const std::array<int, 3>& axes,
                                 const std::array<double, 3>& signs = {1.0, 1.0, 1.0});
};

Mat4 operator*(const Mat4& lhs, const Mat4& rhs);
Vec3 transform_point(const Mat4& matrix, const Vec3& point);
Mat4 inverse_affine(const Mat4& matrix);
double distance(const Vec3& a, const Vec3& b);
double normalize_angle_deg(double degrees);

struct ReversibleTransform {
    Mat4 forward{Mat4::identity()};
    Mat4 inverse{Mat4::identity()};
    std::string description{"identity"};

    Vec3 apply(const Vec3& point) const { return transform_point(forward, point); }
    Vec3 restore(const Vec3& point) const { return transform_point(inverse, point); }
};

struct GeometryView {
    std::string source_id;
    std::string type;
    std::vector<Vec3> points;

    double radius{};
    double start_angle_deg{};
    double end_angle_deg{};
    double rotation_deg{};
    double text_height{};

    bool has_radius{};
    bool has_angles{};
    bool has_rotation{};
    bool has_text_height{};
    bool supported{};
    bool complete{};
};

GeometryView extract_geometry_view(const DxfDocument& document, const DxfEntity& entity);
std::vector<GeometryView> extract_geometry_views(const DxfDocument& document);

ReversibleTransform make_reversible_transform(const Mat4& forward,
                                              std::string description);
ReversibleTransform make_canonical_transform(const GeometryView& view,
                                             bool normalize_uniform_scale = false);
GeometryView transform_geometry_view(const GeometryView& view, const Mat4& transform);
double max_roundtrip_drift(const GeometryView& view,
                           const ReversibleTransform& transform);

} // namespace cadopt
