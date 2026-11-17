#ifndef SIMPLE_GAME_H /* 防止头文件重复包含。 */
#define SIMPLE_GAME_H /* 定义头文件保护标记。 */

#define MAX_NOTES 2000 /* 一份谱面最多保存两千个音符。 */
#define MAX_ACCOUNTS 100 /* 本地最多保存一百个账号。 */
#define MAX_RECORDS 200 /* 最多保存二百条账号最佳成绩。 */
#define NAME_SIZE 65 /* 为账号预留 UTF-8 字符和结束符空间。 */
#define PASSWORD_SIZE 257 /* 为密码预留 UTF-8 字符和结束符空间。 */
#define MESSAGE_SIZE 256 /* 错误提示缓冲区大小。 */
#define PERFECT_WINDOW_MS 150 /* 完美判定允许提前或晚一百五十毫秒。 */
#define GREAT_WINDOW_MS 250 /* 优秀判定允许提前或晚二百五十毫秒。 */
#define GOOD_WINDOW_MS 350 /* 良好判定允许提前或晚三百五十毫秒。 */

enum NoteType { NOTE_TAP, NOTE_HOLD, NOTE_CHAIN }; /* 三种音符类型，同时按键以分组表达。 */
enum NoteState { NOTE_WAIT, NOTE_HOLDING, NOTE_HIT, NOTE_MISS }; /* 音符运行状态。 */
enum Grade { PERFECT, GREAT, GOOD, MISS }; /* 按从好到差排列的判定等级。 */
enum GameState { GAME_READY, GAME_PLAYING, GAME_PAUSED }; /* 游戏是否已经开始或暂停。 */

typedef struct /* 保存一个音符的固定信息及运行状态。 */
{ /* 音符结构开始。 */
    int id; /* 谱面中的唯一编号。 */
    int lane; /* 轨道编号，取零到三。 */
    int time; /* 音符头部目标时刻，单位为毫秒。 */
    int end; /* 长按尾部时刻，普通音符等于头部时刻。 */
    int type; /* 单点、长按或连续节点。 */
    int chain; /* 连续节点的分组编号，无分组时为负一。 */
    int chord; /* 同时按键的分组编号，无分组时为负一。 */
    int state; /* 当前等待、保持、命中或漏击状态。 */
    int headGrade; /* 长按头部命中时保存的评级。 */
} Note; /* 音符结构结束。 */

typedef struct /* 用固定数组保存一份谱面，避免动态内存。 */
{ /* 谱面结构开始。 */
    int count; /* 音符数量。 */
    int duration; /* 音乐总时长，单位为毫秒。 */
    int approach; /* 音符从出现到判定线的移动时长。 */
    Note notes[MAX_NOTES]; /* 保存本谱面的全部音符。 */
} Chart; /* 谱面结构结束。 */

typedef struct /* 保存本局判定和连击统计。 */
{ /* 统计结构开始。 */
    int perfect; /* 完美判定数量。 */
    int great; /* 优秀判定数量。 */
    int good; /* 良好判定数量。 */
    int miss; /* 漏击数量。 */
    int combo; /* 当前连击数量。 */
    int maxCombo; /* 本局最大连击数量。 */
    int finished; /* 已经最终结算的音符数量。 */
    int units; /* 按一千、七百、四百、零累加的权重。 */
} Statistics; /* 统计结构结束。 */

typedef struct /* 保存显示和排行榜使用的结果。 */
{ /* 结果结构开始。 */
    int score; /* 满分为一百万分的标准分。 */
    double accuracy; /* 零到一百之间的准确率百分数。 */
    char grade; /* S、A、B、C、D 中的总体评级。 */
    int complete; /* 是否已经完成全部音符判定。 */
} Result; /* 结果结构结束。 */

typedef struct /* 保存一局游戏，界面只读取这个结构进行绘制。 */
{ /* 游戏结构开始。 */
    Chart chart; /* 本局拥有的谱面副本。 */
    Statistics stats; /* 本局判定统计。 */
    int state; /* 是否正在演奏或暂停。 */
    int now; /* 当前音乐位置，单位为毫秒。 */
    int offset; /* 输入时间校准，范围为负二百到二百。 */
    int held[4]; /* 四条轨道是否正在按住。 */
    int lastGrade; /* 最近一次最终判定，供界面显示。 */
    int feedbackTime; /* 最近判定发生时的音乐位置。 */
} Game; /* 游戏结构结束。 */

typedef struct /* 保存一个账号，只保存盐和密码摘要。 */
{ /* 账号结构开始。 */
    char name[NAME_SIZE]; /* 账号名，最多十六个 UTF-16 代码单元。 */
    char salt[33]; /* 十六字节随机盐的十六进制文本。 */
    char hash[65]; /* SHA-256 密码摘要的十六进制文本。 */
} Account; /* 账号结构结束。 */

typedef struct /* 每个账号在同一首歌和难度下保留一条最佳成绩。 */
{ /* 成绩结构开始。 */
    char name[NAME_SIZE]; /* 此成绩所属的登录账号。 */
    int song; /* 歌曲下标，零为小星星，一为霓虹脉冲。 */
    int difficulty; /* 难度下标，零到二分别为简单、普通、困难。 */
    int noteCount; /* 谱面总音符数。 */
    Statistics stats; /* 原始判定数量，用于重新计算得分。 */
    char date[32]; /* 本机完成时间文本。 */
} Record; /* 成绩结构结束。 */

typedef struct /* 保存简单的音量与输入校准设置。 */
{ /* 设置结构开始。 */
    int volume; /* 音乐音量，零到一百。 */
    int offset; /* 输入校准偏移，单位为毫秒。 */
} Settings; /* 设置结构结束。 */

void stats_add(Statistics *stats, int grade); /* 累计一次最终判定，不重复统计长按头部。 */
Result stats_result(const Statistics *stats, int count); /* 根据固定总判定量计算得分和评级。 */
int stats_valid(const Statistics *stats, int count); /* 核对完整成绩的数量和权重是否合法。 */
void game_start(Game *game, const Chart *chart, int offset); /* 复制谱面并重置本局状态。 */
void game_update(Game *game, int time); /* 按音乐位置处理长按尾部和超时漏击。 */
int game_press(Game *game, int lane, int time); /* 处理一次轨道按下，返回是否命中。 */
void game_release(Game *game, int lane, int time); /* 处理轨道松开，过早松开长按记漏击。 */
void game_pause(Game *game); /* 清除物理按键状态并冻结判定。 */
int game_resume(Game *game); /* 活动长按全部重新按住后才能恢复。 */

int chart_load(int song, int difficulty, Chart *chart, char *error); /* 从简单文本文件读取谱面。 */
const char *song_title(int song); /* 返回歌曲中文名称。 */
const char *difficulty_title(int difficulty); /* 返回难度中文名称。 */
const char *audio_path(int song); /* 返回歌曲音频的相对路径。 */

int text_valid(const char *text, int maximum); /* 检查 UTF-8、字符数量与控制字符。 */
int password_hash(const char *salt, const char *password, char hash[65]); /* 计算盐与密码的摘要。 */
int make_salt(char salt[33]); /* 使用系统随机源生成十六字节随机盐。 */
void storage_folder(const char *folder); /* 指定当前程序使用的本地存档目录。 */
int account_register(const char *name, const char *password, char *error); /* 注册并保存一个新账号。 */
int account_login(const char *name, const char *password, char *error); /* 验证账号及密码，不修改文件。 */
int records_load(Record records[MAX_RECORDS], char *error); /* 返回成绩数量，出错时返回负一。 */
int record_save(const Record *record, char *error); /* 仅保存合法完整成绩，每账号保留最佳。 */
int ranking_make(const Record *records, int count, int song, int difficulty, int order[MAX_RECORDS]); /* 生成降序下标表。 */
int settings_load(Settings *settings, char *error); /* 读取设置，不存在时使用默认值。 */
int settings_save(const Settings *settings, char *error); /* 校验后保存设置文件。 */

int music_open(int song, int volume, char *error); /* 打开已选歌曲并设置音量，不立即播放。 */
int music_play(char *error); /* 从音乐开头播放。 */
int music_pause(char *error); /* 暂停并保留播放位置。 */
int music_resume(char *error); /* 从暂停位置继续播放。 */
int music_position(void); /* 返回当前音乐毫秒位置，失败时返回负一。 */
int music_stopped(void); /* 检查当前媒体是否停止，辅助识别播放中断。 */
void music_close(void); /* 停止并关闭当前音频。 */

#endif /* 头文件声明结束。 */
