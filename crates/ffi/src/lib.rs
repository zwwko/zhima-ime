//! C ABI 边界：句柄归调用者独占；输出字符串必须使用 zf_string_free 释放。
use ime_core::{Config, Engine, Session};
use ime_dictionary::Dictionary;
use std::{
    ffi::{c_char, CStr, CString},
    panic::{catch_unwind, AssertUnwindSafe},
};
/// 同一输入上下文的引擎与组合状态，不支持并发调用。
pub struct Handle {
    engine: Engine,
    session: Session,
}
/// 从 UTF-8 TSV 文件创建句柄；任意失败返回空指针。
/// # Safety
/// 两个路径必须是有效且以零结尾的字符串。
#[no_mangle]
pub unsafe extern "C" fn zf_create(wubi: *const c_char, pinyin: *const c_char) -> *mut Handle {
    catch_unwind(|| {
        let read = |ptr: *const c_char| -> Option<Dictionary> {
            if ptr.is_null() {
                return None;
            }
            let path = CStr::from_ptr(ptr).to_str().ok()?;
            Dictionary::parse(&std::fs::read_to_string(path).ok()?).ok()
        };
        let Some(wubi) = read(wubi) else {
            return std::ptr::null_mut();
        };
        let Some(pinyin) = read(pinyin) else {
            return std::ptr::null_mut();
        };
        Box::into_raw(Box::new(Handle {
            engine: Engine { wubi, pinyin },
            session: Session::new(Config::default()),
        }))
    })
    .unwrap_or(std::ptr::null_mut())
}
/// 释放句柄；空指针可安全传入。
/// # Safety
/// 句柄必须来自 zf_create，且只能释放一次。
#[no_mangle]
pub unsafe extern "C" fn zf_destroy(h: *mut Handle) {
    if !h.is_null() {
        drop(Box::from_raw(h));
    }
}
/// 判断标准化按键是否处理；不修改会话。
/// # Safety
/// 句柄及字符串必须有效，调用期间不得释放或并发访问。
#[no_mangle]
pub unsafe extern "C" fn zf_test_key(h: *const Handle, key: *const c_char) -> bool {
    catch_unwind(|| {
        !h.is_null()
            && !key.is_null()
            && CStr::from_ptr(key)
                .to_str()
                .is_ok_and(|k| (*h).session.test_key(k))
    })
    .unwrap_or(false)
}
/// 处理事件并返回 JSON 快照；错误返回空指针。
/// # Safety
/// 句柄必须独占有效，key 必须是以零结尾的 UTF-8。
#[no_mangle]
pub unsafe extern "C" fn zf_process(h: *mut Handle, key: *const c_char) -> *mut c_char {
    catch_unwind(AssertUnwindSafe(|| {
        if h.is_null() || key.is_null() {
            return std::ptr::null_mut();
        }
        let Ok(key) = CStr::from_ptr(key).to_str() else {
            return std::ptr::null_mut();
        };
        let h = &mut *h;
        let out = h.session.process_event(&h.engine, key);
        serde_json::to_string(&out)
            .ok()
            .and_then(|s| CString::new(s).ok())
            .map_or(std::ptr::null_mut(), CString::into_raw)
    }))
    .unwrap_or(std::ptr::null_mut())
}
/// 确认文本已成功提交；重复确认不产生额外学习。
/// # Safety
/// 句柄必须独占有效。
#[no_mangle]
pub unsafe extern "C" fn zf_ack_commit(h: *mut Handle, id: u64, success: bool) {
    let _ = catch_unwind(AssertUnwindSafe(|| {
        if let Some(h) = h.as_mut() {
            h.session.ack_commit(id, success);
        }
    }));
}
/// 释放由本库分配的 JSON 字符串。
/// # Safety
/// 指针必须由 zf_process 返回，且仅释放一次。
#[no_mangle]
pub unsafe extern "C" fn zf_string_free(s: *mut c_char) {
    if !s.is_null() {
        drop(CString::from_raw(s));
    }
}
