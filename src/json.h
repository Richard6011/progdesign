#ifndef NC_JSON_H
#define NC_JSON_H
#include "cJSON.h"
#include "nc.h"
/* Checked JSON accessors shared by resources, persistence and wire protocol. */
/**
 * @brief 从 JSON 对象读取具有闭区间约束的整数。
 * 返回：类型为数值且有限、无小数、位于 [lo,hi] 时返回 true 并写 out，否则返回 false。
 * 详见：json.c 的同名函数定义（含参数、步骤及边界说明）。
 */
bool json_int(const cJSON *o, const char *key, int min, int max, int *out);
/**
 * @brief 读取非空且可完整装入缓冲区的 JSON 字符串。
 * 返回：字段存在、类型正确且字节长度小于 cap 时返回 true，否则返回 false。
 * 详见：json.c 的同名函数定义（含参数、步骤及边界说明）。
 */
bool json_str(const cJSON *o, const char *key, char *out, size_t capacity);
/**
 * @brief 读取限长文件，并解析为一个完整 JSON 对象。
 * 返回：成功返回新建 cJSON 根对象；读文件、语法或根类型错误时返回 NULL。
 * 详见：json.c 的同名函数定义（含参数、步骤及边界说明）。
 */
cJSON *json_read(const char *path, size_t limit, char *error);
/**
 * @brief 把一个 JSON 对象序列化并原子保存。
 * 返回：序列化与 nc_atomic 均成功返回 true，否则返回 false。
 * 详见：json.c 的同名函数定义（含参数、步骤及边界说明）。
 */
bool json_save(const char *path, const cJSON *o, char *error);
/**
 * @brief 把八项演奏统计转换为 JSON 对象。
 * 返回：返回新建对象指针；调用方取得所有权，需要删除或挂到另一棵 JSON 树上。
 * 详见：json.c 的同名函数定义（含参数、步骤及边界说明）。
 */
cJSON *stats_json(const Stats *s);
#endif
