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
                         bool potentially_lossy) {
    if (entity_index >= entities_.size()) throw std::out_of_range("trace entity index out of range");
    entities_[entity_index].steps.push_back({std::move(operation), std::move(parameters), potentially_lossy});
}

std::string TraceLedger::to_json() const {
    std::ostringstream out;
    out << "{\"entities\":[";
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
                << "\",\"potentially_lossy\":" << (s.potentially_lossy ? "true" : "false") << '}';
        }
        out << "]}";
    }
    out << "]}";
    return out.str();
}

} // namespace cadopt
