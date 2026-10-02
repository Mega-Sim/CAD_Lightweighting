#pragma once

#include <cadopt/geometry.hpp>

#include <cstddef>
#include <string>
#include <vector>

namespace cadopt {

enum class CandidateKind {
    Raw,
    ReferenceTransform,
    ReferenceResidual,
    Symmetry,
    Grid,
    SequenceGrammar,
    TensorProbe,
    WaveletProbe,
    SpectralProbe
};

enum class CandidateScopeKind {
    WholeDrawing,
    Region,
    Layer,
    Network,
    Component,
    Object,
    SubObject,
    Pattern
};

struct ScopeDescriptor {
    CandidateScopeKind kind{CandidateScopeKind::Object};
    std::string id;
    std::vector<std::string> source_ids;
};

struct CandidateRecipe {
    std::string id;
    CandidateKind kind{CandidateKind::Raw};
    ScopeDescriptor scope;
    std::vector<std::string> source_ids;
    std::string reference_source_id;
    std::vector<std::string> conflicts;
    std::string reconstruction_recipe;
    std::string provenance;
    double estimated_bytes{};
    double residual_estimated_bytes{};
    double loss_risk{};
    bool preserves_selection_cardinality{true};
};

struct CandidateSet {
    std::vector<std::string> source_universe;
    std::vector<CandidateRecipe> candidates;
};

struct CandidateValidation {
    bool pass{true};
    std::vector<std::string> issues;
};

std::string candidate_kind_name(CandidateKind kind);
std::string candidate_scope_name(CandidateScopeKind kind);
std::string canonical_signature(const GeometryView& view,
                                double quantization = 1e-9,
                                bool normalize_uniform_scale = false);
double estimate_raw_geometry_bytes(const GeometryView& view);
double estimate_description_bytes(const std::vector<std::string>& tokens);
CandidateSet discover_representation_candidates(const std::vector<GeometryView>& views);
CandidateValidation validate_candidate_set(const CandidateSet& set);
std::string candidate_set_to_json(const CandidateSet& set);

} // namespace cadopt
