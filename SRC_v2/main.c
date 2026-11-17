#include "ui.h" /* 引入界面入口。 */
#include "game.h" /* 设置程序自己的数据目录。 */
#include <wchar.h> /* 使用宽字符路径和命令行检查。 */

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR commandLine, int show) /* 图形程序入口。 */
{ /* 函数开始。 */
    wchar_t path[MAX_PATH]; /* 保存本程序的完整路径。 */
    wchar_t *separator; /* 指向程序文件名前的最后一个反斜杠。 */
    int smokeTest; /* 自动界面检查时短时间后关闭。 */
    DWORD length; /* 保存系统返回的路径长度。 */
    (void)previous; /* Windows 保留参数，本程序不使用。 */
    length = GetModuleFileNameW(NULL, path, MAX_PATH); /* 不依赖用户从哪个文件夹双击程序。 */
    if (length == 0 || length >= MAX_PATH) /* 系统路径不可用或过长。 */
        return 1; /* 不在意外目录中创建存档。 */
    separator = wcsrchr(path, L'\\'); /* 找到程序所在目录。 */
    if (separator == NULL) /* 正常完整 Windows 路径应具有分隔符。 */
        return 1; /* 无法确定目录时退出。 */
    *separator = 0; /* 去掉可执行文件名。 */
    if (!SetCurrentDirectoryW(path)) /* 将资源和存档都定位到程序自己的文件夹。 */
        return 1; /* 无法切换目录时退出。 */
    storage_folder("data"); /* 正式账号、设置和成绩保存在第二版 data 目录。 */
    if (wcsstr(commandLine, L"--test-mode") != NULL) /* 自动测试不能写入正式账号。 */
        storage_folder("ui_test_data"); /* 测试使用独立存档。 */
    smokeTest = wcsstr(commandLine, L"--smoke-test") != NULL; /* 是否只短暂打开界面。 */
    return ui_run(instance, show, smokeTest); /* 进入原生窗口程序。 */
} /* 函数结束。 */
