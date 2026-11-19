#ifndef NC_H
#define NC_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define NC_NOTES 20000
#define NC_SONGS 64
#define NC_PATH 1024
#define NC_ERROR 512
#define NC_RULES 2
#define NC_PROTOCOL 5
typedef enum { TAP, HOLD, CHAIN } NoteType;
typedef enum { PENDING, HOLDING, HIT, MISSED } NoteState;
typedef enum { PERFECT, GREAT, GOOD, MISS } Judgement;
typedef enum { IDLE, PLAYING, PAUSED, RESUME_READY, FINISHED } GameState;
typedef struct {
    int id, lane, time, end;
    NoteType type;
    bool chord;
} Note;
typedef struct {
    NoteState state;
    Judgement head;
} Runtime;
typedef struct {
    char id[128], song[128], difficulty[16], hash[65];
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
    int hits[4], combo, max_combo, finalized;
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
    const char *session, *name, *date, *match, *opponent;
    const Chart *chart;
    const Stats *stats;
    int opponent_score;
} Record;
typedef struct {
    const char *name, *date;
    Result result;
} RankRow;
typedef struct {
    struct cJSON *file, *records;
    int count;
    char root[NC_PATH];
} Store;
extern const char *nc_difficulties[3];
bool nc_error(char *error, const char *fmt, ...);
void nc_copy(char *out, size_t cap, const char *text);
int64_t nc_now(void);
void nc_uuid(char out[65]);
bool nc_read(const char *path, char **out, size_t *size, size_t limit, char *error);
bool nc_atomic(const char *path, const char *data, size_t size, char *error);
bool nc_exists(const char *path);
bool nc_mkdir(const char *path, char *error);
void nc_exe_dir(char out[NC_PATH]);
void nc_data_dir(const char *exe, char out[NC_PATH]);
bool nc_hash_file(const char *path, char out[65], char *error);
bool nc_hash(const void *data, size_t len, char out[65]);
void chart_free(Chart *chart);
bool library_load(Library *lib, const char *root, char *error);
bool chart_load(const Library *lib, int song, int difficulty, Chart *chart, bool for_ranking,
                char *error);
bool engine_start(Engine *engine, const Chart *chart, int offset);
void engine_free(Engine *engine);
void engine_tick(Engine *engine, int time);
bool engine_key(Engine *engine, int lane, int time, bool pressed);
void engine_pause(Engine *engine);
bool engine_resume(Engine *engine);
void stats_apply(Stats *stats, Judgement judgement);
Result stats_result(const Stats *stats, int count);
bool account_action(const char *root, const char *name, const char *password, bool registration,
                    char *error);
bool settings_load(const char *root, Settings *settings, char *error);
bool store_load(Store *store, const char *root, char *error);
void store_free(Store *store);
bool store_save(Store *store, const Record *record, char *error);
int store_ranking(const Store *store, const Chart *chart, int mode, RankRow *rows);
bool audio_open(const char *path, int volume, int duration, char *error);
bool audio_play(char *error);
bool audio_pause(char *error);
bool audio_resume(char *error);
int audio_position(void);
void audio_close(void);
#endif
