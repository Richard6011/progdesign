#include "json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void chart_free(Chart *c) {
    free(c->notes);
    memset(c, 0, sizeof(*c));
}
static bool path_join(const Library *l, const char *rel, char *path, char *error) {
    if (!rel[0] || rel[0] == '/' || rel[0] == '\\' || strstr(rel, "..") || strchr(rel, ':'))
        return nc_error(error, "资源路径非法：%s", rel);
    if (snprintf(path, NC_PATH, "%s/%s", l->root, rel) >= NC_PATH)
        return nc_error(error, "资源路径过长");
    return true;
}
static int note_order(const void *a, const void *b) {
    const Note *x = a, *y = b;
    if (x->time != y->time)
        return x->time < y->time ? -1 : 1;
    if (x->lane != y->lane)
        return x->lane - y->lane;
    return x->id - y->id;
}
static bool chart_sort(Chart *c, char *err) {
    qsort(c->notes, (size_t)c->count, sizeof(Note), note_order);
    int lane_end[4] = {-1, -1, -1, -1};
    for (int i = 0; i < c->count; i++) {
        const Note *n = &c->notes[i];
        if (n->time <= lane_end[n->lane])
            return nc_error(err, "同轨音符重叠");
        lane_end[n->lane] = n->end;
    }
    return true;
}
bool chart_load(const Library *l, int song, int diff, Chart *out, bool for_ranking, char *error) {
    if (song < 0 || song >= l->count || diff < 0 || diff > 2)
        return nc_error(error, "曲目或难度无效");
    const Song *s = &l->songs[song];
    char path[NC_PATH];
    Chart c = {0};
    int schema;
    if (!path_join(l, s->charts[diff], path, error))
        return false;
    cJSON *o = json_read(path, 16 * 1024 * 1024, error);
    if (!o)
        return false;
    bool ok = json_int(o, "schemaVersion", 1, 1, &schema) &&
              json_int(o, "chartVersion", 1, 100000, &c.version) &&
              json_str(o, "chartId", c.id, sizeof(c.id)) &&
              json_str(o, "songId", c.song, sizeof(c.song)) &&
              json_str(o, "difficulty", c.difficulty, sizeof(c.difficulty)) &&
              !strcmp(c.song, s->id) && !strcmp(c.difficulty, nc_difficulties[diff]) &&
              json_int(o, "audioDurationMs", 1, 3600000, &c.duration) &&
              json_int(o, "approachMs", 100, 10000, &c.approach);
    cJSON *notes = cJSON_GetObjectItemCaseSensitive(o, "notes");
    c.count = cJSON_GetArraySize(notes);
    if (!ok || !cJSON_IsArray(notes) || c.count < 1 || c.count > NC_NOTES) {
        nc_error(error, "谱面字段或版本无效：%s", path);
        goto fail;
    }
    c.notes = calloc((size_t)c.count, sizeof(Note));
    if (!c.notes) {
        nc_error(error, "内存不足");
        goto fail;
    }
    const cJSON *v;
    int i = 0;
    cJSON_ArrayForEach(v, notes) {
        Note *n = &c.notes[i++];
        char type[16];
        if (!json_int(v, "id", 1, 100000000, &n->id) || !json_int(v, "lane", 0, 3, &n->lane) ||
            !json_int(v, "timeMs", 0, c.duration - 421, &n->time) ||
            !json_str(v, "type", type, sizeof(type))) {
            nc_error(error, "音符字段缺失或类型错误");
            goto fail;
        }
        if (!strcmp(type, "tap"))
            n->type = TAP;
        else if (!strcmp(type, "hold"))
            n->type = HOLD;
        else if (!strcmp(type, "chain"))
            n->type = CHAIN;
        else {
            nc_error(error, "未知音符类型：%s", type);
            goto fail;
        }
        n->end = n->time;
        if (((n->type == HOLD || cJSON_HasObjectItem(v, "endMs")) &&
             !json_int(v, "endMs", 0, c.duration, &n->end)) ||
            (n->type == HOLD ? n->end <= n->time : n->end != n->time)) {
            nc_error(error, "音符 endMs 无效");
            goto fail;
        }
        int chord = -1;
        if (cJSON_HasObjectItem(v, "chordId") && !json_int(v, "chordId", -1, 100000000, &chord)) {
            nc_error(error, "音符 chordId 无效");
            goto fail;
        }
        n->chord = chord >= 0;
    }
    if (!chart_sort(&c, error))
        goto fail;
    if (for_ranking) {
        if (!nc_hash_file(path, c.hash, error) || !path_join(l, s->audio, path, error))
            goto fail;
        if (!nc_exists(path)) {
            nc_error(error, "找不到音频：%s", path);
            goto fail;
        }
    }
    cJSON_Delete(o);
    chart_free(out);
    *out = c;
    return true;
fail:
    cJSON_Delete(o);
    chart_free(&c);
    return false;
}
bool library_load(Library *l, const char *root, char *error) {
    memset(l, 0, sizeof(*l));
    nc_copy(l->root, sizeof(l->root), root);
    char path[NC_PATH];
    if (!path_join(l, "index.json", path, error))
        return false;
    cJSON *o = json_read(path, 1024 * 1024, error);
    if (!o)
        return false;
    int schema;
    cJSON *songs = cJSON_GetObjectItemCaseSensitive(o, "songs");
    int count = cJSON_GetArraySize(songs);
    bool ok = json_int(o, "schemaVersion", 1, 1, &schema) && cJSON_IsArray(songs) && count > 0 &&
              count <= NC_SONGS;
    for (int i = 0; ok && i < count; i++) {
        cJSON *v = cJSON_GetArrayItem(songs, i);
        Song *s = &l->songs[i];
        ok = json_str(v, "id", s->id, sizeof(s->id)) &&
             json_str(v, "title", s->title, sizeof(s->title)) &&
             json_str(v, "audioPath", s->audio, sizeof(s->audio)) &&
             path_join(l, s->audio, path, error);
        cJSON *charts = cJSON_GetObjectItemCaseSensitive(v, "charts");
        for (int d = 0; ok && d < 3; d++)
            ok = json_str(charts, nc_difficulties[d], s->charts[d], NC_PATH) &&
                 path_join(l, s->charts[d], path, error);
        for (int j = 0; j < i; j++)
            if (!strcmp(l->songs[j].id, s->id))
                ok = false;
    }
    cJSON_Delete(o);
    if (!ok)
        return nc_error(error, "曲目索引版本、字段或路径无效");
    l->count = count;
    return true;
}
