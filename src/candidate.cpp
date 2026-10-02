#include <cadopt/candidate.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <unordered_set>

namespace cadopt {
namespace {

std::string escape_json(const std::string& text) {
    std::ostringstream out;
    for (const char c : text) {
        switch (c) {
        case '"': out << "\\\""; break;
        case '\\': out << "\\\\"; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default: out << c; break;
        }
    }
    return out.str();
}

double quantize(double value, double step) {
    if (!std::isfinite(value)) return value;
    if (!std::isfinite(step) || step <= 0.0) step = 1e-9;
    const double q = std::round(value / step) * step;
    return std::abs(q) < step * 0.5 ? 0.0 : q;
}

double sweep_deg(const GeometryView& view) {
    if (!view.has_angles) return 0.0;
    double sweep = normalize_angle_deg(view.end_angle_deg) - normalize_angle_deg(view.start_angle_deg);
    if (sweep < 0.0) sweep += 360.0;
    return sweep;
}

double estimate_residual_bytes(const GeometryView& reference,
                               const GeometryView& candidate) {
    if (reference.type != candidate.type || reference.points.size() != candidate.points.size()) {
        return estimate_raw_geometry_bytes(candidate);
    }

    const auto ref_transform = make_canonical_transform(reference, false);
    const auto cand_transform = make_canonical_transform(candidate, false);
    const auto ref = transform_geometry_view(reference, ref_transform.forward);
    const auto cand = transform_geometry_view(candidate, cand_transform.forward);

    std::size_t changed_components = 0;
    const double tolerance = 1e-9;
    for (std::size_t i = 0; i < ref.points.size(); ++i) {
        if (std::abs(ref.points[i].x - cand.points[i].x) > tolerance) ++changed_components;
        if (std::abs(ref.points[i].y - cand.points[i].y) > tolerance) ++changed_components;
        if (std::abs(ref.points[i].z - cand.points[i].z) > tolerance) ++changed_components;
    }
    if (ref.has_radius && std::abs(ref.radius - cand.radius) > tolerance) ++changed_components;
    if (ref.has_angles && std::abs(sweep_deg(ref) - sweep_deg(cand)) > tolerance) ++changed_components;
    if (ref.has_text_height && std::abs(ref.text_height - cand.text_height) > tolerance) ++changed_components;

    return 12.0 + static_cast<double>(changed_components) * 8.0;
}

CandidateRecipe raw_candidate(const GeometryView& view, std::size_t index) {
    CandidateRecipe recipe;
    recipe.id = "raw:" + std::to_string(index) + ':' + view.source_id;
    recipe.kind = CandidateKind::Raw;
    recipe.scope = {CandidateScopeKind::Object, view.source_id, {view.source_id}};
    recipe.source_ids = {view.source_id};
    recipe.reconstruction_recipe = "emit_original_source_entity";
    recipe.provenance = "immutable_source_truth";
    recipe.estimated_bytes = estimate_raw_geometry_bytes(view);
    recipe.loss_risk = 0.0;
    recipe.preserves_selection_cardinality = true;
    return recipe;
}

} // namespace

std::string candidate_kind_name(CandidateKind kind) {
    switch (kind) {
    case CandidateKind::Raw: return "raw";
    case CandidateKind::ReferenceTransform: return "reference_transform";
    case CandidateKind::ReferenceResidual: return "reference_residual";
    case CandidateKind::Symmetry: return "symmetry";
    case CandidateKind::Grid: return "grid";
    case CandidateKind::SequenceGrammar: return "sequence_grammar";
    case CandidateKind::TensorProbe: return "tensor_probe";
    case CandidateKind::WaveletProbe: return "wavelet_probe";
    case CandidateKind::SpectralProbe: return "spectral_probe";
    }
    return "unknown";
}

std::string candidate_scope_name(CandidateScopeKind kind) {
    switch (kind) {
    case CandidateScopeKind::WholeDrawing: return "whole_drawing";
    case CandidateScopeKind::Region: return "region";
    case CandidateScopeKind::Layer: return "layer";
    case CandidateScopeKind::Network: return "network";
    case CandidateScopeKind::Component: return "component";
    case CandidateScopeKind::Object: return "object";
    case CandidateScopeKind::SubObject: return "sub_object";
    case CandidateScopeKind::Pattern: return "pattern";
    }
    return "unknown";
}

std::string canonical_signature(const GeometryView& view,
                                double quantization,
                                bool normalize_uniform_scale) {
    if (!view.supported || !view.complete) {
        return "unsupported:" + view.type + ':' + view.source_id;
    }
    // TEXT contents are semantic, not only geometric. Until text payload is part of
    // GeometryView, keep TEXT instances isolated rather than risking false deduplication.
    if (view.type == "TEXT") return "text-isolated:" + view.source_id;

    const auto transform = make_canonical_transform(view, normalize_uniform_scale);
    const auto canonical = transform_geometry_view(view, transform.forward);

    std::ostringstream out;
    out << canonical.type << '|';
    out << std::fixed << std::setprecision(9);
    out << canonical.points.size() << '|';
    for (const auto& point : canonical.points) {
        out << quantize(point.x, quantization) << ','
            << quantize(point.y, quantization) << ','
            << quantize(point.z, quantization) << ';';
    }
    if (canonical.has_radius) out << "r=" << quantize(canonical.radius, quantization) << '|';
    if (canonical.has_angles) out << "sweep=" << quantize(sweep_deg(canonical), quantization) << '|';
    if (canonical.has_text_height) {
        out << "h=" << quantize(canonical.text_height, quantization) << '|';
    }
    return out.str();
}

double estimate_raw_geometry_bytes(const GeometryView& view) {
    double bytes = 24.0 + static_cast<double>(view.type.size());
    bytes += static_cast<double>(view.points.size()) * 3.0 * sizeof(double);
    if (view.has_radius) bytes += sizeof(double);
    if (view.has_angles) bytes += 2.0 * sizeof(double);
    if (view.has_rotation) bytes += sizeof(double);
    if (view.has_text_height) bytes += sizeof(double);
    if (!view.supported || !view.complete) bytes += 64.0;
    return bytes;
}

double estimate_description_bytes(const std::vector<std::string>& tokens) {
    std::array<std::size_t, 256> counts{};
    std::size_t total = 0;
    for (const auto& token : tokens) {
        for (const unsigned char c : token) {
            ++counts[c];
            ++total;
        }
        ++counts[0];
        ++total;
    }
    if (total == 0) return 0.0;

    double entropy_bits_per_symbol = 0.0;
    for (const auto count : counts) {
        if (count == 0) continue;
        const double p = static_cast<double>(count) / static_cast<double>(total);
        entropy_bits_per_symbol -= p * std::log2(p);
    }
    const double entropy_bytes = entropy_bits_per_symbol * static_cast<double>(total) / 8.0;
    return std::max(1.0, entropy_bytes + static_cast<double>(tokens.size()) * 2.0);
}

CandidateSet discover_representation_candidates(const std::vector<GeometryView>& views) {
    CandidateSet result;
    result.source_universe.reserve(views.size());

    std::map<std::string, std::vector<std::size_t>> exact_groups;
    std::map<std::string, std::vector<std::size_t>> near_groups;
    double raw_total = 0.0;

    for (std::size_t index = 0; index < views.size(); ++index) {
        const auto& view = views[index];
        result.source_universe.push_back(view.source_id);
        auto raw = raw_candidate(view, index);
        raw_total += raw.estimated_bytes;
        result.candidates.push_back(std::move(raw));

        if (!view.supported || !view.complete || view.type == "TEXT") continue;
        exact_groups[canonical_signature(view, 1e-9, false)].push_back(index);
        near_groups[canonical_signature(view, 1e-3, false)].push_back(index);
    }

    std::size_t group_number = 0;
    double estimated_structural_savings = 0.0;
    for (const auto& [signature, indices] : exact_groups) {
        if (indices.size() < 2) continue;
        const auto& reference = views[indices.front()];
        CandidateRecipe recipe;
        recipe.id = "ref:" + std::to_string(group_number++);
        recipe.kind = CandidateKind::ReferenceTransform;
        recipe.scope.kind = CandidateScopeKind::Pattern;
        recipe.scope.id = recipe.id;
        recipe.reference_source_id = reference.source_id;
        recipe.reconstruction_recipe = "expand_reference_transform_then_restore_original_entity_units";
        recipe.provenance = "exact_transform_invariant_signature:" + signature;
        recipe.loss_risk = 0.0;
        recipe.preserves_selection_cardinality = true;
        double raw_group = 0.0;
        for (const auto index : indices) {
            recipe.source_ids.push_back(views[index].source_id);
            recipe.scope.source_ids.push_back(views[index].source_id);
            raw_group += estimate_raw_geometry_bytes(views[index]);
        }
        recipe.estimated_bytes = estimate_raw_geometry_bytes(reference)
                               + static_cast<double>(indices.size()) * 48.0
                               + estimate_description_bytes({signature});
        if (recipe.estimated_bytes < raw_group) {
            estimated_structural_savings += raw_group - recipe.estimated_bytes;
        }
        result.candidates.push_back(recipe);

        if (indices.size() >= 3) {
            auto grid = recipe;
            grid.id = "grid:" + std::to_string(group_number++);
            grid.kind = CandidateKind::Grid;
            grid.scope.id = grid.id;
            grid.provenance = "repeated_transform_family_grid_probe";
            grid.reconstruction_recipe = "expand_grid_placements_then_restore_original_entity_units";
            grid.estimated_bytes = estimate_raw_geometry_bytes(reference)
                                 + 32.0 * static_cast<double>(indices.size())
                                 + 48.0;
            result.candidates.push_back(std::move(grid));
        }
        if (indices.size() >= 4) {
            auto grammar = recipe;
            grammar.id = "grammar:" + std::to_string(group_number++);
            grammar.kind = CandidateKind::SequenceGrammar;
            grammar.scope.id = grammar.id;
            grammar.provenance = "repeated_exact_signature_sequence_probe";
            grammar.reconstruction_recipe = "expand_grammar_tokens_then_restore_original_entity_units";
            grammar.estimated_bytes = estimate_raw_geometry_bytes(reference)
                                    + 20.0 * static_cast<double>(indices.size())
                                    + 64.0;
            result.candidates.push_back(std::move(grammar));
        }
    }

    std::size_t near_number = 0;
    for (const auto& [signature, indices] : near_groups) {
        if (indices.size() < 2) continue;
        std::set<std::string> exact_signatures;
        for (const auto index : indices) {
            exact_signatures.insert(canonical_signature(views[index], 1e-9, false));
        }
        if (exact_signatures.size() <= 1) continue;

        const auto& reference = views[indices.front()];
        CandidateRecipe recipe;
        recipe.id = "residual:" + std::to_string(near_number++);
        recipe.kind = CandidateKind::ReferenceResidual;
        recipe.scope.kind = CandidateScopeKind::Pattern;
        recipe.scope.id = recipe.id;
        recipe.reference_source_id = reference.source_id;
        recipe.reconstruction_recipe = "decode_reference_plus_residual_then_restore_original_entity_units";
        recipe.provenance = "near_transform_invariant_signature:" + signature;
        recipe.loss_risk = 0.25;
        recipe.preserves_selection_cardinality = true;
        recipe.estimated_bytes = estimate_raw_geometry_bytes(reference);
        for (const auto index : indices) {
            recipe.source_ids.push_back(views[index].source_id);
            recipe.scope.source_ids.push_back(views[index].source_id);
            if (index != indices.front()) {
                recipe.residual_estimated_bytes += estimate_residual_bytes(reference, views[index]);
            }
        }
        recipe.estimated_bytes += recipe.residual_estimated_bytes
                                + 40.0 * static_cast<double>(indices.size());
        result.candidates.push_back(std::move(recipe));
    }

    if (!result.source_universe.empty() && estimated_structural_savings > 0.0) {
        CandidateRecipe whole;
        whole.id = "whole:structural-composite";
        whole.kind = CandidateKind::SequenceGrammar;
        whole.scope = {CandidateScopeKind::WholeDrawing, "whole", result.source_universe};
        whole.source_ids = result.source_universe;
        whole.reconstruction_recipe = "compose_discovered_patterns_and_raw_fallback_then_restore_source_units";
        whole.provenance = "whole_drawing_description_length_probe";
        whole.estimated_bytes = std::max(1.0, raw_total - estimated_structural_savings * 0.8);
        whole.loss_risk = 0.1;
        whole.preserves_selection_cardinality = true;
        result.candidates.push_back(std::move(whole));
    }

    return result;
}

CandidateValidation validate_candidate_set(const CandidateSet& set) {
    CandidateValidation validation;
    std::unordered_set<std::string> universe;
    for (const auto& source_id : set.source_universe) {
        if (source_id.empty() || !universe.insert(source_id).second) {
            validation.pass = false;
            validation.issues.push_back("source universe contains empty or duplicate id: " + source_id);
        }
    }

    std::unordered_set<std::string> candidate_ids;
    for (const auto& candidate : set.candidates) {
        if (candidate.id.empty() || !candidate_ids.insert(candidate.id).second) {
            validation.pass = false;
            validation.issues.push_back("candidate id is empty or duplicated: " + candidate.id);
        }
        if (!candidate.preserves_selection_cardinality) {
            validation.pass = false;
            validation.issues.push_back(candidate.id + " does not preserve source selection cardinality");
        }
        std::unordered_set<std::string> local;
        for (const auto& source_id : candidate.source_ids) {
            if (!universe.contains(source_id)) {
                validation.pass = false;
                validation.issues.push_back(candidate.id + " references unknown source id: " + source_id);
            }
            if (!local.insert(source_id).second) {
                validation.pass = false;
                validation.issues.push_back(candidate.id + " covers source more than once: " + source_id);
            }
        }
    }
    return validation;
}

std::string candidate_set_to_json(const CandidateSet& set) {
    std::ostringstream out;
    out << "{\"source_universe\":[";
    for (std::size_t i = 0; i < set.source_universe.size(); ++i) {
        if (i) out << ',';
        out << '"' << escape_json(set.source_universe[i]) << '"';
    }
    out << "],\"candidates\":[";
    for (std::size_t i = 0; i < set.candidates.size(); ++i) {
        if (i) out << ',';
        const auto& candidate = set.candidates[i];
        out << "{\"id\":\"" << escape_json(candidate.id)
            << "\",\"kind\":\"" << candidate_kind_name(candidate.kind)
            << "\",\"scope\":\"" << candidate_scope_name(candidate.scope.kind)
            << "\",\"estimated_bytes\":" << candidate.estimated_bytes
            << ",\"residual_estimated_bytes\":" << candidate.residual_estimated_bytes
            << ",\"loss_risk\":" << candidate.loss_risk
            << ",\"preserves_selection_cardinality\":"
            << (candidate.preserves_selection_cardinality ? "true" : "false")
            << ",\"reference_source_id\":\"" << escape_json(candidate.reference_source_id)
            << "\",\"source_ids\":[";
        for (std::size_t j = 0; j < candidate.source_ids.size(); ++j) {
            if (j) out << ',';
            out << '"' << escape_json(candidate.source_ids[j]) << '"';
        }
        out << "],\"reconstruction_recipe\":\"" << escape_json(candidate.reconstruction_recipe)
            << "\",\"provenance\":\"" << escape_json(candidate.provenance) << "\"}";
    }
    out << "]}";
    return out.str();
}

} // namespace cadopt
