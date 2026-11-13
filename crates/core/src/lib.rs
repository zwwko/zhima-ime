//! 平台无关的输入会话、候选排序及提交确认状态机。
use ime_dictionary::Dictionary;
use serde::{Deserialize, Serialize};
use std::collections::{BTreeMap, BTreeSet};
/// 用户选择的输入模式。
#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub enum Mode {
    Mixed,
    Wubi,
    Pinyin,
    English,
}
/// 一页候选的显示配置和上屏策略。
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Config {
    pub mode: Mode,
    pub page_size: usize,
    pub auto_four: bool,
    pub push_five: bool,
}
impl Default for Config {
    /// 返回稳定码序、手动确认的混输配置。
    fn default() -> Self {
        Self {
            mode: Mode::Mixed,
            page_size: 5,
            auto_four: false,
            push_five: false,
        }
    }
}
/// 向平台公开的候选内容，来源可同时包含五笔与拼音。
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Candidate {
    pub id: u64,
    pub text: String,
    pub sources: Vec<String>,
    pub code: String,
    pub wubi_hint: String,
}
/// 等待平台确认的文本提交。
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Commit {
    pub id: u64,
    pub text: String,
}
/// 一次事件处理后的不可变快照。
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Output {
    pub consumed: bool,
    pub preedit: String,
    pub candidates: Vec<Candidate>,
    pub selected: usize,
    pub page: usize,
    pub commits: Vec<Commit>,
}
/// 共享系统词库；会话持有各自状态。
#[derive(Clone)]
pub struct Engine {
    pub wubi: Dictionary,
    pub pinyin: Dictionary,
}
/// 单输入上下文；序号和待确认提交仅在本会话内有效。
pub struct Session {
    pub config: Config,
    raw: String,
    temporary: bool,
    candidates: Vec<Candidate>,
    selected: usize,
    next: u64,
    pending: BTreeMap<u64, (String, String)>,
    pub learned: BTreeMap<(String, String), u64>,
    pub pinned: BTreeMap<String, String>,
    pub user_words: Dictionary,
    queued: Option<Config>,
}
impl Session {
    /// 建立空会话；候选页大小限制为 3–9。
    pub fn new(mut config: Config) -> Self {
        config.page_size = config.page_size.clamp(3, 9);
        Self {
            config,
            raw: String::new(),
            temporary: false,
            candidates: vec![],
            selected: 0,
            next: 1,
            pending: BTreeMap::new(),
            learned: BTreeMap::new(),
            pinned: BTreeMap::new(),
            user_words: Dictionary::default(),
            queued: None,
        }
    }
    /// 延迟到组合结束后应用新配置。
    pub fn configure(&mut self, c: Config) {
        if self.raw.is_empty() {
            self.config = c;
            self.config.page_size = self.config.page_size.clamp(3, 9);
        } else {
            self.queued = Some(c);
        }
    }
    /// 无副作用地判断是否接收按键；修饰键快捷键由适配器放行。
    pub fn test_key(&self, key: &str) -> bool {
        if key == "Shift" {
            return true;
        }
        if self.config.mode == Mode::English {
            return false;
        }
        if key.len() == 1 && key.as_bytes()[0].is_ascii_lowercase() {
            return true;
        }
        if key == "`" && self.raw.is_empty() {
            return true;
        }
        !self.raw.is_empty()
            && matches!(
                key,
                "Space"
                    | "Enter"
                    | "Escape"
                    | "Backspace"
                    | "Up"
                    | "Down"
                    | "PageUp"
                    | "PageDown"
                    | "'"
                    | "1"
                    | "2"
                    | "3"
                    | "4"
                    | "5"
                    | "6"
                    | "7"
                    | "8"
                    | "9"
            )
    }
    /// 清空组合及临时模式，不触发用户学习。
    fn clear(&mut self) {
        self.raw.clear();
        self.temporary = false;
        self.candidates.clear();
        self.selected = 0;
        if let Some(c) = self.queued.take() {
            self.configure(c);
        }
    }
    /// 生成平台提交动作；学习必须等待平台确认。
    fn commit(&mut self, text: String, learn: bool) -> Commit {
        let id = self.next;
        self.next += 1;
        if learn {
            self.pending.insert(id, (self.raw.clone(), text.clone()));
        }
        self.clear();
        Commit { id, text }
    }
    /// 确认一次提交；重复确认和失败确认均不增加词频。
    pub fn ack_commit(&mut self, id: u64, success: bool) {
        if let Some(k) = self.pending.remove(&id) {
            if success {
                *self.learned.entry(k).or_default() += 1;
            }
        }
    }
    /// 刷新候选；先分组排序，再按文本去重并合并来源。
    fn refresh(&mut self, e: &Engine) {
        let mut rows: Vec<(u8, u64, usize, String, String, String)> = vec![];
        let wb = !self.temporary && matches!(self.config.mode, Mode::Mixed | Mode::Wubi);
        let py = self.temporary || matches!(self.config.mode, Mode::Mixed | Mode::Pinyin);
        if wb {
            for (i, x) in e.wubi.lookup(&self.raw, 512).into_iter().enumerate() {
                rows.push((
                    if x.code == self.raw { 1 } else { 3 },
                    0,
                    i,
                    x.text.clone(),
                    x.code.clone(),
                    "五笔".into(),
                ));
            }
        }
        if py {
            for (i, (x, exact)) in ime_pinyin::lookup(&e.pinyin, &self.raw, 512)
                .into_iter()
                .enumerate()
            {
                let n = self
                    .learned
                    .get(&(self.raw.clone(), x.text.clone()))
                    .copied()
                    .unwrap_or(0);
                rows.push((
                    if exact { 2 } else { 4 },
                    u64::MAX - x.weight.saturating_add(n.saturating_mul(1000)),
                    i,
                    x.text.clone(),
                    x.code.clone(),
                    "拼音".into(),
                ));
            }
        }
        for (i, x) in self
            .user_words
            .lookup(&self.raw, 128)
            .into_iter()
            .enumerate()
        {
            if x.code == self.raw {
                rows.push((1, 0, i, x.text.clone(), x.code.clone(), "用户".into()));
            }
        }
        if let Some(text) = self.pinned.get(&self.raw) {
            rows.push((0, 0, 0, text.clone(), self.raw.clone(), "置顶".into()));
        }
        rows.sort_by_key(|x| (x.0, x.1, x.2));
        self.candidates.clear();
        let mut seen = BTreeMap::<String, usize>::new();
        for (_, _, _, text, code, source) in rows {
            if let Some(i) = seen.get(&text) {
                if !self.candidates[*i].sources.contains(&source) {
                    self.candidates[*i].sources.push(source);
                }
                continue;
            }
            let hint = text
                .chars()
                .filter_map(|c| e.wubi.full_code(c))
                .collect::<Vec<_>>()
                .join(" ");
            let id = self.next;
            self.next += 1;
            seen.insert(text.clone(), self.candidates.len());
            self.candidates.push(Candidate {
                id,
                text,
                code,
                sources: vec![source],
                wubi_hint: hint,
            });
        }
        self.selected = self.selected.min(self.candidates.len().saturating_sub(1));
    }
    /// 按候选标识提交；旧快照标识失效时返回空结果。
    pub fn select(&mut self, id: u64) -> Option<Commit> {
        let text = self.candidates.iter().find(|c| c.id == id)?.text.clone();
        Some(self.commit(text, true))
    }
    /// 处理标准化按键或 FocusOut 事件；每次调用至多消费一次事件。
    pub fn process_event(&mut self, e: &Engine, key: &str) -> Output {
        let mut commits = vec![];
        let consumed = if key == "FocusOut" {
            self.clear();
            self.pending.clear();
            false
        } else {
            self.test_key(key)
        };
        if consumed {
            match key {
                "Shift" => {
                    if !self.raw.is_empty() {
                        commits.push(self.commit(self.raw.clone(), false));
                    }
                    self.config.mode = if self.config.mode == Mode::English {
                        Mode::Mixed
                    } else {
                        Mode::English
                    };
                }
                "`" => {
                    self.temporary = true;
                }
                "Escape" => self.clear(),
                "Backspace" => {
                    self.raw.pop();
                    if self.raw.is_empty() {
                        self.clear();
                    }
                }
                "Space" | "Enter" => {
                    let candidate = key == "Space" && !self.candidates.is_empty();
                    let text = if candidate {
                        self.candidates[self.selected].text.clone()
                    } else {
                        format!("{}{}", self.raw, if key == "Space" { " " } else { "" })
                    };
                    commits.push(self.commit(text, candidate));
                }
                "Down" => {
                    self.selected = (self.selected + 1).min(self.candidates.len().saturating_sub(1))
                }
                "Up" => self.selected = self.selected.saturating_sub(1),
                "PageDown" => {
                    self.selected = (self.selected + self.config.page_size)
                        .min(self.candidates.len().saturating_sub(1))
                }
                "PageUp" => self.selected = self.selected.saturating_sub(self.config.page_size),
                _ => {
                    if key.len() == 1 && key.as_bytes()[0].is_ascii_digit() {
                        let n = key.parse::<usize>().unwrap() - 1;
                        let i = self.selected / self.config.page_size * self.config.page_size + n;
                        if n < self.config.page_size && i < self.candidates.len() {
                            commits.push(self.commit(self.candidates[i].text.clone(), true));
                        }
                    } else if self.raw.len() < 64 {
                        if self.config.mode == Mode::Wubi
                            && !self.temporary
                            && self.config.push_five
                            && self.raw.len() == 4
                            && !self.candidates.is_empty()
                        {
                            commits.push(self.commit(self.candidates[0].text.clone(), true));
                        }
                        self.raw.push_str(key);
                        self.refresh(e);
                        if self.config.mode == Mode::Wubi
                            && !self.temporary
                            && self.config.auto_four
                            && self.raw.len() == 4
                        {
                            let exact: Vec<_> = self
                                .candidates
                                .iter()
                                .filter(|x| x.code == self.raw)
                                .collect();
                            if exact.len() == 1 {
                                let text = exact[0].text.clone();
                                commits.push(self.commit(text, true));
                            }
                        }
                    }
                }
            }
        }
        if !self.raw.is_empty() && matches!(key, "Backspace") {
            self.refresh(e);
        }
        let page = self.selected / self.config.page_size;
        Output {
            consumed,
            preedit: self.raw.clone(),
            candidates: self
                .candidates
                .iter()
                .skip(page * self.config.page_size)
                .take(self.config.page_size)
                .cloned()
                .collect(),
            selected: self.selected % self.config.page_size,
            page,
            commits,
        }
    }
}
/// 检查一个回放结果是否存在重复文本，供平台一致性验收使用。
pub fn unique_candidates(out: &Output) -> bool {
    let s: BTreeSet<_> = out.candidates.iter().map(|c| &c.text).collect();
    s.len() == out.candidates.len()
}
