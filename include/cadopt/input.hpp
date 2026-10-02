#pragma once

#include <cadopt/dwg_backend.hpp>

#include <filesystem>
#include <string>

namespace cadopt {

enum class CadInputKind {
    Dxf,
    Dwg,
    Unsupported
};

struct InputPreparationResult {
    bool ok{};
    CadInputKind kind{CadInputKind::Unsupported};
    std::filesystem::path original_input;
    std::filesystem::path dxf_path;
    bool converted_from_dwg{};
    ConversionResult conversion;
    std::string message;
};

CadInputKind detect_cad_input_kind(const std::filesystem::path& path);
std::string cad_input_kind_name(CadInputKind kind);

InputPreparationResult prepare_input_as_dxf(const std::filesystem::path& input,
                                            const std::filesystem::path& work_directory,
                                            const DwgBackend* backend);

} // namespace cadopt
