#include "core/ScoreService.h"
#include "storage/RecordRepository.h"
#include "storage/SettingsRepository.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <iostream>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
int failures = 0;

void check(bool condition, const char *description) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL " << description << '\n';
    }
}

bool writeFile(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

QByteArray readFile(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}

QByteArray oldRecords(int schema) {
    // Old contents are deliberately irrelevant to a reset; only a recognized envelope is needed.
    return QJsonDocument(QJsonObject{{"schemaVersion", schema},
                                    {"records", QJsonArray{QJsonObject{{"old", true}}}}})
        .toJson();
}

QByteArray oldSettings() {
    return QJsonDocument(QJsonObject{{"schemaVersion", 1}, {"musicVolume", 12},
                                    {"effectVolume", 8}, {"inputOffsetMs", -85},
                                    {"playerName", "Old Player"}})
        .toJson();
}

nc::PlayRecord completedRecord() {
    nc::PlayRecord record;
    record.sessionId = "new-session";
    record.playerName = "New Player";
    record.songId = "demo";
    record.chartId = "demo-easy";
    record.chartVersion = 1;
    record.chartHash = QString(64, 'a');
    record.difficulty = "Easy";
    record.rulesVersion = nc::JudgeConfig{}.rulesVersion;
    record.noteCount = 1;
    nc::ScoreService::applyJudgement(record.stats, nc::Judgement::Perfect);
    record.result = nc::ScoreService::buildResult(record.stats, 1);
    record.finishedAtUtc = QDateTime::currentDateTimeUtc();
    return record;
}

void testOldRecordsReset() {
    for (const int schema : {1, 2}) {
        QTemporaryDir directory;
        check(directory.isValid(), "record temporary directory created");
        const auto path = QDir(directory.path()).filePath("records.json");
        check(writeFile(path, oldRecords(schema)), "old record fixture written");
        nc::RecordRepository records(directory.path());
        QString error;
        check(records.load(&error) && error.isEmpty() && records.records().empty(),
              "recognized old records reset successfully to an empty repository");
        const auto reset = QJsonDocument::fromJson(readFile(path)).object();
        check(reset["schemaVersion"].toInt() == 3 && reset["records"].toArray().isEmpty(),
              "old file replaced by schema 3 empty records");
        check(QDir(directory.path()).entryList({"records.json.corrupt-*"}, QDir::Files).isEmpty(),
              "intentional old-version reset creates no legacy backup");
        check(records.saveOnce(completedRecord(), &error), "new valid record saves after reset");
        nc::RecordRepository reopened(directory.path());
        check(reopened.load(&error) && reopened.records().size() == 1,
              "schema 3 record survives reopening without another reset");
    }
}

void testOldSettingsReset() {
    QTemporaryDir directory;
    const auto path = QDir(directory.path()).filePath("settings.json");
    check(writeFile(path, oldSettings()), "old settings fixture written");
    nc::SettingsRepository settings(directory.path());
    QString warning;
    auto result = settings.load(&warning);
    const nc::AppSettings defaults;
    check(result.schemaVersion == 2 && result.musicVolume == defaults.musicVolume &&
              result.effectVolume == defaults.effectVolume && result.inputOffsetMs == 0 &&
              result.playerName == defaults.playerName,
          "old settings restore all default values");
    check(!warning.isEmpty() && QJsonDocument::fromJson(readFile(path)).object()["schemaVersion"].toInt() == 2,
          "settings reset is reported and persisted in schema 2");
    result.playerName = "Current Player";
    result.musicVolume = 45;
    check(settings.save(result, &warning), "new settings save after reset");
    const auto reopened = settings.load(&warning);
    check(warning.isEmpty() && reopened.playerName == "Current Player" && reopened.musicVolume == 45,
          "current settings survive reopening without another reset");
}

void testUnknownOrDamagedFilesStayProtected() {
    QTemporaryDir directory;
    const auto recordPath = QDir(directory.path()).filePath("records.json");
    const auto unknown = QJsonDocument(QJsonObject{{"schemaVersion", 4}, {"records", QJsonArray{}}}).toJson();
    check(writeFile(recordPath, unknown), "future record fixture written");
    nc::RecordRepository records(directory.path());
    QString error;
    check(!records.load(&error) && !records.saveOnce(completedRecord(), &error) && readFile(recordPath) == unknown,
          "future record version stays protected from reset and save");
    const auto settingsPath = QDir(directory.path()).filePath("settings.json");
    const QByteArray bad = "{broken";
    check(writeFile(settingsPath, bad), "damaged setting fixture written");
    nc::SettingsRepository settings(directory.path());
    settings.load(&error);
    check(!error.isEmpty() && readFile(settingsPath) == bad, "damaged settings stay unchanged");
    const auto futureSettings = QJsonDocument(QJsonObject{{"schemaVersion", 3}}).toJson();
    check(writeFile(settingsPath, futureSettings), "future settings fixture written");
    settings.load(&error);
    check(!error.isEmpty() && readFile(settingsPath) == futureSettings,
          "future settings stay protected from automatic reset");
}

void testResetWriteFailurePreservesOldFile() {
#ifdef Q_OS_WIN
    QTemporaryDir directory;
    const auto path = QDir(directory.path()).filePath("records.json");
    const auto before = oldRecords(2);
    check(writeFile(path, before), "write-failure record fixture written");
    HANDLE lock = CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), GENERIC_READ, FILE_SHARE_READ,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    check(lock != INVALID_HANDLE_VALUE, "old records locked against replacement");
    if (lock != INVALID_HANDLE_VALUE) {
        nc::RecordRepository records(directory.path());
        QString error;
        check(!records.load(&error) && !error.isEmpty(), "record reset write failure is reported");
        check(readFile(path) == before && !records.saveOnce(completedRecord(), &error),
              "failed reset preserves old bytes and blocks false saved result");
        CloseHandle(lock);
        check(records.load(&error) && records.saveOnce(completedRecord(), &error),
              "record reset and saving recover after file lock is released");
    }

    const auto settingsPath = QDir(directory.path()).filePath("settings.json");
    const auto settingsBefore = oldSettings();
    check(writeFile(settingsPath, settingsBefore), "write-failure setting fixture written");
    lock = CreateFileW(reinterpret_cast<LPCWSTR>(settingsPath.utf16()), GENERIC_READ, FILE_SHARE_READ,
                       nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    check(lock != INVALID_HANDLE_VALUE, "old settings locked against replacement");
    if (lock != INVALID_HANDLE_VALUE) {
        nc::SettingsRepository settings(directory.path());
        QString warning;
        const auto result = settings.load(&warning);
        check(!warning.isEmpty() && result.schemaVersion == 2 && readFile(settingsPath) == settingsBefore,
              "failed settings reset returns defaults with warning and preserves old bytes");
        CloseHandle(lock);
        settings.load(&warning);
        check(QJsonDocument::fromJson(readFile(settingsPath)).object()["schemaVersion"].toInt() == 2,
              "settings reset recovers after file lock is released");
    }
#endif
}
} // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    testOldRecordsReset();
    testOldSettingsReset();
    testUnknownOrDamagedFilesStayProtected();
    testResetWriteFailurePreservesOldFile();
    std::cout << "Data reset failures=" << failures << '\n';
    return failures ? 1 : 0;
}
