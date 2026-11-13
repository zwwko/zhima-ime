// 安装器隔离验收用模拟注册组件，不包含于交付给用户的安装包。
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
HMODULE gFixture = nullptr;
/** 定位模拟 DLL 所在目录，所有记录仅写入测试安装树。 */
std::wstring ModuleDirectory() {
    wchar_t path[32768]{};
    DWORD length = GetModuleFileNameW(gFixture, path, ARRAYSIZE(path));
    if (!length || length == ARRAYSIZE(path)) return {};
    std::wstring value(path, length);
    return value.substr(0, value.find_last_of(L'\\'));
}
/** 在隔离目录记录操作，便于独立核查两种位数都参与回滚与卸载。 */
void Record(bool registration) {
    auto root = ModuleDirectory();
    root = root.substr(0, root.find_last_of(L'\\'));
    root = root.substr(0, root.find_last_of(L'\\'));
    HANDLE log = CreateFileW((root + L"\\registration-events.log").c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, 0, nullptr);
    if (log == INVALID_HANDLE_VALUE) return;
#ifdef _WIN64
    const char* message = registration ? "注册64\n" : "注销64\n";
#else
    const char* message = registration ? "注册32\n" : "注销32\n";
#endif
    DWORD written; WriteFile(log, message, static_cast<DWORD>(strlen(message)), &written, nullptr); CloseHandle(log);
}
/** 故障仅由测试进程环境变量注入，不接触真实 TSF 或注册表。 */
bool InjectFailure(bool registration) {
    wchar_t value[2]{};
#ifdef _WIN64
    const wchar_t* name = registration ? L"ZHIMA_TEST_FAIL_REGISTER_X64" : L"ZHIMA_TEST_FAIL_UNREGISTER_X64";
#else
    const wchar_t* name = registration ? L"ZHIMA_TEST_FAIL_REGISTER_X86" : L"ZHIMA_TEST_FAIL_UNREGISTER_X86";
#endif
    return GetEnvironmentVariableW(name, value, ARRAYSIZE(value)) > 0;
}
/** 注册仅创建标记文件，供安装事务测试。 */
extern "C" HRESULT __stdcall DllRegisterServer() {
    Record(true);
    if (InjectFailure(true)) return E_FAIL;
    HANDLE flag = CreateFileW((ModuleDirectory() + L"\\registered.flag").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (flag == INVALID_HANDLE_VALUE) return E_FAIL;
    CloseHandle(flag); return S_OK;
}
/** 注销仅移除测试标记，可注入失败以验证保留文件的行为。 */
extern "C" HRESULT __stdcall DllUnregisterServer() {
    Record(false);
    if (InjectFailure(false)) return E_FAIL;
    auto path = ModuleDirectory() + L"\\registered.flag";
    if (!DeleteFileW(path.c_str()) && GetLastError() != ERROR_FILE_NOT_FOUND) return E_FAIL;
    return S_OK;
}
/** 只记录模块路径，不执行任何系统注册。 */
BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) { if (reason == DLL_PROCESS_ATTACH) gFixture = module; return TRUE; }
