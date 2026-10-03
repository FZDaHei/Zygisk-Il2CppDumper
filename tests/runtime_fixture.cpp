#include "runtime_export.h"
#include "export_model.h"
#include "il2cpp-class.h"
#include "il2cpp-tabledefs.h"
#include <cassert>
#include <cstring>
#include <dlfcn.h>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <sys/mman.h>
#include <unistd.h>

#define DO_API(r,n,p) r (*n) p = nullptr
#include "il2cpp-api-functions.h"
#undef DO_API

struct Il2CppClass { uintptr_t marker = 0; };
struct Il2CppImage { uintptr_t marker = 0; };
struct Il2CppAssembly { uintptr_t marker = 0; };
struct FieldInfo { uintptr_t marker = 0; };

namespace {
Il2CppClass object_class{}, string_class{}, demo_class{};
Il2CppType object_type{}, string_type{}, demo_type{}, int_type{}, void_type{};
Il2CppImage image{};
Il2CppAssembly assembly{};
MethodInfo method{};
FieldInfo field{};
void demo_function() {}
struct StringObject { uintptr_t klass, monitor; int32_t length; uint16_t chars[8]; } literal;
uintptr_t references[5];
void configure() {
    object_type.type = IL2CPP_TYPE_OBJECT;
    string_type.type = IL2CPP_TYPE_STRING;
    demo_type.type = IL2CPP_TYPE_CLASS;
    int_type.type = IL2CPP_TYPE_I4;
    void_type.type = IL2CPP_TYPE_VOID;
    method.methodPointer = demo_function;
    il2cpp_domain_get = []() -> Il2CppDomain* { return reinterpret_cast<Il2CppDomain*>(1); };
    il2cpp_domain_get_assemblies = [](const Il2CppDomain*, size_t *n) -> const Il2CppAssembly** {
        static const Il2CppAssembly *a[] = {&assembly}; *n = 1; return a;
    };
    il2cpp_assembly_get_image = [](const Il2CppAssembly*) -> const Il2CppImage* { return &image; };
    il2cpp_image_get_class_count = [](const Il2CppImage*) -> size_t { return 3; };
    il2cpp_image_get_class = [](const Il2CppImage*, size_t index) -> const Il2CppClass* {
        static Il2CppClass *classes[] = {&object_class, &string_class, &demo_class}; return classes[index];
    };
    il2cpp_class_get_type = [](Il2CppClass *k) -> const Il2CppType* {
        return k == &object_class ? &object_type : (k == &string_class ? &string_type : &demo_type);
    };
    il2cpp_class_get_name = [](Il2CppClass *k) -> const char* {
        return k == &object_class ? "Object" : (k == &string_class ? "String" : "Demo");
    };
    il2cpp_class_get_namespace = [](Il2CppClass *k) -> const char* { return k == &demo_class ? "Game" : "System"; };
    il2cpp_class_from_type = [](const Il2CppType *t) -> Il2CppClass* {
        return t == &object_type ? &object_class : (t == &string_type ? &string_class : &demo_class);
    };
    il2cpp_class_get_parent = [](Il2CppClass *k) -> Il2CppClass* { return k == &object_class ? nullptr : &object_class; };
    il2cpp_class_instance_size = [](Il2CppClass*) -> int32_t { return sizeof(void*) * 2 + 8; };
    il2cpp_class_is_enum = [](const Il2CppClass*) { return false; };
    il2cpp_class_is_valuetype = [](const Il2CppClass*) { return false; };
    il2cpp_class_get_fields = [](Il2CppClass *k, void **iter) -> FieldInfo* {
        if (k != &demo_class || *iter) return nullptr;
        *iter = reinterpret_cast<void*>(1); return &field;
    };
    il2cpp_field_get_name = [](FieldInfo*) -> const char* { return "value"; };
    il2cpp_field_get_flags = [](FieldInfo*) -> int { return FIELD_ATTRIBUTE_PUBLIC; };
    il2cpp_field_get_type = [](FieldInfo*) -> const Il2CppType* { return &int_type; };
    il2cpp_field_get_offset = [](FieldInfo*) -> size_t { return sizeof(void*) * 2; };
    il2cpp_class_get_methods = [](Il2CppClass *k, void **iter) -> const MethodInfo* {
        if (k != &demo_class || *iter) return nullptr;
        *iter = reinterpret_cast<void*>(1); return &method;
    };
    il2cpp_method_get_name = [](const MethodInfo*) -> const char* { return "Run"; };
    il2cpp_method_get_flags = [](const MethodInfo*, uint32_t *impl) -> uint32_t { *impl = 0; return METHOD_ATTRIBUTE_PUBLIC; };
    il2cpp_method_get_return_type = [](const MethodInfo*) -> const Il2CppType* { return &void_type; };
    il2cpp_method_get_param_count = [](const MethodInfo*) -> uint32_t { return 1; };
    il2cpp_method_get_param = [](const MethodInfo*, uint32_t) -> const Il2CppType* { return &int_type; };
    il2cpp_method_get_param_name = [](const MethodInfo*, uint32_t) -> const char* { return "value"; };
    il2cpp_get_corlib = []() -> const Il2CppImage* { return &image; };
    il2cpp_class_from_name = [](const Il2CppImage*, const char*, const char*) -> Il2CppClass* { return &string_class; };
    literal.klass = reinterpret_cast<uintptr_t>(&string_class);
    literal.length = 5;
    literal.chars[0] = 'A'; literal.chars[1] = '"'; literal.chars[2] = 0xd83d; literal.chars[3] = 0xde00; literal.chars[4] = '\n';
    references[0] = reinterpret_cast<uintptr_t>(&demo_class);
    references[1] = reinterpret_cast<uintptr_t>(&demo_type);
    references[2] = reinterpret_cast<uintptr_t>(&method);
    references[3] = reinterpret_cast<uintptr_t>(&literal);
    references[4] = 0xa0000001;
}
}
int main(int argc, char **argv) {
    if (argc != 2) return 2;
    configure();
    std::string dir = argv[1];
    const std::string metadata_path = dir + "/global-metadata.dat";
    int fd = open(metadata_path.c_str(), O_CREAT | O_TRUNC | O_RDWR, 0600);
    assert(fd >= 0 && ftruncate(fd, 4096) == 0);
    auto *data = static_cast<uint32_t*>(mmap(nullptr, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));
    assert(data != MAP_FAILED);
    data[0] = 0xfab11baf; data[1] = 29; data[2] = 64; data[3] = 8; data[4] = 80; data[5] = 5;
    data[16] = 5; data[17] = 0;
    memcpy(reinterpret_cast<char*>(data) + 80, "hello", 5);
    Dl_info info{};
    assert(dladdr(reinterpret_cast<void*>(demo_function), &info));
    const auto base = reinterpret_cast<uintptr_t>(info.dli_fbase);
    std::ofstream expected(dir + "/expected.json");
    expected << "{\"method\":" << reinterpret_cast<uintptr_t>(demo_function) - base
             << ",\"type_ref\":" << reinterpret_cast<uintptr_t>(&references[0]) - base
             << ",\"method_ref\":" << reinterpret_cast<uintptr_t>(&references[2]) - base
             << ",\"string_ref\":" << reinterpret_cast<uintptr_t>(&references[3]) - base
             << ",\"encoded_ref\":" << reinterpret_cast<uintptr_t>(&references[4]) - base << '}';
    expected.close();
    std::string error;
    if (!export_runtime_files(dir, base, error)) { std::cerr << error << '\n'; return 1; }
    munmap(data, 4096); close(fd);
    il2cpp_image_get_class = nullptr;
    assert(!export_runtime_files(dir, base, error));
    assert(error.find("API") != std::string::npos);
    std::cout << "runtime collection, memory references, missing-API failure: PASS\n";
}
