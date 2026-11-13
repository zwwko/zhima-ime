; 中文安装器；注册、文件和启动项由安装事务及卸载日志管理。
#ifndef PackageDir
  #error 请指定开发包目录 PackageDir
#endif
#ifndef OutputDir
  #error 请指定输出目录 OutputDir
#endif
#ifndef ChineseMessages
  #error 请指定中文语言文件 ChineseMessages
#endif
#define AppVersion "0.1.0"
#ifdef InstallerTest
  #define ProductId "ZhimaIME.InstallerTest"
  #define ProductName "芝麻输入法安装隔离测试"
  #define StartupKey "Software\ZhimaIME\InstallerTest\Run"
  #define RegistryRoot "HKCU"
#else
  #define ProductId "ZhimaIME.Dev"
  #define ProductName "芝麻输入法（开发版）"
  #define StartupKey "Software\Microsoft\Windows\CurrentVersion\Run"
  #define RegistryRoot "HKLM"
#endif

[Setup]
AppId={#ProductId}
AppName={#ProductName}
AppVersion={#AppVersion}
AppPublisher=芝麻输入法项目
AppCopyright=芝麻输入法项目，Apache-2.0
DefaultDirName={autopf}\ZhimaIME
DefaultGroupName={#ProductName}
DisableDirPage=yes
DisableProgramGroupPage=yes
UsePreviousAppDir=no
#ifdef InstallerTest
PrivilegesRequired=lowest
#else
PrivilegesRequired=admin
#endif
ArchitecturesAllowed=x64os
ArchitecturesInstallIn64BitMode=x64os
MinVersion=10.0.22000
WizardStyle=modern
DisableWelcomePage=no
LicenseFile={#PackageDir}\LICENSE
OutputDir={#OutputDir}
OutputBaseFilename=ZhimaIME-0.1.0-windows-x64-setup
Compression=lzma2
SolidCompression=yes
UninstallDisplayName={#ProductName}
UninstallDisplayIcon={app}\ZhimaMaintenance.exe
UninstallFilesDir={app}
SetupLogging=yes
CloseApplications=no
RestartApplications=no
Uninstallable=yes
AllowNoIcons=yes
; 测试构建使用独立目录和 HKCU，不注册真实输入源，也不进入系统应用列表。
#ifdef InstallerTest
CreateUninstallRegKey=no
#endif

[Languages]
Name: "zhCN"; MessagesFile: "{#ChineseMessages}"

[Messages]
WelcomeLabel2=即将在您的计算机上安装芝麻输入法开发版。%n%n当前包含小型测试词库，适合开发验证。请先关闭使用本输入法的应用程序。
FinishedLabel=芝麻输入法已安装。请使用 Win+空格切换；若列表未刷新，请注销后重新登录。%n%n卸载可通过 Windows“已安装的应用”或开始菜单完成。

[Files]
Source: "{#PackageDir}\ime-windows-service.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#PackageDir}\ZhimaMaintenance.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#PackageDir}\x64\ZhimaTsf.dll"; DestDir: "{app}\x64"; Flags: ignoreversion regserver 64bit
Source: "{#PackageDir}\x86\ZhimaTsf.dll"; DestDir: "{app}\x86"; Flags: ignoreversion regserver 32bit
Source: "{#PackageDir}\wubi.tsv"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#PackageDir}\pinyin.tsv"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#PackageDir}\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#PackageDir}\README.md"; DestDir: "{app}"; Flags: ignoreversion

[Registry]
Root: {#RegistryRoot}; Subkey: "{#StartupKey}"; ValueType: string; ValueName: "ZhimaIME"; ValueData: """{app}\ime-windows-service.exe"""; Flags: uninsdeletevalue

[Icons]
#ifndef InstallerTest
Name: "{group}\启用芝麻输入法"; Filename: "{app}\ZhimaMaintenance.exe"; Parameters: "--enable-profile"
Name: "{group}\启动芝麻输入法引擎"; Filename: "{app}\ime-windows-service.exe"
Name: "{group}\使用说明"; Filename: "{sys}\notepad.exe"; Parameters: """{app}\README.md"""
Name: "{group}\卸载芝麻输入法"; Filename: "{uninstallexe}"
#endif

[Run]
#ifndef InstallerTest
Filename: "{app}\ZhimaMaintenance.exe"; Parameters: "--enable-profile"; StatusMsg: "正在为当前用户启用芝麻输入法……"; Flags: runasoriginaluser waituntilterminated
Filename: "{app}\ime-windows-service.exe"; Description: "启动芝麻输入法引擎"; Flags: postinstall nowait skipifsilent runasoriginaluser
#endif

[Code]
{ 开始写入之前拒绝覆盖旧版本和现有启动项，避免误接管脚本安装的版本。 }
function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := '';
  if DirExists(ExpandConstant('{app}')) then
    Result := '安装目录已存在。请先卸载旧版芝麻输入法，再重新安装。'
  else if RegValueExists({#RegistryRoot}, '{#StartupKey}', 'ZhimaIME') then
    Result := '已检测到芝麻输入法启动项。请先卸载旧版，避免覆盖现有配置。';
end;

{ 维护工具只定位自身目录内的引擎，失败时阻止继续删除安装文件。 }
procedure RunMaintenance(const Command: String);
var
  ResultCode: Integer;
begin
  if not Exec(ExpandConstant('{app}\ZhimaMaintenance.exe'), Command,
      ExpandConstant('{app}'), SW_HIDE, ewWaitUntilTerminated, ResultCode) then
    RaiseException('无法启动维护工具，已停止卸载。')
  else if ResultCode <> 0 then
    RaiseException('维护操作失败，已停止卸载。请关闭使用输入法的应用后重试。');
end;

{ 用户确认卸载后才停止引擎；预先验证注销，失败时保留文件供重试。 }
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then begin
    Log('正在停止安装目录中的芝麻输入法引擎。');
    RunMaintenance('--stop-engine');
#ifndef InstallerTest
    RunMaintenance('--disable-profile');
#endif
    if not UnregisterServer(False, ExpandConstant('{app}\x86\ZhimaTsf.dll'), False) then
      RaiseException('32 位输入源注销失败，文件已保留，请重试卸载。');
    if not UnregisterServer(True, ExpandConstant('{app}\x64\ZhimaTsf.dll'), False) then
      RaiseException('64 位输入源注销失败，文件已保留，请重试卸载。');
  end;
end;
