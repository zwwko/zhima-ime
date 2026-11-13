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

## Windows 开发版验证

日期：2026-11-13（Asia/Shanghai）。平台：Windows x64，Rust 1.94.0、MSVC 19.44、Windows SDK 10.0.26100.0。

- `cargo test --workspace --locked`：13 项测试通过，包含新增的协议输入校验、试探无副作用、请求重放拒绝、连接隔离和提交确认测试。
- `cargo fmt --all -- --check`：通过。
- `cargo clippy --workspace --all-targets --locked --offline -- -D warnings`：通过（默认功能）。
- `scripts/build-windows.ps1`：x64 Rust Release 服务、x64/x86 TSF DLL 及两种位数的原生验证程序构建通过，C++ 使用 `/W4 /WX`。
- `scripts/test-windows.ps1`：两种位数的前端通信代码均连接真实 Rust 服务，拼音和五笔候选、提交与重复确认、试探、焦点隔离、模式切换和断线会话重建通过；服务退出和挂起场景验证失败返回及超时取消。
- 原生 COM：工厂创建、接口身份、对象引用和卸载条件验证通过。无注册环境中尝试前台激活返回 `E_INVALIDARG`，已验证失败回滚；不将该结果计为输入源激活通过。

未安装或注册系统输入源，未执行真实输入、安装/卸载回滚、跨应用、跨用户、多登录会话、DPI/多屏和性能验收。产物为 `dist/windows` 未签名开发包，仍使用测试词库。详细范围见 [Windows 开发版说明](windows.md)。

## Windows EXE 安装与卸载程序

日期：2026-11-13（Asia/Shanghai）。使用官方 Inno Setup 6.7.4，安装编译器经 SHA-256 与有效的 Pyrsys B.V. Authenticode 签名核验；中文语言资源固定官方提交及校验值。

- `scripts/build-windows-installer.ps1 -SkipBuild`：成功生成 `dist/ZhimaIME-0.1.0-windows-x64-setup.exe` 和 `.sha256` 校验文件。用户安装不需要 PowerShell 7 或编译工具，安装后自动生成 `unins000.exe` 和卸载数据，提供系统应用列表及开始菜单卸载入口。
- `scripts/test-windows-installer.ps1`：在工作区和独立 HKCU 测试键执行，与生产安装定义相同，使用模拟注册 DLL 与模拟引擎，不修改真实输入法列表。通过 x64/x86 注册、第二种架构注册失败后的完整回滚、中文路径、启动项引号、重复安装拦截、注销失败中止删除、重试卸载清理、同名不同目录进程隔离、安装树外用户文件保留。
- 隔离验收日志：`target/installer-tests/208e674b094f4d4384af85cd7fc24432/`，包含 `failed-install.log`、`install.log`、`duplicate-install.log`、`failed-uninstall.log`、`uninstall.log` 和模拟注册事件记录。
- 隔离测试未安装真实 TSF DLL，未验证真实输入源启用、真实系统应用列表、开始菜单交互、跨用户启用或完整安装界面视觉。真实系统注册与输入验收仍需独立执行，不能以隔离结果代替。
