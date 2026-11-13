# 本机验证记录

日期：2026-11-13（Asia/Shanghai）。平台：macOS Apple Silicon；Rust 1.93.1。

- `cargo test --workspace`：10 个集成测试通过。
- `cargo fmt --all -- --check`：通过。
- `cargo clippy --workspace --all-targets -- -D warnings`：通过（默认功能）。
- `cargo check -p ime-settings --features gui`：通过。
- Objective-C InputMethodKit 接入：clang 语法检查通过。
- macOS Release 静态库及应用包：构建成功，未签名、未安装。
- C ABI 动态库：Python ctypes 创建会话、ni 输入、空格提交、释放资源通过。

没有执行真实输入源安装、跨应用输入、Windows/Linux 构建与运行或性能基准；这些不能由上述结果推导。

## 芝麻输入法重命名验证

日期：2026-11-13（Asia/Shanghai）。工程目录为 `/Users/ct/zww/code/github/zhima-ime`。

- 完整目标设计已保存为 [设计方案](design.md)，与 [实际实现状态](status.md) 分开记录；README 和 Markdown 相对链接检查通过。
- `cargo test --workspace --locked --offline`：10 个集成测试通过。
- `cargo fmt --all -- --check`：通过。
- `cargo clippy --workspace --all-targets --locked --offline -- -D warnings`：通过。
- `cargo check -p ime-settings --features gui --locked --offline`：通过。
- `sh scripts/build-macos.sh`：成功构建 Apple Silicon 未签名开发包 `dist/芝麻输入法.app`，仍使用测试词库。
- 包内可执行文件为 `Zhima`；Bundle ID 与输入源 ID 为 `org.zhima.inputmethod`；原生代码、二进制与 plist 中的连接名均为 `ZhimaConnection`。
- `include/zhima.h` 的 C 语法检查通过；源码、文档和脚本中旧名称扫描通过。
- 重命名前后 FFI 实现、词典实现及 Cargo.lock 的 SHA-256 一致，保留 `zf_*` ABI、`ZFIM` 格式及依赖版本。
- 旧名称的生成包已清理；没有安装输入法或修改系统输入源。
