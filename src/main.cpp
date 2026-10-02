#include <cadopt/dwg_backend.hpp>
#include <cadopt/dxf.hpp>
#include <cadopt/trace.hpp>
#include <cadopt/verifier.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

struct Options {
    fs::path input;
    fs::path output;
    fs::path report{"cadopt_report.json"};
    std::string dxf_to_dwg;
    std::string dwg_to_dxf;
    bool dry_run{};
};

static Options parse_args(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto value = [&](const char* name) -> std::string {
            if (++i >= argc) throw std::runtime_error(std::string("missing value for ") + name);
            return argv[i];
        };
        if (a == "--input" || a == "-i") o.input = value(a.c_str());
        else if (a == "--output" || a == "-o") o.output = value(a.c_str());
        else if (a == "--report") o.report = value(a.c_str());
        else if (a == "--dxf-to-dwg") o.dxf_to_dwg = value(a.c_str());
        else if (a == "--dwg-to-dxf") o.dwg_to_dxf = value(a.c_str());
        else if (a == "--dry-run") o.dry_run = true;
        else if (a == "--help" || a == "-h") {
            std::cout << "cadopt --input input.dxf --output output.dwg [options]\n"
                      << "  --dry-run                 Parse -> IR -> DXF round-trip only\n"
                      << "  --dxf-to-dwg CMD          External converter command with {input} {output}\n"
                      << "  --dwg-to-dxf CMD          Reverse converter command for independent verification\n"
                      << "  --report FILE             JSON verification report\n";
            std::exit(0);
        } else throw std::runtime_error("unknown argument: " + a);
    }
    if (o.input.empty()) throw std::runtime_error("--input is required");
    if (!o.dry_run && o.output.empty()) throw std::runtime_error("--output is required unless --dry-run");
    return o;
}

static void write_report(const fs::path& path,
                         const cadopt::VerificationReport& report,
                         const cadopt::TraceLedger& trace,
                         const std::string& phase,
                         const std::string& backend_message) {
    std::ofstream out(path, std::ios::trunc);
    if (!out) throw std::runtime_error("cannot write report: " + path.string());
    out << "{\n  \"phase\": \"" << phase << "\",\n"
        << "  \"backend\": \"" << backend_message << "\",\n"
        << "  \"verification\": " << report.to_json() << ",\n"
        << "  \"trace\": " << trace.to_json() << "\n}\n";
}

int main(int argc, char** argv) {
    try {
        const auto opt = parse_args(argc, argv);
        const auto source = cadopt::DxfDocument::read(opt.input);

        cadopt::TraceLedger trace;
        for (const auto& e : source.entities()) {
            const auto id = trace.begin_entity(e.source_id, e.type);
            trace.record(id, "source_ingest", "zero_optimization=true", false);
        }

        const fs::path staged = fs::temp_directory_path() / "cadopt_m1_staged.dxf";
        source.write(staged, cadopt::DxfWriteMode::PreserveLexical);
        const auto staged_doc = cadopt::DxfDocument::read(staged);
        auto report = cadopt::verify_semantic_equivalence(source, staged_doc);
        if (!report.pass) {
            write_report(opt.report, report, trace, "dxf_roundtrip", "not_started");
            std::cerr << "DXF round-trip verification FAILED. See " << opt.report << "\n";
            return 3;
        }

        if (opt.dry_run) {
            write_report(opt.report, report, trace, "dxf_roundtrip", "dry_run");
            std::cout << "Milestone 1 dry-run PASS: " << source.entities().size()
                      << " entities preserved. Report: " << opt.report << "\n";
            return 0;
        }

        if (opt.dxf_to_dwg.empty() || opt.dwg_to_dxf.empty()) {
            write_report(opt.report, report, trace, "dwg_roundtrip", "converter_not_configured");
            std::cerr << "DWG backend is not configured. Both --dxf-to-dwg and --dwg-to-dxf are required\n";
            return 4;
        }

        cadopt::ExternalCommandDwgBackend backend(opt.dxf_to_dwg, opt.dwg_to_dxf);
        const auto encode = backend.dxf_to_dwg(staged, opt.output);
        if (!encode.ok) {
            write_report(opt.report, report, trace, "dwg_roundtrip", encode.message);
            std::cerr << "DXF->DWG failed: " << encode.message << "\n";
            return 5;
        }

        const fs::path verify_dxf = fs::temp_directory_path() / "cadopt_m1_verify.dxf";
        const auto decode = backend.dwg_to_dxf(opt.output, verify_dxf);
        if (!decode.ok) {
            write_report(opt.report, report, trace, "dwg_roundtrip", decode.message);
            std::cerr << "DWG->DXF verification conversion failed: " << decode.message << "\n";
            return 6;
        }

        const auto roundtrip = cadopt::DxfDocument::read(verify_dxf);
        report = cadopt::verify_semantic_equivalence(source, roundtrip);
        write_report(opt.report, report, trace, "dwg_roundtrip", "ok");
        if (!report.pass) {
            std::cerr << "DWG semantic round-trip FAILED. See " << opt.report << "\n";
            return 7;
        }
        std::cout << "Milestone 1 DWG round-trip PASS. Output: " << opt.output << "\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "cadopt: " << e.what() << "\n";
        return 2;
    }
}
