# Windows 开发版

面向 Windows 11 x64 的桌面应用接入。提供 x64 Rust 引擎服务，以及分别加载到 64 位和 32 位应用的 TSF COM DLL。当前仍为未签名开发版本，仅打包人工测试词库；实际输入源安装、跨应用输入、缩放与多屏等验收尚未执行。

## 构建与验证

需要 Windows x64、Rust MSVC 工具链、Visual Studio 2022 的“使用 C++ 的桌面开发”组件及 Windows SDK。不需要 Rust x86 目标；32 位前端由 MSVC 构建，连接同一个 x64 服务。PowerShell 脚本建议在 PowerShell 7 的 64 位进程中运行。

在仓库根目录执行：

```powershell
cargo test --workspace --locked
cargo fmt --all -- --check
cargo clippy --workspace --all-targets --locked -- -D warnings
./scripts/build-windows.ps1
./scripts/test-windows.ps1
./scripts/build-windows-installer.ps1 -DownloadTools -SkipBuild
./scripts/test-windows-installer.ps1
```

首次打包通过 `-DownloadTools` 下载固定版本 Inno Setup 6.7.4 及固定提交的官方中文语言文件。编译器安装文件同时验证 SHA-256、Authenticode 状态和发布者，语言文件验证 SHA-256；构建工具以便携方式解包至 `target/tools`，不添加工具的系统卸载项。资源准备后可去掉该参数离线构建。构建工具许可依照 [Inno Setup 官方说明](https://jrsoftware.org/isinfo.php)。

安装器为 `dist/ZhimaIME-0.1.0-windows-x64-setup.exe`，旁边提供 `.sha256` 校验文件。面向最终使用者的安装过程不要求 PowerShell、Rust、Visual Studio 或额外 VC++ 运行库，也不联网下载组件。

开发包生成在 `dist/windows/`：

同时生成便于传递的 `dist/zhima-ime-windows-dev.zip`，包含相同文件和本文说明。Rust 服务和 C++ 前端静态链接 CRT，不要求另行安装 VC++ 运行库。

| 文件 | 用途 |
|---|---|
| `ime-windows-service.exe` | 当前用户身份运行的 Rust 引擎进程 |
| `ZhimaMaintenance.exe` | 当前用户启用输入法、按完整路径停止安装目录引擎的维护工具 |
| `x64/ZhimaTsf.dll`、`x86/ZhimaTsf.dll` | 对应应用位数的 TSF COM 前端 |
| `x64/zhima-smoke.exe`、`x86/zhima-smoke.exe` | 使用生产通信代码的原生冒烟程序 |
| `wubi.tsv`、`pinyin.tsv` | 人工测试词典，不能作为日用词库 |
| `install-windows.ps1`、`uninstall-windows.ps1` | 显式安装及撤销系统注册 |
| `test-windows.ps1`、`LICENSE` | 包内验证入口与代码许可证 |

构建与测试不会调用注册入口。测试只启动本包服务，完成后终止本脚本启动的进程；已有同名服务运行时拒绝测试，防止误用其他词库。

## 安装、使用与回滚

### 推荐：双击 EXE 安装

1. 双击 `dist/ZhimaIME-0.1.0-windows-x64-setup.exe`，按 Windows 提示授予安装权限。
2. 按中文向导完成安装。安装目录默认为 `C:\Program Files\ZhimaIME`，当前不支持覆盖升级；已有目录或芝麻启动项会阻止安装，要求先卸载旧版。
3. 完成时保留“启动芝麻输入法引擎”选项。安装器使用安装前用户的身份调用 `InstallLayoutOrTip` 添加本输入法，不设置默认输入法、不清空其他输入源。使用 Win+空格选择芝麻输入法；列表未刷新时注销再登录。
4. 新登录用户可从开始菜单“芝麻输入法（开发版）→ 启用芝麻输入法”添加自己的启用项；引擎随各用户登录启动。若从已提升权限的进程启动安装器或使用另一个管理员账号授权，用户启用结果需在实际使用账号下检查，可使用同一开始菜单入口补充启用。

EXE 安装器会注册两种位数的 TSF DLL、创建登录启动项和开始菜单快捷方式，自动生成 `C:\Program Files\ZhimaIME\unins000.exe` 及对应卸载数据。可在 Windows“设置 → 应用 → 已安装的应用”中查找“芝麻输入法（开发版）”并卸载，也可通过开始菜单“卸载芝麻输入法”或直接运行 `unins000.exe`。卸载 EXE 必须与安装时生成的卸载数据一起保留，不单独移动该文件。

卸载前切换到其他输入源并关闭使用本输入法的应用。卸载器只停止安装目录的引擎进程；无法停止或注销失败会中止删除并保留程序文件。正常卸载移除自身的输入法注册、启动项、快捷方式、系统应用列表项和安装器记录的文件；安装树外的用户文件不会删除。被应用占用的 DLL 可能需要关闭应用或重新登录后再次清理。

### 备选：脚本开发包

安装会将包复制至 `%ProgramFiles%/ZhimaIME`，注册两种架构的 COM 类、简体中文 TSF 语言配置及键盘类别，添加系统登录启动项 `ZhimaIME`。启动项在用户登录时以该用户身份启动引擎，不创建以 SYSTEM 身份运行的 Windows 服务。不支持覆盖升级；目标目录已经存在时停止安装。

在提升权限的 **64 位 PowerShell 7** 中执行：

```powershell
./dist/windows/install-windows.ps1
```

注销后重新登录，使用 Win+空格选择“芝麻输入法（开发版）”。如未出现在列表中，检查 Windows 的“时间和语言 → 语言和区域 → 中文（简体）→ 语言选项 → 添加键盘”，并记录具体系统版本进行安装验收；不要修改默认键盘或手工删除其他输入源。真实输入源注册仍待验收，不能以构建成功推断该流程已通过。

默认五笔与拼音混输，字母输入编码，空格提交候选，数字 1–5 选当前页候选；上下键选择、PageUp/PageDown 翻页，Enter 提交原编码，Escape 取消，Backspace 删除编码。反引号进入临时拼音，Shift 单击切换中英文。Ctrl/Alt/Win 快捷键及大写字母交还应用。候选窗暂不支持鼠标操作。

卸载前切换到其他输入源并关闭使用本输入法的应用。提升权限运行：

```powershell
& "$env:ProgramFiles/ZhimaIME/uninstall-windows.ps1"
```

卸载撤销语言配置和两种架构的 COM 注册，移除登录启动项、停止安装路径对应的引擎进程，并删除固定安装目录。用户数据不在该目录内，不会删除。若 DLL 被应用占用，会保留剩余文件并提示注销后清理。安装注册失败会尝试撤销本次注册；若撤销也失败，保留文件并报告，不得把部分回滚表述为成功。

## 接入边界与数据流

```text
64 位应用 → x64 TSF DLL ─┐
                        ├→ 当前用户命名管道 → x64 Rust 服务 → 共享只读词典
32 位应用 → x86 TSF DLL ─┘                         └→ 每连接独立 Session
```

- 前端使用 C++/Win32 COM，复用系统 SDK 和 WRL；与设计目标中的 `windows-rs` 前端存在实现语言差异。引擎、词典和会话规则继续由 Rust 负责，不需要在两种位数的 DLL 中重复词典与算法。
- `OnTestKeyDown` 只查询 `Session::test_key`；实际事件在同步 TSF 写锁内送往服务。申请写锁失败时不推进引擎状态。文本写入成功后才发送提交确认；事件已经写入文本后，即使确认或界面更新失败也保持按键已消费，避免重复输入。
- 原生组合区使用 TSF composition；候选浮窗不抢焦点，位置限制在光标所在显示器工作区。焦点、上下文变化或应用强制终止组合时清理旧连接、候选及原上下文组合，不向新输入框提交旧内容。
- 读取上下文禁用/空上下文标记、只读状态及系统提供的密码 InputScope。敏感字段策略依赖宿主正确提供这些系统信息，仍需在真实密码控件验收。
- 系统 COM 类和 TSF 类别/语言配置的注册流程依据 [Microsoft TSF 注册文档](https://learn.microsoft.com/en-us/windows/win32/tsf/text-service-registration)；同步写锁用于按键回调，焦点清理使用异步锁，参见 [RequestEditSession](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfcontext-requesteditsession)。

## 通信协议与故障行为

管道名为 `\\.\pipe\ZhimaIME-v1-<当前进程用户 SID>`，SID 来自进程令牌。DACL 仅授予该 SID 访问权限，拒绝远程客户端。首实例使用 `FILE_FLAG_FIRST_PIPE_INSTANCE`，接受连接后先创建下一监听实例，再移交原连接，避免无人监听时被其他用户抢占同名管道。设计依据见 [CreateNamedPipeW](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-createnamedpipew)。同一用户内的恶意进程不在隔离保证范围内。

协议所有整数均为小端，无指针或平台位宽字段：

| 字段 | 内容 |
|---|---|
| 帧长度 | u32，后续帧字节数，范围 16–65536 |
| 通用头 | version: u32=1、sequence: u64、operation: u32 |
| 请求 TEST=1 / PROCESS=2 | 剩余内容为 UTF-8 标准按键，1–32 字节 |
| 请求 ACK=3 | commit_id: u64、success: u32（0 或 1） |
| 响应头 | 原样回显通用头，前端验证版本、序号和操作 |
| TEST 响应 | consumed: u32 |
| PROCESS 响应 | consumed: u32、preedit: 字符串、selected/page/count: u32，随后 count 对候选文本/五笔提示；commit_count: u32，随后提交 id: u64 与文本 |
| ACK 响应 | 仅通用头 |

字符串编码为 u32 字节数及 UTF-8 内容。服务要求请求序号严格递增；未知版本/操作、重放、非法编码、截断及超长输入会断开连接。每连接至多一次处理一个请求，不自动重放任何业务操作；断线时销毁该连接的组合与待确认提交。

前端每次请求的读写总等待预算为 100 ms；超时取消 I/O、断开连接并放行未消费的按键，随后 1 秒退避。测试与实际事件各自计时；该上限是故障保护，并不是设计中的 16 ms 性能验收结果。服务最多 64 个管道实例，保留监听空间；满载时等待旧连接释放，不创建无界线程。

## 已验证与待验收

2026-11-13 在本机 Windows 上已执行：Rust 工作区 13 项测试、格式检查、Clippy 默认功能检查；MSVC x64/x86 `/W4 /WX` Release 构建；两种位数的真实管道、候选、提交、焦点隔离、试探无副作用、连接重建、服务失联和挂起超时测试；COM 工厂、接口身份、失败清理及卸载引用计数检查。

无注册的原生测试尝试使用 TSF 线程管理器激活，系统返回 `E_INVALIDARG`；测试明确记录该结果并验证失败回滚，不将其计为输入源激活通过。[微软接口文档](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfkeystrokemgr-advisekeyeventsink)将此值定义为无效参数错误；具体安装后的行为仍需验证。

安装器新增隔离验收：使用相同 `.iss` 安装定义，将 x86/x64 TSF DLL 替换为仅写入测试标记的模拟 DLL，把目录限定在工作区、启动项限定在 `HKCU/Software/ZhimaIME/InstallerTest`，不添加真实输入源、开始菜单或系统应用列表项。已验证注册失败回滚、成功安装、重复安装拦截、注销失败保留文件及重试卸载、完整路径识别进程、中文路径和安装树外文件保留。日志目录见 [验证记录](verification.md)。真实安装器构建不包含测试宏、模拟 DLL 或故障注入。

输入法启用接口依据 [Microsoft InstallLayoutOrTip 文档](https://learn.microsoft.com/en-us/windows/win32/tsf/installlayoutortip)，注册和卸载日志使用 [Inno Setup regserver 机制](https://jrsoftware.org/ishelp/topic_filessection.htm)。构建和隔离测试不能替代真实系统上的注册验收。

待验收：真实 TSF 注册/注销、实际输入法列表刷新、真实键盘输入和密码控件、浏览器/Office/VS Code/终端及 32 位应用、Shift 组合键、多显示器与 DPI、Modern/沙箱应用、多个登录会话、跨用户拒绝访问、并发上限和长期稳定性。当前没有 MSI、签名、自动升级、托盘管理、语言栏模式按钮、候选鼠标操作或正式性能结论。用户学习仅保存在当前连接内存中，尚未接入 SQLite 持久化和配置热更新。
