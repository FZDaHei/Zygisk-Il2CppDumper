#!/usr/bin/env python3
"""在宿主机验证真正的原生采集/文件内容，不把样本测试当作 Android 验收。"""
import json
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
CPP = ROOT / "module/src/main/cpp"
CXX = os.environ.get("CXX", "c++")


def run(*args):
    subprocess.run([str(x) for x in args], check=True)


def read(directory, name):
    return json.loads((directory / name).read_text(encoding="utf-8"))


def schema(directory):
    script = read(directory, "script.json")
    assert set(script) == {"ScriptMethod", "ScriptString", "ScriptMetadata", "ScriptMetadataMethod", "Addresses"}
    for field, keys in {
        "ScriptMethod": {"Address", "Name", "Signature", "TypeSignature"},
        "ScriptString": {"Address", "Value"},
        "ScriptMetadata": {"Address", "Name", "Signature"},
        "ScriptMetadataMethod": {"Address", "Name", "MethodAddress"},
    }.items():
        for row in script[field]:
            assert set(row) == keys
            assert isinstance(row["Address"], int) and row["Address"] >= 0
    assert script["Addresses"] == sorted(set(script["Addresses"]))
    strings = read(directory, "stringliteral.json")
    assert strings == [{"value": row["Value"], "address": f'0x{row["Address"]:X}'} for row in script["ScriptString"]]
    report = read(directory, "export-report.json")
    assert report["EquivalentToStaticExe"] is False
    assert report["DummyDllRequested"] is False
    assert report["Methods"] == len(script["ScriptMethod"])
    assert report["StringReferences"] == len(script["ScriptString"])
    return script


with tempfile.TemporaryDirectory(prefix="il2cpp-export-test-") as temporary:
    directory = Path(temporary)
    for fixture in ("model", "runtime"):
        executable = directory / fixture
        sources = [ROOT / "tests" / f"{fixture}_fixture.cpp", CPP / "export_model.cpp"]
        if fixture == "runtime":
            sources.append(CPP / "runtime_export.cpp")
        run(CXX, "-std=c++20", "-Wall", "-Wextra", "-Werror", "-O1", "-g", "-I", CPP,
            *sources, "-ldl", "-o", executable)
        output = directory / f"{fixture}-output"
        output.mkdir()
        run(executable, output)
        script = schema(output)
        header_test = '#include "il2cpp.h"\n'
        for method in script["ScriptMethod"]:
            if method["Signature"]:
                header_test += method["Signature"] + "\n"
        if fixture == "model":
            assert script["Addresses"] == [0x1000, 0x2000]
            assert '\0' in script["ScriptString"][0]["Value"]
            assert script["ScriptString"][1]["Value"] == '中😀�'
            header_test += 'static_assert(sizeof(sample_o) == 32);\n'
            header_test += 'static_assert(offsetof(sample_o, t_int_1) == 8);\n'
        else:
            expected = read(output, "expected.json")
            method = next(m for m in script["ScriptMethod"] if m["Name"] == "Game.Demo$$Run")
            assert method["Address"] == expected["method"]
            assert method["TypeSignature"] == "viii"
            strings = {s["Address"]: s["Value"] for s in script["ScriptString"]}
            assert strings[expected["string_ref"]] == 'A"😀\n'
            assert strings[expected["encoded_ref"]] == 'hello'
            assert any(r["Address"] == expected["type_ref"] and r["Name"] == "Game.Demo_TypeInfo" for r in script["ScriptMetadata"])
            assert any(r["Address"] == expected["method_ref"] and r["MethodAddress"] == expected["method"] for r in script["ScriptMetadataMethod"])
            header_test += 'static_assert(sizeof(t_Game_Demo_2_o) == sizeof(void*) * 2 + 8);\n'
            header_test += 'static_assert(offsetof(t_Game_Demo_2_o, t_value_2) == sizeof(void*) * 2);\n'
        (output / "header_test.cpp").write_text(header_test)
        run(CXX, "-std=c++20", "-Wall", "-Wextra", "-Werror", "-fsyntax-only", output / "header_test.cpp")
        # IDA 的 C 声明解析器需要 typedef；额外检查标准 C11 的可解析性。
        run(os.environ.get("CC", "cc"), "-x", "c", "-std=c11", "-Werror", "-fsyntax-only", output / "il2cpp.h")
        print(f"{fixture}: JSON schema, RVA, strings and compilable layouts PASS", flush=True)
print("native exports: PASS", flush=True)
