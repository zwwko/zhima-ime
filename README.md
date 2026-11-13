# 芝麻输入法 · Zhima IME

工程目录：`zhima-ime`。当前为开发版本。

Rust 自研的 86 五笔与拼音混输工程。当前为开发版本，尚未达到计划中的三平台正式发行条件。

## 已实现

- Cargo workspace：词典、五笔、拼音、核心状态机、SQLite 用户词库、C ABI、命令行及可选 egui 设置表单。
- 五笔精确匹配优先、拼音字词查询、候选去重与反查、按键回放、分页、临时拼音、提交确认、焦点隔离。
- 确定性词典编译和损坏校验、Rime 数据子集读取、TSV 输出、五笔手动造词编码函数。
- macOS InputMethodKit 原生开发接入和未签名应用构建脚本。

## 构建与验证

```sh
cargo test --workspace
cargo run -p ime-settings -- replay data/fixtures/wubi.tsv data/fixtures/pinyin.tsv
cargo run -p ime-settings -- compile data/fixtures/wubi.tsv /tmp/wubi.zfd
cargo run -p ime-settings --features gui -- gui
sh scripts/build-macos.sh
```

回放输入每行一个按键，例如 `n`、`i`、`Space`、`FocusOut`。输出 JSON 仅用于开发回放，包含测试输入；生产日志不得调用此回放入口。

`data/fixtures` 是人工编写的小型测试数据，不是完整可日用词库。生成的 macOS 包仅供开发测试，未自动安装或修改输入源。

## 当前限制

- Windows TSF/命名管道服务、Linux Fcitx 5/IBus 适配尚未实现。
- 完整上游原始词库和许可证位于 `data/upstream`，快照校验值记录在 `manifest.json`；开发包仍使用测试词库。规范全码须核对来源字段。
- 拼音采用有界候选输出的词典扫描，尚未实现计划中的音节 Trie 与路径预算。
- 用户数据库和配置组件已实现，但尚未接入引擎后台持久化、实时配置与原生候选窗口。
- 设置表单中的字体、主题、标点与全半角尚未应用到输入会话；缺少词库管理界面和中文字体打包。
- macOS 尚缺 Shift 单击、候选鼠标操作、实际输入源安装测试和 Intel 构建验收。
- 尚无签名、公证、Windows 安装包、Linux DEB、跨平台应用兼容及性能验收结果。

设计目标见 [完整设计方案](docs/design.md)，实际完成情况见 [实现状态](docs/status.md)。自研代码 Apache-2.0；后续引入词库将独立保留原许可证。
