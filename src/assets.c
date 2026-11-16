#include "json.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/**
 * @brief 释放谱面拥有的音符数组，并清空谱面结构。
 * 参数：c 为已清零初始化或由 chart_load 填充的谱面指针。
 * 返回：无返回值；c 变回空谱面。
 * 说明：只释放 c->notes，不释放 c 本身。外部 Engine
 * 持有对该谱面的借用指针，必须先停止并释放引擎，再释放或替换谱面。
 */
void chart_free(Chart *c) {
    free(c->notes);
    memset(c, 0, sizeof(*c));
}
/**
 * @brief 检查清单中的资源相对路径，再拼接资源根目录。
 * 参数：l 提供 root；rel 为清单中的相对路径；path 至少容纳 NC_PATH 字节；error 可为空。
 * 返回：合法且拼接未截断时返回 true，否则返回 false 并填写错误原因。
 * 说明：拒绝空路径、以斜杠开头的路径、含 .. 或冒号的文本。该函数只做字符串检查，不打开文件，也不解析目录联接或符号链接。
 */
static bool path_join(const Library *l, const char *rel, char *path, char *error) {
    /* No absolute paths, traversal or alternate data streams in resource manifests. */
    if (!rel[0] || rel[0] == '/' || rel[0] == '\\' || strstr(rel, "..") || strchr(rel, ':'))
        return nc_error(error, "资源路径非法：%s", rel);
    if (snprintf(path, NC_PATH, "%s/%s", l->root, rel) >= NC_PATH)
        return nc_error(error, "资源路径过长");
    return true;
}
/**
 * @brief 为谱面排序提供稳定的比较规则。
 * 参数：a、b 分别指向 qsort 传入的两个 Note，ID 应先满足谱面规定的数值范围。
 * 返回：按击打时刻、轨道、ID 依次升序比较，返回负数、零或正数。
 * 说明：相同时间的多轨音符仍作为独立判定单元；排序只改变处理次序，不合并音符。
 */
static int note_order(const void *a, const void *b) {
    const Note *x = a, *y = b;
    if (x->time != y->time)
        return x->time < y->time ? -1 : 1;
    if (x->lane != y->lane)
        return x->lane - y->lane;
    return x->id - y->id;
}
/**
 * @brief 校验谱面内部约束，并按游戏处理顺序原地排序。
 * 参数：c 提供已分配的 notes、count、duration 和 approach；err 可为空。
 * 返回：全部约束满足时返回 true；首次发现错误时返回 false 并写入原因。
 * 说明：限制 1—20000 个音符、最多一小时、100—10000ms 接近时间；检查
 * ID、轨道、尾部、同轨占用和分组。音符头部需为最大判定窗口及偏移预留 421ms；失败前可能已改变数组排序。
 */
bool chart_validate(Chart *c, char *err) {
    if (c->count < 1 || c->count > NC_NOTES || c->duration < 1 || c->duration > 3600000 ||
        c->approach < 100 || c->approach > 10000)
        return nc_error(err, "谱面数量、时长或接近时间非法");
    qsort(c->notes, (size_t)c->count, sizeof(Note), note_order);
    for (int i = 0; i < c->count; i++) {
        Note *n = &c->notes[i];
        if (n->id <= 0 || n->lane < 0 || n->lane > 3 || n->time < 0 ||
            n->time > c->duration - 421 || n->end > c->duration ||
            (n->type == HOLD ? n->end <= n->time : n->end != n->time))
            return nc_error(err, "音符 %d 的轨道、时刻或长度无效", n->id);
        for (int j = 0; j < i; j++) {
            Note *p = &c->notes[j];
            if (p->id == n->id)
                return nc_error(err, "音符 id 重复");
            if (p->lane == n->lane &&
                (p->time == n->time || (p->type == HOLD && p->end >= n->time)))
                return nc_error(err, "同轨音符重叠");
        }
        if (n->type == CHAIN && n->chain < 0)
            return nc_error(err, "连锁节点缺少 chainId");
        if (n->chain >= 0) {
            int peers = 0;
            for (int j = 0; j < c->count; j++)
                if (c->notes[j].chain == n->chain) {
                    if (c->notes[j].type != CHAIN)
                        return nc_error(err, "连锁分组含非连锁音符");
                    peers++;
                }
            if (peers < 2)
                return nc_error(err, "连锁分组至少需要两个节点");
        }
        if (n->chord >= 0) {
            int peers = 0;
            for (int j = 0; j < c->count; j++)
                if (c->notes[j].chord == n->chord) {
                    if (c->notes[j].time != n->time)
                        return nc_error(err, "同时按键分组时刻不一致");
                    peers++;
                }
            if (peers < 2)
                return nc_error(err, "同时按键分组至少需要两个音符");
        }
    }
    return true;
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
 * @brief 核对 PCM WAV 数据块及其实际播放时长。
 * 参数：path 为 UTF-8 文件路径；duration 为谱面声明的毫秒时长；error 可为空。
 * 返回：头部、块边界和时长符合要求返回 true；读取或校验失败返回 false。
 * 说明：最多读取 128MiB，检查 RIFF/WAVE、PCM fmt 和 data，按块长度及奇数字节填充跳转。使用
 * data 字节数/字节率计算时长，允许 1ms 误差；所有出口释放文件缓冲区。
 */
static bool wav_validate(const char *path, int duration, char *error) {
    char *bytes;
    size_t size;
    if (!nc_read(path, &bytes, &size, 128 * 1024 * 1024, error))
        return false;
    unsigned char *p = (unsigned char *)bytes;
    uint32_t rate = 0, data = 0;
    bool ok = false;
    if (size < 12 || memcmp(p, "RIFF", 4) || memcmp(p + 8, "WAVE", 4))
        goto end;
    for (size_t pos = 12; pos + 8 <= size;) {
        uint32_t n = le32(p + pos + 4);
        if (n > size - pos - 8)
            goto end;
        if (!memcmp(p + pos, "fmt ", 4) && n >= 16) {
            if (p[pos + 8] != 1 || p[pos + 9] != 0)
                goto end; /* PCM only, as shipped */
            rate = le32(p + pos + 16);
        }
        if (!memcmp(p + pos, "data", 4))
            data = n;
        pos += 8 + (size_t)n + (n & 1);
    }
    ok = rate && data && llabs((int64_t)data * 1000 / rate - duration) <= 1;
end:
    free(bytes);
    return ok || nc_error(error, "WAV 格式损坏或时长与谱面不一致：%s", path);
}
/**
 * @brief 从选定曲目和难度的 JSON 载入一份完整谱面。
 * 参数：l 为有效曲库；song 为歌曲下标；diff 为 0/1/2；out 必须为清零初始化或有效旧谱面；error
 * 可为空。
 * 返回：成功返回 true 并替换 out；失败返回 false，out 保持原内容。
 * 说明：先在局部 Chart 中解析和分配，检查身份、音符、WAV 时长，再计算谱面与音频的文件
 * SHA-256。仅全部成功后释放旧 out 并转移 notes 所有权，失败释放临时资源。
 */
bool chart_load(const Library *l, int song, int diff, Chart *out, char *error) {
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
    for (int i = 0; i < c.count; i++) {
        cJSON *v = cJSON_GetArrayItem(notes, i);
        Note *n = &c.notes[i];
        char type[16];
        n->chain = n->chord = -1;
        if (!json_int(v, "id", 1, 100000000, &n->id) || !json_int(v, "lane", 0, 3, &n->lane) ||
            !json_int(v, "timeMs", 0, 3600000, &n->time) ||
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
        if ((n->type == HOLD || cJSON_HasObjectItem(v, "endMs")) &&
            !json_int(v, "endMs", 0, 3600000, &n->end)) {
            nc_error(error, "音符 endMs 无效");
            goto fail;
        }
        if ((cJSON_HasObjectItem(v, "chainId") &&
             !json_int(v, "chainId", -1, 100000000, &n->chain)) ||
            (cJSON_HasObjectItem(v, "chordId") &&
             !json_int(v, "chordId", -1, 100000000, &n->chord))) {
            nc_error(error, "音符分组无效");
            goto fail;
        }
    }
    if (!chart_validate(&c, error) || !nc_hash_file(path, c.hash, error) ||
        !path_join(l, s->audio, path, error) || !wav_validate(path, c.duration, error) ||
        !nc_hash_file(path, c.audio_hash, error))
        goto fail;
    cJSON_Delete(o);
    chart_free(out);
    *out = c;
    return true;
fail:
    cJSON_Delete(o);
    chart_free(&c);
    return false;
}
/**
 * @brief 载入歌曲索引，并预检每首歌曲的三个难度。
 * 参数：l 为输出曲库；root 是独立输入字符串，不能指向 l 内部待清空的字段；error 可为空。
 * 返回：索引及所有可选谱面均有效返回 true，否则返回 false。
 * 说明：先清空 l，限制歌曲数量为 NC_SONGS 并拒绝重复歌曲 ID；逐档调用 chart_load
 * 后立即释放临时谱面。失败时 l 可能已有部分字段，调用方不能将其视为有效曲库继续使用。
 */
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
             json_str(v, "artist", s->artist, sizeof(s->artist)) &&
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
    /* Validate every selectable chart at startup rather than fail half way through a room. */
    for (int i = 0; i < count; i++)
        for (int d = 0; d < 3; d++) {
            Chart c = {0};
            if (!chart_load(l, i, d, &c, error))
                return false;
            chart_free(&c);
        }
    return true;
}
