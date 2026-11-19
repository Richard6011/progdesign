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
int gfx_open(int width, int height, const char *title);
void gfx_close(void);
int gfx_event(UiEvent *e);
void gfx_text_input(int enabled);
void gfx_clear(uint32_t color);
void gfx_rect(int x, int y, int w, int h, uint32_t color, int radius);
void gfx_line(int x, int y, int x2, int y2, uint32_t color);
void gfx_text(int x, int y, int size, uint32_t color, const char *text);
void gfx_text_center(int x, int y, int width, int height, int size, uint32_t color,
                     const char *text);
void gfx_present(void);
void gfx_sleep(int milliseconds);
int gfx_alive(void);
void gfx_error(const char *text);
#ifdef __cplusplus
}
#endif
#endif
