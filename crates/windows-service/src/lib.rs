//! Windows 前端与引擎服务之间的有界、小端二进制协议。
use ime_core::{Engine, Output, Session};
use std::io::{self, Read, Write};

pub const VERSION: u32 = 1;
pub const MAX_FRAME: usize = 65536;
pub const TEST: u32 = 1;
pub const PROCESS: u32 = 2;
pub const ACK: u32 = 3;

/// 读取长度前缀帧；拒绝空帧、超长帧和截断输入。
pub fn read_frame(reader: &mut impl Read) -> io::Result<Vec<u8>> {
    let mut size = [0; 4];
    reader.read_exact(&mut size)?;
    let size = u32::from_le_bytes(size) as usize;
    if !(16..=MAX_FRAME).contains(&size) {
        return Err(invalid());
    }
    let mut frame = vec![0; size];
    reader.read_exact(&mut frame)?;
    Ok(frame)
}
/// 写入完整帧，不重试任何业务请求。
pub fn write_frame(writer: &mut impl Write, frame: &[u8]) -> io::Result<()> {
    if !(16..=MAX_FRAME).contains(&frame.len()) {
        return Err(invalid());
    }
    writer.write_all(&(frame.len() as u32).to_le_bytes())?;
    writer.write_all(frame)
}
/// 将协议输入错误统一为中文，避免泄漏输入正文。
fn invalid() -> io::Error {
    io::Error::new(io::ErrorKind::InvalidData, "输入法通信协议无效")
}
/// 读取固定长度整数，所有边界均先验证。
fn number<const N: usize>(bytes: &mut &[u8]) -> io::Result<[u8; N]> {
    if bytes.len() < N {
        return Err(invalid());
    }
    let (head, tail) = bytes.split_at(N);
    *bytes = tail;
    Ok(head.try_into().expect("固定长度已验证"))
}
/// 写入长度前缀的 UTF-8 字段。
fn string(out: &mut Vec<u8>, value: &str) {
    out.extend((value.len() as u32).to_le_bytes());
    out.extend(value.as_bytes());
}
/// 编码一次不可变结果快照；候选和提交均保留引擎标识。
fn snapshot(out: &mut Vec<u8>, value: &Output) {
    out.extend(u32::from(value.consumed).to_le_bytes());
    string(out, &value.preedit);
    out.extend((value.selected as u32).to_le_bytes());
    out.extend((value.page as u32).to_le_bytes());
    out.extend((value.candidates.len() as u32).to_le_bytes());
    for candidate in &value.candidates {
        string(out, &candidate.text);
        string(out, &candidate.wubi_hint);
    }
    out.extend((value.commits.len() as u32).to_le_bytes());
    for commit in &value.commits {
        out.extend(commit.id.to_le_bytes());
        string(out, &commit.text);
    }
}
/// 串行处理单连接请求；序号严格递增，非法请求不会变更会话。
pub fn dispatch(
    frame: &[u8],
    last: &mut u64,
    session: &mut Session,
    engine: &Engine,
) -> io::Result<Vec<u8>> {
    let mut body = frame;
    let version = u32::from_le_bytes(number(&mut body)?);
    let sequence = u64::from_le_bytes(number(&mut body)?);
    let operation = u32::from_le_bytes(number(&mut body)?);
    if version != VERSION || sequence <= *last || frame.len() > MAX_FRAME {
        return Err(invalid());
    }
    let mut out = frame[..16].to_vec();
    match operation {
        TEST | PROCESS => {
            let key = std::str::from_utf8(body).map_err(|_| invalid())?;
            if key.len() > 32 || key.is_empty() {
                return Err(invalid());
            }
            if operation == TEST {
                out.extend(u32::from(session.test_key(key)).to_le_bytes());
            } else {
                snapshot(&mut out, &session.process_event(engine, key));
            }
        }
        ACK => {
            let id = u64::from_le_bytes(number(&mut body)?);
            let success = u32::from_le_bytes(number(&mut body)?);
            if !body.is_empty() || success > 1 {
                return Err(invalid());
            }
            session.ack_commit(id, success == 1);
        }
        _ => return Err(invalid()),
    }
    *last = sequence;
    Ok(out)
}

#[cfg(test)]
mod tests {
    use super::*;
    use ime_core::Config;
    use ime_dictionary::Dictionary;

    /// 构造固定夹具和单连接状态。
    fn fixture() -> (Engine, Session) {
        (
            Engine {
                wubi: Dictionary::default(),
                pinyin: Dictionary::parse("你\tni\t10\n").unwrap(),
            },
            Session::new(Config::default()),
        )
    }
    /// 生成独立请求，便于验证重放保护。
    fn request(sequence: u64, operation: u32, body: &[u8]) -> Vec<u8> {
        let mut frame = VERSION.to_le_bytes().to_vec();
        frame.extend(sequence.to_le_bytes());
        frame.extend(operation.to_le_bytes());
        frame.extend(body);
        frame
    }
    /// 试探无副作用、序号重放被拒绝，两个连接保持焦点隔离。
    #[test]
    fn probe_replay_and_isolation() {
        let (engine, mut a) = fixture();
        let (_, mut b) = fixture();
        let mut seq = 0;
        dispatch(&request(1, PROCESS, b"n"), &mut seq, &mut a, &engine).unwrap();
        let probe = request(2, TEST, b"i");
        assert_eq!(
            &dispatch(&probe, &mut seq, &mut a, &engine).unwrap()[16..],
            &1u32.to_le_bytes()
        );
        assert!(dispatch(&probe, &mut seq, &mut a, &engine).is_err());
        dispatch(&request(3, PROCESS, b"i"), &mut seq, &mut a, &engine).unwrap();
        assert_eq!(a.process_event(&engine, "Space").commits[0].text, "你");
        assert!(!b.process_event(&engine, "Space").consumed);
    }
    /// 拒绝未知版本、操作、非法 UTF-8、超长与截断帧。
    #[test]
    fn malformed_frames() {
        let (engine, mut session) = fixture();
        let mut seq = 0;
        for frame in [
            vec![],
            request(1, 99, b"n"),
            request(1, PROCESS, &[255]),
            request(1, ACK, &[0; 11]),
            request(1, PROCESS, &[b'a'; 33]),
        ] {
            assert!(dispatch(&frame, &mut seq, &mut session, &engine).is_err());
            assert_eq!(seq, 0);
        }
        let mut frame = request(1, PROCESS, b"n");
        frame[0] = 2;
        assert!(dispatch(&frame, &mut seq, &mut session, &engine).is_err());
        assert!(read_frame(&mut &((MAX_FRAME as u32 + 1).to_le_bytes())[..]).is_err());
        assert!(read_frame(&mut &16u32.to_le_bytes()[..]).is_err());
    }
    /// 提交必须显式确认；失败和重复确认均不增加学习次数。
    #[test]
    fn commit_acknowledgement() {
        let (engine, mut session) = fixture();
        session.process_event(&engine, "n");
        session.process_event(&engine, "i");
        let commit = session.process_event(&engine, "Space").commits.remove(0);
        let mut body = commit.id.to_le_bytes().to_vec();
        body.extend(1u32.to_le_bytes());
        let mut seq = 0;
        dispatch(&request(1, ACK, &body), &mut seq, &mut session, &engine).unwrap();
        dispatch(&request(2, ACK, &body), &mut seq, &mut session, &engine).unwrap();
        assert_eq!(session.learned.values().sum::<u64>(), 1);
    }
}
