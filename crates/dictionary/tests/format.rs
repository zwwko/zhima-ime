//! 词库源格式与索引损坏测试。
use ime_dictionary::Dictionary;
/// 验证 Rime 子集、确定性编译和损坏拒绝。
#[test]
fn roundtrip() {
    let d = Dictionary::parse("# 说明\n---\nname: fixture\n...\n中国\tkhlg\t10\n").unwrap();
    let p = std::env::temp_dir().join(format!("zf-dict-{}.zfd", std::process::id()));
    d.compile(&p).unwrap();
    let first = std::fs::read(&p).unwrap();
    d.compile(&p).unwrap();
    assert_eq!(first, std::fs::read(&p).unwrap());
    assert_eq!(
        Dictionary::load(&p).unwrap().lookup("kh", 1)[0].text,
        "中国"
    );
    let mut bad = first;
    bad[21] ^= 1;
    std::fs::write(&p, bad).unwrap();
    assert!(Dictionary::load(&p).is_err());
    std::fs::remove_file(p).unwrap();
}
/// 拒绝错误词频与没有结束标记的头部。
#[test]
fn invalid_source() {
    assert!(Dictionary::parse("中国\tkhlg\t坏值").is_err());
    assert!(Dictionary::parse("---\nname: x").is_err());
}
