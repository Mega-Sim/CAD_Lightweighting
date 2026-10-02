#include <cadopt/dwg_backend.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

static void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

static std::string read_all(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

int main() {
    try {
        const fs::path root = fs::temp_directory_path() / "cadopt_native_backend_test";
        fs::create_directories(root);
        const fs::path source = root / "input.dxf";
        const fs::path dwg = root / "output.dwg";
        const fs::path roundtrip = root / "roundtrip.dxf";
        {
            std::ofstream out(source, std::ios::binary | std::ios::trunc);
            out << "native-backend-roundtrip";
        }

        cadopt::NativeLibraryDwgBackend backend(
            fs::path(CADOPT_TEST_NATIVE_BACKEND_PATH),
            cadopt::DwgBackendProfile::OdaSdk);
        const auto caps = backend.capabilities();
        require(caps.can_write_dwg && caps.can_read_dwg && caps.supports_independent_roundtrip,
                "native backend capabilities must enable strict round trip");
        require(caps.name == "cadopt-fake-native-backend",
                "native backend name export was not loaded");

        const auto encode = backend.dxf_to_dwg(source, dwg);
        require(encode.ok && encode.output_bytes > 0,
                "native backend DXF->DWG conversion failed: " + encode.message);
        const auto decode = backend.dwg_to_dxf(dwg, roundtrip);
        require(decode.ok && decode.output_bytes > 0,
                "native backend DWG->DXF conversion failed: " + decode.message);
        require(read_all(source) == read_all(roundtrip),
                "fake native backend round trip changed payload");

        fs::remove_all(root);
        std::cout << "cadopt_native_backend_tests: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "cadopt_native_backend_tests: FAIL: " << error.what() << '\n';
        return 1;
    }
}
