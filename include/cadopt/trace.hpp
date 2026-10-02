#pragma once
#include <cstddef>
#include <string>
#include <vector>

namespace cadopt {

struct TraceStep {
    std::string operation;
    std::string parameters;
    bool potentially_lossy{};
};

struct TraceEntity {
    std::string source_id;
    std::string source_type;
    std::vector<TraceStep> steps;
};

class TraceLedger {
public:
    std::size_t begin_entity(std::string source_id, std::string source_type);
    void record(std::size_t entity_index,
                std::string operation,
                std::string parameters,
                bool potentially_lossy);
    std::string to_json() const;

private:
    std::vector<TraceEntity> entities_;
};

} // namespace cadopt
