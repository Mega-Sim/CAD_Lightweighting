#pragma once
#include <cadopt/dxf.hpp>
#include <cstddef>
#include <string>
#include <vector>

namespace cadopt {

struct VerificationIssue {
    std::string source_id;
    std::string category;
    std::string detail;
};

struct VerificationReport {
    bool pass{true};
    std::size_t source_entities{};
    std::size_t candidate_entities{};
    std::size_t entity_type_mismatches{};
    std::size_t layer_mismatches{};
    std::size_t semantic_record_mismatches{};
    std::vector<VerificationIssue> issues;
    std::string to_json() const;
};

VerificationReport verify_semantic_equivalence(const DxfDocument& source,
                                                const DxfDocument& candidate);

} // namespace cadopt
