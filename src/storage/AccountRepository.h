#pragma once

#include <QMap>
#include <QString>

namespace nc {

// 本地账号表：每行保存“账号\t明文密码”，只用于本机登录。
class AccountRepository {
  public:
    explicit AccountRepository(QString dataDir = {});
    QString filePath() const;
    bool registerAccount(const QString &username, const QString &password,
                         QString *error = nullptr);
    bool authenticate(const QString &username, const QString &password,
                      QString *error = nullptr) const;

  private:
    bool load(QMap<QString, QString> &accounts, QString *error) const;
    QString dataDir_; // accounts.txt 所在的用户数据目录。
};

} // namespace nc
