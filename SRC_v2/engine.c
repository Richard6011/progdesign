#include "game.h" /* 引入游戏的数据结构和函数声明。 */
#include <string.h> /* 使用内存清零函数。 */

/* 参数 grade 为单次判定；本函数只在一个音符最终结算时调用。 */
void stats_add(Statistics *stats, int grade) /* 累计判定数量、权重及连击。 */
{ /* 函数开始。 */
    if (grade < PERFECT || grade > MISS) /* 未知等级不能修改统计。 */
        return; /* 忽略非法调用。 */
    if (grade == PERFECT) /* 完美判定对应一千权重。 */
    { /* 完美分支开始。 */
        stats->perfect++; /* 完美数量增加一。 */
        stats->units += 1000; /* 累计完美权重。 */
    } /* 完美分支结束。 */
    else if (grade == GREAT) /* 优秀判定对应七百权重。 */
    { /* 优秀分支开始。 */
        stats->great++; /* 优秀数量增加一。 */
        stats->units += 700; /* 累计优秀权重。 */
    } /* 优秀分支结束。 */
    else if (grade == GOOD) /* 良好判定对应四百权重。 */
    { /* 良好分支开始。 */
        stats->good++; /* 良好数量增加一。 */
        stats->units += 400; /* 累计良好权重。 */
    } /* 良好分支结束。 */
    else /* 其余最终判定按漏击处理。 */
    { /* 漏击分支开始。 */
        stats->miss++; /* 漏击数量增加一。 */
    } /* 漏击分支结束。 */
    stats->finished++; /* 每个音符只增加一次最终结算数量。 */
    if (grade == MISS) /* 漏击会中断连击。 */
        stats->combo = 0; /* 清零当前连击。 */
    else /* 有效命中会增加连击。 */
        stats->combo++; /* 当前连击增加一。 */
    if (stats->combo > stats->maxCombo) /* 当前连击超过历史最大值。 */
        stats->maxCombo = stats->combo; /* 更新本局最大连击。 */
} /* 函数结束。 */

Result stats_result(const Statistics *stats, int count) /* 返回本局得分、准确率和评级。 */
{ /* 函数开始。 */
    Result result = {0, 0.0, 'D', 0}; /* 默认分数为零，评级为 D。 */
    if (count <= 0) /* 没有音符时不进行除法。 */
        return result; /* 返回默认结果。 */
    result.score = (int)((long long)stats->units * 1000000 / (1000LL * count)); /* 换算为百万分。 */
    result.accuracy = 100.0 * stats->units / (1000.0 * count); /* 换算为百分数准确率。 */
    result.complete = stats_valid(stats, count); /* 只有完整合法统计才标记完成。 */
    if (result.accuracy >= 95.0 && stats->maxCombo * 100 >= count * 80) /* S 同时要求较高连击。 */
        result.grade = 'S'; /* 最高评级为 S。 */
    else if (result.accuracy >= 90.0) /* 准确率至少九十。 */
        result.grade = 'A'; /* 评级为 A。 */
    else if (result.accuracy >= 80.0) /* 准确率至少八十。 */
        result.grade = 'B'; /* 评级为 B。 */
    else if (result.accuracy >= 70.0) /* 准确率至少七十。 */
        result.grade = 'C'; /* 评级为 C。 */
    return result; /* 返回计算结果。 */
} /* 函数结束。 */

int stats_valid(const Statistics *s, int count) /* 核对可保存成绩，防止非法计数。 */
{ /* 函数开始。 */
    int hits; /* 保存所有成功命中的数量。 */
    int units; /* 保存由各判定数量重算的权重。 */
    if (count < 1 || count > MAX_NOTES) /* 限制总判定量。 */
        return 0; /* 总数量非法。 */
    if (s->perfect < 0 || s->great < 0 || s->good < 0 || s->miss < 0) /* 数量不能为负。 */
        return 0; /* 拒绝负数。 */
    if (s->perfect > count || s->great > count || s->good > count || s->miss > count) /* 限制上界。 */
        return 0; /* 拒绝超大数量，避免后续计算溢出。 */
    hits = s->perfect + s->great + s->good; /* 计算成功命中总数。 */
    if (hits + s->miss != count || s->finished != count) /* 完整成绩必须覆盖所有音符。 */
        return 0; /* 拒绝缺失或重复的判定。 */
    if (s->combo < 0 || s->maxCombo < s->combo || s->maxCombo > hits) /* 连击不能超过成功数量。 */
        return 0; /* 拒绝不合理连击。 */
    if (hits > s->maxCombo * (s->miss + 1)) /* 漏击将成功操作分成若干段连击。 */
        return 0; /* 最大连击不足以容纳全部成功操作。 */
    if (s->miss == 0 && s->maxCombo != hits) /* 全部成功时最大连击应等于总数量。 */
        return 0; /* 拒绝与全连不符的数据。 */
    units = s->perfect * 1000 + s->great * 700 + s->good * 400; /* 重算标准权重。 */
    return units == s->units; /* 权重一致才接受。 */
} /* 函数结束。 */

void game_start(Game *game, const Chart *chart, int offset) /* 用一份已校验谱面初始化游戏。 */
{ /* 函数开始。 */
    int i; /* 遍历本局音符。 */
    memset(game, 0, sizeof(*game)); /* 清除上一局统计及按键状态。 */
    game->chart = *chart; /* 固定数组可以直接按结构体复制。 */
    game->state = GAME_PLAYING; /* 判定引擎进入运行状态。 */
    game->offset = offset; /* 保存输入校准。 */
    game->feedbackTime = -10000; /* 开局不显示上一局反馈。 */
    for (i = 0; i < chart->count; i++) /* 初始化每个音符。 */
    { /* 遍历开始。 */
        game->chart.notes[i].state = NOTE_WAIT; /* 所有音符等待首次按下。 */
        game->chart.notes[i].headGrade = MISS; /* 尚无有效长按头部评级。 */
    } /* 遍历结束。 */
} /* 函数结束。 */

static void finish_note(Game *game, int index, int grade, int time) /* 对一个音符最终计分。 */
{ /* 函数开始。 */
    Note *note = &game->chart.notes[index]; /* 取得需要结算的音符。 */
    if (note->state == NOTE_HIT || note->state == NOTE_MISS) /* 已结算的音符不能再次计分。 */
        return; /* 忽略重复结算。 */
    note->state = grade == MISS ? NOTE_MISS : NOTE_HIT; /* 保存最终状态。 */
    stats_add(&game->stats, grade); /* 更新本局统计。 */
    game->lastGrade = grade; /* 保存反馈等级。 */
    game->feedbackTime = time; /* 保存反馈起始时刻。 */
} /* 函数结束。 */

static int due_time(const Game *game, const Note *note) /* 计算一个音符下次自动结算的时刻。 */
{ /* 函数开始。 */
    if (note->state == NOTE_HOLDING) /* 活动长按在尾部到期。 */
        return note->end; /* 返回长按结束时刻。 */
    if (note->state == NOTE_WAIT) /* 等待音符越过最大窗口后漏击。 */
        return note->time + GOOD_WINDOW_MS + 1 - game->offset; /* 最大窗口的边界仍可命中，下一毫秒才漏击。 */
    return 2147483647; /* 已结算音符不再自动到期。 */
} /* 函数结束。 */

static int next_due(const Game *game, int time) /* 找出最先到期的待处理音符。 */
{ /* 函数开始。 */
    int best = -1; /* 负一下标表示没有到期音符。 */
    int bestTime = 2147483647; /* 初始化最早时刻上界。 */
    int i; /* 遍历谱面。 */
    for (i = 0; i < game->chart.count; i++) /* 按头部时刻、轨道、编号的固定顺序遍历。 */
    { /* 遍历开始。 */
        int due = due_time(game, &game->chart.notes[i]); /* 计算本音符的到期时刻。 */
        if (due <= time && due < bestTime) /* 相同到期时刻保留排序较靠前的音符。 */
        { /* 更早音符分支开始。 */
            best = i; /* 保存待处理下标。 */
            bestTime = due; /* 更新最早结算时刻。 */
        } /* 更早音符分支结束。 */
    } /* 遍历结束。 */
    return best; /* 返回最早到期项。 */
} /* 函数结束。 */

void game_update(Game *game, int time) /* 用音乐时刻推进漏击和长按判定。 */
{ /* 函数开始。 */
    int index; /* 保存本轮找到的到期音符。 */
    if (game->state != GAME_PLAYING || time < game->now) /* 暂停或时间倒退时不推进。 */
        return; /* 保持原有状态。 */
    game->now = time; /* 保存本轮音乐位置。 */
    index = next_due(game, time); /* 查询第一项到期音符。 */
    while (index >= 0) /* 逐项处理到期事件，保持掉帧前后的结算顺序一致。 */
    { /* 结算循环开始。 */
        Note *note = &game->chart.notes[index]; /* 取得当前到期项。 */
        int grade = MISS; /* 默认按漏击处理。 */
        int due = due_time(game, note); /* 记录真正生效的结算时刻。 */
        if (note->state == NOTE_HOLDING && game->held[note->lane]) /* 长按必须仍按住对应轨道。 */
            grade = note->headGrade; /* 成功保持时沿用头部评级。 */
        finish_note(game, index, grade, due); /* 执行唯一一次最终结算。 */
        index = next_due(game, time); /* 查询下一项到期音符。 */
    } /* 结算循环结束。 */
} /* 函数结束。 */

static int judge_grade(int difference) /* 按绝对时间偏差得到判定等级。 */
{ /* 函数开始。 */
    if (difference < 0) /* 提前按下时偏差为负。 */
        difference = -difference; /* 取偏差绝对值。 */
    if (difference <= PERFECT_WINDOW_MS) /* 一百五十毫秒以内为完美。 */
        return PERFECT; /* 返回完美等级。 */
    if (difference <= GREAT_WINDOW_MS) /* 二百五十毫秒以内为优秀。 */
        return GREAT; /* 返回优秀等级。 */
    if (difference <= GOOD_WINDOW_MS) /* 三百五十毫秒以内为良好。 */
        return GOOD; /* 返回良好等级。 */
    return MISS; /* 窗口以外不能命中。 */
} /* 函数结束。 */

int game_press(Game *game, int lane, int time) /* 按下轨道键，最多命中一个候选音符。 */
{ /* 函数开始。 */
    int i; /* 按固定顺序查找本轨道候选音符。 */
    if (lane < 0 || lane > 3 || game->held[lane]) /* 拒绝非法轨道和持续按住产生的重复操作。 */
        return 0; /* 本次不产生新命中。 */
    if (game->state == GAME_PAUSED) /* 暂停后允许重新确认活动长按的按键。 */
    { /* 暂停分支开始。 */
        game->held[lane] = 1; /* 只登记物理按住状态。 */
        return 0; /* 暂停期间不计分。 */
    } /* 暂停分支结束。 */
    if (game->state != GAME_PLAYING) /* 尚未开始的游戏不能产生音符命中。 */
        return 0; /* 只允许运行或恢复准备时处理按下。 */
    if (time < game->now) /* 拒绝倒退输入。 */
        return 0; /* 不改变按键状态。 */
    game_update(game, time); /* 先结算当前时刻之前的到期事件。 */
    game->held[lane] = 1; /* 记录本轨道已经按住。 */
    for (i = 0; i < game->chart.count; i++) /* 谱面已按目标时刻排序。 */
    { /* 候选遍历开始。 */
        Note *note = &game->chart.notes[i]; /* 取得当前候选音符。 */
        int grade; /* 保存本候选的时间评级。 */
        if (note->lane != lane || note->state != NOTE_WAIT) /* 跳过其他轨道和已处理音符。 */
            continue; /* 继续寻找下一候选。 */
        if (note->type == NOTE_HOLD && time >= note->end) /* 长按尾部已过去时不能补按头部取分。 */
            continue; /* 即使窗口更宽，也必须实际完成保持过程。 */
        grade = judge_grade(time + game->offset - note->time); /* 计算含校准偏移的评级。 */
        if (grade == MISS) /* 本音符不在有效判定窗口内。 */
            continue; /* 查找后续可能符合窗口的音符。 */
        if (note->type == NOTE_HOLD) /* 长按头部先记录，尾部才最终结算。 */
        { /* 长按分支开始。 */
            note->state = NOTE_HOLDING; /* 长按进入持续保持状态。 */
            note->headGrade = grade; /* 保存长按头部评级。 */
        } /* 长按分支结束。 */
        else /* 单点和连续节点立即完成结算。 */
            finish_note(game, i, grade, time); /* 最终计分一次。 */
        return 1; /* 一次按下最多处理一个音符。 */
    } /* 候选遍历结束。 */
    return 0; /* 没有找到有效候选音符。 */
} /* 函数结束。 */

void game_release(Game *game, int lane, int time) /* 松开轨道键，并检查活动长按。 */
{ /* 函数开始。 */
    int i; /* 遍历当前轨道上的长按。 */
    if (lane < 0 || lane > 3) /* 拒绝非法轨道。 */
        return; /* 不访问数组。 */
    if (game->state == GAME_PLAYING && time < game->now) /* 运行时不接受倒退事件。 */
        return; /* 保持原有按住状态。 */
    game_update(game, time); /* 先推进，恰好在尾部松开也能成功结算。 */
    game->held[lane] = 0; /* 然后清除物理按住状态。 */
    if (game->state != GAME_PLAYING) /* 暂停时松开不会导致额外漏击。 */
        return; /* 保留音符运行状态。 */
    for (i = 0; i < game->chart.count; i++) /* 检查尚未完成的活动长按。 */
    { /* 遍历开始。 */
        Note *note = &game->chart.notes[i]; /* 取得音符状态。 */
        if (note->lane == lane && note->state == NOTE_HOLDING) /* 同轨长按尚未到达尾部。 */
            finish_note(game, i, MISS, time); /* 过早松开按漏击处理。 */
    } /* 遍历结束。 */
} /* 函数结束。 */

void game_pause(Game *game) /* 暂停时保留音符，只清除物理按键状态。 */
{ /* 函数开始。 */
    game->state = GAME_PAUSED; /* 进入暂停状态。 */
    memset(game->held, 0, sizeof(game->held)); /* 恢复前需要重新按住活动长按。 */
} /* 函数结束。 */

int game_resume(Game *game) /* 活动长按全部按住后，返回一并恢复判定。 */
{ /* 函数开始。 */
    int i; /* 检查每个活动长按。 */
    if (game->state != GAME_PAUSED) /* 只允许从暂停状态恢复。 */
        return 0; /* 当前状态不支持恢复。 */
    for (i = 0; i < game->chart.count; i++) /* 遍历全部音符。 */
    { /* 遍历开始。 */
        Note *note = &game->chart.notes[i]; /* 取得音符状态。 */
        if (note->state == NOTE_HOLDING && !game->held[note->lane]) /* 尚有长按未重新按住。 */
            return 0; /* 暂时保持暂停。 */
    } /* 遍历结束。 */
    game->state = GAME_PLAYING; /* 所有恢复条件均满足。 */
    return 1; /* 告知界面可以恢复音乐。 */
} /* 函数结束。 */
