// 仅用于测试按完整路径停止进程，不包含于正式开发安装包。
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
/** 保持运行，供卸载测试确认目标进程退出而其他目录进程不受影响。 */
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) { Sleep(INFINITE); return 0; }
