#define WIN32_LEAN_AND_MEAN
#include "json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

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

static bool text_is(const cJSON *item, const char *key, const char *value) {
    const char *text = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(item, key));
    return text && !strcmp(text, value);
}

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

void store_free(Store *s) {
    cJSON_Delete(s->file);
    *s = (Store){0};
}

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

static int rank_compare(const void *a, const void *b) {
    const RankRow *x = a, *y = b;
    if (x->result.score != y->result.score)
        return y->result.score - x->result.score;
    return strcmp(x->date, y->date);
}

static int account_compare(const void *a, const void *b) {
    const RankRow *x = a, *y = b;
    int name = strcmp(x->name, y->name);
    return name ? name : rank_compare(a, b);
}

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
