#define WIN32_LEAN_AND_MEAN
#include "app.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

/**
 * @brief 更新应用底部的用户提示文字。
 * 参数：a 为应用状态；s 为 UTF-8 提示，来源不能与目标缓冲区不安全重叠。
 * 返回：无返回值，替换 a->message。
 * 说明：只负责提示，不弹窗、不改页面、不决定错误严重程度；调用它的流程负责是否中止或返回。
 */
static void report(App *a, const char *s) { nc_copy(a->message, sizeof(a->message), s); }
/**
 * @brief 按当前选曲释放旧引擎并替换谱面。
 * 参数：a 已含有效曲库、song 和 diff，chart/engine 已清零或初始化。
 * 返回：chart_load 成功返回 true，失败返回 false 并将原因写入 message。
 * 说明：先 engine_free，确保旧引擎不再借用谱面数组，再清空旧谱面。加载失败时
 * chart.count 为零，选曲页禁用开始按钮，排行榜也不会误用上一首歌的谱面。
 */
static bool load_chart(App *a) {
    engine_free(&a->engine);
    chart_free(&a->chart);
    return chart_load(&a->lib, a->song, a->diff, &a->chart, a->message);
}
/**
 * @brief 为当前选择打开音频设备，等到开局再播放。
 * 参数：a 的曲库索引和 chart 必须对应且已有效加载。
 * 返回：audio_open 成功返回 true，否则返回 false。
 * 说明：以 lib.root 和歌曲相对路径组成实际文件名，使用保存的音量并核对谱面时长；只预加载不播放，错误写入
 * message。
 */
static bool preload(App *a) {
    char path[NC_PATH];
    snprintf(path, sizeof(path), "%s/%s", a->lib.root, a->lib.songs[a->song].audio);
    return audio_open(path, a->settings.music, a->chart.duration, a->message);
}
/**
 * @brief 中止尚未正式确认的演奏并返回准备页面。
 * 参数：a 为应用状态；why 为中止原因。
 * 返回：无返回值；关闭音乐，释放本局引擎，清除倒计时。
 * 说明：谱面继续由 App 持有。联机同时 net_abort 并返回大厅，单人返回选曲。不调用
 * save_result，因此未完成成绩不会因中止写入。
 */
static void abort_run(App *a, const char *why) {
    audio_close();
    engine_free(&a->engine);
    a->countdown = false;
    bool lan = a->net.state != NET_IDLE;
    if (lan)
        net_abort(&a->net, why);
    report(a, why);
    a->page = lan ? PAGE_LAN : PAGE_SELECT;
}
/**
 * @brief 建立新局的引擎状态并进入计划倒计时。
 * 参数：a 的 chart 和音频预加载应已完成；target 为本机单调毫秒时钟中的计划起播点。
 * 返回：无返回值；引擎初始化失败时由 abort_run 返回准备页。
 * 说明：创建新 session 标识，清除上局保存状态，并记录计划开局时刻。进入游戏页表示有正在运行的对局，
 * countdown 表示音乐尚未起播，真正起播由 update_game 触发。
 */
static void begin_run(App *a, int64_t target) {
    if (!engine_start(&a->engine, &a->chart, a->settings.offset)) {
        abort_run(a, "游戏引擎分配内存失败");
        return;
    }
    nc_uuid(a->session);
    a->saved = false;
    a->target = target;
    a->countdown = true;
    a->page = PAGE_GAME;
    report(a, "");
}
/**
 * @brief 用当前曲目配置开始一场独立单人演奏。
 * 参数：a 为单人应用状态，chart 对应当前选曲和难度。
 * 返回：无返回值，错误通过 message 或 begin_run 的中止流程反映。
 * 说明：复用选曲时载入的谱面，重新打开音频并安排三秒倒计时。重开复用同一流程，未完成的旧统计不会保存。
 * 音频打开失败时回到选曲页并显示原因，避免停在已失去音乐的演奏页。
 */
static void solo_start(App *a) {
    if (a->chart.count && preload(a))
        begin_run(a, nc_now() + 3000);
    else {
        a->countdown = false;
        a->page = PAGE_SELECT;
    }
}
/**
 * @brief 读取适合本局判定的非倒退音乐时间。
 * 参数：a 为游戏页的应用状态，已打开音频设备。
 * 返回：查询失败时返回 -1，否则返回不早于引擎时刻的音频毫秒位置。
 * 说明：以音乐为判定时钟，短暂回退的设备时间以已处理位置为下限。
 */
static int music_time(const App *a) {
    int pos = audio_position();
    return pos < 0 || pos >= a->engine.now ? pos : a->engine.now;
}
/**
 * @brief 把暂停请求映射为单人暂停或联机中止。
 * 参数：a 为当前演奏状态。
 * 返回：无返回值；失败原因写 message，必要时 abort_run。
 * 说明：联机无暂停而直接中止；单人倒计时中按暂停视为取消。PLAYING
 * 时先补算当前音乐位置，再暂停音频和引擎，保留活动长按等待恢复。
 */
static void pause_run(App *a) {
    if (a->net.state != NET_IDLE) {
        abort_run(a, "联机演奏中退出，本局不保存");
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
/**
 * @brief 协调引擎长按恢复条件与音频继续播放。
 * 参数：a 为暂停或恢复准备中的应用状态。
 * 返回：无返回值；不能恢复时保持等待，恢复失败可中止本局。
 * 说明：PAUSED 先转 RESUME_READY；无长按可立即继续，有长按须等轨道按住。仅 engine_resume
 * 成功才发音频 resume，使按键状态与音频同步恢复。
 */
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
/**
 * @brief 在满足完成与联机确认要求时保存一次成绩。
 * 参数：a 提供本局 session、当前账号、谱面和最终统计；联机由调用方等待双方确认。
 * 返回：无返回值；成功设 saved=true，失败通过 message 提示。
 * 说明：构建 Record 并用 UTC 系统日期记录成绩。不使用单调时钟作日期，存储层负责完赛校验、去重与原子写入。
 */
static void save_result(App *a) {
    char date[32];
    SYSTEMTIME t;
    GetSystemTime(&t);
    snprintf(date, sizeof(date), "%04u-%02u-%02uT%02u:%02u:%02uZ", t.wYear, t.wMonth, t.wDay,
             t.wHour, t.wMinute, t.wSecond);
    bool lan = a->net.state == NET_DONE;
    Record r = {.session = a->session, .name = a->name, .date = date,
                .chart = &a->chart, .stats = &a->engine.stats,
                .match = lan ? a->net.match : NULL, .opponent = lan ? a->net.peer : NULL,
                .opponent_score = lan ? a->net.peer_score : 0};
    if (store_save(&a->store, &r, a->message)) {
        a->saved = true;
        report(a, "成绩已保存");
    }
}
/**
 * @brief 在房间内加载谱面和音频，再发送配置或资源确认。
 * 参数：a 为已握手的房主或已收到配置的客人，song/diff 为本轮选择。
 * 返回：无返回值，错误写 message 或 net_abort。
 * 说明：房主发布本地选择，客人按房主选择加载并核对指纹。资源加载失败时终止连接。
 */
static void prepare_room(App *a) {
    if (!load_chart(a) || !preload(a)) {
        net_abort(&a->net, a->message);
        return;
    }
    if (a->net.host ? net_configure(&a->net, a->song, a->diff, &a->chart)
                    : net_verify(&a->net, &a->chart))
        report(a, "");
}
/**
 * @brief 把非阻塞网络状态转换成应用层加载、开局与保存动作。
 * 参数：a 为每帧应用状态；仅有建房或连接会话时工作。
 * 返回：无返回值，修改页面与网络状态。
 * 说明：先 net_poll，处理中止、再来一局、房主发配置/客人校验。倒计时建立本地引擎，
 * update_game 收到 NET_PLAY 状态后起播；结果页首次进入 NET_DONE 时保存本局成绩。
 */
static void update_network(App *a) {
    Network *n = &a->net;
    if (n->state == NET_IDLE)
        return;
    NetState previous_state = n->state;
    net_poll(n);
    if (n->state == NET_ABORT) {
        audio_close();
        a->countdown = false;
        if (a->page == PAGE_GAME || a->page == PAGE_RESULT) {
            engine_free(&a->engine);
            a->page = PAGE_LAN;
        }
        report(a, n->status);
        return;
    }
    if (n->state == NET_ROOM) {
        if (a->page == PAGE_RESULT)
            a->page = PAGE_LAN;
        if (n->host && n->handshake && !n->revision)
            prepare_room(a);
        else if (!n->host && n->revision && !n->peer_verified) {
            a->song = n->song;
            a->diff = n->difficulty;
            prepare_room(a);
        }
    } else if (a->page == PAGE_LAN && (n->state == NET_COUNTDOWN || n->state == NET_PLAY))
        begin_run(a, n->target);
    else if (previous_state != NET_DONE && n->state == NET_DONE && a->page == PAGE_RESULT)
        save_result(a);
}
/**
 * @brief 逐帧推进演奏生命周期并在音乐结束附近进入结算。
 * 参数：a 包含已准备的引擎、媒体、倒计时和网络状态。
 * 返回：无返回值；启动或播放错误通过状态与 message 反映。
 * 说明：单人倒计时到时起播；联机等待网络模块按校准后的时钟进入 NET_PLAY，两种模式共用音频起播逻辑。
 * 随后用 music_time 推进引擎并发布联机分数。所有音符结算且音乐到尾前 20ms 才停止，
 * 单人保存、联机提交后等待确认。
 */
static void update_game(App *a) {
    if (a->page != PAGE_GAME)
        return;
    bool lan = a->net.state != NET_IDLE;
    if (a->countdown) {
        if (lan ? a->net.state != NET_PLAY : nc_now() < a->target)
            return;
        if (!audio_play(a->message)) {
            abort_run(a, lan ? "计划开局时音乐播放失败" : "音乐播放失败");
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
    int score = lan ? stats_result(&a->engine.stats, a->chart.count).score : 0;
    if (lan)
        net_score(&a->net, score);
    if (a->engine.state == FINISHED && now >= a->chart.duration - 20) {
        audio_close();
        if (lan) {
            if (!net_finish(&a->net, score)) {
                abort_run(a, "最终成绩发送失败");
                return;
            }
            report(a, "等待成绩确认");
        } else
            save_result(a);
        a->page = PAGE_RESULT;
    }
}
/**
 * @brief 清理房间与媒体，使应用退出联机运行流程。
 * 参数：a 为应用状态，网络对象已经初始化。
 * 返回：无返回值；关闭联机、音频，清除倒计时状态。
 * 说明：若正在开局/演奏/等待结果，先发中止再关闭连接；其他阶段直接 net_leave。该函数不切换
 * page，也不释放谱面，调用动作随后决定目的页面。
 */
static void leave_room(App *a) {
    if (a->net.state == NET_PLAY || a->net.state == NET_COUNTDOWN || a->net.state == NET_WAIT)
        net_abort(&a->net, "玩家离开房间");
    net_leave(&a->net);
    audio_close();
    a->countdown = false;
}
/**
 * @brief 切换曲目或难度并按需更新房主配置。
 * 参数：a 为状态；song 为欲选下标（越过首尾时循环）；diff 应由界面保证在 0—2。
 * 返回：无返回值；加载结果通过 message 和 chart 体现。
 * 说明：联机仅 NET_ROOM 中的房主可改；先停旧音乐。已握手的房主通过 prepare_room 载入并发布新谱面，
 * 其余页面只载入谱面。切换不会开始演奏或保存旧局。
 */
static void change_selection(App *a, int song, int diff) {
    bool lan = a->page == PAGE_LAN;
    if (lan && (!a->net.host || !a->net.handshake || a->net.state != NET_ROOM))
        return;
    if (song < 0)
        song = a->lib.count - 1;
    if (song >= a->lib.count)
        song = 0;
    a->song = song;
    a->diff = diff;
    /* 切换榜单条件后回到第一页，避免沿用旧曲目的分页位置。 */
    if (a->page == PAGE_RANK)
        a->rank_page = 0;
    audio_close();
    if (lan)
        prepare_room(a);
    else if (load_chart(a))
        report(a, "");
}
/**
 * @brief 根据统一动作枚举执行界面业务操作。
 * 参数：a 为应用状态；act 是按钮/键盘映射的 Action；value 为难度等动作附参。
 * 返回：无返回值，操作结果体现在 a 的页面、网络状态及 message。
 * 说明：集中处理账号、页面导航、选曲、开局、重开、保存、联机准备和再来一局。绘制模块只登记动作，不直接执行业务；各网络/存储函
 * 数仍检查自己的前置条件。
 */
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
        /* 排行榜固定展示单人成绩，离开旧联机会话才能正常切换曲目。 */
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
        /* 界面只接受 1—65535，测试中的系统分配端口不经过此入口。 */
        char *end;
        long port = strtol(a->port, &end, 10);
        const char *ip = act == ACT_JOIN ? a->ip : NULL;
        if (*end || port < 1 || port > 65535)
            report(a, "端口范围是 1—65535");
        else if (net_open(&a->net, ip, (int)port, a->name, a->message))
            report(a, ip ? "正在连接房主" : "等待加入");
    } break;
    case ACT_READY:
        net_ready(&a->net, a->message);
        break;
    case ACT_NET_START:
        net_start(&a->net, a->message);
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
    case ACT_REMATCH:
        if (net_rematch(&a->net, a->message)) {
            a->page = PAGE_LAN;
            report(a, "加载中");
        }
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
/**
 * @brief 向当前有焦点的输入框追加字符或删除一个 UTF-8 字符。
 * 参数：a 提供 page/focus 和输入缓冲；ch 为窗口字符事件，当前实现只接收非代理区 BMP 字符。
 * 返回：无返回值；无有效焦点、控制字符或缓冲不足时忽略输入。
 * 说明：退格从尾部跨过 UTF-8 续字节删除整字；IPv4
 * 仅数字和点，端口仅数字。登录框按字节容量防溢出，提交时再检查账号长度和字符合法性；不处理剪贴板或光标中间编辑。
 */
static void edit_character(App *a, uint32_t ch) {
    int first_focus = a->page == PAGE_LOGIN ? 1 : 3;
    if ((a->page != PAGE_LOGIN && a->page != PAGE_LAN) ||
        a->focus < first_focus || a->focus >= first_focus + 2)
        return;
    const struct { char *text; size_t capacity; } fields[] = {
        {a->name, sizeof(a->name)}, {a->password, sizeof(a->password)},
        {a->ip, sizeof(a->ip)}, {a->port, sizeof(a->port)},
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
/**
 * @brief 把窗口事件分发给输入框、按钮或游戏判定。
 * 参数：a 为状态；e 是 gfx_event 返回的单个借用 UiEvent。
 * 返回：无返回值，依据事件修改状态或调用业务函数。
 * 说明：关闭/失焦先处理；忽略自动重复按下。点击使用上一帧按钮表；Tab/Enter 处理登录，Esc/Enter
 * 处理暂停，D/F/J/K 按下和松开传给引擎。倒计时不判键，恢复准备中的按键触发再次检查恢复条件。
 */
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
        if (a->page == PAGE_GAME)
            pause_run(a);
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
/**
 * @brief 初始化应用、运行事件循环，并在正常退出时释放资源。
 * 参数：argc/argv 为进程命令行，可含 --data-dir、--assets、--quit-after 及各自参数。
 * 返回：正常退出返回 0；分配、库加载、网络初始化或窗口创建失败返回 1。
 * 说明：在堆上分配大型 App，读取设置、成绩和曲库索引；实际谱面按选择加载。每帧最多处理 128 个事件，再更新网络、游戏、绘制和短暂让出
 * CPU。最终按音频、引擎、谱面、存储、网络和窗口次序清理。
 */
int app_run(int argc, char **argv) {
    App *a = calloc(1, sizeof(*a));
    if (!a)
        return 1;
    char exe[NC_PATH], assets[NC_PATH], warning[NC_ERROR];
    nc_exe_dir(exe);
    snprintf(assets, sizeof(assets), "%s/assets/songs", exe);
    nc_data_dir(a->data);
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--data-dir") && i + 1 < argc)
            nc_copy(a->data, sizeof(a->data), argv[++i]);
        else if (!strcmp(argv[i], "--assets") && i + 1 < argc)
            nc_copy(assets, sizeof(assets), argv[++i]);
        else if (!strcmp(argv[i], "--quit-after") && i + 1 < argc)
            a->quit_at = nc_now() + atoi(argv[++i]);
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
        if (a->quit_at && nc_now() >= a->quit_at)
            action(a, ACT_QUIT, 0);
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
/**
 * @brief 提供可执行文件的 C 入口。
 * 参数：argc/argv 为运行时传入的命令行，直接转交 app_run。
 * 返回：返回 app_run 的退出码，0 正常、1 初始化失败。
 * 说明：本入口不包含游戏循环和规则，保持与应用初始化分开，方便从入口沿 app_run 阅读整体流程。
 */
int main(int argc, char **argv) { return app_run(argc, argv); }
