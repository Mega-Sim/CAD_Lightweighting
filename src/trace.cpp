#include <cadopt/trace.hpp>
#include <sstream>
#include <stdexcept>

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

std::size_t TraceLedger::begin_entity(std::string source_id, std::string source_type) {
    entities_.push_back({std::move(source_id), std::move(source_type), {}});
    return entities_.size() - 1;
}

void TraceLedger::record(std::size_t entity_index,
                         std::string operation,
                         std::string parameters,
                         bool potentially_lossy,
                         std::string loss_category,
                         std::string severity,
                         std::string detail,
                         std::string metric_name,
                         double metric_value) {
    if (entity_index >= entities_.size()) throw std::out_of_range("trace entity index out of range");
    TraceStep step;
    step.operation = std::move(operation);
    step.parameters = std::move(parameters);
    step.potentially_lossy = potentially_lossy;
    step.loss_category = std::move(loss_category);
    step.severity = std::move(severity);
    step.detail = std::move(detail);
    step.metric_name = std::move(metric_name);
    step.metric_value = metric_value;
    step.has_metric = !step.metric_name.empty();
    entities_[entity_index].steps.push_back(std::move(step));
}

bool TraceLedger::record_for_source(const std::string& source_id,
                                    std::string operation,
                                    std::string parameters,
                                    bool potentially_lossy,
                                    std::string loss_category,
                                    std::string severity,
                                    std::string detail,
                                    std::string metric_name,
                                    double metric_value) {
    for (std::size_t i = 0; i < entities_.size(); ++i) {
        if (entities_[i].source_id != source_id) continue;
        record(i,
               std::move(operation),
               std::move(parameters),
               potentially_lossy,
               std::move(loss_category),
               std::move(severity),
               std::move(detail),
               std::move(metric_name),
               metric_value);
        return true;
    }
    return false;
}

bool TraceLedger::record_loss_for_source(const std::string& source_id,
                                         std::string category,
                                         std::string severity,
                                         std::string detail,
                                         std::string metric_name,
                                         double metric_value) {
    return record_for_source(source_id,
                             "verification_issue",
                             "source_correlated=true",
                             true,
                             std::move(category),
                             std::move(severity),
                             std::move(detail),
                             std::move(metric_name),
                             metric_value);
}

bool TraceLedger::has_fatal_issue() const noexcept {
    for (const auto& entity : entities_) {
        for (const auto& step : entity.steps) {
            if (step.severity == "fatal") return true;
        }
    }
    return false;
}

std::string TraceLedger::to_json() const {
    std::ostringstream out;
    out << "{\"has_fatal_issue\":" << (has_fatal_issue() ? "true" : "false")
        << ",\"entities\":[";
    for (std::size_t i = 0; i < entities_.size(); ++i) {
        if (i) out << ',';
        const auto& e = entities_[i];
        out << "{\"source_id\":\"" << escape_json(e.source_id)
            << "\",\"source_type\":\"" << escape_json(e.source_type) << "\",\"steps\":[";
        for (std::size_t j = 0; j < e.steps.size(); ++j) {
            if (j) out << ',';
            const auto& s = e.steps[j];
            out << "{\"operation\":\"" << escape_json(s.operation)
                << "\",\"parameters\":\"" << escape_json(s.parameters)
                << "\",\"potentially_lossy\":" << (s.potentially_lossy ? "true" : "false")
                << ",\"loss_category\":\"" << escape_json(s.loss_category)
                << "\",\"severity\":\"" << escape_json(s.severity)
                << "\",\"detail\":\"" << escape_json(s.detail) << "\"";
            if (s.has_metric) {
                out << ",\"metric_name\":\"" << escape_json(s.metric_name)
                    << "\",\"metric_value\":" << s.metric_value;
            }
            out << '}';
        }
        out << "]}";
    }
    out << "]}";
    return out.str();
}

} // namespace cadopt
