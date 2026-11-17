#ifndef SIMPLE_UI_H /* 防止重复包含界面声明。 */
#define SIMPLE_UI_H /* 定义界面头文件保护标记。 */
#define WIN32_LEAN_AND_MEAN /* 只引入必要的 Windows 类型。 */
#include <windows.h> /* 使用原生窗口类型，不依赖 EasyX 或 C++。 */

int ui_run(HINSTANCE instance, int show, int smokeTest); /* 创建窗口并运行图形消息循环。 */

#endif /* 界面声明结束。 */
