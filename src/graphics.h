#ifndef NC_GRAPHICS_H
#define NC_GRAPHICS_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum { UI_CLICK = 1, UI_KEYDOWN, UI_KEYUP, UI_CHAR, UI_FOCUS_LOST, UI_CLOSE, UI_MOVE };
typedef struct {
    int type, key, x, y, repeat;
    uint32_t character;
} UiEvent;
/**
 * @brief 创建 Qt 客户区并准备双缓冲绘制。
 * 返回：创建成功返回 1，否则返回 0。
 * 详见：qt_bridge.cpp 的同名函数定义（含参数、步骤及边界说明）。
 */
int gfx_open(int width, int height, const char *title);
/**
 * @brief 结束批量绘制并销毁图形窗口。
 * 返回：无返回值；模块窗口句柄清为空。
 * 详见：qt_bridge.cpp 的同名函数定义（含参数、步骤及边界说明）。
 */
void gfx_close(void);
/**
 * @brief 从 Qt 队列取出一个应用关心的事件。
 * 返回：取到映射的事件返回 1，没有可用映射事件返回 0。
 * 详见：qt_bridge.cpp 的同名函数定义（含参数、步骤及边界说明）。
 */
int gfx_event(UiEvent *e);
/**
 * @brief 根据当前文本框焦点启用或关闭输入法。
 * 参数：enabled 非零时允许文字输入，零时让游戏直接接收按键。
 * 说明：退出文本输入时清除未提交的组词；只影响本窗口，不修改系统输入法设置。
 */
void gfx_text_input(int enabled);
/**
 * @brief 设置背景颜色并清空当前绘图面。
 * 返回：无返回值。
 * 详见：qt_bridge.cpp 的同名函数定义（含参数、步骤及边界说明）。
 */
void gfx_clear(uint32_t color);
/**
 * @brief 绘制实心矩形或圆角矩形。
 * 返回：无返回值。
 * 详见：qt_bridge.cpp 的同名函数定义（含参数、步骤及边界说明）。
 */
void gfx_rect(int x, int y, int w, int h, uint32_t color, int radius);
/**
 * @brief 绘制两个客户区坐标之间的线段。
 * 返回：无返回值。
 * 详见：qt_bridge.cpp 的同名函数定义（含参数、步骤及边界说明）。
 */
void gfx_line(int x, int y, int x2, int y2, uint32_t color);
/**
 * @brief 按统一字体绘制 UTF-8 文本。
 * 返回：无返回值。
 * 详见：qt_bridge.cpp 的同名函数定义（含参数、步骤及边界说明）。
 */
void gfx_text(int x, int y, int size, uint32_t color, const char *text);
/**
 * @brief 在指定矩形内水平、垂直居中绘制文字。
 * 说明：使用字体实际度量，避免中文和英文按钮靠字符数估算而出现偏移。
 */
void gfx_text_center(int x, int y, int width, int height, int size, uint32_t color,
                     const char *text);
/**
 * @brief 把批量绘制缓冲区刷新到窗口。
 * 返回：无返回值。
 * 详见：qt_bridge.cpp 的同名函数定义（含参数、步骤及边界说明）。
 */
void gfx_present(void);
/**
 * @brief 短暂让出主线程 CPU。
 * 返回：无返回值。
 * 详见：qt_bridge.cpp 的同名函数定义（含参数、步骤及边界说明）。
 */
void gfx_sleep(int milliseconds);
/**
 * @brief 查询图形窗口是否仍是有效 Windows 窗口。
 * 返回：窗口存在且可见返回非零，否则返回 0。
 * 详见：qt_bridge.cpp 的同名函数定义（含参数、步骤及边界说明）。
 */
int gfx_alive(void);
/**
 * @brief 将当前绘图内容保存为图片。
 * 返回：无返回值；本接口不提供保存失败诊断。
 * 详见：qt_bridge.cpp 的同名函数定义（含参数、步骤及边界说明）。
 */
void gfx_capture(const char *path);
/**
 * @brief 通过 Windows 消息框显示致命错误。
 * 返回：无返回值，用户关闭对话框后继续调用方流程。
 * 详见：qt_bridge.cpp 的同名函数定义（含参数、步骤及边界说明）。
 */
void gfx_error(const char *text);
#ifdef __cplusplus
}
#endif
#endif
