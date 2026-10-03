#ifndef IL2CPP_EXPORT_MODEL_H
#define IL2CPP_EXPORT_MODEL_H

#include <cstdint>
#include <string>
#include <vector>

namespace il2cpp_export {
struct Method {
    uint64_t address = 0;
    std::string name, signature, type_signature;
};
struct StringLiteral { uint64_t address; std::string value; };
struct Metadata { uint64_t address; std::string name, signature; };
struct MetadataMethod { uint64_t address; std::string name; uint64_t method_address; };
struct Field {
    std::string name, ctype;
    uint64_t offset = 0, size = 0;
    // 空类型表示只知道大小，不编造 C 类型。
};
struct Type {
    std::string name, cname;
    uint64_t size = 0;
    std::vector<Field> fields;
};
struct Snapshot {
    unsigned pointer_size = sizeof(void*);
    uint64_t image_base = 0;
    std::vector<Method> methods;
    std::vector<StringLiteral> strings;
    std::vector<Metadata> metadata;
    std::vector<MetadataMethod> metadata_methods;
    std::vector<Type> types;
    std::vector<std::string> limitations;
    uint64_t scanned_bytes = 0;
};
std::string json_string(const std::string &value);
std::string identifier(const std::string &value);
std::string utf16_to_utf8(const std::vector<uint16_t> &value);
std::string script_json(const Snapshot &snapshot);
std::string stringliteral_json(const Snapshot &snapshot);
std::string header(const Snapshot &snapshot);
std::string report_json(const Snapshot &snapshot);
bool write_exports(const std::string &directory, const Snapshot &snapshot, std::string &error);
}
#endif
