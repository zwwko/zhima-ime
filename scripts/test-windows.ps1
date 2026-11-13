<# 启动测试包服务，执行两种位数的原生验证，最后关闭本脚本启动的进程。 #>
[CmdletBinding()]
param([string]$PackagePath)
$ErrorActionPreference = 'Stop'
if (-not $PackagePath) {
    $PackagePath = if (Test-Path -LiteralPath (Join-Path $PSScriptRoot 'ime-windows-service.exe')) { $PSScriptRoot } else { Join-Path (Split-Path $PSScriptRoot -Parent) 'dist\windows' }
}
$package = (Resolve-Path -LiteralPath $PackagePath).Path
if (Get-Process -Name 'ime-windows-service' -ErrorAction SilentlyContinue) { throw '已有芝麻引擎服务运行，请先关闭，避免测试混用其他词库或进程。' }
$service = Start-Process -FilePath (Join-Path $package 'ime-windows-service.exe') -PassThru -WindowStyle Hidden
try {
    Start-Sleep -Milliseconds 500
    if ($service.HasExited) { throw '引擎服务提前退出，请核对测试词库和管道占用情况。' }
    foreach ($arch in @('x64', 'x86')) {
        & (Join-Path $package "$arch\zhima-smoke.exe") (Join-Path $package "$arch\ZhimaTsf.dll")
        if ($LASTEXITCODE -ne 0) { throw "$arch 原生验证失败。" }
    }
} finally {
    if (-not $service.HasExited) { Stop-Process -Id $service.Id; $service.WaitForExit() }
}
foreach ($arch in @('x64', 'x86')) {
    & (Join-Path $package "$arch\zhima-smoke.exe") --offline
    if ($LASTEXITCODE -ne 0) { throw "$arch 服务失联验证失败。" }
}
Write-Host 'Windows 开发包验证完成；未注册或切换系统输入源。'
