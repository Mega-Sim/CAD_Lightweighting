#include <cadopt/input.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <system_error>

namespace cadopt {
namespace {

std::string lowercase(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return text;
}

} // namespace

CadInputKind detect_cad_input_kind(const std::filesystem::path& path) {
    const auto extension = lowercase(path.extension().string());
    if (extension == ".dxf") return CadInputKind::Dxf;
    if (extension == ".dwg") return CadInputKind::Dwg;
    return CadInputKind::Unsupported;
}

std::string cad_input_kind_name(CadInputKind kind) {
    switch (kind) {
    case CadInputKind::Dxf: return "dxf";
    case CadInputKind::Dwg: return "dwg";
    case CadInputKind::Unsupported: return "unsupported";
    }
    return "unsupported";
}

InputPreparationResult prepare_input_as_dxf(const std::filesystem::path& input,
                                            const std::filesystem::path& work_directory,
                                            const DwgBackend* backend) {
    InputPreparationResult result;
    result.original_input = input;
    result.kind = detect_cad_input_kind(input);

    std::error_code error;
    if (!std::filesystem::exists(input, error) || error) {
        result.message = "input file does not exist: " + input.string();
        return result;
    }
    error.clear();
    if (!std::filesystem::is_regular_file(input, error) || error) {
        result.message = "input path is not a regular file: " + input.string();
        return result;
    }

    if (result.kind == CadInputKind::Unsupported) {
        result.message = "unsupported CAD input extension: " + input.extension().string()
                       + " (expected .dxf or .dwg)";
        return result;
    }

    if (result.kind == CadInputKind::Dxf) {
        result.ok = true;
        result.dxf_path = input;
        result.message = "DXF input accepted without normalization";
        return result;
    }

    if (!backend) {
        result.message = "DWG input requires a configured DWG-to-DXF backend";
        return result;
    }

    const auto caps = backend->capabilities();
    if (!caps.can_read_dwg) {
        result.message = "configured backend cannot read DWG input: " + caps.name;
        return result;
    }

    const auto root = work_directory.empty()
        ? std::filesystem::temp_directory_path() / "cadopt_input"
        : work_directory;
    const auto input_work_directory = root / "input";
    error.clear();
    std::filesystem::create_directories(input_work_directory, error);
    if (error) {
        result.message = "cannot create input normalization work directory: " + error.message();
        return result;
    }

    result.dxf_path = input_work_directory / "normalized_source.dxf";
    error.clear();
    if (std::filesystem::equivalent(input, result.dxf_path, error) && !error) {
        result.message = "refusing to overwrite source input during DWG normalization";
        result.dxf_path.clear();
        return result;
    }

    result.conversion = backend->dwg_to_dxf(input, result.dxf_path);
    if (!result.conversion.ok) {
        result.message = "DWG input normalization failed: " + result.conversion.message;
        result.dxf_path.clear();
        return result;
    }

    error.clear();
    if (!std::filesystem::exists(result.dxf_path, error) || error) {
        result.message = "DWG backend reported success but normalized DXF is missing";
        result.dxf_path.clear();
        return result;
    }
    error.clear();
    const auto bytes = std::filesystem::file_size(result.dxf_path, error);
    if (error || bytes == 0) {
        result.message = error
            ? "cannot measure normalized DXF: " + error.message()
            : "DWG backend produced an empty normalized DXF";
        result.dxf_path.clear();
        return result;
    }

    result.ok = true;
    result.converted_from_dwg = true;
    result.message = "DWG input normalized to DXF through backend";
    return result;
}

} // namespace cadopt
