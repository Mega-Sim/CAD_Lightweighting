#include <cadopt/dwg_backend.hpp>
#include <cadopt/input.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>

namespace fs = std::filesystem;

static void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

static void write_all(const fs::path& path, const std::string& bytes) {
    if (!path.parent_path().empty()) fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

static std::string read_all(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

class FakeDecodeBackend final : public cadopt::DwgBackend {
public:
    explicit FakeDecodeBackend(fs::path decoded_fixture)
        : decoded_fixture_(std::move(decoded_fixture)) {}

    cadopt::DwgBackendCapabilities capabilities() const override {
        cadopt::DwgBackendCapabilities caps;
        caps.name = "fake-decode";
        caps.profile = cadopt::DwgBackendProfile::ExternalCommand;
        caps.can_read_dwg = true;
        caps.can_write_dwg = true;
        caps.supports_independent_roundtrip = true;
        return caps;
    }

    cadopt::ConversionResult dxf_to_dwg(const fs::path&, const fs::path&) const override {
        ++encode_calls;
        return {false, -1, "not used", 0};
    }

    cadopt::ConversionResult dwg_to_dxf(const fs::path& input,
                                        const fs::path& output) const override {
        ++decode_calls;
        last_decode_input = input;
        last_decode_output = output;
        fs::create_directories(output.parent_path());
        fs::copy_file(decoded_fixture_, output, fs::copy_options::overwrite_existing);
        return {true, 0, "ok", fs::file_size(output)};
    }

    mutable int encode_calls{};
    mutable int decode_calls{};
    mutable fs::path last_decode_input;
    mutable fs::path last_decode_output;

private:
    fs::path decoded_fixture_;
};

static fs::path make_temp_root(const char* name) {
    const auto root = fs::temp_directory_path() / name;
    std::error_code error;
    fs::remove_all(root, error);
    fs::create_directories(root);
    return root;
}

static void test_input_kind_detection_is_case_insensitive() {
    require(cadopt::detect_cad_input_kind("drawing.dxf") == cadopt::CadInputKind::Dxf,
            "lower-case DXF extension should be accepted");
    require(cadopt::detect_cad_input_kind("drawing.DXF") == cadopt::CadInputKind::Dxf,
            "upper-case DXF extension should be accepted");
    require(cadopt::detect_cad_input_kind("drawing.dwg") == cadopt::CadInputKind::Dwg,
            "lower-case DWG extension should be accepted");
    require(cadopt::detect_cad_input_kind("drawing.DWG") == cadopt::CadInputKind::Dwg,
            "upper-case DWG extension should be accepted");
}

static void test_dxf_input_bypasses_backend() {
    const auto root = make_temp_root("cadopt_input_dxf_test");
    const auto dxf = root / "source.dxf";
    write_all(dxf, "0\nSECTION\n2\nENTITIES\n0\nENDSEC\n0\nEOF\n");
    FakeDecodeBackend backend(dxf);

    const auto prepared = cadopt::prepare_input_as_dxf(dxf, root / "work", &backend);
    require(prepared.ok, "DXF input should prepare successfully without conversion");
    require(prepared.kind == cadopt::CadInputKind::Dxf, "DXF input kind should be retained");
    require(!prepared.converted_from_dwg, "DXF input must not be marked as converted");
    require(prepared.dxf_path == dxf, "DXF input should remain the source path");
    require(backend.decode_calls == 0, "DXF input must not call DWG decoder");

    fs::remove_all(root);
}

static void test_dwg_input_requires_backend() {
    const auto root = make_temp_root("cadopt_input_dwg_missing_backend_test");
    const auto dwg = root / "source.dwg";
    write_all(dwg, "DWG_PLACEHOLDER");

    const auto prepared = cadopt::prepare_input_as_dxf(dwg, root / "work", nullptr);
    require(!prepared.ok, "DWG input without backend must fail closed");
    require(prepared.message.find("DWG") != std::string::npos,
            "missing backend diagnostic should mention DWG input");

    fs::remove_all(root);
}

static void test_dwg_input_decodes_inside_work_directory_without_touching_source() {
    const auto root = make_temp_root("cadopt_input_dwg_decode_test");
    const auto dwg = root / "customer_source.dwg";
    const auto fixture = root / "decoded_fixture.dxf";
    const std::string original_bytes = "ORIGINAL_DWG_BYTES";
    write_all(dwg, original_bytes);
    write_all(fixture, "0\nSECTION\n2\nENTITIES\n0\nENDSEC\n0\nEOF\n");
    FakeDecodeBackend backend(fixture);

    const auto work = root / "optimizer_work";
    const auto prepared = cadopt::prepare_input_as_dxf(dwg, work, &backend);
    require(prepared.ok, "DWG input should decode successfully through backend");
    require(prepared.kind == cadopt::CadInputKind::Dwg, "DWG input kind should be retained");
    require(prepared.converted_from_dwg, "DWG input should be marked as converted");
    require(backend.decode_calls == 1, "DWG decoder should run exactly once");
    require(backend.encode_calls == 0, "input normalization must not invoke DXF encoder");
    require(backend.last_decode_input == dwg, "decoder should receive original DWG path");
    require(prepared.dxf_path == work / "input" / "normalized_source.dxf",
            "normalized DXF should live under the optimizer work directory");
    require(fs::exists(prepared.dxf_path), "normalized DXF output should exist");
    require(read_all(dwg) == original_bytes, "input normalization must not modify source DWG");

    fs::remove_all(root);
}

static void test_unsupported_extension_fails_closed() {
    const auto root = make_temp_root("cadopt_input_extension_test");
    const auto file = root / "drawing.txt";
    write_all(file, "not cad");

    const auto prepared = cadopt::prepare_input_as_dxf(file, root / "work", nullptr);
    require(!prepared.ok, "unsupported input extension must fail closed");
    require(prepared.message.find("unsupported") != std::string::npos,
            "unsupported extension diagnostic should be explicit");

    fs::remove_all(root);
}

int main() {
    try {
        test_input_kind_detection_is_case_insensitive();
        test_dxf_input_bypasses_backend();
        test_dwg_input_requires_backend();
        test_dwg_input_decodes_inside_work_directory_without_touching_source();
        test_unsupported_extension_fails_closed();
        std::cout << "cadopt_input_tests: PASS\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "cadopt_input_tests: FAIL: " << e.what() << '\n';
        return 1;
    }
}
