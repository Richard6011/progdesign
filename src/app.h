#ifndef NC_APP_H
#define NC_APP_H
#include "graphics.h"
#include "nc.h"
#include "network.h"
typedef enum {
    PAGE_LOGIN,
    PAGE_HOME,
    PAGE_SELECT,
    PAGE_LAN,
    PAGE_GAME,
    PAGE_RESULT,
    PAGE_RANK,
    PAGE_QUIT
} Page;
typedef enum {
    ACT_NONE,
    ACT_LOGIN,
    ACT_REGISTER,
    ACT_LOGIN_MODE,
    ACT_FIELD_NAME,
    ACT_FIELD_PASSWORD,
    ACT_FIELD_IP,
    ACT_FIELD_PORT,
    ACT_SOLO,
    ACT_LAN,
    ACT_RANK,
    ACT_QUIT,
    ACT_HOME,
    ACT_SONG_PREV,
    ACT_SONG_NEXT,
    ACT_DIFF,
    ACT_PLAY,
    ACT_HOST,
    ACT_JOIN,
    ACT_READY,
    ACT_NET_START,
    ACT_PAUSE,
    ACT_RESUME,
    ACT_RESTART,
    ACT_ABORT,
    ACT_SAVE,
    ACT_REMATCH,
    ACT_LOGOUT,
    ACT_RANK_PREV,
    ACT_RANK_NEXT
} Action;
typedef struct {
    int x, y, w, h;
    Action action;
    int value;
} Button;
typedef struct {
    Library lib;
    Store store;
    Settings settings;
    Network net;
    Chart chart;
    Engine engine;
    Page page;
    int song, diff, rank_page, focus;
    bool registering, lan_mode, saved, submitted, started;
    bool media_ready, countdown, settings_writable, run_active;
    int loaded_revision;
    int64_t target, last_audio_change;
    int last_audio_position;
    char name[128], password[256], ip[64], port[8], data[NC_PATH], message[NC_ERROR], session[65],
        addresses[512];
    Button buttons[96];
    int button_count, mx, my;
    int64_t quit_at;
} App;
/**
 * @brief 按当前页面生成一帧界面与新的按钮命中表。
 * 返回：无返回值；清零并重建按钮表，最后提交 Qt 离屏画布。
 * 详见：ui.c 的同名函数定义（含参数、步骤及边界说明）。
 */
void ui_render(App *a);
/**
 * @brief 把客户区点击坐标映射到上一帧登记的按钮动作。
 * 返回：无返回值；命中则写动作及附参，无命中写 ACT_NONE 和 0。
 * 详见：ui.c 的同名函数定义（含参数、步骤及边界说明）。
 */
void ui_action_at(App *a, int x, int y, Action *action, int *value);
#endif
