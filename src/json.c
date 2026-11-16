#include "json.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
/**
 * @brief 从 JSON 对象读取具有闭区间约束的整数。
 * 参数：o 为对象；key 为区分大小写的字段名；lo/hi 为允许范围；out 为非空输出指针。
 * 返回：类型为数值且有限、无小数、位于 [lo,hi] 时返回 true 并写 out，否则返回 false。
 * 说明：检查 double 的取值后才转换为 int，拒绝字符串数字、缺字段及非整数。失败不写
 * out，因此调用方不能在失败后使用未初始化输出。
 */
bool json_int(const cJSON *o, const char *key, int lo, int hi, int *out) {
    cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    if (!cJSON_IsNumber(v) || !isfinite(v->valuedouble) ||
        floor(v->valuedouble) != v->valuedouble || v->valuedouble < lo || v->valuedouble > hi)
        return false;
    *out = (int)v->valuedouble;
    return true;
}
/**
 * @brief 读取非空且可完整装入缓冲区的 JSON 字符串。
 * 参数：o 为对象；key 为字段名；out 为输出缓冲区；cap 为含结尾零的字节容量。
 * 返回：字段存在、类型正确且字节长度小于 cap 时返回 true，否则返回 false。
 * 说明：使用大小写敏感查找，不接受空字符串或截断结果。这里不检查 UTF-8
 * 字符语义；网络帧和账号模块在各自边界执行进一步验证。
 */
bool json_str(const cJSON *o, const char *key, char *out, size_t cap) {
    cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    if (!cJSON_IsString(v) || !v->valuestring || !v->valuestring[0] ||
        strlen(v->valuestring) >= cap)
        return false;
    nc_copy(out, cap, v->valuestring);
    return true;
}
/**
 * @brief 读取限长文件，并解析为一个完整 JSON 对象。
 * 参数：path 为 UTF-8 路径；limit 为最大文件字节数；error 可为空。
 * 返回：成功返回新建 cJSON 根对象；读文件、语法或根类型错误时返回 NULL。
 * 说明：文本缓冲区在解析后立即释放；返回树由调用者 cJSON_Delete
 * 释放。启用完整文本终止检查，避免把对象后额外拼接的数据当成合法文件。
 */
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
/**
 * @brief 把一个 JSON 对象序列化并原子保存。
 * 参数：path 为目标 UTF-8 路径；o 为借用的 JSON 树；error 可为空。
 * 返回：序列化与 nc_atomic 均成功返回 true，否则返回 false。
 * 说明：使用带缩进输出便于人工检查，内部释放序列化字符串，但不删除传入的树。该函数不创建父目录，调用方在需要时先调用
 * nc_mkdir。
 */
bool json_save(const char *path, const cJSON *o, char *error) {
    char *text = cJSON_Print(o);
    if (!text)
        return nc_error(error, "JSON 内存不足");
    bool ok = nc_atomic(path, text, strlen(text), error);
    cJSON_free(text);
    return ok;
}
/**
 * @brief 把八项演奏统计转换为 JSON 对象。
 * 参数：s 为借用的 Stats，预期已由调用方确认有效。
 * 返回：返回新建对象指针；调用方取得所有权，需要删除或挂到另一棵 JSON 树上。
 * 说明：字段使用协议/存储共用名称，包含四类数量、combo、maxCombo、finalized、units；此处只转换，不
 * 重新校验或计算最终分数。
 */
cJSON *stats_json(const Stats *s) {
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "perfect", s->perfect);
    cJSON_AddNumberToObject(o, "great", s->great);
    cJSON_AddNumberToObject(o, "good", s->good);
    cJSON_AddNumberToObject(o, "miss", s->miss);
    cJSON_AddNumberToObject(o, "combo", s->combo);
    cJSON_AddNumberToObject(o, "maxCombo", s->max_combo);
    cJSON_AddNumberToObject(o, "finalized", s->finalized);
    cJSON_AddNumberToObject(o, "units", (double)s->units);
    return o;
}
/**
 * @brief 从 JSON 读出统计并执行数值与关系检查。
 * 参数：o 为统计对象；s 为输出；n 为已限制在 1—NC_NOTES 的音符总数；complete 指定是否完整。
 * 返回：所有字段合法且 stats_valid 通过返回 true，否则返回 false。
 * 说明：先清零 s，再逐项读取；失败时 s 可能是部分已写字段，不能视为有效统计。units 先按 n*1000 限定为
 * int，再赋给 int64_t，避免接受伪造的超范围权重。
 */
bool stats_parse(const cJSON *o, Stats *s, int n, bool complete) {
    int units;
    memset(s, 0, sizeof(*s));
    if (!json_int(o, "perfect", 0, n, &s->perfect) || !json_int(o, "great", 0, n, &s->great) ||
        !json_int(o, "good", 0, n, &s->good) || !json_int(o, "miss", 0, n, &s->miss) ||
        !json_int(o, "combo", 0, n, &s->combo) || !json_int(o, "maxCombo", 0, n, &s->max_combo) ||
        !json_int(o, "finalized", 0, n, &s->finalized) ||
        !json_int(o, "units", 0, n * 1000, &units))
        return false;
    s->units = units;
    return stats_valid(s, n, complete);
}
