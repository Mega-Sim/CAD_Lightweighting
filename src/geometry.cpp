#include <cadopt/geometry.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace cadopt {
namespace {

constexpr double kEpsilon = 1e-12;

std::string trim(std::string text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

std::optional<double> nth_double(const DxfDocument& document,
                                 const DxfEntity& entity,
                                 int code,
                                 std::size_t occurrence = 0) {
    std::size_t seen = 0;
    for (std::size_t i = entity.first_record;
         i < entity.last_record_exclusive && i < document.records().size(); ++i) {
        const auto& record = document.records()[i];
        if (record.code != code) continue;
        if (seen++ != occurrence) continue;
        const auto text = trim(record.value);
        if (text.empty()) return std::nullopt;
        char* end = nullptr;
        const double value = std::strtod(text.c_str(), &end);
        if (end == text.c_str() || (end && *end != '\0') || !std::isfinite(value)) {
            return std::nullopt;
        }
        return value;
    }
    return std::nullopt;
}

std::vector<double> all_doubles(const DxfDocument& document,
                                const DxfEntity& entity,
                                int code) {
    std::vector<double> values;
    for (std::size_t occurrence = 0;; ++occurrence) {
        const auto value = nth_double(document, entity, code, occurrence);
        if (!value) break;
        values.push_back(*value);
    }
    return values;
}

Vec3 coordinate(const DxfDocument& document,
                const DxfEntity& entity,
                int x_code,
                int y_code,
                int z_code,
                std::size_t occurrence,
                bool& complete) {
    const auto x = nth_double(document, entity, x_code, occurrence);
    const auto y = nth_double(document, entity, y_code, occurrence);
    const auto z = nth_double(document, entity, z_code, occurrence);
    complete = x.has_value() && y.has_value();
    return {x.value_or(0.0), y.value_or(0.0), z.value_or(0.0)};
}

double uniform_scale_factor(const Mat4& matrix) {
    return std::sqrt(matrix.v[0] * matrix.v[0]
                   + matrix.v[4] * matrix.v[4]
                   + matrix.v[8] * matrix.v[8]);
}

} // namespace

Mat4 Mat4::identity() {
    Mat4 out;
    out.v = {1.0, 0.0, 0.0, 0.0,
             0.0, 1.0, 0.0, 0.0,
             0.0, 0.0, 1.0, 0.0,
             0.0, 0.0, 0.0, 1.0};
    return out;
}

Mat4 Mat4::translation(double tx, double ty, double tz) {
    auto out = identity();
    out.v[3] = tx;
    out.v[7] = ty;
    out.v[11] = tz;
    return out;
}

Mat4 Mat4::rotation_x(double radians) {
    auto out = identity();
    const double c = std::cos(radians);
    const double s = std::sin(radians);
    out.v[5] = c;
    out.v[6] = -s;
    out.v[9] = s;
    out.v[10] = c;
    return out;
}

Mat4 Mat4::rotation_y(double radians) {
    auto out = identity();
    const double c = std::cos(radians);
    const double s = std::sin(radians);
    out.v[0] = c;
    out.v[2] = s;
    out.v[8] = -s;
    out.v[10] = c;
    return out;
}

Mat4 Mat4::rotation_z(double radians) {
    auto out = identity();
    const double c = std::cos(radians);
    const double s = std::sin(radians);
    out.v[0] = c;
    out.v[1] = -s;
    out.v[4] = s;
    out.v[5] = c;
    return out;
}

Mat4 Mat4::uniform_scale(double scale) {
    if (!std::isfinite(scale) || std::abs(scale) < kEpsilon) {
        throw std::invalid_argument("uniform scale must be finite and non-zero");
    }
    auto out = identity();
    out.v[0] = scale;
    out.v[5] = scale;
    out.v[10] = scale;
    return out;
}

Mat4 Mat4::axis_permutation(const std::array<int, 3>& axes,
                            const std::array<double, 3>& signs) {
    std::array<bool, 3> used{false, false, false};
    Mat4 out{};
    out.v[15] = 1.0;
    for (std::size_t row = 0; row < 3; ++row) {
        const int axis = axes[row];
        if (axis < 0 || axis > 2 || used[static_cast<std::size_t>(axis)]) {
            throw std::invalid_argument("axis permutation must contain each axis exactly once");
        }
        if (!std::isfinite(signs[row]) || std::abs(std::abs(signs[row]) - 1.0) > kEpsilon) {
            throw std::invalid_argument("axis permutation signs must be +1 or -1");
        }
        used[static_cast<std::size_t>(axis)] = true;
        out.v[row * 4 + static_cast<std::size_t>(axis)] = signs[row];
    }
    return out;
}

Mat4 operator*(const Mat4& lhs, const Mat4& rhs) {
    Mat4 out{};
    for (std::size_t row = 0; row < 4; ++row) {
        for (std::size_t col = 0; col < 4; ++col) {
            double sum = 0.0;
            for (std::size_t k = 0; k < 4; ++k) {
                sum += lhs.v[row * 4 + k] * rhs.v[k * 4 + col];
            }
            out.v[row * 4 + col] = sum;
        }
    }
    return out;
}

Vec3 transform_point(const Mat4& matrix, const Vec3& point) {
    return {
        matrix.v[0] * point.x + matrix.v[1] * point.y + matrix.v[2] * point.z + matrix.v[3],
        matrix.v[4] * point.x + matrix.v[5] * point.y + matrix.v[6] * point.z + matrix.v[7],
        matrix.v[8] * point.x + matrix.v[9] * point.y + matrix.v[10] * point.z + matrix.v[11]
    };
}

Mat4 inverse_affine(const Mat4& matrix) {
    const double a = matrix.v[0], b = matrix.v[1], c = matrix.v[2];
    const double d = matrix.v[4], e = matrix.v[5], f = matrix.v[6];
    const double g = matrix.v[8], h = matrix.v[9], i = matrix.v[10];
    const double det = a * (e * i - f * h)
                     - b * (d * i - f * g)
                     + c * (d * h - e * g);
    if (!std::isfinite(det) || std::abs(det) < kEpsilon) {
        throw std::invalid_argument("affine transform is not invertible");
    }

    const double inv_det = 1.0 / det;
    Mat4 out = Mat4::identity();
    out.v[0] =  (e * i - f * h) * inv_det;
    out.v[1] = -(b * i - c * h) * inv_det;
    out.v[2] =  (b * f - c * e) * inv_det;
    out.v[4] = -(d * i - f * g) * inv_det;
    out.v[5] =  (a * i - c * g) * inv_det;
    out.v[6] = -(a * f - c * d) * inv_det;
    out.v[8] =  (d * h - e * g) * inv_det;
    out.v[9] = -(a * h - b * g) * inv_det;
    out.v[10] = (a * e - b * d) * inv_det;

    const Vec3 t{matrix.v[3], matrix.v[7], matrix.v[11]};
    out.v[3] = -(out.v[0] * t.x + out.v[1] * t.y + out.v[2] * t.z);
    out.v[7] = -(out.v[4] * t.x + out.v[5] * t.y + out.v[6] * t.z);
    out.v[11] = -(out.v[8] * t.x + out.v[9] * t.y + out.v[10] * t.z);
    return out;
}

double distance(const Vec3& a, const Vec3& b) {
    const double dx = a.x - b.x;
    const double dy = a.y - b.y;
    const double dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

double normalize_angle_deg(double degrees) {
    if (!std::isfinite(degrees)) return degrees;
    double value = std::fmod(degrees, 360.0);
    if (value < 0.0) value += 360.0;
    if (std::abs(value) < kEpsilon || std::abs(value - 360.0) < kEpsilon) return 0.0;
    return value;
}

GeometryView extract_geometry_view(const DxfDocument& document, const DxfEntity& entity) {
    GeometryView view;
    view.source_id = entity.source_id;
    view.type = entity.type;

    if (entity.type == "LINE") {
        view.supported = true;
        bool first_ok = false;
        bool second_ok = false;
        view.points.push_back(coordinate(document, entity, 10, 20, 30, 0, first_ok));
        view.points.push_back(coordinate(document, entity, 11, 21, 31, 0, second_ok));
        view.complete = first_ok && second_ok;
        return view;
    }

    if (entity.type == "CIRCLE" || entity.type == "ARC") {
        view.supported = true;
        bool center_ok = false;
        view.points.push_back(coordinate(document, entity, 10, 20, 30, 0, center_ok));
        const auto radius = nth_double(document, entity, 40);
        view.has_radius = radius.has_value();
        view.radius = radius.value_or(0.0);
        view.complete = center_ok && view.has_radius && view.radius >= 0.0;
        if (entity.type == "ARC") {
            const auto start = nth_double(document, entity, 50);
            const auto end = nth_double(document, entity, 51);
            view.has_angles = start.has_value() && end.has_value();
            view.start_angle_deg = normalize_angle_deg(start.value_or(0.0));
            view.end_angle_deg = normalize_angle_deg(end.value_or(0.0));
            view.complete = view.complete && view.has_angles;
        }
        return view;
    }

    if (entity.type == "LWPOLYLINE") {
        view.supported = true;
        const auto xs = all_doubles(document, entity, 10);
        const auto ys = all_doubles(document, entity, 20);
        const auto elevation = nth_double(document, entity, 38).value_or(0.0);
        const auto count = std::min(xs.size(), ys.size());
        view.points.reserve(count);
        for (std::size_t index = 0; index < count; ++index) {
            view.points.push_back({xs[index], ys[index], elevation});
        }
        view.complete = !view.points.empty() && xs.size() == ys.size();
        return view;
    }

    if (entity.type == "TEXT") {
        view.supported = true;
        bool insertion_ok = false;
        view.points.push_back(coordinate(document, entity, 10, 20, 30, 0, insertion_ok));
        const auto height = nth_double(document, entity, 40);
        const auto rotation = nth_double(document, entity, 50);
        view.has_text_height = height.has_value();
        view.text_height = height.value_or(0.0);
        view.has_rotation = rotation.has_value();
        view.rotation_deg = normalize_angle_deg(rotation.value_or(0.0));
        view.complete = insertion_ok && view.has_text_height;
        return view;
    }

    view.supported = false;
    view.complete = false;
    return view;
}

std::vector<GeometryView> extract_geometry_views(const DxfDocument& document) {
    std::vector<GeometryView> result;
    result.reserve(document.entities().size());
    for (const auto& entity : document.entities()) {
        result.push_back(extract_geometry_view(document, entity));
    }
    return result;
}

ReversibleTransform make_reversible_transform(const Mat4& forward,
                                              std::string description) {
    ReversibleTransform transform;
    transform.forward = forward;
    transform.inverse = inverse_affine(forward);
    transform.description = std::move(description);
    return transform;
}

ReversibleTransform make_canonical_transform(const GeometryView& view,
                                             bool normalize_uniform_scale) {
    if (!view.supported || !view.complete || view.points.empty()) {
        return make_reversible_transform(Mat4::identity(),
                                         "canonical:identity(unsupported_or_incomplete)");
    }

    Vec3 centroid{};
    for (const auto& point : view.points) {
        centroid.x += point.x;
        centroid.y += point.y;
        centroid.z += point.z;
    }
    const double inv_count = 1.0 / static_cast<double>(view.points.size());
    centroid.x *= inv_count;
    centroid.y *= inv_count;
    centroid.z *= inv_count;

    const Mat4 translate = Mat4::translation(-centroid.x, -centroid.y, -centroid.z);
    double angle = 0.0;
    if (view.points.size() > 1) {
        for (std::size_t index = 1; index < view.points.size(); ++index) {
            const double dx = view.points[index].x - view.points[0].x;
            const double dy = view.points[index].y - view.points[0].y;
            if (std::hypot(dx, dy) > kEpsilon) {
                angle = std::atan2(dy, dx);
                break;
            }
        }
    }

    Mat4 forward = Mat4::rotation_z(-angle) * translate;
    double applied_scale = 1.0;
    if (normalize_uniform_scale) {
        double extent = 0.0;
        for (const auto& point : view.points) {
            const auto transformed = transform_point(forward, point);
            extent = std::max(extent, std::sqrt(transformed.x * transformed.x
                                              + transformed.y * transformed.y
                                              + transformed.z * transformed.z));
        }
        if (extent > kEpsilon) {
            applied_scale = 1.0 / extent;
            forward = Mat4::uniform_scale(applied_scale) * forward;
        }
    }

    std::ostringstream description;
    description << "canonical:centroid=(" << centroid.x << ',' << centroid.y << ',' << centroid.z
                << "),rz_rad=" << -angle << ",scale=" << applied_scale;
    return make_reversible_transform(forward, description.str());
}

GeometryView transform_geometry_view(const GeometryView& view, const Mat4& transform) {
    GeometryView out = view;
    for (auto& point : out.points) point = transform_point(transform, point);
    const double scale = uniform_scale_factor(transform);
    if (out.has_radius) out.radius *= scale;
    if (out.has_text_height) out.text_height *= scale;
    return out;
}

double max_roundtrip_drift(const GeometryView& view,
                           const ReversibleTransform& transform) {
    const auto transformed = transform_geometry_view(view, transform.forward);
    const auto restored = transform_geometry_view(transformed, transform.inverse);
    if (view.points.size() != restored.points.size()) {
        return std::numeric_limits<double>::infinity();
    }
    double max_drift = 0.0;
    for (std::size_t index = 0; index < view.points.size(); ++index) {
        max_drift = std::max(max_drift, distance(view.points[index], restored.points[index]));
    }
    if (view.has_radius) max_drift = std::max(max_drift, std::abs(view.radius - restored.radius));
    if (view.has_text_height) {
        max_drift = std::max(max_drift, std::abs(view.text_height - restored.text_height));
    }
    return max_drift;
}

} // namespace cadopt
