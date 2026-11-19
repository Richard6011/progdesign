#include "json.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
bool json_int(const cJSON *o, const char *key, int lo, int hi, int *out) {
    cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    if (!cJSON_IsNumber(v) || !isfinite(v->valuedouble) ||
        floor(v->valuedouble) != v->valuedouble || v->valuedouble < lo || v->valuedouble > hi)
        return false;
    *out = (int)v->valuedouble;
    return true;
}
bool json_str(const cJSON *o, const char *key, char *out, size_t cap) {
    cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    if (!cJSON_IsString(v) || !v->valuestring || !v->valuestring[0] ||
        strlen(v->valuestring) >= cap)
        return false;
    nc_copy(out, cap, v->valuestring);
    return true;
}
cJSON *json_read(const char *path, size_t limit, char *error) {
    char *text;
    size_t size;
    if (!nc_read(path, &text, &size, limit, error))
        return NULL;
    cJSON *o = cJSON_ParseWithLengthOpts(text, size + 1, NULL, 1);
    free(text);
    if (!cJSON_IsObject(o)) {
        cJSON_Delete(o);
        nc_error(error, "JSON 文件格式错误：%s", path);
        return NULL;
    }
    return o;
}
bool json_save(const char *path, const cJSON *o, char *error) {
    char *text = cJSON_Print(o);
    if (!text)
        return nc_error(error, "JSON 内存不足");
    bool ok = nc_atomic(path, text, strlen(text), error);
    cJSON_free(text);
    return ok;
}
cJSON *stats_json(const Stats *s) {
    cJSON *o = cJSON_CreateObject();
    static const char *const keys[] = {"perfect", "great", "good", "miss"};
    for (int j = PERFECT; j <= MISS; j++)
        cJSON_AddNumberToObject(o, keys[j], s->hits[j]);
    cJSON_AddNumberToObject(o, "combo", s->combo);
    cJSON_AddNumberToObject(o, "maxCombo", s->max_combo);
    cJSON_AddNumberToObject(o, "finalized", s->finalized);
    cJSON_AddNumberToObject(o, "units", (double)s->units);
    return o;
}
