#define CADOPT_BACKEND_BUILD
#include <cadopt/native_backend_api.h>

#include <algorithm>
#include <cstring>
#include <exception>
#include <filesystem>
#include <string>

namespace {

void write_diag(char* buffer, size_t capacity, const std::string& message) {
    if (!buffer || capacity == 0) return;
    const auto count = std::min(capacity - 1, message.size());
    std::memcpy(buffer, message.data(), count);
    buffer[count] = '\0';
}

int copy_file_utf8(const char* input_utf8,
                   const char* output_utf8,
                   char* diagnostic_utf8,
                   size_t diagnostic_capacity) {
    if (!input_utf8 || !output_utf8) {
        write_diag(diagnostic_utf8, diagnostic_capacity, "null input/output path");
        return 2;
    }
    try {
        std::error_code error;
        const std::filesystem::path input(input_utf8);
        const std::filesystem::path output(output_utf8);
        if (!output.parent_path().empty()) {
            std::filesystem::create_directories(output.parent_path(), error);
            if (error) {
                write_diag(diagnostic_utf8, diagnostic_capacity, error.message());
                return 3;
            }
        }
        error.clear();
        std::filesystem::copy_file(input, output,
                                   std::filesystem::copy_options::overwrite_existing,
                                   error);
        if (error) {
            write_diag(diagnostic_utf8, diagnostic_capacity, error.message());
            return 4;
        }
        write_diag(diagnostic_utf8, diagnostic_capacity, "fake native backend ok");
        return 0;
    } catch (const std::exception& error) {
        write_diag(diagnostic_utf8, diagnostic_capacity, error.what());
        return 5;
    }
}

} // namespace

extern "C" CADOPT_BACKEND_EXPORT int cadopt_backend_api_version(void) {
    return CADOPT_BACKEND_API_VERSION;
}

extern "C" CADOPT_BACKEND_EXPORT const char* cadopt_backend_name(void) {
    return "cadopt-fake-native-backend";
}

extern "C" CADOPT_BACKEND_EXPORT int cadopt_backend_dxf_to_dwg(
    const char* input_utf8,
    const char* output_utf8,
    char* diagnostic_utf8,
    size_t diagnostic_capacity) {
    return copy_file_utf8(input_utf8, output_utf8, diagnostic_utf8, diagnostic_capacity);
}

extern "C" CADOPT_BACKEND_EXPORT int cadopt_backend_dwg_to_dxf(
    const char* input_utf8,
    const char* output_utf8,
    char* diagnostic_utf8,
    size_t diagnostic_capacity) {
    return copy_file_utf8(input_utf8, output_utf8, diagnostic_utf8, diagnostic_capacity);
}
