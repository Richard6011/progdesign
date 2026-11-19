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
    ACT_FOCUS,
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
    ACT_NET_START,
    ACT_PAUSE,
    ACT_RESUME,
    ACT_RESTART,
    ACT_ABORT,
    ACT_LEAVE_ROOM,
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
    bool registering, saved, countdown;
    int64_t target;
    char name[128], password[256], ip[64], port[8], data[NC_PATH], message[NC_ERROR], session[65],
        addresses[512];
    Button buttons[96];
    int button_count, mx, my;
} App;
void ui_render(App *a);
void ui_action_at(App *a, int x, int y, Action *action, int *value);
#endif
