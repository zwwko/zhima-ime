<# 使用同一安装定义和模拟 DLL 验证事务；不注册真实 TSF，不改系统输入法列表。 #>
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$workRoot = Join-Path $projectRoot ('target\installer-tests\' + [Guid]::NewGuid().ToString('N'))
$payload = Join-Path $workRoot 'payload'
$app = Join-Path $workRoot '中文安装目录'
$testRegistry = 'HKCU:\Software\ZhimaIME\InstallerTest\Run'
if (Get-ItemProperty -LiteralPath $testRegistry -Name 'ZhimaIME' -ErrorAction SilentlyContinue) { throw '已有隔离安装测试状态，请先完成上次测试的卸载。' }
foreach ($name in @('ZHIMA_TEST_FAIL_REGISTER_X86', 'ZHIMA_TEST_FAIL_UNREGISTER_X86')) {
    if ([Environment]::GetEnvironmentVariable($name)) { throw "测试环境变量已存在，请先核对：$name" }
}

function Assert-Installer {
    <# 验证安装边界，失败保留日志供定位。 #>
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) { throw "$Message；测试记录：$workRoot" }
}
function Invoke-InstallerProcess {
    <# 等待安装器或卸载器完整退出，返回真实进程退出码。 #>
    param([string]$File, [string[]]$Arguments)
    $process = Start-Process -FilePath $File -ArgumentList $Arguments -PassThru -Wait -WindowStyle Hidden
    return $process.ExitCode
}
function Invoke-TestUninstall {
    <# 无交互卸载当前测试目录，不接受外部路径。 #>
    param([string]$LogName)
    return Invoke-InstallerProcess -File (Join-Path $app 'unins000.exe') -Arguments @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', "/LOG=`"$(Join-Path $workRoot $LogName)`"")
}

New-Item -ItemType Directory -Force -Path $payload, (Join-Path $payload 'x64'), (Join-Path $payload 'x86') | Out-Null
$iscc = Join-Path $projectRoot 'target\tools\InnoSetup\ISCC.exe'
$messages = Join-Path $projectRoot 'target\tools\InnoSetup\Languages\ChineseSimplified.isl'
Assert-Installer (Test-Path -LiteralPath $iscc) '请先构建 Windows 安装包以准备编译器'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsRoot = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$vcvars = Join-Path $vsRoot 'VC\Auxiliary\Build\vcvarsall.bat'
Push-Location $projectRoot
try {
    foreach ($arch in @('x64', 'x86')) {
        $archRoot = Join-Path $payload $arch
        $batch = Join-Path $workRoot "build-$arch.cmd"
        $compile = "`"$vcvars`" $arch && cl /nologo /std:c++17 /utf-8 /EHsc /MT /W4 /WX /DUNICODE /D_UNICODE /LD platforms\windows\installer\registration-fixture.cpp /Fo`"$archRoot\fixture.obj`" /link /DEF:platforms\windows\installer\fixture.def /OUT:`"$archRoot\ZhimaTsf.dll`" /IMPLIB:`"$archRoot\fixture.lib`""
        [IO.File]::WriteAllText($batch, "@echo off`r`nchcp 65001 >nul`r`n$compile`r`n", [Text.UTF8Encoding]::new($false))
        & $env:ComSpec /d /c $batch
        Assert-Installer ($LASTEXITCODE -eq 0) "$arch 模拟注册组件编译失败"
    }
    $batch = Join-Path $workRoot 'build-engine.cmd'
    $compile = "`"$vcvars`" x64 && cl /nologo /std:c++17 /utf-8 /EHsc /MT /W4 /WX platforms\windows\installer\engine-fixture.cpp /Fo`"$workRoot\engine.obj`" /Fe`"$payload\ime-windows-service.exe`" /link /SUBSYSTEM:WINDOWS"
    [IO.File]::WriteAllText($batch, "@echo off`r`nchcp 65001 >nul`r`n$compile`r`n", [Text.UTF8Encoding]::new($false))
    & $env:ComSpec /d /c $batch
    Assert-Installer ($LASTEXITCODE -eq 0) '模拟引擎编译失败'
    foreach ($name in @('ZhimaMaintenance.exe', 'wubi.tsv', 'pinyin.tsv', 'README.md', 'LICENSE')) {
        Copy-Item -LiteralPath (Join-Path $projectRoot "dist\windows\$name") -Destination $payload
    }
    & $iscc /Q "/DPackageDir=$payload" "/DOutputDir=$workRoot" "/DChineseMessages=$messages" /DInstallerTest (Join-Path $projectRoot 'platforms\windows\installer\zhima.iss')
    Assert-Installer ($LASTEXITCODE -eq 0) '隔离安装器编译失败'
    $setup = Join-Path $workRoot 'ZhimaIME-0.1.0-windows-x64-setup.exe'
    $installArgs = @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', "/DIR=`"$app`"")
    # 先在第二种架构注册时注入故障，检查第一种架构、文件和启动项均回滚。
    $env:ZHIMA_TEST_FAIL_REGISTER_X86 = '1'
    $code = Invoke-InstallerProcess $setup ($installArgs + "/LOG=`"$workRoot\failed-install.log`"")
    $env:ZHIMA_TEST_FAIL_REGISTER_X86 = $null
    Assert-Installer ($code -ne 0) '注册失败未阻止安装完成'
    Assert-Installer (-not (Test-Path -LiteralPath (Join-Path $app 'ime-windows-service.exe'))) '安装失败后未回滚程序文件'
    Assert-Installer (-not (Get-ItemProperty -LiteralPath $testRegistry -Name 'ZhimaIME' -ErrorAction SilentlyContinue)) '安装失败后残留启动项'
    $events = Get-Content -LiteralPath (Join-Path $workRoot 'registration-events.log') -Raw -Encoding utf8
    Assert-Installer ($events.Contains('注册64') -and $events.Contains('注册32') -and $events.Contains('注销64')) '未验证两种位数的注册及回滚'
    Assert-Installer (-not (Test-Path -LiteralPath $app)) '安装回滚后测试目录未清理'
    Write-Host '已验证：注册失败回滚、两种位数注册和中文路径。'

    $code = Invoke-InstallerProcess $setup ($installArgs + "/LOG=`"$workRoot\install.log`"")
    Assert-Installer ($code -eq 0) '全新安装失败'
    Assert-Installer ((Test-Path -LiteralPath (Join-Path $app 'x64\registered.flag')) -and (Test-Path -LiteralPath (Join-Path $app 'x86\registered.flag'))) '未完成两种位数注册'
    $startup = (Get-ItemProperty -LiteralPath $testRegistry -Name 'ZhimaIME').ZhimaIME
    Assert-Installer ($startup -eq ('"' + (Join-Path $app 'ime-windows-service.exe') + '"')) '启动项路径或引号不正确'
    $code = Invoke-InstallerProcess $setup ($installArgs + "/LOG=`"$workRoot\duplicate-install.log`"")
    Assert-Installer ($code -ne 0) '未阻止覆盖已有安装'
    Write-Host '已验证：完整安装、启动项和重复安装拦截。'

    # 用户数据样本放在安装树以外；卸载不得遍历或删除该位置。
    $userData = Join-Path $workRoot '用户数据样本.db'
    [IO.File]::WriteAllText($userData, '应保留的用户词库样本')
    $foreignRoot = Join-Path $workRoot '其他目录'
    New-Item -ItemType Directory -Path $foreignRoot | Out-Null
    Copy-Item -LiteralPath (Join-Path $payload 'ime-windows-service.exe') -Destination $foreignRoot
    $foreign = Start-Process -FilePath (Join-Path $foreignRoot 'ime-windows-service.exe') -PassThru -WindowStyle Hidden
    $installed = Start-Process -FilePath (Join-Path $app 'ime-windows-service.exe') -PassThru -WindowStyle Hidden
    $env:ZHIMA_TEST_FAIL_UNREGISTER_X86 = '1'
    $code = Invoke-TestUninstall 'failed-uninstall.log'
    $env:ZHIMA_TEST_FAIL_UNREGISTER_X86 = $null
    Assert-Installer ($code -ne 0) '注销失败未阻止卸载完成'
    Assert-Installer (Test-Path -LiteralPath (Join-Path $app 'x64\ZhimaTsf.dll')) '注销失败后错误删除了程序文件'
    $installed.Refresh(); $foreign.Refresh()
    Assert-Installer ($installed.HasExited -and -not $foreign.HasExited) '未按完整路径隔离引擎停止操作'
    Write-Host '已验证：注销失败保留文件、仅停止安装路径对应的引擎。'

    $code = Invoke-TestUninstall 'uninstall.log'
    Assert-Installer ($code -eq 0) '重试卸载失败'
    # 卸载器自身删除由退出后的清理进程执行，短时等待文件系统完成。
    for ($attempt = 0; $attempt -lt 30 -and (Test-Path -LiteralPath $app); $attempt++) { Start-Sleep -Milliseconds 100 }
    Assert-Installer (-not (Test-Path -LiteralPath $app)) '卸载后安装目录仍有残留'
    Assert-Installer (-not (Get-ItemProperty -LiteralPath $testRegistry -Name 'ZhimaIME' -ErrorAction SilentlyContinue)) '卸载后残留启动项'
    Assert-Installer ([IO.File]::ReadAllText($userData) -eq '应保留的用户词库样本') '卸载影响了安装树外的用户数据'
    $foreign.Refresh(); Assert-Installer (-not $foreign.HasExited) '卸载影响了其他目录中的同名进程'
    Write-Host "安装与卸载隔离测试通过。日志：$workRoot"
} finally {
    $env:ZHIMA_TEST_FAIL_REGISTER_X86 = $null
    $env:ZHIMA_TEST_FAIL_UNREGISTER_X86 = $null
    foreach ($process in @($installed, $foreign)) { if ($process -and -not $process.HasExited) { Stop-Process -Id $process.Id } }
    Pop-Location
}
