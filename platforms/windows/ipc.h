#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <sddl.h>
#include <cstdint>
#include <string>
#include <vector>
#include <stdexcept>

namespace zhima {
constexpr uint32_t kVersion = 1, kMaxFrame = 65536;
/** 根据当前进程用户令牌生成管道名，不使用可伪造的环境变量。 */
inline std::wstring PipeName() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return {};
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    std::vector<uintptr_t> storage((size + sizeof(uintptr_t) - 1) / sizeof(uintptr_t));
    bool ok = GetTokenInformation(token, TokenUser, storage.data(), size, &size) != FALSE;
    CloseHandle(token);
    LPWSTR sid = nullptr;
    if (!ok || !ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(storage.data())->User.Sid, &sid)) return {};
    std::wstring name = L"\\\\.\\pipe\\ZhimaIME-v1-";
    name += sid;
    LocalFree(sid);
    return name;
}
/** 使用显式小端字段写入，兼容 x86 与 x64。 */
inline void Put(std::vector<uint8_t>& bytes, uint64_t value, size_t count = 4) {
    for (size_t i = 0; i < count; ++i) bytes.push_back(static_cast<uint8_t>(value >> (i * 8)));
}
/** 有界响应读取器；拒绝越界、非法 UTF-8 和多余数据。 */
class Reader {
    const std::vector<uint8_t>& bytes_;
    size_t position_ = 0;
public:
    /** 引用单个响应帧，生命周期由调用方管理。 */
    explicit Reader(const std::vector<uint8_t>& bytes) : bytes_(bytes) {}
    /** 读取协议整数并验证剩余长度。 */
    uint64_t Number(size_t count = 4) {
        if (count > bytes_.size() - position_) throw std::runtime_error("输入法响应被截断");
        uint64_t value = 0;
        for (size_t i = 0; i < count; ++i) value |= uint64_t(bytes_[position_++]) << (8 * i);
        return value;
    }
    /** 将 UTF-8 文本转换为 Windows UTF-16，拒绝非法编码。 */
    std::wstring Text() {
        size_t size = static_cast<size_t>(Number());
        if (size > bytes_.size() - position_) throw std::runtime_error("输入法文本越界");
        if (!size) return {};
        auto text = reinterpret_cast<const char*>(bytes_.data() + position_);
        int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, static_cast<int>(size), nullptr, 0);
        if (!length) throw std::runtime_error("输入法文本编码无效");
        std::wstring result(length, L'\0');
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, static_cast<int>(size), result.data(), length);
        position_ += size;
        return result;
    }
    /** 确认帧被完整解析。 */
    bool Done() const { return position_ == bytes_.size(); }
};
/** 每个前端上下文独占一个连接；超时关闭，不重放提交请求。 */
class Pipe {
    HANDLE handle_ = INVALID_HANDLE_VALUE;
    uint64_t sequence_ = 0;
    ULONGLONG retryAfter_ = 0;
    /** 对每次读写设置上限，取消后等待完成再释放 OVERLAPPED。 */
    bool Transfer(bool write, void* bytes, DWORD size, ULONGLONG deadline) {
        auto cursor = static_cast<uint8_t*>(bytes);
        while (size) {
            if (GetTickCount64() >= deadline) return false;
            OVERLAPPED operation{};
            operation.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            if (!operation.hEvent) return false;
            DWORD done = 0;
            BOOL ok = write ? WriteFile(handle_, cursor, size, &done, &operation) : ReadFile(handle_, cursor, size, &done, &operation);
            if (!ok && GetLastError() == ERROR_IO_PENDING) {
                auto now = GetTickCount64();
                DWORD timeout = now < deadline ? static_cast<DWORD>(deadline - now) : 0;
                if (WaitForSingleObject(operation.hEvent, timeout) != WAIT_OBJECT_0) CancelIoEx(handle_, &operation);
                ok = GetOverlappedResult(handle_, &operation, &done, TRUE);
            }
            CloseHandle(operation.hEvent);
            if (!ok || done == 0) return false;
            cursor += done;
            size -= done;
        }
        return true;
    }
public:
    /** 禁止复制管道句柄。 */
    Pipe() = default;
    Pipe(const Pipe&) = delete;
    Pipe& operator=(const Pipe&) = delete;
    /** 断开连接即销毁远端会话。 */
    ~Pipe() { Close(); }
    /** 立即清除连接和序号。 */
    void Close() { if (handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_); handle_ = INVALID_HANDLE_VALUE; sequence_ = 0; }
    /** 测试与事件都使用同一会话；失败后短暂退避，防止反复阻塞应用。 */
    bool Request(uint32_t operation, const std::vector<uint8_t>& body, std::vector<uint8_t>& response) {
        if (handle_ == INVALID_HANDLE_VALUE) {
            if (GetTickCount64() < retryAfter_) return false;
            auto name = PipeName();
            if (name.empty()) return false;
            handle_ = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr);
            if (handle_ == INVALID_HANDLE_VALUE) { retryAfter_ = GetTickCount64() + 1000; return false; }
        }
        std::vector<uint8_t> frame;
        Put(frame, kVersion); Put(frame, ++sequence_, 8); Put(frame, operation);
        frame.insert(frame.end(), body.begin(), body.end());
        std::vector<uint8_t> packet; Put(packet, frame.size()); packet.insert(packet.end(), frame.begin(), frame.end());
        ULONGLONG deadline = GetTickCount64() + 100;
        uint8_t length[4]{};
        bool ok = Transfer(true, packet.data(), static_cast<DWORD>(packet.size()), deadline) && Transfer(false, length, 4, deadline);
        uint32_t size = uint32_t(length[0]) | uint32_t(length[1]) << 8 | uint32_t(length[2]) << 16 | uint32_t(length[3]) << 24;
        ok = ok && size >= 16 && size <= kMaxFrame;
        if (ok) { response.resize(size); ok = Transfer(false, response.data(), size, deadline); }
        if (ok) {
            Reader reader(response);
            ok = reader.Number() == kVersion && reader.Number(8) == sequence_ && reader.Number() == operation;
        }
        if (!ok) { Close(); retryAfter_ = GetTickCount64() + 1000; }
        return ok;
    }
    /** 规范化按键内容不包含日志输出。 */
    bool Key(uint32_t operation, const std::string& key, std::vector<uint8_t>& response) {
        return Request(operation, std::vector<uint8_t>(key.begin(), key.end()), response);
    }
    /** 上屏结果仅确认一次，通信失败也不自动重发。 */
    void Ack(uint64_t id, bool success) {
        std::vector<uint8_t> body, response; Put(body, id, 8); Put(body, success ? 1 : 0);
        Request(3, body, response);
    }
};
}
