#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include "nc.h"
#include "network.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
static int checks;
#define CHECK(c)                                                                                   \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c);                                   \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
static char error[NC_ERROR];
/**
 * @brief 验证计分权重、完整性和非法统计拒绝。
 * 参数：无参数，使用局部 Stats 与 Result。
 * 返回：无返回值；任一 CHECK 失败打印行号并结束测试进程。
 * 说明：覆盖混合判定的 525000 分、无效单位/连击、全 Perfect 百万分和不完整/过大计数，测试不依赖窗口或磁盘。
 */
static void score_tests(void) {
    Stats s = {0};
    stats_apply(&s, PERFECT);
    stats_apply(&s, GREAT);
    stats_apply(&s, GOOD);
    stats_apply(&s, MISS);
    CHECK(stats_valid(&s, 4, true));
    Result r = stats_result(&s, 4);
    CHECK(r.score == 525000);
    CHECK(!r.fc && r.complete && r.grade == 'D');
    s.units++;
    CHECK(!stats_valid(&s, 4, true));
    s.units--;
    s.max_combo = 0;
    CHECK(!stats_valid(&s, 4, true));
    s = (Stats){0};
    for (int i = 0; i < 100; i++)
        stats_apply(&s, PERFECT);
    r = stats_result(&s, 100);
    CHECK(r.score == 1000000 && r.ap && r.fc && r.grade == 'S');
    r = stats_result(&s, 0);
    CHECK(r.score == 0 && !r.complete && r.grade == 'D');
    CHECK(!stats_valid(&s, 99, true));
    s.perfect = INT_MAX;
    CHECK(!stats_valid(&s, 100, true));
    puts("PASS scoring, completeness and impossible statistics");
}
/**
 * @brief 为判定用例构造轻量的借用谱面。
 * 参数：notes 为调用者数组，n 为元素数。
 * 返回：返回 Chart 值，时长 6000ms、approach 1800ms，notes 不复制。
 * 说明：仅用于受控单元测试；它不分配内存、不做校验，不得对返回值调用 chart_free 释放栈数组，调用者需让 notes
 * 按头部时刻、轨道、ID 排序并在引擎使用期间保持有效。
 */
static Chart tiny(Note *notes, int n) {
    Chart c = {0};
    c.notes = notes;
    c.count = n;
    c.duration = 6000;
    c.approach = 1800;
    return c;
}
/**
 * @brief 验证时间窗口、长按和暂停等引擎边界。
 * 参数：无参数，使用人工构造的局部谱面。
 * 返回：无返回值，失败由 CHECK 立即终止。
 * 说明：逐个测试 ±90/150/220ms
 * 与窗口外、重复结算、精确尾部释放、早放失败、快慢帧统计一致、暂停长按重新按住以及偏移上下界；结束释放每个引擎。
 */
static void engine_tests(void) {
    const int offsets[] = {-221, -220, -151, -150, -91, -90, 0, 90, 91, 150, 151, 220, 221};
    for (int k = 0; k < 13; k++) {
        Note note = {.id = 1, .lane = 0, .time = 1000, .end = 1000, .type = TAP};
        Chart c = tiny(&note, 1);
        Engine e = {0};
        CHECK(engine_start(&e, &c, 0));
        bool accepted = engine_press(&e, 0, 1000 + offsets[k]);
        engine_release(&e, 0, 1001 + offsets[k]);
        engine_tick(&e, 1600);
        int d = abs(offsets[k]);
        CHECK(accepted == (d <= 220));
        CHECK(e.stats.finalized == 1);
        CHECK(e.last == (d <= 90 ? PERFECT : d <= 150 ? GREAT : d <= 220 ? GOOD : MISS));
        engine_tick(&e, 5000);
        CHECK(e.stats.finalized == 1);
        engine_free(&e);
    }
    Note notes[] = {{.id = 1, .lane = 0, .time = 1000, .end = 2000, .type = HOLD},
                    {.id = 2, .lane = 1, .time = 1200, .end = 1200, .type = TAP},
                    {.id = 3, .lane = 2, .time = 2100, .end = 2100, .type = TAP}};
    Chart c = tiny(notes, 3);
    Engine e = {0};
    CHECK(engine_start(&e, &c, 0));
    CHECK(engine_press(&e, 0, 1000));
    CHECK(!engine_press(&e, 0, 1001));
    engine_release(&e, 0, 1999);
    CHECK(e.stats.miss == 2);
    engine_free(&e);
    CHECK(engine_start(&e, &c, 0));
    engine_press(&e, 0, 1000);
    engine_release(&e, 0, 2000);
    CHECK(e.stats.perfect == 1 && e.stats.miss == 1);
    CHECK(e.stats.combo == 1);
    engine_free(&e);
    Engine fine = {0}, slow = {0};
    CHECK(engine_start(&fine, &c, 0));
    CHECK(engine_start(&slow, &c, 0));
    engine_press(&fine, 0, 1000);
    engine_press(&slow, 0, 1000);
    for (int t = 1001; t <= 3000; t++)
        engine_tick(&fine, t);
    engine_tick(&slow, 3000);
    CHECK(!memcmp(&fine.stats, &slow.stats, sizeof(Stats)));
    engine_free(&fine);
    engine_free(&slow);
    CHECK(engine_start(&e, &c, 0));
    engine_press(&e, 0, 1000);
    engine_tick(&e, 1100);
    engine_pause(&e);
    engine_tick(&e, 4500);
    CHECK(e.now == 1100);
    e.state = RESUME_READY;
    CHECK(!engine_resume(&e));
    engine_press(&e, 0, 1100);
    CHECK(engine_resume(&e));
    engine_release(&e, 0, 2000);
    CHECK(e.stats.perfect == 1);
    engine_free(&e);
    Note tap = {.id = 1, .lane = 0, .time = 1000, .end = 1000, .type = TAP};
    c = tiny(&tap, 1);
    CHECK(engine_start(&e, &c, 200));
    CHECK(engine_press(&e, 0, 800));
    CHECK(e.stats.perfect == 1);
    engine_free(&e);
    CHECK(!engine_start(&e, &c, 201));
    puts("PASS timing windows, offsets, hold tail, pause and deterministic combo");
}
typedef struct {
    int time, lane;
    bool press;
} Event;
/**
 * @brief 为回放测试的按键事件确定时间顺序。
 * 参数：a/b 为 qsort 提供的 Event 指针。
 * 返回：先比较 time；同一毫秒松开先于按下；返回负数、零或正数。
 * 说明：优先 release 可模拟前一个音符结束后同毫秒再按下；不同轨道同时间次序不影响独立判定。
 */
static int event_cmp(const void *a, const void *b) {
    const Event *x = a, *y = b;
    return x->time == y->time ? (int)x->press - (int)y->press : x->time - y->time;
}
/**
 * @brief 把真实谱面转换为精确按键序列并验证满分。
 * 参数：c 为已通过资源校验的借用谱面。
 * 返回：无返回值，任何非满分/未完成结果会 CHECK 失败。
 * 说明：为每音符生成头部按下与尾部松开（短音符一毫秒后松开），按时序喂入
 * Engine，再推进到曲尾。回放只验证确定性规则，不模拟音频延迟或真人键盘。
 */
static void replay(const Chart *c) {
    Event *events = calloc((size_t)c->count * 2, sizeof(*events));
    CHECK(events != NULL);
    for (int i = 0; i < c->count; i++) {
        const Note *n = &c->notes[i];
        events[2 * i] = (Event){n->time, n->lane, true};
        events[2 * i + 1] = (Event){n->type == HOLD ? n->end : n->time + 1, n->lane, false};
    }
    qsort(events, (size_t)c->count * 2, sizeof(*events), event_cmp);
    Engine e = {0};
    CHECK(engine_start(&e, c, 0));
    for (int i = 0; i < c->count * 2; i++) {
        Event *v = &events[i];
        if (v->press)
            engine_press(&e, v->lane, v->time);
        else
            engine_release(&e, v->lane, v->time);
    }
    engine_tick(&e, c->duration);
    Result r = stats_result(&e.stats, c->count);
    CHECK(r.complete && r.ap && r.score == 1000000);
    CHECK(e.stats.perfect == c->count);
    engine_free(&e);
    free(events);
}
/**
 * @brief 在发布前检查内置谱面的音符 ID、连锁组和同时按键组。
 * 参数：path 为原始谱面 JSON 路径；c 为同一谱面加载后的运行数据。
 * 返回：无返回值；字段或分组有误时由 CHECK 结束测试。
 * 说明：检查 ID 唯一、连锁组至少两项、同时按键组至少两项且时刻相同，并核对已加载的同时按键标记数量。
 */
static void check_note_groups(const char *path, const Chart *c) {
    cJSON *object = json_read(path, 16 * 1024 * 1024, error);
    CHECK(object != NULL);
    const cJSON *array = cJSON_GetObjectItemCaseSensitive(object, "notes");
    CHECK(cJSON_IsArray(array) && cJSON_GetArraySize(array) == c->count);
    struct GroupNote {
        int id, time, chain, chord;
    } *groups = calloc((size_t)c->count, sizeof(*groups));
    CHECK(groups != NULL);

    const cJSON *item;
    int index = 0, chord_members = 0;
    cJSON_ArrayForEach(item, array) {
        struct GroupNote *n = &groups[index++];
        char type[16];
        n->chain = n->chord = -1;
        CHECK(json_int(item, "id", 1, 100000000, &n->id));
        CHECK(json_int(item, "timeMs", 0, c->duration, &n->time));
        CHECK(json_str(item, "type", type, sizeof(type)));
        if (cJSON_HasObjectItem(item, "chainId"))
            CHECK(json_int(item, "chainId", -1, 100000000, &n->chain));
        if (cJSON_HasObjectItem(item, "chordId"))
            CHECK(json_int(item, "chordId", -1, 100000000, &n->chord));
        CHECK((n->chain >= 0) == (strcmp(type, "chain") == 0));
        chord_members += n->chord >= 0;
    }
    for (int i = 0; i < c->count; i++) {
        const struct GroupNote *n = &groups[i];
        int chains = 0, chords = 0;
        for (int j = 0; j < c->count; j++) {
            const struct GroupNote *p = &groups[j];
            if (j < i)
                CHECK(p->id != n->id);
            if (n->chain >= 0 && n->chain == p->chain)
                chains++;
            if (n->chord >= 0 && n->chord == p->chord) {
                CHECK(p->time == n->time);
                chords++;
            }
        }
        CHECK(n->chain < 0 || chains >= 2);
        CHECK(n->chord < 0 || chords >= 2);
        chord_members -= c->notes[i].chord;
    }
    CHECK(chord_members == 0);
    free(groups);
    cJSON_Delete(object);
}
/**
 * @brief 从四个字节读取 WAV 使用的小端无符号整数。
 * 参数：p 必须指向至少四个可读字节，读取前由调用方完成边界检查。
 * 返回：返回四个字节合成的 uint32_t。
 * 说明：逐字节移位后按位或，不依赖 CPU 字节序，也不要求输入指针按四字节对齐。
 */
static uint32_t le32(const unsigned char *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
/**
 * @brief 在发布前核对内置 PCM WAV 的数据块和播放时长。
 * 参数：path 为 UTF-8 文件路径；duration 为谱面声明的毫秒时长。
 * 返回：无返回值；文件头、块边界或时长有误时由 CHECK 结束测试。
 * 说明：最多读取 128MiB，检查 RIFF/WAVE、PCM fmt 和 data，按块长度及奇数字节填充跳转。使用
 * data 字节数/字节率计算时长，允许 1ms 误差；测试通过后释放文件缓冲区。
 */
static void check_wav(const char *path, int duration) {
    char *bytes;
    size_t size;
    CHECK(nc_read(path, &bytes, &size, 128 * 1024 * 1024, error));
    const unsigned char *p = (const unsigned char *)bytes;
    uint32_t rate = 0, data = 0;
    CHECK(size >= 12 && !memcmp(p, "RIFF", 4) && !memcmp(p + 8, "WAVE", 4));
    for (size_t pos = 12; pos + 8 <= size;) {
        uint32_t n = le32(p + pos + 4);
        CHECK(n <= size - pos - 8);
        if (!memcmp(p + pos, "fmt ", 4) && n >= 16) {
            CHECK(p[pos + 8] == 1 && p[pos + 9] == 0); /* PCM only, as shipped */
            rate = le32(p + pos + 16);
        }
        if (!memcmp(p + pos, "data", 4))
            data = n;
        pos += 8 + (size_t)n + (n & 1);
    }
    CHECK(rate && data && llabs((int64_t)data * 1000 / rate - duration) <= 1);
    free(bytes);
}
/**
 * @brief 验证按需加载仍会拒绝缺失或不可演奏的谱面，并保留调用者持有的旧谱面。
 * 参数：l 为正常曲库，作为临时目录中索引及已加载谱面的来源。
 * 返回：无返回值；任何与预期不符的结果由 CHECK 结束测试。
 */
static void chart_loading_tests(const Library *l) {
    char exe[NC_PATH], root[NC_PATH], id[65], path[NC_PATH];
    nc_exe_dir(exe);
    nc_uuid(id);
    CHECK(snprintf(root, sizeof(root), "%s/test-data/%s", exe, id) < (int)sizeof(root));
    CHECK(nc_mkdir(root, error));
    CHECK(snprintf(path, sizeof(path), "%s/index.json", l->root) < (int)sizeof(path));
    cJSON *index = json_read(path, 1024 * 1024, error);
    CHECK(index != NULL);
    CHECK(snprintf(path, sizeof(path), "%s/index.json", root) < (int)sizeof(path));
    CHECK(json_save(path, index, error));
    cJSON_Delete(index);

    Library temporary;
    CHECK(library_load(&temporary, root, error));
    Chart c = {0};
    CHECK(chart_load(l, 0, 0, &c, error));
    Note *old_notes = c.notes;
    CHECK(!chart_load(&temporary, 0, 0, &c, error));
    CHECK(c.notes == old_notes);

    const char *overlap = "{\"schemaVersion\":1,\"chartVersion\":1,\"chartId\":\"overlap\","
                          "\"songId\":\"neon-pulse\",\"difficulty\":\"Easy\","
                          "\"audioDurationMs\":6000,\"approachMs\":1800,\"notes\":["
                          "{\"id\":1,\"type\":\"tap\",\"lane\":0,\"timeMs\":1500},"
                          "{\"id\":2,\"type\":\"hold\",\"lane\":0,\"timeMs\":1000,\"endMs\":2000}]}";
    nc_copy(temporary.songs[0].charts[0], NC_PATH, "overlap.json");
    CHECK(snprintf(path, sizeof(path), "%s/overlap.json", root) < (int)sizeof(path));
    CHECK(nc_atomic(path, overlap, strlen(overlap), error));
    CHECK(!chart_load(&temporary, 0, 0, &c, error));
    CHECK(strcmp(error, "同轨音符重叠") == 0 && c.notes == old_notes);
    chart_free(&c);
}
/**
 * @brief 验证项目交付的曲库、六份谱面及非法谱面。
 * 参数：l 为输出曲库，root 为 assets/songs 的实际路径。
 * 返回：无返回值，测试成功后 l 保持已加载可供后续用例使用。
 * 说明：检查两首歌三档数量、SHA-256 字符长度、WAV 时长、音符 ID 和分组，对每份谱面执行完整满分回放；
 * 另外检查按需加载、缺失谱面及重叠轨道。
 */
static void resource_tests(Library *l, const char *root) {
    CHECK(library_load(l, root, error));
    CHECK(l->count == 2);
    int expected[2][3] = {{68, 84, 136}, {43, 57, 66}};
    for (int s = 0; s < l->count; s++)
        for (int d = 0; d < 3; d++) {
            Chart c = {0};
            char path[NC_PATH];
            CHECK(chart_load(l, s, d, &c, error));
            CHECK(c.count == expected[s][d]);
            CHECK(strlen(c.hash) == 64 && strlen(c.audio_hash) == 64);
            CHECK(snprintf(path, sizeof(path), "%s/%s", root, l->songs[s].charts[d]) <
                  (int)sizeof(path));
            check_note_groups(path, &c);
            CHECK(snprintf(path, sizeof(path), "%s/%s", root, l->songs[s].audio) < (int)sizeof(path));
            check_wav(path, c.duration);
            replay(&c);
            chart_free(&c);
        }
    Chart c = {0};
    CHECK(!chart_load(l, 99, 0, &c, error));
    CHECK(!chart_load(l, 0, 3, &c, error));
    chart_loading_tests(l);
    puts("PASS all six charts, group/WAV/SHA-256 checks, lazy loading and complete perfect replays");
}
/**
 * @brief 构造一条完整全 Perfect 的存储测试记录。
 * 参数：c 为真实有效谱面；name 为测试昵称。
 * 返回：返回自含字段的 Record 值，具有新 session、当前规则及固定测试 UTC 日期。
 * 说明：只在内存生成，不写文件；判定计数通过 stats_apply 累加，以保证测试样本满足实际存储约束。
 */
static Record record_make(const Chart *c, const char *name) {
    Record r = {0};
    nc_uuid(r.session);
    nc_copy(r.name, sizeof(r.name), name);
    nc_copy(r.chart, sizeof(r.chart), c->id);
    nc_copy(r.hash, sizeof(r.hash), c->hash);
    nc_copy(r.difficulty, sizeof(r.difficulty), c->difficulty);
    nc_copy(r.date, sizeof(r.date), "2026-11-16T00:00:00Z");
    r.rules = NC_RULES;
    r.version = c->version;
    r.count = c->count;
    for (int i = 0; i < c->count; i++)
        stats_apply(&r.stats, PERFECT);
    return r;
}
/**
 * @brief 验证真实 UTF-8 文件持久化及失败保护。
 * 参数：l 为可用曲库，测试使用其中小星星 Easy。
 * 返回：无返回值，CHECK 失败结束进程。
 * 说明：在测试程序旁创建唯一 test-data
 * 子目录，覆盖中文账号、密码空格、重复注册、读取旧版设置、幂等成绩、账号最佳成绩及模式隔离；主动损坏测试文件后验证拒绝覆盖，不触碰用户
 * AppData 数据。
 */
static void storage_tests(const Library *l) {
    char exe[NC_PATH], root[NC_PATH], id[65], path[NC_PATH];
    nc_exe_dir(exe);
    nc_uuid(id);
    snprintf(root, sizeof(root), "%s/test-data/%s", exe, id);
    CHECK(nc_mkdir(root, error));
    CHECK(account_action(root, "测试玩家", " a password ", true, error));
    CHECK(account_action(root, "测试玩家", " a password ", false, error));
    CHECK(!account_action(root, "测试玩家", "a password", false, error));
    CHECK(!account_action(root, "测试玩家", "pw", true, error));
    CHECK(!account_action(root, "bad\nname", "pw", true, error));
    CHECK(!account_action(root, "", "pw", true, error));
    Settings read;
    snprintf(path, sizeof(path), "%s/settings.json", root);
    const char legacy_settings[] = "{\"schemaVersion\":1,\"music\":45,\"effect\":70,\"offset\":-125}";
    CHECK(nc_atomic(path, legacy_settings, sizeof(legacy_settings) - 1, error));
    CHECK(settings_load(root, &read, error));
    CHECK(read.music == 45 && read.offset == -125);
    Chart c = {0};
    CHECK(chart_load(l, 1, 0, &c, error));
    Store store = {0};
    CHECK(store_load(&store, root, error));
    Record one = record_make(&c, "测试玩家");
    /* 先存较低成绩，再存满分，验证榜单选择最高分而不是最早的一局。 */
    memset(&one.stats, 0, sizeof(one.stats));
    for (int i = 0; i < c.count; i++)
        stats_apply(&one.stats, GREAT);
    CHECK(store_save(&store, &one, error));
    CHECK(store_save(&store, &one, error));
    CHECK(store.count == 1);
    Record two = record_make(&c, "另一个玩家");
    CHECK(store_save(&store, &two, error));
    Record best = record_make(&c, "测试玩家");
    nc_copy(best.date, sizeof(best.date), "2026-11-17T00:00:00Z");
    CHECK(store_save(&store, &best, error));
    Record tied = record_make(&c, "测试玩家");
    nc_copy(tied.date, sizeof(tied.date), "2026-11-15T00:00:00Z");
    CHECK(store_save(&store, &tied, error));
    /* 联机更早的同分记录仍须隔离，不能替换单人榜中该账号的最佳成绩。 */
    Record lan = record_make(&c, "测试玩家");
    lan.mode = 1;
    nc_uuid(lan.match);
    nc_copy(lan.opponent, sizeof(lan.opponent), "另一个玩家");
    nc_copy(lan.date, sizeof(lan.date), "2026-11-14T00:00:00Z");
    CHECK(store_save(&store, &lan, error));
    const Record *rows[8];
    CHECK(store_ranking(&store, &c, 0, rows, 8) == 2);
    CHECK(!strcmp(rows[0]->session, tied.session));
    CHECK(!strcmp(rows[1]->session, two.session));
    CHECK(store_ranking(&store, &c, 0, rows, 1) == 1);
    CHECK(!strcmp(rows[0]->session, tied.session));
    CHECK(store_ranking(&store, &c, 1, rows, 8) == 1);
    CHECK(!strcmp(rows[0]->session, lan.session));
    CHECK(store.count == 5);
    store_free(&store);
    CHECK(store_load(&store, root, error));
    CHECK(store.count == 5);
    two.stats.miss++;
    CHECK(!store_save(&store, &two, error));
    snprintf(path, sizeof(path), "%s/records.json", root);
    CHECK(nc_atomic(path, "{broken", 7, error));
    char before[65], after[65];
    CHECK(nc_hash_file(path, before, error));
    CHECK(!store_load(&store, root, error));
    CHECK(!store_save(&store, &one, error));
    CHECK(nc_hash_file(path, after, error));
    CHECK(!strcmp(before, after));
    snprintf(path, sizeof(path), "%s/settings.json", root);
    const char *future = "{\"schemaVersion\":99}";
    CHECK(nc_atomic(path, future, strlen(future), error));
    CHECK(!settings_load(root, &read, error));
    CHECK(read.music == 70 && read.offset == 0);
    store_free(&store);
    chart_free(&c);
    puts("PASS real UTF-8 accounts, settings, idempotent saves and corrupt-file preservation");
}
/**
 * @brief 验证 Windows MCI 的真实音频生命周期。
 * 参数：lib 为有效曲库，读取小星星 WAV。
 * 返回：无返回值，设备/时间行为不符合预期则 CHECK 失败。
 * 说明：以零音量实际预加载、播放、暂停和恢复，检查音乐位置冻结/递增，并验证关闭后返回 -1 与缺文件拒绝；包含短时间
 * Sleep，属于需要 Windows 媒体设备的集成测试。
 */
static void audio_tests(const Library *lib) {
    char path[NC_PATH];
    snprintf(path, sizeof(path), "%s/%s", lib->root, lib->songs[1].audio);
    CHECK(audio_open(path, 0, 33800, error));
    CHECK(audio_play(error));
    Sleep(180);
    int first = audio_position();
    CHECK(first >= 50 && first < 1500);
    CHECK(audio_pause(error));
    int paused = audio_position();
    Sleep(100);
    CHECK(abs(audio_position() - paused) <= 35);
    CHECK(audio_resume(error));
    /* Windows MCI 恢复后可能要等待音频设备再次推进播放位置。 */
    int64_t resume_deadline = nc_now() + 1000;
    while (audio_position() <= paused + 30 && nc_now() < resume_deadline)
        Sleep(20);
    CHECK(audio_position() > paused + 30);
    audio_close();
    CHECK(audio_position() == -1);
    CHECK(!audio_open("Z:/missing-note-collision.wav", 0, 33800, error));
    puts("PASS real WAV preload, play, pause, resume, clock and missing media");
}
/**
 * @brief 验证 TCP 分帧器对半包和坏数据的处理。
 * 参数：无参数，使用本地构造的字节数组。
 * 返回：无返回值，检查返回码、消费长度和 JSON 对象释放。
 * 说明：遍历完整消息到达前的每个截断长度，并覆盖超长前缀、零长度、非法 UTF-8 与对象后尾随字符；这里不需要创建真实
 * socket。
 */
static void frame_tests(void) {
    const char *body = "{\"protocol\":2,\"seq\":1}";
    size_t size = strlen(body), used;
    unsigned char frame[256];
    frame[0] = frame[1] = frame[2] = 0;
    frame[3] = (unsigned char)size;
    memcpy(frame + 4, body, size);
    cJSON *o;
    for (size_t n = 0; n < size + 4; n++)
        CHECK(net_frame(frame, n, &o, &used) == 0);
    CHECK(net_frame(frame, size + 4, &o, &used) == 1);
    CHECK(used == size + 4);
    cJSON_Delete(o);
    frame[0] = 1;
    CHECK(net_frame(frame, size + 4, &o, &used) == -1);
    frame[0] = frame[3] = 0;
    CHECK(net_frame(frame, size + 4, &o, &used) == -1);
    frame[3] = 2;
    frame[4] = 0xff;
    frame[5] = 0xff;
    CHECK(net_frame(frame, 6, &o, &used) == -1);
    frame[3] = 3;
    memcpy(frame + 4, "{}x", 3);
    CHECK(net_frame(frame, 7, &o, &used) == -1);
    puts("PASS fragmented, oversized, invalid UTF-8 and trailing-data frames");
}
/**
 * @brief 在限定时长内交替推进两个测试端的网络状态。
 * 参数：h/g 为已初始化网络对象；ms 为非负等待毫秒数。
 * 返回：无返回值；状态变化留在两个对象中。
 * 说明：以单调时钟循环调用两端 net_poll，每轮 Sleep(1) 给系统 socket
 * 时间；仅用于测试，没有额外线程或人工伪造协议状态。
 */
static void pump(Network *h, Network *g, int ms) {
    int64_t end = nc_now() + ms;
    while (nc_now() < end) {
        net_poll(h);
        net_poll(g);
        Sleep(1);
    }
}
/**
 * @brief 检查本机双端测试是否意外中止。
 * 参数：h/g 为两端 Network。
 * 返回：无返回值；任一为 NET_ABORT 时打印双方状态并触发 CHECK(false)。
 * 说明：辅助把模糊的状态断言错误变成可读的双方错误原因；不修复网络、不重试，也不改变被测状态。
 */
static void healthy(Network *h, Network *g) {
    if (h->state == NET_ABORT || g->state == NET_ABORT) {
        fprintf(stderr, "host: %s\nguest: %s\n", h->status, g->status);
        CHECK(false);
    }
}
/**
 * @brief 通过真实 localhost TCP 验证完整对战和异常流程。
 * 参数：l 为有效曲库，提供资源校验与完整统计的基准。
 * 返回：无返回值，所有断言完成后关闭 socket 并配对 Winsock 清理。
 * 说明：建立房主/客人并同步时钟，跑两轮准备、开局、比分与结果确认，中间含
 * rematch；交替改变双方完赛先后顺序，检查复赛紧随确认时仍能观察到成绩。还覆盖重复提交、断线、非法地址及超大帧。
 */
static void network_tests(const Library *l) {
    CHECK(net_init(error));
    Network *h = calloc(1, sizeof(*h)), *g = calloc(1, sizeof(*g));
    CHECK(h && g);
    net_reset(h);
    net_reset(g);
    CHECK(net_host(h, 0, "host", error));
    CHECK(h->port > 0);
    CHECK(net_join(g, "127.0.0.1", h->port, "guest", error));
    pump(h, g, 2000);
    healthy(h, g);
    CHECK(h->handshake && g->handshake && g->samples >= 3 && h->samples >= 3);
    Chart c = {0};
    CHECK(chart_load(l, 1, 0, &c, error));
    for (int round = 0; round < 2; round++) {
        CHECK(net_configure(h, 1, 0, &c, error));
        pump(h, g, 50);
        healthy(h, g);
        CHECK(g->configured);
        CHECK(net_verify(g, &c, error));
        pump(h, g, 50);
        CHECK(h->peer_verified);
        CHECK(net_ready(h, error));
        CHECK(net_ready(g, error));
        pump(h, g, 50);
        CHECK(h->peer_ready && g->peer_ready);
        CHECK(net_start(h, error));
        pump(h, g, 3200);
        healthy(h, g);
        CHECK(h->state == NET_PLAY && g->state == NET_PLAY);
        CHECK(llabs(h->target - g->target) < 50);
        Stats a = {0}, b = {0};
        for (int i = 0; i < c.count; i++) {
            stats_apply(&a, PERFECT);
            stats_apply(&b, round ? GREAT : PERFECT);
        }
        net_stats(h, &a, 33000);
        net_stats(g, &b, 33000);
        pump(h, g, 30);
        CHECK(h->peer_stats.finalized == c.count);
        Network *first = round ? g : h, *second = round ? h : g;
        const Stats *first_score = round ? &b : &a, *second_score = round ? &a : &b;
        CHECK(net_finish(first, first_score, error));
        CHECK(!net_finish(first, first_score, error));
        pump(h, g, 50);
        CHECK(first->state == NET_WAIT && second->state == NET_PLAY && second->peer_finished);
        CHECK(net_finish(second, second_score, error));
        if (round == 0) {
            int64_t deadline = nc_now() + 500;
            while (h->state == NET_WAIT && nc_now() < deadline) {
                net_poll(g);
                net_poll(h);
                Sleep(1);
            }
            CHECK(h->state == NET_DONE && g->state == NET_WAIT);
            CHECK(net_rematch(h, error));
            net_poll(h);
            deadline = nc_now() + 500;
            while (g->state == NET_WAIT && nc_now() < deadline) {
                net_poll(g);
                Sleep(1);
            }
        } else
            pump(h, g, 100);
        healthy(h, g);
        CHECK(h->confirmed && g->confirmed && h->state == NET_DONE && g->state == NET_DONE);
        CHECK(stats_result(&h->peer_stats, c.count).score == (round ? 700000 : 1000000));
        if (round == 0) {
            pump(h, g, 100);
            healthy(h, g);
            CHECK(h->state == NET_ROOM && g->state == NET_ROOM && !h->ready && !g->ready &&
                  !h->configured && !g->configured);
        }
    }
    net_leave(h);
    pump(h, g, 20);
    net_leave(g);
    CHECK(net_host(h, 0, "host", error));
    CHECK(net_join(g, "127.0.0.1", h->port, "guest", error));
    pump(h, g, 50);
    net_leave(g);
    net_poll(h);
    CHECK(h->state == NET_ABORT && !h->confirmed);
    net_leave(h);
    CHECK(!net_join(g, "not-an-ip", 9527, "guest", error));
    CHECK(net_host(h, 0, "host", error));
    SOCKET raw = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    struct sockaddr_in address = {0};
    address.sin_family = AF_INET;
    address.sin_port = htons((u_short)h->port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    CHECK(connect(raw, (struct sockaddr *)&address, sizeof(address)) == 0);
    net_poll(h);
    unsigned char oversized[] = {0, 2, 0, 0};
    CHECK(send(raw, (const char *)oversized, 4, 0) == 4);
    Sleep(5);
    net_poll(h);
    CHECK(h->state == NET_ABORT);
    closesocket(raw);
    net_leave(h);
    chart_free(&c);
    free(h);
    free(g);
    net_shutdown();
    puts("PASS real TCP handshake, resource verification, sync, two rounds, confirmed results, "
         "disconnect and malformed peer");
}
/**
 * @brief 运行全部回归测试并报告断言总数。
 * 参数：argc 应为 2；argv[1] 指向真实 assets/songs。
 * 返回：全部测试成功返回 0；任意 CHECK 失败通过 exit(1) 退出。
 * 说明：在堆上分配曲库后依次执行计分、判定、资源、文件、音频、分帧和网络集成测试；最后释放曲库并输出总断言数。
 */
int main(int argc, char **argv) {
    CHECK(argc == 2);
    Library *l = calloc(1, sizeof(*l));
    CHECK(l != NULL);
    score_tests();
    engine_tests();
    resource_tests(l, argv[1]);
    storage_tests(l);
    audio_tests(l);
    frame_tests();
    network_tests(l);
    free(l);
    printf("ALL PASS: %d assertions\n", checks);
    return 0;
}
