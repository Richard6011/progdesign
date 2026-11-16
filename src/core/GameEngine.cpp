#include "GameEngine.h"

#include "ScoreService.h"

#include <QUuid>

#include <algorithm>
#include <set>
#include <tuple>

namespace nc {

bool GameEngine::startSession(const Chart &chart, const JudgeConfig &config,
                              const int inputOffsetMs) {
    if (chart.notes.empty() || config.perfectMs < 0 || config.greatMs < config.perfectMs ||
        config.goodMs < config.greatMs || inputOffsetMs < -200 || inputOffsetMs > 200) {
        return false;
    }

    std::set<int> noteIds;
    for (const auto &note : chart.notes) {
        if (!validLane(note.lane) || note.id <= 0 || !noteIds.insert(note.id).second ||
            note.hitTimeMs < 0 ||
            (note.type == NoteType::Hold && note.endTimeMs <= note.hitTimeMs) ||
            (note.type != NoteType::Hold && note.endTimeMs != note.hitTimeMs)) {
            return false;
        }
    }

    chart_ = chart;
    config_ = config;
    inputOffsetMs_ = inputOffsetMs;
    runtime_.assign(chart_.notes.size(), NoteRuntime{});
    stats_ = {};
    laneDown_.fill(false);
    activeHolds_.clear();
    pausedHolds_.clear();
    events_.clear();
    nowMs_ = 0;
    sessionId_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
    state_ = SessionState::Playing;
    return true;
}

void GameEngine::reset() {
    chart_ = {};
    config_ = {};
    inputOffsetMs_ = 0;
    sessionId_.clear();
    state_ = SessionState::Idle;
    nowMs_ = 0;
    stats_ = {};
    runtime_.clear();
    laneDown_.fill(false);
    activeHolds_.clear();
    pausedHolds_.clear();
    events_.clear();
}

Judgement GameEngine::classify(const qint64 deltaMs) const {
    if (deltaMs >= -config_.perfectMs && deltaMs <= config_.perfectMs) {
        return Judgement::Perfect;
    }
    if (deltaMs >= -config_.greatMs && deltaMs <= config_.greatMs) {
        return Judgement::Great;
    }
    if (deltaMs >= -config_.goodMs && deltaMs <= config_.goodMs) {
        return Judgement::Good;
    }
    return Judgement::Miss;
}

bool GameEngine::isPlayable() const {
    return state_ == SessionState::Playing;
}

void GameEngine::finalizeNote(const std::size_t index, const Judgement judgement,
                              const qint64 musicTimeMs) {
    if (index >= runtime_.size() || runtime_[index].finalized) {
        return;
    }

    runtime_[index].finalized = true;
    runtime_[index].state = judgement == Judgement::Miss ? NoteState::Miss : NoteState::Hit;
    ScoreService::applyJudgement(stats_, judgement);
    events_.push_back({chart_.notes[index].id, judgement, musicTimeMs});
    activeHolds_.erase(chart_.notes[index].id);
}

void GameEngine::updateFinishedState() {
    if (!runtime_.empty() && stats_.finalizedCount == static_cast<int>(runtime_.size())) {
        state_ = SessionState::Finished;
        laneDown_.fill(false);
        activeHolds_.clear();
    }
}

void GameEngine::advanceTo(const qint64 musicTimeMs) {
    if (musicTimeMs < nowMs_ || !isPlayable()) {
        return;
    }
    nowMs_ = musicTimeMs;

    struct DueFinalization {
        std::size_t index;
        qint64 effectiveTimeMs;
    };
    std::vector<DueFinalization> due;
    due.reserve(chart_.notes.size());
    for (std::size_t i = 0; i < chart_.notes.size(); ++i) {
        const auto &note = chart_.notes[i];
        const auto &runtime = runtime_[i];
        const bool endedHold = runtime.state == NoteState::Holding && nowMs_ >= note.endTimeMs;
        const bool missedHead = runtime.state == NoteState::Pending &&
                                nowMs_ > note.hitTimeMs + config_.goodMs - inputOffsetMs_;
        if (!runtime.finalized && (endedHold || missedHead)) {
            due.push_back({i, endedHold ? note.endTimeMs
                                        : note.hitTimeMs + config_.goodMs - inputOffsetMs_ + 1});
        }
    }
    std::sort(due.begin(), due.end(),
              [this](const DueFinalization &left, const DueFinalization &right) {
                  const auto &a = chart_.notes[left.index];
                  const auto &b = chart_.notes[right.index];
                  // A late frame must produce the same Combo as many timely frames. First order by
                  // the time each result became due; only true ties use target time, lane, and id.
                  return std::tie(left.effectiveTimeMs, a.hitTimeMs, a.lane, a.id) <
                         std::tie(right.effectiveTimeMs, b.hitTimeMs, b.lane, b.id);
              });
    for (const auto &finalization : due) {
        const std::size_t i = finalization.index;
        const auto &note = chart_.notes[i];
        if (runtime_[i].state == NoteState::Holding) {
            const bool held = validLane(note.lane) && laneDown_[note.lane];
            finalizeNote(i, held ? runtime_[i].headGrade : Judgement::Miss,
                         finalization.effectiveTimeMs);
        } else {
            finalizeNote(i, Judgement::Miss, finalization.effectiveTimeMs);
        }
    }
    updateFinishedState();
}

bool GameEngine::handleKeyPress(const int lane, const qint64 musicTimeMs, const bool autoRepeat) {
    if (!validLane(lane) || autoRepeat || !isPlayable() || musicTimeMs < nowMs_) {
        return false;
    }
    advanceTo(musicTimeMs);
    if (!isPlayable() || laneDown_[lane]) {
        return false;
    }
    laneDown_[lane] = true;

    const qint64 judgeTimeMs = musicTimeMs + inputOffsetMs_;
    std::size_t candidate = runtime_.size();
    for (std::size_t i = 0; i < chart_.notes.size(); ++i) {
        const auto &note = chart_.notes[i];
        if (note.lane != lane || runtime_[i].finalized || runtime_[i].state == NoteState::Holding) {
            continue;
        }
        const qint64 delta = judgeTimeMs - note.hitTimeMs;
        if (delta < -config_.goodMs || delta > config_.goodMs) {
            continue;
        }
        if (candidate == runtime_.size() || note.hitTimeMs < chart_.notes[candidate].hitTimeMs ||
            (note.hitTimeMs == chart_.notes[candidate].hitTimeMs &&
             note.id < chart_.notes[candidate].id)) {
            candidate = i;
        }
    }

    if (candidate == runtime_.size()) {
        return false;
    }

    const auto judgement = classify(judgeTimeMs - chart_.notes[candidate].hitTimeMs);
    if (judgement == Judgement::Miss) {
        return false;
    }
    const auto &note = chart_.notes[candidate];
    if (note.type == NoteType::Hold) {
        runtime_[candidate].state = NoteState::Holding;
        runtime_[candidate].headGrade = judgement;
        activeHolds_.insert(note.id);
        return true;
    }
    finalizeNote(candidate, judgement, musicTimeMs);
    updateFinishedState();
    return true;
}

void GameEngine::handleKeyRelease(const int lane, const qint64 musicTimeMs, const bool autoRepeat) {
    if (!validLane(lane) || autoRepeat || (isPlayable() && musicTimeMs < nowMs_)) {
        return;
    }
    if (isPlayable()) {
        // Keep the key down while advancing so an exact tail is successful before release.
        advanceTo(musicTimeMs);
        laneDown_[lane] = false;
        for (std::size_t i = 0; i < chart_.notes.size(); ++i) {
            const auto &note = chart_.notes[i];
            if (note.lane == lane && runtime_[i].state == NoteState::Holding &&
                !runtime_[i].finalized) {
                finalizeNote(i, Judgement::Miss, musicTimeMs);
            }
        }
        updateFinishedState();
        return;
    }
    if (state_ == SessionState::ResumeReady || state_ == SessionState::Paused) {
        laneDown_[lane] = false;
    }
}

bool GameEngine::pause() {
    if (state_ == SessionState::ResumeReady) {
        // Cancel resume preparation without losing the holds captured at the original pause.
        laneDown_.fill(false);
        state_ = SessionState::Paused;
        return true;
    }
    if (state_ != SessionState::Playing) {
        return false;
    }
    pausedHolds_.clear();
    for (const int noteId : activeHolds_) {
        pausedHolds_.insert(noteId);
    }
    laneDown_.fill(false);
    state_ = SessionState::Paused;
    return true;
}

bool GameEngine::prepareResume() {
    if (state_ != SessionState::Paused) {
        return false;
    }
    state_ = SessionState::ResumeReady;
    laneDown_.fill(false);
    return true;
}

bool GameEngine::resumeLanePress(const int lane) {
    if (state_ != SessionState::ResumeReady || !validLane(lane)) {
        return false;
    }
    laneDown_[lane] = true;
    return true;
}

bool GameEngine::resumeLaneRelease(const int lane) {
    if (state_ != SessionState::ResumeReady || !validLane(lane)) {
        return false;
    }
    laneDown_[lane] = false;
    return true;
}

bool GameEngine::resumeReady() const {
    if (state_ != SessionState::ResumeReady) {
        return false;
    }
    for (const int noteId : pausedHolds_) {
        const auto it =
            std::find_if(chart_.notes.begin(), chart_.notes.end(),
                         [noteId](const NoteDefinition &note) { return note.id == noteId; });
        if (it != chart_.notes.end() && !laneDown_[it->lane]) {
            return false;
        }
    }
    return true;
}

bool GameEngine::resume() {
    if (!resumeReady()) {
        return false;
    }
    state_ = SessionState::Playing;
    pausedHolds_.clear();
    return true;
}

void GameEngine::abort() {
    if (state_ == SessionState::Idle || state_ == SessionState::Finished) {
        return;
    }
    state_ = SessionState::Aborted;
    laneDown_.fill(false);
    activeHolds_.clear();
}

std::vector<JudgementEvent> GameEngine::takeEvents() {
    std::vector<JudgementEvent> events;
    events.swap(events_);
    return events;
}

} // namespace nc
