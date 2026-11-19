#include "app.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static const uint32_t BG = 0xf0f0f0, PANEL = 0xffffff, CARD = 0xe6e6e6, TEXT = 0x202020,
                      MUTED = 0x666666, BORDER = 0xb8b8b8, BLUE = 0xdce9f7,
                      CYAN = 0x245a81, PINK = 0x73576d, GREEN = 0x356443, RED = 0xb03030;
static const uint32_t lanes[4] = {0x6899b4, 0x7e8fb6, 0x9985ad, 0xb18698};
static void label(int x, int y, int size, uint32_t c, const char *s) { gfx_text(x, y, size, c, s); }
static void label_format(int x, int y, int size, uint32_t color, const char *format, ...) {
    char text[256];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    label(x, y, size, color, text);
}
static bool hover(const App *a, int x, int y, int w, int h) {
    return a->mx >= x && a->mx < x + w && a->my >= y && a->my < y + h;
}
static void button(App *a, int x, int y, int w, int h, const char *s, Action act, int val,
                   bool enabled, bool primary) {
    uint32_t c = enabled ? (primary ? BLUE : CARD) : 0xededed;
    if (enabled && hover(a, x, y, w, h))
        c = primary ? 0xcbdff3 : 0xd9d9d9;
    gfx_rect(x, y, w, h, enabled && primary ? 0x7c9bb9 : BORDER, 4);
    gfx_rect(x + 1, y + 1, w - 2, h - 2, c, 2);
    gfx_text_center(x, y, w, h, 20, enabled ? TEXT : 0x888888, s);
    if (enabled && a->button_count < 96)
        a->buttons[a->button_count++] = (Button){x, y, w, h, act, val};
}
static void btn(App *a, int x, int y, int w, const char *s, Action act) {
    button(a, x, y, w, 52, s, act, 0, true, false);
}
static void field(App *a, int x, int y, int w, const char *value, const char *hint, int focus,
                  bool secret) {
    gfx_rect(x, y, w, 54, a->focus == focus ? CYAN : BORDER, 2);
    gfx_rect(x + 1, y + 1, w - 2, 52, PANEL, 0);
    char display[256];
    nc_copy(display, sizeof(display), value);
    if (secret) {
        size_t n = 0;
        for (size_t i = 0; value[i]; i++)
            if (((unsigned char)value[i] & 0xc0) != 0x80)
                n++;
        if (n > 60)
            n = 60;
        memset(display, '*', n);
        display[n] = 0;
    }
    if (a->focus == focus && (nc_now() / 500) % 2 && strlen(display) < sizeof(display) - 2)
        strcat(display, "|");
    label(x + 16, y + 16, 20, value[0] ? TEXT : MUTED,
          value[0] ? display : (a->focus == focus ? display : hint));
    a->buttons[a->button_count++] = (Button){x, y, w, 54, ACT_FOCUS, focus};
}
static void heading(App *a, const char *s) {
    gfx_clear(BG);
    label(48, 30, 32, TEXT, s);
    if (a->page != PAGE_LOGIN && a->page != PAGE_HOME && a->page != PAGE_GAME)
        btn(a, 1064, 36, 168, "返回主页", ACT_HOME);
    gfx_line(48, 104, 1232, 104, BORDER);
}
static void footer(App *a) {
    if (!a->message[0])
        return;
    char line[512];
    size_t pos = 0, len = strlen(a->message);
    int row = 0;
    while (pos < len && row < 2) {
        size_t end = pos, units = 0;
        while (end < len && units < 100) {
            size_t step = ((unsigned char)a->message[end] < 128)
                              ? 1
                              : (((unsigned char)a->message[end] & 0xe0) == 0xc0 ? 2 : 3);
            if (end + step > len)
                step = 1;
            units += step == 1 ? 1 : 2;
            end += step;
        }
        size_t n = end - pos;
        if (n >= sizeof(line))
            n = sizeof(line) - 1;
        memcpy(line, a->message + pos, n);
        line[n] = 0;
        label(50, 750 + row * 22, 16, MUTED, line);
        pos = end;
        row++;
    }
}
static void login(App *a) {
    heading(a, "音符碰撞");
    gfx_rect(376, 150, 528, 542, PANEL, 4);
    gfx_text_center(416, 174, 448, 42, 28, TEXT, a->registering ? "注册" : "登录");
    label(416, 242, 18, TEXT, "账号");
    field(a, 416, 274, 448, a->name, "账号", 1, false);
    label(416, 354, 18, TEXT, "密码");
    field(a, 416, 386, 448, a->password, "密码", 2, true);
    button(a, 416, 474, 448, 54, a->registering ? "注册" : "登录",
           a->registering ? ACT_REGISTER : ACT_LOGIN, 0, true, true);
    btn(a, 416, 548, 448, a->registering ? "返回登录" : "注册账号", ACT_LOGIN_MODE);
    btn(a, 416, 622, 448, "退出", ACT_QUIT);
}
static void home(App *a) {
    gfx_clear(BG);
    gfx_text_center(0, 94, 1280, 58, 36, TEXT, "音符碰撞");
    gfx_text_center(0, 170, 1280, 32, 20, MUTED, a->name);
    const int x = (1280 - 360) / 2, w = 360, h = 54, gap = 20;
    const char *names[] = {"单人模式", "局域网双人", "排行榜", "切换账号", "退出"};
    const Action actions[] = {ACT_SOLO, ACT_LAN, ACT_RANK, ACT_LOGOUT, ACT_QUIT};
    for (int i = 0; i < 5; i++)
        button(a, x, 248 + i * (h + gap), w, h, names[i], actions[i], 0, true, i == 0);
}
static void selection(App *a, int x, int y, bool editable) {
    Song *s = &a->lib.songs[a->song];
    bool selected = a->page != PAGE_LAN || a->net.host || !strcmp(s->id, a->net.song);
    gfx_rect(x, y, 716, 170, PANEL, 4);
    if (selected)
        label(x + 32, y + 54, 32, TEXT, s->title);
    button(a, x + 522, y + 42, 72, 48, "<", ACT_SONG_PREV, 0, editable, false);
    button(a, x + 612, y + 42, 72, 48, ">", ACT_SONG_NEXT, 0, editable, false);
    for (int d = 0; d < 3; d++)
        button(a, x + d * 244, y + 192, 228, 52, nc_difficulties[d], ACT_DIFF, d, editable,
               selected && d == a->diff);
}
static void select_page(App *a) {
    heading(a, "选曲");
    selection(a, 282, 180, true);
    button(a, 282, 492, 716, 58, "开始", ACT_PLAY, 0, a->chart.count > 0, true);
}
static void lobby(App *a) {
    bool connected = a->net.state == NET_ROOM && a->net.handshake,
         free_room = a->net.state == NET_IDLE || a->net.state == NET_ABORT;
    heading(a, "局域网双人");
    if (free_room || a->net.state == NET_CONNECT || a->net.state == NET_LISTEN) {
        gfx_rect(160, 163, 960, 440, PANEL, 4);
        label(200, 200, 18, TEXT, "房主 IPv4");
        field(a, 200, 234, 550, a->ip, "IPv4", 3, false);
        label(782, 200, 18, TEXT, "端口");
        field(a, 782, 234, 298, a->port, "9527", 4, false);
        button(a, 200, 326, 420, 54, "创建房间", ACT_HOST, 0, free_room, true);
        button(a, 660, 326, 420, 54, "加入房间", ACT_JOIN, 0, free_room, false);
        label(200, 424, 18, MUTED, "本机 IPv4");
        char addresses[sizeof(a->addresses)];
        nc_copy(addresses, sizeof(addresses), a->addresses);
        char *split = addresses;
        int separators = 0;
        while (*split && separators < 3)
            if (*split++ == '/')
                separators++;
        if (separators == 3) {
            split[-1] = 0;
            label(200, 462, 17, TEXT, addresses);
            label(200, 494, 17, TEXT, split);
        } else
            label(200, 462, 17, TEXT, addresses);
        label(200, 550, 17, MUTED, a->net.status);
    } else {
        selection(a, 48, 153, connected && a->net.host && !a->net.peer_started);
        gfx_rect(806, 153, 426, 382, PANEL, 4);
        label_format(838, 192, 20, TEXT, "%s：%s", a->net.host ? "房主" : "客人", a->name);
        label_format(838, 242, 20, TEXT, "对方：%s", a->net.peer);
        button(a, 838, 438, 362, 58, "开始", ACT_NET_START, 0,
               connected && a->net.song[0] && a->chart.count > 0, true);
    }
}
static void game(App *a) {
    gfx_clear(BG);
    const int left = 350, width = 580, top = 128, bottom = 635;
    Result r = stats_result(&a->engine.stats, a->chart.count);
    label(42, 30, 27, TEXT, a->lib.songs[a->song].title);
    label(44, 74, 17, MUTED, a->chart.difficulty);
    label_format(44, 164, 35, CYAN, "%07d", r.score);
    label(44, 214, 17, MUTED, "分数");
    label_format(44, 286, 50, TEXT, "%d", a->engine.stats.combo);
    label(44, 346, 18, MUTED, "连击");
    label_format(44, 409, 28, GREEN, "%.2f%%", r.accuracy * 100);
    label(44, 451, 17, MUTED, "准确率");
    gfx_rect(left - 3, top - 3, width + 6, bottom - top + 60, BORDER, 4);
    for (int l = 0; l < 4; l++) {
        int x = left + l * width / 4;
        gfx_rect(x, top, width / 4 - 2, bottom - top + 55, a->engine.down[l] ? 0xdce9f7 : PANEL,
                 0);
        gfx_rect(x + 12, bottom + 15, width / 4 - 26, 37, a->engine.down[l] ? lanes[l] : CARD, 7);
        label(x + 60, bottom + 20, 23, a->engine.down[l] ? BG : TEXT,
              (const char *[]){"D", "F", "J", "K"}[l]);
    }
    int time = a->countdown ? (int)(nc_now() - a->target) : a->engine.now;
    for (int i = 0; i < a->chart.count; i++) {
        Note *n = &a->chart.notes[i];
        Runtime *rt = &a->engine.runtime[i];
        if (rt->state == HIT || rt->state == MISSED)
            continue;
        int x = left + n->lane * width / 4 + 10, w = width / 4 - 23;
        int y = bottom - (int)((int64_t)(n->time - time) * (bottom - top) / a->chart.approach);
        int tail = bottom - (int)((int64_t)(n->end - time) * (bottom - top) / a->chart.approach);
        if (n->type == HOLD) {
            if (y < top || tail > bottom + 12)
                continue;
            int head = rt->state == HOLDING ? bottom : y;
            if (tail < top)
                tail = top;
            if (head > bottom)
                head = bottom;
            if (head >= top && head >= tail)
                gfx_rect(x + 22, tail, w - 44, head - tail, 0xa9c3ce, 6);
            if (y >= top && y <= bottom + 20)
                gfx_rect(x, y - 7, w, 15, CYAN, 5);
        } else {
            if (y < top || y > bottom + 20)
                continue;
            gfx_rect(x, y - 7, w, 15, n->type == CHAIN ? 0xb39a5a : lanes[n->lane], 6);
            if (n->chord)
                gfx_rect(x + w / 2 - 12, y - 3, 24, 5, TEXT, 2);
        }
    }
    gfx_rect(left, bottom, width, 3, PINK, 0);
    if (a->engine.last_time >= 0 && time - a->engine.last_time < 650) {
        const char *names[] = {"PERFECT", "GREAT", "GOOD", "MISS"};
        label(546, 292, 31, a->engine.last == MISS ? RED : CYAN, names[a->engine.last]);
    }
    if (a->net.state != NET_IDLE) {
        label(970, 163, 20, MUTED, "对方分数");
        label_format(970, 205, 28, PINK, "%07d", a->net.peer_score);
        label(970, 254, 20, TEXT, a->net.peer);
        btn(a, 970, 571, 265, "退出本局", ACT_ABORT);
    } else {
        btn(a, 970, 477, 265, "暂停 / Esc", ACT_PAUSE);
        btn(a, 970, 543, 265, "重新开始", ACT_RESTART);
        btn(a, 970, 609, 265, "返回选曲", ACT_ABORT);
    }
    double progress = a->chart.duration ? (double)a->engine.now / a->chart.duration : 0;
    if (progress > 1)
        progress = 1;
    gfx_rect(350, 710, width, 5, CARD, 2);
    gfx_rect(350, 710, (int)(width * progress), 5, CYAN, 2);
    label_format(350, 725, 16, MUTED, "%.1f / %.1f 秒", a->engine.now / 1000.0,
                 a->chart.duration / 1000.0);
    if (a->countdown) {
        gfx_rect(452, 275, 374, 158, PANEL, 16);
        int seconds = (int)((a->target - nc_now() + 999) / 1000);
        label_format(612, 293, 58, CYAN, "%d", seconds > 0 ? seconds : 1);
    }
    if (a->engine.state == PAUSED || a->engine.state == RESUME_READY) {
        gfx_rect(418, 264, 446, 242, PANEL, 14);
        label(532, 288, 31, TEXT, "已暂停");
        button(a, 450, 393, 382, 56,
               a->engine.state == RESUME_READY ? "按住长按键继续" : "继续 · Enter",
               ACT_RESUME, 0, true, true);
    }
}
static void result(App *a) {
    heading(a, "成绩");
    Result r = stats_result(&a->engine.stats, a->chart.count);
    bool lan = a->net.state != NET_IDLE;
    gfx_rect(48, 153, 716, 248, PANEL, 14);
    label_format(80, 190, 99, CYAN, "%c", r.grade);
    label_format(245, 197, 53, TEXT, "%07d", r.score);
    label_format(245, 270, 22, MUTED, "准确率 %.2f%%    最大连击 %d", r.accuracy * 100,
                 a->engine.stats.max_combo);
    label(245, 322, 21, GREEN,
          r.ap         ? "ALL PERFECT"
          : r.fc       ? "FULL COMBO"
          : r.complete ? "演奏完成"
                       : "未完成");
    const char *names[] = {"Perfect", "Great", "Good", "Miss"};
    for (int i = 0; i < 4; i++) {
        gfx_rect(48 + i * 182, 430, 170, 105, PANEL, 10);
        label(65 + i * 182, 445, 18, MUTED, names[i]);
        label_format(65 + i * 182, 479, 28, TEXT, "%d", a->engine.stats.hits[i]);
    }
    gfx_rect(806, 153, 426, 382, PANEL, 14);
    label(840, 190, 24, TEXT, lan ? "对战结果" : "本局信息");
    label(840, 247, 21, TEXT, a->lib.songs[a->song].title);
    label(840, 286, 18, MUTED, a->chart.difficulty);
    if (lan) {
        int peer = a->net.peer_score;
        const char *outcome = r.score == peer ? "平 局" : (r.score > peer ? "胜 利" : "落 败");
        if (a->net.peer_final)
            label(840, 333, 27, CYAN, outcome);
        else if (a->net.state == NET_ROOM)
            label(840, 333, 27, MUTED, a->net.peer_started ? "演奏中" : "未开始");
        label_format(840, 382, 19, TEXT, "对方 %s / %d 分", a->net.peer, peer);
    }
    label(840, 454, 19, a->saved ? GREEN : MUTED, a->saved ? "已保存" : "未保存");
    button(a, 48, 585, 340, 58, lan ? "返回房间" : "重新演奏",
           lan ? ACT_LEAVE_ROOM : ACT_RESTART, 0, true, true);
}
static void ranking(App *a) {
    heading(a, "排行榜");
    label(48, 150, 25, TEXT, a->lib.songs[a->song].title);
    btn(a, 670, 145, 70, "<", ACT_SONG_PREV);
    btn(a, 754, 145, 70, ">", ACT_SONG_NEXT);
    for (int d = 0; d < 3; d++)
        button(a, 48 + d * 150, 211, 138, 45, nc_difficulties[d], ACT_DIFF, d, true, d == a->diff);
    RankRow *rows = malloc((size_t)(a->store.count + 1) * sizeof(*rows));
    int n = rows ? store_ranking(&a->store, &a->chart, 0, rows) : 0;
    if (a->rank_page * 8 >= n)
        a->rank_page = n ? (n - 1) / 8 : 0;
    gfx_rect(48, 279, 1184, 40, CARD, 2);
    label(70, 288, 17, TEXT, "排名");
    label(200, 288, 17, TEXT, "账号");
    label(570, 288, 17, TEXT, "分数");
    label(820, 288, 17, TEXT, "准确率");
    label(1100, 288, 17, TEXT, "评级");
    int rank = 0, previous = -1;
    for (int i = 0; i < n; i++) {
        Result r = rows[i].result;
        if (r.score != previous)
            rank = i + 1;
        previous = r.score;
        if (i < a->rank_page * 8 || i >= a->rank_page * 8 + 8)
            continue;
        int y = 340 + (i - a->rank_page * 8) * 40;
        label_format(70, y, 19, TEXT, "%d", rank);
        label(200, y, 19, TEXT, rows[i].name);
        label_format(570, y, 19, TEXT, "%d", r.score);
        label_format(820, y, 19, TEXT, "%.2f%%", r.accuracy * 100);
        label_format(1100, y, 19, TEXT, "%c", r.grade);
    }
    if (!n)
        label(70, 357, 20, MUTED, rows ? "暂无成绩" : "加载失败");
    free(rows);
    if (n > 8) {
        button(a, 48, 677, 130, 44, "上一页", ACT_RANK_PREV, 0, a->rank_page > 0, false);
        button(a, 198, 677, 130, 44, "下一页", ACT_RANK_NEXT, 0, (a->rank_page + 1) * 8 < n, false);
        label_format(362, 688, 18, MUTED, "%d / %d", a->rank_page + 1, (n + 7) / 8);
    }
}
void ui_action_at(App *a, int x, int y, Action *action, int *value) {
    *action = ACT_NONE;
    *value = 0;
    for (int i = a->button_count - 1; i >= 0; i--) {
        Button *b = &a->buttons[i];
        if (x >= b->x && x < b->x + b->w && y >= b->y && y < b->y + b->h) {
            *action = b->action;
            *value = b->value;
            return;
        }
    }
}
void ui_render(App *a) {
    gfx_text_input((a->page == PAGE_LOGIN && (a->focus == 1 || a->focus == 2)) ||
                   (a->page == PAGE_LAN && (a->focus == 3 || a->focus == 4)));
    a->button_count = 0;
    switch (a->page) {
    case PAGE_LOGIN:
        login(a);
        break;
    case PAGE_HOME:
        home(a);
        break;
    case PAGE_SELECT:
        select_page(a);
        break;
    case PAGE_LAN:
        lobby(a);
        break;
    case PAGE_GAME:
        game(a);
        break;
    case PAGE_RESULT:
        result(a);
        break;
    case PAGE_RANK:
        ranking(a);
        break;
    default:
        break;
    }
    if (a->page != PAGE_GAME)
        footer(a);
    gfx_present();
}
