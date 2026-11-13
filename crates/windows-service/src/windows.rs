//! 通过受当前用户 SID 限制的本地命名管道提供独立会话。
use ime_core::{Config, Engine, Session};
use ime_dictionary::Dictionary;
use ime_windows_service::{dispatch, read_frame, write_frame};
use std::{ffi::c_void, fs::File, io, os::windows::io::FromRawHandle, ptr, sync::Arc};

type Handle = *mut c_void;
const PIPE_ACCESS_DUPLEX: u32 = 3;
const FILE_FLAG_FIRST_PIPE_INSTANCE: u32 = 0x80000;
const PIPE_REJECT_REMOTE_CLIENTS: u32 = 8;
const MAX_INSTANCES: u32 = 64;
const ERROR_PIPE_CONNECTED: i32 = 535;
const ERROR_PIPE_BUSY: i32 = 231;
/// Win32 安全属性；安全描述符由系统分配并由调用方释放。
#[repr(C)]
struct SecurityAttributes {
    length: u32,
    descriptor: Handle,
    inherit: i32,
}
#[link(name = "kernel32")]
extern "system" {
    fn GetCurrentProcess() -> Handle;
    fn CloseHandle(handle: Handle) -> i32;
    fn LocalFree(memory: Handle) -> Handle;
    fn CreateNamedPipeW(
        name: *const u16,
        access: u32,
        mode: u32,
        instances: u32,
        output: u32,
        input: u32,
        timeout: u32,
        security: *const SecurityAttributes,
    ) -> Handle;
    fn ConnectNamedPipe(pipe: Handle, overlapped: Handle) -> i32;
}
#[link(name = "advapi32")]
extern "system" {
    fn OpenProcessToken(process: Handle, access: u32, token: *mut Handle) -> i32;
    fn GetTokenInformation(
        token: Handle,
        class: u32,
        info: Handle,
        size: u32,
        needed: *mut u32,
    ) -> i32;
    fn ConvertSidToStringSidW(sid: Handle, output: *mut *mut u16) -> i32;
    fn ConvertStringSecurityDescriptorToSecurityDescriptorW(
        text: *const u16,
        revision: u32,
        output: *mut Handle,
        size: *mut u32,
    ) -> i32;
}
/// 将系统字符串转换为以零结尾的 UTF-16。
fn wide(text: &str) -> Vec<u16> {
    text.encode_utf16().chain(Some(0)).collect()
}
/// 获取令牌 SID，使用指针对齐缓冲区避免 TOKEN_USER 非对齐访问。
fn user_sid() -> io::Result<String> {
    unsafe {
        let mut token = ptr::null_mut();
        if OpenProcessToken(GetCurrentProcess(), 8, &mut token) == 0 {
            return Err(io::Error::last_os_error());
        }
        let mut needed = 0;
        GetTokenInformation(token, 1, ptr::null_mut(), 0, &mut needed);
        let mut buffer = vec![0usize; (needed as usize).div_ceil(size_of::<usize>())];
        let ok = GetTokenInformation(token, 1, buffer.as_mut_ptr().cast(), needed, &mut needed);
        let error = io::Error::last_os_error();
        CloseHandle(token);
        if ok == 0 {
            return Err(error);
        }
        let mut sid = ptr::null_mut();
        if ConvertSidToStringSidW(*(buffer.as_ptr().cast::<Handle>()), &mut sid) == 0 {
            return Err(io::Error::last_os_error());
        }
        let mut length = 0;
        while *sid.add(length) != 0 {
            length += 1;
        }
        let text = String::from_utf16_lossy(std::slice::from_raw_parts(sid, length));
        LocalFree(sid.cast());
        Ok(text)
    }
}
/// 为每个已连接前端保持独立状态；断线直接丢弃组合和待确认提交。
fn serve(mut pipe: File, engine: Arc<Engine>) {
    let mut session = Session::new(Config::default());
    let mut sequence = 0;
    while let Ok(frame) = read_frame(&mut pipe) {
        let Ok(response) = dispatch(&frame, &mut sequence, &mut session, &engine) else {
            break;
        };
        if write_frame(&mut pipe, &response).is_err() {
            break;
        }
    }
}
/// 加载随包词典并监听；首实例防抢占，拒绝远程及其他用户访问。
pub fn run() -> io::Result<()> {
    let root = std::env::current_exe()?
        .parent()
        .ok_or_else(|| io::Error::other("无法定位程序目录"))?
        .to_path_buf();
    let load = |name: &str| -> io::Result<Dictionary> {
        Dictionary::parse(&std::fs::read_to_string(root.join(name))?).map_err(io::Error::other)
    };
    let engine = Arc::new(Engine {
        wubi: load("wubi.tsv")?,
        pinyin: load("pinyin.tsv")?,
    });
    let sid = user_sid()?;
    let name = wide(&format!(r"\\.\pipe\ZhimaIME-v1-{sid}"));
    let acl = wide(&format!("D:P(A;;GA;;;{sid})"));
    unsafe {
        let mut descriptor = ptr::null_mut();
        if ConvertStringSecurityDescriptorToSecurityDescriptorW(
            acl.as_ptr(),
            1,
            &mut descriptor,
            ptr::null_mut(),
        ) == 0
        {
            return Err(io::Error::last_os_error());
        }
        let security = SecurityAttributes {
            length: size_of::<SecurityAttributes>() as u32,
            descriptor,
            inherit: 0,
        };
        // 首实例防止同名抢占；每次先建立下一监听实例，再移交已连接实例。
        let mut raw = CreateNamedPipeW(
            name.as_ptr(),
            PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
            PIPE_REJECT_REMOTE_CLIENTS,
            MAX_INSTANCES,
            65536,
            65536,
            0,
            &security,
        );
        if raw as isize == -1 {
            LocalFree(descriptor);
            return Err(io::Error::last_os_error());
        }
        loop {
            let pipe = File::from_raw_handle(raw);
            if ConnectNamedPipe(raw, ptr::null_mut()) == 0
                && io::Error::last_os_error().raw_os_error() != Some(ERROR_PIPE_CONNECTED)
            {
                LocalFree(descriptor);
                return Err(io::Error::last_os_error());
            }
            // 达到实例上限时等待旧会话断开；不为过载创建无界线程，也不终止已有会话。
            let next = loop {
                let candidate = CreateNamedPipeW(
                    name.as_ptr(),
                    PIPE_ACCESS_DUPLEX,
                    PIPE_REJECT_REMOTE_CLIENTS,
                    MAX_INSTANCES,
                    65536,
                    65536,
                    0,
                    &security,
                );
                if candidate as isize != -1 {
                    break candidate;
                }
                let error = io::Error::last_os_error();
                if error.raw_os_error() != Some(ERROR_PIPE_BUSY) {
                    LocalFree(descriptor);
                    return Err(error);
                }
                std::thread::sleep(std::time::Duration::from_millis(10));
            };
            let shared = Arc::clone(&engine);
            std::thread::Builder::new()
                .name("芝麻输入会话".into())
                .spawn(move || serve(pipe, shared))?;
            raw = next;
        }
    }
}
