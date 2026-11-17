#define WIN32_LEAN_AND_MEAN /* 使用必要的 Windows 类型。 */
#include <windows.h> /* 使用测试计时和独立进程编号。 */
#include <stdio.h> /* 输出测试结果并检查文本存档。 */
#include <stdlib.h> /* 测试失败时结束程序。 */
#include <string.h> /* 比较测试结果。 */
#include "game.h" /* 测试实际游戏模块，不复制实现。 */

static int checks = 0; /* 记录已经执行的检查次数。 */
static char error[MESSAGE_SIZE]; /* 保存被测接口输出的错误。 */
static char testFolder[128]; /* 本次测试使用独立存档目录。 */
static Chart chart; /* 大结构使用静态内存，避免堆栈不足。 */
static Game game; /* 所有游戏测试复用一份静态结构。 */
static Record records[MAX_RECORDS]; /* 保存从文件读取的成绩。 */

static void check(int condition, const char *description) /* 用普通函数替代较难理解的 CHECK 宏。 */
{ /* 函数开始。 */
    checks++; /* 检查次数增加一。 */
    if (!condition) /* 条件不满足意味着测试失败。 */
    { /* 失败分支开始。 */
        fprintf(stderr, "FAIL: %s\nDetail: %s\n", description, error); /* 输出用例和具体错误。 */
        exit(1); /* 以失败状态结束整个测试程序。 */
    } /* 失败分支结束。 */
} /* 函数结束。 */

static void one_note(int type) /* 构造一个可控的测试音符。 */
{ /* 函数开始。 */
    memset(&chart, 0, sizeof(chart)); /* 清除上一份测试数据。 */
    chart.count = 1; /* 本用例只有一个音符。 */
    chart.duration = 3000; /* 音乐总时长三秒。 */
    chart.approach = 1000; /* 音符提前一秒出现。 */
    chart.notes[0].id = 1; /* 音符编号为一。 */
    chart.notes[0].time = 1000; /* 头部目标时刻一秒。 */
    chart.notes[0].end = type == NOTE_HOLD ? 1500 : 1000; /* 长按持续半秒。 */
    chart.notes[0].type = type; /* 保存当前测试类型。 */
} /* 函数结束。 */

static void test_scores(void) /* 验证固定计分规则及合法性检查。 */
{ /* 函数开始。 */
    Statistics stats = {0}; /* 从空统计开始。 */
    Result result; /* 保存结算结果。 */
    stats_add(&stats, PERFECT); /* 加入一次完美。 */
    stats_add(&stats, GREAT); /* 加入一次优秀。 */
    stats_add(&stats, GOOD); /* 加入一次良好。 */
    stats_add(&stats, MISS); /* 加入一次漏击。 */
    result = stats_result(&stats, 4); /* 总权重为两千一百。 */
    check(result.score == 525000, "mixed grades produce 525000"); /* 核对标准分。 */
    check(result.accuracy == 52.5, "mixed accuracy is 52.5 percent"); /* 核对百分比。 */
    check(stats.combo == 0 && stats.maxCombo == 3, "miss resets current combo only"); /* 核对断连。 */
    check(result.complete, "complete mixed statistics accepted"); /* 完整合法成绩可以保存。 */
    stats.units++; /* 人为破坏累计权重。 */
    check(!stats_valid(&stats, 4), "inconsistent weight rejected"); /* 必须拒绝篡改。 */
    stats.units--; /* 恢复合法权重。 */
    stats.maxCombo = 0; /* 构造不可能的最大连击。 */
    check(!stats_valid(&stats, 4), "impossible combo rejected"); /* 核对连击关系。 */
    memset(&stats, 0, sizeof(stats)); /* 构造准确率高但连击不足的结果。 */
    stats.perfect = 95; /* 九十五次完美。 */
    stats.miss = 5; /* 五次漏击。 */
    stats.maxCombo = 19; /* 成功操作可以分成五段十九连击。 */
    stats.finished = 100; /* 已完成一百个音符。 */
    stats.units = 95000; /* 权重与数量保持一致。 */
    result = stats_result(&stats, 100); /* 准确率虽高仍需检查 S 的连击要求。 */
    check(result.complete && result.grade == 'A', "S requires sufficient maximum combo"); /* 连击不足只能为 A。 */
    check(stats_result(&stats, 0).score == 0, "zero note count does not divide by zero"); /* 边界输入安全。 */
} /* 函数结束。 */

static void test_boundaries(void) /* 验证每个时间窗口的正负边界。 */
{ /* 函数开始。 */
    int differences[17] = {-351, -350, -300, -251, -250, -221, -151, -150, 0, 150, 151, 221, 250, 251, 300, 350, 351}; /* 新窗口边界及原先不能命中的偏差。 */
    int expected[17] = {MISS, GOOD, GOOD, GOOD, GREAT, GREAT, GREAT, PERFECT, PERFECT, PERFECT, GREAT, GREAT, GREAT, GOOD, GOOD, GOOD, MISS}; /* 对应的新预期评级。 */
    int i; /* 逐项执行边界检查。 */
    int offsets[2] = {-200, 200}; /* 同时检查输入校准的两个极限值。 */
    for (i = 0; i < 17; i++) /* 每个偏差都初始化独立一局。 */
    { /* 边界循环开始。 */
        int time = 1000 + differences[i]; /* 换算成实际击打时刻。 */
        int hit; /* 保存是否命中。 */
        one_note(NOTE_TAP); /* 构造一个单点。 */
        game_start(&game, &chart, 0); /* 使用零校准。 */
        hit = game_press(&game, 0, time); /* 执行真实被测接口。 */
        check(hit == (expected[i] != MISS), "time-window hit boundary"); /* 命中范围必须连续且明确。 */
        game_release(&game, 0, time); /* 松开按键，避免后续重复状态干扰。 */
        game_update(&game, 2000); /* 超时音符也必须自动漏击。 */
        check(game.stats.finished == 1, "each note is finalized exactly once"); /* 最终数量始终为一。 */
        check(game.lastGrade == expected[i], "time-window judgement grade"); /* 核对每个边界的归属。 */
        check(stats_valid(&game.stats, 1), "boundary result remains valid"); /* 统计必须可核对。 */
    } /* 边界循环结束。 */
    one_note(NOTE_CHAIN); /* 连续节点仍按单个节点判定。 */
    game_start(&game, &chart, 100); /* 正校准使九百毫秒输入对应一千毫秒。 */
    check(game_press(&game, 0, 900), "input offset allows matching earlier physical input"); /* 检查校准生效。 */
    check(game.stats.perfect == 1, "offset input receives Perfect"); /* 校准后应精确命中。 */
    check(!game_press(&game, 0, 900), "held key cannot repeat a tap"); /* 防止自动重复计分。 */
    check(!game_press(&game, -1, 900), "invalid lane rejected"); /* 防止轨道数组越界。 */
    for (i = 0; i < 2; i++) /* 扩宽窗口后仍要正确处理正负校准。 */
    { /* 校准边界循环开始。 */
        one_note(NOTE_TAP); /* 使用独立的单点谱面。 */
        game_start(&game, &chart, offsets[i]); /* 分别测试提前和延后校准。 */
        check(game_press(&game, 0, 1000 + 350 - offsets[i]), "late 350ms boundary works with offset"); /* 晚按边界仍应命中。 */
        check(game.stats.good == 1, "late boundary receives Good with offset"); /* 边界属于良好等级。 */
        game_start(&game, &chart, offsets[i]); /* 重置同一单点。 */
        check(!game_press(&game, 0, 1000 + 351 - offsets[i]), "late 351ms becomes Miss with offset"); /* 窗口外一毫秒必须失败。 */
        check(game.stats.miss == 1, "automatic Miss follows the wider deadline"); /* 自动漏击与命中窗口保持一致。 */
        game_start(&game, &chart, offsets[i]); /* 再次重置以检查提前输入。 */
        check(game_press(&game, 0, 1000 - 350 - offsets[i]), "early 350ms boundary works with offset"); /* 提前边界同样宽松。 */
        check(game.stats.good == 1, "early boundary receives Good with offset"); /* 提前与晚按使用相同规则。 */
        game_start(&game, &chart, offsets[i]); /* 检查提前窗口外输入。 */
        check(!game_press(&game, 0, 1000 - 351 - offsets[i]), "early 351ms cannot hit with offset"); /* 过早输入不命中。 */
        game_update(&game, 2000); /* 没有后续有效输入时应自动漏击。 */
        check(game.stats.miss == 1, "too-early input eventually receives one Miss"); /* 不会重复或漏掉最终结算。 */
    } /* 校准边界循环结束。 */
} /* 函数结束。 */

static void test_holds_and_pause(void) /* 验证长按头尾、提前松开及暂停恢复。 */
{ /* 函数开始。 */
    one_note(NOTE_HOLD); /* 构造半秒长按。 */
    game_start(&game, &chart, 0); /* 初始化本局。 */
    check(game_press(&game, 0, 1000), "hold head is accepted"); /* 按下头部。 */
    game_update(&game, 1499); /* 尾部前一毫秒。 */
    check(game.stats.finished == 0, "hold head is not counted twice"); /* 头部不单独计入总判定量。 */
    game_release(&game, 0, 1500); /* 精确在尾部松开。 */
    check(game.stats.perfect == 1, "exact-tail release succeeds"); /* 先推进再松开可以成功。 */
    game_update(&game, 3000); /* 多次刷新不得重复结算。 */
    check(game.stats.finished == 1, "finished hold cannot score twice"); /* 核对唯一结算。 */
    game_start(&game, &chart, 0); /* 重新开始同一长按。 */
    game_press(&game, 0, 1000); /* 命中头部。 */
    game_release(&game, 0, 1400); /* 提前一百毫秒松开。 */
    check(game.stats.miss == 1, "early hold release becomes Miss"); /* 必须判为漏击。 */
    game_start(&game, &chart, 0); /* 再初始化一局供暂停检查。 */
    game_press(&game, 0, 1000); /* 开始长按。 */
    game_update(&game, 1200); /* 推进到长按中途。 */
    game_pause(&game); /* 冻结并清除按键状态。 */
    game_update(&game, 9000); /* 暂停期间的系统时间不应影响音乐判定。 */
    check(game.now == 1200 && game.stats.finished == 0, "pause freezes judgement time"); /* 核对冻结。 */
    check(!game_resume(&game), "resume waits for active hold keys"); /* 尚未重新按住时不能恢复。 */
    game_press(&game, 0, 1200); /* 暂停时只登记重新按住。 */
    check(game_resume(&game), "resume succeeds after holding required lane"); /* 所有条件满足。 */
    game_release(&game, 0, 1500); /* 按原音乐位置完成尾部。 */
    check(game.stats.perfect == 1 && game.stats.finished == 1, "pause does not damage a valid hold"); /* 不产生额外漏击。 */
    game_start(&game, &chart, 0); /* 检查长按头部也使用更宽的窗口。 */
    check(game_press(&game, 0, 1350), "hold head accepts the wider 350ms window"); /* 晚按三百五十毫秒仍可开始保持。 */
    game_release(&game, 0, 1500); /* 玩家仍需实际保持到尾部。 */
    check(game.stats.good == 1, "wide-window hold keeps its head grade"); /* 完成尾部后沿用良好等级。 */
    game_start(&game, &chart, -200); /* 校准后最大晚按边界可能晚于很短长按的尾部。 */
    check(!game_press(&game, 0, 1550), "hold cannot start after its tail has passed"); /* 防止尾部已过仍补按取分。 */
    game_update(&game, 1600); /* 经过正常的扩宽漏击截止时刻。 */
    check(game.stats.miss == 1, "expired short hold becomes one Miss"); /* 保持长按规则完整。 */
} /* 函数结束。 */

typedef struct /* 用事件列表对真实谱面做确定性回放。 */
{ /* 回放事件结构开始。 */
    int time; /* 事件发生时的音乐位置。 */
    int lane; /* 事件所属轨道。 */
    int pressed; /* 一表示按下，零表示松开。 */
} TestEvent; /* 回放事件结构结束。 */

static TestEvent events[MAX_NOTES * 2]; /* 每个音符最多产生按下和松开两个事件。 */

static void replay_chart(void) /* 按真实音符时间回放已加载的谱面。 */
{ /* 函数开始。 */
    int count = chart.count * 2; /* 按下和松开事件总数。 */
    int i; /* 创建和回放事件。 */
    int j; /* 使用基础插入排序。 */
    for (i = 0; i < chart.count; i++) /* 根据谱面创建理想输入。 */
    { /* 创建事件循环开始。 */
        Note *note = &chart.notes[i]; /* 当前谱面音符。 */
        events[i * 2].time = note->time; /* 按下恰好发生在头部。 */
        events[i * 2].lane = note->lane; /* 对应同一轨道。 */
        events[i * 2].pressed = 1; /* 这是一条按下事件。 */
        events[i * 2 + 1].time = note->type == NOTE_HOLD ? note->end : note->time + 1; /* 长按在尾部松开。 */
        events[i * 2 + 1].lane = note->lane; /* 松开同一轨道。 */
        events[i * 2 + 1].pressed = 0; /* 这是一条松开事件。 */
    } /* 创建事件循环结束。 */
    for (i = 1; i < count; i++) /* 按时间将回放事件排好。 */
    { /* 排序循环开始。 */
        TestEvent current = events[i]; /* 暂存当前事件。 */
        j = i - 1; /* 从有序部分末尾比较。 */
        while (j >= 0 && events[j].time > current.time) /* 保持相同时刻的原有顺序。 */
        { /* 插入循环开始。 */
            events[j + 1] = events[j]; /* 较晚事件向后移动。 */
            j--; /* 继续向前查找位置。 */
        } /* 插入循环结束。 */
        events[j + 1] = current; /* 插入当前事件。 */
    } /* 排序循环结束。 */
    game_start(&game, &chart, 0); /* 用真实谱面初始化引擎。 */
    for (i = 0; i < count; i++) /* 不依赖真实音频硬件的精确回放。 */
    { /* 回放循环开始。 */
        if (events[i].pressed) /* 输入为按下。 */
            check(game_press(&game, events[i].lane, events[i].time), "real-chart ideal press hits"); /* 每个头部都应命中。 */
        else /* 输入为松开。 */
            game_release(&game, events[i].lane, events[i].time); /* 精确完成长按尾部。 */
        check(game.stats.miss == 0, "real-chart ideal replay has no Miss"); /* 检查组合音符不互相误伤。 */
        check(game.stats.finished <= chart.count, "replay never finalizes extra notes"); /* 防止重复计分。 */
    } /* 回放循环结束。 */
    game_update(&game, chart.duration + 500); /* 完成末尾到期检查。 */
    check(game.stats.perfect == chart.count, "all real notes receive Perfect"); /* 全部音符精确命中。 */
    check(stats_result(&game.stats, chart.count).score == 1000000, "real-chart replay reaches one million"); /* 核对满分。 */
    check(stats_valid(&game.stats, chart.count), "real-chart final statistics are complete"); /* 核对完整性。 */
} /* 函数结束。 */

static void test_resources(void) /* 六份原有谱面都需要实际读取并回放。 */
{ /* 函数开始。 */
    int song; /* 遍历两首歌曲。 */
    int difficulty; /* 遍历三种难度。 */
    for (song = 0; song < 2; song++) /* 两首歌均不能遗漏。 */
    { /* 歌曲循环开始。 */
        for (difficulty = 0; difficulty < 3; difficulty++) /* 每个难度独立载入。 */
        { /* 难度循环开始。 */
            check(chart_load(song, difficulty, &chart, error), "all six converted charts load"); /* 检查转换格式。 */
            replay_chart(); /* 检查长按、连续节点及同时按键均可完成。 */
        } /* 难度循环结束。 */
    } /* 歌曲循环结束。 */
    check(!chart_load(2, 0, &chart, error), "unknown song rejected"); /* 下标越界不能形成任意路径。 */
    check(!chart_load(0, 3, &chart, error), "unknown difficulty rejected"); /* 防止难度越界。 */
} /* 函数结束。 */

static void test_file_path(char path[256], const char *name) /* 测试只访问本次运行创建的目录。 */
{ /* 函数开始。 */
    snprintf(path, 256, "%s/%s", testFolder, name); /* 拼接独立测试数据位置。 */
} /* 函数结束。 */

static void write_test_file(const char *name, const char *text) /* 人为构造损坏文件，验证保护逻辑。 */
{ /* 函数开始。 */
    char path[256]; /* 测试文件路径。 */
    FILE *file; /* 测试专用文件句柄。 */
    test_file_path(path, name); /* 只指向本次测试目录。 */
    file = fopen(path, "wb"); /* 覆盖本次测试自己创建的文件。 */
    check(file != NULL, "test fixture opens"); /* 检查测试环境本身有效。 */
    check(fputs(text, file) >= 0, "test fixture writes"); /* 检查故障样本实际写入。 */
    check(fclose(file) == 0, "test fixture closes"); /* 检查样本没有写入中断。 */
} /* 函数结束。 */

static void read_test_file(const char *name, char text[2000]) /* 读取小型测试文件供内容核对。 */
{ /* 函数开始。 */
    char path[256]; /* 保存测试路径。 */
    FILE *file; /* 保存文件句柄。 */
    size_t length; /* 本次实际读取长度。 */
    test_file_path(path, name); /* 使用独立测试目录。 */
    file = fopen(path, "rb"); /* 不修改样本。 */
    check(file != NULL, "saved file exists"); /* 保存后应确实具有文件。 */
    length = fread(text, 1, 1999, file); /* 留出文本结束符空间。 */
    text[length] = 0; /* 便于字符串比较。 */
    check(!ferror(file), "saved file is readable"); /* 检查完整读取。 */
    fclose(file); /* 关闭读取句柄。 */
} /* 函数结束。 */

static void test_accounts(void) /* 验证密码摘要、中文账号、错误密码及损坏文件。 */
{ /* 函数开始。 */
    char hash[65]; /* 保存系统 SHA-256 结果。 */
    char before[2000]; /* 保存正常账号文件样本。 */
    char after[2000]; /* 保存校验失败后的文件内容。 */
    check(password_hash("salt", "password", hash), "SHA-256 operation succeeds"); /* 实际调用系统摘要算法。 */
    check(strcmp(hash, "291e247d155354e48fec2b579637782446821935fc96a5a08a0b7885179c408b") == 0, "SHA-256 matches independent known vector"); /* 与独立计算值比较。 */
    check(account_register("Alice", " secretPass ", error), "first account registers"); /* 密码首尾空格有效。 */
    check(account_login("Alice", " secretPass ", error), "exact password logs in"); /* 必须完整保留密码空格。 */
    check(!account_login("Alice", "secretPass", error), "trimmed password is rejected"); /* 不可自动裁剪密码。 */
    check(!account_login("alice", " secretPass ", error), "account comparison is case sensitive"); /* 账号按大小写区分。 */
    check(!account_register("Alice", "another", error), "duplicate account is rejected"); /* 不能覆盖原账号。 */
    check(!account_register(" Bob", "abc", error), "leading account space is rejected"); /* 明确账号输入规则。 */
    check(!account_register("bad\tname", "abc", error), "control characters are rejected"); /* 文件分隔符不能进入账号。 */
    check(account_register("测试同学", "中文 密码", error), "UTF-8 account registers"); /* 验证中文编码。 */
    check(account_login("测试同学", "中文 密码", error), "UTF-8 account survives disk reload"); /* 通过实际文件重新读取验证。 */
    read_test_file("accounts.txt", before); /* 查看实际保存内容。 */
    check(strstr(before, "secretPass") == NULL, "account file contains no plain password"); /* 不得保存明文密码。 */
    write_test_file("accounts.txt", "broken account file\n"); /* 构造无效格式版本。 */
    check(!account_register("Bob", "abc", error), "damaged account file blocks registration"); /* 不覆盖坏文件。 */
    read_test_file("accounts.txt", after); /* 检查失败后的内容。 */
    check(strcmp(after, "broken account file\n") == 0, "damaged account file remains unchanged"); /* 保留原始故障现场。 */
    write_test_file("accounts.txt", before); /* 恢复本次测试自己的正常样本。 */
    check(account_login("Alice", " secretPass ", error), "restored account data still works"); /* 确认之前账号未发生改变。 */
} /* 函数结束。 */

static Record make_record(const char *name, int grade) /* 构造四个音符的合法完整成绩。 */
{ /* 函数开始。 */
    Record record = {0}; /* 初始化所有字段。 */
    int i; /* 累计四个相同等级的音符。 */
    snprintf(record.name, sizeof(record.name), "%s", name); /* 保存目标账号。 */
    snprintf(record.date, sizeof(record.date), "2026-11-16_12:00:00"); /* 使用固定样本时间。 */
    record.noteCount = 4; /* 本样本共有四个音符。 */
    for (i = 0; i < 4; i++) /* 用实际计分接口构造统计。 */
        stats_add(&record.stats, grade); /* 避免测试重复实现计分规则。 */
    return record; /* 返回完整样本。 */
} /* 函数结束。 */

static void test_records(void) /* 验证最佳成绩合并、分榜、排序及坏文件保护。 */
{ /* 函数开始。 */
    Record best = make_record("Alice", PERFECT); /* 一百万分。 */
    Record worse = make_record("Alice", GOOD); /* 相同账号的较低成绩。 */
    Record second = make_record("Bob", GREAT); /* 另一账号七十万分。 */
    int count; /* 读取到的记录数量。 */
    int order[MAX_RECORDS]; /* 排行榜下标表。 */
    char text[2000]; /* 检查损坏文件是否保持原样。 */
    check(record_save(&best, error), "complete best result saves"); /* 完整成绩允许保存。 */
    check(record_save(&best, error), "saving same result again succeeds safely"); /* 重复保存不增加条目。 */
    check(record_save(&worse, error), "lower result is safely ignored"); /* 旧最佳成绩不会下降。 */
    check(record_save(&second, error), "another account gets separate best result"); /* 不同账号不合并。 */
    count = records_load(records, error); /* 实际从磁盘重新读取。 */
    check(count == 2, "one best row per account and board"); /* 两个账号只有两条最佳记录。 */
    check(ranking_make(records, count, 0, 0, order) == 2, "ranking includes both accounts"); /* 当前榜单含两条。 */
    check(strcmp(records[order[0]].name, "Alice") == 0, "ranking sorts higher score first"); /* 满分记录排在前面。 */
    check(stats_result(&records[order[0]].stats, 4).score == 1000000, "lower score did not overwrite best"); /* 验证最佳保留。 */
    best.difficulty = 1; /* 同一账号换到另一难度。 */
    check(record_save(&best, error), "another difficulty saves independently"); /* 分榜规则有效。 */
    count = records_load(records, error); /* 重新读取分榜后的文件。 */
    check(count == 3, "separate difficulty creates one new row"); /* 应多出一条独立最佳。 */
    check(ranking_make(records, count, 0, 1, order) == 1, "difficulty filter is respected"); /* 普通榜单只有一条。 */
    best.stats.finished = 3; /* 人为将结果改成未完成。 */
    check(!record_save(&best, error), "unfinished result cannot be saved"); /* 中止局不能入榜。 */
    write_test_file("scores.txt", "damaged scores\n"); /* 人为损坏本次测试成绩文件。 */
    check(!record_save(&second, error), "damaged score file blocks overwrite"); /* 应明确返回失败。 */
    read_test_file("scores.txt", text); /* 检查失败后内容。 */
    check(strcmp(text, "damaged scores\n") == 0, "damaged scores remain intact"); /* 不销毁坏文件。 */
} /* 函数结束。 */

static void test_settings(void) /* 验证默认值、边界值、错误范围及损坏文件。 */
{ /* 函数开始。 */
    Settings settings; /* 保存读取到的设置。 */
    Settings boundary = {0, -200}; /* 合法下边界。 */
    char before[2000]; /* 保存原设置样本。 */
    char after[2000]; /* 比较拒绝错误值后的内容。 */
    check(settings_load(&settings, error), "missing settings use defaults"); /* 首次运行无需事先创建文件。 */
    check(settings.volume == 70 && settings.offset == 0, "default settings are documented values"); /* 默认配置明确。 */
    check(settings_save(&boundary, error), "minimum allowed settings save"); /* 音量零与负偏移合法。 */
    check(settings_load(&settings, error), "saved settings reload"); /* 文件可以完整读回。 */
    check(settings.volume == 0 && settings.offset == -200, "saved values survive reload"); /* 检查序列化。 */
    read_test_file("settings.txt", before); /* 保存正常样本。 */
    boundary.offset = 201; /* 构造超上界值。 */
    check(!settings_save(&boundary, error), "out-of-range settings rejected"); /* 不能截断或自动放宽范围。 */
    read_test_file("settings.txt", after); /* 比较文件是否被改写。 */
    check(strcmp(before, after) == 0, "invalid settings do not overwrite previous settings"); /* 原值必须保留。 */
    write_test_file("settings.txt", "broken settings\n"); /* 构造未知格式。 */
    check(!settings_load(&settings, error), "damaged settings reported"); /* 不能静默当作读取成功。 */
    check(settings.volume == 70 && settings.offset == 0, "damaged settings use temporary defaults"); /* 仍提供可用默认值。 */
    read_test_file("settings.txt", after); /* 检查读取没有覆盖文件。 */
    check(strcmp(after, "broken settings\n") == 0, "settings load preserves damaged original"); /* 保留故障现场。 */
} /* 函数结束。 */

static void test_audio(void) /* 实际使用零音量音频设备，不影响用户听音。 */
{ /* 函数开始。 */
    int before; /* 暂停前的位置。 */
    int after; /* 暂停后的查询位置。 */
    check(music_open(0, 0, error), "real WAV opens at zero volume"); /* 实际验证资源可被 Windows 播放。 */
    check(music_play(error), "real WAV begins playing"); /* 请求起播。 */
    Sleep(160); /* 等待短暂真实设备进度。 */
    check(music_position() > 0, "music position advances"); /* 不是用假系统时间代替音频位置。 */
    check(music_pause(error), "real audio pauses"); /* 暂停真实设备。 */
    before = music_position(); /* 记录暂停时刻。 */
    Sleep(100); /* 等待以验证位置冻结。 */
    after = music_position(); /* 再次读取暂停位置。 */
    check(after == before, "paused music position is frozen"); /* 暂停不继续播放。 */
    check(music_resume(error), "real audio resumes"); /* 请求继续。 */
    Sleep(120); /* 等待设备继续推进。 */
    check(music_position() > after, "resumed music advances"); /* 恢复后位置应增加。 */
    music_close(); /* 测试结束释放设备。 */
    check(music_position() == -1, "closed audio reports unavailable position"); /* 不返回伪造的有效位置。 */
    check(!music_open(5, 0, error), "invalid song cannot open audio"); /* 检查歌曲下标限制。 */
} /* 函数结束。 */

int main(void) /* 核心测试入口，默认不操作正式 data 文件夹。 */
{ /* 函数开始。 */
    wchar_t path[MAX_PATH]; /* 获取测试可执行文件所在位置。 */
    wchar_t *separator; /* 用于去掉 build 和可执行文件名。 */
    SetConsoleOutputCP(CP_UTF8); /* 正确显示来自被测模块的中文错误。 */
    GetModuleFileNameW(NULL, path, MAX_PATH); /* 测试程序位于第二版 build 文件夹。 */
    separator = wcsrchr(path, L'\\'); /* 去掉测试程序文件名。 */
    *separator = 0; /* 得到 build 目录。 */
    separator = wcsrchr(path, L'\\'); /* 再向上一级找到第二版目录。 */
    *separator = 0; /* 去掉 build 目录名。 */
    check(SetCurrentDirectoryW(path), "test uses the version-two directory"); /* 资源读取始终指向第二版。 */
    snprintf(testFolder, sizeof(testFolder), "tests/run_%lu_%llu", GetCurrentProcessId(), GetTickCount64()); /* 每次运行创建唯一目录。 */
    storage_folder(testFolder); /* 所有账号及坏文件样本只保存在测试目录。 */
    test_scores(); /* 先验证纯计分规则。 */
    test_boundaries(); /* 验证各判定窗口。 */
    test_holds_and_pause(); /* 验证长按和暂停状态。 */
    test_resources(); /* 回放六份真实谱面。 */
    test_accounts(); /* 测试账号和密码摘要落盘。 */
    test_records(); /* 测试榜单与坏文件保护。 */
    test_settings(); /* 测试设置保存边界。 */
    test_audio(); /* 最后实际检查短暂零音量播放。 */
    printf("PASS: %d checks; six real charts; accounts, scores, settings and real silent audio.\n", checks); /* 输出实际完成的检查数量。 */
    printf("Test data: %s\n", testFolder); /* 便于用户复查文件样本。 */
    return 0; /* 所有检查通过。 */
} /* 函数结束。 */
