// 不注册输入源的原生冒烟验证：COM 生命周期及真实 x86/x64 管道交互。
#include "ipc.h"
#include <msctf.h>
#include <wrl/client.h>
#include <iostream>
#include <thread>
using Microsoft::WRL::ComPtr;
/** UTF-8 输出兼容控制台和重定向，不依赖进程区域设置。 */
void Print(const std::wstring& message) {
    int length = WideCharToMultiByte(CP_UTF8, 0, message.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string text(length, '\0');
    WideCharToMultiByte(CP_UTF8, 0, message.c_str(), -1, text.data(), length, nullptr, nullptr);
    std::cerr << text.c_str() << "\n";
}
/** 失败即退出，输出中文且不包含输入正文。 */
void Require(bool condition, const wchar_t* message) { if (!condition) { Print(message); throw std::runtime_error("Windows 验证失败"); } }
/** 从响应中提取可断言的会话状态。 */
struct Snapshot {
    bool consumed = false;
    std::wstring preedit, first, commit;
    uint64_t commitId = 0;
};
/** 使用生产前端通信代码处理一次事件并解析完整快照。 */
Snapshot Send(zhima::Pipe& pipe, const std::string& key) {
    std::vector<uint8_t> bytes;
    Require(pipe.Key(2, key, bytes), L"管道事件请求失败");
    zhima::Reader reader(bytes); reader.Number(); reader.Number(8); reader.Number();
    Snapshot out; out.consumed = reader.Number() != 0; out.preedit = reader.Text();
    reader.Number(); reader.Number();
    auto count = reader.Number(); Require(count <= 9, L"候选数量越界");
    for (size_t i = 0; i < count; ++i) { auto text = reader.Text(); reader.Text(); if (!i) out.first = text; }
    auto commits = reader.Number(); Require(commits <= 1, L"提交数量越界");
    if (commits) { out.commitId = reader.Number(8); out.commit = reader.Text(); }
    Require(reader.Done(), L"快照解析不完整");
    return out;
}
/** 验证工厂、接口身份、真实 TSF 激活结果及失败回滚；不注册输入源。 */
void ComSmoke(const wchar_t* dll) {
    HMODULE module = LoadLibraryW(dll); Require(module != nullptr, L"无法加载 TSF DLL");
    auto get = reinterpret_cast<HRESULT (__stdcall*)(REFCLSID, REFIID, void**)>(GetProcAddress(module, "DllGetClassObject"));
    auto unload = reinterpret_cast<HRESULT (__stdcall*)()>(GetProcAddress(module, "DllCanUnloadNow"));
    Require(get && unload, L"缺少 COM 导出");
    const CLSID service = {0x63f94bc2,0x23a1,0x47fa,{0xa2,0x67,0x48,0x68,0x82,0x42,0xf6,0xa9}};
    ComPtr<IClassFactory> factory;
    Require(SUCCEEDED(get(service, IID_PPV_ARGS(&factory))), L"创建 COM 工厂失败");
    ComPtr<ITfTextInputProcessor> processor;
    Require(SUCCEEDED(factory->CreateInstance(nullptr, IID_PPV_ARGS(&processor))), L"创建输入法实例失败");
    ComPtr<ITfKeyEventSink> keys;
    Require(SUCCEEDED(processor.As(&keys)), L"缺少按键接口");
    ComPtr<IUnknown> a, b; processor.As(&a); keys.As(&b);
    Require(a.Get() == b.Get(), L"COM 身份不一致");
    ComPtr<ITfThreadMgr> manager;
    Require(SUCCEEDED(CoCreateInstance(CLSID_TF_ThreadMgr, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&manager))), L"创建 TSF 线程管理器失败");
    TfClientId client;
    Require(SUCCEEDED(manager->Activate(&client)), L"激活 TSF 失败");
    HRESULT activation = processor->Activate(manager.Get(), client);
    if (activation == E_INVALIDARG) Print(L"无注册测试环境中前台激活返回 E_INVALIDARG；已验证失败回滚，实际输入源激活仍需安装验收。");
    else Require(SUCCEEDED(activation), L"激活输入法出现非预期错误");
    Require(SUCCEEDED(processor->Deactivate()), L"停用输入法失败");
    manager->Deactivate(); manager.Reset(); a.Reset(); b.Reset(); keys.Reset(); processor.Reset(); factory.Reset();
    Require(unload() == S_OK, L"COM 资源未完全释放");
    FreeLibrary(module);
}
/** 模拟已连接但无响应的服务，验证生产客户端确实取消超时 I/O。 */
void StalledServer() {
    auto name = zhima::PipeName();
    HANDLE server = CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_BYTE | PIPE_REJECT_REMOTE_CLIENTS, 1, 65536, 65536, 0, nullptr);
    Require(server != INVALID_HANDLE_VALUE, L"无法创建故障测试管道");
    std::thread worker([server] {
        if (ConnectNamedPipe(server, nullptr) || GetLastError() == ERROR_PIPE_CONNECTED) Sleep(250);
        CloseHandle(server);
    });
    zhima::Pipe pipe; std::vector<uint8_t> bytes;
    auto before = GetTickCount64();
    bool success = pipe.Key(1, "n", bytes);
    auto elapsed = GetTickCount64() - before;
    pipe.Close();
    worker.join();
    Require(!success && elapsed < 500, L"服务挂起时未及时取消请求");
    Print(L"服务挂起验证通过：请求超时被取消且未重放。");
}
/** 验证测试词典、只读试探、断线隔离、Shift 模式及候选提交。 */
int wmain(int argc, wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);
    if (argc != 2) { std::wcerr << L"请提供 TSF DLL 路径或 --offline\n"; return 2; }
    HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(initialized)) { Print(L"COM 初始化失败，错误码：" + std::to_wstring(initialized)); return 1; }
    try {
        if (std::wstring(argv[1]) == L"--offline") {
            zhima::Pipe pipe; std::vector<uint8_t> bytes;
            auto before = GetTickCount64();
            Require(!pipe.Key(1, "n", bytes), L"服务断开后仍返回成功");
            Require(GetTickCount64() - before < 500, L"服务断开时超过等待上限");
            Print(L"服务失联验证通过：失败返回且未阻塞。");
            StalledServer();
            CoUninitialize(); return 0;
        }
        ComSmoke(argv[1]);
        zhima::Pipe a, b;
        auto start = Send(a, "n"); Require(start.preedit == L"n", L"预编辑不正确");
        std::vector<uint8_t> probe;
        for (int i = 0; i < 5; ++i) Require(a.Key(1, "i", probe), L"试探请求失败");
        auto ready = Send(a, "i"); Require(ready.preedit == L"ni" && ready.first == L"你", L"试探产生副作用或候选错误");
        Require(!Send(b, "Space").consumed, L"会话状态串入其他连接");
        auto commit = Send(a, "Space"); Require(commit.commit == L"你" && commit.preedit.empty(), L"候选提交失败");
        a.Ack(commit.commitId, true); a.Ack(commit.commitId, true);
        Send(a, "n"); Send(a, "FocusOut"); Require(!Send(a, "Space").consumed, L"焦点取消失败");
        Send(a, "Shift"); Require(!Send(a, "n").consumed, L"英文模式未直通");
        a.Close(); Require(Send(a, "n").preedit == L"n", L"断线后未建立独立会话");
        Send(a, "Escape"); Send(a, "w"); Send(a, "q"); Send(a, "i");
        Require(Send(a, "y").first == L"你", L"五笔候选错误");
        Print(L"Windows 原生验证通过：COM 生命周期、管道、试探、焦点隔离、拼音与五笔提交。");
    } catch (...) { CoUninitialize(); return 1; }
    CoUninitialize(); return 0;
}
