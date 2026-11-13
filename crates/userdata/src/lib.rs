//! SQLite 用户词库及版本化 TOML 配置。
use rusqlite::{params, Connection};
use serde::{Deserialize, Serialize};
use std::path::Path;
/// 持久化的用户设置。
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Settings {
    pub version: u32,
    pub page_size: usize,
    pub font_size: f32,
    pub vertical: bool,
    pub dark: bool,
    pub chinese_punctuation: bool,
    pub full_width: bool,
}
impl Default for Settings {
    /// 返回首版默认设置。
    fn default() -> Self {
        Self {
            version: 1,
            page_size: 5,
            font_size: 18.,
            vertical: false,
            dark: false,
            chinese_punctuation: true,
            full_width: false,
        }
    }
}
impl Settings {
    /// 读取配置并检查版本与范围；失败返回中文说明。
    pub fn load(path: &Path) -> Result<Self, String> {
        let text = std::fs::read_to_string(path).map_err(|e| format!("读取配置失败：{e}"))?;
        let s: Self = toml::from_str(&text).map_err(|e| format!("配置格式错误：{e}"))?;
        if s.version != 1
            || !(3..=9).contains(&s.page_size)
            || !(10.0..=48.0).contains(&s.font_size)
        {
            return Err("配置版本或参数范围错误".into());
        }
        Ok(s)
    }
    /// 原子保存配置；临时文件位于目标目录。
    pub fn save(&self, path: &Path) -> Result<(), String> {
        let tmp = path.with_extension("toml.tmp");
        let text = toml::to_string_pretty(self).map_err(|e| format!("编码配置失败：{e}"))?;
        std::fs::write(&tmp, text)
            .and_then(|_| std::fs::rename(&tmp, path))
            .map_err(|e| format!("保存配置失败：{e}"))
    }
}
/// 同一线程内访问的用户数据库；调用者应在后台线程执行写操作。
pub struct UserStore {
    conn: Connection,
}
impl UserStore {
    /// 打开数据库并执行首版事务迁移；拒绝未来版本。
    pub fn open(path: &Path) -> Result<Self, String> {
        let conn = Connection::open(path).map_err(|e| format!("打开用户词库失败：{e}"))?;
        let v: u32 = conn
            .pragma_query_value(None, "user_version", |r| r.get(0))
            .map_err(|e| format!("读取数据库版本失败：{e}"))?;
        if v > 1 {
            return Err("用户词库来自更新版本，请升级程序".into());
        }
        conn.execute_batch("PRAGMA journal_mode=WAL; BEGIN; CREATE TABLE IF NOT EXISTS words(code TEXT NOT NULL,text TEXT NOT NULL, pinned INTEGER NOT NULL DEFAULT 0,uses INTEGER NOT NULL DEFAULT 0,PRIMARY KEY(code,text)); PRAGMA user_version=1; COMMIT;").map_err(|e|format!("初始化用户词库失败：{e}"))?;
        Ok(Self { conn })
    }
    /// 添加或修改用户词；拒绝空文本及非字母编码。
    pub fn put(&self, code: &str, text: &str, pinned: bool) -> Result<(), String> {
        if code.is_empty()
            || code.len() > 64
            || !code.bytes().all(|x| x.is_ascii_lowercase() || x == b'\'')
            || text.is_empty()
        {
            return Err("用户词编码或文本非法".into());
        }
        self.conn.execute("INSERT INTO words(code,text,pinned) VALUES(?1,?2,?3) ON CONFLICT(code,text) DO UPDATE SET pinned=excluded.pinned",params![code,text,pinned]).map(|_|()).map_err(|e|format!("保存用户词失败：{e}"))
    }
    /// 删除指定用户词，不影响系统词典。
    pub fn remove(&self, code: &str, text: &str) -> Result<(), String> {
        self.conn
            .execute(
                "DELETE FROM words WHERE code=?1 AND text=?2",
                params![code, text],
            )
            .map(|_| ())
            .map_err(|e| format!("删除用户词失败：{e}"))
    }
    /// 读取稳定排序的数据快照。
    pub fn snapshot(&self) -> Result<Vec<(String, String, bool, u64)>, String> {
        let mut q = self
            .conn
            .prepare("SELECT code,text,pinned,uses FROM words ORDER BY code,text")
            .map_err(|e| format!("查询用户词失败：{e}"))?;
        let rows = q
            .query_map([], |r| Ok((r.get(0)?, r.get(1)?, r.get(2)?, r.get(3)?)))
            .map_err(|e| format!("读取用户词失败：{e}"))?;
        rows.collect::<Result<Vec<_>, _>>()
            .map_err(|e| format!("解码用户词失败：{e}"))
    }
}
