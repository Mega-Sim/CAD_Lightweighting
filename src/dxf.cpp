#include <cadopt/dxf.hpp>
#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iterator>
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
    doc.original_bytes_ = std::string(std::istreambuf_iterator<char>(in),
                                      std::istreambuf_iterator<char>());
    std::istringstream source(doc.original_bytes_);

    std::string code_line;
    std::string value_line;
    std::size_t line_no = 1;
    while (std::getline(source, code_line)) {
        if (!std::getline(source, value_line)) {
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
    doc.rebuild_semantic_indexes();
    return doc;
}

void DxfDocument::write(const std::filesystem::path& path, DxfWriteMode mode) const {
    if (mode != DxfWriteMode::PreserveLexical) {
        throw std::runtime_error("Unsupported DXF write mode");
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("Unable to write DXF: " + path.string());
    out.write(original_bytes_.data(), static_cast<std::streamsize>(original_bytes_.size()));
    if (!out) throw std::runtime_error("Unable to finish writing DXF: " + path.string());
}

void DxfDocument::rebuild_semantic_indexes() {
    entities_.clear();
    blocks_.clear();
    objects_.clear();

    std::string section;
    std::size_t i = 0;
    while (i < records_.size()) {
        const auto& r = records_[i];
        const auto value = trim(r.value);

        if (r.code == 0 && value == "SECTION" && i + 1 < records_.size()
            && records_[i + 1].code == 2) {
            section = trim(records_[i + 1].value);
            i += 2;
            continue;
        }
        if (r.code == 0 && value == "ENDSEC") {
            section.clear();
            ++i;
            continue;
        }

        if (section == "ENTITIES" && r.code == 0) {
            const std::string type = value;
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
                const auto rec_value = trim(records_[j].value);
                if (records_[j].code == 5 && entity.handle.empty()) entity.handle = rec_value;
                if (records_[j].code == 8 && entity.layer.empty()) entity.layer = rec_value;
                if (type == "INSERT" && records_[j].code == 2 && entity.block_name.empty()) {
                    entity.block_name = rec_value;
                }
                if (records_[j].code == 1001) entity.xdata_apps.push_back(rec_value);
            }
            entity.source_id = "E:" + (entity.handle.empty() ? std::to_string(first) : entity.handle)
                             + ":" + std::to_string(first);
            entities_.push_back(std::move(entity));
            i = last;
            continue;
        }

        if (section == "BLOCKS" && r.code == 0 && value == "BLOCK") {
            const std::size_t first = i;
            std::size_t header_end = i + 1;
            while (header_end < records_.size() && records_[header_end].code != 0) ++header_end;

            std::size_t endblk = header_end;
            while (endblk < records_.size()) {
                if (records_[endblk].code == 0 && trim(records_[endblk].value) == "ENDBLK") break;
                ++endblk;
            }
            std::size_t last = endblk < records_.size() ? endblk + 1 : records_.size();
            while (last < records_.size() && records_[last].code != 0) ++last;

            DxfBlockDefinition block;
            block.first_record = first;
            block.last_record_exclusive = last;
            for (std::size_t j = first + 1; j < header_end; ++j) {
                const auto rec_value = trim(records_[j].value);
                if (records_[j].code == 2 && block.name.empty()) block.name = rec_value;
                if (records_[j].code == 5 && block.handle.empty()) block.handle = rec_value;
            }
            const auto identity = !block.handle.empty() ? block.handle
                                : (!block.name.empty() ? block.name : std::to_string(first));
            block.source_id = "B:" + identity + ":" + std::to_string(first);
            blocks_.push_back(std::move(block));
            i = last;
            continue;
        }

        if (section == "OBJECTS" && r.code == 0) {
            const std::string type = value;
            if (type == "EOF" || type == "ENDSEC") {
                ++i;
                continue;
            }
            const std::size_t first = i;
            std::size_t last = i + 1;
            while (last < records_.size() && records_[last].code != 0) ++last;

            DxfObject object;
            object.type = type;
            object.first_record = first;
            object.last_record_exclusive = last;
            for (std::size_t j = first + 1; j < last; ++j) {
                const auto rec_value = trim(records_[j].value);
                if (records_[j].code == 5 && object.handle.empty()) object.handle = rec_value;
                if (records_[j].code == 330 && object.owner_handle.empty()) object.owner_handle = rec_value;
                if (type == "GROUP" && records_[j].code == 300 && object.description.empty()) {
                    object.description = rec_value;
                }
                if (type == "GROUP" && records_[j].code == 340) object.referenced_handles.push_back(rec_value);
                if (records_[j].code == 1001) object.xdata_apps.push_back(rec_value);
            }
            const auto identity = !object.handle.empty() ? object.handle
                                : type + ":" + std::to_string(first);
            object.source_id = "O:" + identity + ":" + std::to_string(first);
            objects_.push_back(std::move(object));
            i = last;
            continue;
        }

        ++i;
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
