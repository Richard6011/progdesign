#ifndef NC_JSON_H
#define NC_JSON_H
#include "cJSON.h"
#include "nc.h"
bool json_int(const cJSON *o, const char *key, int min, int max, int *out);
bool json_str(const cJSON *o, const char *key, char *out, size_t capacity);
cJSON *json_read(const char *path, size_t limit, char *error);
bool json_save(const char *path, const cJSON *o, char *error);
cJSON *stats_json(const Stats *s);
#endif
