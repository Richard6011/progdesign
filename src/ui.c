/* All page layout, interaction and rhythm rendering are ordinary C. */
#include "app.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* 浅灰背景、白色内容和深色文字沿用常见桌面界面配色；蓝色仅用于选中状态。
 * 音符仍用四种低饱和颜色区分轨道，避免换肤影响演奏识别。 */
static const uint32_t BG = 0xf0f0f0, PANEL = 0xffffff, CARD = 0xe6e6e6, TEXT = 0x202020,
                      MUTED = 0x666666, BORDER = 0xb8b8b8, BLUE = 0xdce9f7,
                      CYAN = 0x245a81, PINK = 0x73576d, GREEN = 0x356443, RED = 0xb03030;
static const uint32_t lanes[4] = {0x6899b4, 0x7e8fb6, 0x9985ad, 0xb18698};
/**
 * @brief 在界面上绘制一段 UTF-8 文本。
 * 参数：x/y 为窗口客户区坐标；size 为字体高度；c 为 0xRRGGBB；s 为零结尾文本。
 * 返回：无返回值。
 * 说明：只转发 gfx_text，不登记点击区域，不修改业务数据；文本使用统一的 Qt UTF-8 字体绘制路径。
 */
static void label(int x, int y, int size, uint32_t c, const char *s) { gfx_text(x, y, size, c, s); }
/**
 * @brief 检测鼠标当前坐标是否处于矩形内。
 * 参数：a 提供 mx/my；x/y/w/h 定义客户区矩形，宽高应非负。
 * 返回：位于左上含边、右下不含边的区域时返回 true，否则返回 false。
 * 说明：仅用于按钮高亮，采用与点击判定一致的半开范围，防止共享边界被两个按钮同时命中。
 */
static bool hover(const App *a, int x, int y, int w, int h) {
    return a->mx >= x && a->mx < x + w && a->my >= y && a->my < y + h;
}
/**
 * @brief 绘制可禁用或高亮的按钮，并登记可点击动作。
 * 参数：a 为每帧按钮表与鼠标状态；x/y/w/h 为矩形；s 为标签；act/val
 * 为点击动作；enabled/primary 控制交互与配色。
 * 返回：无返回值；启用且表未满时追加一个 Button。
 * 说明：hover 只改变配色。禁用按钮仍绘制但不登记命中区；最多 96 项。业务动作稍后由 ui_action_at 和
 * main.c 的 action 执行，绘制不直接发网包或写文件。
 */
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
/**
 * @brief 绘制标准高度的普通按钮。
 * 参数：a、x/y/w、s、act 与 button 含义一致。
 * 返回：无返回值；等价于 enabled=true、primary=false、value=0、高度 52 的 button。
 * 说明：把重复的默认参数集中，特殊需要禁用、主色或非零附参的按钮应直接调用 button。
 */
static void btn(App *a, int x, int y, int w, const char *s, Action act) {
    button(a, x, y, w, 52, s, act, 0, true, false);
}
/**
 * @brief 绘制自定义文本输入框并登记焦点动作。
 * 参数：a 含当前焦点与按钮表；x/y/w 为位置；value/hint 为文本；act 为选择该字段的动作；focus
 * 为字段编号；secret 控制掩码。
 * 返回：无返回值；登记输入框点击区，不直接改 value。
 * 说明：按 500ms 闪烁竖线模拟光标，密码按 UTF-8 字符起始字节数量显示星号且最多 60 个。实际编辑由
 * edit_character 完成；页面须保证按钮表仍有一项容量。
 */
static void field(App *a, int x, int y, int w, const char *value, const char *hint, Action act,
                  int focus, bool secret) {
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
    a->buttons[a->button_count++] = (Button){x, y, w, 54, act, 0};
}
/**
 * @brief 绘制普通页面的统一页头。
 * 参数：a 提供当前 page；s 为标题。
 * 返回：无返回值；清空背景、绘制分隔线，并按页面登记返回主页按钮。
 * 说明：登录、主页和游戏页不显示该返回按钮；普通页面先调用 heading 再绘制正文，避免后续背景清除已画内容。
 */
static void heading(App *a, const char *s) {
    gfx_clear(BG);
    label(48, 30, 32, TEXT, s);
    if (a->page != PAGE_LOGIN && a->page != PAGE_HOME && a->page != PAGE_GAME)
        btn(a, 1064, 36, 168, "返回主页", ACT_HOME);
    gfx_line(48, 104, 1232, 104, BORDER);
}
/**
 * @brief 在底部有限空间内绘制应用提示。
 * 参数：a->message 为预期有效的 UTF-8 提示文本。
 * 返回：无返回值；最多显示两行，其余文本不显示。
 * 说明：按 ASCII/二三字节字符估算宽度，尽量在字符边界换行而不截断中文；它不是完整排版或四字节 Unicode
 * 换行器，供当前项目中文短提示使用。
 */
static void footer(App *a) {
    if (!a->message[0])
        return;
    /* 仅有实际状态或错误时才占用底部；按 UTF-8 边界折行，避免中文截断。 */
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
/**
 * @brief 绘制登录或注册页。
 * 参数：a 提供 registering、name/password、focus 等当前表单状态。
 * 返回：无返回值；登记表单、登录/注册、模式切换及退出按钮。
 * 说明：只读取账号文本并对密码掩码，不进行认证或持久化；点击后由业务动作调用
 * account_action，避免把文件访问放在每帧绘制中。
 */
static void login(App *a) {
    heading(a, "音符碰撞");
    gfx_rect(376, 150, 528, 542, PANEL, 4);
    gfx_text_center(416, 174, 448, 42, 28, TEXT, a->registering ? "注册" : "登录");
    label(416, 242, 18, TEXT, "账号");
    field(a, 416, 274, 448, a->name, "账号", ACT_FIELD_NAME, 1, false);
    label(416, 354, 18, TEXT, "密码");
    field(a, 416, 386, 448, a->password, "密码", ACT_FIELD_PASSWORD, 2, true);
    button(a, 416, 474, 448, 54, a->registering ? "注册" : "登录",
           a->registering ? ACT_REGISTER : ACT_LOGIN, 0, true, true);
    btn(a, 416, 548, 448, a->registering ? "返回登录" : "注册账号", ACT_LOGIN_MODE);
    btn(a, 416, 622, 448, "退出", ACT_QUIT);
}
/**
 * @brief 绘制登录后的主菜单。
 * 参数：a 提供已登录昵称和当前鼠标状态。
 * 返回：无返回值；登记单人、联机、排行、换账号与退出动作。
 * 说明：显示当前账号和居中菜单；不预加载媒体、不改变模式，具体页面切换在动作处理阶段进行。
 */
static void home(App *a) {
    gfx_clear(BG);
    gfx_text_center(0, 94, 1280, 58, 36, TEXT, "音符碰撞");
    gfx_text_center(0, 170, 1280, 32, 20, MUTED, a->name);
    /* 统一宽度和间距，并从窗口中线定位；五个入口均处于同一竖列。 */
    const int x = (1280 - 360) / 2, w = 360, h = 54, gap = 20;
    const char *names[] = {"单人模式", "局域网双人", "排行榜", "切换账号", "退出"};
    const Action actions[] = {ACT_SOLO, ACT_LAN, ACT_RANK, ACT_LOGOUT, ACT_QUIT};
    for (int i = 0; i < 5; i++)
        button(a, x, 248 + i * (h + gap), w, h, names[i], actions[i], 0, true, i == 0);
}
/**
 * @brief 绘制可复用的曲目卡片与三档难度选择器。
 * 参数：a 的 song/diff/chart 已合法；x/y 为卡片左上角；editable 控制是否可切换。
 * 返回：无返回值；按 editable 登记前后歌曲与难度按钮。
 * 说明：只展示歌曲名和难度；不读取谱面文件。单人和联机房主复用，客人可见配置但不能操作。
 */
static void selection(App *a, int x, int y, bool editable) {
    Song *s = &a->lib.songs[a->song];
    gfx_rect(x, y, 716, 170, PANEL, 4);
    label(x + 32, y + 54, 32, TEXT, s->title);
    button(a, x + 522, y + 42, 72, 48, "<", ACT_SONG_PREV, 0, editable, false);
    button(a, x + 612, y + 42, 72, 48, ">", ACT_SONG_NEXT, 0, editable, false);
    for (int d = 0; d < 3; d++)
        button(a, x + d * 244, y + 192, 228, 52, nc_difficulties[d], ACT_DIFF, d, editable,
               d == a->diff);
}
/**
 * @brief 绘制单人准备演奏页。
 * 参数：a 含当前选曲和已载入谱面。
 * 返回：无返回值；仅在 chart.count>0 时启用开始按钮。
 * 说明：调用 selection 绘制公共选曲区；仅发 ACT_PLAY，真正重载音频和倒计时由 solo_start 完成。
 */
static void select_page(App *a) {
    heading(a, "选曲");
    selection(a, 282, 180, true);
    button(a, 282, 492, 716, 58, "开始", ACT_PLAY, 0, a->chart.count > 0, true);
}
/**
 * @brief 根据连接阶段绘制大厅及可用操作。
 * 参数：a 含 lan_mode、Network、当前曲目和本机地址。
 * 返回：无返回值；登记创建/加入、准备、房主开始及可编辑选曲动作。
 * 说明：空闲/连接/监听时显示 IPv4/端口；已入房显示角色、资源校验和准备状态。按钮启用是 UI
 * 引导，网络模块还会独立检查协议状态，绘制本身不操作 socket。
 */
static void lobby(App *a) {
    bool connected = a->net.state == NET_ROOM && a->net.handshake,
         free_room = a->net.state == NET_IDLE || a->net.state == NET_ABORT;
    heading(a, "局域网双人");
    if (free_room || a->net.state == NET_CONNECT || a->net.state == NET_LISTEN) {
        gfx_rect(160, 163, 960, 440, PANEL, 4);
        label(200, 200, 18, TEXT, "房主 IPv4");
        field(a, 200, 234, 550, a->ip, "IPv4", ACT_FIELD_IP, 3, false);
        label(782, 200, 18, TEXT, "端口");
        field(a, 782, 234, 298, a->port, "9527", ACT_FIELD_PORT, 4, false);
        button(a, 200, 326, 420, 54, "创建房间", ACT_HOST, 0, free_room, true);
        button(a, 660, 326, 420, 54, "加入房间", ACT_JOIN, 0, free_room, false);
        label(200, 424, 18, MUTED, "本机 IPv4");
        /* 多网卡地址分两行显示，保留连接所需信息，避免长串超出卡片。 */
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
        selection(a, 48, 153, connected && a->net.host);
        char line[256];
        gfx_rect(806, 153, 426, 382, PANEL, 4);
        snprintf(line, sizeof(line), "%s：%s", a->net.host ? "房主" : "客人", a->name);
        label(838, 192, 20, TEXT, line);
        snprintf(line, sizeof(line), "对方：%s", a->net.peer);
        label(838, 242, 20, TEXT, line);
        label(838, 302, 18, MUTED, a->net.peer_verified ? "资源一致" : "校验中");
        label(838, 344, 18, MUTED, a->net.peer_ready ? "对方已准备" : "对方未准备");
        button(a, 48, 468, 340, 58, a->net.ready ? "取消准备" : "准备", ACT_READY, 0,
               connected && a->net.peer_verified, true);
        button(a, 838, 438, 362, 58, "开始", ACT_NET_START, 0,
               connected && a->net.host && a->net.ready && a->net.peer_ready && a->net.samples >= 3,
               true);
    }
}
/**
 * @brief 按音乐绝对时间绘制四轨、音符、分数和暂停层。
 * 参数：a 必须包含有效 chart、runtime 及当前运行/倒计时状态。
 * 返回：无返回值；刷新游戏画面并登记暂停、重开、退出或恢复按钮。
 * 说明：用 y=判定线-(音符时刻-音乐时刻)*轨道高度/approach
 * 映射位置；长按绘制头尾，已结算音符跳过。倒计时使用相对 target 的负时间，所有判定仍由 engine.c
 * 完成，不依据像素碰撞计分。
 */
static void game(App *a) {
    gfx_clear(BG);
    const int left = 350, width = 580, top = 128, bottom = 635;
    char s[256];
    Result r = stats_result(&a->engine.stats, a->chart.count);
    label(42, 30, 27, TEXT, a->lib.songs[a->song].title);
    label(44, 74, 17, MUTED, a->chart.difficulty);
    snprintf(s, sizeof(s), "%07d", r.score);
    label(44, 164, 35, CYAN, s);
    label(44, 214, 17, MUTED, "分数");
    snprintf(s, sizeof(s), "%d", a->engine.stats.combo);
    label(44, 286, 50, TEXT, s);
    label(44, 346, 18, MUTED, "连击");
    snprintf(s, sizeof(s), "%.2f%%", r.accuracy * 100);
    label(44, 409, 28, GREEN, s);
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
            if (n->chord >= 0)
                gfx_rect(x + w / 2 - 12, y - 3, 24, 5, TEXT, 2);
        }
    }
    gfx_rect(left, bottom, width, 3, PINK, 0);
    if (a->engine.last_time >= 0 && time - a->engine.last_time < 650) {
        const char *names[] = {"PERFECT", "GREAT", "GOOD", "MISS"};
        label(546, 292, 31, a->engine.last == MISS ? RED : CYAN, names[a->engine.last]);
    }
    if (a->lan_mode) {
        label(970, 163, 20, MUTED, "对方分数");
        snprintf(s, sizeof(s), "%07d", stats_result(&a->net.peer_stats, a->chart.count).score);
        label(970, 205, 28, PINK, s);
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
    snprintf(s, sizeof(s), "%.1f / %.1f 秒", a->engine.now / 1000.0, a->chart.duration / 1000.0);
    label(350, 725, 16, MUTED, s);
    if (a->countdown) {
        gfx_rect(452, 275, 374, 158, PANEL, 16);
        int seconds = (int)((a->target - nc_now() + 999) / 1000);
        snprintf(s, sizeof(s), "%d", seconds > 0 ? seconds : 1);
        label(612, 293, 58, CYAN, s);
    }
    if (a->engine.state == PAUSED || a->engine.state == RESUME_READY) {
        gfx_rect(418, 264, 446, 242, PANEL, 14);
        label(532, 288, 31, TEXT, "已暂停");
        button(a, 450, 393, 382, 56,
               a->engine.state == RESUME_READY ? "按住长按键继续" : "继续 · Enter",
               ACT_RESUME, 0, true, true);
    }
}
/**
 * @brief 绘制本局统计、保存状态与联机胜负。
 * 参数：a 提供最终引擎统计及网络确认状态。
 * 返回：无返回值；按模式和确认状态登记重演、再来一局或重试保存。
 * 说明：分数/评级统一由 stats_result 计算；联机只有 confirmed
 * 才显示正式胜负和开放保存，同分为平局，客人等待房主再来一局。不在每帧绘制中再次保存。
 */
static void result(App *a) {
    heading(a, "成绩");
    Result r = stats_result(&a->engine.stats, a->chart.count);
    char s[256];
    gfx_rect(48, 153, 716, 248, PANEL, 14);
    char grade[2] = {r.grade, 0};
    label(80, 190, 99, CYAN, grade);
    snprintf(s, sizeof(s), "%07d", r.score);
    label(245, 197, 53, TEXT, s);
    snprintf(s, sizeof(s), "准确率 %.2f%%    最大连击 %d", r.accuracy * 100,
             a->engine.stats.max_combo);
    label(245, 270, 22, MUTED, s);
    label(245, 322, 21, GREEN,
          r.ap         ? "ALL PERFECT"
          : r.fc       ? "FULL COMBO"
          : r.complete ? "演奏完成"
                       : "未完成");
    int values[] = {a->engine.stats.perfect, a->engine.stats.great, a->engine.stats.good,
                    a->engine.stats.miss};
    const char *names[] = {"Perfect", "Great", "Good", "Miss"};
    for (int i = 0; i < 4; i++) {
        gfx_rect(48 + i * 182, 430, 170, 105, PANEL, 10);
        label(65 + i * 182, 445, 18, MUTED, names[i]);
        snprintf(s, sizeof(s), "%d", values[i]);
        label(65 + i * 182, 479, 28, TEXT, s);
    }
    gfx_rect(806, 153, 426, 382, PANEL, 14);
    label(840, 190, 24, TEXT, a->lan_mode ? "对战结果" : "本局信息");
    label(840, 247, 21, TEXT, a->lib.songs[a->song].title);
    label(840, 286, 18, MUTED, a->chart.difficulty);
    if (a->lan_mode) {
        int peer = stats_result(&a->net.peer_stats, a->chart.count).score;
        label(840, 333, 27, a->net.confirmed ? CYAN : MUTED,
              a->net.confirmed ? (r.score == peer  ? "平 局"
                                  : r.score > peer ? "胜 利"
                                                   : "落 败")
                               : "确认中");
        snprintf(s, sizeof(s), "对方 %s / %d 分", a->net.peer, peer);
        label(840, 382, 19, TEXT, s);
    }
    label(840, 454, 19, a->saved ? GREEN : MUTED, a->saved ? "已保存" : "未保存");
    button(a, 48, 585, 340, 58,
           a->lan_mode ? (a->net.host ? "返回房间" : "等待房主") : "重新演奏",
           a->lan_mode ? ACT_REMATCH : ACT_RESTART, 0,
           !a->lan_mode || (a->net.confirmed && a->net.host), true);
    button(a, 414, 585, 350, 58, "重试保存", ACT_SAVE, 0,
           !a->saved && (!a->lan_mode || a->net.confirmed), false);
}
/**
 * @brief 绘制单人模式下各账号最佳成绩。
 * 参数：a 提供 Store、选曲、diff 和 rank_page。
 * 返回：无返回值；每页显示八条，可修正越界页号并登记翻页/筛选按钮。
 * 说明：借用 store_ranking
 * 返回的记录指针，高分在前，同分沿用相同名次。临时指针数组当帧释放，记录本身不释放；无记录或数组分配失败显示空榜。
 */
static void ranking(App *a) {
    heading(a, "排行榜");
    char s[256];
    label(48, 150, 25, TEXT, a->lib.songs[a->song].title);
    btn(a, 670, 145, 70, "<", ACT_SONG_PREV);
    btn(a, 754, 145, 70, ">", ACT_SONG_NEXT);
    for (int d = 0; d < 3; d++)
        button(a, 48 + d * 150, 211, 138, 45, nc_difficulties[d], ACT_DIFF, d, true, d == a->diff);
    /* 排行入口固定查询单人模式（0）；存储层按账号保留最佳成绩，避免同账号占据多行。 */
    const Record **rows = malloc(20000 * sizeof(*rows));
    int n = rows ? store_ranking(&a->store, &a->chart, 0, rows, 20000) : 0;
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
        Result r = stats_result(&rows[i]->stats, rows[i]->count);
        if (r.score != previous)
            rank = i + 1;
        previous = r.score;
        if (i < a->rank_page * 8 || i >= a->rank_page * 8 + 8)
            continue;
        int y = 340 + (i - a->rank_page * 8) * 40;
        snprintf(s, sizeof(s), "%d", rank);
        label(70, y, 19, TEXT, s);
        label(200, y, 19, TEXT, rows[i]->name);
        snprintf(s, sizeof(s), "%d", r.score);
        label(570, y, 19, TEXT, s);
        snprintf(s, sizeof(s), "%.2f%%", r.accuracy * 100);
        label(820, y, 19, TEXT, s);
        s[0] = r.grade;
        s[1] = 0;
        label(1100, y, 19, TEXT, s);
    }
    if (!n)
        label(70, 357, 20, MUTED, rows ? "暂无成绩" : "加载失败");
    free(rows);
    /* 一页能显示完时不显示无效的翻页按钮，减少空榜页面上的控件。 */
    if (n > 8) {
        button(a, 48, 677, 130, 44, "上一页", ACT_RANK_PREV, 0, a->rank_page > 0, false);
        button(a, 198, 677, 130, 44, "下一页", ACT_RANK_NEXT, 0, (a->rank_page + 1) * 8 < n, false);
        snprintf(s, sizeof(s), "%d / %d", a->rank_page + 1, (n + 7) / 8);
        label(362, 688, 18, MUTED, s);
    }
}
/**
 * @brief 把客户区点击坐标映射到上一帧登记的按钮动作。
 * 参数：a 为已由 ui_render 填充的按钮表；x/y 为点击位置；action/value 为非空输出指针。
 * 返回：无返回值；命中则写动作及附参，无命中写 ACT_NONE 和 0。
 * 说明：逆序查找，后绘制的按钮优先响应，便于暂停层覆盖底层控件；不执行动作、不修改业务状态，矩形右下边界不包含在内。
 */
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
/**
 * @brief 按当前页面生成一帧界面与新的按钮命中表。
 * 参数：a 为完整应用状态，page 决定调用哪个页面绘制函数。
 * 返回：无返回值；清零并重建按钮表，最后提交 Qt 离屏画布。
 * 说明：游戏页使用独立布局，其余页追加 footer。绘制主要读取状态，少数展示性字段如 rank_page
 * 会做纠正；网络和音乐时间推进应在本函数之前完成。
 */
void ui_render(App *a) {
    // 全部页面共用一个 Qt 窗口；仅文本框允许组词，演奏页必须直接收到 DFJK 按下/松开。
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
