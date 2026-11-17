#include "ScoreService.h"

#include <algorithm>

namespace nc {

int ScoreService::weight(const Judgement judgement) {
    switch (judgement) {
    case Judgement::Perfect:
        return 1000;
    case Judgement::Great:
        return 700;
    case Judgement::Good:
        return 400;
    case Judgement::Miss:
        return 0;
    }
    return 0;
}

void ScoreService::applyJudgement(PlayStats &stats, const Judgement judgement) {
    switch (judgement) {
    case Judgement::Perfect:
        ++stats.perfect;
        break;
    case Judgement::Great:
        ++stats.great;
        break;
    case Judgement::Good:
        ++stats.good;
        break;
    case Judgement::Miss:
        ++stats.miss;
        stats.combo = 0;
        break;
    }
    const int units = weight(judgement);
    if (units > 0) {
        ++stats.combo;
        stats.earnedUnits += units;
    }
    ++stats.finalizedCount;
    stats.maxCombo = std::max(stats.maxCombo, stats.combo);
}

bool ScoreService::isValidStats(const PlayStats &stats, const int noteCount,
                                const bool requireComplete) {
    if (noteCount <= 0 || stats.perfect < 0 || stats.great < 0 || stats.good < 0 ||
        stats.miss < 0 || stats.finalizedCount < 0 || stats.finalizedCount > noteCount ||
        stats.combo < 0 || stats.maxCombo < stats.combo || stats.maxCombo > stats.finalizedCount) {
        return false;
    }
    const qint64 successCount = static_cast<qint64>(stats.perfect) + stats.great + stats.good;
    const qint64 count = successCount + stats.miss;
    const qint64 units = 1000LL * stats.perfect + 700LL * stats.great + 400LL * stats.good;
    if (count != stats.finalizedCount || units != stats.earnedUnits ||
        stats.maxCombo > successCount ||
        successCount > static_cast<qint64>(stats.maxCombo) * (stats.miss + 1LL)) {
        return false;
    }
    if (stats.miss == 0 && (stats.combo != successCount || stats.maxCombo != successCount)) {
        return false;
    }
    return !requireComplete || count == noteCount;
}

bool ScoreService::isComplete(const PlayStats &stats, const int noteCount) {
    return isValidStats(stats, noteCount, true);
}

PlayResult ScoreService::buildResult(const PlayStats &stats, const int noteCount) {
    PlayResult result;
    result.complete = isComplete(stats, noteCount);
    if (!isValidStats(stats, noteCount)) {
        return result;
    }

    result.accuracy = static_cast<double>(stats.earnedUnits) / (1000.0 * noteCount);
    const qint64 score = (1000000LL * stats.earnedUnits) / (1000LL * noteCount);
    result.score = static_cast<int>(std::clamp<qint64>(score, 0, 1000000));
    result.fullCombo = result.complete && stats.miss == 0;
    result.allPerfect = result.complete && stats.perfect == noteCount;

    const qint64 denominator = 1000LL * noteCount;
    if (stats.earnedUnits * 100 >= denominator * 95 && 100LL * stats.maxCombo >= 80LL * noteCount) {
        result.grade = QStringLiteral("S");
    } else if (stats.earnedUnits * 100 >= denominator * 90) {
        result.grade = QStringLiteral("A");
    } else if (stats.earnedUnits * 100 >= denominator * 80) {
        result.grade = QStringLiteral("B");
    } else if (stats.earnedUnits * 100 >= denominator * 70) {
        result.grade = QStringLiteral("C");
    } else {
        result.grade = QStringLiteral("D");
    }
    return result;
}

} // namespace nc
