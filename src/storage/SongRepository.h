#pragma once

#include "model/Models.h"
#include <QString>
#include <vector>

namespace nc {

// 从明确的只读资源目录加载歌曲，任何资源均不得越过该目录。
class SongRepository {
  public:
    explicit SongRepository(QString resourceRoot);
    // 读取 index.json，并完整检查每首歌的三个难度；失败不发布部分歌曲。
    bool loadIndex(QString *error = nullptr);
    const std::vector<SongInfo> &songs() const {
        return songs_;
    }
    bool loadChart(const SongInfo &song, const QString &difficulty, Chart *chart,
                   QString *error = nullptr) const;
    QString audioHash(const SongInfo &song, QString *error = nullptr) const;
    QString resourceRoot() const {
        return root_;
    }

  private:
    bool resolveResource(const QString &path, QString *resolved, QString *error) const;
    QString root_;
    std::vector<SongInfo> songs_;
};

} // namespace nc
