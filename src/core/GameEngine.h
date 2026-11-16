#pragma once

#include "../model/Models.h"

#include <array>
#include <set>
#include <vector>

namespace nc {

struct JudgementEvent {
    int noteId = 0;
    Judgement judgement = Judgement::Miss;
    qint64 musicTimeMs = 0;
};

class GameEngine {
  public:
    bool startSession(const Chart &chart, const JudgeConfig &config = {}, int inputOffsetMs = 0);
    void reset();

    // Advance the deterministic game clock. Times earlier than the previous time are ignored.
    void advanceTo(qint64 musicTimeMs);
    bool handleKeyPress(int lane, qint64 musicTimeMs, bool autoRepeat = false);
    void handleKeyRelease(int lane, qint64 musicTimeMs, bool autoRepeat = false);

    bool pause();
    bool prepareResume();
    bool resumeLanePress(int lane);
    bool resumeLaneRelease(int lane);
    bool resumeReady() const;
    bool resume();
    void abort();

    const Chart &chart() const {
        return chart_;
    }
    const std::vector<NoteRuntime> &runtime() const {
        return runtime_;
    }
    const PlayStats &stats() const {
        return stats_;
    }
    const std::array<bool, 4> &laneDown() const {
        return laneDown_;
    }
    const std::set<int> &activeHolds() const {
        return activeHolds_;
    }
    const QString &sessionId() const {
        return sessionId_;
    }
    SessionState state() const {
        return state_;
    }
    qint64 nowMs() const {
        return nowMs_;
    }
    bool finished() const {
        return state_ == SessionState::Finished;
    }
    std::vector<JudgementEvent> takeEvents();

  private:
    static bool validLane(int lane) {
        return lane >= 0 && lane < 4;
    }
    Judgement classify(qint64 deltaMs) const;
    void finalizeNote(std::size_t index, Judgement judgement, qint64 musicTimeMs);
    void updateFinishedState();
    bool isPlayable() const;

    Chart chart_;
    JudgeConfig config_;
    int inputOffsetMs_ = 0;
    QString sessionId_;
    SessionState state_ = SessionState::Idle;
    qint64 nowMs_ = 0;
    PlayStats stats_;
    std::vector<NoteRuntime> runtime_;
    std::array<bool, 4> laneDown_{};
    std::set<int> activeHolds_;
    std::set<int> pausedHolds_;
    std::vector<JudgementEvent> events_;
};

} // namespace nc
