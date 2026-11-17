#include "AccountRepository.h"

#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStringDecoder>

namespace nc {
namespace {
bool fail(QString *error, const QString &message) {
    if (error)
        *error = message;
    return false;
}

bool hasControl(const QString &text) {
    for (const auto character : text)
        if (character.category() == QChar::Other_Control)
            return true;
    return false;
}

bool validInput(const QString &username, const QString &password, QString *error) {
    if (username.trimmed().isEmpty() || username.trimmed().size() > 16 || hasControl(username))
        return fail(error, "账号应为 1—16 个字符，不能包含制表符、换行或控制字符");
    if (password.isEmpty() || password.size() > 64 || hasControl(password))
        return fail(error, "密码应为 1—64 个字符，不能包含制表符、换行或控制字符");
    return true;
}
} // namespace

AccountRepository::AccountRepository(QString dataDir)
    : dataDir_(dataDir.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                                 : QDir::cleanPath(dataDir)) {}

QString AccountRepository::filePath() const {
    return QDir(dataDir_).filePath("accounts.txt");
}

bool AccountRepository::load(QMap<QString, QString> &accounts, QString *error) const {
    accounts.clear();
    if (QFile::exists(filePath())) {
        QFile file(filePath());
        if (!file.open(QIODevice::ReadOnly))
            return fail(error, "无法读取账号文件：" + file.errorString());
        const auto bytes = file.readAll();
        if (file.error() != QFileDevice::NoError)
            return fail(error, "读取账号文件失败：" + file.errorString());
        QStringDecoder decoder(QStringDecoder::Utf8);
        const QString text = decoder.decode(bytes);
        if (decoder.hasError())
            return fail(error, "账号文件不是有效的 UTF-8 文本");
        auto lines = text.split('\n');
        if (lines.last().isEmpty())
            lines.removeLast();
        for (auto line : lines) {
            if (line.endsWith('\r'))
                line.chop(1);
            const auto fields = line.split('\t');
            if (fields.size() != 2 || !validInput(fields[0], fields[1], nullptr) ||
                fields[0] != fields[0].trimmed() || accounts.contains(fields[0]))
                return fail(error, "账号文件格式错误，已保留原文件");
            accounts.insert(fields[0], fields[1]);
        }
    }
    if (error)
        error->clear();
    return true;
}

bool AccountRepository::registerAccount(const QString &username, const QString &password,
                                        QString *error) {
    if (!validInput(username, password, error))
        return false;
    QMap<QString, QString> accounts;
    if (!load(accounts, error))
        return false;
    const QString name = username.trimmed();
    if (accounts.contains(name))
        return fail(error, "该账号已存在");
    accounts.insert(name, password);
    if (dataDir_.isEmpty() || !QDir().mkpath(dataDir_))
        return fail(error, "无法创建账号目录");
    QByteArray bytes;
    for (auto it = accounts.cbegin(); it != accounts.cend(); ++it)
        bytes += (it.key() + '\t' + it.value() + '\n').toUtf8();
    QSaveFile file(filePath());
    if (!file.open(QIODevice::WriteOnly))
        return fail(error, "无法保存账号文件：" + file.errorString());
    if (file.write(bytes) != bytes.size() || !file.commit())
        return fail(error, "账号保存失败：" + file.errorString());
    if (error)
        error->clear();
    return true;
}

bool AccountRepository::authenticate(const QString &username, const QString &password,
                                     QString *error) const {
    if (!validInput(username, password, error))
        return false;
    QMap<QString, QString> accounts;
    if (!load(accounts, error))
        return false;
    const auto account = accounts.constFind(username.trimmed());
    if (account == accounts.cend() || account.value() != password)
        return fail(error, "账号或密码错误");
    if (error)
        error->clear();
    return true;
}

} // namespace nc
