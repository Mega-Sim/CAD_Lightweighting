#include <cadopt/dwg_backend.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <system_error>

namespace cadopt {
namespace {

std::string shell_quote(const std::filesystem::path& p) {
#ifdef _WIN32
    std::string s = p.string();
    std::string out = "\"";
    for (char c : s) {
        if (c == '"') out += "\\\"";
        else out += c;
    }
    out += "\"";
    return out;
#else
    std::string s = p.string();
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    out += "'";
    return out;
#endif
}

void replace_all(std::string& text, const std::string& from, const std::string& to) {
    std::size_t pos = 0;
    while ((pos = text.find(from, pos)) != std::string::npos) {
        text.replace(pos, from.size(), to);
        pos += to.size();
    }
}

std::string lowercase(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return text;
}

} // namespace

std::string dwg_backend_profile_name(DwgBackendProfile profile) {
    switch (profile) {
    case DwgBackendProfile::ExternalCommand: return "external";
    case DwgBackendProfile::OdaFileConverter: return "oda-file-converter";
    case DwgBackendProfile::OdaSdk: return "oda-sdk";
    case DwgBackendProfile::RealDwgHost: return "realdwg-host";
    }
    return "unknown";
}

DwgBackendProfile parse_dwg_backend_profile(const std::string& text) {
    const auto value = lowercase(text);
    if (value.empty() || value == "external" || value == "command") {
        return DwgBackendProfile::ExternalCommand;
    }
    if (value == "oda" || value == "oda-file-converter" || value == "odafileconverter") {
        return DwgBackendProfile::OdaFileConverter;
    }
    if (value == "oda-sdk" || value == "odacopyex") return DwgBackendProfile::OdaSdk;
    if (value == "realdwg" || value == "realdwg-host") return DwgBackendProfile::RealDwgHost;
    throw std::invalid_argument("unknown DWG backend profile: " + text);
}

DwgBackendCapabilities capabilities_for_profile(DwgBackendProfile profile) {
    DwgBackendCapabilities caps;
    caps.profile = profile;
    caps.can_write_dwg = true;
    caps.can_read_dwg = true;
    caps.supports_independent_roundtrip = true;
    switch (profile) {
    case DwgBackendProfile::ExternalCommand:
        caps.name = "external-command";
        caps.requires_external_license = false;
        caps.notes = "Caller supplies explicit DXF->DWG and DWG->DXF command templates.";
        break;
    case DwgBackendProfile::OdaFileConverter:
        caps.name = "ODA File Converter";
        caps.requires_external_license = true;
        caps.notes = "Research adapter only; commercial use must comply with current ODA licensing.";
        break;
    case DwgBackendProfile::OdaSdk:
        caps.name = "ODA Drawings SDK host";
        caps.requires_external_license = true;
        caps.notes = "Command template should invoke a properly licensed ODA SDK host such as an OdCopyEx-based wrapper.";
        break;
    case DwgBackendProfile::RealDwgHost:
        caps.name = "Autodesk RealDWG host";
        caps.requires_external_license = true;
        caps.notes = "Command template should invoke a separately built/licensed RealDWG host.";
        break;
    }
    return caps;
}

ExternalCommandDwgBackend::ExternalCommandDwgBackend(std::string dxf_to_dwg_template,
                                                       std::string dwg_to_dxf_template,
                                                       DwgBackendProfile profile)
    : dxf_to_dwg_template_(std::move(dxf_to_dwg_template)),
      dwg_to_dxf_template_(std::move(dwg_to_dxf_template)),
      profile_(profile) {}

DwgBackendCapabilities ExternalCommandDwgBackend::capabilities() const {
    return capabilities_for_profile(profile_);
}

ConversionResult ExternalCommandDwgBackend::run_template(const std::string& command_template,
                                                          const std::filesystem::path& input,
                                                          const std::filesystem::path& output) {
    if (command_template.empty()) {
        return {false, -1, "DWG converter command is not configured", 0};
    }
    if (command_template.find("{input}") == std::string::npos
        || command_template.find("{output}") == std::string::npos) {
        return {false, -1, "converter command must contain both {input} and {output}", 0};
    }

    std::error_code error;
    if (!output.parent_path().empty()) {
        std::filesystem::create_directories(output.parent_path(), error);
        if (error) return {false, -1, "cannot create converter output directory: " + error.message(), 0};
    }
    error.clear();
    if (std::filesystem::exists(output, error) && !error) {
        std::filesystem::remove(output, error);
        if (error) return {false, -1, "cannot remove stale converter output: " + error.message(), 0};
    }

    std::string command = command_template;
    replace_all(command, "{input}", shell_quote(input));
    replace_all(command, "{output}", shell_quote(output));
    const int code = std::system(command.c_str());
    if (code != 0) return {false, code, "converter command failed", 0};

    error.clear();
    if (!std::filesystem::exists(output, error) || error) {
        return {false, code, "converter returned success but output file is missing", 0};
    }
    error.clear();
    const auto bytes = std::filesystem::file_size(output, error);
    if (error) return {false, code, "unable to measure converter output: " + error.message(), 0};
    if (bytes == 0) return {false, code, "converter produced an empty output file", 0};
    return {true, code, "ok", bytes};
}

ConversionResult ExternalCommandDwgBackend::dxf_to_dwg(const std::filesystem::path& input,
                                                         const std::filesystem::path& output) const {
    return run_template(dxf_to_dwg_template_, input, output);
}

ConversionResult ExternalCommandDwgBackend::dwg_to_dxf(const std::filesystem::path& input,
                                                         const std::filesystem::path& output) const {
    return run_template(dwg_to_dxf_template_, input, output);
}

} // namespace cadopt
