/* EasyX explicitly rejects C compilation. This file exposes only drawing/events
   through a C ABI; all application behavior lives in .c translation units. */
#include "graphics.h"
#include <easyx.h>
#include <string>
static HWND window;
/**
 * @brief 把 C 接口输入的 UTF-8 字符串转换为 Windows UTF-16 字符串。
 * 参数：s 为非空且有效的零结尾 UTF-8 文本。
 * 返回：返回拥有自身存储的 std::wstring，包含转换时分配的结尾零。
 * 说明：先查询所需长度再分配并转换，临时 c_str 指针仅在字符串生存期有效；本包装没有独立错误返回，调用方需保证输入有效。
 */
static std::wstring utf16(const char *s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    std::wstring result(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s, -1, result.data(), n);
    return result;
}
/**
 * @brief 将界面使用的 RGB 整数转为 Windows COLORREF。
 * 参数：c 为 0xRRGGBB 颜色，高于 24 位的部分忽略。
 * 返回：返回 RGB 宏构造的 COLORREF。
 * 说明：Windows COLORREF 的字节布局不同于项目书写的 RGB 常量，因此绘图入口统一转换，不直接把 c 传入
 * EasyX。
 */
static COLORREF color(uint32_t c) { return RGB(c >> 16 & 255, c >> 8 & 255, c & 255); }
/**
 * @brief 创建 EasyX 客户区并准备双缓冲绘制。
 * 参数：w/h 为客户区像素大小；title 为 UTF-8 窗口标题。
 * 返回：创建成功返回 1，否则返回 0。
 * 说明：保存模块唯一 HWND，设置标题与关闭菜单，开启 BeginBatchDraw 并用透明文本背景。图形资源由
 * gfx_close 释放；该文件只适配 C++ API，不持有游戏状态。
 */
extern "C" int gfx_open(int w, int h, const char *title) {
    window = initgraph(w, h, EX_NOCLOSE);
    if (!window)
        return 0;
    SetWindowTextW(window, utf16(title).c_str());
    // Close is handled by application state so an unfinished LAN result cannot be saved.
    EnableMenuItem(GetSystemMenu(window, FALSE), SC_CLOSE, MF_BYCOMMAND | MF_ENABLED);
    BeginBatchDraw();
    setbkmode(TRANSPARENT);
    return 1;
}
/**
 * @brief 结束批量绘制并销毁图形窗口。
 * 参数：无参数，预期在 gfx_open 成功后调用一次。
 * 返回：无返回值；模块窗口句柄清为空。
 * 说明：EndBatchDraw 会刷新剩余绘制，再 closegraph 关闭
 * EasyX；必须先结束应用循环，不能在窗口销毁后继续调用绘制函数。
 */
extern "C" void gfx_close(void) {
    EndBatchDraw();
    closegraph();
    window = nullptr;
}
/**
 * @brief 查询图形窗口是否仍是有效 Windows 窗口。
 * 参数：无参数，读取模块保存的 HWND。
 * 返回：存在且 IsWindow 成功返回非零，否则返回 0。
 * 说明：用于主循环退出判断，不表示窗口一定可见或在前台，也不表示程序业务处于运行状态。
 */
extern "C" int gfx_alive(void) { return window && IsWindow(window); }
/**
 * @brief 从 EasyX 队列取出一个应用关心的事件。
 * 参数：e 为非空 UiEvent 输出指针。
 * 返回：取到映射的事件返回 1，没有可用映射事件返回 0。
 * 说明：非阻塞丢弃不关心的消息，把鼠标、键盘、字符、失焦与关闭转换为 C 结构。每处理一个原始消息先清 e；prevdown
 * 映射为 repeat，是否忽略重复由应用层决定。
 */
extern "C" int gfx_event(UiEvent *e) {
    ExMessage m;
    while (peekmessage(&m)) {
        *e = {};
        switch (m.message) {
        case WM_LBUTTONDOWN:
            e->type = UI_CLICK;
            e->x = m.x;
            e->y = m.y;
            break;
        case WM_MOUSEMOVE:
            e->type = UI_MOVE;
            e->x = m.x;
            e->y = m.y;
            break;
        case WM_KEYDOWN:
            e->type = UI_KEYDOWN;
            e->key = m.vkcode;
            e->repeat = m.prevdown;
            break;
        case WM_KEYUP:
            e->type = UI_KEYUP;
            e->key = m.vkcode;
            break;
        case WM_CHAR:
            e->type = UI_CHAR;
            e->character = (uint32_t)m.ch;
            break;
        case WM_ACTIVATE:
            if (LOWORD(m.wParam) == WA_INACTIVE)
                e->type = UI_FOCUS_LOST;
            break;
        case WM_CLOSE:
            e->type = UI_CLOSE;
            break;
        }
        if (e->type)
            return 1;
    }
    return 0;
}
/**
 * @brief 设置背景颜色并清空当前绘图面。
 * 参数：c 为 0xRRGGBB 背景色，窗口需已创建。
 * 返回：无返回值。
 * 说明：操作 EasyX 内部绘图状态，尚未提交给显示时不保证屏幕立即变化；整帧完成后调用 gfx_present。
 */
extern "C" void gfx_clear(uint32_t c) {
    setbkcolor(color(c));
    cleardevice();
}
/**
 * @brief 绘制实心矩形或圆角矩形。
 * 参数：x/y 为左上角，w/h 为尺寸，c 为 RGB 颜色，radius 为 EasyX 圆角椭圆宽高参数，0
 * 表示普通矩形。
 * 返回：无返回值。
 * 说明：将宽高转换为右下坐标；radius 在此直接传给
 * EasyX，并非数学上再次乘二的圆半径；参数几何合法性由页面布局保证。
 */
extern "C" void gfx_rect(int x, int y, int w, int h, uint32_t c, int radius) {
    setfillcolor(color(c));
    if (radius)
        solidroundrect(x, y, x + w, y + h, radius, radius);
    else
        solidrectangle(x, y, x + w, y + h);
}
/**
 * @brief 绘制两个客户区坐标之间的线段。
 * 参数：x/y 与 x2/y2 为端点；c 为 RGB 颜色。
 * 返回：无返回值。
 * 说明：先设置 EasyX 当前线色再调用 line；沿用当前默认线宽，不改变应用层状态，也不登记输入区域。
 */
extern "C" void gfx_line(int x, int y, int x2, int y2, uint32_t c) {
    setlinecolor(color(c));
    line(x, y, x2, y2);
}
/**
 * @brief 按统一字体绘制 UTF-8 文本。
 * 参数：x/y 为绘制起点；size 为字体高度；c 为 RGB 文本色；text 为有效 UTF-8 字符串。
 * 返回：无返回值。
 * 说明：内部转换 UTF-16，设置 Microsoft YaHei UI 字体后调用
 * outtextxy；不自动换行、不测量边界，页面层负责文本长度与位置。
 */
extern "C" void gfx_text(int x, int y, int size, uint32_t c, const char *text) {
    settextcolor(color(c));
    settextstyle(size, 0, L"Microsoft YaHei UI");
    outtextxy(x, y, utf16(text).c_str());
}
/**
 * @brief 把批量绘制缓冲区刷新到窗口。
 * 参数：无参数，需已调用 gfx_open 开启批量绘制。
 * 返回：无返回值。
 * 说明：调用 FlushBatchDraw 但不结束批量模式，因此每一帧只需本函数，退出时再 gfx_close。
 */
extern "C" void gfx_present(void) { FlushBatchDraw(); }
/**
 * @brief 短暂让出主线程 CPU。
 * 参数：ms 为非负毫秒数，由调用者保证不能为负。
 * 返回：无返回值。
 * 说明：直接转为 DWORD 传给 Sleep，实际等待精度取决于 Windows
 * 调度；不将等待时长累加为游戏时间，也不承担音频同步。
 */
extern "C" void gfx_sleep(int ms) { Sleep((DWORD)ms); }
/**
 * @brief 将当前绘图内容保存为图片。
 * 参数：p 为 UTF-8 输出文件路径，格式由 EasyX 按路径扩展名处理。
 * 返回：无返回值；本接口不提供保存失败诊断。
 * 说明：转换为 UTF-16 后调用 saveimage；父目录应已存在，不改变当前页面或游戏状态，主要供人工验证时使用。
 */
extern "C" void gfx_capture(const char *p) { saveimage(utf16(p).c_str()); }
/**
 * @brief 通过 Windows 消息框显示致命错误。
 * 参数：s 为有效 UTF-8 错误说明。
 * 返回：无返回值，用户关闭对话框后继续调用方流程。
 * 说明：转换为 UTF-16，用当前窗口作父窗口；尚未创建图形窗口时父句柄可为空。此调用是模态阻塞的，常规每帧提示应使用
 * message/footer。
 */
extern "C" void gfx_error(const char *s) {
    MessageBoxW(window, utf16(s).c_str(), L"音符碰撞", MB_OK | MB_ICONERROR);
}
