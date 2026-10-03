#include "export_model.h"
#include <cassert>
#include <iostream>
int main(int argc, char **argv) {
    if (argc != 2) return 2;
    using namespace il2cpp_export;
    Snapshot s;
    s.methods = {{0x2000, "Game.\"Demo$$Run", "void demo(void);", "v"}, {0x1000, "First", "", "i"}, {0x2000, "Alias", "void alias(void);", "v"}};
    s.strings = {{0x3000, std::string("quote\"\\\n\t\0tail", 14)}, {0x3010, utf16_to_utf8({0x4e2d,0xd83d,0xde00,0xd800})}};
    s.metadata = {{0x4000, "TypeInfo", "Il2CppClass*"}, {0x4010, "Field$Demo.value", ""}};
    s.metadata_methods = {{0x4020, "Method$Demo.Run()", 0x2000}};
    Type layout;
    layout.name = "class */ evil"; layout.cname = "sample"; layout.size = 32;
    layout.fields = {{"int", "uint32_t", 8, 4}, {"duplicate", "uint32_t", 16, 4}, {"duplicate", "uint32_t", 20, 4},
        {"overlay", "uint16_t", 21, 2}, {"opaque", "", 24, 8}};
    s.types.push_back(layout);
    s.limitations = {"测试仅覆盖公开模型，不是设备验收"};
    std::string error;
    assert(!write_exports(std::string(argv[1])+"/missing/child",s,error));
    assert(!error.empty());
    assert(write_exports(argv[1],s,error));
    assert(identifier("int") != "int");
    std::cout << "escaping, UTF-16, overlapping layout, duplicates, write failure: PASS\n";
}
