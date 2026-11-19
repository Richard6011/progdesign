#include "nc.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>
static const int weights[4] = {1000, 700, 400, 0};
void stats_apply(Stats *s, Judgement j) {
    s->hits[j]++;
    s->units += weights[j];
    s->finalized++;
    s->combo = j == MISS ? 0 : s->combo + 1;
    if (s->combo > s->max_combo)
        s->max_combo = s->combo;
}
Result stats_result(const Stats *s, int n) {
    Result r = {0};
    r.grade = 'D';
    if (n <= 0)
        return r;
    r.score = (int)(1000000LL * s->units / (1000LL * n));
    r.accuracy = (double)s->units / (1000.0 * n);
    r.complete = s->finalized == n;
    r.fc = r.complete && !s->hits[MISS];
    r.ap = r.complete && s->hits[PERFECT] == n;
    if (r.score >= 950000 && 100LL * s->max_combo >= 80LL * n)
        r.grade = 'S';
    else if (r.score >= 900000)
        r.grade = 'A';
    else if (r.score >= 800000)
        r.grade = 'B';
    else if (r.score >= 700000)
        r.grade = 'C';
    return r;
}
static Judgement classify(int delta) {
    int64_t d = delta;
    if (d < 0)
        d = -d;
    return d <= 90 ? PERFECT : d <= 150 ? GREAT : d <= 220 ? GOOD : MISS;
}
static void finish_note(Engine *e, int i, Judgement j, int time) {
    e->runtime[i].state = j == MISS ? MISSED : HIT;
    stats_apply(&e->stats, j);
    e->last = j;
    e->last_time = time;
    if (e->stats.finalized == e->chart->count)
        e->state = FINISHED;
}
void engine_free(Engine *e) {
    free(e->runtime);
    memset(e, 0, sizeof(*e));
}
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
void engine_tick(Engine *e, int time) {
    if (e->state != PLAYING || time < e->now)
        return;
    e->now = time;
    for (;;) {
        int best = -1, due = INT_MAX;
        for (int i = 0; i < e->chart->count; i++) {
            const Note *n = &e->chart->notes[i];
            const Runtime *r = &e->runtime[i];
            int t;
            if (r->state == HOLDING)
                t = n->end;
            else if (r->state == PENDING)
                t = n->time + 221 - e->offset;
            else
                continue;
            if (t <= time && t < due) {
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
bool engine_key(Engine *e, int lane, int time, bool pressed) {
    if (lane < 0 || lane > 3)
        return false;
    if (e->state == RESUME_READY || (e->state == PAUSED && !pressed)) {
        e->down[lane] = pressed;
        return true;
    }
    if (e->state != PLAYING || time < e->now)
        return false;
    engine_tick(e, time);
    if (pressed && (e->state != PLAYING || e->down[lane]))
        return false;
    e->down[lane] = pressed;
    NoteState wanted = pressed ? PENDING : HOLDING;
    for (int i = 0; i < e->chart->count; i++) {
        const Note *n = &e->chart->notes[i];
        if (n->lane != lane || e->runtime[i].state != wanted)
            continue;
        Judgement j = pressed ? classify(time + e->offset - n->time) : MISS;
        if (pressed && j == MISS)
            continue;
        if (pressed && n->type == HOLD) {
            e->runtime[i].state = HOLDING;
            e->runtime[i].head = j;
        } else
            finish_note(e, i, j, time);
        return true;
    }
    return false;
}
void engine_pause(Engine *e) {
    if (e->state == PLAYING || e->state == RESUME_READY) {
        e->state = PAUSED;
        memset(e->down, 0, sizeof(e->down));
    }
}
bool engine_resume(Engine *e) {
    if (e->state != RESUME_READY)
        return false;
    for (int i = 0; i < e->chart->count; i++)
        if (e->runtime[i].state == HOLDING && !e->down[e->chart->notes[i].lane])
            return false;
    e->state = PLAYING;
    return true;
}
