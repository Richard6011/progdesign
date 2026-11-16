#pragma once

#include "../model/Models.h"

namespace nc {

class ScoreService {
  public:
    // Apply one final judgement. A final judgement must be emitted exactly once per note.
    static void applyJudgement(PlayStats &stats, Judgement judgement);

    // Build a result from the complete statistics and fixed chart note count.
    static PlayResult buildResult(const PlayStats &stats, int noteCount);

    static bool isValidStats(const PlayStats &stats, int noteCount, bool requireComplete = false);
    static bool isComplete(const PlayStats &stats, int noteCount);
    static int weight(Judgement judgement);
};

} // namespace nc
