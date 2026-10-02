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

static std::string escape_json(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else out += c;
    }
    return out;
}

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
                      << "  --report FILE             JSON verification/loss trace report\n";
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
    out << "{\n  \"milestone\": 2,\n"
        << "  \"phase\": \"" << escape_json(phase) << "\",\n"
        << "  \"backend\": \"" << escape_json(backend_message) << "\",\n"
        << "  \"verification\": " << report.to_json() << ",\n"
        << "  \"trace\": " << trace.to_json() << "\n}\n";
}

static std::size_t count_xdata_owners(const cadopt::DxfDocument& doc) {
    std::size_t count = 0;
    for (const auto& entity : doc.entities()) if (!entity.xdata_apps.empty()) ++count;
    for (const auto& object : doc.objects()) if (!object.xdata_apps.empty()) ++count;
    return count;
}

static void seed_trace(const cadopt::DxfDocument& source, cadopt::TraceLedger& trace) {
    const auto document = trace.begin_entity("document", "DOCUMENT");
    trace.record(document, "source_ingest", "immutable_source_truth=true", false);

    const auto blocks = trace.begin_entity("blocks", "BLOCK_COLLECTION");
    trace.record(blocks, "source_ingest", "collection=true", false);
    const auto objects = trace.begin_entity("objects", "OBJECT_COLLECTION");
    trace.record(objects, "source_ingest", "collection=true", false);

    for (const auto& e : source.entities()) {
        const auto id = trace.begin_entity(e.source_id, e.type);
        trace.record(id, "source_ingest", "selection_unit=true;zero_optimization=true", false);
    }
    for (const auto& b : source.blocks()) {
        const auto id = trace.begin_entity(b.source_id, "BLOCK:" + b.name);
        trace.record(id, "source_ingest", "block_definition=true", false);
    }
    for (const auto& o : source.objects()) {
        const auto id = trace.begin_entity(o.source_id, "OBJECT:" + o.type);
        trace.record(id, "source_ingest", "object_semantics=true", false);
    }
}

static void trace_verification_failures(cadopt::TraceLedger& trace,
                                        const cadopt::VerificationReport& report) {
    cadopt::append_verification_issues_to_trace(trace, report);
    if (!report.issues.empty()) {
        std::cerr << "[CADOPT][M2] verification issues=" << report.issues.size()
                  << " geometry=" << report.geometry_mismatches
                  << " interaction=" << report.interaction_mismatches
                  << " references=" << report.reference_mismatches
                  << " xdata=" << report.xdata_mismatches << "\n";
    }
}

int main(int argc, char** argv) {
    try {
        const auto opt = parse_args(argc, argv);
        const auto source = cadopt::DxfDocument::read(opt.input);
        std::cout << "[CADOPT][M2] source indexed: entities=" << source.entities().size()
                  << " blocks=" << source.blocks().size()
                  << " objects=" << source.objects().size()
                  << " xdata_owners=" << count_xdata_owners(source) << "\n";

        cadopt::TraceLedger trace;
        seed_trace(source, trace);

        const fs::path staged = fs::temp_directory_path() / "cadopt_m2_staged.dxf";
        source.write(staged, cadopt::DxfWriteMode::PreserveLexical);
        const auto staged_doc = cadopt::DxfDocument::read(staged);
        auto report = cadopt::verify_semantic_equivalence(source, staged_doc);
        if (!report.pass) {
            trace_verification_failures(trace, report);
            write_report(opt.report, report, trace, "dxf_roundtrip", "not_started");
            std::cerr << "[CADOPT][M2] DXF round-trip verification FAILED. See " << opt.report << "\n";
            return 3;
        }
        std::cout << "[CADOPT][M2] zero-optimization DXF round-trip verified\n";

        if (opt.dry_run) {
            write_report(opt.report, report, trace, "dxf_roundtrip", "dry_run");
            std::cout << "Milestone 2 dry-run PASS: " << source.entities().size()
                      << " selection units preserved. Report: " << opt.report << "\n";
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
        std::cout << "[CADOPT][M2] DWG emitted; starting independent reverse verification\n";

        const fs::path verify_dxf = fs::temp_directory_path() / "cadopt_m2_verify.dxf";
        const auto decode = backend.dwg_to_dxf(opt.output, verify_dxf);
        if (!decode.ok) {
            write_report(opt.report, report, trace, "dwg_roundtrip", decode.message);
            std::cerr << "DWG->DXF verification conversion failed: " << decode.message << "\n";
            return 6;
        }

        const auto roundtrip = cadopt::DxfDocument::read(verify_dxf);
        report = cadopt::verify_semantic_equivalence(source, roundtrip);
        trace_verification_failures(trace, report);
        write_report(opt.report, report, trace, "dwg_roundtrip", "ok");
        if (!report.pass) {
            std::cerr << "[CADOPT][M2] DWG semantic/interaction round-trip FAILED. See "
                      << opt.report << "\n";
            return 7;
        }
        std::cout << "Milestone 2 DWG round-trip PASS. Output: " << opt.output << "\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "cadopt: " << e.what() << "\n";
        return 2;
    }
}
