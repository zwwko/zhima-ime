//! 有界全拼字词检索；保留词典中的音节边界。
use ime_dictionary::{Dictionary, Entry};
/// 将 ü/v 和音节分隔符规范化；不合并用户显式指定的分界。
pub fn normalize(s: &str) -> String {
    s.to_lowercase()
        .replace('ü', "v")
        .replace(' ', "'")
        .replace("u:", "v")
}
/// 匹配完整字词或末音节前缀；显式撇号必须匹配词典音节边界。
pub fn lookup<'a>(d: &'a Dictionary, input: &str, limit: usize) -> Vec<(&'a Entry, bool)> {
    let q = normalize(input);
    let compact = q.replace('\'', "");
    let mut out = Vec::new();
    for e in &d.entries {
        let k = normalize(&e.code);
        let flat = k.replace('\'', "");
        let boundary_ok = !q.contains('\'') || k.starts_with(&q);
        if boundary_ok && flat.starts_with(&compact) {
            out.push((e, flat == compact));
        }
    }
    out.sort_by_key(|(e, exact)| (!*exact, std::cmp::Reverse(e.weight)));
    out.truncate(limit);
    out
}
