# Zygisk-Il2CppDumper
Il2CppDumper with Zygisk, dump il2cpp data at runtime, can bypass protection, encryption and obfuscation.

中文说明请戳[这里](README.zh-CN.md)

## How to use
1. Install [Magisk](https://github.com/topjohnwu/Magisk) v24 or later and enable Zygisk
2. Build module
   - GitHub Actions
      1. Fork this repo
      2. Go to the **Actions** tab in your forked repo
      3. In the left sidebar, click the **Build** workflow.
      4. Above the list of workflow runs, select **Run workflow**
      5. Input the game package name and click **Run workflow**
      6. Wait for the action to complete and download the artifact
   - Android Studio
      1. Download the source code
      2. Edit `game.h`, modify `GamePackageName` to the game package name
      3. Use Android Studio to run the gradle task `:module:assembleRelease` to compile, the zip package will be generated in the `out` folder
3. Install module in Magisk
4. Start the game, `dump.cs` will be generated in the `/data/data/GamePackageName/files/` directory

## 手机端多产物导出

本分支在手机端额外生成 `script.json`、`stringliteral.json`、`il2cpp.h` 和覆盖范围报告，不生成 `DummyDll`。输出格式参考 Il2CppDumper 6.7.46；并非静态 EXE 全量内容等价。详细使用和验证边界见 [RUNTIME_EXPORTS.md](RUNTIME_EXPORTS.md)。
