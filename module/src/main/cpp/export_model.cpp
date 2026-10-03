#include "export_model.h"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

namespace il2cpp_export {
std::string json_string(const std::string &s) {
    std::ostringstream o;
    o << '"';
    for (unsigned char c : s) {
        switch (c) {
            case '"': o << "\\\""; break;
            case '\\': o << "\\\\"; break;
            case '\b': o << "\\b"; break;
            case '\f': o << "\\f"; break;
            case '\n': o << "\\n"; break;
            case '\r': o << "\\r"; break;
            case '\t': o << "\\t"; break;
            default:
                if (c < 32) o << "\\u00" << std::hex << std::setw(2) << std::setfill('0') << unsigned(c);
                else o << char(c);
        }
    }
    o << '"';
    return o.str();
}
std::string identifier(const std::string &s) {
    // 固定 ASCII 规则避免 locale 和 C/C++ 关键字影响生成头文件。
    std::string out = "t_";
    for (unsigned char c : s)
        out += ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9')) ? char(c) : '_';
    return out;
}
static void utf8_append(std::string &s, uint32_t c) {
    if (c < 0x80) s += char(c);
    else if (c < 0x800) { s += char(0xc0 | (c >> 6)); s += char(0x80 | (c & 63)); }
    else if (c < 0x10000) {
        s += char(0xe0 | (c >> 12)); s += char(0x80 | ((c >> 6) & 63)); s += char(0x80 | (c & 63));
    } else {
        s += char(0xf0 | (c >> 18)); s += char(0x80 | ((c >> 12) & 63));
        s += char(0x80 | ((c >> 6) & 63)); s += char(0x80 | (c & 63));
    }
}
std::string utf16_to_utf8(const std::vector<uint16_t> &value) {
    std::string s;
    for (size_t i = 0; i < value.size(); ++i) {
        uint32_t c = value[i];
        if (c >= 0xd800 && c <= 0xdbff && i + 1 < value.size() &&
            value[i + 1] >= 0xdc00 && value[i + 1] <= 0xdfff)
            c = 0x10000 + ((c - 0xd800) << 10) + (value[++i] - 0xdc00);
        else if (c >= 0xd800 && c <= 0xdfff) c = 0xfffd;
        utf8_append(s, c);
    }
    return s;
}
std::string script_json(const Snapshot &s) {
    std::ostringstream o;
    o << "{\n  \"ScriptMethod\": [";
    bool first = true;
    for (const auto &m : s.methods) {
        if (!first) o << ',';
        first = false;
        o << "\n    {\"Address\":" << m.address << ",\"Name\":" << json_string(m.name)
          << ",\"Signature\":" << (m.signature.empty() ? "null" : json_string(m.signature))
          << ",\"TypeSignature\":" << json_string(m.type_signature) << '}';
    }
    o << "\n  ],\n  \"ScriptString\": [";
    first = true;
    for (const auto &v : s.strings) {
        if (!first) o << ',';
        first = false;
        o << "\n    {\"Address\":" << v.address << ",\"Value\":" << json_string(v.value) << '}';
    }
    o << "\n  ],\n  \"ScriptMetadata\": [";
    first = true;
    for (const auto &v : s.metadata) {
        if (!first) o << ',';
        first = false;
        o << "\n    {\"Address\":" << v.address << ",\"Name\":" << json_string(v.name)
          << ",\"Signature\":" << (v.signature.empty() ? "null" : json_string(v.signature)) << '}';
    }
    o << "\n  ],\n  \"ScriptMetadataMethod\": [";
    first = true;
    for (const auto &v : s.metadata_methods) {
        if (!first) o << ',';
        first = false;
        o << "\n    {\"Address\":" << v.address << ",\"Name\":" << json_string(v.name)
          << ",\"MethodAddress\":" << v.method_address << '}';
    }
    o << "\n  ],\n  \"Addresses\": [";
    std::set<uint64_t> addresses;
    for (const auto &m : s.methods) addresses.insert(m.address);
    first = true;
    for (auto a : addresses) { if (!first) o << ','; first = false; o << a; }
    o << "]\n}\n";
    return o.str();
}
std::string stringliteral_json(const Snapshot &s) {
    std::ostringstream o;
    o << '[';
    bool first = true;
    for (const auto &v : s.strings) {
        if (!first) o << ',';
        first = false;
        o << "\n  {\"value\":" << json_string(v.value) << ",\"address\":\"0x"
          << std::hex << std::uppercase << v.address << "\"}";
    }
    o << "\n]\n";
    return o.str();
}
std::string header(const Snapshot &s) {
    std::ostringstream o;
    o << "/* 运行时布局；仅适用于本次导出的 ABI。未恢复的内部类型保持不透明。 */\n"
         "#ifndef IL2CPP_RUNTIME_EXPORTED_H\n#define IL2CPP_RUNTIME_EXPORTED_H\n"
         "#include <stdint.h>\n#include <stddef.h>\n"
         "typedef struct Il2CppClass Il2CppClass;\n"
         "typedef struct Il2CppType Il2CppType;\n"
         "typedef struct MethodInfo MethodInfo;\n"
         "typedef struct Il2CppObject { Il2CppClass *klass; void *monitor; } Il2CppObject;\n"
         "typedef struct Il2CppString { Il2CppObject object; int32_t length; uint16_t chars[1]; } Il2CppString;\n"
         "typedef struct Il2CppArray { Il2CppObject object; void *bounds; uintptr_t max_length; } Il2CppArray;\n"
      << "#if defined(__cplusplus)\nstatic_assert(sizeof(void*) == " << s.pointer_size
      << ", \"导出目标与当前编译器指针宽度不同\");\n#endif\n";
    for (const auto &t : s.types) o << "typedef struct " << t.cname << "_o " << t.cname << "_o;\n";
    o << "#pragma pack(push, 1)\n";
    for (const auto &t : s.types) {
        o << "/* " << identifier(t.name) << " */\nstruct " << t.cname << "_o {\n";
        std::vector<Field> fields = t.fields;
        std::stable_sort(fields.begin(), fields.end(), [](const Field &a, const Field &b) { return a.offset < b.offset; });
        uint64_t offset = 0;
        size_t index = 0;
        for (size_t i = 0; i < fields.size(); ++i) {
            const auto &f = fields[i];
            if (!f.size || f.offset < offset || f.offset > t.size || f.size > t.size - f.offset) continue;
            if (f.offset > offset) o << "  uint8_t pad_" << index++ << '[' << f.offset - offset << "];\n";
            // 显式布局中有重叠字段时使用不透明字节，不输出错误的顺序结构。
            size_t end = i + 1;
            uint64_t group_end = f.offset + f.size;
            while (end < fields.size() && fields[end].offset < group_end) {
                if (fields[end].offset <= t.size && fields[end].size <= t.size - fields[end].offset)
                    group_end = std::max(group_end, fields[end].offset + fields[end].size);
                ++end;
            }
            const std::string name = identifier(f.name) + "_" + std::to_string(index++);
            if (end > i + 1 || f.ctype.empty()) o << "  uint8_t " << name << '[' << group_end - f.offset << "];";
            else o << "  " << f.ctype << ' ' << name << ';';
            o << " /* +0x" << std::hex << f.offset << std::dec << " */\n";
            offset = group_end;
            i = end - 1;
        }
        if (offset < t.size) o << "  uint8_t trailing[" << t.size - offset << "];\n";
        if (!t.size) o << "  uint8_t unavailable;\n";
        o << "};\n";
    }
    o << "#pragma pack(pop)\n#endif\n";
    return o.str();
}
std::string report_json(const Snapshot &s) {
    std::ostringstream o;
    o << "{\n\"SchemaVersion\":1,\n\"Source\":\"runtime-api-and-data-references\",\n"
         "\"EquivalentToStaticExe\":false,\n\"DummyDllRequested\":false,\n"
      << "\"PointerSize\":" << s.pointer_size << ",\n\"ImageBase\":" << s.image_base
      << ",\n\"Methods\":" << s.methods.size() << ",\n\"Types\":" << s.types.size()
      << ",\n\"StringReferences\":" << s.strings.size() << ",\n\"MetadataReferences\":" << s.metadata.size()
      << ",\n\"MethodReferences\":" << s.metadata_methods.size() << ",\n\"ScannedBytes\":" << s.scanned_bytes
      << ",\n\"Limitations\":[";
    bool first = true;
    for (const auto &v : s.limitations) { if (!first) o << ','; first = false; o << json_string(v); }
    o << "]\n}\n";
    return o.str();
}
bool write_exports(const std::string &dir, const Snapshot &s, std::string &error) {
    // 新目录由调用方以原子 rename 发布，避免混合两次导出的文件。
    const std::vector<std::pair<std::string, std::string>> files = {
        {"script.json", script_json(s)}, {"stringliteral.json", stringliteral_json(s)},
        {"il2cpp.h", header(s)}, {"export-report.json", report_json(s)}
    };
    for (const auto &f : files) {
        std::ofstream stream(dir + "/" + f.first, std::ios::binary | std::ios::trunc);
        stream.write(f.second.data(), static_cast<std::streamsize>(f.second.size()));
        stream.flush();
        if (!stream) { error = "write failed: " + f.first; return false; }
        stream.close();
        if (!stream) { error = "close failed: " + f.first; return false; }
    }
    return true;
}
}
