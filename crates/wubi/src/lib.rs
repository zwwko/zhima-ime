//! 86 五笔码表查询和词语编码。
use ime_dictionary::Dictionary;
/// 按规范单字全码生成词语码；缺失全码时拒绝猜测。
pub fn encode_phrase(text: &str, d: &Dictionary) -> Result<String, String> {
    let c: Vec<_> = text.chars().collect();
    if c.len() < 2 {
        return Err("造词至少需要两个汉字".into());
    }
    let take = |ch, n| -> Result<String, String> {
        let s = d
            .full_code(ch)
            .ok_or_else(|| format!("缺少汉字「{ch}」的规范全码，请手动填写"))?;
        if s.len() < n {
            return Err(format!("汉字「{ch}」的全码不足 {n} 码，请手动填写"));
        }
        Ok(s.chars().take(n).collect())
    };
    match c.len() {
        2 => Ok(take(c[0], 2)? + &take(c[1], 2)?),
        3 => Ok(take(c[0], 1)? + &take(c[1], 1)? + &take(c[2], 2)?),
        _ => Ok(take(c[0], 1)? + &take(c[1], 1)? + &take(c[2], 1)? + &take(*c.last().unwrap(), 1)?),
    }
}
