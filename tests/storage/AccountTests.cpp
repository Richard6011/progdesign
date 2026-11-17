#include "storage/AccountRepository.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <iostream>

namespace {
int failures = 0;

void check(bool condition, const char *description) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL " << description << '\n';
    }
}

QByteArray readFile(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}

bool writeFile(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
} // namespace

int main(int argc, char **argv) {
    QCoreApplication application(argc, argv);
    QTemporaryDir directory;
    check(directory.isValid(), "isolated account storage is available");
    if (!directory.isValid())
        return 1;
    nc::AccountRepository accounts(directory.path());
    QString error;
    check(!accounts.authenticate("new-player", "fixture", &error) && !error.isEmpty(),
          "missing account cannot log in");
    check(accounts.registerAccount("  玩家  ", " fixture pass ", &error) && error.isEmpty(),
          "registration trims username and preserves password spaces");
    check(readFile(accounts.filePath()) == QStringLiteral("玩家\t fixture pass \n").toUtf8(),
          "temporary TXT fixture stores UTF-8 account and plaintext password");
    check(accounts.authenticate("玩家", " fixture pass ", &error) && error.isEmpty(),
          "registered account can log in");
    check(!accounts.authenticate("玩家", "fixture pass", &error),
          "password spaces must match exactly");
    check(!accounts.authenticate("unknown", " fixture pass ", &error),
          "unknown username is refused");
    const auto original = readFile(accounts.filePath());
    check(!accounts.registerAccount("玩家", "different", &error) &&
              readFile(accounts.filePath()) == original,
          "duplicate registration preserves existing password");
    check(accounts.registerAccount("second", "another", &error), "second account registers");
    nc::AccountRepository reopened(directory.path());
    check(reopened.authenticate("  玩家 ", " fixture pass ", &error) &&
              reopened.authenticate("second", "another", &error),
          "both accounts survive reopening");

    const auto twoAccounts = readFile(accounts.filePath());
    for (const auto &username : QStringList{"", "  ", "bad\tname", "bad\nname",
                                           QString(17, 'a'), QStringLiteral("bad") + QChar(1)})
        check(!accounts.registerAccount(username, "fixture", &error),
              "invalid username is refused");
    for (const auto &password : QStringList{"", "bad\tpassword", "bad\npassword",
                                           "bad\rpassword", QString(65, 'x'),
                                           QStringLiteral("bad") + QChar(0)})
        check(!accounts.registerAccount("valid-name", password, &error),
              "invalid password is refused");
    check(readFile(accounts.filePath()) == twoAccounts, "invalid input leaves file unchanged");

    QTemporaryDir malformedDirectory;
    nc::AccountRepository malformed(malformedDirectory.path());
    for (const auto &bytes : QList<QByteArray>{"broken-line\n", "same\tfirst\nsame\tsecond\n",
                                               QByteArray("bad\t") + char(0xff) + '\n'}) {
        check(writeFile(malformed.filePath(), bytes), "malformed fixture written");
        check(!malformed.authenticate("same", "first", &error),
              "malformed file cannot authenticate");
        check(!malformed.registerAccount("new", "fixture", &error) &&
                  readFile(malformed.filePath()) == bytes,
              "malformed account file is never overwritten");
    }
    check(writeFile(malformed.filePath(), "crlf\tfixture\r\n") &&
              malformed.authenticate("crlf", "fixture", &error),
          "Windows CRLF text file is accepted");

    const auto blockedDirectory = QDir(directory.path()).filePath("blocked");
    check(writeFile(blockedDirectory, "parent-is-a-file"), "blocked directory fixture written");
    nc::AccountRepository blocked(blockedDirectory);
    check(!blocked.registerAccount("valid", "fixture", &error) && !error.isEmpty(),
          "registration reports write failure instead of success");
    std::cout << "Account tests: " << failures << " failure(s)\n";
    return failures == 0 ? 0 : 1;
}
