#pragma once

#include "model/Models.h"
#include <QString>

namespace nc {

class SettingsRepository {
  public:
    // 空目录使用 QStandardPaths::AppDataLocation；显式目录供独立测试使用。
    explicit SettingsRepository(QString dataDir = {});
    AppSettings load(QString *warning = nullptr);
    bool save(const AppSettings &settings, QString *error = nullptr);
    QString filePath() const;

  private:
    QString dataDir_;
};

} // namespace nc
