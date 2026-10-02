#include <cadopt/dwg_backend.hpp>
#include <cstdlib>
#include <sstream>

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

} // namespace

ExternalCommandDwgBackend::ExternalCommandDwgBackend(std::string dxf_to_dwg_template,
                                                       std::string dwg_to_dxf_template)
    : dxf_to_dwg_template_(std::move(dxf_to_dwg_template)),
      dwg_to_dxf_template_(std::move(dwg_to_dxf_template)) {}

ConversionResult ExternalCommandDwgBackend::run_template(const std::string& command_template,
                                                          const std::filesystem::path& input,
                                                          const std::filesystem::path& output) {
    if (command_template.empty()) return {false, -1, "DWG converter command is not configured"};
    std::string command = command_template;
    replace_all(command, "{input}", shell_quote(input));
    replace_all(command, "{output}", shell_quote(output));
    if (command == command_template && command.find("{input}") == std::string::npos
        && command.find("{output}") == std::string::npos) {
        return {false, -1, "converter command must contain {input} and {output}"};
    }
    const int code = std::system(command.c_str());
    if (code != 0) return {false, code, "converter command failed"};
    if (!std::filesystem::exists(output)) return {false, code, "converter returned success but output file is missing"};
    return {true, code, "ok"};
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
