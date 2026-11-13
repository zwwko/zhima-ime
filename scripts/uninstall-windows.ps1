#Requires -RunAsAdministrator
<# 卸载固定目录内的开发版，保留未来版本可能产生的用户数据。 #>
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
if (-not [Environment]::Is64BitProcess) { throw '请在 64 位 PowerShell 中卸载。' }
$destination = Join-Path $env:ProgramFiles 'ZhimaIME'
if (-not (Test-Path -LiteralPath $destination)) { throw '未找到芝麻输入法安装目录。' }
$expected = [IO.Path]::GetFullPath((Join-Path $env:ProgramFiles 'ZhimaIME'))
if ((Resolve-Path -LiteralPath $destination).Path -ne $expected) { throw '安装目录路径验证失败，已停止卸载。' }
$nativeUninstaller = Join-Path $destination 'unins000.exe'
if (Test-Path -LiteralPath $nativeUninstaller) {
    # EXE 安装版须交给其卸载日志处理，避免遗漏系统应用列表和开始菜单入口。
    Write-Host '检测到 EXE 安装版，正在打开对应的卸载向导。'
    $nativeProcess = Start-Process -FilePath $nativeUninstaller -Wait -PassThru -WindowStyle Normal
    if ($nativeProcess.ExitCode -ne 0) { throw "卸载未完成，退出码：$($nativeProcess.ExitCode)" }
    return
}
foreach ($arch in @('x86', 'x64')) {
    $registrar = if ($arch -eq 'x64') { Join-Path $env:WINDIR 'System32\regsvr32.exe' } else { Join-Path $env:WINDIR 'SysWOW64\regsvr32.exe' }
    $dll = Join-Path $destination "$arch\ZhimaTsf.dll"
    $process = Start-Process -FilePath $registrar -ArgumentList @('/s', '/u', "`"$dll`"") -Wait -PassThru -WindowStyle Hidden
    if ($process.ExitCode -ne 0) { throw "$arch 输入源注销失败，错误码：$($process.ExitCode)；安装文件已保留。" }
}
Remove-ItemProperty -Path 'HKLM:\Software\Microsoft\Windows\CurrentVersion\Run' -Name 'ZhimaIME' -ErrorAction SilentlyContinue
Get-Process -Name 'ime-windows-service' -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq (Join-Path $destination 'ime-windows-service.exe') } | Stop-Process
try {
    Remove-Item -LiteralPath $destination -Recurse -Force
    Write-Host '已注销输入源、移除登录启动项并删除开发包；用户数据未修改。'
} catch {
    Write-Warning "输入源和启动项已移除，部分文件仍被应用占用。请注销后删除目录：$destination"
}
