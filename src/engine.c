#include "nc.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>
static const int weights[4] = {1000, 700, 400, 0};
/**
 * @brief 将一个音符的最终判定累加到统计。
 * 参数：s 为本局统计；j 必须是 PERFECT、GREAT、GOOD、MISS 中的有效枚举。
 * 返回：无返回值；更新判定计数、权重和、当前连击、最大连击及完成数量。
 * 说明：Perfect/Great/Good/Miss 权重为 1000/700/400/0；Miss
 * 清空连击，其余加一。函数不负责去重，调用者必须保证同一判定单元只提交一次。
 */
void stats_apply(Stats *s, Judgement j) {
    if (j == PERFECT)
        s->perfect++;
    else if (j == GREAT)
        s->great++;
    else if (j == GOOD)
        s->good++;
    else
        s->miss++;
    s->units += weights[j];
    s->finalized++;
    if (j == MISS)
        s->combo = 0;
    else
        s->combo++;
    if (s->combo > s->max_combo)
        s->max_combo = s->combo;
}
/**
 * @brief 检查统计字段范围、计数和分值之间是否自洽。
 * 参数：s 为待验证统计；n 为谱面固定音符数；complete 指定是否要求全部判定完成。
 * 返回：所有检查通过返回 true，否则返回 false；不修改输入。
 * 说明：除非负与上界，还检查 finalized 等于四类总和、units 等于加权和、连击不超成功数量，以及 Miss
 * 分隔下最大连击可覆盖的成功数量。网络与存储均复用这些约束，但它不能证明输入来自真实击键。
 */
bool stats_valid(const Stats *s, int n, bool complete) {
    if (n < 1 || n > NC_NOTES || s->perfect < 0 || s->great < 0 || s->good < 0 || s->miss < 0 ||
        s->finalized < 0 || s->finalized > n || s->perfect > n || s->great > n || s->good > n ||
        s->miss > n || s->combo < 0 || s->max_combo < s->combo || s->max_combo > n)
        return false;
    int success = s->perfect + s->great + s->good;
    return s->finalized == success + s->miss &&
           s->units == 1000LL * s->perfect + 700LL * s->great + 400LL * s->good &&
           s->max_combo <= success && success <= (int64_t)s->max_combo * (s->miss + 1) &&
           (s->miss || (s->combo == success && s->max_combo == success)) &&
           (!complete || s->finalized == n);
}
/**
 * @brief 根据固定音符总数计算标准化成绩。
 * 参数：s 为统计；n 为谱面全部判定单元数量，不能使用当前已判定数量代替。
 * 返回：返回 Result；统计非法时为零分、D 级且不完整。
 * 说明：分数使用整数除法归一化到百万分，准确率按相同权重计算。S 要求准确率至少 95% 且最大连击至少 80%；A/B/C
 * 为 90/80/70%。FC/AP 仅在完整局中成立。
 */
Result stats_result(const Stats *s, int n) {
    Result r = {0};
    r.grade = 'D';
    if (!stats_valid(s, n, false))
        return r;
    r.score = (int)(1000000LL * s->units / (1000LL * n));
    r.accuracy = (double)s->units / (1000.0 * n);
    r.complete = stats_valid(s, n, true);
    r.fc = r.complete && !s->miss;
    r.ap = r.complete && s->perfect == n;
    if (s->units * 100 >= 95000LL * n && 100LL * s->max_combo >= 80LL * n)
        r.grade = 'S';
    else if (s->units * 100 >= 90000LL * n)
        r.grade = 'A';
    else if (s->units * 100 >= 80000LL * n)
        r.grade = 'B';
    else if (s->units * 100 >= 70000LL * n)
        r.grade = 'C';
    return r;
}
/**
 * @brief 按击打时间误差确定判定等级。
 * 参数：delta 为校准后的输入时刻减目标时刻，单位毫秒，可为负值。
 * 返回：绝对误差 <=90/150/220 分别返回 Perfect/Great/Good，否则返回 Miss。
 * 说明：边界值包含在对应窗口内；先提升为 int64_t 再取绝对值，避免对最小负 int 直接取反溢出。
 */
static Judgement classify(int delta) {
    int64_t d = delta;
    if (d < 0)
        d = -d;
    return d <= 90 ? PERFECT : d <= 150 ? GREAT : d <= 220 ? GOOD : MISS;
}
/**
 * @brief 为一个音符执行最终结算，并阻止重复记分。
 * 参数：e 为运行引擎；i 必须是有效音符下标；j 为最终判定；time 为该判定的有效音乐时刻。
 * 返回：无返回值；更新 Runtime、Stats、最近判定提示与事件数量。
 * 说明：已为 HIT/MISSED 的音符直接忽略。长按成功也仅在尾部调用一次；完成数量达到谱面总数后将引擎置
 * FINISHED，不负责停止音乐或保存记录。
 */
static void finish_note(Engine *e, int i, Judgement j, int time) {
    if (e->runtime[i].state == HIT || e->runtime[i].state == MISSED)
        return;
    e->runtime[i].state = j == MISS ? MISSED : HIT;
    stats_apply(&e->stats, j);
    e->last = j;
    e->last_time = time;
    e->events++;
    if (e->stats.finalized == e->chart->count)
        e->state = FINISHED;
}
/**
 * @brief 释放引擎运行数组，并恢复初始状态。
 * 参数：e 为已清零初始化或已启动的 Engine。
 * 返回：无返回值；e 清零后状态为 IDLE。
 * 说明：只拥有并释放 runtime；chart 及其 notes 由调用者管理，因此不在这里释放。可对已清空引擎重复调用。
 */
void engine_free(Engine *e) {
    free(e->runtime);
    memset(e, 0, sizeof(*e));
}
/**
 * @brief 为借用的谱面创建本局运行状态。
 * 参数：e 必须已清零或有效初始化；c 为已由资源模块校验的谱面；offset 为 -200 至 +200ms 输入校准。
 * 返回：成功分配 Runtime 返回 true，并置 PLAYING；前置参数非法或分配失败返回 false。
 * 说明：会释放上一局运行数组、清空统计和按键状态，但不复制谱面。c 及 notes
 * 在整局期间必须保持有效且不能被修改；这里只做必要的数量与偏移检查，不重复完整谱面校验。
 */
bool engine_start(Engine *e, const Chart *c, int offset) {
    if (!c || !c->notes || c->count < 1 || c->count > NC_NOTES || offset < -200 || offset > 200)
        return false;
    engine_free(e);
    e->runtime = calloc((size_t)c->count, sizeof(Runtime));
    if (!e->runtime)
        return false;
    e->chart = c;
    e->offset = offset;
    e->state = PLAYING;
    e->last_time = -10000;
    return true;
}
/* Select due events by effective time, so a slow frame gives the same combo as fast frames. */
/**
 * @brief 推进音乐时间，并结算漏击头部与已到尾的长按。
 * 参数：e 为引擎；time 为非负、单调前进的音乐位置，单位毫秒。
 * 返回：无返回值；非 PLAYING 或时间倒退时直接忽略。
 * 说明：每次寻找有效结算时间最早的待处理事件，先处理实际先发生的事件，再按头部时刻、轨道、ID 破同序。漏击截止为
 * timeMs+221-offset；按住的长按到尾保留头部等级，确保慢帧与快帧产生相同连击。
 */
void engine_tick(Engine *e, int time) {
    if (e->state != PLAYING || time < e->now)
        return;
    e->now = time;
    for (;;) {
        int best = -1, due = INT_MAX;
        for (int i = 0; i < e->chart->count; i++) {
            Note *n = &e->chart->notes[i];
            Runtime *r = &e->runtime[i];
            int t = INT_MAX;
            if (r->state == HOLDING)
                t = n->end;
            else if (r->state == PENDING)
                t = n->time + 221 - e->offset;
            if (t <= time &&
                (t < due || (t == due && (best < 0 || n->time < e->chart->notes[best].time ||
                                          (n->time == e->chart->notes[best].time &&
                                           (n->lane < e->chart->notes[best].lane ||
                                            (n->lane == e->chart->notes[best].lane &&
                                             n->id < e->chart->notes[best].id))))))) {
                best = i;
                due = t;
            }
        }
        if (best < 0)
            break;
        Judgement j = e->runtime[best].state == HOLDING && e->down[e->chart->notes[best].lane]
                          ? e->runtime[best].head
                          : MISS;
        finish_note(e, best, j, due);
    }
}
/**
 * @brief 处理一条轨道从松开到按下的输入。
 * 参数：e 为引擎；lane 为 0—3；time 为音乐位置。
 * 返回：命中候选音符或登记恢复阶段按键时返回 true；无候选、重复按住、非法轨道/状态/倒退时返回 false。
 * 说明：先补算该时刻前的到期事件，再用 time+offset 寻找窗口内最早音符。单点/连锁立即结算，长按仅进入
 * HOLDING 并记录头部等级。RESUME_READY 中只记按住状态，实际是否继续由 engine_resume 判断。
 */
bool engine_press(Engine *e, int lane, int time) {
    if (lane < 0 || lane > 3)
        return false;
    if (e->state == RESUME_READY) {
        e->down[lane] = true;
        return true;
    }
    if (e->state != PLAYING || time < e->now)
        return false;
    engine_tick(e, time);
    if (e->state != PLAYING || e->down[lane])
        return false;
    e->down[lane] = true;
    int best = -1;
    for (int i = 0; i < e->chart->count; i++) {
        Note *n = &e->chart->notes[i];
        if (n->lane != lane || e->runtime[i].state != PENDING ||
            classify(time + e->offset - n->time) == MISS)
            continue;
        if (best < 0 || n->time < e->chart->notes[best].time ||
            (n->time == e->chart->notes[best].time && n->id < e->chart->notes[best].id))
            best = i;
    }
    if (best < 0)
        return false;
    Judgement j = classify(time + e->offset - e->chart->notes[best].time);
    if (e->chart->notes[best].type == HOLD) {
        e->runtime[best].state = HOLDING;
        e->runtime[best].head = j;
    } else
        finish_note(e, best, j, time);
    return true;
}
/**
 * @brief 处理轨道松开，并将提前放开的活动长按记为 Miss。
 * 参数：e 为引擎；lane 为 0—3；time 为音乐位置。
 * 返回：无返回值；非法轨道、非运行状态或倒退输入被忽略。
 * 说明：先保持按住状态调用 engine_tick，再清除
 * down，因此在精确尾时刻松开仍算成功。暂停/恢复准备阶段只清按键，不让长按失败。
 */
void engine_release(Engine *e, int lane, int time) {
    if (lane < 0 || lane > 3)
        return;
    if (e->state == RESUME_READY || e->state == PAUSED) {
        e->down[lane] = false;
        return;
    }
    if (e->state != PLAYING || time < e->now)
        return;
    engine_tick(e, time);
    e->down[lane] = false;
    for (int i = 0; i < e->chart->count; i++)
        if (e->chart->notes[i].lane == lane && e->runtime[i].state == HOLDING)
            finish_note(e, i, MISS, time);
}
/**
 * @brief 冻结可暂停的引擎状态。
 * 参数：e 为引擎，当前可为 PLAYING 或 RESUME_READY。
 * 返回：无返回值；合法状态变为 PAUSED，并清空四轨物理按住状态。
 * 说明：保留 Runtime 中的 HOLDING
 * 音符，供恢复时重新按住；本函数不控制音频，也不判断单人/联机，模式限制由应用层负责。
 */
void engine_pause(Engine *e) {
    if (e->state == PLAYING || e->state == RESUME_READY) {
        e->state = PAUSED;
        memset(e->down, 0, sizeof(e->down));
    }
}
/**
 * @brief 仅在活动长按都已重新按住时恢复判定。
 * 参数：e 必须处于 RESUME_READY；恢复所需轨道通过 engine_press 登记。
 * 返回：所有活动长按的轨道都按住时返回 true 并置 PLAYING，否则返回 false。
 * 说明：没有活动长按可立即恢复；这里不推进音乐位置、不播放音频，应用层只有在成功后才恢复音乐。
 */
bool engine_resume(Engine *e) {
    if (e->state != RESUME_READY)
        return false;
    for (int i = 0; i < e->chart->count; i++)
        if (e->runtime[i].state == HOLDING && !e->down[e->chart->notes[i].lane])
            return false;
    e->state = PLAYING;
    return true;
}
