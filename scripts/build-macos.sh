#!/bin/sh
# 构建本机架构的未签名输入法包；不修改系统输入源。
set -eu
cd "$(dirname "$0")/.."
cargo build --release -p ime-ffi
app="dist/芝麻输入法.app/Contents"
mkdir -p "$app/MacOS" "$app/Resources"
cp platforms/macos/Info.plist "$app/Info.plist"
cp data/fixtures/wubi.tsv "$app/Resources/wubi.tsv"
cp data/fixtures/pinyin.tsv "$app/Resources/pinyin.tsv"
clang -fobjc-arc -mmacosx-version-min=14.0 -Iinclude platforms/macos/main.m target/release/libime_ffi.a -framework Cocoa -framework InputMethodKit -framework Security -framework SystemConfiguration -liconv -o "$app/MacOS/Zhima"
printf '%s\n' '已生成未签名开发包：dist/芝麻输入法.app（仅含测试词库）'
