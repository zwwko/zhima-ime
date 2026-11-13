//! Windows 当前用户引擎进程；其他平台仍可编译并验证协议。
#![cfg_attr(windows, windows_subsystem = "windows")]
#[cfg(windows)]
mod windows;

/// 启动平台服务；错误不包含用户输入正文。
fn main() {
    #[cfg(windows)]
    if let Err(error) = windows::run() {
        eprintln!("芝麻输入法服务启动失败：{error}");
        std::process::exit(1);
    }
    #[cfg(not(windows))]
    {
        eprintln!("引擎服务仅支持 Windows");
        std::process::exit(1);
    }
}
