#include <cadopt/verifier.hpp>
#include <iomanip>
#include <sstream>

namespace cadopt {
namespace {
std::string escape_json(const std::string& s) {
    std::ostringstream out;
    for (char c : s) {
        if (c == '"') out << "\\\"";
        else if (c == '\\') out << "\\\\";
        else if (c == '\n') out << "\\n";
        else out << c;
    }
    return out.str();
}
}

VerificationReport verify_semantic_equivalence(const DxfDocument& source,
                                                const DxfDocument& candidate) {
    VerificationReport report;
    report.source_entities = source.entities().size();
    report.candidate_entities = candidate.entities().size();
    if (report.source_entities != report.candidate_entities) {
        report.pass = false;
        report.issues.push_back({"document", "entity_count",
            "source=" + std::to_string(report.source_entities) + ", candidate=" + std::to_string(report.candidate_entities)});
    }

    const auto n = std::min(source.entities().size(), candidate.entities().size());
    for (std::size_t i = 0; i < n; ++i) {
        const auto& s = source.entities()[i];
        const auto& c = candidate.entities()[i];
        if (s.type != c.type) {
            report.pass = false;
            ++report.entity_type_mismatches;
            report.issues.push_back({s.source_id, "entity_type", s.type + " -> " + c.type});
            continue;
        }
        if (s.layer != c.layer) {
            report.pass = false;
            ++report.layer_mismatches;
            report.issues.push_back({s.source_id, "layer", s.layer + " -> " + c.layer});
        }
        if (source.semantic_fingerprint(s) != candidate.semantic_fingerprint(c)) {
            report.pass = false;
            ++report.semantic_record_mismatches;
            report.issues.push_back({s.source_id, "entity_records", "group-code/value content changed"});
        }
    }
    return report;
}

std::string VerificationReport::to_json() const {
    std::ostringstream out;
    out << "{\n"
        << "  \"pass\": " << (pass ? "true" : "false") << ",\n"
        << "  \"source_entities\": " << source_entities << ",\n"
        << "  \"candidate_entities\": " << candidate_entities << ",\n"
        << "  \"entity_type_mismatches\": " << entity_type_mismatches << ",\n"
        << "  \"layer_mismatches\": " << layer_mismatches << ",\n"
        << "  \"semantic_record_mismatches\": " << semantic_record_mismatches << ",\n"
        << "  \"issues\": [";
    for (std::size_t i = 0; i < issues.size(); ++i) {
        if (i) out << ',';
        out << "\n    {\"source_id\": \"" << escape_json(issues[i].source_id)
            << "\", \"category\": \"" << escape_json(issues[i].category)
            << "\", \"detail\": \"" << escape_json(issues[i].detail) << "\"}";
    }
    if (!issues.empty()) out << '\n' << "  ";
    out << "]\n}";
    return out.str();
}

} // namespace cadopt
