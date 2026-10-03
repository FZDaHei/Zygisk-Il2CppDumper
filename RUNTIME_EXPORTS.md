# 手机端多产物导出

目标：在原有 `dump.cs` 之外，直接在游戏进程内导出 `script.json`、`stringliteral.json`、`il2cpp.h`。不生成 `DummyDll`，不需要电脑转换或手机安装 .NET。

## 文件位置

启动编译时填写的目标应用后，原路径 `files/dump.cs` 保持不变。新增产物在应用自己的数据目录下：

```text
files/il2cpp-export-<本次随机标识>/
  dump.cs
  script.json
  stringliteral.json
  il2cpp.h
  export-report.json
```

例如主用户的应用数据目录通常为 `/data/user/0/<package>/`；实际使用 Zygisk 传入的 `app_data_dir`，不硬编码用户编号。每次使用独立目录，全部写入成功后才发布。失败保留原 `dump.cs`，原因写入 `files/il2cpp-export-error.txt` 和 `Perfare` 日志；下次成功会清除旧错误文件。每次成功保留一个快照，请自行清理不再需要的历史快照。

## 与 Il2CppDumper 6.7.46 的关系

JSON 字段取自参考项目的 `ScriptJson.cs` / `StructGenerator.cs`：

| 产物 | 本实现内容 |
| --- | --- |
| `script.json` | `ScriptMethod`、`ScriptString`、`ScriptMetadata`、`ScriptMetadataMethod`、去重排序的 `Addresses`；地址为相对本次 `libil2cpp` 加载基址的 RVA，不是 ASLR 后的绝对地址。 |
| `stringliteral.json` | 真实发现的引用地址与字符串值，字段为小写 `value`、`address`，地址使用 `0x` 十六进制。 |
| `il2cpp.h` | 运行时实例字段与继承字段的实际偏移、对象大小、指针宽度；类名经过 ASCII 转义并消除碰撞；保留对象头。复杂值类型字段和显式布局重叠区域按真实大小输出不透明字节，不编造私有结构。 |
| `export-report.json` | 实际类型/方法/引用数量、扫描字节数和未恢复信息。`EquivalentToStaticExe=false` 明确表示不能宣称与静态 EXE 全量结果相同。 |

**这是原生运行时导出，不是将 EXE 的全部内部分析器移植到 Android。文件种类与 JSON 字段对齐，但目前不能保证内容与 EXE 完全一致。** 原始 `dump.cs` 的既有语义保持不变。

方法从公开 IL2CPP API 枚举；类、类型、字段和方法引用从 `libil2cpp` 的可读非执行 `PT_LOAD` 段识别。字符串来源：

1. 已初始化的真实 `System.String` 对象引用，校验其类指针与长度，并正确处理 UTF-16 代理对。
2. 若能取得标准明文 `global-metadata.dat` 内存映射，对 metadata v27+ 尚未解析的字符串引用按编码索引读取字面量。

不扫描任意进程；只读取当前目标进程。所有内存读取经当前映射范围检查和 `/proc/self/mem`，不可读地址跳过；不调用游戏方法触发字符串初始化。引用扫描有 15 秒、256 MiB、100000 候选对象及约 32 MiB 字符串输出预算，触及上限会报告不完整。

以下不是本版的完成声明：全部泛型实例、旧版未初始化 metadata usages、静态字段块、vtable、RGCTXData、完整 `Il2CppClass` / `MethodInfo` 私有布局。缺少 metadata 版本时，不猜测静态方法的隐藏参数 ABI；无法正确表达的按值参数/返回值同样用 `Signature=null` 表示。空字符串数组只表示当前扫描没有发现引用，不证明游戏没有字符串。

## IDA / Ghidra 使用

先设置与目标 ABI 一致的数据库位数和 image base。JSON 的地址由导入脚本加数据库 image base 得到分析地址。

- 仅导入名称和字符串，可使用原版 `ida_py3.py` 或相应 Ghidra JSON 导入脚本。
- 导入本版头文件和方法签名，使用仓库 `tools/ida_runtime.py`，先在 IDA 导入生成的 `il2cpp.h`。该脚本基于 6.7.46 的 `ida_with_struct_py3.py`，增加 `Signature=null` 跳过逻辑；许可证随附。原版带结构脚本直接解析空签名可能失败。
- `il2cpp.h` 的 C/C++ 编译检查通过不等于特定 IDA/Ghidra 版本已实测；其解析器可能需要按工具版本处理标准头文件。

## root 与版本边界

沿用标准 Zygisk API 2 和 `zygisk/<ABI>.so` 布局；KernelSU 需要正常工作的 Zygisk Next。没有添加 Magisk 专有运行时依赖。旧 Gradle `flashRelease` 使用 Magisk 命令，KernelSU/APatch 请用自己的管理器安装 ZIP。

新增完整导出需要 `il2cpp_image_get_class` 等 API，通常对应 Unity 2018.3+；若不可用会明确失败并保留既有 `dump.cs`。这不是所有 Unity 版本或修改版 IL2CPP ABI 的兼容保证，原项目也仍通过 `MethodInfo` 首字段读取方法地址。

## 验证

运行 `python3 tests/test_exports.py`。测试真正编译并运行 C++ 导出器，使用可控 IL2CPP API 和真实的当前进程 ELF 数据段、内存映射，验证：

- 方法 RVA、方法/类型引用槽地址、已初始化与 v27+ 编码字符串引用。
- JSON schema、转义、Unicode、排序去重、缺失 API 和文件写入失败。
- 头文件在 C11/C++20 下可解析，实例大小与字段偏移符合样本，重叠字段不会产生错误顺序布局。

测试样本不证明真实 Android 游戏加载成功。现有 GitHub `Build` 工作流继续构建 Android 模块；本地执行上述测试命令。新增 CI 测试步骤因当前凭据无 workflow 写权限，未提交到远程。设备验收还需要安装、重启、启动目标应用，并检查上述五个文件及报告中的覆盖范围。
