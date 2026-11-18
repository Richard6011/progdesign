#define WIN32_LEAN_AND_MEAN
#include "json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

/**
 * @brief 打开一个版本为 1 的本地数据列表。
 * 参数：path 为完整文件路径；key 为列表字段；limit 为读取上限；error 可空。
 * 返回：调用方拥有的 JSON 根对象，或失败时的 NULL。
 * 说明：文件尚不存在时创建空列表；文件已存在且格式错误时保留原文件。
 */
static cJSON *open_list(const char *path, const char *key, size_t limit, char *error) {
    if (!nc_exists(path)) {
        cJSON *file = cJSON_CreateObject();
        if (cJSON_AddNumberToObject(file, "schemaVersion", 1) && cJSON_AddArrayToObject(file, key))
            return file;
        cJSON_Delete(file);
        nc_error(error, "无法创建数据文件");
        return NULL;
    }

    cJSON *file = json_read(path, limit, error);
    int schema;
    if (file && (!json_int(file, "schemaVersion", 1, 1, &schema) ||
                 !cJSON_IsArray(cJSON_GetObjectItemCaseSensitive(file, key)))) {
        cJSON_Delete(file);
        nc_error(error, "数据文件损坏或版本未知，保留原文件：%s", path);
        return NULL;
    }
    return file;
}

/**
 * @brief 比较 JSON 记录的一个文本字段。
 * 参数：item 为记录；key 为字段名；value 为待比较的字符串。
 * 返回：字段存在且与 value 完全相同时返回 true。
 */
static bool text_is(const cJSON *item, const char *key, const char *value) {
    const char *text = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(item, key));
    return text && !strcmp(text, value);
}

/**
 * @brief 验证本地账号或密码的长度和控制字符。
 * 参数：s 为 UTF-8 字符串；max 为允许的 UTF-16 代码单元数。
 * 返回：转换成功、非空且无控制字符时返回 true。
 * 说明：密码中的空格原样保留，账号的首尾空格在提交时另行检查。
 */
static bool valid_text(const char *s, int max) {
    wchar_t wide[256];
    int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, wide, 256) - 1;
    if (length < 1 || length > max)
        return false;
    for (int i = 0; i < length; i++)
        if (wide[i] < 32 || wide[i] == 127)
            return false;
    return true;
}

/**
 * @brief 注册本机账号或用密码登录现有账号。
 * 参数：root 为数据目录；name/password 为表单文字；registration 为 true 表示注册；error 可空。
 * 返回：验证成功或注册落盘成功返回 true。
 * 说明：注册和登录共用 SHA-256(盐:密码) 的计算，原有 accounts.json 可直接使用。
 */
bool account_action(const char *root, const char *name, const char *password, bool registration,
                    char *error) {
    if (!valid_text(name, 16) || !valid_text(password, 64) || name[0] == ' ' ||
        name[strlen(name) - 1] == ' ')
        return nc_error(error,
                        "账号为 1—16 字符，密码为 1—64 字符；不能包含控制字符，账号首尾不能有空格");

    char path[NC_PATH];
    snprintf(path, sizeof(path), "%s/accounts.json", root);
    cJSON *file = open_list(path, "accounts", 1024 * 1024, error);
    if (!file)
        return false;
    cJSON *accounts = cJSON_GetObjectItemCaseSensitive(file, "accounts"), *user;
    cJSON_ArrayForEach(user, accounts)
        if (text_is(user, "name", name))
            break;

    bool ok = false;
    char salt[65], digest[65], candidate[512];
    if (registration) {
        if (user || cJSON_GetArraySize(accounts) >= 1000) {
            nc_error(error, user ? "账号已存在" : "账号数量达到上限");
            goto done;
        }
        nc_uuid(salt);
    } else if (!json_str(user, "salt", salt, sizeof(salt))) {
        nc_error(error, "账号或密码错误");
        goto done;
    }

    snprintf(candidate, sizeof(candidate), "%s:%s", salt, password);
    if (!nc_hash(candidate, strlen(candidate), digest)) {
        nc_error(error, "密码摘要失败");
    } else if (registration) {
        user = cJSON_CreateObject();
        cJSON_AddItemToArray(accounts, user);
        cJSON_AddStringToObject(user, "name", name);
        cJSON_AddStringToObject(user, "salt", salt);
        cJSON_AddStringToObject(user, "hash", digest);
        ok = nc_mkdir(root, error) && json_save(path, file, error);
    } else {
        ok = text_is(user, "hash", digest) || nc_error(error, "账号或密码错误");
    }
done:
    cJSON_Delete(file);
    return ok;
}

/**
 * @brief 读取旧设置中的音量及输入校准偏移。
 * 参数：root 为数据目录；s 为输出；error 可空。
 * 返回：文件不存在或读取成功返回 true；格式错误时保留默认值并返回 false。
 */
bool settings_load(const char *root, Settings *s, char *error) {
    *s = (Settings){.music = 70};
    char path[NC_PATH];
    snprintf(path, sizeof(path), "%s/settings.json", root);
    if (!nc_exists(path))
        return true;
    cJSON *file = json_read(path, 65536, error);
    if (!file)
        return false;
    int schema;
    Settings settings;
    bool ok = json_int(file, "schemaVersion", 1, 1, &schema) &&
              json_int(file, "music", 0, 100, &settings.music) &&
              json_int(file, "offset", -200, 200, &settings.offset);
    cJSON_Delete(file);
    if (ok)
        *s = settings;
    return ok || nc_error(error, "设置文件损坏或版本未知，保留原文件并使用默认值");
}

/**
 * @brief 释放内存中的成绩文档。
 * 参数：s 为已清零或已加载的仓库。
 * 返回：无返回值。借用文档中昵称、日期的榜单行随之失效。
 */
void store_free(Store *s) {
    cJSON_Delete(s->file);
    *s = (Store){0};
}

/**
 * @brief 打开已有成绩文件，或准备一个空的成绩列表。
 * 参数：s 为已初始化的仓库；root 为独立输入路径；error 可空。
 * 返回：成功返回 true。格式错误时无法写入，旧文件仍在。
 * 说明：直接保留 JSON 文档。渲染榜单时才读取相关记录的显示字段。
 */
bool store_load(Store *s, const char *root, char *error) {
    store_free(s);
    nc_copy(s->root, sizeof(s->root), root);
    char path[NC_PATH];
    snprintf(path, sizeof(path), "%s/records.json", root);
    s->file = open_list(path, "records", 16 * 1024 * 1024, error);
    s->records = cJSON_GetObjectItemCaseSensitive(s->file, "records");
    s->count = cJSON_GetArraySize(s->records);
    return s->file != NULL;
}

/**
 * @brief 向当前成绩文件追加一条完赛记录。
 * 参数：s 为已加载的仓库；r 借用本次完赛数据；error 可空。
 * 返回：成功写盘或已存在同一局时返回 true。
 * 说明：保持已有 JSON 字段。若写盘失败，从内存文档移除本次添加的记录。
 * 同一会话或同一联机局的同一账号只记录一次。
 */
bool store_save(Store *s, const Record *r, char *error) {
    if (!s->records)
        return nc_error(error, "成绩文件不可写，请先检查已保留的损坏文件");
    const Stats *stats = r->stats;
    const Chart *chart = r->chart;
    int total = stats->hits[PERFECT] + stats->hits[GREAT] + stats->hits[GOOD] + stats->hits[MISS];
    if (chart->count <= 0 || stats->finalized != chart->count || total != chart->count)
        return nc_error(error, "拒绝保存不完整成绩");
    const cJSON *existing;
    cJSON_ArrayForEach(existing, s->records)
        if (text_is(existing, "session", r->session) ||
            (r->match && text_is(existing, "match", r->match) && text_is(existing, "name", r->name)))
            return true;

    cJSON *item = cJSON_CreateObject();
    if (!cJSON_AddItemToArray(s->records, item)) {
        cJSON_Delete(item);
        return nc_error(error, "内存不足");
    }
    cJSON_AddStringToObject(item, "session", r->session);
    cJSON_AddStringToObject(item, "name", r->name);
    cJSON_AddStringToObject(item, "date", r->date);
    cJSON_AddNumberToObject(item, "mode", r->match != NULL);
    cJSON_AddNumberToObject(item, "rules", NC_RULES);
    cJSON_AddStringToObject(item, "chart", chart->id);
    cJSON_AddStringToObject(item, "hash", chart->hash);
    cJSON_AddStringToObject(item, "difficulty", chart->difficulty);
    cJSON_AddNumberToObject(item, "version", chart->version);
    cJSON_AddNumberToObject(item, "count", chart->count);
    cJSON_AddStringToObject(item, "match", r->match ? r->match : "");
    cJSON_AddStringToObject(item, "opponent", r->opponent ? r->opponent : "");
    cJSON_AddNumberToObject(item, "opponentScore", r->opponent_score);
    cJSON_AddItemToObject(item, "stats", stats_json(stats));

    char path[NC_PATH];
    snprintf(path, sizeof(path), "%s/records.json", s->root);
    if (!nc_mkdir(s->root, error) || !json_save(path, s->file, error)) {
        cJSON_Delete(cJSON_DetachItemViaPointer(s->records, item));
        return false;
    }
    s->count++;
    return true;
}

/**
 * @brief 在榜单中优先展示高分和较早的同分记录。
 * 参数：a/b 指向 RankRow。
 * 返回：qsort 比较值。同分者日期只影响显示顺序，名次仍相同。
 */
static int rank_compare(const void *a, const void *b) {
    const RankRow *x = a, *y = b;
    if (x->result.score != y->result.score)
        return y->result.score - x->result.score;
    return strcmp(x->date, y->date);
}

/**
 * @brief 将同一账号的成绩排在一起。
 * 参数：a/b 指向 RankRow。
 * 返回：优先按昵称排序，同账号内沿用榜单的分数和日期顺序。
 */
static int account_compare(const void *a, const void *b) {
    const RankRow *x = a, *y = b;
    int name = strcmp(x->name, y->name);
    return name ? name : rank_compare(a, b);
}

/**
 * @brief 从成绩文档生成当前谱面每个账号的最佳记录。
 * 参数：s 为仓库；c 为选择的谱面；mode 为单人或联机；rows 至少有 s->count 个位置。
 * 返回：写入 rows 的实际账号数。
 * 说明：谱面文件的指纹覆盖曲目、难度和谱面版本。只解读榜单使用的字段，
 * 排序后每个账号取最高分，再按分数和日期排列，昵称和日期借用文档中的字符串。
 */
int store_ranking(const Store *s, const Chart *c, int mode, RankRow *rows) {
    if (!c->count)
        return 0;
    int n = 0;
    const cJSON *item;
    cJSON_ArrayForEach(item, s->records) {
        int value, units, max_combo;
        const cJSON *stats = cJSON_GetObjectItemCaseSensitive(item, "stats");
        const char *name = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(item, "name"));
        const char *date = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(item, "date"));
        if (name && date && text_is(item, "hash", c->hash) &&
            json_int(item, "mode", mode, mode, &value) &&
            json_int(item, "rules", NC_RULES, NC_RULES, &value) &&
            json_int(stats, "units", 0, c->count * 1000, &units) &&
            json_int(stats, "maxCombo", 0, c->count, &max_combo)) {
            Result result = stats_result(&(Stats){.units = units, .max_combo = max_combo}, c->count);
            rows[n++] = (RankRow){name, date, result};
        }
    }
    qsort(rows, (size_t)n, sizeof(*rows), account_compare);
    int unique = 0;
    for (int i = 0; i < n; i++)
        if (!unique || strcmp(rows[i].name, rows[unique - 1].name))
            rows[unique++] = rows[i];
    qsort(rows, (size_t)unique, sizeof(*rows), rank_compare);
    return unique;
}
