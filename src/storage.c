#define WIN32_LEAN_AND_MEAN
#include "json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
/**
 * @brief 拼接某类用户数据的完整文件路径。
 * 参数：out 至少 NC_PATH 字节；root 为数据目录；file 为固定文件名。
 * 返回：无返回值，输出 root/file。
 * 说明：只拼接文本，不创建目录或验证文件；调用方应保证组合长度可装入缓冲区，因为该包装不单独报告 snprintf 截断。
 */
static void filename(char out[NC_PATH], const char *root, const char *file) {
    snprintf(out, NC_PATH, "%s/%s", root, file);
}
/**
 * @brief 验证本地账号/密码文本的编码、长度和控制字符。
 * 参数：s 为零结尾 UTF-8 字符串；max 为允许的 UTF-16 代码单元数。
 * 返回：有效非空、转换成功且不超过上限时返回 true，否则返回 false。
 * 说明：转换缓冲区为 256 个 wchar_t，拒绝小于 32 或等于 127 的字符。空格在此允许；账号首尾空格由
 * account_action 另行限制，密码空格原样保留。
 */
static bool valid_text(const char *s, int max) {
    wchar_t w[256];
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, w, 256);
    if (n < 2 || n - 1 > max)
        return false;
    for (int i = 0; i < n - 1; i++)
        if (w[i] < 32 || w[i] == 127)
            return false;
    return true;
}
/**
 * @brief 注册本机账号，或校验已有账号密码。
 * 参数：root 为数据目录；name/password 为 UTF-8 输入；registration 为 true
 * 表示注册；error 可空。
 * 返回：注册落盘或登录验证成功返回 true，输入、文件或认证失败返回 false。
 * 说明：账号 1—16、密码 1—64 个 UTF-16 单元，禁止控制字符；读取版本为 1 的
 * accounts.json。注册拒绝同名并保存随机盐及 SHA-256(盐:密码)，登录只校验不写入；所有 JSON
 * 树在返回前释放。
 */
bool account_action(const char *root, const char *name, const char *password, bool registration,
                    char *error) {
    if (!valid_text(name, 16) || !valid_text(password, 64) || name[0] == ' ' ||
        name[strlen(name) - 1] == ' ')
        return nc_error(error,
                        "账号为 1—16 字符，密码为 1—64 字符；不能包含控制字符，账号首尾不能有空格");
    char path[NC_PATH];
    filename(path, root, "accounts.json");
    cJSON *o;
    if (nc_exists(path))
        o = json_read(path, 1024 * 1024, error);
    else {
        o = cJSON_CreateObject();
        cJSON_AddNumberToObject(o, "schemaVersion", 1);
        cJSON_AddArrayToObject(o, "accounts");
    }
    if (!o)
        return false;
    int schema;
    cJSON *a = cJSON_GetObjectItemCaseSensitive(o, "accounts");
    if (!json_int(o, "schemaVersion", 1, 1, &schema) || !cJSON_IsArray(a)) {
        cJSON_Delete(o);
        return nc_error(error, "账号文件损坏，已保留");
    }
    bool exists = false, authenticated = false;
    char salt[65], digest[65], candidate[512];
    cJSON *v;
    cJSON_ArrayForEach(v, a) {
        char user[128], saved[65], s[65];
        if (!json_str(v, "name", user, sizeof(user)) || !json_str(v, "salt", s, sizeof(s)) ||
            !json_str(v, "hash", saved, sizeof(saved))) {
            cJSON_Delete(o);
            return nc_error(error, "账号字段损坏，已保留");
        }
        if (strcmp(user, name))
            continue;
        exists = true;
        snprintf(candidate, sizeof(candidate), "%s:%s", s, password);
        if (nc_hash(candidate, strlen(candidate), digest))
            authenticated = !strcmp(saved, digest);
    }
    if (!registration) {
        cJSON_Delete(o);
        return authenticated || nc_error(error, "账号或密码错误");
    }
    if (exists) {
        cJSON_Delete(o);
        return nc_error(error, "账号已存在");
    }
    if (cJSON_GetArraySize(a) >= 1000) {
        cJSON_Delete(o);
        return nc_error(error, "账号数量达到上限");
    }
    nc_uuid(salt);
    snprintf(candidate, sizeof(candidate), "%s:%s", salt, password);
    if (!nc_hash(candidate, strlen(candidate), digest)) {
        cJSON_Delete(o);
        return nc_error(error, "密码摘要失败");
    }
    v = cJSON_CreateObject();
    cJSON_AddStringToObject(v, "name", name);
    cJSON_AddStringToObject(v, "salt", salt);
    cJSON_AddStringToObject(v, "hash", digest);
    cJSON_AddItemToArray(a, v);
    bool ok = nc_mkdir(root, error) && json_save(path, o, error);
    cJSON_Delete(o);
    return ok;
}
/**
 * @brief 读取设置，出错时保留默认配置供程序启动。
 * 参数：root 为数据目录；s 为非空输出；error 可空。
 * 返回：文件不存在或已成功读取时返回 true；文件损坏、未知版本或字段非法返回 false。
 * 说明：默认音乐音量 70、偏移 0；限制音量 0—100、偏移 ±200ms。读取版本 1 的音乐音量和校准偏移，
 * 保留原文件中的旧音效字段。
 */
bool settings_load(const char *root, Settings *s, char *error) {
    *s = (Settings){.music = 70, .offset = 0};
    char path[NC_PATH];
    filename(path, root, "settings.json");
    if (!nc_exists(path))
        return true;
    cJSON *o = json_read(path, 65536, error);
    if (!o)
        return false;
    int schema;
    Settings temp;
    bool ok = json_int(o, "schemaVersion", 1, 1, &schema) &&
              json_int(o, "music", 0, 100, &temp.music) &&
              json_int(o, "offset", -200, 200, &temp.offset);
    cJSON_Delete(o);
    if (ok)
        *s = temp;
    return ok || nc_error(error, "设置文件损坏或版本未知，保留原文件并使用默认值");
}
/**
 * @brief 从 JSON 解析一条完整、有效的历史成绩。
 * 参数：v 为单条 JSON 对象；r 为输出记录。
 * 返回：身份、模式、规则、范围及完整统计均合法时返回 true，否则返回 false。
 * 说明：先清零 r，失败可留下部分字段。联机记录额外要求 match、opponent、opponentScore；只接受当前
 * NC_RULES，哈希要求 64 字节，不在此验证成绩的外部来源。
 */
static bool record_parse(const cJSON *v, Record *r) {
    memset(r, 0, sizeof(*r));
    return json_str(v, "session", r->session, sizeof(r->session)) &&
           json_str(v, "name", r->name, sizeof(r->name)) &&
           json_str(v, "chart", r->chart, sizeof(r->chart)) &&
           json_str(v, "hash", r->hash, sizeof(r->hash)) && strlen(r->hash) == 64 &&
           json_str(v, "difficulty", r->difficulty, sizeof(r->difficulty)) &&
           json_str(v, "date", r->date, sizeof(r->date)) && json_int(v, "mode", 0, 1, &r->mode) &&
           json_int(v, "rules", NC_RULES, NC_RULES, &r->rules) &&
           json_int(v, "version", 1, 100000, &r->version) &&
           json_int(v, "count", 1, NC_NOTES, &r->count) &&
           (!r->mode || (json_str(v, "match", r->match, sizeof(r->match)) &&
                         json_str(v, "opponent", r->opponent, sizeof(r->opponent)) &&
                         json_int(v, "opponentScore", 0, 1000000, &r->opponent_score))) &&
           stats_parse(cJSON_GetObjectItemCaseSensitive(v, "stats"), &r->stats, r->count, true);
}
/**
 * @brief 把内存成绩转换成持久化 JSON 对象。
 * 参数：r 为经上层验证的借用 Record。
 * 返回：返回新建 cJSON 对象，所有权交给调用者，需挂接到数组或删除。
 * 说明：写入身份、谱面、UTC 日期、模式、对手及原始统计；不保存可重新计算的
 * score/grade，避免它们与统计失配，不在这里进行文件操作。
 */
static cJSON *record_json(const Record *r) {
    cJSON *v = cJSON_CreateObject();
    cJSON_AddStringToObject(v, "session", r->session);
    cJSON_AddStringToObject(v, "match", r->match);
    cJSON_AddStringToObject(v, "name", r->name);
    cJSON_AddStringToObject(v, "opponent", r->opponent);
    cJSON_AddStringToObject(v, "chart", r->chart);
    cJSON_AddStringToObject(v, "hash", r->hash);
    cJSON_AddStringToObject(v, "difficulty", r->difficulty);
    cJSON_AddStringToObject(v, "date", r->date);
    cJSON_AddNumberToObject(v, "mode", r->mode);
    cJSON_AddNumberToObject(v, "rules", r->rules);
    cJSON_AddNumberToObject(v, "version", r->version);
    cJSON_AddNumberToObject(v, "count", r->count);
    cJSON_AddNumberToObject(v, "opponentScore", r->opponent_score);
    cJSON_AddItemToObject(v, "stats", stats_json(&r->stats));
    return v;
}
/**
 * @brief 释放成绩列表并清空仓库状态。
 * 参数：s 为已清零或有效初始化的 Store。
 * 返回：无返回值；释放 items 后清零整个结构。
 * 说明：不删除磁盘成绩，也不释放 s 本身。调用前应停止使用 store_ranking 返回的借用记录指针。
 */
void store_free(Store *s) {
    free(s->items);
    memset(s, 0, sizeof(*s));
}
/**
 * @brief 把本地完整成绩文件装入内存仓库。
 * 参数：s 必须已清零或有效初始化；root 为独立数据路径字符串，不能借用即将被清空的 s 内存；error 可空。
 * 返回：不存在文件时以空可写仓库返回 true；合法文件返回 true；读入或验证失败返回 false。
 * 说明：先释放旧列表，最多 20000 条，检查版本、每条完整统计及重复 session。失败清理部分数组并设
 * writable=false，保留磁盘文件，阻止后续误覆盖。
 */
bool store_load(Store *s, const char *root, char *error) {
    store_free(s);
    nc_copy(s->root, sizeof(s->root), root);
    s->writable = true;
    char path[NC_PATH];
    filename(path, root, "records.json");
    if (!nc_exists(path))
        return true;
    cJSON *o = json_read(path, 16 * 1024 * 1024, error);
    if (!o) {
        s->writable = false;
        return false;
    }
    int schema;
    cJSON *a = cJSON_GetObjectItemCaseSensitive(o, "records");
    int n = cJSON_GetArraySize(a);
    bool ok = json_int(o, "schemaVersion", 1, 1, &schema) && cJSON_IsArray(a) && n <= 20000;
    if (ok && n) {
        s->items = calloc((size_t)n, sizeof(Record));
        ok = s->items != NULL;
    }
    for (int i = 0; ok && i < n; i++) {
        ok = record_parse(cJSON_GetArrayItem(a, i), &s->items[i]);
        for (int j = 0; ok && j < i; j++)
            if (!strcmp(s->items[j].session, s->items[i].session))
                ok = false;
    }
    cJSON_Delete(o);
    if (ok)
        s->count = n;
    else {
        free(s->items);
        s->items = NULL;
        s->count = 0;
        s->writable = false;
        nc_error(error, "成绩文件损坏或版本未知，保留原文件并禁止覆盖");
    }
    return ok;
}
/**
 * @brief 幂等地追加一条完整成绩，并原子提交整个列表。
 * 参数：s 为已载入仓库；r 为待保存且完整的成绩；error 可空。
 * 返回：新记录落盘成功或已存在相同会话/联机玩家记录时返回 true，否则返回 false。
 * 说明：拒绝不可写仓库、缺字段、非法统计及超 20000 条；先扩容和序列化，磁盘提交成功后才增加 count。realloc
 * 可使旧记录指针失效，调用后需重新查询排行榜；联机确认资格由应用层负责。
 */
bool store_save(Store *s, const Record *r, char *error) {
    if (!s->writable)
        return nc_error(error, "成绩文件不可写，请先检查已保留的损坏文件");
    if (!stats_valid(&r->stats, r->count, true) || !r->session[0] || r->rules != NC_RULES ||
        !r->name[0] || strlen(r->hash) != 64 || (r->mode && (!r->match[0] || !r->opponent[0])))
        return nc_error(error, "拒绝保存不完整成绩");
    for (int i = 0; i < s->count; i++) {
        if (!strcmp(s->items[i].session, r->session) ||
            (r->mode && !strcmp(s->items[i].match, r->match) && !strcmp(s->items[i].name, r->name)))
            return true;
    }
    if (s->count >= 20000)
        return nc_error(error, "成绩数量达到上限");
    Record *p = realloc(s->items, (size_t)(s->count + 1) * sizeof(Record));
    if (!p)
        return nc_error(error, "内存不足");
    s->items = p;
    cJSON *o = cJSON_CreateObject(), *a = cJSON_AddArrayToObject(o, "records");
    cJSON_AddNumberToObject(o, "schemaVersion", 1);
    for (int i = 0; i < s->count; i++)
        cJSON_AddItemToArray(a, record_json(&s->items[i]));
    cJSON_AddItemToArray(a, record_json(r));
    char path[NC_PATH];
    filename(path, s->root, "records.json");
    bool ok = nc_mkdir(s->root, error) && json_save(path, o, error);
    cJSON_Delete(o);
    if (ok)
        s->items[s->count++] = *r;
    return ok;
}
/**
 * @brief 为成绩列表提供降序分数排序规则。
 * 参数：a/b 各指向一个 const Record*，用于 qsort 的指针数组。
 * 返回：高分排前；同分按 date 字符串升序；返回比较值。
 * 说明：分数通过 stats_result 从原始统计计算；同分日期只决定展示次序，不决定排名，具体并列排名由 UI
 * 根据分数变化计算。
 */
static int rank_compare(const void *a, const void *b) {
    const Record *x = *(const Record *const *)a, *y = *(const Record *const *)b;
    int sx = stats_result(&x->stats, x->count).score, sy = stats_result(&y->stats, y->count).score;
    return sx == sy ? strcmp(x->date, y->date) : sy - sx;
}
/**
 * @brief 将同一账号的记录排在一起，并让其最高分位于最前。
 * 参数：a/b 指向 Record 指针；比较过程不修改原始成绩。
 * 返回：先按账号排序，同账号复用分数和日期规则。
 * 说明：先分组再去重可避免逐条扫描账号的平方复杂度，最多 20000 条记录时仍能快速查询。
 */
static int account_rank_compare(const void *a, const void *b) {
    const Record *x = *(const Record *const *)a, *y = *(const Record *const *)b;
    int name = strcmp(x->name, y->name);
    return name ? name : rank_compare(a, b);
}
/**
 * @brief 查询某模式下与当前谱面完全匹配的账号最佳成绩榜。
 * 参数：s 为仓库；c 为有效谱面；mode 为 0 单人/1 联机；out 至少 cap 个指针；cap 必须非负。
 * 返回：返回写入 out 的数量，最大 cap；无记录或临时分配失败均返回 0。
 * 说明：按模式、规则、谱面版本、ID、哈希、难度过滤；每账号仅保留最高分，同分取较早日期。原始记录全部保留。
 * 输出指针借用仓库内存，不得释放；store_free、store_
 * load 或会 realloc 的 store_save 后不能继续使用。
 */
int store_ranking(const Store *s, const Chart *c, int mode, const Record **out, int cap) {
    if (!out || cap <= 0)
        return 0;
    const Record **all = malloc((size_t)(s->count + 1) * sizeof(*all));
    if (!all)
        return 0;
    int n = 0;
    for (int i = 0; i < s->count; i++) {
        const Record *r = &s->items[i];
        if (r->mode == mode && r->rules == NC_RULES && r->version == c->version &&
            !strcmp(r->hash, c->hash) && !strcmp(r->chart, c->id) &&
            !strcmp(r->difficulty, c->difficulty))
            all[n++] = r;
    }
    qsort(all, (size_t)n, sizeof(*all), account_rank_compare);
    int unique = 0;
    for (int i = 0; i < n; i++) {
        /* 同账号已按成绩排序，只取组首；先去重再截断，避免重复账号占满榜单容量。 */
        if (!unique || strcmp(all[i]->name, all[unique - 1]->name))
            all[unique++] = all[i];
    }
    n = unique;
    qsort(all, (size_t)n, sizeof(*all), rank_compare);
    if (n > cap)
        n = cap;
    memcpy(out, all, (size_t)n * sizeof(*all));
    free(all);
    return n;
}
