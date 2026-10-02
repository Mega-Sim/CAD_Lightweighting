#pragma once
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace cadopt {

enum class DxfWriteMode { PreserveLexical };

struct DxfRecord {
    int code{};
    std::string value;
    std::string raw_code_line;
    std::string raw_value_line;
    std::size_t code_line_number{};
};

struct DxfEntity {
    std::string source_id;
    std::string type;
    std::string handle;
    std::string layer;
    std::string block_name;
    std::vector<std::string> xdata_apps;
    std::size_t first_record{};
    std::size_t last_record_exclusive{};
};

struct DxfBlockDefinition {
    std::string source_id;
    std::string name;
    std::string handle;
    std::size_t first_record{};
    std::size_t last_record_exclusive{};
};

struct DxfObject {
    std::string source_id;
    std::string type;
    std::string description;
    std::string handle;
    std::string owner_handle;
    std::vector<std::string> referenced_handles;
    std::vector<std::string> xdata_apps;
    std::size_t first_record{};
    std::size_t last_record_exclusive{};
};

class DxfDocument {
public:
    static DxfDocument read(const std::filesystem::path& path);
    void write(const std::filesystem::path& path, DxfWriteMode mode) const;

    const std::vector<DxfRecord>& records() const noexcept { return records_; }
    const std::vector<DxfEntity>& entities() const noexcept { return entities_; }
    const std::vector<DxfBlockDefinition>& blocks() const noexcept { return blocks_; }
    const std::vector<DxfObject>& objects() const noexcept { return objects_; }
    const std::string& original_bytes() const noexcept { return original_bytes_; }
    std::vector<DxfEntity>& mutable_entities_for_test() noexcept { return entities_; }

    std::string semantic_fingerprint(const DxfEntity& entity) const;

private:
    std::string original_bytes_;
    std::vector<DxfRecord> records_;
    std::vector<DxfEntity> entities_;
    std::vector<DxfBlockDefinition> blocks_;
    std::vector<DxfObject> objects_;
    void rebuild_semantic_indexes();
};

} // namespace cadopt
