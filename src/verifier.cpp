#include <cadopt/verifier.hpp>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <map>
#include <optional>
#include <sstream>
#include <unordered_map>
#include <utility>
#include <vector>

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

std::string trim(std::string s) {
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

bool parse_double(const std::string& text, double& value) {
    const auto cleaned = trim(text);
    if (cleaned.empty()) return false;
    char* end = nullptr;
    value = std::strtod(cleaned.c_str(), &end);
    return end && *end == '\0';
}

bool is_real_code(int code) {
    return (code >= 10 && code <= 59)
        || (code >= 110 && code <= 149)
        || (code >= 210 && code <= 239)
        || (code >= 460 && code <= 469)
        || (code >= 1010 && code <= 1059);
}

bool is_volatile_handle_code(int code) {
    return code == 5 || code == 105 || code == 330 || code == 360;
}

bool is_xdata_code(int code) {
    return code >= 1000 && code <= 1071;
}

bool geometry_supported(const std::string& type) {
    return type == "LINE" || type == "ARC" || type == "CIRCLE"
        || type == "LWPOLYLINE" || type == "TEXT";
}

bool is_geometry_code(const std::string& type, int code) {
    if (type == "LINE") {
        return code == 10 || code == 20 || code == 30
            || code == 11 || code == 21 || code == 31;
    }
    if (type == "ARC") {
        return code == 10 || code == 20 || code == 30 || code == 40
            || code == 50 || code == 51;
    }
    if (type == "CIRCLE") {
        return code == 10 || code == 20 || code == 30 || code == 40;
    }
    if (type == "LWPOLYLINE") {
        return code == 10 || code == 20 || code == 38 || code == 39
            || code == 40 || code == 41 || code == 42 || code == 43;
    }
    if (type == "TEXT") {
        return code == 10 || code == 20 || code == 30
            || code == 11 || code == 21 || code == 31
            || code == 40 || code == 41 || code == 50 || code == 51;
    }
    return false;
}

std::string normalized_record_value(const DxfRecord& record) {
    if (!is_real_code(record.code)) return trim(record.value);
    double value = 0.0;
    if (!parse_double(record.value, value)) return trim(record.value);
    std::ostringstream out;
    out << std::setprecision(17) << value;
    return out.str();
}

std::string canonical_span(const DxfDocument& doc,
                           std::size_t first,
                           std::size_t last,
                           const std::string& entity_type,
                           bool exclude_geometry,
                           bool xdata_only,
                           bool skip_group_references) {
    std::ostringstream out;
    const auto& records = doc.records();
    last = std::min(last, records.size());
    for (std::size_t i = first; i < last; ++i) {
        const auto& record = records[i];
        if (xdata_only) {
            if (!is_xdata_code(record.code)) continue;
        } else {
            if (is_xdata_code(record.code)) continue;
            if (is_volatile_handle_code(record.code)) continue;
            if (skip_group_references && record.code == 340) continue;
            if (exclude_geometry && is_geometry_code(entity_type, record.code)) continue;
        }
        out << record.code << '=' << normalized_record_value(record) << '\n';
    }
    return out.str();
}

struct GeometryValue {
    int code{};
    double value{};
};

std::vector<GeometryValue> geometry_values(const DxfDocument& doc, const DxfEntity& entity) {
    std::vector<GeometryValue> values;
    if (!geometry_supported(entity.type)) return values;
    const auto& records = doc.records();
    const auto last = std::min(entity.last_record_exclusive, records.size());
    for (std::size_t i = entity.first_record; i < last; ++i) {
        const auto& record = records[i];
        if (!is_geometry_code(entity.type, record.code)) continue;
        double value = 0.0;
        if (!parse_double(record.value, value)) continue;
        values.push_back({record.code, value});
    }
    return values;
}

double geometry_delta(int code, double a, double b) {
    if (code == 50 || code == 51) {
        auto normalize = [](double angle) {
            angle = std::fmod(angle, 360.0);
            if (angle < 0.0) angle += 360.0;
            if (std::abs(angle - 360.0) < 1.0e-12) angle = 0.0;
            return angle;
        };
        const double na = normalize(a);
        const double nb = normalize(b);
        const double direct = std::abs(na - nb);
        return std::min(direct, 360.0 - direct);
    }
    return std::abs(a - b);
}

bool geometry_equivalent(const DxfDocument& source,
                         const DxfEntity& source_entity,
                         const DxfDocument& candidate,
                         const DxfEntity& candidate_entity,
                         const VerificationOptions& options,
                         double& max_delta,
                         std::string& detail) {
    max_delta = 0.0;
    if (!geometry_supported(source_entity.type)) return true;

    const auto source_values = geometry_values(source, source_entity);
    const auto candidate_values = geometry_values(candidate, candidate_entity);
    if (source_values.size() != candidate_values.size()) {
        detail = "geometry field count changed: source=" + std::to_string(source_values.size())
               + ", candidate=" + std::to_string(candidate_values.size());
        return false;
    }

    for (std::size_t i = 0; i < source_values.size(); ++i) {
        if (source_values[i].code != candidate_values[i].code) {
            detail = "geometry group-code sequence changed at field " + std::to_string(i);
            return false;
        }
        const double delta = geometry_delta(source_values[i].code,
                                            source_values[i].value,
                                            candidate_values[i].value);
        max_delta = std::max(max_delta, delta);
        const double scale = std::max(std::abs(source_values[i].value),
                                      std::abs(candidate_values[i].value));
        const double allowed = options.absolute_geometry_tolerance
                             + options.relative_geometry_tolerance * scale;
        if (delta > allowed) {
            std::ostringstream out;
            out << "geometry value changed at group code " << source_values[i].code
                << ": source=" << std::setprecision(17) << source_values[i].value
                << ", candidate=" << candidate_values[i].value
                << ", delta=" << delta
                << ", allowed=" << allowed;
            detail = out.str();
            return false;
        }
    }
    return true;
}

void add_issue(VerificationReport& report,
               std::string source_id,
               std::string category,
               std::string detail,
               std::string metric_name = {},
               double metric_value = 0.0,
               std::string severity = "error") {
    if (severity == "error") report.pass = false;
    VerificationIssue issue;
    issue.source_id = std::move(source_id);
    issue.severity = std::move(severity);
    issue.category = std::move(category);
    issue.detail = std::move(detail);
    issue.metric_name = std::move(metric_name);
    issue.metric_value = metric_value;
    issue.has_metric = !issue.metric_name.empty();
    report.issues.push_back(std::move(issue));
}

std::string entity_semantic_span(const DxfDocument& doc, const DxfEntity& entity) {
    return canonical_span(doc,
                          entity.first_record,
                          entity.last_record_exclusive,
                          entity.type,
                          geometry_supported(entity.type),
                          false,
                          false);
}

std::string entity_xdata_span(const DxfDocument& doc, const DxfEntity& entity) {
    return canonical_span(doc,
                          entity.first_record,
                          entity.last_record_exclusive,
                          entity.type,
                          false,
                          true,
                          false);
}

std::string entity_bucket_key(const DxfDocument& doc, const DxfEntity& entity) {
    std::ostringstream out;
    out << entity.type << '\x1f'
        << entity.layer << '\x1f'
        << entity.block_name << '\x1f'
        << entity_semantic_span(doc, entity) << '\x1f'
        << entity_xdata_span(doc, entity);
    return out.str();
}

bool entities_fully_equivalent(const DxfDocument& source,
                               const DxfEntity& source_entity,
                               const DxfDocument& candidate,
                               const DxfEntity& candidate_entity,
                               const VerificationOptions& options) {
    if (source_entity.type != candidate_entity.type
        || source_entity.layer != candidate_entity.layer
        || source_entity.block_name != candidate_entity.block_name
        || entity_semantic_span(source, source_entity) != entity_semantic_span(candidate, candidate_entity)
        || entity_xdata_span(source, source_entity) != entity_xdata_span(candidate, candidate_entity)) {
        return false;
    }
    double max_delta = 0.0;
    std::string detail;
    return geometry_equivalent(source, source_entity, candidate, candidate_entity,
                               options, max_delta, detail);
}

int diagnostic_match_score(const DxfDocument& source,
                           const DxfEntity& source_entity,
                           const DxfDocument& candidate,
                           const DxfEntity& candidate_entity,
                           const VerificationOptions& options) {
    if (source_entity.type != candidate_entity.type) return -1;
    int score = 0;
    if (source_entity.layer == candidate_entity.layer) score += 8;
    if (source_entity.block_name == candidate_entity.block_name) score += 8;
    if (entity_semantic_span(source, source_entity) == entity_semantic_span(candidate, candidate_entity)) score += 16;
    if (entity_xdata_span(source, source_entity) == entity_xdata_span(candidate, candidate_entity)) score += 8;
    double max_delta = 0.0;
    std::string detail;
    if (geometry_equivalent(source, source_entity, candidate, candidate_entity,
                            options, max_delta, detail)) {
        score += 32;
    }
    return score;
}

std::string entity_selection_signature(const DxfDocument& doc, const DxfEntity& entity) {
    std::ostringstream out;
    out << entity.type << '|' << entity.layer << '|';
    out << canonical_span(doc,
                          entity.first_record,
                          entity.last_record_exclusive,
                          entity.type,
                          false,
                          false,
                          false);
    return out.str();
}

std::vector<std::string> resolved_group_members(const DxfDocument& doc, const DxfObject& object) {
    std::unordered_map<std::string, const DxfEntity*> entities_by_handle;
    for (const auto& entity : doc.entities()) {
        if (!entity.handle.empty()) entities_by_handle[entity.handle] = &entity;
    }

    std::vector<std::string> signatures;
    signatures.reserve(object.referenced_handles.size());
    for (const auto& handle : object.referenced_handles) {
        const auto it = entities_by_handle.find(handle);
        if (it == entities_by_handle.end()) {
            signatures.push_back("UNRESOLVED:" + handle);
        } else {
            signatures.push_back(entity_selection_signature(doc, *it->second));
        }
    }
    std::sort(signatures.begin(), signatures.end());
    return signatures;
}

std::string object_key(const DxfObject& object, std::size_t ordinal) {
    return object.type + ":" + std::to_string(ordinal);
}

} // namespace

VerificationReport verify_semantic_equivalence(const DxfDocument& source,
                                                const DxfDocument& candidate,
                                                const VerificationOptions& options) {
    VerificationReport report;
    report.source_entities = source.entities().size();
    report.candidate_entities = candidate.entities().size();
    report.source_blocks = source.blocks().size();
    report.candidate_blocks = candidate.blocks().size();
    report.source_objects = source.objects().size();
    report.candidate_objects = candidate.objects().size();

    if (report.source_entities != report.candidate_entities) {
        add_issue(report,
                  "document",
                  "interaction.entity_count",
                  "source=" + std::to_string(report.source_entities)
                      + ", candidate=" + std::to_string(report.candidate_entities));
        ++report.interaction_mismatches;
    }

    const auto& source_entities = source.entities();
    const auto& candidate_entities = candidate.entities();
    std::vector<std::optional<std::size_t>> matches(source_entities.size());
    std::vector<bool> candidate_used(candidate_entities.size(), false);

    std::unordered_map<std::string, std::vector<std::size_t>> candidate_buckets;
    candidate_buckets.reserve(candidate_entities.size());
    for (std::size_t i = 0; i < candidate_entities.size(); ++i) {
        candidate_buckets[entity_bucket_key(candidate, candidate_entities[i])].push_back(i);
    }

    // First pass: consume entities that are fully equivalent. This makes DWG
    // reader/writer reordering irrelevant without weakening any semantic gate.
    for (std::size_t source_index = 0; source_index < source_entities.size(); ++source_index) {
        const auto& s = source_entities[source_index];
        const auto bucket_it = candidate_buckets.find(entity_bucket_key(source, s));
        if (bucket_it == candidate_buckets.end()) continue;
        for (const auto candidate_index : bucket_it->second) {
            if (candidate_used[candidate_index]) continue;
            if (!entities_fully_equivalent(source, s, candidate, candidate_entities[candidate_index], options)) {
                continue;
            }
            matches[source_index] = candidate_index;
            candidate_used[candidate_index] = true;
            break;
        }
    }

    // Second pass: only changed/unmatched entities remain. Match within the same
    // entity type for detailed diagnostics; never reuse a candidate selection unit.
    for (std::size_t source_index = 0; source_index < source_entities.size(); ++source_index) {
        if (matches[source_index]) continue;
        const auto& s = source_entities[source_index];
        int best_score = -1;
        std::optional<std::size_t> best_candidate;
        for (std::size_t candidate_index = 0; candidate_index < candidate_entities.size(); ++candidate_index) {
            if (candidate_used[candidate_index]) continue;
            const int score = diagnostic_match_score(source, s, candidate,
                                                     candidate_entities[candidate_index], options);
            if (score > best_score) {
                best_score = score;
                best_candidate = candidate_index;
            }
        }
        if (best_candidate) {
            matches[source_index] = *best_candidate;
            candidate_used[*best_candidate] = true;
        }
    }

    for (std::size_t source_index = 0; source_index < source_entities.size(); ++source_index) {
        const auto& s = source_entities[source_index];
        if (!matches[source_index]) {
            ++report.entity_type_mismatches;
            ++report.interaction_mismatches;
            add_issue(report,
                      s.source_id,
                      "interaction.entity_type",
                      "no unmatched candidate entity of type " + s.type + " remains");
            continue;
        }
        const auto& c = candidate_entities[*matches[source_index]];

        if (s.layer != c.layer) {
            ++report.layer_mismatches;
            add_issue(report, s.source_id, "semantic.layer", s.layer + " -> " + c.layer);
        }
        if (s.type == "INSERT" && s.block_name != c.block_name) {
            ++report.interaction_mismatches;
            ++report.reference_mismatches;
            add_issue(report,
                      s.source_id,
                      "interaction.insert_target",
                      s.block_name + " -> " + c.block_name);
        }

        double max_delta = 0.0;
        std::string geometry_detail;
        if (!geometry_equivalent(source, s, candidate, c, options, max_delta, geometry_detail)) {
            ++report.geometry_mismatches;
            add_issue(report,
                      s.source_id,
                      "geometry",
                      geometry_detail,
                      "max_abs_delta",
                      max_delta);
        }

        const auto source_semantics = entity_semantic_span(source, s);
        const auto candidate_semantics = entity_semantic_span(candidate, c);
        if (source_semantics != candidate_semantics) {
            ++report.semantic_record_mismatches;
            add_issue(report,
                      s.source_id,
                      "semantic.records",
                      "non-geometry group-code/value content changed");
        }

        const auto source_xdata = entity_xdata_span(source, s);
        const auto candidate_xdata = entity_xdata_span(candidate, c);
        if (source_xdata != candidate_xdata) {
            ++report.xdata_mismatches;
            add_issue(report, s.source_id, "semantic.xdata", "XDATA content changed");
        }
    }

    if (source.blocks().size() != candidate.blocks().size()) {
        ++report.block_mismatches;
        ++report.interaction_mismatches;
        add_issue(report,
                  "blocks",
                  "interaction.block_count",
                  "source=" + std::to_string(source.blocks().size())
                      + ", candidate=" + std::to_string(candidate.blocks().size()));
    }

    std::unordered_map<std::string, const DxfBlockDefinition*> candidate_blocks;
    for (const auto& block : candidate.blocks()) candidate_blocks[block.name] = &block;
    for (const auto& block : source.blocks()) {
        const auto it = candidate_blocks.find(block.name);
        if (it == candidate_blocks.end()) {
            ++report.block_mismatches;
            ++report.interaction_mismatches;
            add_issue(report, block.source_id, "interaction.block_missing", "block missing: " + block.name);
            continue;
        }
        const auto source_signature = canonical_span(source,
                                                     block.first_record,
                                                     block.last_record_exclusive,
                                                     {},
                                                     false,
                                                     false,
                                                     false);
        const auto candidate_signature = canonical_span(candidate,
                                                        it->second->first_record,
                                                        it->second->last_record_exclusive,
                                                        {},
                                                        false,
                                                        false,
                                                        false);
        if (source_signature != candidate_signature) {
            ++report.block_mismatches;
            add_issue(report,
                      block.source_id,
                      "semantic.block_definition",
                      "block definition content changed: " + block.name);
        }
    }

    if (source.objects().size() != candidate.objects().size()) {
        ++report.object_mismatches;
        add_issue(report,
                  "objects",
                  "semantic.object_count",
                  "source=" + std::to_string(source.objects().size())
                      + ", candidate=" + std::to_string(candidate.objects().size()));
    }

    std::map<std::string, const DxfObject*> candidate_objects;
    std::map<std::string, std::size_t> candidate_ordinals;
    for (const auto& object : candidate.objects()) {
        const auto ordinal = candidate_ordinals[object.type]++;
        candidate_objects[object_key(object, ordinal)] = &object;
    }

    std::map<std::string, std::size_t> source_ordinals;
    for (const auto& object : source.objects()) {
        const auto ordinal = source_ordinals[object.type]++;
        const auto key = object_key(object, ordinal);
        const auto it = candidate_objects.find(key);
        if (it == candidate_objects.end()) {
            ++report.object_mismatches;
            add_issue(report, object.source_id, "semantic.object_missing", "object missing: " + key);
            continue;
        }
        const auto& candidate_object = *it->second;
        const auto source_signature = canonical_span(source,
                                                     object.first_record,
                                                     object.last_record_exclusive,
                                                     object.type,
                                                     false,
                                                     false,
                                                     object.type == "GROUP");
        const auto candidate_signature = canonical_span(candidate,
                                                        candidate_object.first_record,
                                                        candidate_object.last_record_exclusive,
                                                        candidate_object.type,
                                                        false,
                                                        false,
                                                        candidate_object.type == "GROUP");
        if (source_signature != candidate_signature) {
            ++report.object_mismatches;
            add_issue(report,
                      object.source_id,
                      "semantic.object_records",
                      "object content changed: " + key);
        }

        if (object.type == "GROUP") {
            const auto source_members = resolved_group_members(source, object);
            const auto candidate_members = resolved_group_members(candidate, candidate_object);
            if (source_members != candidate_members) {
                ++report.reference_mismatches;
                ++report.interaction_mismatches;
                add_issue(report,
                          object.source_id,
                          "interaction.group_membership",
                          "GROUP member selection set changed");
            }
        }

        const auto source_xdata = canonical_span(source,
                                                 object.first_record,
                                                 object.last_record_exclusive,
                                                 object.type,
                                                 false,
                                                 true,
                                                 false);
        const auto candidate_xdata = canonical_span(candidate,
                                                    candidate_object.first_record,
                                                    candidate_object.last_record_exclusive,
                                                    candidate_object.type,
                                                    false,
                                                    true,
                                                    false);
        if (source_xdata != candidate_xdata) {
            ++report.xdata_mismatches;
            add_issue(report, object.source_id, "semantic.xdata", "object XDATA content changed");
        }
    }

    return report;
}

void append_verification_issues_to_trace(TraceLedger& trace,
                                         const VerificationReport& report) {
    for (const auto& issue : report.issues) {
        trace.record_loss_for_source(issue.source_id,
                                     issue.category,
                                     issue.severity,
                                     issue.detail,
                                     issue.metric_name,
                                     issue.metric_value);
    }
}

std::string VerificationReport::to_json() const {
    std::ostringstream out;
    out << "{\n"
        << "  \"pass\": " << (pass ? "true" : "false") << ",\n"
        << "  \"source_entities\": " << source_entities << ",\n"
        << "  \"candidate_entities\": " << candidate_entities << ",\n"
        << "  \"source_blocks\": " << source_blocks << ",\n"
        << "  \"candidate_blocks\": " << candidate_blocks << ",\n"
        << "  \"source_objects\": " << source_objects << ",\n"
        << "  \"candidate_objects\": " << candidate_objects << ",\n"
        << "  \"entity_type_mismatches\": " << entity_type_mismatches << ",\n"
        << "  \"layer_mismatches\": " << layer_mismatches << ",\n"
        << "  \"geometry_mismatches\": " << geometry_mismatches << ",\n"
        << "  \"semantic_record_mismatches\": " << semantic_record_mismatches << ",\n"
        << "  \"block_mismatches\": " << block_mismatches << ",\n"
        << "  \"object_mismatches\": " << object_mismatches << ",\n"
        << "  \"xdata_mismatches\": " << xdata_mismatches << ",\n"
        << "  \"interaction_mismatches\": " << interaction_mismatches << ",\n"
        << "  \"reference_mismatches\": " << reference_mismatches << ",\n"
        << "  \"issues\": [";
    for (std::size_t i = 0; i < issues.size(); ++i) {
        if (i) out << ',';
        const auto& issue = issues[i];
        out << "\n    {\"source_id\": \"" << escape_json(issue.source_id)
            << "\", \"severity\": \"" << escape_json(issue.severity)
            << "\", \"category\": \"" << escape_json(issue.category)
            << "\", \"detail\": \"" << escape_json(issue.detail) << "\"";
        if (issue.has_metric) {
            out << ", \"metric_name\": \"" << escape_json(issue.metric_name)
                << "\", \"metric_value\": " << issue.metric_value;
        }
        out << '}';
    }
    if (!issues.empty()) out << '\n' << "  ";
    out << "]\n}";
    return out.str();
}

} // namespace cadopt