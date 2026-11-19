#define WIN32_LEAN_AND_MEAN
#include "app.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

static void report(App *a, const char *s) { nc_copy(a->message, sizeof(a->message), s); }
static bool load_chart(App *a) {
    engine_free(&a->engine);
    chart_free(&a->chart);
    return chart_load(&a->lib, a->song, a->diff, &a->chart, true, a->message);
}
static bool preload(App *a, int song, int duration) {
    char path[NC_PATH];
    snprintf(path, sizeof(path), "%s/%s", a->lib.root, a->lib.songs[song].audio);
    return audio_open(path, a->settings.music, duration, a->message);
}
static void stop_run(App *a) {
    audio_close();
    engine_free(&a->engine);
    a->countdown = false;
}
static void leave_room(App *a) {
    net_leave(&a->net);
    stop_run(a);
}
static void abort_run(App *a, const char *why) {
    bool lan = a->net.state != NET_IDLE;
    leave_room(a);
    report(a, why);
    a->page = lan ? PAGE_LAN : PAGE_SELECT;
}
static void begin_run(App *a, int64_t target) {
    if (!engine_start(&a->engine, &a->chart, a->settings.offset)) {
        abort_run(a, "游戏引擎分配内存失败");
        return;
    }
    if (!target && !audio_play(a->message)) {
        abort_run(a, "音乐播放失败");
        return;
    }
    nc_uuid(a->session);
    a->saved = false;
    a->target = target;
    a->countdown = target != 0;
    a->page = PAGE_GAME;
    report(a, "");
}
static void solo_start(App *a) {
    if (a->chart.count && preload(a, a->song, a->chart.duration))
        begin_run(a, nc_now() + 3000);
    else {
        a->countdown = false;
        a->page = PAGE_SELECT;
    }
}
static void lan_start(App *a) {
    if (a->page != PAGE_LAN || a->net.state != NET_ROOM || !a->net.handshake ||
        !a->net.song[0] || !a->chart.count)
        return;
    if (!preload(a, a->song, a->chart.duration)) {
        chart_free(&a->chart);
        return;
    }
    begin_run(a, 0);
    if (a->page == PAGE_GAME)
        net_score(&a->net, 0, false);
}
static int music_time(const App *a) {
    int pos = audio_position();
    return pos < 0 || pos >= a->engine.now ? pos : a->engine.now;
}
static void pause_run(App *a) {
    if (a->net.state != NET_IDLE) {
        abort_run(a, "联机演奏中退出");
        return;
    }
    if (a->countdown) {
        abort_run(a, "已取消倒计时");
        return;
    }
    engine_tick(&a->engine, music_time(a));
    if (a->engine.state == PLAYING) {
        if (!audio_pause(a->message)) {
            abort_run(a, "音乐暂停失败");
            return;
        }
        engine_pause(&a->engine);
        report(a, "");
    }
}
static void resume_run(App *a) {
    if (a->engine.state == PAUSED) {
        a->engine.state = RESUME_READY;
        report(a, "请按住长按键");
    }
    if (engine_resume(&a->engine)) {
        if (!audio_resume(a->message)) {
            abort_run(a, "音乐恢复失败");
            return;
        }
        report(a, "");
    }
}
static void save_result(App *a) {
    char date[32];
    SYSTEMTIME t;
    GetSystemTime(&t);
    snprintf(date, sizeof(date), "%04u-%02u-%02uT%02u:%02u:%02uZ", t.wYear, t.wMonth, t.wDay,
             t.wHour, t.wMinute, t.wSecond);
    bool lan = a->net.state != NET_IDLE;
    Record r = {.session = a->session,
                .name = a->name,
                .date = date,
                .chart = &a->chart,
                .stats = &a->engine.stats,
                .match = lan ? a->session : NULL,
                .opponent = lan ? a->net.peer : NULL,
                .opponent_score = lan ? a->net.peer_score : 0};
    if (store_save(&a->store, &r, a->message)) {
        a->saved = true;
        report(a, "成绩已保存");
    }
}
static bool load_room_selection(App *a, int song, int diff) {
    Chart chart = {0};
    if (!chart_load(&a->lib, song, diff, &chart, false, a->message))
        return false;
    if (!preload(a, song, chart.duration)) {
        chart_free(&chart);
        return false;
    }
    chart_free(&a->chart);
    a->chart = chart;
    a->song = song;
    a->diff = diff;
    report(a, "");
    return true;
}
static void select_for_room(App *a, int song, int diff) {
    if (load_room_selection(a, song, diff) &&
        !net_configure(&a->net, a->lib.songs[song].id, diff))
        report(a, a->net.status);
}
static void follow_room_selection(App *a) {
    bool interrupted = a->page == PAGE_GAME || a->page == PAGE_RESULT;
    stop_run(a);
    chart_free(&a->chart);
    a->page = PAGE_LAN;
    a->diff = a->net.difficulty;
    for (int song = 0; song < a->lib.count; song++) {
        if (strcmp(a->lib.songs[song].id, a->net.song))
            continue;
        a->song = song;
        if (load_room_selection(a, song, a->diff) && interrupted)
            report(a, "房主已更改曲目或难度");
        return;
    }
    report(a, "找不到房主选择的曲目");
}
static void update_network(App *a) {
    Network *n = &a->net;
    if (n->state == NET_IDLE)
        return;
    bool disconnected = n->state == NET_ABORT;
    bool had_nickname = n->handshake, had_final_score = n->peer_final;
    int old_diff = n->difficulty;
    char old_song[sizeof(n->song)];
    memcpy(old_song, n->song, sizeof(old_song));
    net_poll(n);
    bool new_song = n->song[0] && (n->difficulty != old_diff || strcmp(old_song, n->song));
    if (!had_final_score && n->peer_final && !new_song && a->page == PAGE_RESULT)
        save_result(a);
    if (n->state == NET_ABORT) {
        if ((a->page == PAGE_GAME && !n->peer_final) ||
            (new_song && (a->page == PAGE_GAME || a->page == PAGE_RESULT))) {
            stop_run(a);
            a->page = PAGE_LAN;
            report(a, n->status);
        } else if (!disconnected) {
            if (a->page == PAGE_LAN)
                stop_run(a);
            report(a, n->status);
        }
        return;
    }
    if (n->host && !had_nickname && n->handshake)
        select_for_room(a, a->song, a->diff);
    else if (!n->host && new_song)
        follow_room_selection(a);
}
static void update_game(App *a) {
    if (a->page != PAGE_GAME)
        return;
    bool lan = a->net.state != NET_IDLE;
    if (a->countdown) {
        if (nc_now() < a->target)
            return;
        if (!audio_play(a->message)) {
            abort_run(a, "音乐播放失败");
            return;
        }
        a->countdown = false;
    }
    if (a->engine.state == PAUSED || a->engine.state == RESUME_READY)
        return;
    int now = music_time(a);
    if (now < 0) {
        abort_run(a, "音乐播放失败");
        return;
    }
    engine_tick(&a->engine, now);
    bool finished = a->engine.state == FINISHED && now >= a->chart.duration - 20;
    if (lan)
        net_score(&a->net, stats_result(&a->engine.stats, a->chart.count).score, finished);
    if (finished) {
        audio_close();
        if (!lan || a->net.peer_final)
            save_result(a);
        a->page = PAGE_RESULT;
    }
}
static void change_selection(App *a, int song, int diff) {
    if (song < 0)
        song = a->lib.count - 1;
    if (song >= a->lib.count)
        song = 0;
    if (a->page == PAGE_LAN) {
        if (a->net.host && a->net.handshake && a->net.state == NET_ROOM && !a->net.peer_started)
            select_for_room(a, song, diff);
        return;
    }
    a->song = song;
    a->diff = diff;
    if (a->page == PAGE_RANK)
        a->rank_page = 0;
    audio_close();
    if (load_chart(a))
        report(a, "");
}
static void action(App *a, Action act, int value) {
    switch (act) {
    case ACT_FOCUS:
        a->focus = value;
        break;
    case ACT_LOGIN_MODE:
        a->registering = !a->registering;
        report(a, "");
        break;
    case ACT_LOGIN:
    case ACT_REGISTER:
        if (account_action(a->data, a->name, a->password, act == ACT_REGISTER, a->message)) {
            a->password[0] = 0;
            if (act == ACT_REGISTER) {
                a->registering = false;
                report(a, "注册成功");
            } else {
                a->page = PAGE_HOME;
                a->focus = 0;
                report(a, "");
            }
        }
        break;
    case ACT_LAN:
        leave_room(a);
        a->page = PAGE_LAN;
        report(a, "");
        break;
    case ACT_SOLO:
    case ACT_RANK:
        leave_room(a);
        a->page = act == ACT_SOLO ? PAGE_SELECT : PAGE_RANK;
        a->rank_page = 0;
        if (load_chart(a))
            report(a, "");
        break;
    case ACT_LOGOUT:
        leave_room(a);
        a->page = PAGE_LOGIN;
        a->focus = 1;
        report(a, "");
        break;
    case ACT_QUIT:
        leave_room(a);
        a->page = PAGE_QUIT;
        break;
    case ACT_HOME:
        leave_room(a);
        a->page = PAGE_HOME;
        report(a, "");
        break;
    case ACT_SONG_PREV:
        change_selection(a, a->song - 1, a->diff);
        break;
    case ACT_SONG_NEXT:
        change_selection(a, a->song + 1, a->diff);
        break;
    case ACT_DIFF:
        change_selection(a, a->song, value);
        break;
    case ACT_PLAY:
    case ACT_RESTART:
        solo_start(a);
        break;
    case ACT_HOST:
    case ACT_JOIN: {
        char *end;
        long port = strtol(a->port, &end, 10);
        const char *ip = act == ACT_JOIN ? a->ip : NULL;
        if (*end || port < 1 || port > 65535)
            report(a, "端口范围是 1—65535");
        else if (net_open(&a->net, ip, (int)port, a->name, a->message)) {
            chart_free(&a->chart);
            a->focus = 0;
            report(a, ip ? "正在连接房主" : "等待加入");
        }
    } break;
    case ACT_NET_START:
        lan_start(a);
        break;
    case ACT_PAUSE:
        pause_run(a);
        break;
    case ACT_RESUME:
        resume_run(a);
        break;
    case ACT_ABORT:
        abort_run(a, "");
        break;
    case ACT_LEAVE_ROOM:
        leave_room(a);
        a->page = PAGE_LAN;
        report(a, "");
        break;
    case ACT_RANK_PREV:
        if (a->rank_page > 0)
            a->rank_page--;
        break;
    case ACT_RANK_NEXT:
        a->rank_page++;
        break;
    default:
        break;
    }
}
static void edit_character(App *a, uint32_t ch) {
    int first_focus = a->page == PAGE_LOGIN ? 1 : 3;
    if ((a->page != PAGE_LOGIN && a->page != PAGE_LAN) || a->focus < first_focus ||
        a->focus >= first_focus + 2)
        return;
    const struct {
        char *text;
        size_t capacity;
    } fields[] = {
        {a->name, sizeof(a->name)},
        {a->password, sizeof(a->password)},
        {a->ip, sizeof(a->ip)},
        {a->port, sizeof(a->port)},
    };
    char *field = fields[a->focus - 1].text;
    size_t cap = fields[a->focus - 1].capacity;
    size_t len = strlen(field);
    if (ch == 8) {
        if (len) {
            do {
                len--;
            } while (len && ((unsigned char)field[len] & 0xc0) == 0x80);
            field[len] = 0;
        }
        return;
    }
    if (ch < 32 || ch == 127 || (ch >= 0xd800 && ch <= 0xdfff))
        return;
    if (a->page == PAGE_LAN && (ch < '0' || ch > '9') && !(a->focus == 3 && ch == '.'))
        return;
    wchar_t w[2] = {(wchar_t)ch, 0};
    char u[8];
    int n = WideCharToMultiByte(CP_UTF8, 0, w, 1, u, sizeof(u), NULL, NULL);
    if (n > 0 && len + (size_t)n < cap) {
        memcpy(field + len, u, (size_t)n);
        field[len + n] = 0;
    }
}
static void input(App *a, const UiEvent *e) {
    switch (e->type) {
    case UI_CLOSE:
        action(a, ACT_QUIT, 0);
        return;
    case UI_MOVE:
        a->mx = e->x;
        a->my = e->y;
        return;
    case UI_FOCUS_LOST:
        if (a->page == PAGE_GAME) {
            if (a->net.state == NET_IDLE)
                pause_run(a);
            else
                for (int lane = 0; lane < 4; lane++)
                    if (a->engine.down[lane])
                        engine_key(&a->engine, lane, music_time(a), false);
        }
        return;
    case UI_CLICK: {
        Action act;
        int value;
        ui_action_at(a, e->x, e->y, &act, &value);
        action(a, act, value);
        return;
    }
    case UI_CHAR:
        edit_character(a, e->character);
        return;
    case UI_KEYDOWN:
        if (e->repeat)
            return;
        break;
    case UI_KEYUP:
        break;
    default:
        return;
    }
    bool down = e->type == UI_KEYDOWN;
    if (a->page == PAGE_LOGIN && down) {
        if (e->key == VK_TAB)
            a->focus = a->focus == 1 ? 2 : 1;
        if (e->key == VK_RETURN)
            action(a, a->registering ? ACT_REGISTER : ACT_LOGIN, 0);
        return;
    }
    if (a->page != PAGE_GAME) {
        if (down && e->key == VK_ESCAPE)
            action(a, ACT_HOME, 0);
        return;
    }
    if (down && e->key == VK_ESCAPE) {
        if (a->engine.state == PAUSED || a->engine.state == RESUME_READY)
            action(a, ACT_ABORT, 0);
        else
            pause_run(a);
        return;
    }
    if (down && e->key == VK_RETURN) {
        resume_run(a);
        return;
    }
    int lane = e->key == 'D' ? 0 : e->key == 'F' ? 1 : e->key == 'J' ? 2 : e->key == 'K' ? 3 : -1;
    if (lane < 0 || a->countdown)
        return;
    engine_key(&a->engine, lane, music_time(a), down);
    if (a->engine.state == RESUME_READY)
        resume_run(a);
}
int app_run(int argc, char **argv) {
    App *a = calloc(1, sizeof(*a));
    if (!a)
        return 1;
    char exe[NC_PATH], assets[NC_PATH], warning[NC_ERROR];
    nc_exe_dir(exe);
    snprintf(assets, sizeof(assets), "%s/assets/songs", exe);
    nc_data_dir(exe, a->data);
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--data-dir") && i + 1 < argc)
            nc_copy(a->data, sizeof(a->data), argv[++i]);
        else if (!strcmp(argv[i], "--assets") && i + 1 < argc)
            nc_copy(assets, sizeof(assets), argv[++i]);
    }
    warning[0] = 0;
    if (!settings_load(a->data, &a->settings, a->message))
        nc_copy(warning, sizeof(warning), a->message);
    if (!store_load(&a->store, a->data, a->message))
        nc_copy(warning, sizeof(warning), a->message);
    if (!library_load(&a->lib, assets, a->message) || !net_init(a->message)) {
        MessageBoxA(NULL, a->message, "音符碰撞", MB_ICONERROR);
        store_free(&a->store);
        free(a);
        return 1;
    }
    nc_copy(a->ip, sizeof(a->ip), "127.0.0.1");
    nc_copy(a->port, sizeof(a->port), "9527");
    net_addresses(a->addresses, sizeof(a->addresses));
    a->song = a->lib.count > 1 ? 1 : 0;
    a->page = PAGE_LOGIN;
    a->focus = 1;
    net_reset(&a->net);
    report(a, warning);
    if (!gfx_open(1280, 800, "音符碰撞")) {
        store_free(&a->store);
        net_shutdown();
        free(a);
        return 1;
    }
    ui_render(a);
    while (gfx_alive() && a->page != PAGE_QUIT) {
        UiEvent e;
        int n = 0;
        while (n++ < 128 && gfx_event(&e)) {
            input(a, &e);
            if (a->page == PAGE_QUIT)
                break;
        }
        update_network(a);
        update_game(a);
        ui_render(a);
        gfx_sleep(8);
    }
    audio_close();
    engine_free(&a->engine);
    chart_free(&a->chart);
    store_free(&a->store);
    net_leave(&a->net);
    net_shutdown();
    gfx_close();
    free(a);
    return 0;
}
int main(int argc, char **argv) { return app_run(argc, argv); }
