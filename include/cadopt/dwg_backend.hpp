#pragma once
#include <filesystem>
#include <string>

namespace cadopt {

struct ConversionResult {
    bool ok{};
    int exit_code{};
    std::string message;
};

class DwgBackend {
public:
    virtual ~DwgBackend() = default;
    virtual ConversionResult dxf_to_dwg(const std::filesystem::path& input,
                                        const std::filesystem::path& output) const = 0;
    virtual ConversionResult dwg_to_dxf(const std::filesystem::path& input,
                                        const std::filesystem::path& output) const = 0;
};

class ExternalCommandDwgBackend final : public DwgBackend {
public:
    ExternalCommandDwgBackend(std::string dxf_to_dwg_template,
                              std::string dwg_to_dxf_template);

    ConversionResult dxf_to_dwg(const std::filesystem::path& input,
                                const std::filesystem::path& output) const override;
    ConversionResult dwg_to_dxf(const std::filesystem::path& input,
                                const std::filesystem::path& output) const override;

private:
    std::string dxf_to_dwg_template_;
    std::string dwg_to_dxf_template_;
    static ConversionResult run_template(const std::string& command_template,
                                         const std::filesystem::path& input,
                                         const std::filesystem::path& output);
};

} // namespace cadopt
