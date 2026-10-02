#include <cadopt/dxf.hpp>
#include <algorithm>
#include <charconv>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace cadopt {
namespace {

std::string trim(std::string s) {
    auto not_space = [](unsigned char c) { return !std::isspace(c); };
    auto first = std::find_if(s.begin(), s.end(), not_space);
    auto last = std::find_if(s.rbegin(), s.rend(), not_space).base();
    if (first >= last) return {};
    return std::string(first, last);
}

std::uint64_t fnv1a(std::string_view text, std::uint64_t seed = 1469598103934665603ULL) {
    std::uint64_t hash = seed;
    for (unsigned char c : text) {
        hash ^= c;
        hash *= 1099511628211ULL;
    }
    return hash;
}

std::string hex64(std::uint64_t value) {
    std::ostringstream out;
    out << std::hex << std::setw(16) << std::setfill('0') << value;
    return out.str();
}

} // namespace

DxfDocument DxfDocument::read(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("Unable to open DXF: " + path.string());

    DxfDocument doc;
    std::string code_line;
    std::string value_line;
    std::size_t line_no = 1;
    while (std::getline(in, code_line)) {
        if (!std::getline(in, value_line)) {
            throw std::runtime_error("Malformed DXF: dangling group-code line at " + std::to_string(line_no));
        }
        if (!code_line.empty() && code_line.back() == '\r') code_line.pop_back();
        if (!value_line.empty() && value_line.back() == '\r') value_line.pop_back();

        const auto code_text = trim(code_line);
        int code = 0;
        const auto* begin = code_text.data();
        const auto* end = code_text.data() + code_text.size();
        const auto [ptr, ec] = std::from_chars(begin, end, code);
        if (ec != std::errc{} || ptr != end) {
            throw std::runtime_error("Malformed DXF group code at line " + std::to_string(line_no));
        }
        doc.records_.push_back(DxfRecord{code, value_line, code_line, value_line, line_no});
        line_no += 2;
    }
    doc.rebuild_entity_index();
    return doc;
}

void DxfDocument::write(const std::filesystem::path& path, DxfWriteMode mode) const {
    if (mode != DxfWriteMode::PreserveLexical) {
        throw std::runtime_error("Unsupported DXF write mode");
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("Unable to write DXF: " + path.string());
    for (const auto& record : records_) {
        out << record.raw_code_line << '\n' << record.raw_value_line << '\n';
    }
}

void DxfDocument::rebuild_entity_index() {
    entities_.clear();
    bool in_entities = false;
    std::size_t i = 0;
    while (i < records_.size()) {
        const auto& r = records_[i];
        if (r.code == 0 && trim(r.value) == "SECTION" && i + 1 < records_.size()
            && records_[i + 1].code == 2 && trim(records_[i + 1].value) == "ENTITIES") {
            in_entities = true;
            i += 2;
            continue;
        }
        if (in_entities && r.code == 0 && trim(r.value) == "ENDSEC") {
            in_entities = false;
            ++i;
            continue;
        }
        if (!in_entities || r.code != 0) {
            ++i;
            continue;
        }

        const std::string type = trim(r.value);
        if (type == "EOF" || type == "ENDSEC") {
            ++i;
            continue;
        }
        const std::size_t first = i;
        std::size_t last = i + 1;
        while (last < records_.size() && records_[last].code != 0) ++last;

        DxfEntity entity;
        entity.type = type;
        entity.first_record = first;
        entity.last_record_exclusive = last;
        for (std::size_t j = first + 1; j < last; ++j) {
            if (records_[j].code == 5 && entity.handle.empty()) entity.handle = trim(records_[j].value);
            if (records_[j].code == 8 && entity.layer.empty()) entity.layer = trim(records_[j].value);
        }
        entity.source_id = "E:" + (entity.handle.empty() ? std::to_string(first) : entity.handle) + ":" + std::to_string(first);
        entities_.push_back(std::move(entity));
        i = last;
    }
}

std::string DxfDocument::semantic_fingerprint(const DxfEntity& entity) const {
    std::uint64_t hash = 1469598103934665603ULL;
    for (std::size_t i = entity.first_record; i < entity.last_record_exclusive && i < records_.size(); ++i) {
        const auto& r = records_[i];
        hash = fnv1a(std::to_string(r.code), hash);
        hash = fnv1a("=", hash);
        hash = fnv1a(trim(r.value), hash);
        hash = fnv1a("\n", hash);
    }
    return hex64(hash);
}

} // namespace cadopt
