#include <cadopt/dxf.hpp>
#include <cadopt/verifier.hpp>
#include <cadopt/trace.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

static void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

static std::string read_all(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

static void test_lexical_roundtrip_is_byte_exact() {
    const fs::path input = fs::path(CADOPT_TEST_FIXTURE_DIR) / "minimal.dxf";
    const fs::path out = fs::temp_directory_path() / "cadopt_roundtrip.dxf";
    auto doc = cadopt::DxfDocument::read(input);
    doc.write(out, cadopt::DxfWriteMode::PreserveLexical);
    require(read_all(input) == read_all(out), "DXF lexical round-trip changed bytes");
    fs::remove(out);
}

static void test_crlf_source_truth_roundtrip_is_byte_exact() {
    const fs::path input = fs::temp_directory_path() / "cadopt_roundtrip_crlf_input.dxf";
    const fs::path out = fs::temp_directory_path() / "cadopt_roundtrip_crlf_output.dxf";
    const std::string bytes =
        "0\r\nSECTION\r\n"
        "2\r\nENTITIES\r\n"
        "0\r\nLINE\r\n"
        "5\r\nA\r\n"
        "8\r\n0\r\n"
        "10\r\n0.125000\r\n"
        "20\r\n0.0\r\n"
        "11\r\n1.125000\r\n"
        "21\r\n1.0\r\n"
        "0\r\nENDSEC\r\n"
        "0\r\nEOF";
    {
        std::ofstream file(input, std::ios::binary | std::ios::trunc);
        file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }
    auto doc = cadopt::DxfDocument::read(input);
    doc.write(out, cadopt::DxfWriteMode::PreserveLexical);
    require(read_all(input) == read_all(out),
            "PreserveLexical must retain CRLF and the original final-newline state byte-for-byte");
    fs::remove(input);
    fs::remove(out);
}

static void test_entity_index_preserves_selection_units() {
    const fs::path input = fs::path(CADOPT_TEST_FIXTURE_DIR) / "minimal.dxf";
    auto doc = cadopt::DxfDocument::read(input);
    require(doc.entities().size() == 3, "expected three ENTITIES records");
    require(doc.entities()[0].type == "LINE", "entity 0 must be LINE");
    require(doc.entities()[1].type == "ARC", "entity 1 must be ARC");
    require(doc.entities()[2].type == "TEXT", "entity 2 must be TEXT");
    require(doc.entities()[0].source_id != doc.entities()[1].source_id, "source IDs must be stable and unique");
}

static void test_verifier_detects_semantic_loss() {
    const fs::path input = fs::path(CADOPT_TEST_FIXTURE_DIR) / "minimal.dxf";
    auto source = cadopt::DxfDocument::read(input);
    auto candidate = source;
    candidate.mutable_entities_for_test()[1].type = "LINE";
    auto report = cadopt::verify_semantic_equivalence(source, candidate);
    require(!report.pass, "semantic verifier should reject changed entity type");
    require(report.entity_type_mismatches == 1, "expected one entity type mismatch");
}

static void test_trace_reports_transform_chain() {
    cadopt::TraceLedger ledger;
    auto id = ledger.begin_entity("source-42", "LINE");
    ledger.record(id, "global_rotate", "angle_deg=27.4", false);
    ledger.record(id, "inverse_rotate", "angle_deg=-27.4", false);
    const auto text = ledger.to_json();
    require(text.find("global_rotate") != std::string::npos, "trace missing transform");
    require(text.find("source-42") != std::string::npos, "trace missing source id");
}

int main() {
    try {
        test_lexical_roundtrip_is_byte_exact();
        test_crlf_source_truth_roundtrip_is_byte_exact();
        test_entity_index_preserves_selection_units();
        test_verifier_detects_semantic_loss();
        test_trace_reports_transform_chain();
        std::cout << "cadopt_tests: PASS\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "cadopt_tests: FAIL: " << e.what() << "\n";
        return 1;
    }
}
