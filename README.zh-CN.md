# Zygisk-Il2CppDumper
Zygisk版Il2CppDumper，在游戏运行时dump il2cpp数据，可以绕过保护，加密以及混淆。

## 如何食用
1. 安装[Magisk](https://github.com/topjohnwu/Magisk) v24以上版本并开启Zygisk
2. 生成模块
   - GitHub Actions
      1. Fork这个项目
      2. 在你fork的项目中选择**Actions**选项卡
      3. 在左边的侧边栏中，单击**Build**
      4. 选择**Run workflow**
      5. 输入游戏包名并点击**Run workflow**
      6. 等待操作完成并下载
   - Android Studio
      1. 下载源码
      2. 编辑`game.h`, 修改`GamePackageName`为游戏包名
      3. 使用Android Studio运行gradle任务`:module:assembleRelease`编译，zip包会生成在`out`文件夹下
3. 在Magisk里安装模块
4. 启动游戏，会在`/data/data/GamePackageName/files/`目录下生成`dump.cs`

## 手机端多产物导出

除原有 `dump.cs`，本分支新增 `script.json`、`stringliteral.json`、`il2cpp.h` 和 `export-report.json`，全部在手机端生成，不生成 `DummyDll`。产物位于应用数据目录的 `files/il2cpp-export-<本次随机标识>/`。

JSON 字段对齐 Il2CppDumper 6.7.46；运行时可恢复的信息与静态 EXE 不完全相同。使用方法、准确覆盖范围、IDA 导入和验证说明见 [RUNTIME_EXPORTS.md](RUNTIME_EXPORTS.md)。
