#include "ui.h" /* 使用 Windows 原生窗口，不使用 C++ 图形库。 */
#include "game.h" /* 使用已独立封装的游戏、资源和存储模块。 */
#include <stdio.h> /* 格式化界面文字。 */
#include <string.h> /* 清理输入与复制账号。 */
#include <stdlib.h> /* 解析设置输入。 */
#include <wchar.h> /* 转换和绘制中文文字。 */
#include <errno.h> /* 检查整数输入溢出。 */

enum Page { PAGE_LOGIN, PAGE_HOME, PAGE_SONG, PAGE_DIFFICULTY, PAGE_GAME, PAGE_RESULT, PAGE_RANKING, PAGE_SETTINGS, PAGE_HELP }; /* 逐页显示界面。 */
enum Phase { PHASE_COUNTDOWN, PHASE_PLAYING, PHASE_PAUSED }; /* 只保留单人游戏需要的三个阶段。 */
enum ControlId /* 每个控件一个编号，连续编号方便映射歌曲及难度。 */
{ /* 控件编号开始。 */
    ID_NAME = 100, /* 账号输入框。 */
    ID_PASSWORD, /* 密码输入框。 */
    ID_CONFIRM, /* 注册时的确认密码框。 */
    ID_SUBMIT, /* 登录或注册提交按钮。 */
    ID_SWITCH, /* 切换注册和登录页面。 */
    ID_START, /* 主菜单开始游戏按钮。 */
    ID_RANKING, /* 查看排行榜。 */
    ID_SETTINGS, /* 查看设置。 */
    ID_HELP, /* 查看操作说明。 */
    ID_LOGOUT, /* 退出当前账号。 */
    ID_BACK, /* 返回上一页或主菜单。 */
    ID_SONG0, /* 选择小星星。 */
    ID_SONG1, /* 选择霓虹脉冲。 */
    ID_EASY, /* 选择简单谱面。 */
    ID_NORMAL, /* 选择普通谱面。 */
    ID_HARD, /* 选择困难谱面。 */
    ID_PAUSE, /* 暂停或继续游戏。 */
    ID_ABORT, /* 中止未完成局次。 */
    ID_RETRY, /* 重新开始同一曲目。 */
    ID_SAVE_AGAIN, /* 文件错误后重试保存完整成绩。 */
    ID_VOLUME, /* 音量输入框。 */
    ID_OFFSET, /* 输入校准框。 */
    ID_SAVE_SETTINGS, /* 提交设置。 */
    ID_RANK_SONG, /* 排行榜歌曲筛选框。 */
    ID_RANK_DIFFICULTY, /* 排行榜难度筛选框。 */
    ID_RANK_LIST /* 排行榜的可滚动成绩列表。 */
}; /* 控件编号结束。 */

static HWND window; /* 本程序的唯一主窗口。 */
static HFONT font; /* 所有原生控件使用的中文字体。 */
static int page = PAGE_LOGIN; /* 当前显示哪一个页面。 */
static int registering = 0; /* 当前提交注册还是登录。 */
static int selectedSong = 0; /* 当前选中的歌曲。 */
static int selectedDifficulty = 0; /* 当前选中的难度。 */
static int phase = PHASE_COUNTDOWN; /* 当前游戏阶段。 */
static int resumeRequested = 0; /* 用户是否已请求继续暂停的游戏。 */
static int settingsWritable = 1; /* 损坏设置文件不能被静默覆盖。 */
static int saved = 0; /* 本局最佳成绩是否成功处理。 */
static ULONGLONG startAt; /* 倒计时结束的系统时刻。 */
static char playerName[NAME_SIZE]; /* 已验证的账号，不保存密码。 */
static char pendingName[NAME_SIZE]; /* 注册后方便用户继续登录的账号文本。 */
static char message[MESSAGE_SIZE]; /* 当前最近的错误提示。 */
static Chart chosenChart; /* 已选曲目和难度的只读原始谱面。 */
static Game game; /* 当前游戏的状态和音符副本。 */
static Settings settings; /* 已加载的音量和输入校准。 */
static Record finalRecord; /* 已完成本局的最终成绩。 */
static Record rankRecords[MAX_RECORDS]; /* 排行榜使用的固定成绩数组。 */
static int rankSong = 0; /* 排行榜正在查看的歌曲。 */
static int rankDifficulty = 0; /* 排行榜正在查看的难度。 */

static void show_login(int registration); /* 前置声明，页面之间可以相互跳转。 */
static void show_home(void); /* 声明登录后的主菜单。 */
static void show_songs(void); /* 声明歌曲选择页。 */
static void show_difficulties(void); /* 声明难度选择页。 */
static void show_result(void); /* 声明结算页面。 */
static void show_ranking(void); /* 声明排行榜页面。 */
static void show_settings(void); /* 声明设置页面。 */
static void show_help(void); /* 声明操作说明页面。 */

static void to_wide(const char *text, wchar_t *output, int capacity) /* 将 UTF-8 转为窗口使用的 UTF-16。 */
{ /* 函数开始。 */
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, output, capacity)) /* 严格转换文字。 */
        output[0] = 0; /* 无法转换时不显示损坏字符。 */
} /* 函数结束。 */

static void show_error(const char *text) /* 使用图形消息框提示失败。 */
{ /* 函数开始。 */
    wchar_t wide[512]; /* 保存中文错误文字。 */
    to_wide(text, wide, 512); /* 转换错误编码。 */
    MessageBoxW(window, wide, L"提示", MB_OK | MB_ICONINFORMATION); /* 用户无需查看终端。 */
} /* 函数结束。 */

static HWND control(const wchar_t *kind, const wchar_t *text, DWORD style, int x, int y, int width, int height, int id) /* 创建一个基础控件。 */
{ /* 函数开始。 */
    HWND child; /* 保存新控件句柄。 */
    child = CreateWindowExW(0, kind, text, WS_CHILD | WS_VISIBLE | style, x, y, width, height, window, (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL); /* 创建系统自带控件。 */
    SendMessageW(child, WM_SETFONT, (WPARAM)font, TRUE); /* 应用统一中文字体。 */
    return child; /* 返回控件，供设置限制或读取内容。 */
} /* 函数结束。 */

static void label(const wchar_t *text, int x, int y, int width, int height) /* 创建文字说明。 */
{ /* 函数开始。 */
    control(L"STATIC", text, SS_LEFT, x, y, width, height, 0); /* 使用系统文字控件。 */
} /* 函数结束。 */

static void label_utf8(const char *text, int x, int y, int width, int height) /* 显示由数据模块提供的文字。 */
{ /* 函数开始。 */
    wchar_t wide[1200]; /* 保存转换后的较长页面文字。 */
    to_wide(text, wide, 1200); /* 转为窗口宽字符。 */
    label(wide, x, y, width, height); /* 创建普通文字控件。 */
} /* 函数结束。 */

static void button(const wchar_t *text, int x, int y, int id) /* 创建大小统一的按钮。 */
{ /* 函数开始。 */
    control(L"BUTTON", text, WS_TABSTOP | BS_PUSHBUTTON, x, y, 240, 44, id); /* 使用系统按钮。 */
} /* 函数结束。 */

static void new_page(int nextPage, const wchar_t *title) /* 销毁旧页面控件，再显示新页面。 */
{ /* 函数开始。 */
    HWND child = GetWindow(window, GW_CHILD); /* 查找第一个旧控件。 */
    while (child != NULL) /* 逐个清除上一页控件。 */
    { /* 控件清理循环开始。 */
        HWND next = GetWindow(child, GW_HWNDNEXT); /* 先保存下一个控件句柄。 */
        DestroyWindow(child); /* 释放本页控件。 */
        child = next; /* 继续处理下一个。 */
    } /* 控件清理循环结束。 */
    page = nextPage; /* 更新当前页面。 */
    SetWindowTextW(window, title); /* 标题同时说明当前所在页面。 */
    InvalidateRect(window, NULL, TRUE); /* 请求重绘页面背景。 */
} /* 函数结束。 */

static void show_login(int registration) /* 显示注册或登录页面，密码框不会保留旧值。 */
{ /* 函数开始。 */
    HWND edit; /* 保存需要限制字符数的输入框。 */
    wchar_t name[NAME_SIZE]; /* 保存上次输入的账号文本。 */
    registering = registration; /* 设置此页提交动作。 */
    new_page(PAGE_LOGIN, L"音符碰撞 - 注册登录"); /* 替换上一页。 */
    label(L"音符碰撞", 100, 45, 800, 45); /* 显示程序名称。 */
    label(registration ? L"注册新账号" : L"登录已有账号", 100, 110, 700, 35); /* 提示当前操作。 */
    label(L"账号（1~16 个字符，区分大小写）", 100, 170, 700, 30); /* 说明账号要求。 */
    to_wide(pendingName, name, NAME_SIZE); /* 注册完成后保留账号文本。 */
    edit = control(L"EDIT", name, WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, 100, 205, 600, 34, ID_NAME); /* 创建账号框。 */
    SendMessageW(edit, EM_LIMITTEXT, 16, 0); /* 限制输入的 UTF-16 单元数。 */
    label(L"密码（1~64 个字符，首尾空格会保留）", 100, 260, 750, 30); /* 说明密码要求。 */
    edit = control(L"EDIT", L"", WS_BORDER | WS_TABSTOP | ES_PASSWORD | ES_AUTOHSCROLL, 100, 295, 600, 34, ID_PASSWORD); /* 创建密码框。 */
    SendMessageW(edit, EM_LIMITTEXT, 64, 0); /* 限制密码长度。 */
    if (registration) /* 只有注册时显示确认密码。 */
    { /* 注册控件分支开始。 */
        label(L"再次输入密码", 100, 350, 650, 30); /* 提示确认密码。 */
        edit = control(L"EDIT", L"", WS_BORDER | WS_TABSTOP | ES_PASSWORD | ES_AUTOHSCROLL, 100, 385, 600, 34, ID_CONFIRM); /* 创建确认框。 */
        SendMessageW(edit, EM_LIMITTEXT, 64, 0); /* 保持与密码框相同的上限。 */
    } /* 注册控件分支结束。 */
    button(registration ? L"注册" : L"登录", 100, 460, ID_SUBMIT); /* 创建提交按钮。 */
    button(registration ? L"已有账号，去登录" : L"没有账号，去注册", 380, 460, ID_SWITCH); /* 切换表单模式。 */
    label(L"账号、设置和成绩保存在本程序旁边的 data 文件夹。", 100, 560, 850, 60); /* 说明本地保存位置。 */
    SetFocus(GetDlgItem(window, ID_NAME)); /* 优先输入账号。 */
} /* 函数结束。 */

static int read_text(int id, char *text, int capacity) /* 读取输入框并转换成 UTF-8。 */
{ /* 函数开始。 */
    wchar_t wide[260]; /* 保存宽字符输入。 */
    GetDlgItemTextW(window, id, wide, 260); /* 从原生编辑框读取文本。 */
    if (!WideCharToMultiByte(CP_UTF8, 0, wide, -1, text, capacity, NULL, NULL)) /* 检查输出容量。 */
    { /* 文本过长分支开始。 */
        text[0] = 0; /* 不使用截断文本作为账号或密码。 */
        return 0; /* 告知读取失败。 */
    } /* 文本过长分支结束。 */
    return 1; /* 输入读取完成。 */
} /* 函数结束。 */

static void login_command(int id) /* 只处理账号页面的按钮。 */
{ /* 函数开始。 */
    char password[PASSWORD_SIZE]; /* 临时保存输入密码，操作后立即清除。 */
    char confirmation[PASSWORD_SIZE]; /* 临时保存确认密码。 */
    int okay; /* 保存注册或登录结果。 */
    if (id != ID_SUBMIT && id != ID_SWITCH) /* 忽略其他控件通知。 */
        return; /* 不执行账号操作。 */
    if (!read_text(ID_NAME, pendingName, sizeof(pendingName))) /* 读取账号。 */
        return; /* 超长编码不进入验证。 */
    if (id == ID_SWITCH) /* 用户要切换注册或登录模式。 */
    { /* 模式切换分支开始。 */
        show_login(!registering); /* 重建页面会清空密码框。 */
        return; /* 本次不提交。 */
    } /* 模式切换分支结束。 */
    if (!read_text(ID_PASSWORD, password, sizeof(password))) /* 读取密码文本。 */
        return; /* 无法读取时不继续。 */
    if (registering) /* 注册需要额外核对确认密码。 */
    { /* 注册分支开始。 */
        read_text(ID_CONFIRM, confirmation, sizeof(confirmation)); /* 读取确认密码。 */
        okay = strcmp(password, confirmation) == 0; /* 确认两次输入一致。 */
        SecureZeroMemory(confirmation, sizeof(confirmation)); /* 不保留确认密码。 */
        if (!okay) /* 两次密码不同。 */
        { /* 不一致分支开始。 */
            SecureZeroMemory(password, sizeof(password)); /* 清理本次密码。 */
            show_error("两次密码输入不一致。"); /* 显示图形提示。 */
            return; /* 不创建账号。 */
        } /* 不一致分支结束。 */
        okay = account_register(pendingName, password, message); /* 调用独立存储模块注册。 */
    } /* 注册分支结束。 */
    else /* 登录只验证已有账号。 */
        okay = account_login(pendingName, password, message); /* 不在绘图函数中读文件。 */
    SecureZeroMemory(password, sizeof(password)); /* 尽早清除临时密码。 */
    if (!okay) /* 认证或文件保存失败。 */
    { /* 错误分支开始。 */
        show_error(message); /* 显示失败原因。 */
        return; /* 保留当前页供用户修正。 */
    } /* 错误分支结束。 */
    if (registering) /* 注册成功后仍需要登录。 */
    { /* 注册成功分支开始。 */
        show_login(0); /* 返回登录页且保留账号名。 */
        show_error("注册成功，请输入密码登录。"); /* 明确还没有进入游戏。 */
    } /* 注册成功分支结束。 */
    else /* 登录已经通过。 */
    { /* 登录成功分支开始。 */
        snprintf(playerName, sizeof(playerName), "%s", pendingName); /* 保存经过验证的账号身份。 */
        show_home(); /* 跳转主菜单。 */
    } /* 登录成功分支结束。 */
} /* 函数结束。 */

static void show_home(void) /* 显示单人版主菜单。 */
{ /* 函数开始。 */
    char text[200]; /* 保存当前账号说明。 */
    new_page(PAGE_HOME, L"音符碰撞 - 主菜单"); /* 清理旧页。 */
    label(L"音符碰撞 · 单人练习", 100, 55, 800, 45); /* 显示页面名称。 */
    snprintf(text, sizeof(text), "当前账号：%s", playerName); /* 仅使用登录成功的账号。 */
    label_utf8(text, 100, 125, 800, 35); /* 显示当前身份。 */
    button(L"开始单人游戏", 100, 210, ID_START); /* 下一页选择歌曲。 */
    button(L"排行榜", 400, 210, ID_RANKING); /* 查看同曲同难度的最佳成绩。 */
    button(L"音量和输入校准", 100, 300, ID_SETTINGS); /* 进入设置页。 */
    button(L"操作说明", 400, 300, ID_HELP); /* 查看按键和规则。 */
    button(L"退出当前账号", 100, 420, ID_LOGOUT); /* 返回独立登录界面。 */
    label(L"使用 D / F / J / K 击打四条轨道。先选曲，再选难度。", 100, 540, 850, 70); /* 说明基础操作。 */
} /* 函数结束。 */

static void show_songs(void) /* 先选择歌曲，不把全部选项挤在同一页。 */
{ /* 函数开始。 */
    new_page(PAGE_SONG, L"音符碰撞 - 选择歌曲"); /* 切换歌曲页。 */
    label(L"第一步：选择歌曲", 100, 70, 800, 40); /* 说明当前步骤。 */
    button(L"小星星", 100, 200, ID_SONG0); /* 提供公版旋律练习曲。 */
    button(L"霓虹脉冲", 400, 200, ID_SONG1); /* 提供原仓库的合成曲目。 */
    button(L"返回主菜单", 100, 470, ID_BACK); /* 可以退回上一层。 */
} /* 函数结束。 */

static void show_difficulties(void) /* 歌曲确定后，再选择对应谱面难度。 */
{ /* 函数开始。 */
    char text[200]; /* 保存当前歌曲名称。 */
    new_page(PAGE_DIFFICULTY, L"音符碰撞 - 选择难度"); /* 切换难度页。 */
    snprintf(text, sizeof(text), "第二步：为《%s》选择难度", song_title(selectedSong)); /* 显示当前歌曲。 */
    label_utf8(text, 100, 70, 800, 45); /* 显示步骤标题。 */
    button(L"简单 Easy", 100, 180, ID_EASY); /* 选择低密度谱面。 */
    button(L"普通 Normal", 100, 260, ID_NORMAL); /* 选择中等难度谱面。 */
    button(L"困难 Hard", 100, 340, ID_HARD); /* 选择更复杂的组合谱面。 */
    label(L"点击难度后，会加载音乐和谱面，再进行三秒倒计时。", 100, 420, 850, 50); /* 说明下一步。 */
    button(L"返回选歌", 100, 520, ID_BACK); /* 返回歌曲页。 */
} /* 函数结束。 */

static void start_game(void) /* 完整加载资源后才进入游戏页。 */
{ /* 函数开始。 */
    if (!chart_load(selectedSong, selectedDifficulty, &chosenChart, message)) /* 加载并校验选定谱面。 */
    { /* 谱面失败分支开始。 */
        show_error(message); /* 留在选择页，允许重新选择。 */
        return; /* 不进入空白游戏。 */
    } /* 谱面失败分支结束。 */
    if (!music_open(selectedSong, settings.volume, message)) /* 实际预加载音乐。 */
    { /* 音频失败分支开始。 */
        show_error(message); /* 显示系统音频错误。 */
        return; /* 不在没有音乐时开始判定。 */
    } /* 音频失败分支结束。 */
    game_start(&game, &chosenChart, settings.offset); /* 初始化本机游戏副本。 */
    phase = PHASE_COUNTDOWN; /* 先倒计时，不立即推进音符。 */
    resumeRequested = 0; /* 清除上一局继续请求。 */
    startAt = GetTickCount64() + 3000; /* 三秒后安排音乐播放。 */
    saved = 0; /* 新一局尚未保存。 */
    new_page(PAGE_GAME, L"音符碰撞 - 正在游戏"); /* 创建独立游戏页。 */
    button(L"暂停 / 继续", 750, 490, ID_PAUSE); /* 单人允许暂停。 */
    button(L"中止，返回选曲", 750, 555, ID_ABORT); /* 中止局不保存。 */
    SetFocus(window); /* 将键盘输入交给四轨游戏。 */
} /* 函数结束。 */

static void abort_game(void) /* 主动中止或音频异常时放弃本局。 */
{ /* 函数开始。 */
    music_close(); /* 停止播放并释放音频设备。 */
    game.state = GAME_READY; /* 终止本局判定。 */
    show_difficulties(); /* 允许重新选择相同歌曲难度。 */
} /* 函数结束。 */

static void try_resume(void) /* 等待长按键重新按住，再继续音乐。 */
{ /* 函数开始。 */
    if (!resumeRequested || phase != PHASE_PAUSED) /* 只有用户明确请求继续才尝试恢复。 */
        return; /* 保持暂停。 */
    if (!game_resume(&game)) /* 尚有活动长按没有重新按住。 */
        return; /* 界面继续显示按键提示。 */
    if (!music_resume(message)) /* 先确认音频恢复成功。 */
    { /* 音频恢复失败分支开始。 */
        abort_game(); /* 中止未完成局次。 */
        show_error(message); /* 提示不能继续的原因。 */
        return; /* 不继续无声判定。 */
    } /* 音频恢复失败分支结束。 */
    phase = PHASE_PLAYING; /* 音符判定重新随音乐推进。 */
    resumeRequested = 0; /* 本次继续请求已完成。 */
    SetFocus(window); /* 继续接收轨道按键。 */
} /* 函数结束。 */

static void pause_game(void) /* 处理游戏中的暂停按钮及 Esc。 */
{ /* 函数开始。 */
    int position; /* 暂停前的实际音乐位置。 */
    if (phase == PHASE_COUNTDOWN) /* 倒计时期间按暂停等同于取消准备。 */
    { /* 取消准备分支开始。 */
        abort_game(); /* 不开始音乐，也不保存成绩。 */
        return; /* 返回选曲。 */
    } /* 取消准备分支结束。 */
    if (phase == PHASE_PAUSED) /* 已暂停时按钮表示请求继续。 */
    { /* 继续请求分支开始。 */
        resumeRequested = 1; /* 长按未重新按住时仍不会立即恢复。 */
        try_resume(); /* 检查恢复条件。 */
        SetFocus(window); /* 允许玩家重新按住长按轨道。 */
        return; /* 不重复执行暂停操作。 */
    } /* 继续请求分支结束。 */
    position = music_position(); /* 从同一音频时间基准获得暂停位置。 */
    if (position >= game.now) /* 只接受单调前进的位置。 */
        game_update(&game, position); /* 结算暂停前真实已经发生的事件。 */
    if (!music_pause(message)) /* 音乐不能暂停时不假装成功。 */
    { /* 暂停失败分支开始。 */
        abort_game(); /* 中止未完成局。 */
        show_error(message); /* 显示错误。 */
        return; /* 不继续运行。 */
    } /* 暂停失败分支结束。 */
    game_pause(&game); /* 冻结判定并记录活动长按状态。 */
    phase = PHASE_PAUSED; /* 进入暂停显示阶段。 */
    resumeRequested = 0; /* 等待用户明确继续。 */
    SetFocus(window); /* 暂停后仍可读取轨道键。 */
} /* 函数结束。 */

static void finish_game(void) /* 音乐及判定都完成后生成可保存成绩。 */
{ /* 函数开始。 */
    SYSTEMTIME time; /* 保存本地完成时间。 */
    music_close(); /* 自然完成后停止音频设备。 */
    memset(&finalRecord, 0, sizeof(finalRecord)); /* 清除上一局最终结果。 */
    snprintf(finalRecord.name, sizeof(finalRecord.name), "%s", playerName); /* 关联当前登录账号。 */
    finalRecord.song = selectedSong; /* 记录本次歌曲。 */
    finalRecord.difficulty = selectedDifficulty; /* 记录本次难度。 */
    finalRecord.noteCount = game.chart.count; /* 固定本局总判定量。 */
    finalRecord.stats = game.stats; /* 保存原始判定和连击统计。 */
    GetLocalTime(&time); /* 读取实际完成时刻。 */
    snprintf(finalRecord.date, sizeof(finalRecord.date), "%04d-%02d-%02d_%02d:%02d:%02d", time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond); /* 保存便于阅读的时间。 */
    saved = record_save(&finalRecord, message); /* 只保存完整成绩，较低分数不覆盖最佳。 */
    show_result(); /* 切换结算界面。 */
} /* 函数结束。 */

static void update_game(void) /* 每十六毫秒调用一次，时间仍以音频位置为准。 */
{ /* 函数开始。 */
    int position; /* 当前实际音乐毫秒位置。 */
    if (page != PAGE_GAME) /* 其他页面没有游戏需要推进。 */
        return; /* 立即返回。 */
    if (phase == PHASE_COUNTDOWN) /* 倒计时只等待目标时刻。 */
    { /* 倒计时分支开始。 */
        if (GetTickCount64() < startAt) /* 尚未到达三秒目标。 */
            return; /* 不提前开始判定。 */
        if (!music_play(message)) /* 请求从零位置播放音频。 */
        { /* 起播失败分支开始。 */
            abort_game(); /* 放弃没有成功起播的局次。 */
            show_error(message); /* 显示原因。 */
            return; /* 不继续推进音符。 */
        } /* 起播失败分支结束。 */
        phase = PHASE_PLAYING; /* 之后由实际音乐位置驱动游戏。 */
    } /* 倒计时分支结束。 */
    if (phase == PHASE_PAUSED) /* 暂停时只检查恢复条件。 */
    { /* 暂停分支开始。 */
        try_resume(); /* 未满足长按恢复条件时继续等待。 */
        return; /* 暂停期间不会增加音乐判定时间。 */
    } /* 暂停分支结束。 */
    position = music_position(); /* 查询播放器的位置。 */
    if (position < 0) /* 音频设备位置查询失败。 */
    { /* 音频失效分支开始。 */
        abort_game(); /* 不使用系统时钟伪造音乐进度。 */
        show_error("音频播放位置读取失败，本局已中止。"); /* 明确不保存未完成局。 */
        return; /* 等待用户重新开局。 */
    } /* 音频失效分支结束。 */
    game_update(&game, position); /* 自动处理漏击和长按尾部。 */
    if (music_stopped()) /* 可能自然结束，也可能设备意外停止。 */
    { /* 音频停止分支开始。 */
        if (position >= game.chart.duration - 100) /* 到达已知音频尾部。 */
        { /* 自然结束分支开始。 */
            game_update(&game, game.chart.duration + 500); /* 完成所有仍待处理的漏击。 */
            finish_game(); /* 校验并保存完整成绩。 */
        } /* 自然结束分支结束。 */
        else /* 过早停止不能当作自然演奏完成。 */
        { /* 意外停止分支开始。 */
            abort_game(); /* 不保存异常中止的局次。 */
            show_error("音乐意外提前停止，本局没有保存。"); /* 提示重新尝试。 */
        } /* 意外停止分支结束。 */
    } /* 音频停止分支结束。 */
} /* 函数结束。 */

static void show_result(void) /* 显示最终结果及最佳成绩保存情况。 */
{ /* 函数开始。 */
    Result result = stats_result(&finalRecord.stats, finalRecord.noteCount); /* 使用与排行榜相同的规则。 */
    char text[1100]; /* 保存结算文字。 */
    new_page(PAGE_RESULT, L"音符碰撞 - 本局结果"); /* 切换结算页。 */
    label(L"本局演奏完成", 100, 55, 850, 45); /* 显示页面标题。 */
    snprintf(text, sizeof(text), "账号：%s\n歌曲：%s  /  %s\n\n得分：%d    评级：%c\n准确率：%.2f%%    最大连击：%d\n\nPerfect：%d    Great：%d\nGood：%d    Miss：%d\n\n%s", playerName, song_title(selectedSong), difficulty_title(selectedDifficulty), result.score, result.grade, result.accuracy, finalRecord.stats.maxCombo, finalRecord.stats.perfect, finalRecord.stats.great, finalRecord.stats.good, finalRecord.stats.miss, saved ? "排行榜已处理，只保留此账号在本曲本难度的最高分。" : message); /* 生成易读结果。 */
    label_utf8(text, 100, 125, 880, 360); /* 在原生界面显示完整统计。 */
    button(L"再玩同一首", 100, 530, ID_RETRY); /* 重新完整加载并倒计时。 */
    button(L"查看排行榜", 400, 530, ID_RANKING); /* 查看当前曲目对应榜单。 */
    button(L"返回主菜单", 100, 600, ID_BACK); /* 可以退出结算。 */
    if (!saved) /* 文件错误时提供明确重试入口。 */
        button(L"重新保存成绩", 400, 600, ID_SAVE_AGAIN); /* 不虚报保存成功。 */
} /* 函数结束。 */

static void refresh_ranking(void) /* 当前筛选变化时重新加载和排序。 */
{ /* 函数开始。 */
    int order[MAX_RECORDS]; /* 保存基础插入排序生成的下标表。 */
    int count; /* 文件中的最佳成绩数量。 */
    int used; /* 当前榜单的实际条目数。 */
    int i; /* 逐条显示记录。 */
    int rank = 0; /* 同分显示相同名次。 */
    int previousScore = -1; /* 记录上一条的分数。 */
    HWND list = GetDlgItem(window, ID_RANK_LIST); /* 取得带滚动条的原生列表。 */
    SendMessageW(list, LB_RESETCONTENT, 0, 0); /* 清除上一次筛选结果。 */
    count = records_load(rankRecords, message); /* 每次读取最新完整文件。 */
    if (count < 0) /* 损坏文件不能显示部分记录。 */
    { /* 文件错误分支开始。 */
        show_error(message); /* 保留原文件并提示错误。 */
        return; /* 不将错误伪装为空榜。 */
    } /* 文件错误分支结束。 */
    used = ranking_make(rankRecords, count, rankSong, rankDifficulty, order); /* 当前曲目和难度分开排序。 */
    for (i = 0; i < used; i++) /* 列表自带滚动条，可查看全部条目。 */
    { /* 显示循环开始。 */
        const Record *record = &rankRecords[order[i]]; /* 借助下标取得当前条目。 */
        Result result = stats_result(&record->stats, record->noteCount); /* 重算可信成绩。 */
        char text[350]; /* 保存一行显示文字。 */
        wchar_t wide[350]; /* 保存转换后的列表文字。 */
        if (result.score != previousScore) /* 不同分数使用新的顺序名次。 */
            rank = i + 1; /* 同分后采用一、二、二、四式排名。 */
        previousScore = result.score; /* 供下一条比较。 */
        snprintf(text, sizeof(text), "%d.  %s    %d 分    %.2f%%    %c    最大连击 %d", rank, record->name, result.score, result.accuracy, result.grade, record->stats.maxCombo); /* 生成榜单行。 */
        to_wide(text, wide, 350); /* 转换中文账号。 */
        SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)wide); /* 添加到可滚动列表。 */
    } /* 显示循环结束。 */
    if (used == 0) /* 新账号或首次运行可能没有记录。 */
        SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)L"这个榜单还没有完整成绩。"); /* 显示明确空榜说明。 */
} /* 函数结束。 */

static void show_ranking(void) /* 创建独立排行榜页。 */
{ /* 函数开始。 */
    HWND combo; /* 保存歌曲或难度下拉框。 */
    int i; /* 为下拉框填入固定选项。 */
    wchar_t text[100]; /* 保存每项中文文字。 */
    new_page(PAGE_RANKING, L"音符碰撞 - 排行榜"); /* 切换排行榜页。 */
    label(L"排行榜：每个账号、每首歌、每个难度只保留最高分", 70, 45, 900, 40); /* 说明榜单规则。 */
    combo = control(L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, 70, 115, 320, 160, ID_RANK_SONG); /* 创建歌曲筛选。 */
    for (i = 0; i < 2; i++) /* 添加两首歌曲。 */
    { /* 歌曲列表循环开始。 */
        to_wide(song_title(i), text, 100); /* 转为宽字符。 */
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)text); /* 添加选项。 */
    } /* 歌曲列表循环结束。 */
    SendMessageW(combo, CB_SETCURSEL, rankSong, 0); /* 保留当前歌曲筛选。 */
    combo = control(L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, 430, 115, 320, 160, ID_RANK_DIFFICULTY); /* 创建难度筛选。 */
    for (i = 0; i < 3; i++) /* 添加三个难度。 */
    { /* 难度列表循环开始。 */
        to_wide(difficulty_title(i), text, 100); /* 转换难度文字。 */
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)text); /* 添加选项。 */
    } /* 难度列表循环结束。 */
    SendMessageW(combo, CB_SETCURSEL, rankDifficulty, 0); /* 保留当前难度筛选。 */
    control(L"LISTBOX", L"", WS_BORDER | WS_VSCROLL | LBS_NOINTEGRALHEIGHT, 70, 185, 880, 380, ID_RANK_LIST); /* 使用原生滚动列表。 */
    button(L"返回主菜单", 70, 620, ID_BACK); /* 退出排行榜。 */
    refresh_ranking(); /* 填入当前筛选结果。 */
} /* 函数结束。 */

static void show_settings(void) /* 使用两个数值输入框展示基础设置。 */
{ /* 函数开始。 */
    wchar_t number[32]; /* 格式化当前数值。 */
    new_page(PAGE_SETTINGS, L"音符碰撞 - 设置"); /* 切换设置页。 */
    label(L"音乐和输入校准", 100, 65, 850, 45); /* 显示标题。 */
    label(L"音乐音量：0~100", 100, 170, 750, 30); /* 说明音量范围。 */
    swprintf(number, 32, L"%d", settings.volume); /* 显示当前音量。 */
    control(L"EDIT", number, WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, 100, 210, 300, 35, ID_VOLUME); /* 创建音量输入框。 */
    label(L"输入偏移：-200~200 毫秒；正数将输入时刻向后校准", 100, 290, 850, 40); /* 说明校准意义。 */
    swprintf(number, 32, L"%d", settings.offset); /* 显示当前偏移。 */
    control(L"EDIT", number, WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, 100, 335, 300, 35, ID_OFFSET); /* 创建偏移输入框。 */
    button(L"保存并返回", 100, 460, ID_SAVE_SETTINGS); /* 保存到本程序数据目录。 */
    button(L"取消，返回主菜单", 400, 460, ID_BACK); /* 取消未保存的修改。 */
    if (!settingsWritable) /* 本次启动发现了损坏设置文件。 */
        label(L"设置文件有误，正在使用默认值；请先检查 data/settings.txt。", 100, 560, 850, 70); /* 不默默覆盖旧文件。 */
} /* 函数结束。 */

static int read_number(int id, int *value) /* 严格读取整个整数输入，不接受后缀垃圾文字。 */
{ /* 函数开始。 */
    wchar_t text[100]; /* 保存完整编辑框输入。 */
    wchar_t *end; /* 指向整数后第一个未解析字符。 */
    long number; /* 先用较大整数类型解析。 */
    GetDlgItemTextW(window, id, text, 100); /* 读取原生输入框。 */
    errno = 0; /* 清除此前溢出标记。 */
    number = wcstol(text, &end, 10); /* 解析十进制整数。 */
    if (end == text || *end != 0 || errno == ERANGE || number < -10000 || number > 10000) /* 检查完整解析及范围。 */
        return 0; /* 不使用错误或截断的整数。 */
    *value = (int)number; /* 输出经过初步范围检查的整数。 */
    return 1; /* 数字读取成功。 */
} /* 函数结束。 */

static void save_settings_page(void) /* 处理设置页的保存动作。 */
{ /* 函数开始。 */
    Settings candidate; /* 保存用户尚未提交的新值。 */
    if (!settingsWritable) /* 保留启动时发现的损坏文件。 */
    { /* 不可写分支开始。 */
        show_error("原设置文件损坏，已保留。请先检查或备份该文件后重新启动。"); /* 说明不能覆盖的原因。 */
        return; /* 不写文件。 */
    } /* 不可写分支结束。 */
    if (!read_number(ID_VOLUME, &candidate.volume) || !read_number(ID_OFFSET, &candidate.offset)) /* 检查数字输入。 */
    { /* 数字错误分支开始。 */
        show_error("请输入完整的整数。"); /* 不接受数字后夹杂文字。 */
        return; /* 允许用户继续修改。 */
    } /* 数字错误分支结束。 */
    if (!settings_save(&candidate, message)) /* 由存储模块检查业务范围并保存。 */
    { /* 保存失败分支开始。 */
        show_error(message); /* 显示文件错误。 */
        return; /* 不修改正在使用的设置。 */
    } /* 保存失败分支结束。 */
    settings = candidate; /* 文件提交成功后才更新内存值。 */
    show_home(); /* 返回主菜单。 */
} /* 函数结束。 */

static void show_help(void) /* 显示实际实现的操作和规则。 */
{ /* 函数开始。 */
    new_page(PAGE_HELP, L"音符碰撞 - 操作说明"); /* 切换说明页。 */
    label(L"操作说明", 80, 45, 850, 45); /* 显示标题。 */
    label(L"1. 注册账号并登录，再依次选择歌曲和难度。\n\n2. 用 D / F / J / K 对应从左到右的四条轨道。\n\n3. 单点和连续节点到线时按下；长按要按住直到尾部。\n\n4. 双押或三押同时按相应轨道键，各音符分别判定。\n\n5. Esc 暂停，Enter 继续。暂停前有长按时，需要重新按住。\n\n6. 完美 ±150ms，优秀 ±250ms，良好 ±350ms，越界为漏击。\n\n7. 满分一百万；S 要求准确率至少 95%，最大连击至少覆盖 80%。\n\n8. 自然完成自动处理最佳成绩；中止和关闭不会保存未完成局。", 80, 120, 880, 470); /* 按实际功能说明玩法。 */
    button(L"返回主菜单", 80, 630, ID_BACK); /* 返回上一层。 */
} /* 函数结束。 */

static void home_command(int id) /* 主菜单只负责跳转，避免一个巨大动作函数。 */
{ /* 函数开始。 */
    switch (id) /* 按主菜单按钮处理。 */
    { /* 分派开始。 */
    case ID_START: /* 用户开始单人准备。 */
        show_songs(); /* 先进入选歌页。 */
        break; /* 当前按钮处理结束。 */
    case ID_RANKING: /* 用户查看排行榜。 */
        show_ranking(); /* 显示独立榜单页。 */
        break; /* 当前按钮处理结束。 */
    case ID_SETTINGS: /* 用户修改设置。 */
        show_settings(); /* 显示音量与校准。 */
        break; /* 当前按钮处理结束。 */
    case ID_HELP: /* 用户查看操作说明。 */
        show_help(); /* 显示规则。 */
        break; /* 当前按钮处理结束。 */
    case ID_LOGOUT: /* 用户主动退出当前身份。 */
        music_close(); /* 确保不遗留音频设备。 */
        playerName[0] = 0; /* 清除已登录账号。 */
        pendingName[0] = 0; /* 不向下一位玩家保留账号输入。 */
        show_login(0); /* 返回独立登录界面。 */
        break; /* 当前按钮处理结束。 */
    } /* 分派结束。 */
} /* 函数结束。 */

static void selection_command(int id) /* 处理选歌和选难度页。 */
{ /* 函数开始。 */
    if (id == ID_BACK) /* 用户返回上一层。 */
    { /* 返回分支开始。 */
        if (page == PAGE_SONG) /* 歌曲页的上一级是主菜单。 */
            show_home(); /* 返回主菜单。 */
        else /* 难度页的上一级是选歌页。 */
            show_songs(); /* 返回歌曲选择。 */
        return; /* 不再执行选择逻辑。 */
    } /* 返回分支结束。 */
    if (page == PAGE_SONG && (id == ID_SONG0 || id == ID_SONG1)) /* 只接受当前页的歌曲按钮。 */
    { /* 歌曲选择分支开始。 */
        selectedSong = id - ID_SONG0; /* 两个连续编号对应两首歌。 */
        show_difficulties(); /* 下一页再选难度。 */
        return; /* 不直接开始游戏。 */
    } /* 歌曲选择分支结束。 */
    if (page == PAGE_DIFFICULTY && id >= ID_EASY && id <= ID_HARD) /* 只接受当前页的难度按钮。 */
    { /* 难度选择分支开始。 */
        selectedDifficulty = id - ID_EASY; /* 转为零到二的难度下标。 */
        start_game(); /* 加载当前歌曲的对应谱面。 */
    } /* 难度选择分支结束。 */
} /* 函数结束。 */

static void result_command(int id) /* 单独处理结算页，不混入登录和联机状态。 */
{ /* 函数开始。 */
    if (id == ID_RETRY) /* 用户再次挑战相同曲目。 */
        start_game(); /* 重置统计并重新倒计时。 */
    else if (id == ID_RANKING) /* 用户查看当前曲目排行榜。 */
    { /* 排行榜分支开始。 */
        rankSong = selectedSong; /* 使用本局歌曲作为筛选。 */
        rankDifficulty = selectedDifficulty; /* 使用本局难度作为筛选。 */
        show_ranking(); /* 打开榜单。 */
    } /* 排行榜分支结束。 */
    else if (id == ID_SAVE_AGAIN) /* 文件暂时不可写后允许重试。 */
    { /* 重试保存分支开始。 */
        saved = record_save(&finalRecord, message); /* 仍然需要完整性校验。 */
        show_result(); /* 刷新保存状态。 */
    } /* 重试保存分支结束。 */
    else if (id == ID_BACK) /* 用户退出本局结算。 */
        show_home(); /* 返回主菜单。 */
} /* 函数结束。 */

static void on_command(int id, int notification) /* 先按当前页面分流，避免所有操作挤在一个函数。 */
{ /* 函数开始。 */
    if (page == PAGE_LOGIN) /* 登录页有自己的处理函数。 */
        login_command(id); /* 只允许处理账号操作。 */
    else if (page == PAGE_HOME) /* 主菜单负责功能入口。 */
        home_command(id); /* 按按钮跳转。 */
    else if (page == PAGE_SONG || page == PAGE_DIFFICULTY) /* 选择页负责歌曲与难度。 */
        selection_command(id); /* 处理逐页准备。 */
    else if (page == PAGE_RESULT) /* 已完成本局的操作独立处理。 */
        result_command(id); /* 不允许重新修改本局原始统计。 */
    else if (page == PAGE_GAME) /* 游戏页只保留暂停和中止。 */
    { /* 游戏按钮分支开始。 */
        if (id == ID_PAUSE) /* 用户点暂停或继续。 */
            pause_game(); /* 保持音乐和判定同步。 */
        else if (id == ID_ABORT) /* 用户主动放弃本局。 */
            abort_game(); /* 未完成成绩不保存。 */
    } /* 游戏按钮分支结束。 */
    else if (page == PAGE_SETTINGS && id == ID_SAVE_SETTINGS) /* 设置页提交新数值。 */
        save_settings_page(); /* 校验后保存。 */
    else if (page == PAGE_RANKING && notification == CBN_SELCHANGE) /* 排行榜下拉选择变化。 */
    { /* 筛选变化分支开始。 */
        rankSong = (int)SendDlgItemMessageW(window, ID_RANK_SONG, CB_GETCURSEL, 0, 0); /* 读取歌曲筛选。 */
        rankDifficulty = (int)SendDlgItemMessageW(window, ID_RANK_DIFFICULTY, CB_GETCURSEL, 0, 0); /* 读取难度筛选。 */
        refresh_ranking(); /* 重算并刷新榜单。 */
    } /* 筛选变化分支结束。 */
    else if (id == ID_BACK) /* 设置、说明和排行榜都可以返回。 */
        show_home(); /* 返回登录后的主菜单。 */
} /* 函数结束。 */

static int key_lane(WPARAM key) /* 将 D、F、J、K 转换为轨道编号。 */
{ /* 函数开始。 */
    if (key == 'D') /* 最左侧轨道。 */
        return 0; /* 返回轨道零。 */
    if (key == 'F') /* 左侧第二轨。 */
        return 1; /* 返回轨道一。 */
    if (key == 'J') /* 右侧第二轨。 */
        return 2; /* 返回轨道二。 */
    if (key == 'K') /* 最右侧轨道。 */
        return 3; /* 返回轨道三。 */
    return -1; /* 其他键不是游戏轨道键。 */
} /* 函数结束。 */

static void on_key(WPARAM key, int pressed, int repeat) /* 仅在游戏页处理轨道按键。 */
{ /* 函数开始。 */
    int lane; /* 当前键对应的轨道。 */
    int position; /* 输入发生时的实际音乐位置。 */
    if (page != PAGE_GAME || repeat) /* 忽略非游戏页和系统自动重复按键。 */
        return; /* 不干扰账号输入框。 */
    if (pressed && (key == VK_ESCAPE || key == VK_RETURN)) /* Esc 暂停，Enter 请求继续。 */
    { /* 控制键分支开始。 */
        if (key == VK_ESCAPE || phase == PHASE_PAUSED) /* Enter 不能在运行中意外暂停。 */
            pause_game(); /* 执行暂停或继续操作。 */
        return; /* 控制键不是音符输入。 */
    } /* 控制键分支结束。 */
    if (phase == PHASE_COUNTDOWN) /* 准备期间不能提前保持按键命中。 */
        return; /* 等待音乐正式播放。 */
    lane = key_lane(key); /* 转成四轨编号。 */
    if (lane < 0) /* 无关按键不参与判定。 */
        return; /* 不影响分数。 */
    position = phase == PHASE_PAUSED ? game.now : music_position(); /* 暂停时冻结音乐时间。 */
    if (position < game.now) /* 不接受查询失败或音乐时间倒退。 */
        position = game.now; /* 暂时使用已确认位置。 */
    if (pressed) /* 处理真实按下。 */
        game_press(&game, lane, position); /* 返回值只决定命中，不直接另行加分。 */
    else /* 处理真实松开。 */
        game_release(&game, lane, position); /* 长按尾部使用同一音乐时钟。 */
    try_resume(); /* 暂停时按齐活动长按后可恢复。 */
} /* 函数结束。 */

static void fill_rectangle(HDC dc, int x, int y, int width, int height, COLORREF color) /* 绘制基础矩形。 */
{ /* 函数开始。 */
    RECT rectangle = {x, y, x + width, y + height}; /* 定义矩形范围。 */
    HBRUSH brush = CreateSolidBrush(color); /* 创建一次性纯色画刷。 */
    FillRect(dc, &rectangle, brush); /* 填充指定区域。 */
    DeleteObject(brush); /* 立即释放画刷，避免逐帧泄漏。 */
} /* 函数结束。 */

static void draw_text(HDC dc, int x, int y, const char *text) /* 在游戏画面显示 UTF-8 文本。 */
{ /* 函数开始。 */
    wchar_t wide[500]; /* 保存一次绘制使用的文字。 */
    RECT rectangle = {x, y, x + 850, y + 60}; /* 给文字提供足够显示宽度。 */
    to_wide(text, wide, 500); /* 转为 GDI 使用的宽字符。 */
    DrawTextW(dc, wide, -1, &rectangle, DT_LEFT | DT_TOP | DT_NOPREFIX); /* 显示实际文字内容。 */
} /* 函数结束。 */

static int note_y(int time) /* 按谱面目标时刻和实际音乐位置计算纵坐标。 */
{ /* 函数开始。 */
    return 570 - (int)((long long)(time - game.now) * 460 / game.chart.approach); /* 目标时刻恰好到判定线。 */
} /* 函数结束。 */

static void draw_notes(HDC dc) /* 分开绘制音符，避免主绘图函数包含所有分支。 */
{ /* 函数开始。 */
    int i; /* 遍历音符。 */
    for (i = 0; i < game.chart.count; i++) /* 只绘制未结算音符。 */
    { /* 音符绘制循环开始。 */
        const Note *note = &game.chart.notes[i]; /* 取得当前音符。 */
        int x = 85 + note->lane * 160; /* 四轨各占一百六十像素。 */
        int head = note_y(note->time); /* 头部显示位置。 */
        int tail = note_y(note->end); /* 长按尾部显示位置。 */
        COLORREF color = RGB(50, 110, 220); /* 单点默认显示蓝色。 */
        if (note->state == NOTE_HIT || note->state == NOTE_MISS) /* 已结算音符无需继续显示。 */
            continue; /* 跳过当前音符。 */
        if (head < 90 || tail > 625) /* 整个音符还未进入或已经离开轨道区域。 */
            continue; /* 不绘制不可见区域。 */
        if (head > 620) /* 长按头部经过判定线后保持在轨道下缘。 */
            head = 620; /* 限制到可见区域。 */
        if (tail < 100) /* 长按尾部尚在屏幕外时裁剪。 */
            tail = 100; /* 只画轨道内的部分。 */
        if (note->type == NOTE_HOLD) /* 长按用连接头尾的长条表示。 */
        { /* 长按绘制分支开始。 */
            color = RGB(35, 150, 85); /* 长按显示绿色。 */
            fill_rectangle(dc, x + 40, tail, 50, head - tail + 14, RGB(150, 215, 170)); /* 显示保持区间。 */
            fill_rectangle(dc, x, tail, 130, 14, color); /* 显示长按尾部。 */
        } /* 长按绘制分支结束。 */
        else if (note->type == NOTE_CHAIN) /* 连续节点使用另一种颜色。 */
            color = RGB(155, 65, 195); /* 连续节点显示紫色。 */
        fill_rectangle(dc, x, head, 130, 16, color); /* 显示每个音符头部。 */
    } /* 音符绘制循环结束。 */
} /* 函数结束。 */

static void draw_game(HDC dc) /* 绘制四轨和分数，不执行文件读写。 */
{ /* 函数开始。 */
    int lane; /* 绘制四条轨道。 */
    char text[300]; /* 格式化分数和状态。 */
    const char *keys[4] = {"D", "F", "J", "K"}; /* 轨道键位显示。 */
    const char *grades[4] = {"Perfect", "Great", "Good", "Miss"}; /* 单次判定显示文字。 */
    Result result = stats_result(&game.stats, game.chart.count); /* 计算当前实时分数。 */
    SelectObject(dc, font); /* 使用统一字体。 */
    SetBkMode(dc, TRANSPARENT); /* 文字背景透明。 */
    SetTextColor(dc, RGB(30, 35, 45)); /* 采用清晰的深色文字。 */
    snprintf(text, sizeof(text), "%s  /  %s  /  %s", playerName, song_title(selectedSong), difficulty_title(selectedDifficulty)); /* 生成本局标题。 */
    draw_text(dc, 60, 30, text); /* 显示账号及曲目信息。 */
    for (lane = 0; lane < 4; lane++) /* 绘制四条基础轨道。 */
    { /* 轨道绘制循环开始。 */
        COLORREF color = game.held[lane] ? RGB(200, 220, 250) : RGB(225, 230, 238); /* 按住时改变轨道底色。 */
        fill_rectangle(dc, 75 + lane * 160, 100, 150, 530, color); /* 显示轨道。 */
        draw_text(dc, 140 + lane * 160, 650, keys[lane]); /* 显示按键名称。 */
    } /* 轨道绘制循环结束。 */
    draw_notes(dc); /* 根据真实音乐位置绘制音符。 */
    fill_rectangle(dc, 70, 570, 645, 4, RGB(220, 75, 60)); /* 红线为判定线。 */
    snprintf(text, sizeof(text), "得分：%d", result.score); /* 格式化实时分数。 */
    draw_text(dc, 750, 130, text); /* 显示分数。 */
    snprintf(text, sizeof(text), "连击：%d", game.stats.combo); /* 格式化当前连击。 */
    draw_text(dc, 750, 185, text); /* 显示连击。 */
    snprintf(text, sizeof(text), "准确率：%.2f%%", result.accuracy); /* 分母始终包含全谱面音符。 */
    draw_text(dc, 750, 240, text); /* 显示当前累计准确率。 */
    snprintf(text, sizeof(text), "进度：%d / %d 秒", game.now / 1000, game.chart.duration / 1000); /* 格式化音乐进度。 */
    draw_text(dc, 750, 295, text); /* 显示播放位置。 */
    if (phase == PHASE_COUNTDOWN) /* 准备阶段显示剩余秒数。 */
    { /* 倒计时显示分支开始。 */
        ULONGLONG now = GetTickCount64(); /* 读取当前系统时刻。 */
        int seconds = now < startAt ? (int)((startAt - now + 999) / 1000) : 0; /* 避免无符号减法下溢。 */
        snprintf(text, sizeof(text), "准备：%d", seconds); /* 格式化准备提示。 */
        draw_text(dc, 280, 300, text); /* 在轨道中央显示。 */
    } /* 倒计时显示分支结束。 */
    else if (phase == PHASE_PAUSED) /* 暂停时显示恢复提示。 */
        draw_text(dc, 190, 300, resumeRequested ? "请重新按住活动长按轨道" : "已暂停，按 Enter 继续"); /* 提示真实恢复条件。 */
    else if (game.now - game.feedbackTime < 600) /* 最终判定后短暂显示反馈。 */
        draw_text(dc, 300, 350, grades[game.lastGrade]); /* 显示 Perfect 等级。 */
} /* 函数结束。 */

static void paint_window(void) /* 用系统 GDI 双缓冲绘制游戏画面。 */
{ /* 函数开始。 */
    PAINTSTRUCT paint; /* 保存绘图过程信息。 */
    HDC dc = BeginPaint(window, &paint); /* 取得窗口绘图设备。 */
    RECT area; /* 保存客户区大小。 */
    GetClientRect(window, &area); /* 查询当前窗口客户区。 */
    if (page == PAGE_GAME) /* 游戏画面用双缓冲减少闪烁。 */
    { /* 游戏绘图分支开始。 */
        HDC memory = CreateCompatibleDC(dc); /* 创建内存绘图区。 */
        HBITMAP bitmap = CreateCompatibleBitmap(dc, area.right, area.bottom); /* 创建一帧位图。 */
        HGDIOBJ oldBitmap = SelectObject(memory, bitmap); /* 保存原位图用于恢复。 */
        FillRect(memory, &area, (HBRUSH)(COLOR_WINDOW + 1)); /* 清空本帧背景。 */
        draw_game(memory); /* 在内存中绘制完整一帧。 */
        BitBlt(dc, 0, 0, area.right, area.bottom, memory, 0, 0, SRCCOPY); /* 一次性显示到窗口。 */
        SelectObject(memory, oldBitmap); /* 解除对临时位图的使用。 */
        DeleteObject(bitmap); /* 释放本帧位图。 */
        DeleteDC(memory); /* 释放内存绘图区。 */
    } /* 游戏绘图分支结束。 */
    else /* 普通页面主要由 Windows 原生控件绘制。 */
        FillRect(dc, &area, (HBRUSH)(COLOR_WINDOW + 1)); /* 填充干净背景。 */
    EndPaint(window, &paint); /* 完成本次窗口绘制。 */
} /* 函数结束。 */

static LRESULT CALLBACK window_proc(HWND handle, UINT event, WPARAM wParam, LPARAM lParam) /* Windows 消息入口。 */
{ /* 函数开始。 */
    switch (event) /* 只处理本程序关心的系统事件。 */
    { /* 消息分派开始。 */
    case WM_COMMAND: /* 用户点击按钮或改变选择。 */
        on_command(LOWORD(wParam), HIWORD(wParam)); /* 按页面分发操作。 */
        return 0; /* 已完成处理。 */
    case WM_TIMER: /* 系统定时器通知。 */
        if (wParam == 2) /* 短暂界面检查的退出定时器。 */
            DestroyWindow(handle); /* 自动检查结束后关闭自己的窗口。 */
        else /* 正常游戏刷新定时器。 */
        { /* 刷新分支开始。 */
            update_game(); /* 先更新状态，再请求绘图。 */
            if (page == PAGE_GAME) /* 菜单页无需持续重绘。 */
                InvalidateRect(handle, NULL, FALSE); /* 游戏页保持平滑刷新。 */
        } /* 刷新分支结束。 */
        return 0; /* 定时器事件已处理。 */
    case WM_KEYDOWN: /* 玩家真实按下键盘。 */
        on_key(wParam, 1, (lParam & (1LL << 30)) != 0); /* 忽略长按产生的自动重复。 */
        return 0; /* 键盘事件处理结束。 */
    case WM_KEYUP: /* 玩家松开键盘。 */
        on_key(wParam, 0, 0); /* 松开事件只处理一次。 */
        return 0; /* 键盘事件处理结束。 */
    case WM_ACTIVATEAPP: /* 用户切换到其他应用。 */
        if (!wParam && page == PAGE_GAME && phase == PHASE_PLAYING) /* 演奏期间失去应用焦点。 */
            pause_game(); /* 避免切出去后产生额外漏击。 */
        return 0; /* 焦点事件处理结束。 */
    case WM_PAINT: /* 系统要求重绘窗口。 */
        paint_window(); /* 绘制当前页面。 */
        return 0; /* 绘图完成。 */
    case WM_DESTROY: /* 用户关闭窗口或短时测试结束。 */
        KillTimer(handle, 1); /* 停止游戏刷新。 */
        KillTimer(handle, 2); /* 停止自动退出定时器。 */
        music_close(); /* 关闭本程序音频，不保存未完成局。 */
        PostQuitMessage(0); /* 结束消息循环。 */
        return 0; /* 清理完成。 */
    } /* 消息分派结束。 */
    return DefWindowProcW(handle, event, wParam, lParam); /* 其他事件交给系统默认处理。 */
} /* 函数结束。 */

int ui_run(HINSTANCE instance, int show, int smokeTest) /* 创建基础 C 图形窗口并运行。 */
{ /* 函数开始。 */
    WNDCLASSW windowClass = {0}; /* 初始化系统窗口类。 */
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN; /* 固定客户区便于初学者阅读布局。 */
    RECT area = {0, 0, 1000, 720}; /* 所有页面使用相同客户区大小。 */
    MSG event; /* 保存系统消息。 */
    int received; /* 区分消息、正常退出和消息读取错误。 */
    settingsWritable = settings_load(&settings, message); /* 读取设置，错误时暂用默认值。 */
    windowClass.lpfnWndProc = window_proc; /* 指定本程序消息处理函数。 */
    windowClass.hInstance = instance; /* 使用当前可执行程序。 */
    windowClass.hCursor = LoadCursorW(NULL, IDC_ARROW); /* 使用标准鼠标箭头。 */
    windowClass.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1); /* 使用系统白色背景。 */
    windowClass.lpszClassName = L"NoteCollisionBasicV2"; /* 本程序独立窗口类名。 */
    if (!RegisterClassW(&windowClass)) /* 注册窗口类。 */
        return 1; /* 无法创建窗口时明确退出失败。 */
    AdjustWindowRect(&area, style, FALSE); /* 计算包含标题栏的总窗口大小。 */
    window = CreateWindowExW(0, windowClass.lpszClassName, L"音符碰撞  ", style, CW_USEDEFAULT, CW_USEDEFAULT, area.right - area.left, area.bottom - area.top, NULL, NULL, instance, NULL); /* 创建唯一主窗口。 */
    if (window == NULL) /* 系统无法创建窗口。 */
        return 1; /* 返回失败。 */
    font = CreateFontW(-20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI"); /* 选择中文字体。 */
    show_login(0); /* 所有用户都先经过注册登录。 */
    SetTimer(window, 1, 16, NULL); /* 每十六毫秒请求一次状态更新。 */
    if (smokeTest) /* 自动检查只短暂打开，不开始音乐。 */
        SetTimer(window, 2, 1200, NULL); /* 一点二秒后自动关闭。 */
    ShowWindow(window, show); /* 根据正常启动或测试方式显示窗口。 */
    UpdateWindow(window); /* 绘制第一帧登录界面。 */
    while ((received = GetMessageW(&event, NULL, 0, 0)) > 0) /* 运行图形消息循环。 */
    { /* 消息循环开始。 */
        if (page != PAGE_GAME && IsDialogMessageW(window, &event)) /* 普通菜单支持 Tab 切换输入框。 */
            continue; /* 系统已经处理这条控件消息。 */
        TranslateMessage(&event); /* 将键盘消息转换成字符输入。 */
        DispatchMessageW(&event); /* 分发到窗口及其控件。 */
    } /* 消息循环结束。 */
    DeleteObject(font); /* 销毁全部窗口后释放统一字体。 */
    return received < 0 ? 1 : 0; /* 消息读取错误时返回失败。 */
} /* 函数结束。 */
