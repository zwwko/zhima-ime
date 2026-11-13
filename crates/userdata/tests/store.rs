//! 用户词的事务存储与参数验证。
use ime_userdata::UserStore;
/// 验证修改、删除及参数化 SQL 不破坏数据库。
#[test]
fn user_words() {
    let s = UserStore::open(std::path::Path::new(":memory:")).unwrap();
    s.put("abc", "用户'词", true).unwrap();
    s.put("abc", "用户'词", false).unwrap();
    let rows = s.snapshot().unwrap();
    assert_eq!(rows.len(), 1);
    assert!(!rows[0].2);
    assert!(s.put("BAD", "错误", false).is_err());
    s.remove("abc", "用户'词").unwrap();
    assert!(s.snapshot().unwrap().is_empty());
}
