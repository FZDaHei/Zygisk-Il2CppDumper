#include "runtime_export.h"
#include "export_model.h"
#include "il2cpp-class.h"
#include "il2cpp-tabledefs.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <dlfcn.h>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <fcntl.h>
#include <link.h>
#include <unistd.h>

#define DO_API(r, n, p) extern r (*n) p
#include "il2cpp-api-functions.h"
#undef DO_API

namespace {
using namespace il2cpp_export;
struct Map { uintptr_t begin, end; bool readable; std::string path; uint64_t offset; };
class Memory {
    int fd = -1;
public:
    std::vector<Map> maps;
    Memory() {
        fd = open("/proc/self/mem", O_RDONLY | O_CLOEXEC);
        std::ifstream f("/proc/self/maps");
        std::string line;
        while (std::getline(f, line)) {
            std::istringstream stream(line);
            std::string range, perms, off, dev, inode, path;
            stream >> range >> perms >> off >> dev >> inode;
            std::getline(stream, path);
            unsigned long long begin = 0, end = 0, offset = 0;
            if (sscanf(range.c_str(), "%llx-%llx", &begin, &end) != 2 ||
                sscanf(off.c_str(), "%llx", &offset) != 1 || perms.empty()) continue;
            maps.push_back({uintptr_t(begin), uintptr_t(end), perms[0] == 'r', path, offset});
        }
    }
    ~Memory() { if (fd >= 0) close(fd); }
    bool available() const { return fd >= 0 && !maps.empty(); }
    bool readable(uintptr_t p, size_t size) const {
        if (size > UINTPTR_MAX - p) return false;
        auto it = std::upper_bound(maps.begin(), maps.end(), p,
            [](uintptr_t v, const Map &m) { return v < m.begin; });
        if (it == maps.begin()) return false;
        --it;
        uintptr_t end = p + size;
        while (it != maps.end() && it->readable && p >= it->begin && p < it->end) {
            if (end <= it->end) return true;
            p = it->end;
            ++it;
        }
        return false;
    }
    bool read(uintptr_t p, void *buffer, size_t size) const {
        if (!size) return true;
        if (fd < 0 || !readable(p, size)) return false;
        size_t done = 0;
        while (done < size) {
            auto count = pread64(fd, static_cast<char*>(buffer) + done, size - done, off64_t(p + done));
            if (count <= 0) return false;
            done += size_t(count);
        }
        return true;
    }
};
std::string safe(const char *s) { return s ? s : ""; }
struct NativeType { std::string ctype; uint64_t size = 0; char code = 'i'; bool byref = false; };
class Collector {
    Snapshot &s;
    Memory &memory;
    std::vector<Il2CppClass*> classes;
    std::unordered_map<Il2CppClass*, std::string> names;
    std::unordered_set<const MethodInfo*> methods;
public:
    std::unordered_map<uintptr_t, Metadata> references;
    std::unordered_map<uintptr_t, MetadataMethod> method_references;
    explicit Collector(Snapshot &snapshot, Memory &mem) : s(snapshot), memory(mem) {}
    std::string full_name(Il2CppClass *k) {
        if (!k) return "unknown";
        std::string name = safe(il2cpp_class_get_name(k));
        // API 返回的类型名称包括闭合泛型信息，并由 il2cpp_free 配对释放。
        if (il2cpp_type_get_name && il2cpp_free) {
            char *p = il2cpp_type_get_name(il2cpp_class_get_type(k));
            if (p) { name = p; il2cpp_free(p); return name; }
        }
        auto ns = safe(il2cpp_class_get_namespace(k));
        return ns.empty() ? name : ns + "." + name;
    }
    std::string add_class(Il2CppClass *k) {
        if (!k) return {};
        auto it = names.find(k);
        if (it != names.end()) return it->second;
        auto n = full_name(k);
        auto c = identifier(n) + "_" + std::to_string(classes.size());
        names[k] = c;
        classes.push_back(k);
        references[reinterpret_cast<uintptr_t>(k)] = {0, n + "_TypeInfo", "Il2CppClass*"};
        references[reinterpret_cast<uintptr_t>(il2cpp_class_get_type(k))] = {0, n + "_var", "Il2CppType*"};
        return c;
    }
    NativeType native_type(const Il2CppType *t) {
        NativeType r;
        if (!t) return r;
        int type = il2cpp_type_get_type ? il2cpp_type_get_type(t) : t->type;
        const bool byref = il2cpp_type_is_byref ? il2cpp_type_is_byref(t) : t->byref;
        switch (type) {
            case IL2CPP_TYPE_VOID: r.ctype = "void"; r.code = 'v'; break;
            case IL2CPP_TYPE_BOOLEAN: case IL2CPP_TYPE_U1: r.ctype = "uint8_t"; r.size = 1; break;
            case IL2CPP_TYPE_I1: r.ctype = "int8_t"; r.size = 1; break;
            case IL2CPP_TYPE_CHAR: case IL2CPP_TYPE_U2: r.ctype = "uint16_t"; r.size = 2; break;
            case IL2CPP_TYPE_I2: r.ctype = "int16_t"; r.size = 2; break;
            case IL2CPP_TYPE_U4: r.ctype = "uint32_t"; r.size = 4; break;
            case IL2CPP_TYPE_I4: r.ctype = "int32_t"; r.size = 4; break;
            case IL2CPP_TYPE_U8: r.ctype = "uint64_t"; r.size = 8; r.code = 'j'; break;
            case IL2CPP_TYPE_I8: r.ctype = "int64_t"; r.size = 8; r.code = 'j'; break;
            case IL2CPP_TYPE_R4: r.ctype = "float"; r.size = 4; r.code = 'f'; break;
            case IL2CPP_TYPE_R8: r.ctype = "double"; r.size = 8; r.code = 'd'; break;
            case IL2CPP_TYPE_I: r.ctype = "intptr_t"; r.size = sizeof(void*); break;
            case IL2CPP_TYPE_U: r.ctype = "uintptr_t"; r.size = sizeof(void*); break;
            case IL2CPP_TYPE_PTR: case IL2CPP_TYPE_FNPTR: r.ctype = "void*"; r.size = sizeof(void*); break;
            case IL2CPP_TYPE_STRING: r.ctype = "Il2CppString*"; r.size = sizeof(void*); break;
            case IL2CPP_TYPE_OBJECT: r.ctype = "Il2CppObject*"; r.size = sizeof(void*); break;
            case IL2CPP_TYPE_ARRAY: case IL2CPP_TYPE_SZARRAY: r.ctype = "Il2CppArray*"; r.size = sizeof(void*); break;
            case IL2CPP_TYPE_CLASS: case IL2CPP_TYPE_GENERICINST: case IL2CPP_TYPE_VALUETYPE: {
                auto k = il2cpp_class_from_type(t);
                if (!k) break;
                auto cname = add_class(k);
                if (il2cpp_class_is_enum(k) && il2cpp_class_enum_basetype) {
                    const auto *base = il2cpp_class_enum_basetype(k);
                    if (base && base != t) r = native_type(base);
                } else if (il2cpp_class_is_valuetype(k)) {
                    uint32_t align = 0;
                    int size = il2cpp_class_value_size ? il2cpp_class_value_size(k, &align) : 0;
                    if (size > 0) r.size = uint64_t(size);
                    // 未重建按值传递 ABI 时保持不透明，不能用 void* 冒充值类型。
                } else { r.ctype = cname + "_o*"; r.size = sizeof(void*); }
                break;
            }
            default: break;
        }
        r.byref = byref;
        if (byref) {
            if (!r.ctype.empty()) r.ctype += '*';
            r.size = sizeof(void*);
            r.code = 'i';
        }
        return r;
    }
    void collect_method(Il2CppClass *k, const MethodInfo *m, int metadata_version) {
        if (!methods.insert(m).second) return;
        uintptr_t address = 0;
        if (!memory.read(reinterpret_cast<uintptr_t>(m), &address, sizeof(address))) return;
        Dl_info owner{};
        if (address && (!dladdr(reinterpret_cast<void*>(address), &owner) ||
            reinterpret_cast<uintptr_t>(owner.dli_fbase) != s.image_base)) return;
        // MethodInfo 的第一个字段在支持的标准 ABI 中为 methodPointer。
        auto name = full_name(k) + "$$" + safe(il2cpp_method_get_name(m));
        uint64_t rva = address >= s.image_base ? address - s.image_base : 0;
        method_references[reinterpret_cast<uintptr_t>(m)] = {0,
            "Method$" + full_name(k) + "." + safe(il2cpp_method_get_name(m)) + "()", rva};
        if (!address || address < s.image_base) return;
        uint32_t impl_flags = 0;
        auto flags = il2cpp_method_get_flags(m, &impl_flags);
        auto ret = native_type(il2cpp_method_get_return_type(m));
        Method out;
        out.address = rva;
        out.name = name;
        out.type_signature += ret.code;
        bool complete = !ret.ctype.empty();
        std::vector<std::string> params;
        if (!(flags & METHOD_ATTRIBUTE_STATIC)) {
            if (il2cpp_class_is_valuetype(k)) complete = false;
            params.push_back(add_class(k) + "_o* __this");
            out.type_signature += 'i';
        } else if (metadata_version > 0 && metadata_version <= 24) {
            params.emplace_back("Il2CppObject* __this");
            out.type_signature += 'i';
        } else if (!metadata_version) {
            complete = false;
        }
        auto count = il2cpp_method_get_param_count(m);
        for (uint32_t i = 0; i < count; ++i) {
            auto param = native_type(il2cpp_method_get_param(m, i));
            complete = complete && !param.ctype.empty();
            params.push_back(param.ctype + " " + identifier(safe(il2cpp_method_get_param_name(m, i))) + "_" + std::to_string(i));
            out.type_signature += param.code;
        }
        params.emplace_back("const MethodInfo* method");
        out.type_signature += 'i';
        if (complete) {
            out.signature = ret.ctype + " " + identifier(name) + "_" + std::to_string(s.methods.size()) + " (";
            for (size_t i = 0; i < params.size(); ++i) { if (i) out.signature += ", "; out.signature += params[i]; }
            out.signature += ");";
        }
        s.methods.push_back(std::move(out));
    }
    void collect(int metadata_version) {
        size_t count = 0;
        auto assemblies = il2cpp_domain_get_assemblies(il2cpp_domain_get(), &count);
        for (size_t i = 0; i < count; ++i) {
            auto image = il2cpp_assembly_get_image(assemblies[i]);
            auto n = il2cpp_image_get_class_count(image);
            for (size_t j = 0; j < n; ++j)
                add_class(const_cast<Il2CppClass*>(il2cpp_image_get_class(image, j)));
        }
        size_t opaque = 0, unknown_size = 0;
        // 字段和方法签名可能引入额外闭合泛型类，沿已发现类型继续处理。
        for (size_t i = 0; i < classes.size() && i < 100000; ++i) {
            auto k = classes[i];
            Type t;
            t.name = full_name(k); t.cname = names.at(k);
            int size = il2cpp_class_instance_size(k);
            if (size <= 0 || size > 64 * 1024 * 1024) { ++unknown_size; continue; }
            t.size = uint64_t(size);
            t.fields.push_back({"klass", "Il2CppClass*", 0, sizeof(void*)});
            t.fields.push_back({"monitor", "void*", sizeof(void*), sizeof(void*)});
            std::unordered_set<Il2CppClass*> parents;
            for (auto owner = k; owner && parents.insert(owner).second; owner = il2cpp_class_get_parent(owner)) {
                void *iter = nullptr;
                while (auto f = il2cpp_class_get_fields(owner, &iter)) {
                    auto fname = safe(il2cpp_field_get_name(f));
                    references[reinterpret_cast<uintptr_t>(f)] = {0, "Field$" + full_name(owner) + "." + fname, ""};
                    auto flags = il2cpp_field_get_flags(f);
                    if (flags & (FIELD_ATTRIBUTE_STATIC | FIELD_ATTRIBUTE_LITERAL)) continue;
                    auto nt = native_type(il2cpp_field_get_type(f));
                    uint64_t offset = il2cpp_field_get_offset(f);
                    if (!nt.size || offset > t.size || nt.size > t.size - offset) { ++unknown_size; continue; }
                    if (nt.ctype.empty()) ++opaque;
                    t.fields.push_back({fname, nt.ctype, offset, nt.size});
                }
            }
            void *iter = nullptr;
            while (auto m = il2cpp_class_get_methods(k, &iter)) collect_method(k, m, metadata_version);
            s.types.push_back(std::move(t));
        }
        if (classes.size() > 100000) s.limitations.emplace_back("类型数量超过 100000 的扫描上限；后续类型未导出。");
        if (opaque) s.limitations.push_back(std::to_string(opaque) + " 个值类型字段仅恢复大小，以字节数组保留准确偏移。");
        if (unknown_size) s.limitations.push_back(std::to_string(unknown_size) + " 个类型/字段大小不可用，未伪造布局。");
        size_t missing = 0;
        for (const auto &m : s.methods) if (m.signature.empty()) ++missing;
        if (missing) s.limitations.push_back(std::to_string(missing) + " 个方法的 ABI/按值类型签名不可确定，Signature=null。");
    }
};
struct MetadataLiterals {
    uintptr_t base = 0;
    uint32_t table_offset = 0, table_size = 0, data_offset = 0, data_size = 0;
    int version = 0;
    void locate(const Memory &mem) {
        for (const auto &m : mem.maps) {
            if (!m.readable || m.offset != 0 || m.path.find("global-metadata.dat") == std::string::npos) continue;
            uint32_t h[6]{};
            if (!mem.read(m.begin, h, sizeof(h)) || h[0] != 0xfab11baf || h[1] < 16 || h[1] > 40) continue;
            if (h[3] % 8 || h[3] > 256 * 1024 * 1024 || h[5] > 512 * 1024 * 1024) continue;
            if (h[2] > UINTPTR_MAX - m.begin || h[4] > UINTPTR_MAX - m.begin ||
                !mem.readable(m.begin + h[2], h[3]) || !mem.readable(m.begin + h[4], h[5])) continue;
            base = m.begin; version = int(h[1]);
            table_offset = h[2]; table_size = h[3]; data_offset = h[4]; data_size = h[5];
            return;
        }
    }
    bool literal(const Memory &mem, uint32_t index, std::string &out) const {
        if (!base || index >= table_size / 8) return false;
        uint32_t record[2]{};
        if (!mem.read(base + table_offset + index * 8, record, sizeof(record)) ||
            record[0] > 1024 * 1024 || record[1] > data_size || record[0] > data_size - record[1]) return false;
        out.resize(record[0]);
        return mem.read(base + data_offset + record[1], out.data(), out.size());
    }
};
struct Segment { uintptr_t begin, end; };
struct Segments { uintptr_t base; std::vector<Segment> values; };
int find_segments(dl_phdr_info *info, size_t, void *data) {
    auto &s = *static_cast<Segments*>(data);
    if (uintptr_t(info->dlpi_addr) != s.base) return 0;
    for (size_t i = 0; i < info->dlpi_phnum; ++i) {
        const auto &p = info->dlpi_phdr[i];
        if (p.p_type == PT_LOAD && (p.p_flags & PF_R) && !(p.p_flags & PF_X))
            s.values.push_back({s.base + p.p_vaddr, s.base + p.p_vaddr + p.p_memsz});
    }
    return 1;
}
void collect_references(Snapshot &s, Memory &mem, Collector &collector, const MetadataLiterals &metadata) {
    Segments segments{uintptr_t(s.image_base), {}};
    dl_iterate_phdr(find_segments, &segments);
    if (segments.values.empty()) { s.limitations.emplace_back("未找到 libil2cpp 的可读非执行 ELF PT_LOAD 段，未扫描引用。"); return; }
    auto corlib = il2cpp_get_corlib ? il2cpp_get_corlib() : nullptr;
    auto string_class = corlib && il2cpp_class_from_name ? il2cpp_class_from_name(corlib, "System", "String") : nullptr;
    uintptr_t string_klass = reinterpret_cast<uintptr_t>(string_class);
    std::unordered_map<uintptr_t, std::string> cache;
    std::unordered_set<uintptr_t> rejected;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    size_t probes = 0, unreadable = 0;
    uint64_t string_bytes = 0;
    constexpr uint64_t scan_limit = 256ULL * 1024 * 1024;
    std::vector<uintptr_t> buffer(8192);
    bool limited = false;
    for (auto segment : segments.values) {
        uintptr_t begin = (segment.begin + sizeof(void*) - 1) & ~(uintptr_t(sizeof(void*) - 1));
        while (begin < segment.end) {
            if (s.scanned_bytes >= scan_limit || probes >= 100000 || string_bytes > 32 * 1024 * 1024 ||
                std::chrono::steady_clock::now() > deadline) { limited = true; break; }
            size_t bytes = std::min<size_t>(buffer.size() * sizeof(uintptr_t), segment.end - begin);
            bytes -= bytes % sizeof(uintptr_t);
            if (!bytes) break;
            if (!mem.read(begin, buffer.data(), bytes)) { ++unreadable; begin += bytes; continue; }
            s.scanned_bytes += bytes;
            for (size_t i = 0; i < bytes / sizeof(uintptr_t); ++i) {
                if (string_bytes >= 32 * 1024 * 1024 ||
                    ((i & 255) == 0 && std::chrono::steady_clock::now() > deadline)) {
                    limited = true;
                    break;
                }
                const auto value = buffer[i];
                if (!value) continue;
                const uint64_t rva = begin + i * sizeof(uintptr_t) - s.image_base;
                const auto ref = collector.references.find(value);
                if (ref != collector.references.end()) { auto v = ref->second; v.address = rva; s.metadata.push_back(v); continue; }
                const auto method = collector.method_references.find(value);
                if (method != collector.method_references.end()) { auto v = method->second; v.address = rva; s.metadata_methods.push_back(v); continue; }
                std::string literal;
                // v27+ 的尚未解析字符串引用编码，不能用于旧版 metadataUsagePairs。
                if (metadata.version >= 27 && value <= UINT32_MAX && (value & 1) && (value >> 29) == 5) {
                    const auto index = uint32_t(value & 0x1fffffff) >> 1;
                    if (metadata.literal(mem, index, literal)) { string_bytes += literal.size(); s.strings.push_back({rva, literal}); }
                    continue;
                }
                if (!string_klass || value % sizeof(void*) || rejected.count(value) || !mem.readable(value, sizeof(void*) * 2 + 4)) continue;
                auto cached = cache.find(value);
                if (cached != cache.end()) { string_bytes += cached->second.size(); s.strings.push_back({rva, cached->second}); continue; }
                if (++probes > 100000) { limited = true; break; }
                uintptr_t klass = 0;
                int32_t length = -1;
                if (!mem.read(value, &klass, sizeof(klass)) || klass != string_klass ||
                    !mem.read(value + sizeof(void*) * 2, &length, sizeof(length)) || length < 0 || length > 524288) {
                    rejected.insert(value); continue;
                }
                std::vector<uint16_t> chars(static_cast<size_t>(length));
                if (!mem.read(value + sizeof(void*) * 2 + 4, chars.data(), chars.size() * 2)) { rejected.insert(value); continue; }
                literal = utf16_to_utf8(chars);
                cache.emplace(value, literal);
                string_bytes += literal.size();
                s.strings.push_back({rva, std::move(literal)});
            }
            if (limited) break;
            begin += bytes;
        }
        if (limited) break;
    }
    if (limited) s.limitations.emplace_back("引用扫描达到时间/字节/候选数上限；只导出已读取部分。");
    if (unreadable) s.limitations.emplace_back("部分 ELF 段在扫描时不可读，已跳过。");
    if (!string_class) s.limitations.emplace_back("未取得 System.String 类，仅能解析可用 metadata 的编码字符串引用。");
}
}
bool export_runtime_files(const std::string &directory, uint64_t image_base, std::string &error) {
    if (!image_base || !il2cpp_image_get_class || !il2cpp_image_get_class_count ||
        !il2cpp_class_instance_size || !il2cpp_class_get_type || !il2cpp_class_get_fields ||
        !il2cpp_class_get_methods || !il2cpp_class_get_parent || !il2cpp_class_from_type ||
        !il2cpp_class_get_name || !il2cpp_class_get_namespace || !il2cpp_class_is_enum ||
        !il2cpp_class_is_valuetype || !il2cpp_domain_get || !il2cpp_domain_get_assemblies ||
        !il2cpp_assembly_get_image || !il2cpp_field_get_name || !il2cpp_field_get_flags ||
        !il2cpp_field_get_type || !il2cpp_field_get_offset || !il2cpp_method_get_name ||
        !il2cpp_method_get_flags || !il2cpp_method_get_return_type || !il2cpp_method_get_param_count ||
        !il2cpp_method_get_param || !il2cpp_method_get_param_name) {
        error = "完整运行时导出所需 API 缺失（需要 il2cpp_image_get_class 等标准接口）";
        return false;
    }
    Memory memory;
    if (!memory.available()) { error = "无法读取 /proc/self/maps 或 /proc/self/mem"; return false; }
    Snapshot snapshot;
    snapshot.image_base = image_base;
    snapshot.limitations.emplace_back("运行时快照不等同静态 EXE 全量结果：未遍历私有注册表、全部泛型实例和未初始化的旧版 metadata usages。");
    snapshot.limitations.emplace_back("il2cpp.h 描述实例字段/继承偏移；未恢复静态字段块、vtable、RGCTXData 和 Il2CppClass/MethodInfo 私有布局。");
    MetadataLiterals metadata;
    metadata.locate(memory);
    if (!metadata.base) snapshot.limitations.emplace_back("未找到标准明文 global-metadata.dat 映射；只导出已初始化字符串引用，静态方法 ABI 未猜测。");
    Collector collector(snapshot, memory);
    collector.collect(metadata.version);
    if (snapshot.types.empty()) { error = "没有取得任何可用运行时类型"; return false; }
    collect_references(snapshot, memory, collector, metadata);
    return write_exports(directory, snapshot, error);
}
