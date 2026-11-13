<# 编译中文 EXE 安装包；工具仅解包到 target/tools，不添加工具卸载项。 #>
[CmdletBinding()]
param([switch]$DownloadTools, [switch]$SkipBuild, [string]$IsccPath)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$toolRoot = Join-Path $projectRoot 'target\tools\InnoSetup'

function Get-VerifiedDownload {
    <# 从固定来源下载构建资源，SHA-256 不一致时禁止使用。 #>
    param([string]$Uri, [string]$Destination, [string]$ExpectedHash)
    if (-not (Test-Path -LiteralPath $Destination)) {
        if (-not $DownloadTools) { throw "缺少构建资源：$Destination。请使用 -DownloadTools 下载官方资源。" }
        New-Item -ItemType Directory -Force -Path (Split-Path $Destination -Parent) | Out-Null
        Invoke-WebRequest -Uri $Uri -OutFile $Destination
    }
    if ((Get-FileHash -Algorithm SHA256 -LiteralPath $Destination).Hash -ne $ExpectedHash) { throw "构建资源校验失败，请移除缓存后重新下载：$Destination" }
}

if (-not $IsccPath) {
    $IsccPath = Join-Path $toolRoot 'ISCC.exe'
    if (-not (Test-Path -LiteralPath $IsccPath)) {
        $installer = Join-Path $projectRoot 'target\tools\innosetup-6.7.4.exe'
        Get-VerifiedDownload -Uri 'https://github.com/jrsoftware/issrc/releases/download/is-6_7_4/innosetup-6.7.4.exe' -Destination $installer -ExpectedHash 'A549929319F9120716677C685535CA4671E4B937B5FDC87E2585F32D8EB30E8C'
        $signature = Get-AuthenticodeSignature -FilePath $installer
        if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notlike '*Pyrsys B.V.*') { throw 'Inno Setup 编译器签名验证失败。' }
        $process = Start-Process -FilePath $installer -ArgumentList @('/PORTABLE=1', '/CURRENTUSER', '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/NOICONS', '/TASKS=', "/DIR=`"$toolRoot`"") -Wait -PassThru -WindowStyle Hidden
        if ($process.ExitCode -ne 0) { throw 'Inno Setup 便携编译器解包失败。' }
    }
}
if (-not (Test-Path -LiteralPath $IsccPath -PathType Leaf)) { throw '未找到 Inno Setup 编译器。' }
$messages = Join-Path $toolRoot 'Languages\ChineseSimplified.isl'
Get-VerifiedDownload -Uri 'https://raw.githubusercontent.com/jrsoftware/issrc/6ef32198ef1f7b7b375cd4b6b90896c2a58eb4c2/Files/Languages/ChineseSimplified.isl' -Destination $messages -ExpectedHash 'E0B0B350E2245F3C5E65586DFE43D574F6E7F06F2261149ABA284954B3FC9A8D'
if (-not $SkipBuild) { & (Join-Path $PSScriptRoot 'build-windows.ps1') }
$package = Join-Path $projectRoot 'dist\windows'
foreach ($relative in @('ime-windows-service.exe', 'ZhimaMaintenance.exe', 'x64\ZhimaTsf.dll', 'x86\ZhimaTsf.dll', 'wubi.tsv', 'pinyin.tsv', 'README.md', 'LICENSE')) {
    if (-not (Test-Path -LiteralPath (Join-Path $package $relative) -PathType Leaf)) { throw "开发包不完整，缺少：$relative" }
}
$output = Join-Path $projectRoot 'dist'
& $IsccPath "/DPackageDir=$package" "/DOutputDir=$output" "/DChineseMessages=$messages" (Join-Path $projectRoot 'platforms\windows\installer\zhima.iss')
if ($LASTEXITCODE -ne 0) { throw 'Windows 安装包编译失败。' }
$setup = Join-Path $output 'ZhimaIME-0.1.0-windows-x64-setup.exe'
$hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $setup).Hash.ToLowerInvariant()
[IO.File]::WriteAllText("$setup.sha256", "$hash  $([IO.Path]::GetFileName($setup))`n", [Text.UTF8Encoding]::new($false))
Write-Host "已生成中文安装包：$setup；安装后提供 unins000.exe 和系统卸载入口。"
