#ifndef NC_H
#define NC_H
/* 业务模块使用 C17；只有 qt_bridge.cpp 通过薄 C++ 接口提供 Qt 窗口和绘图。 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define NC_NOTES 20000
#define NC_SONGS 64
#define NC_PATH 1024
#define NC_ERROR 512
#define NC_RULES 2
#define NC_PROTOCOL 3
typedef enum { TAP, HOLD, CHAIN } NoteType;
typedef enum { PENDING, HOLDING, HIT, MISSED } NoteState;
typedef enum { PERFECT, GREAT, GOOD, MISS } Judgement;
typedef enum { IDLE, PLAYING, PAUSED, RESUME_READY, FINISHED } GameState;
typedef struct {
    int id, lane, time, end;
    NoteType type;
    bool chord; /* 是否属于同时按键分组，用于绘制标记。 */
} Note;
typedef struct {
    NoteState state;
    Judgement head;
} Runtime;
typedef struct {
    char id[128], song[128], difficulty[16], hash[65], audio_hash[65];
    int version, duration, approach, count;
    Note *notes;
} Chart;
typedef struct {
    char id[128], title[256], audio[NC_PATH], charts[3][NC_PATH];
} Song;
typedef struct {
    Song songs[NC_SONGS];
    int count;
    char root[NC_PATH];
} Library;
typedef struct {
    int perfect, great, good, miss, combo, max_combo, finalized;
    int64_t units;
} Stats;
typedef struct {
    int score;
    double accuracy;
    char grade;
    bool complete, fc, ap;
} Result;
typedef struct {
    const Chart *chart;
    Runtime *runtime;
    Stats stats;
    GameState state;
    bool down[4];
    int now, offset;
    Judgement last;
    int last_time;
} Engine;
typedef struct {
    int music, offset;
} Settings;
typedef struct {
    char session[65], match[65], name[128], opponent[128], chart[128], hash[65], difficulty[16],
        date[32];
    int mode, rules, version, count, opponent_score;
    Stats stats;
} Record;
typedef struct {
    Record *items;
    int count;
    bool writable;
    char root[NC_PATH];
} Store;
extern const char *nc_difficulties[3];
/**
 * @brief 格式化一条错误消息，并统一返回失败。
 * 返回：始终返回 false，便于在条件分支中直接 return nc_error(...)。
 * 详见：platform.c 的同名函数定义（含参数、步骤及边界说明）。
 */
bool nc_error(char *error, const char *fmt, ...);
/**
 * @brief 将可空字符串以零结尾形式复制到定长缓冲区。
 * 返回：无返回值；cap 大于零时按容量保留结尾零，超长文本被截断。
 * 详见：platform.c 的同名函数定义（含参数、步骤及边界说明）。
 */
void nc_copy(char *out, size_t cap, const char *text);
/**
 * @brief 获取用于计划开局和超时的单调毫秒时钟。
 * 返回：返回 QueryPerformanceCounter 换算的 int64_t 毫秒值；该值不是日期或 Unix 时间。
 * 详见：platform.c 的同名函数定义（含参数、步骤及边界说明）。
 */
int64_t nc_now(void);
/**
 * @brief 生成会话、对局及临时文件使用的标识文本。
 * 返回：无返回值；输出由调用方持有。
 * 详见：platform.c 的同名函数定义（含参数、步骤及边界说明）。
 */
void nc_uuid(char out[65]);
/**
 * @brief 限长读取整个文件，并额外补上文本结尾零。
 * 返回：成功返回 true，*out 为 malloc 内存；失败返回 false，*out 为 NULL。
 * 详见：platform.c 的同名函数定义（含参数、步骤及边界说明）。
 */
bool nc_read(const char *path, char **out, size_t *size, size_t limit, char *error);
/**
 * @brief 用同目录临时文件提交一份新文件内容。
 * 返回：完整写入、刷新并替换成功返回 true，否则返回 false。
 * 详见：platform.c 的同名函数定义（含参数、步骤及边界说明）。
 */
bool nc_atomic(const char *path, const char *data, size_t size, char *error);
/**
 * @brief 检查 Windows 能否取得路径的文件属性。
 * 返回：GetFileAttributesW 成功返回 true，否则返回 false。
 * 详见：platform.c 的同名函数定义（含参数、步骤及边界说明）。
 */
bool nc_exists(const char *path);
/**
 * @brief 创建目标目录及需要的父目录。
 * 返回：创建成功或系统报告路径已存在时返回 true，否则返回 false。
 * 详见：platform.c 的同名函数定义（含参数、步骤及边界说明）。
 */
bool nc_mkdir(const char *path, char *error);
/**
 * @brief 取得当前可执行文件所在目录。
 * 返回：无返回值，输出 UTF-8 目录路径，不含最后的可执行文件名。
 * 详见：platform.c 的同名函数定义（含参数、步骤及边界说明）。
 */
void nc_exe_dir(char out[NC_PATH]);
/**
 * @brief 构造当前用户的独立 C 版数据目录。
 * 返回：无返回值，正常情况下输出 AppData/Roaming 下的 NoteCollisionC 路径。
 * 详见：platform.c 的同名函数定义（含参数、步骤及边界说明）。
 */
void nc_data_dir(char out[NC_PATH]);
/**
 * @brief 以流式方式计算文件内容的 SHA-256。
 * 返回：成功返回 true；打开、读取或哈希失败返回 false 并写原因。
 * 详见：platform.c 的同名函数定义（含参数、步骤及边界说明）。
 */
bool nc_hash_file(const char *path, char out[65], char *error);
/**
 * @brief 计算一段内存的 SHA-256 文本摘要。
 * 返回：成功返回 true 并填写摘要；失败返回 false，输出不能继续使用。
 * 详见：platform.c 的同名函数定义（含参数、步骤及边界说明）。
 */
bool nc_hash(const void *data, size_t len, char out[65]);
/**
 * @brief 释放谱面拥有的音符数组，并清空谱面结构。
 * 返回：无返回值；c 变回空谱面。
 * 详见：assets.c 的同名函数定义（含参数、步骤及边界说明）。
 */
void chart_free(Chart *chart);
/**
 * @brief 载入歌曲索引，记录各难度的谱面路径。
 * 返回：索引有效返回 true，否则返回 false。
 * 详见：assets.c 的同名函数定义（含参数、步骤及边界说明）。
 */
bool library_load(Library *lib, const char *root, char *error);
/**
 * @brief 从选定曲目和难度的 JSON 载入一份完整谱面。
 * 返回：成功返回 true 并替换 out；失败返回 false，out 保持原内容。
 * 详见：assets.c 的同名函数定义（含参数、步骤及边界说明）。
 */
bool chart_load(const Library *lib, int song, int difficulty, Chart *chart, char *error);
/**
 * @brief 为借用的谱面创建本局运行状态。
 * 返回：成功分配 Runtime 返回 true，并置 PLAYING；前置参数非法或分配失败返回 false。
 * 详见：engine.c 的同名函数定义（含参数、步骤及边界说明）。
 */
bool engine_start(Engine *engine, const Chart *chart, int offset);
/**
 * @brief 释放引擎运行数组，并恢复初始状态。
 * 返回：无返回值；e 清零后状态为 IDLE。
 * 详见：engine.c 的同名函数定义（含参数、步骤及边界说明）。
 */
void engine_free(Engine *engine);
/**
 * @brief 推进音乐时间，并结算漏击头部与已到尾的长按。
 * 返回：无返回值；非 PLAYING 或时间倒退时直接忽略。
 * 详见：engine.c 的同名函数定义（含参数、步骤及边界说明）。
 */
void engine_tick(Engine *engine, int time);
/**
 * @brief 处理一条轨道从松开到按下的输入。
 * 返回：命中候选音符或登记恢复阶段按键时返回 true；无候选、重复按住、非法轨道/状态/倒退时返回 false。
 * 详见：engine.c 的同名函数定义（含参数、步骤及边界说明）。
 */
bool engine_press(Engine *engine, int lane, int time);
/**
 * @brief 处理轨道松开，并将提前放开的活动长按记为 Miss。
 * 返回：无返回值；非法轨道、非运行状态或倒退输入被忽略。
 * 详见：engine.c 的同名函数定义（含参数、步骤及边界说明）。
 */
void engine_release(Engine *engine, int lane, int time);
/**
 * @brief 冻结可暂停的引擎状态。
 * 返回：无返回值；合法状态变为 PAUSED，并清空四轨物理按住状态。
 * 详见：engine.c 的同名函数定义（含参数、步骤及边界说明）。
 */
void engine_pause(Engine *engine);
/**
 * @brief 仅在活动长按都已重新按住时恢复判定。
 * 返回：所有活动长按的轨道都按住时返回 true 并置 PLAYING，否则返回 false。
 * 详见：engine.c 的同名函数定义（含参数、步骤及边界说明）。
 */
bool engine_resume(Engine *engine);
/**
 * @brief 将一个音符的最终判定累加到统计。
 * 返回：无返回值；更新判定计数、权重和、当前连击、最大连击及完成数量。
 * 详见：engine.c 的同名函数定义（含参数、步骤及边界说明）。
 */
void stats_apply(Stats *stats, Judgement judgement);
/**
 * @brief 检查统计字段范围、计数和分值之间是否自洽。
 * 返回：所有检查通过返回 true，否则返回 false；不修改输入。
 * 详见：engine.c 的同名函数定义（含参数、步骤及边界说明）。
 */
bool stats_valid(const Stats *stats, int count, bool complete);
/**
 * @brief 根据固定音符总数计算标准化成绩。
 * 返回：返回 Result；没有可用谱面时为零分、D 级且不完整。
 * 详见：engine.c 的同名函数定义（含参数、步骤及边界说明）。
 */
Result stats_result(const Stats *stats, int count);
/**
 * @brief 注册本机账号，或校验已有账号密码。
 * 返回：注册落盘或登录验证成功返回 true，输入、文件或认证失败返回 false。
 * 详见：storage.c 的同名函数定义（含参数、步骤及边界说明）。
 */
bool account_action(const char *root, const char *name, const char *password, bool registration,
                    char *error);
/**
 * @brief 读取设置，出错时保留默认配置供程序启动。
 * 返回：文件不存在或已成功读取时返回 true；文件损坏、未知版本或字段非法返回 false。
 * 详见：storage.c 的同名函数定义（含参数、步骤及边界说明）。
 */
bool settings_load(const char *root, Settings *settings, char *error);
/**
 * @brief 把本地完整成绩文件装入内存仓库。
 * 返回：不存在文件时以空可写仓库返回 true；合法文件返回 true；读入或验证失败返回 false。
 * 详见：storage.c 的同名函数定义（含参数、步骤及边界说明）。
 */
bool store_load(Store *store, const char *root, char *error);
/**
 * @brief 释放成绩列表并清空仓库状态。
 * 返回：无返回值；释放 items 后清零整个结构。
 * 详见：storage.c 的同名函数定义（含参数、步骤及边界说明）。
 */
void store_free(Store *store);
/**
 * @brief 幂等地追加一条完整成绩，并原子提交整个列表。
 * 返回：新记录落盘成功或已存在相同会话/联机玩家记录时返回 true，否则返回 false。
 * 详见：storage.c 的同名函数定义（含参数、步骤及边界说明）。
 */
bool store_save(Store *store, const Record *record, char *error);
/**
 * @brief 查询某模式下与当前谱面完全匹配的账号最佳成绩榜。
 * 返回：返回写入 out 的数量，最大 cap；无记录或临时分配失败均返回 0。
 * 详见：storage.c 的同名函数定义（含参数、步骤及边界说明）。
 */
int store_ranking(const Store *store, const Chart *chart, int mode, const Record **out,
                  int capacity);
/**
 * @brief 预加载歌曲，为随后的计划起播准备音频设备。
 * 返回：设备打开且报告时长在谱面时长至谱面时长加 100ms 之间时返回 true，否则返回 false。
 * 详见：audio.c 的同名函数定义（含参数、步骤及边界说明）。
 */
bool audio_open(const char *path, int volume, int duration, char *error);
/**
 * @brief 从音乐零毫秒处开始播放。
 * 返回：设备已打开且 play 命令成功返回 true，否则返回 false。
 * 详见：audio.c 的同名函数定义（含参数、步骤及边界说明）。
 */
bool audio_play(char *error);
/**
 * @brief 暂停当前音乐，保留播放位置。
 * 返回：设备已打开且暂停命令成功返回 true，否则返回 false。
 * 详见：audio.c 的同名函数定义（含参数、步骤及边界说明）。
 */
bool audio_pause(char *error);
/**
 * @brief 继续已暂停的音乐设备。
 * 返回：设备已打开且 resume 命令成功返回 true，否则返回 false。
 * 详见：audio.c 的同名函数定义（含参数、步骤及边界说明）。
 */
bool audio_resume(char *error);
/**
 * @brief 读取 MCI 报告的当前音乐位置。
 * 返回：成功返回非负毫秒位置；设备未打开或查询失败返回 -1。
 * 详见：audio.c 的同名函数定义（含参数、步骤及边界说明）。
 */
int audio_position(void);
/**
 * @brief 关闭当前音乐设备并清除已打开标志。
 * 返回：无返回值。
 * 详见：audio.c 的同名函数定义（含参数、步骤及边界说明）。
 */
void audio_close(void);
#endif
