#Requires -RunAsAdministrator
<# 全新安装开发包；系统注册需要提升权限，引擎在各登录用户身份下运行。 #>
[CmdletBinding()]
param([string]$PackagePath)
$ErrorActionPreference = 'Stop'
if (-not [Environment]::Is64BitProcess) { throw '请在 64 位 PowerShell 中安装。' }
if (-not $PackagePath) {
    $PackagePath = if (Test-Path -LiteralPath (Join-Path $PSScriptRoot 'ime-windows-service.exe')) { $PSScriptRoot } else { Join-Path (Split-Path $PSScriptRoot -Parent) 'dist\windows' }
}
$package = (Resolve-Path -LiteralPath $PackagePath).Path
$destination = Join-Path $env:ProgramFiles 'ZhimaIME'
$runKey = 'HKLM:\Software\Microsoft\Windows\CurrentVersion\Run'
if (Test-Path -LiteralPath $destination) { throw '安装目录已存在；开发版暂不支持覆盖升级，请先卸载旧版。' }
if (Get-ItemProperty -Path $runKey -Name 'ZhimaIME' -ErrorAction SilentlyContinue) { throw '已有芝麻输入法启动项，请先核对并卸载旧版，避免覆盖现有配置。' }
foreach ($relative in @('ime-windows-service.exe', 'wubi.tsv', 'pinyin.tsv', 'x64\ZhimaTsf.dll', 'x86\ZhimaTsf.dll', 'uninstall-windows.ps1', 'LICENSE')) {
    if (-not (Test-Path -LiteralPath (Join-Path $package $relative) -PathType Leaf)) { throw "开发包不完整，缺少：$relative" }
}
$registered = @()
$startupWritten = $false
try {
    New-Item -ItemType Directory -Path $destination | Out-Null
    Get-ChildItem -LiteralPath $package | Copy-Item -Destination $destination -Recurse
    foreach ($arch in @('x64', 'x86')) {
        $registrar = if ($arch -eq 'x64') { Join-Path $env:WINDIR 'System32\regsvr32.exe' } else { Join-Path $env:WINDIR 'SysWOW64\regsvr32.exe' }
        $dll = Join-Path $destination "$arch\ZhimaTsf.dll"
        $registered += @(@{ Registrar = $registrar; Dll = $dll })
        $process = Start-Process -FilePath $registrar -ArgumentList @('/s', "`"$dll`"") -Wait -PassThru -WindowStyle Hidden
        if ($process.ExitCode -ne 0) { throw "$arch 输入源注册失败，错误码：$($process.ExitCode)" }
    }
    $engine = Join-Path $destination 'ime-windows-service.exe'
    New-ItemProperty -Path $runKey -Name 'ZhimaIME' -Value "`"$engine`"" -PropertyType String -Force | Out-Null
    $startupWritten = $true
    Write-Host '芝麻输入法开发版已安装。请注销后重新登录，再使用 Win+空格选择输入法；引擎以登录用户身份自动启动。'
} catch {
    $installationError = $_
    $rollbackFailures = @()
    foreach ($entry in $registered) {
        try {
            $rollback = Start-Process -FilePath $entry.Registrar -ArgumentList @('/s', '/u', "`"$($entry.Dll)`"") -Wait -PassThru -WindowStyle Hidden
            if ($rollback.ExitCode -ne 0) { $rollbackFailures += $entry.Dll }
        } catch { $rollbackFailures += $entry.Dll }
    }
    if ($startupWritten) { Remove-ItemProperty -Path $runKey -Name 'ZhimaIME' -ErrorAction SilentlyContinue }
    if ($rollbackFailures.Count -gt 0) { throw "安装失败：$installationError；部分注册撤销失败，文件已保留于 $destination，请运行卸载脚本重试。" }
    # 仅删除本次创建的固定目录；先验证解析后的目标确实在 Program Files 内。
    $expected = [IO.Path]::GetFullPath((Join-Path $env:ProgramFiles 'ZhimaIME'))
    if ((Test-Path -LiteralPath $destination) -and (Resolve-Path -LiteralPath $destination).Path -eq $expected) { Remove-Item -LiteralPath $destination -Recurse -Force }
    throw $installationError
}
