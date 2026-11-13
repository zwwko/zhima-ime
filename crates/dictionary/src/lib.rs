//! 词典读取、确定性索引编译与规范全码反查。
use serde::{Deserialize, Serialize};
use std::{collections::BTreeMap, path::Path};
/// 单条词典记录；规范全码仅对人工确认的单字填写。
#[derive(Clone, Debug, Serialize, Deserialize, PartialEq, Eq)]
pub struct Entry {
    pub text: String,
    pub code: String,
    pub weight: u64,
    pub full_code: Option<String>,
}
/// 编码顺序稳定的只读词典。
#[derive(Clone, Default, Debug, Serialize, Deserialize)]
pub struct Dictionary {
    pub entries: Vec<Entry>,
    #[serde(skip)]
    index: BTreeMap<String, Vec<usize>>,
}
impl Dictionary {
    /// 从记录建立稳定索引；保留源数据中的同码顺序。
    pub fn new(entries: Vec<Entry>) -> Self {
        let mut d = Self {
            entries,
            index: BTreeMap::new(),
        };
        d.reindex();
        d
    }
    /// 重建编码到记录编号的前缀索引。
    fn reindex(&mut self) {
        self.index.clear();
        for (i, e) in self.entries.iter().enumerate() {
            self.index.entry(e.code.clone()).or_default().push(i);
        }
    }
    /// 读取 TSV 或 Rime 词典数据子集；返回包含行号的中文错误。
    pub fn parse(text: &str) -> Result<Self, String> {
        let mut entries = Vec::new();
        let yaml = text.lines().any(|l| l.trim() == "---");
        let mut data = !yaml;
        for (i, line) in text.lines().enumerate() {
            let l = line.trim_end();
            if !data {
                if l == "..." {
                    data = true;
                }
                continue;
            }
            if l.is_empty() || l.starts_with('#') {
                continue;
            }
            let c: Vec<_> = l.split('\t').collect();
            if c.len() < 2 {
                return Err(format!("第 {} 行缺少文本或编码", i + 1));
            }
            let code = c[1].to_lowercase();
            if code.is_empty()
                || !code
                    .chars()
                    .all(|x| x.is_ascii_lowercase() || x == '\'' || x == ' ')
            {
                return Err(format!("第 {} 行编码非法", i + 1));
            }
            let weight = c
                .get(2)
                .filter(|x| !x.is_empty())
                .map(|x| {
                    let value = x.parse::<f64>().map_err(|_| ())?;
                    if !value.is_finite() || value < 0.0 || value > (u64::MAX / 1000) as f64 {
                        return Err(());
                    }
                    Ok((value * 1000.0).round() as u64)
                })
                .transpose()
                .map_err(|_| format!("第 {} 行词频非法", i + 1))?
                .unwrap_or(0);
            let full_code = c.get(3).filter(|x| !x.is_empty()).map(|x| x.to_string());
            entries.push(Entry {
                text: c[0].into(),
                code,
                weight,
                full_code,
            });
        }
        if !data {
            return Err("词典头部缺少结束标记".into());
        }
        Ok(Self::new(entries))
    }
    /// 返回编码前缀匹配；最大结果数限制检索开销。
    pub fn lookup(&self, code: &str, limit: usize) -> Vec<&Entry> {
        self.index
            .range(code.to_string()..)
            .take_while(|(k, _)| k.starts_with(code))
            .flat_map(|(_, ids)| ids.iter().map(|i| &self.entries[*i]))
            .take(limit)
            .collect()
    }
    /// 查询人工标记的单字规范全码。
    pub fn full_code(&self, c: char) -> Option<&str> {
        self.entries
            .iter()
            .find(|e| e.text == c.to_string() && e.full_code.is_some())
            .and_then(|e| e.full_code.as_deref())
    }
    /// 输出可重建的 TSV 文本。
    pub fn to_tsv(&self) -> String {
        self.entries
            .iter()
            .map(|e| {
                format!(
                    "{}\t{}\t{}\t{}\n",
                    e.text,
                    e.code,
                    e.weight as f64 / 1000.0,
                    e.full_code.as_deref().unwrap_or("")
                )
            })
            .collect()
    }
    /// 保存带版本号、长度和校验值的确定性索引。
    pub fn compile(&self, path: &Path) -> Result<(), String> {
        let body = serde_json::to_vec(&self.entries).map_err(|e| format!("序列化失败：{e}"))?;
        let mut b = b"ZFIM\x01".to_vec();
        b.extend((body.len() as u64).to_le_bytes());
        b.extend(checksum(&body).to_le_bytes());
        b.extend(body);
        std::fs::write(path, b).map_err(|e| format!("写入词典失败：{e}"))
    }
    /// 验证完整性后载入索引；拒绝截断、未知版本及损坏数据。
    pub fn load(path: &Path) -> Result<Self, String> {
        let b = std::fs::read(path).map_err(|e| format!("读取词典失败：{e}"))?;
        if b.len() < 21 || &b[..5] != b"ZFIM\x01" {
            return Err("词典格式或版本错误".into());
        }
        let n = u64::from_le_bytes(b[5..13].try_into().unwrap());
        let sum = u64::from_le_bytes(b[13..21].try_into().unwrap());
        if n != b.len() as u64 - 21 || checksum(&b[21..]) != sum {
            return Err("词典长度或校验值错误".into());
        }
        let entries = serde_json::from_slice(&b[21..]).map_err(|e| format!("词典数据错误：{e}"))?;
        Ok(Self::new(entries))
    }
}
/// 计算用于检测文件损坏的 FNV-1a 校验值，不用于安全认证。
fn checksum(b: &[u8]) -> u64 {
    b.iter().fold(0xcbf29ce484222325, |h, x| {
        (h ^ *x as u64).wrapping_mul(0x100000001b3)
    })
}
