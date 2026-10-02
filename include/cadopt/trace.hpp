#pragma once
#include <cstddef>
#include <string>
#include <vector>

namespace cadopt {

struct TraceStep {
    std::string operation;
    std::string parameters;
    bool potentially_lossy{};
    std::string loss_category{"none"};
    std::string severity{"info"};
    std::string detail;
    std::string metric_name;
    double metric_value{};
    bool has_metric{};
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
                bool potentially_lossy,
                std::string loss_category = "none",
                std::string severity = "info",
                std::string detail = {},
                std::string metric_name = {},
                double metric_value = 0.0);
    bool record_for_source(const std::string& source_id,
                           std::string operation,
                           std::string parameters,
                           bool potentially_lossy = false,
                           std::string loss_category = "none",
                           std::string severity = "info",
                           std::string detail = {},
                           std::string metric_name = {},
                           double metric_value = 0.0);
    bool record_loss_for_source(const std::string& source_id,
                                std::string category,
                                std::string severity,
                                std::string detail,
                                std::string metric_name = {},
                                double metric_value = 0.0);
    std::string to_json() const;

private:
    std::vector<TraceEntity> entities_;
};

} // namespace cadopt
