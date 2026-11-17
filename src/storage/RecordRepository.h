#pragma once

#include "model/Models.h"
#include <QString>
#include <vector>

namespace nc {

struct RankingEntry {
    int rank = 0;
    PlayRecord record;
};

class RecordRepository {
  public:
    explicit RecordRepository(QString dataDir = {});
    // 损坏或未知版本的成绩保留原文件，并阻止继续写回。
    bool load(QString *error = nullptr);
    // 单人 sessionId / 联机 matchId+playerId 幂等；完整核对后原子提交。
    bool saveOnce(const PlayRecord &record, QString *error = nullptr);
    std::vector<RankingEntry> queryRanking(const PlayRecord &boardKey) const;
    const std::vector<PlayRecord> &records() const {
        return records_;
    }
    QString filePath() const;

  private:
    QString dataDir_;
    std::vector<PlayRecord> records_;
    bool writable_ = true;
    QString loadError_;
};

} // namespace nc
