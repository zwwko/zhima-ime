//! 开发调试入口、词典编译工具及可选 GUI 设置窗口。
use ime_core::{Config, Engine, Session};
use ime_dictionary::Dictionary;
use std::{
    io::{self, BufRead},
    path::Path,
};
/// 执行命令；失败使用中文提示并返回非零退出码。
fn main() {
    if let Err(e) = run() {
        eprintln!("操作失败：{e}");
        std::process::exit(1);
    }
}
/// 根据命令参数执行编译、回放或设置界面。
fn run() -> Result<(), String> {
    let a: Vec<_> = std::env::args().collect();
    match a.get(1).map(String::as_str) {
        Some("compile") => {
            if a.len() != 4 {
                return Err("用法：ime-settings compile 输入.tsv 输出.zfd".into());
            }
            let d = Dictionary::parse(
                &std::fs::read_to_string(&a[2]).map_err(|e| format!("读取文件失败：{e}"))?,
            )?;
            d.compile(Path::new(&a[3]))
        }
        Some("replay") => {
            if a.len() != 4 {
                return Err(
                    "用法：ime-settings replay 五笔.tsv 拼音.tsv；每行输入一个标准按键".into(),
                );
            }
            let load = |p: &str| {
                Dictionary::parse(
                    &std::fs::read_to_string(p).map_err(|e| format!("读取文件失败：{e}"))?,
                )
            };
            let e = Engine {
                wubi: load(&a[2])?,
                pinyin: load(&a[3])?,
            };
            let mut s = Session::new(Config::default());
            for l in io::stdin().lock().lines() {
                let key = l.map_err(|e| format!("读取按键失败：{e}"))?;
                let out = s.process_event(&e, &key);
                println!(
                    "{}",
                    serde_json::to_string(&out).map_err(|e| format!("输出失败：{e}"))?
                );
            }
            Ok(())
        }
        Some("gui") => gui(),
        _ => Err("支持命令：compile、replay、gui（需启用 gui 功能）".into()),
    }
}
/// 打开可选的原生设置窗口。
#[cfg(not(feature = "gui"))]
fn gui() -> Result<(), String> {
    Err("请使用 --features gui 编译设置窗口".into())
}
/// 启动 egui 桌面设置窗口。
#[cfg(feature = "gui")]
fn gui() -> Result<(), String> {
    eframe::run_native(
        "芝麻输入法设置",
        eframe::NativeOptions::default(),
        Box::new(|_| {
            Ok(Box::new(SettingsApp {
                settings: Default::default(),
                message: String::new(),
            }))
        }),
    )
    .map_err(|e| format!("启动设置窗口失败：{e}"))
}
/// 设置窗口的表单状态。
#[cfg(feature = "gui")]
struct SettingsApp {
    settings: ime_userdata::Settings,
    message: String,
}
#[cfg(feature = "gui")]
impl eframe::App for SettingsApp {
    /// 绘制设置并在用户明确点击时保存到指定路径。
    fn update(&mut self, ctx: &eframe::egui::Context, _: &mut eframe::Frame) {
        eframe::egui::CentralPanel::default().show(ctx, |ui| {
            ui.heading("芝麻输入法");
            ui.add(eframe::egui::Slider::new(&mut self.settings.page_size, 3..=9).text("每页候选"));
            ui.add(
                eframe::egui::Slider::new(&mut self.settings.font_size, 10.0..=48.0).text("字号"),
            );
            ui.checkbox(&mut self.settings.vertical, "竖排候选");
            ui.checkbox(&mut self.settings.dark, "深色主题");
            ui.checkbox(&mut self.settings.chinese_punctuation, "中文标点");
            ui.checkbox(&mut self.settings.full_width, "全角字符");
            if ui.button("保存到当前目录 config.toml").clicked() {
                self.message = match self.settings.save(Path::new("config.toml")) {
                    Ok(()) => "设置已保存".into(),
                    Err(e) => e,
                };
            }
            ui.label(&self.message);
        });
    }
}
