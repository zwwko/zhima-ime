//! 按实际按键流程验证混输、焦点隔离和提交确认。
use ime_core::*;
use ime_dictionary::Dictionary;
/// 使用小型人工测试词典构造可复现引擎。
fn engine() -> Engine {
    Engine {
        wubi: Dictionary::parse(
            "你\twqiy\t100\twqiy\n泥\twqiy\t1\t\n中\tkhk\t100\tkhk\n你\tni\t1\t\n",
        )
        .unwrap(),
        pinyin: Dictionary::parse(
            "你\tni\t100\n你好\tni hao\t90\n中国\tzhong guo\t100\n西安\txi an\t10\n先\txian\t20\n",
        )
        .unwrap(),
    }
}
/// 逐字发送字母并返回最终快照。
fn type_keys(s: &mut Session, e: &Engine, text: &str) -> Output {
    let mut out = s.process_event(e, "Escape");
    for c in text.chars() {
        out = s.process_event(e, &c.to_string());
        assert!(out.commits.is_empty());
    }
    out
}
/// 长拼音保持完整，直到用户明确确认。
#[test]
fn mixed_long_pinyin() {
    let e = engine();
    let mut s = Session::new(Config::default());
    let o = type_keys(&mut s, &e, "zhongguo");
    assert_eq!(o.preedit, "zhongguo");
    assert_eq!(o.candidates[0].text, "中国");
    assert_eq!(s.process_event(&e, "Space").commits[0].text, "中国");
}
/// 同一文本合并来源，五笔精确匹配保留首位。
#[test]
fn dedup_and_test_is_pure() {
    let e = engine();
    let mut s = Session::new(Config::default());
    type_keys(&mut s, &e, "n");
    for _ in 0..10 {
        assert!(s.test_key("i"));
    }
    let o = s.process_event(&e, "i");
    assert_eq!(o.preedit, "ni");
    assert_eq!(o.candidates[0].sources, vec!["五笔", "拼音"]);
    assert!(unique_candidates(&o));
}
/// 只有成功确认的提交学习一次。
#[test]
fn ack_once() {
    let e = engine();
    let mut s = Session::new(Config::default());
    type_keys(&mut s, &e, "ni");
    let c = s.process_event(&e, "Space").commits.remove(0);
    assert!(s.learned.is_empty());
    s.ack_commit(c.id, true);
    s.ack_commit(c.id, true);
    assert_eq!(s.learned.values().sum::<u64>(), 1);
    type_keys(&mut s, &e, "ni");
    let c = s.process_event(&e, "Space").commits.remove(0);
    s.ack_commit(c.id, false);
    assert_eq!(s.learned.values().sum::<u64>(), 1);
}
/// 临时拼音提交后恢复五笔，焦点取消不影响其他会话。
#[test]
fn temporary_and_focus() {
    let e = engine();
    let mut a = Session::new(Config {
        mode: Mode::Wubi,
        ..Config::default()
    });
    let mut b = Session::new(Config::default());
    a.process_event(&e, "`");
    a.process_event(&e, "n");
    a.process_event(&e, "i");
    assert_eq!(a.process_event(&e, "Space").commits[0].text, "你");
    assert_eq!(a.config.mode, Mode::Wubi);
    type_keys(&mut a, &e, "wqiy");
    type_keys(&mut b, &e, "ni");
    assert!(a.process_event(&e, "FocusOut").preedit.is_empty());
    assert_eq!(b.process_event(&e, "Space").commits[0].text, "你");
}
/// 纯五笔存在重码时禁止四码自动提交。
#[test]
fn ambiguous_four_stays() {
    let e = engine();
    let mut s = Session::new(Config {
        mode: Mode::Wubi,
        auto_four: true,
        ..Config::default()
    });
    assert_eq!(type_keys(&mut s, &e, "wqiy").preedit, "wqiy");
}
/// 显式音节分隔和无分隔拼音均按词典边界匹配。
#[test]
fn apostrophe() {
    let e = engine();
    let mut s = Session::new(Config::default());
    let o = type_keys(&mut s, &e, "xi'an");
    assert_eq!(o.candidates[0].text, "西安");
    assert_eq!(o.candidates.len(), 1);
}
/// 超长输入不增长，旧候选标识无法提交。
#[test]
fn bounds_and_stale_selection() {
    let e = engine();
    let mut s = Session::new(Config::default());
    let o = type_keys(&mut s, &e, "ni");
    let id = o.candidates[0].id;
    s.process_event(&e, "h");
    assert!(s.select(id).is_none());
    let o = type_keys(&mut s, &e, &"a".repeat(100));
    assert_eq!(o.preedit.len(), 64);
}
