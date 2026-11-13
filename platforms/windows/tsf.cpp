// Windows TSF 薄前端：应用只加载 COM 接入层，词典与会话在用户服务中运行。
#include "ipc.h"
#include <msctf.h>
#include <initguid.h>
#include <inputscope.h>
#include <wrl/client.h>
#include <functional>
#include <algorithm>
#include <new>
using Microsoft::WRL::ComPtr;
// 稳定的 COM 和语言配置标识，x86/x64 共用。
const CLSID kService = {0x63f94bc2,0x23a1,0x47fa,{0xa2,0x67,0x48,0x68,0x82,0x42,0xf6,0xa9}};
const GUID kProfile = {0xc7a1c3b0,0x78ad,0x4563,{0x91,0xc4,0x69,0xb5,0x63,0x26,0xa9,0x13}};
constexpr wchar_t kClsid[] = L"{63F94BC2-23A1-47FA-A267-48688242F6A9}";
constexpr LANGID kLanguage = 0x0804;
HMODULE gModule = nullptr;
LONG gObjects = 0;

/** 为 TSF 管理编辑锁，捕获 COM 引用确保异步取消不会访问已析构对象。 */
class Edit final : public ITfEditSession {
    LONG refs_ = 1;
    std::function<HRESULT(TfEditCookie)> action_;
public:
    /** 接收一次编辑任务。 */
    explicit Edit(std::function<HRESULT(TfEditCookie)> action) : action_(std::move(action)) { InterlockedIncrement(&gObjects); }
    /** 释放闭包及模块引用计数。 */
    ~Edit() { InterlockedDecrement(&gObjects); }
    /** 提供标准 COM 标识。 */
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (id == IID_IUnknown || id == IID_ITfEditSession) *out = static_cast<ITfEditSession*>(this);
        if (!*out) return E_NOINTERFACE;
        AddRef(); return S_OK;
    }
    /** 保留编辑对象。 */
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs_); }
    /** 释放编辑对象。 */
    ULONG STDMETHODCALLTYPE Release() override { auto refs = InterlockedDecrement(&refs_); if (!refs) delete this; return refs; }
    /** 在 TSF 提供的锁内执行，异常不能越过 COM 边界。 */
    HRESULT STDMETHODCALLTYPE DoEditSession(TfEditCookie cookie) override { try { return action_(cookie); } catch (...) { return E_FAIL; } }
};
/** 申请编辑锁；同步任务只用于按键回调，焦点取消使用异步任务。 */
HRESULT Request(ITfContext* context, TfClientId client, DWORD flags, std::function<HRESULT(TfEditCookie)> action) {
    auto edit = new(std::nothrow) Edit(std::move(action));
    if (!edit) return E_OUTOFMEMORY;
    HRESULT result = E_FAIL;
    HRESULT hr = context->RequestEditSession(client, edit, flags, &result);
    edit->Release();
    return FAILED(hr) ? hr : result;
}
/** 单线程 TSF 实例，焦点切换时丢弃旧管道会话并取消原组合。 */
class Service final : public ITfTextInputProcessor, public ITfKeyEventSink,
    public ITfThreadMgrEventSink, public ITfCompositionSink {
    LONG refs_ = 1;
    ComPtr<ITfThreadMgr> manager_;
    ComPtr<ITfContext> context_;
    ComPtr<ITfComposition> composition_;
    TfClientId client_ = TF_CLIENTID_NULL;
    DWORD sink_ = TF_INVALID_COOKIE;
    zhima::Pipe pipe_;
    HWND panel_ = nullptr;
    HFONT font_ = nullptr;
    bool shiftOnly_ = false;
    /** 清除候选窗口及旧会话；取消任务绑定原组合，不会写入新焦点。 */
    void Reset(TfEditCookie cookie = TF_INVALID_COOKIE) noexcept {
        shiftOnly_ = false;
        pipe_.Close();
        if (panel_) ShowWindow(panel_, SW_HIDE);
        auto context = context_;
        auto composition = composition_;
        composition_.Reset(); context_.Reset();
        if (context && composition) {
            auto cancel = [composition](TfEditCookie editCookie) {
                ComPtr<ITfRange> range;
                if (SUCCEEDED(composition->GetRange(&range))) range->SetText(editCookie, 0, L"", 0);
                return composition->EndComposition(editCookie);
            };
            // 按键故障发生在写锁内时立即清理，避免异步删除波及后续按键。
            try {
                if (cookie != TF_INVALID_COOKIE) cancel(cookie);
                else Request(context.Get(), client_, TF_ES_ASYNC | TF_ES_READWRITE, cancel);
            } catch (...) { OutputDebugStringW(L"芝麻输入法：组合清理失败，连接和候选已关闭。\n"); }
        }
    }
    /** 检查系统禁用和空上下文标志，避免在敏感控件中拦截。 */
    bool Disabled(ITfContext* context) {
        ComPtr<ITfCompartmentMgr> compartments;
        if (FAILED(context->QueryInterface(IID_PPV_ARGS(&compartments)))) return true;
        for (auto id : {GUID_COMPARTMENT_KEYBOARD_DISABLED, GUID_COMPARTMENT_EMPTYCONTEXT}) {
            ComPtr<ITfCompartment> compartment;
            VARIANT value; VariantInit(&value);
            if (SUCCEEDED(compartments->GetCompartment(id, &compartment)) && SUCCEEDED(compartment->GetValue(&value))) {
                bool disabled = value.vt == VT_I4 && value.lVal != 0;
                VariantClear(&value);
                if (disabled) return true;
            }
        }
        TF_STATUS status{};
        if (FAILED(context->GetStatus(&status)) || (status.dwDynamicFlags & TF_SD_READONLY)) return true;
        // 系统输入范围是密码字段识别依据；读取锁失败时保守放行。
        bool password = false;
        HRESULT hr = Request(context, client_, TF_ES_SYNC | TF_ES_READ, [&](TfEditCookie cookie) {
            TF_SELECTION selection{}; ULONG count = 0;
            if (FAILED(context->GetSelection(cookie, TF_DEFAULT_SELECTION, 1, &selection, &count)) || count != 1) return E_FAIL;
            ComPtr<ITfRange> range; range.Attach(selection.range);
            ComPtr<ITfReadOnlyProperty> property;
            VARIANT value; VariantInit(&value);
            if (SUCCEEDED(context->GetAppProperty(GUID_PROP_INPUTSCOPE, &property)) && SUCCEEDED(property->GetValue(cookie, range.Get(), &value)) && value.vt == VT_UNKNOWN && value.punkVal) {
                ComPtr<ITfInputScope> scope;
                if (SUCCEEDED(value.punkVal->QueryInterface(IID_PPV_ARGS(&scope)))) {
                    InputScope* scopes = nullptr; UINT size = 0;
                    if (SUCCEEDED(scope->GetInputScopes(&scopes, &size))) {
                        for (UINT i = 0; i < size; ++i) if (scopes[i] == IS_PASSWORD) password = true;
                        CoTaskMemFree(scopes);
                    }
                }
            }
            VariantClear(&value);
            return S_OK;
        });
        return FAILED(hr) || password;
    }
    /** 规范化按键；快捷键、大小写输入和非单击 Shift 交还应用。 */
    std::string Key(WPARAM key) {
        if (GetKeyState(VK_CONTROL) < 0 || GetKeyState(VK_MENU) < 0 || GetKeyState(VK_LWIN) < 0 || GetKeyState(VK_RWIN) < 0) return {};
        if (key >= 'A' && key <= 'Z') {
            if ((GetKeyState(VK_SHIFT) < 0) != ((GetKeyState(VK_CAPITAL) & 1) != 0)) return {};
            return std::string(1, static_cast<char>(key - 'A' + 'a'));
        }
        if (GetKeyState(VK_SHIFT) < 0) return {};
        if (key >= '1' && key <= '9') return std::string(1, static_cast<char>(key));
        switch (key) {
        case VK_SPACE: return "Space"; case VK_RETURN: return "Enter";
        case VK_BACK: return "Backspace"; case VK_ESCAPE: return "Escape";
        case VK_UP: return "Up"; case VK_DOWN: return "Down";
        case VK_PRIOR: return "PageUp"; case VK_NEXT: return "PageDown";
        case VK_OEM_3: return "`"; case VK_OEM_7: return "'";
        default: return {};
        }
    }
    /** 在光标附近展示当前页；候选窗不获得焦点，限制在当前显示器工作区。 */
    void ShowCandidates(TfEditCookie cookie, ITfRange* range, const std::wstring& text) {
        if (text.empty()) { if (panel_) ShowWindow(panel_, SW_HIDE); return; }
        if (!panel_) {
            panel_ = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"STATIC", L"", WS_POPUP | WS_BORDER | SS_LEFT,
                0, 0, 520, 80, nullptr, nullptr, gModule, nullptr);
            font_ = CreateFontW(-18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Microsoft YaHei UI");
            if (panel_) SendMessageW(panel_, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
        }
        if (!panel_) return;
        ComPtr<ITfContextView> view; RECT caret{}; BOOL clipped = FALSE;
        if (FAILED(context_->GetActiveView(&view)) || FAILED(view->GetTextExt(cookie, range, &caret, &clipped))) { ShowWindow(panel_, SW_HIDE); return; }
        MONITORINFO monitor{sizeof(monitor)};
        GetMonitorInfoW(MonitorFromRect(&caret, MONITOR_DEFAULTTONEAREST), &monitor);
        const RECT& work = monitor.rcWork;
        int width = std::min(620L, work.right - work.left);
        int height = 148;
        int x = std::clamp(caret.left, work.left, work.right - width);
        int y = caret.bottom + 4;
        if (y + height > work.bottom) y = caret.top - height;
        y = std::clamp<LONG>(y, work.top, std::max(work.top, work.bottom - height));
        SetWindowTextW(panel_, text.c_str());
        SetWindowPos(panel_, HWND_TOPMOST, x, y, width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
    /** 在写锁内处理一次事件；文本写入成功后才向服务确认。 */
    HRESULT Process(TfEditCookie cookie, const std::string& key, BOOL* eaten) {
        std::vector<uint8_t> response;
        if (!pipe_.Key(2, key, response)) { Reset(cookie); return E_FAIL; }
        zhima::Reader reader(response); reader.Number(); reader.Number(8); reader.Number();
        bool consumed = reader.Number() != 0;
        auto preedit = reader.Text();
        size_t selected = static_cast<size_t>(reader.Number());
        auto page = reader.Number();
        auto count = reader.Number();
        if (count > 9) throw std::runtime_error("候选数量无效");
        std::wstring candidates;
        for (size_t i = 0; i < count; ++i) {
            auto text = reader.Text(), hint = reader.Text();
            candidates += (i == selected ? L"▶ " : L"   ") + std::to_wstring(i + 1) + L". " + text + L"  " + hint + L"\r\n";
        }
        auto commits = reader.Number();
        if (commits > 1) throw std::runtime_error("提交数量无效");
        uint64_t commitId = 0; std::wstring commit;
        if (commits) { commitId = reader.Number(8); commit = reader.Text(); }
        if (!reader.Done()) throw std::runtime_error("响应包含多余字段");
        if (!consumed) return S_OK;
        // 空组合上的模式切换不应覆盖应用当前选区。
        if (!composition_ && preedit.empty() && !commits) { *eaten = TRUE; return S_OK; }
        ComPtr<ITfRange> range;
        HRESULT hr = S_OK;
        if (composition_) hr = composition_->GetRange(&range);
        else {
            TF_SELECTION selection{}; ULONG fetched = 0;
            hr = context_->GetSelection(cookie, TF_DEFAULT_SELECTION, 1, &selection, &fetched);
            if (SUCCEEDED(hr) && fetched == 1) range.Attach(selection.range); else return E_FAIL;
        }
        if (FAILED(hr)) return hr;
        if (!composition_ && !preedit.empty()) {
            ComPtr<ITfContextComposition> contextComposition;
            hr = context_.As(&contextComposition);
            if (SUCCEEDED(hr)) hr = contextComposition->StartComposition(cookie, range.Get(), this, &composition_);
            if (FAILED(hr)) return hr;
        }
        const auto& replacement = commits ? commit : preedit;
        hr = range->SetText(cookie, 0, replacement.c_str(), static_cast<LONG>(replacement.size()));
        if (SUCCEEDED(hr)) *eaten = TRUE;
        if (SUCCEEDED(hr) && commits && composition_) {
            // 提交文本先脱离组合；后续确认失败也不能在清理时删除已上屏内容。
            auto composition = composition_; composition_.Reset();
            composition->EndComposition(cookie);
        }
        if (commits) pipe_.Ack(commitId, SUCCEEDED(hr));
        if (FAILED(hr)) { Reset(cookie); return hr; }
        // 已写入的事件必须消费，即使后续选区或显示更新失败也不能重复投递按键。
        if (preedit.empty() && composition_) {
            auto composition = composition_; composition_.Reset();
            composition->EndComposition(cookie);
        }
        ComPtr<ITfRange> cursor;
        if (SUCCEEDED(range->Clone(&cursor)) && SUCCEEDED(cursor->Collapse(cookie, TF_ANCHOR_END))) {
            TF_SELECTION selection{cursor.Get(), {TF_AE_NONE, FALSE}};
            context_->SetSelection(cookie, 1, &selection);
        }
        ShowCandidates(cookie, range.Get(), candidates.empty() ? L"" : preedit + L"  第 " + std::to_wstring(page + 1) + L" 页\r\n" + candidates);
        return S_OK;
    }
    /** 在任何试探和实际按键之前确认上下文身份及输入权限。 */
    bool Prepare(ITfContext* context) {
        if (!context || Disabled(context)) { Reset(); return false; }
        ComPtr<IUnknown> before, after;
        if (context_) context_.As(&before);
        context->QueryInterface(IID_PPV_ARGS(&after));
        if (before.Get() != after.Get()) { Reset(); context_ = context; }
        return true;
    }
public:
    /** 记录模块引用，避免被系统提前卸载。 */
    Service() { InterlockedIncrement(&gObjects); }
    /** 销毁原生窗口和字体。 */
    ~Service() { if (panel_) DestroyWindow(panel_); if (font_) DeleteObject(font_); InterlockedDecrement(&gObjects); }
    /** 所有接口共享 ITfTextInputProcessor 的 IUnknown 身份。 */
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override {
        if (!out) return E_POINTER; *out = nullptr;
        if (id == IID_IUnknown || id == IID_ITfTextInputProcessor) *out = static_cast<ITfTextInputProcessor*>(this);
        else if (id == IID_ITfKeyEventSink) *out = static_cast<ITfKeyEventSink*>(this);
        else if (id == IID_ITfThreadMgrEventSink) *out = static_cast<ITfThreadMgrEventSink*>(this);
        else if (id == IID_ITfCompositionSink) *out = static_cast<ITfCompositionSink*>(this);
        if (!*out) return E_NOINTERFACE; AddRef(); return S_OK;
    }
    /** 保留服务实例。 */
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs_); }
    /** 释放服务实例。 */
    ULONG STDMETHODCALLTYPE Release() override { auto refs = InterlockedDecrement(&refs_); if (!refs) delete this; return refs; }
    /** 订阅键盘与焦点事件，任一步失败均撤销已有订阅。 */
    HRESULT STDMETHODCALLTYPE Activate(ITfThreadMgr* manager, TfClientId client) override {
        if (!manager || manager_) return E_INVALIDARG;
        manager_ = manager; client_ = client;
        ComPtr<ITfKeystrokeMgr> keys; ComPtr<ITfSource> source;
        HRESULT hr = manager_.As(&keys);
        if (SUCCEEDED(hr)) hr = keys->AdviseKeyEventSink(client_, this, TRUE);
        if (SUCCEEDED(hr)) hr = manager_.As(&source);
        if (SUCCEEDED(hr)) hr = source->AdviseSink(IID_ITfThreadMgrEventSink, static_cast<ITfThreadMgrEventSink*>(this), &sink_);
        if (FAILED(hr)) Deactivate();
        return hr;
    }
    /** 取消所有订阅，释放线程管理器。 */
    HRESULT STDMETHODCALLTYPE Deactivate() override {
        Reset();
        if (manager_) {
            ComPtr<ITfKeystrokeMgr> keys; if (SUCCEEDED(manager_.As(&keys))) keys->UnadviseKeyEventSink(client_);
            ComPtr<ITfSource> source; if (sink_ != TF_INVALID_COOKIE && SUCCEEDED(manager_.As(&source))) source->UnadviseSink(sink_);
        }
        manager_.Reset(); sink_ = TF_INVALID_COOKIE; client_ = TF_CLIENTID_NULL; return S_OK;
    }
    /** 前台失焦时取消组合。 */
    HRESULT STDMETHODCALLTYPE OnSetFocus(BOOL foreground) override { if (!foreground) Reset(); return S_OK; }
    /** 试探只查询服务，不处理引擎事件。 */
    HRESULT STDMETHODCALLTYPE OnTestKeyDown(ITfContext* context, WPARAM key, LPARAM, BOOL* eaten) override {
        *eaten = FALSE;
        try {
            if (!Prepare(context)) return S_OK;
            if (key == VK_SHIFT) { shiftOnly_ = true; return S_OK; }
            shiftOnly_ = false;
            auto normalized = Key(key); if (normalized.empty()) return S_OK;
            std::vector<uint8_t> response;
            if (pipe_.Key(1, normalized, response)) {
                zhima::Reader reader(response); reader.Number(); reader.Number(8); reader.Number(); *eaten = reader.Number() ? TRUE : FALSE;
            } else Reset();
        } catch (...) { Reset(); }
        return S_OK;
    }
    /** Shift 抬起仅在单击时由本输入法消费。 */
    HRESULT STDMETHODCALLTYPE OnTestKeyUp(ITfContext*, WPARAM key, LPARAM, BOOL* eaten) override { *eaten = key == VK_SHIFT && shiftOnly_; return S_OK; }
    /** 在同步写锁内完成事件与上屏，锁失败不改变引擎状态。 */
    HRESULT STDMETHODCALLTYPE OnKeyDown(ITfContext* context, WPARAM key, LPARAM, BOOL* eaten) override {
        *eaten = FALSE;
        try {
            if (!Prepare(context)) return S_OK;
            if (key == VK_SHIFT) { shiftOnly_ = true; return S_OK; }
            shiftOnly_ = false;
            auto normalized = Key(key); if (normalized.empty()) return S_OK;
            HRESULT hr = Request(context, client_, TF_ES_SYNC | TF_ES_READWRITE, [&](TfEditCookie cookie) {
                try { return Process(cookie, normalized, eaten); }
                catch (...) { Reset(cookie); return E_FAIL; }
            });
            if (FAILED(hr)) Reset();
        } catch (...) { Reset(); }
        return S_OK;
    }
    /** 单击 Shift 切换中英文；组合按键不切换。 */
    HRESULT STDMETHODCALLTYPE OnKeyUp(ITfContext* context, WPARAM key, LPARAM, BOOL* eaten) override {
        *eaten = FALSE;
        if (key == VK_SHIFT && shiftOnly_) {
            shiftOnly_ = false;
            try {
                if (Prepare(context)) {
                    HRESULT hr = Request(context, client_, TF_ES_SYNC | TF_ES_READWRITE, [&](TfEditCookie cookie) {
                        try { return Process(cookie, "Shift", eaten); }
                        catch (...) { Reset(cookie); return E_FAIL; }
                    });
                    if (FAILED(hr)) Reset();
                }
            } catch (...) { Reset(); }
        }
        return S_OK;
    }
    /** 未注册保留快捷键。 */
    HRESULT STDMETHODCALLTYPE OnPreservedKey(ITfContext*, REFGUID, BOOL* eaten) override { *eaten = FALSE; return S_OK; }
    /** 文档创建无需初始化输入状态。 */
    HRESULT STDMETHODCALLTYPE OnInitDocumentMgr(ITfDocumentMgr*) override { return S_OK; }
    /** 文档销毁时保守取消当前组合。 */
    HRESULT STDMETHODCALLTYPE OnUninitDocumentMgr(ITfDocumentMgr*) override { Reset(); return S_OK; }
    /** 焦点文档变化立即隔离会话。 */
    HRESULT STDMETHODCALLTYPE OnSetFocus(ITfDocumentMgr*, ITfDocumentMgr*) override { Reset(); return S_OK; }
    /** 弹窗或新上下文推入时取消旧组合。 */
    HRESULT STDMETHODCALLTYPE OnPushContext(ITfContext*) override { Reset(); return S_OK; }
    /** 上下文退出时取消旧组合。 */
    HRESULT STDMETHODCALLTYPE OnPopContext(ITfContext*) override { Reset(); return S_OK; }
    /** 应用强制终止组合时清除预编辑，不把旧编码带入下一输入框。 */
    HRESULT STDMETHODCALLTYPE OnCompositionTerminated(TfEditCookie cookie, ITfComposition* composition) override {
        if (composition_.Get() == composition) {
            ComPtr<ITfRange> range;
            if (SUCCEEDED(composition->GetRange(&range))) range->SetText(cookie, 0, L"", 0);
            composition_.Reset(); pipe_.Close(); if (panel_) ShowWindow(panel_, SW_HIDE);
        }
        return S_OK;
    }
};

/** 标准 COM 工厂，仅创建非聚合 TSF 实例。 */
class Factory final : public IClassFactory {
    LONG refs_ = 1;
public:
    /** 工厂存活期间保留模块。 */
    Factory() { InterlockedIncrement(&gObjects); }
    /** 工厂释放后允许模块卸载。 */
    ~Factory() { InterlockedDecrement(&gObjects); }
    /** 提供工厂接口。 */
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override {
        if (!out) return E_POINTER; *out = nullptr;
        if (id != IID_IUnknown && id != IID_IClassFactory) return E_NOINTERFACE;
        *out = static_cast<IClassFactory*>(this); AddRef(); return S_OK;
    }
    /** 保留工厂。 */
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs_); }
    /** 释放工厂。 */
    ULONG STDMETHODCALLTYPE Release() override { auto refs = InterlockedDecrement(&refs_); if (!refs) delete this; return refs; }
    /** 创建实例并返回所需接口。 */
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer, REFIID id, void** out) override {
        if (!out) return E_POINTER; *out = nullptr;
        if (outer) return CLASS_E_NOAGGREGATION;
        auto service = new(std::nothrow) Service(); if (!service) return E_OUTOFMEMORY;
        HRESULT hr = service->QueryInterface(id, out); service->Release(); return hr;
    }
    /** 支持 COM 模块锁。 */
    HRESULT STDMETHODCALLTYPE LockServer(BOOL lock) override { if (lock) InterlockedIncrement(&gObjects); else InterlockedDecrement(&gObjects); return S_OK; }
};
/** DLL 装载时只记录模块地址，避免在加载锁内启动服务。 */
BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) { if (reason == DLL_PROCESS_ATTACH) gModule = module; return TRUE; }
/** 仅在所有对象和锁释放后允许卸载。 */
extern "C" HRESULT __stdcall DllCanUnloadNow() { return InterlockedCompareExchange(&gObjects, 0, 0) == 0 ? S_OK : S_FALSE; }
/** 向系统提供本输入法的工厂。 */
extern "C" HRESULT __stdcall DllGetClassObject(REFCLSID clsid, REFIID id, void** out) {
    if (!out) return E_POINTER; *out = nullptr;
    if (clsid != kService) return CLASS_E_CLASSNOTAVAILABLE;
    auto factory = new(std::nothrow) Factory(); if (!factory) return E_OUTOFMEMORY;
    HRESULT hr = factory->QueryInterface(id, out); factory->Release(); return hr;
}
/** 将 COM DLL 路径写入对应架构的系统注册表，安装需要管理员权限。 */
HRESULT RegisterCom() {
    wchar_t path[MAX_PATH]{};
    DWORD length = GetModuleFileNameW(gModule, path, MAX_PATH);
    if (!length || length == MAX_PATH) return E_FAIL;
    std::wstring key = std::wstring(L"Software\\Classes\\CLSID\\") + kClsid + L"\\InprocServer32";
    HKEY registry = nullptr;
    auto status = RegCreateKeyExW(HKEY_LOCAL_MACHINE, key.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &registry, nullptr);
    if (status != ERROR_SUCCESS) return HRESULT_FROM_WIN32(status);
    status = RegSetValueExW(registry, nullptr, 0, REG_SZ, reinterpret_cast<const BYTE*>(path), (length + 1) * sizeof(wchar_t));
    if (status == ERROR_SUCCESS) status = RegSetValueExW(registry, L"ThreadingModel", 0, REG_SZ, reinterpret_cast<const BYTE*>(L"Apartment"), sizeof(L"Apartment"));
    RegCloseKey(registry); return HRESULT_FROM_WIN32(status);
}
/** 撤销语言配置、类别及对应架构的系统 COM 注册，不删除用户词库。 */
extern "C" HRESULT __stdcall DllUnregisterServer() {
    HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ComPtr<ITfInputProcessorProfiles> profiles;
    HRESULT hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&profiles));
    // 安装器预检、卸载日志和失败回滚可能重复注销；先用 TSF 枚举确认是否仍有注册。
    ComPtr<IEnumGUID> processors;
    if (SUCCEEDED(hr)) hr = profiles->EnumInputProcessorInfo(&processors);
    if (SUCCEEDED(hr)) {
        GUID processor{}; ULONG fetched = 0;
        while ((hr = processors->Next(1, &processor, &fetched)) == S_OK && fetched == 1) {
            if (processor == kService) { hr = profiles->Unregister(kService); break; }
        }
    }
    // TSF 注销失败时保留 COM 路径和 DLL，供卸载器重试，避免产生无法恢复的残留。
    if (FAILED(hr)) { processors.Reset(); profiles.Reset(); if (SUCCEEDED(initialized)) CoUninitialize(); return hr; }
    std::wstring key = std::wstring(L"Software\\Classes\\CLSID\\") + kClsid;
    LSTATUS status = RegDeleteTreeW(HKEY_LOCAL_MACHINE, key.c_str());
    processors.Reset(); profiles.Reset(); if (SUCCEEDED(initialized)) CoUninitialize();
    return status == ERROR_FILE_NOT_FOUND ? S_OK : HRESULT_FROM_WIN32(status);
}
/** 注册简体中文键盘服务；仅开发安装脚本显式调用，构建不会调用。 */
extern "C" HRESULT __stdcall DllRegisterServer() {
    HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    HRESULT hr = RegisterCom();
    ComPtr<ITfInputProcessorProfiles> profiles; ComPtr<ITfCategoryMgr> categories;
    if (SUCCEEDED(hr)) hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&profiles));
    if (SUCCEEDED(hr)) hr = profiles->Register(kService);
    const wchar_t description[] = L"芝麻输入法（开发版）";
    if (SUCCEEDED(hr)) hr = profiles->AddLanguageProfile(kService, kLanguage, kProfile, description, ARRAYSIZE(description) - 1, nullptr, 0, 0);
    if (SUCCEEDED(hr)) hr = CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&categories));
    if (SUCCEEDED(hr)) hr = categories->RegisterCategory(kService, GUID_TFCAT_TIP_KEYBOARD, kService);
    if (SUCCEEDED(hr)) hr = profiles->EnableLanguageProfile(kService, kLanguage, kProfile, TRUE);
    profiles.Reset(); categories.Reset();
    if (SUCCEEDED(initialized)) CoUninitialize();
    if (FAILED(hr)) DllUnregisterServer();
    return hr;
}
