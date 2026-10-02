#pragma once
#include <cadopt/dxf.hpp>
#include <cadopt/trace.hpp>
#include <cstddef>
#include <string>
#include <vector>

namespace cadopt {

struct VerificationOptions {
    double absolute_geometry_tolerance{1.0e-9};
    double relative_geometry_tolerance{1.0e-9};
};

struct VerificationIssue {
    std::string source_id;
    std::string severity{"error"};
    std::string category;
    std::string detail;
    std::string metric_name;
    double metric_value{};
    bool has_metric{};
};

struct VerificationReport {
    bool pass{true};
    std::size_t source_entities{};
    std::size_t candidate_entities{};
    std::size_t source_blocks{};
    std::size_t candidate_blocks{};
    std::size_t source_objects{};
    std::size_t candidate_objects{};
    std::size_t entity_type_mismatches{};
    std::size_t layer_mismatches{};
    std::size_t geometry_mismatches{};
    std::size_t semantic_record_mismatches{};
    std::size_t block_mismatches{};
    std::size_t object_mismatches{};
    std::size_t xdata_mismatches{};
    std::size_t interaction_mismatches{};
    std::size_t reference_mismatches{};
    std::vector<VerificationIssue> issues;
    std::string to_json() const;
};

VerificationReport verify_semantic_equivalence(const DxfDocument& source,
                                                const DxfDocument& candidate,
                                                const VerificationOptions& options = {});

void append_verification_issues_to_trace(TraceLedger& trace,
                                         const VerificationReport& report);

} // namespace cadopt
