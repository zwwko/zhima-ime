# 芝麻输入法 · Zhima IME

工程目录：`zhima-ime`。当前为开发版本。

Rust 自研的 86 五笔与拼音混输工程。当前为开发版本，尚未达到计划中的三平台正式发行条件。

## 已实现

- Cargo workspace：词典、五笔、拼音、核心状态机、SQLite 用户词库、C ABI、命令行及可选 egui 设置表单。
- 五笔精确匹配优先、拼音字词查询、候选去重与反查、按键回放、分页、临时拼音、提交确认、焦点隔离。
- 确定性词典编译和损坏校验、Rime 数据子集读取、TSV 输出、五笔手动造词编码函数。
- macOS InputMethodKit 原生开发接入和未签名应用构建脚本。
- Windows x86/x64 TSF 原生开发接入、当前用户命名管道 Rust 引擎、候选浮窗与开发包构建/安装/卸载脚本。

## 构建与验证

```sh
cargo test --workspace
cargo run -p ime-settings -- replay data/fixtures/wubi.tsv data/fixtures/pinyin.tsv
cargo run -p ime-settings -- compile data/fixtures/wubi.tsv /tmp/wubi.zfd
cargo run -p ime-settings --features gui -- gui
sh scripts/build-macos.sh
```

Windows（64 位 PowerShell 7，需 MSVC C++ 桌面开发组件）：

```powershell
./scripts/build-windows.ps1
./scripts/test-windows.ps1
./scripts/build-windows-installer.ps1 -DownloadTools -SkipBuild
```

可双击安装的 EXE 为 `dist/ZhimaIME-0.1.0-windows-x64-setup.exe`，提供中文向导、系统应用列表和开始菜单卸载入口，安装后自动生成 `unins000.exe`。首次打包可使用 `-DownloadTools` 获取固定版本的官方 Inno Setup 便携编译器，后续可离线打包。

脚本开发包位于 `dist/windows`，构建和冒烟测试不会注册真实系统输入源。显式安装步骤、权限范围及回滚方法见 [Windows 开发版说明](docs/windows.md)。

回放输入每行一个按键，例如 `n`、`i`、`Space`、`FocusOut`。输出 JSON 仅用于开发回放，包含测试输入；生产日志不得调用此回放入口。

`data/fixtures` 是人工编写的小型测试数据，不是完整可日用词库。生成的 macOS 包仅供开发测试，未自动安装或修改输入源。

## 当前限制

- Windows 已通过 x86/x64 构建、COM 工厂和真实管道冒烟测试；实际输入源注册、跨应用输入、DPI/多屏及 Modern 应用仍待验收。Linux Fcitx 5/IBus 适配尚未实现。
- 完整上游原始词库和许可证位于 `data/upstream`，快照校验值记录在 `manifest.json`；开发包仍使用测试词库。规范全码须核对来源字段。
- 拼音采用有界候选输出的词典扫描，尚未实现计划中的音节 Trie 与路径预算。
- 用户数据库和配置组件已实现，但尚未接入引擎后台持久化、实时配置；原生候选窗口尚不支持完整外观配置和鼠标交互。
- 设置表单中的字体、主题、标点与全半角尚未应用到输入会话；缺少词库管理界面和中文字体打包。
- macOS 尚缺 Shift 单击、候选鼠标操作、实际输入源安装测试和 Intel 构建验收。
- 已提供未签名 Windows EXE 安装包及卸载程序；仍无签名、公证、Linux DEB、跨平台应用兼容及性能验收结果。

设计目标见 [完整设计方案](docs/design.md)，实际完成情况见 [实现状态](docs/status.md)。自研代码 Apache-2.0；后续引入词库将独立保留原许可证。
