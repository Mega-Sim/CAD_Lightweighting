#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace cadopt {

enum class DwgBackendProfile {
    ExternalCommand,
    OdaFileConverter,
    OdaSdk,
    RealDwgHost
};

struct DwgBackendCapabilities {
    std::string name;
    DwgBackendProfile profile{DwgBackendProfile::ExternalCommand};
    bool can_write_dwg{};
    bool can_read_dwg{};
    bool supports_independent_roundtrip{};
    bool requires_external_license{};
    std::string notes;
};

struct ConversionResult {
    bool ok{};
    int exit_code{};
    std::string message;
    std::uintmax_t output_bytes{};
};

std::string dwg_backend_profile_name(DwgBackendProfile profile);
DwgBackendProfile parse_dwg_backend_profile(const std::string& text);
DwgBackendCapabilities capabilities_for_profile(DwgBackendProfile profile);

class DwgBackend {
public:
    virtual ~DwgBackend() = default;
    virtual DwgBackendCapabilities capabilities() const = 0;
    virtual ConversionResult dxf_to_dwg(const std::filesystem::path& input,
                                        const std::filesystem::path& output) const = 0;
    virtual ConversionResult dwg_to_dxf(const std::filesystem::path& input,
                                        const std::filesystem::path& output) const = 0;
};

class ExternalCommandDwgBackend final : public DwgBackend {
public:
    ExternalCommandDwgBackend(std::string dxf_to_dwg_template,
                              std::string dwg_to_dxf_template,
                              DwgBackendProfile profile = DwgBackendProfile::ExternalCommand);

    DwgBackendCapabilities capabilities() const override;
    ConversionResult dxf_to_dwg(const std::filesystem::path& input,
                                const std::filesystem::path& output) const override;
    ConversionResult dwg_to_dxf(const std::filesystem::path& input,
                                const std::filesystem::path& output) const override;

private:
    std::string dxf_to_dwg_template_;
    std::string dwg_to_dxf_template_;
    DwgBackendProfile profile_{DwgBackendProfile::ExternalCommand};

    static ConversionResult run_template(const std::string& command_template,
                                         const std::filesystem::path& input,
                                         const std::filesystem::path& output);
};

} // namespace cadopt
