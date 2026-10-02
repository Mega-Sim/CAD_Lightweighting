#include <cadopt/dwg_backend.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <system_error>
#include <utility>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#endif

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

std::string path_to_utf8(const std::filesystem::path& path) {
#ifdef _WIN32
    const auto wide = path.wstring();
    if (wide.empty()) return {};
    const int needed = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                                           wide.data(), static_cast<int>(wide.size()),
                                           nullptr, 0, nullptr, nullptr);
    if (needed <= 0) throw std::runtime_error("unable to convert path to UTF-8");
    std::string result(static_cast<std::size_t>(needed), '\0');
    const int written = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                                            wide.data(), static_cast<int>(wide.size()),
                                            result.data(), needed, nullptr, nullptr);
    if (written != needed) throw std::runtime_error("unable to convert path to UTF-8");
    return result;
#else
    return path.string();
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

bool prepare_output_path(const std::filesystem::path& output, std::string& message) {
    std::error_code error;
    if (!output.parent_path().empty()) {
        std::filesystem::create_directories(output.parent_path(), error);
        if (error) {
            message = "cannot create backend output directory: " + error.message();
            return false;
        }
    }
    error.clear();
    if (std::filesystem::exists(output, error) && !error) {
        std::filesystem::remove(output, error);
        if (error) {
            message = "cannot remove stale backend output: " + error.message();
            return false;
        }
    }
    return true;
}

ConversionResult finish_conversion_result(const std::filesystem::path& output,
                                          int code,
                                          std::string message) {
    if (code != 0) {
        if (message.empty()) message = "native backend conversion failed";
        return {false, code, std::move(message), 0};
    }
    std::error_code error;
    if (!std::filesystem::exists(output, error) || error) {
        return {false, code, "backend returned success but output file is missing", 0};
    }
    error.clear();
    const auto bytes = std::filesystem::file_size(output, error);
    if (error) return {false, code, "unable to measure backend output: " + error.message(), 0};
    if (bytes == 0) return {false, code, "backend produced an empty output file", 0};
    if (message.empty()) message = "ok";
    return {true, code, std::move(message), bytes};
}

#ifdef _WIN32
using NativeHandle = HMODULE;

template <typename Function>
Function load_native_symbol(NativeHandle handle, const char* name) {
    return handle ? reinterpret_cast<Function>(GetProcAddress(handle, name)) : nullptr;
}
#else
using NativeHandle = void*;

template <typename Function>
Function load_native_symbol(NativeHandle handle, const char* name) {
    return handle ? reinterpret_cast<Function>(dlsym(handle, name)) : nullptr;
}
#endif

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
        caps.notes = "Uses either an explicit command host or the cadopt native plugin ABI backed by a separately licensed ODA SDK.";
        break;
    case DwgBackendProfile::RealDwgHost:
        caps.name = "Autodesk RealDWG host";
        caps.requires_external_license = true;
        caps.notes = "Uses either an explicit command host or the cadopt native plugin ABI backed by a separately licensed RealDWG host.";
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

    std::string prepare_message;
    if (!prepare_output_path(output, prepare_message)) {
        return {false, -1, std::move(prepare_message), 0};
    }

    std::string command = command_template;
    replace_all(command, "{input}", shell_quote(input));
    replace_all(command, "{output}", shell_quote(output));
    const int code = std::system(command.c_str());
    if (code != 0) return {false, code, "converter command failed", 0};
    return finish_conversion_result(output, code, "ok");
}

ConversionResult ExternalCommandDwgBackend::dxf_to_dwg(const std::filesystem::path& input,
                                                         const std::filesystem::path& output) const {
    return run_template(dxf_to_dwg_template_, input, output);
}

ConversionResult ExternalCommandDwgBackend::dwg_to_dxf(const std::filesystem::path& input,
                                                         const std::filesystem::path& output) const {
    return run_template(dwg_to_dxf_template_, input, output);
}

struct NativeLibraryDwgBackend::Impl {
    using ApiVersionFn = int (*)();
    using NameFn = const char* (*)();
    using ConvertFn = int (*)(const char*, const char*, char*, std::size_t);

    std::filesystem::path library_path;
    DwgBackendProfile profile{DwgBackendProfile::ExternalCommand};
    std::string backend_name;
    NativeHandle handle{};
    ConvertFn dxf_to_dwg{};
    ConvertFn dwg_to_dxf{};

    ~Impl() {
#ifdef _WIN32
        if (handle) FreeLibrary(handle);
#else
        if (handle) dlclose(handle);
#endif
    }

    ConversionResult convert(ConvertFn fn,
                             const std::filesystem::path& input,
                             const std::filesystem::path& output) const {
        if (!fn) return {false, -1, "native backend conversion symbol is unavailable", 0};
        std::string prepare_message;
        if (!prepare_output_path(output, prepare_message)) {
            return {false, -1, std::move(prepare_message), 0};
        }
        const auto input_utf8 = path_to_utf8(input);
        const auto output_utf8 = path_to_utf8(output);
        std::array<char, 4096> diagnostic{};
        const int code = fn(input_utf8.c_str(), output_utf8.c_str(),
                            diagnostic.data(), diagnostic.size());
        std::string message(diagnostic.data());
        return finish_conversion_result(output, code, std::move(message));
    }
};

NativeLibraryDwgBackend::NativeLibraryDwgBackend(std::filesystem::path library_path,
                                                   DwgBackendProfile profile)
    : impl_(std::make_unique<Impl>()) {
    if (profile != DwgBackendProfile::OdaSdk && profile != DwgBackendProfile::RealDwgHost) {
        throw std::invalid_argument("native DWG plugin profile must be oda-sdk or realdwg-host");
    }
    if (library_path.empty()) throw std::invalid_argument("native DWG plugin library path is empty");

    impl_->library_path = std::move(library_path);
    impl_->profile = profile;
#ifdef _WIN32
    impl_->handle = LoadLibraryW(impl_->library_path.wstring().c_str());
    if (!impl_->handle) throw std::runtime_error("unable to load native DWG backend library");
#else
    impl_->handle = dlopen(impl_->library_path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!impl_->handle) {
        const char* error = dlerror();
        throw std::runtime_error(std::string("unable to load native DWG backend library: ")
                                 + (error ? error : "unknown dlopen error"));
    }
#endif

    const auto api_version = load_native_symbol<Impl::ApiVersionFn>(
        impl_->handle, "cadopt_backend_api_version");
    impl_->dxf_to_dwg = load_native_symbol<Impl::ConvertFn>(
        impl_->handle, "cadopt_backend_dxf_to_dwg");
    impl_->dwg_to_dxf = load_native_symbol<Impl::ConvertFn>(
        impl_->handle, "cadopt_backend_dwg_to_dxf");
    const auto name_fn = load_native_symbol<Impl::NameFn>(impl_->handle, "cadopt_backend_name");

    if (!api_version || api_version() != 1) {
        throw std::runtime_error("native DWG backend ABI version mismatch; expected version 1");
    }
    if (!impl_->dxf_to_dwg || !impl_->dwg_to_dxf) {
        throw std::runtime_error("native DWG backend is missing required conversion symbols");
    }
    impl_->backend_name = name_fn && name_fn() ? name_fn() : capabilities_for_profile(profile).name;
}

NativeLibraryDwgBackend::~NativeLibraryDwgBackend() = default;
NativeLibraryDwgBackend::NativeLibraryDwgBackend(NativeLibraryDwgBackend&&) noexcept = default;
NativeLibraryDwgBackend& NativeLibraryDwgBackend::operator=(NativeLibraryDwgBackend&&) noexcept = default;

DwgBackendCapabilities NativeLibraryDwgBackend::capabilities() const {
    auto caps = capabilities_for_profile(impl_->profile);
    caps.name = impl_->backend_name;
    caps.notes += " Loaded through cadopt native plugin ABI v1: " + impl_->library_path.string();
    return caps;
}

ConversionResult NativeLibraryDwgBackend::dxf_to_dwg(const std::filesystem::path& input,
                                                       const std::filesystem::path& output) const {
    return impl_->convert(impl_->dxf_to_dwg, input, output);
}

ConversionResult NativeLibraryDwgBackend::dwg_to_dxf(const std::filesystem::path& input,
                                                       const std::filesystem::path& output) const {
    return impl_->convert(impl_->dwg_to_dxf, input, output);
}

const std::filesystem::path& NativeLibraryDwgBackend::library_path() const noexcept {
    return impl_->library_path;
}

} // namespace cadopt
