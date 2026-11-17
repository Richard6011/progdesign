#include "game.h" /* 引入谱面结构。 */
#include <stdio.h> /* 使用文本文件读取函数。 */
#include <string.h> /* 初始化谱面。 */

static const char *songNames[2] = {"小星星", "霓虹脉冲"}; /* 提供两首已有歌曲。 */
static const char *difficultyNames[3] = {"简单 Easy", "普通 Normal", "困难 Hard"}; /* 提供三种难度。 */
static const char *musicPaths[2] = {"assets/twinkle.wav", "assets/neon.wav"}; /* 音频相对程序目录。 */

const char *song_title(int song) /* 返回歌曲名称，非法下标返回空文本。 */
{ /* 函数开始。 */
    if (song < 0 || song > 1) /* 限制歌曲下标。 */
        return ""; /* 不访问越界数组。 */
    return songNames[song]; /* 返回对应名称。 */
} /* 函数结束。 */

const char *difficulty_title(int difficulty) /* 返回难度名称。 */
{ /* 函数开始。 */
    if (difficulty < 0 || difficulty > 2) /* 限制难度下标。 */
        return ""; /* 不访问越界数组。 */
    return difficultyNames[difficulty]; /* 返回对应名称。 */
} /* 函数结束。 */

const char *audio_path(int song) /* 返回固定的音频资源路径。 */
{ /* 函数开始。 */
    if (song < 0 || song > 1) /* 限制歌曲下标。 */
        return ""; /* 不返回无效资源。 */
    return musicPaths[song]; /* 返回相对路径。 */
} /* 函数结束。 */

static int note_valid(const Note *note, int duration) /* 校验单个音符，不包含分组检查。 */
{ /* 函数开始。 */
    if (note->id < 1 || note->lane < 0 || note->lane > 3) /* 编号及轨道必须合法。 */
        return 0; /* 拒绝非法编号及轨道。 */
    if (note->type < NOTE_TAP || note->type > NOTE_CHAIN) /* 只接受已定义类型。 */
        return 0; /* 拒绝未知音符类型。 */
    if (note->time < GOOD_WINDOW_MS + 201 || note->time > duration - GOOD_WINDOW_MS - 201 || note->end > duration) /* 为最大窗口和二百毫秒校准留余量。 */
        return 0; /* 拒绝超出音乐范围的音符。 */
    if (note->type == NOTE_HOLD) /* 长按必须有长度。 */
        return note->end > note->time; /* 尾部必须晚于头部。 */
    return note->end == note->time; /* 其他音符没有额外尾部时长。 */
} /* 函数结束。 */

static int note_before(const Note *left, const Note *right) /* 定义音符的固定排序顺序。 */
{ /* 函数开始。 */
    if (left->time != right->time) /* 先比较头部目标时刻。 */
        return left->time < right->time; /* 较早目标时刻排在前面。 */
    if (left->lane != right->lane) /* 同时发生时比较轨道。 */
        return left->lane < right->lane; /* 轨道小的先处理。 */
    return left->id < right->id; /* 最后以编号确定稳定顺序。 */
} /* 函数结束。 */

static int chart_check(const Chart *chart) /* 用基础循环检查编号、轨道占用和排序。 */
{ /* 函数开始。 */
    int i; /* 逐个音符检查。 */
    int j; /* 与此前音符比较。 */
    for (i = 0; i < chart->count; i++) /* 检查全部音符。 */
    { /* 遍历开始。 */
        const Note *note = &chart->notes[i]; /* 取得当前音符。 */
        if (!note_valid(note, chart->duration)) /* 首先检查单个音符是否合法。 */
            return 0; /* 拒绝非法音符。 */
        if (i > 0 && note_before(note, &chart->notes[i - 1])) /* 输入必须按固定顺序排列。 */
            return 0; /* 避免同一输入得到不同的候选顺序。 */
        for (j = 0; j < i; j++) /* 检查与此前音符是否冲突。 */
        { /* 内层循环开始。 */
            const Note *previous = &chart->notes[j]; /* 取得此前音符。 */
            if (previous->id == note->id) /* 编号不能重复。 */
                return 0; /* 拒绝重复编号。 */
            if (previous->lane != note->lane) /* 不同轨道之间允许同时操作。 */
                continue; /* 不进行同轨占用检查。 */
            if (previous->time == note->time) /* 同一轨道不能同时出现两个头部。 */
                return 0; /* 拒绝无法独立击打的音符。 */
            if (previous->type == NOTE_HOLD && previous->end >= note->time) /* 长按期间不能再击打同轨。 */
                return 0; /* 拒绝同轨重叠。 */
        } /* 内层循环结束。 */
    } /* 遍历结束。 */
    return 1; /* 所有检查通过。 */
} /* 函数结束。 */

int chart_load(int song, int difficulty, Chart *chart, char *error) /* 从简化文本读取谱面。 */
{ /* 函数开始。 */
    FILE *file; /* 保存打开的谱面文件。 */
    char path[100]; /* 保存固定资源路径。 */
    int i; /* 逐个读取音符。 */
    int fields; /* 保存每次实际读到的字段数量。 */
    int extra; /* 检查文件尾部是否存在意外数据。 */
    if (song < 0 || song > 1 || difficulty < 0 || difficulty > 2) /* 检查选择下标。 */
    { /* 非法选择分支开始。 */
        snprintf(error, MESSAGE_SIZE, "歌曲或难度编号无效。打开失败。"); /* 输出错误原因。 */
        return 0; /* 不读取任意路径。 */
    } /* 非法选择分支结束。 */
    snprintf(path, sizeof(path), "assets/song%d_%d.txt", song, difficulty); /* 仅拼接固定资源名。 */
    file = fopen(path, "rb"); /* 打开相对程序目录的谱面。 */
    if (file == NULL) /* 资源缺失时禁止开局。 */
    { /* 打开失败分支开始。 */
        snprintf(error, MESSAGE_SIZE, "找不到谱面：%s", path); /* 输出缺失资源。 */
        return 0; /* 返回加载失败。 */
    } /* 打开失败分支结束。 */
    memset(chart, 0, sizeof(*chart)); /* 清空输出结构。 */
    fields = fscanf(file, "%d %d %d", &chart->count, &chart->duration, &chart->approach); /* 读取谱面头。 */
    if (fields != 3 || chart->count < 1 || chart->count > MAX_NOTES) /* 检查数组容量。 */
        chart->count = 0; /* 标记文件无效。 */
    if (chart->duration < 1000 || chart->duration > 3600000) /* 限制音乐时长为一小时以内。 */
        chart->count = 0; /* 标记无效时长。 */
    if (chart->approach < 100 || chart->approach > 10000) /* 限制音符移动时长。 */
        chart->count = 0; /* 标记无效移动参数。 */
    for (i = 0; i < chart->count; i++) /* 读取每行七个整数。 */
    { /* 音符读取循环开始。 */
        Note *note = &chart->notes[i]; /* 取得当前写入位置。 */
        fields = fscanf(file, "%d %d %d %d %d %d %d", &note->id, &note->type, &note->lane, &note->time, &note->end, &note->chain, &note->chord); /* 读取音符信息。 */
        if (fields != 7) /* 行内容不足或遇到非法字符。 */
        { /* 解析失败分支开始。 */
            chart->count = 0; /* 标记整份谱面无效。 */
            break; /* 不使用部分读取的数据。 */
        } /* 解析失败分支结束。 */
    } /* 音符读取循环结束。 */
    do /* 跳过末尾空白并检查是否多出了内容。 */
    { /* 尾部检查开始。 */
        extra = fgetc(file); /* 读取一个尾部字节。 */
    } while (extra == ' ' || extra == '\r' || extra == '\n' || extra == '\t'); /* 只跳过文本空白。 */
    if (extra != EOF || ferror(file)) /* 拒绝多余内容和读取错误。 */
        chart->count = 0; /* 标记整份谱面无效。 */
    fclose(file); /* 关闭谱面文件。 */
    if (chart->count == 0 || !chart_check(chart)) /* 完整读取后再检查业务约束。 */
    { /* 校验失败分支开始。 */
        snprintf(error, MESSAGE_SIZE, "谱面格式、排序或音符内容不合法。"); /* 输出校验结果。 */
        chart->count = 0; /* 不向调用方提供部分有效的谱面。 */
        return 0; /* 返回加载失败。 */
    } /* 校验失败分支结束。 */
    error[0] = 0; /* 清除上一次错误。 */
    return 1; /* 谱面可用于游戏。 */
} /* 函数结束。 */
