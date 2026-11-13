// 安装维护工具：只处理本程序目录的引擎，以及芝麻输入法自己的用户配置。
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#include <shellapi.h>
#include <string>
#include <vector>

constexpr wchar_t kEngine[] = L"ime-windows-service.exe";
constexpr wchar_t kProfileList[] = L"0804:{63F94BC2-23A1-47FA-A267-48688242F6A9}{C7A1C3B0-78AD-4563-91C4-69B56326A913}";
constexpr DWORD kUninstallProfile = 1;

/** 获取当前维护工具所在目录，拒绝路径截断。 */
std::wstring InstallDirectory() {
    std::vector<wchar_t> buffer(32768);
    DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (!length || length >= buffer.size()) return {};
    std::wstring path(buffer.data(), length);
    auto separator = path.find_last_of(L"\\/");
    return separator == std::wstring::npos ? L"" : path.substr(0, separator);
}

/** 比较完整路径；不得按进程名终止其他目录中的同名引擎。 */
bool SamePath(const std::wstring& first, const std::wstring& second) {
    return CompareStringOrdinal(first.c_str(), -1, second.c_str(), -1, TRUE) == CSTR_EQUAL;
}

/** 停止安装目录中的引擎并等待退出；任何无法确认的目标均不终止。 */
bool StopEngine() {
    auto root = InstallDirectory();
    if (root.empty()) return false;
    auto expected = root + L"\\" + kEngine;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W entry{}; entry.dwSize = sizeof(entry);
    bool success = true;
    for (BOOL found = Process32FirstW(snapshot, &entry); found; found = Process32NextW(snapshot, &entry)) {
        if (_wcsicmp(entry.szExeFile, kEngine) != 0) continue;
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE | SYNCHRONIZE, FALSE, entry.th32ProcessID);
        if (!process) { if (GetLastError() != ERROR_INVALID_PARAMETER) success = false; continue; }
        std::vector<wchar_t> path(32768); DWORD size = static_cast<DWORD>(path.size());
        if (!QueryFullProcessImageNameW(process, 0, path.data(), &size)) success = false;
        else if (SamePath(std::wstring(path.data(), size), expected)) {
            // 未确认提交不会重放；服务进程退出后前端按既有故障策略直通。
            if (!TerminateProcess(process, 0) || WaitForSingleObject(process, 5000) != WAIT_OBJECT_0) success = false;
        }
        CloseHandle(process);
    }
    CloseHandle(snapshot);
    return success;
}

/** 只增删本输入法的启用项，不使用设置默认或清空其他输入法的标志。 */
bool ConfigureProfile(bool enable) {
    HMODULE input = LoadLibraryExW(L"input.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!input) return false;
    using InstallProfile = BOOL (WINAPI*)(LPCWSTR, DWORD);
    auto install = reinterpret_cast<InstallProfile>(GetProcAddress(input, "InstallLayoutOrTip"));
    bool success = install && install(kProfileList, enable ? 0 : kUninstallProfile) != FALSE;
    FreeLibrary(input);
    return success;
}

/** 无界面维护入口供安装器等待结果；交互启用失败时展示中文处理建议。 */
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    int count = 0;
    wchar_t** args = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!args) return 2;
    int result = 2;
    try {
        const std::wstring command = count == 2 ? args[1] : L"";
        if (command == L"--stop-engine") result = StopEngine() ? 0 : 1;
        else if (command == L"--enable-profile" || command == L"--disable-profile") {
            result = ConfigureProfile(command == L"--enable-profile") ? 0 : 1;
            if (result && command == L"--enable-profile") MessageBoxW(nullptr, L"未能启用芝麻输入法，请注销后重新登录，并在 Windows 语言设置中添加芝麻输入法。", L"芝麻输入法", MB_OK | MB_ICONWARNING);
        } else if (command == L"--self-test") {
            // 无系统修改的验证入口：路径比较必须拒绝前缀目录和其他文件。
            result = !InstallDirectory().empty() && SamePath(L"C:\\芝麻\\引擎.exe", L"c:\\芝麻\\引擎.exe")
                && !SamePath(L"C:\\芝麻\\引擎.exe", L"C:\\芝麻备份\\引擎.exe") ? 0 : 1;
        }
    } catch (...) { OutputDebugStringW(L"芝麻输入法：维护操作失败，请查看安装或卸载日志。\n"); result = 1; }
    LocalFree(args);
    return result;
}
