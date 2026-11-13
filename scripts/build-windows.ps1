<# 构建未签名 Windows x64 引擎及 x86/x64 TSF 开发包，不注册输入源。 #>
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
Push-Location $projectRoot
try {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere)) { throw '未找到 Visual Studio 安装检测工具，请安装 C++ 桌面开发组件。' }
    $vsRoot = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vsRoot) { throw '未找到 MSVC x86/x64 编译器。' }
    $vcvars = Join-Path $vsRoot 'VC\Auxiliary\Build\vcvarsall.bat'
    $previousRustFlags = $env:RUSTFLAGS
    try {
        # 静态链接 CRT，避免开发包依赖用户额外安装 VC++ 运行库。
        $env:RUSTFLAGS = "$previousRustFlags -C target-feature=+crt-static".Trim()
        & cargo build --release --locked -p ime-windows-service
        if ($LASTEXITCODE -ne 0) { throw 'Rust 引擎服务构建失败。' }
    } finally { $env:RUSTFLAGS = $previousRustFlags }
    $package = Join-Path $projectRoot 'dist\windows'
    New-Item -ItemType Directory -Force -Path $package | Out-Null
    foreach ($arch in @('x64', 'x86')) {
        $output = Join-Path $package $arch
        $intermediate = Join-Path $projectRoot "target\windows\$arch"
        New-Item -ItemType Directory -Force -Path $output, $intermediate | Out-Null
        # 路径由固定工程根目录和架构枚举构成；不把用户输入拼接为命令。
        $compile = "`"$vcvars`" $arch && cl /nologo /std:c++17 /utf-8 /EHsc /MT /O2 /W4 /WX /DUNICODE /D_UNICODE /LD platforms\windows\tsf.cpp /Fo`"$intermediate\tsf.obj`" /link /DEF:platforms\windows\zhima.def /OUT:`"$output\ZhimaTsf.dll`" /IMPLIB:`"$intermediate\ZhimaTsf.lib`" ole32.lib oleaut32.lib advapi32.lib user32.lib gdi32.lib uuid.lib"
        $batch = Join-Path $intermediate 'build-tsf.cmd'
        [IO.File]::WriteAllText($batch, "@echo off`r`nchcp 65001 >nul`r`n$compile`r`n", [Text.UTF8Encoding]::new($false))
        & $env:ComSpec /d /c $batch
        if ($LASTEXITCODE -ne 0) { throw "$arch TSF 前端构建失败。" }
        $compile = "`"$vcvars`" $arch && cl /nologo /std:c++17 /utf-8 /EHsc /MT /O2 /W4 /WX /wd4191 /DUNICODE /D_UNICODE platforms\windows\smoke.cpp /Fo`"$intermediate\smoke.obj`" /Fe`"$output\zhima-smoke.exe`" /link ole32.lib advapi32.lib user32.lib uuid.lib"
        $batch = Join-Path $intermediate 'build-smoke.cmd'
        [IO.File]::WriteAllText($batch, "@echo off`r`nchcp 65001 >nul`r`n$compile`r`n", [Text.UTF8Encoding]::new($false))
        & $env:ComSpec /d /c $batch
        if ($LASTEXITCODE -ne 0) { throw "$arch 原生验证程序构建失败。" }
    }
    $maintenanceOutput = Join-Path $projectRoot 'target\windows\x64'
    $compile = "`"$vcvars`" x64 && cl /nologo /std:c++17 /utf-8 /EHsc /MT /O2 /W4 /WX /wd4191 /DUNICODE /D_UNICODE platforms\windows\maintenance.cpp /Fo`"$maintenanceOutput\maintenance.obj`" /Fe`"$package\ZhimaMaintenance.exe`" /link /SUBSYSTEM:WINDOWS user32.lib shell32.lib"
    $batch = Join-Path $maintenanceOutput 'build-maintenance.cmd'
    [IO.File]::WriteAllText($batch, "@echo off`r`nchcp 65001 >nul`r`n$compile`r`n", [Text.UTF8Encoding]::new($false))
    & $env:ComSpec /d /c $batch
    if ($LASTEXITCODE -ne 0) { throw '安装维护工具构建失败。' }
    Copy-Item -LiteralPath 'target\release\ime-windows-service.exe' -Destination $package
    Copy-Item -LiteralPath 'data\fixtures\wubi.tsv', 'data\fixtures\pinyin.tsv', 'LICENSE' -Destination $package
    Copy-Item -LiteralPath 'scripts\install-windows.ps1', 'scripts\uninstall-windows.ps1', 'scripts\test-windows.ps1' -Destination $package
    Copy-Item -LiteralPath 'docs\windows.md' -Destination (Join-Path $package 'README.md')
    Compress-Archive -Path (Join-Path $package '*') -DestinationPath (Join-Path $projectRoot 'dist\zhima-ime-windows-dev.zip') -Force
    Write-Host "已生成未签名开发包：$package（仅含测试词库，未安装）。"
} finally { Pop-Location }
