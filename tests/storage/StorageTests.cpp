#include "storage/SongRepository.h"
#include "storage/RecordRepository.h"
#include "storage/SettingsRepository.h"
#include "core/ScoreService.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QTemporaryDir>
#include <iostream>

namespace {
int failures = 0;
void check(bool condition, const char *message) {
    if (!condition) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
bool writeFile(const QString &path, const QByteArray &content) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(content) == content.size();
}
QByteArray readFile(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}
nc::PlayRecord record(QString id, QString name, const nc::Chart &chart, int misses = 0) {
    nc::PlayRecord result;
    result.sessionId = id;
    result.playerName = name;
    result.songId = chart.songId;
    result.chartId = chart.id;
    result.chartVersion = chart.version;
    result.chartHash = chart.chartHash;
    result.difficulty = chart.difficulty;
    result.rulesVersion = nc::JudgeConfig{}.rulesVersion;
    result.noteCount = static_cast<int>(chart.notes.size());
    for (int i = 0; i < result.noteCount; ++i)
        nc::ScoreService::applyJudgement(result.stats, i < misses ? nc::Judgement::Miss : nc::Judgement::Perfect);
    result.result = nc::ScoreService::buildResult(result.stats, result.noteCount);
    result.finishedAtUtc = QDateTime::fromString("2026-11-16T02:00:00.000Z", Qt::ISODateWithMs);
    return result;
}
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const QString assets = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral(NOTE_COLLISION_SONGS_DIR);
    QString error;
    nc::SongRepository songs(assets);
    check(songs.loadIndex(&error), qPrintable(error));
    if (songs.songs().empty()) return 1;
    const auto song = songs.songs().front();
    nc::Chart chart;
    check(songs.loadChart(song, "Easy", &chart, &error), qPrintable(error));
    check(chart.chartHash.size() == 64, "chart hash is SHA-256");
    check(songs.audioHash(song, &error).size() == 64, "audio hash is SHA-256");

    bool foundTwinkle = false;
    for (const auto &indexedSong : songs.songs()) {
        foundTwinkle |= indexedSong.id == "twinkle-star" && indexedSong.title == "小星星";
        QSet<QString> hashes;
        std::vector<int> counts;
        for (const auto &difficulty : {QString("Easy"), QString("Normal"), QString("Hard")}) {
            nc::Chart current;
            check(songs.loadChart(indexedSong, difficulty, &current, &error), qPrintable(error));
            bool tap = false, hold = false, chain = false, chord = false;
            for (const auto &note : current.notes) {
                tap |= note.type == nc::NoteType::Tap && note.chordId < 0;
                hold |= note.type == nc::NoteType::Hold;
                chain |= note.type == nc::NoteType::ChainStep;
                chord |= note.chordId >= 0;
                check(note.endTimeMs + nc::JudgeConfig{}.goodMs <= current.audioDurationMs,
                      "every delivered note has room for the wider miss window");
            }
            check(tap && hold && chain && chord, "every song difficulty contains the four note forms");
            hashes.insert(current.chartHash);
            counts.push_back(static_cast<int>(current.notes.size()));
        }
        check(hashes.size() == 3, "each song has three distinct chart hashes");
        if (indexedSong.id == "twinkle-star")
            check(counts[0] < counts[1] && counts[1] < counts[2],
                  "Twinkle Star difficulty increases actual note density");
    }
    check(foundTwinkle, "Twinkle Star is present and selectable in the delivered song index");

    QTemporaryDir temp;
    check(temp.isValid(), "temporary directory");
    nc::RecordRepository records(temp.path());
    check(records.load(&error), "empty history loads");
    auto one = record("solo-1", "Alpha", chart);
    check(records.saveOnce(one, &error), qPrintable(error));
    check(records.saveOnce(one, &error), "repeat save is successful and idempotent");
    check(records.records().size() == 1, "duplicate save does not duplicate history");
    check(records.saveOnce(record("solo-2", "Beta", chart), &error), qPrintable(error));
    check(records.saveOnce(record("solo-3", "Gamma", chart, 1), &error), qPrintable(error));
    check(records.saveOnce(record("solo-4", "Alpha", chart, 2), &error), qPrintable(error));
    auto ranking = records.queryRanking(one);
    check(ranking.size() == 3, "each nickname contributes only its best result");
    check(ranking.size() == 3 && ranking[0].rank == 1 && ranking[1].rank == 1 && ranking[2].rank == 3,
          "equal scores use competition ranks 1, 1, 3");
    auto mismatch = one;
    mismatch.mode = nc::GameMode::Lan;
    check(records.queryRanking(mismatch).empty(), "Solo and LAN boards are separate");
    nc::RecordRepository reopened(temp.path());
    check(reopened.load(&error) && reopened.records().size() == 4, "history survives reopening");
    for (const int oldVersion : {1, 2}) {
        QTemporaryDir oldDir;
        auto legacy = QJsonDocument::fromJson(readFile(records.filePath())).object();
        legacy["schemaVersion"] = oldVersion;
        const QString legacyFile = QDir(oldDir.path()).filePath("records.json");
        check(writeFile(legacyFile, QJsonDocument(legacy).toJson()), "write old-version history fixture");
        nc::RecordRepository reset(oldDir.path());
        check(reset.load(&error) && reset.records().empty(), "old-version history resets to an empty new board");
        const auto cleared = QJsonDocument::fromJson(readFile(legacyFile)).object();
        check(cleared["schemaVersion"].toInt() == 3 && cleared["records"].toArray().isEmpty(),
              "old history file is atomically replaced by an empty schema 3 file");
        check(reset.saveOnce(record("new-run", "Delta", chart), &error), qPrintable(error));
        check(reset.records().size() == 1 && reset.records()[0].rulesVersion == "2",
              "new rules save normally after discarding old history");
    }
    auto invalid = record("invalid", "玩家", chart);
    invalid.result.score = 800000;
    check(!records.saveOnce(invalid, &error), "forged score is rejected");
    invalid = record("unfinished-lan", "玩家", chart);
    invalid.mode = nc::GameMode::Lan;
    invalid.matchId = "match";
    invalid.playerId = "guest";
    invalid.opponentName = "Host";
    invalid.opponentScore = invalid.result.score;
    invalid.winResult = "Draw";
    invalid.resultId = "result";
    check(!records.saveOnce(invalid, &error), "unconfirmed LAN result cannot be saved");
    invalid.resultConfirmed = true;
    check(records.saveOnce(invalid, &error), qPrintable(error));

    // A bad history remains on disk, creates a backup, and cannot be overwritten by a later save.
    const auto corrupt = QByteArray("{bad json");
    check(writeFile(records.filePath(), corrupt), "write corrupt fixture");
    nc::RecordRepository damaged(temp.path());
    check(!damaged.load(&error), "corrupt history is reported");
    check(!damaged.saveOnce(one, &error), "corrupt history blocks writes");
    check(!records.saveOnce(record("later", "Later", chart), &error), "already-loaded repository rechecks changed file before saving");
    check(readFile(records.filePath()) == corrupt, "corrupt history original is preserved");
    check(!QDir(temp.path()).entryList({"records.json.corrupt-*"}, QDir::Files).isEmpty(), "corrupt history backup is present");

    QTemporaryDir settingsDir;
    nc::SettingsRepository settings(settingsDir.path());
    auto options = settings.load(&error);
    check(error.isEmpty() && QFile::exists(settings.filePath()), "default settings are generated on first launch");
    options.inputOffsetMs = -75;
    options.musicVolume = 29;
    options.playerName = "测试玩家";
    check(settings.save(options, &error), qPrintable(error));
    const auto loadedOptions = settings.load(&error);
    check(loadedOptions.inputOffsetMs == -75 && loadedOptions.musicVolume == 29 && loadedOptions.playerName == "测试玩家",
          "settings round trip");
    const auto before = readFile(settings.filePath());
    options.inputOffsetMs = 201;
    check(!settings.save(options, &error) && readFile(settings.filePath()) == before, "invalid settings cannot replace valid settings");
    auto oldOptions = QJsonDocument::fromJson(before).object();
    oldOptions["schemaVersion"] = 1;
    check(writeFile(settings.filePath(), QJsonDocument(oldOptions).toJson()), "write old settings fixture");
    const auto defaults = settings.load(&error);
    check(defaults.schemaVersion == 2 && defaults.musicVolume == nc::AppSettings{}.musicVolume
              && defaults.inputOffsetMs == 0 && defaults.playerName == nc::AppSettings{}.playerName,
          "old settings reset to new defaults instead of retaining previous values");
    check(QJsonDocument::fromJson(readFile(settings.filePath())).object()["schemaVersion"].toInt() == 2,
          "old settings file is rewritten with current schema");

    // Path traversal and malformed chart fixtures are isolated from the checked-in resources.
    QTemporaryDir resourceCopy;
    QDir copy(resourceCopy.path());
    copy.mkpath("neon-pulse");
    check(QFile::copy(song.audioPath, copy.filePath("neon-pulse/neon-pulse.wav")), "copy audio fixture");
    for (const auto &difficulty : {QString("Easy"), QString("Normal"), QString("Hard")})
        check(QFile::copy(song.difficultyCharts[difficulty], copy.filePath("neon-pulse/" + difficulty.toLower() + ".json")), "copy chart fixture");
    auto fixtureIndex = QJsonDocument::fromJson(readFile(QDir(assets).filePath("index.json"))).object();
    fixtureIndex["songs"] = QJsonArray{fixtureIndex["songs"].toArray()[0]};
    check(writeFile(copy.filePath("index.json"), QJsonDocument(fixtureIndex).toJson()), "copy single-song index fixture");
    const auto chartPath = copy.filePath("neon-pulse/easy.json");
    auto malformed = QJsonDocument::fromJson(readFile(chartPath)).object();
    const auto originalChart = malformed;
    const qint64 endMs = chart.audioDurationMs - nc::JudgeConfig{}.goodMs;
    auto boundaryChart = originalChart;
    boundaryChart["notes"] = QJsonArray{QJsonObject{{"id", 1}, {"type", "tap"}, {"lane", 0}, {"timeMs", endMs}}};
    check(writeFile(chartPath, QJsonDocument(boundaryChart).toJson()), "write exact wider-tail fixture");
    nc::SongRepository boundarySongs(resourceCopy.path());
    check(boundarySongs.loadIndex(&error), "a last tap with exactly 220ms of audio remaining is allowed");
    boundaryChart["notes"] = QJsonArray{QJsonObject{{"id", 1}, {"type", "tap"}, {"lane", 0}, {"timeMs", endMs + 1}}};
    check(writeFile(chartPath, QJsonDocument(boundaryChart).toJson()), "write short wider-tail fixture");
    check(!boundarySongs.loadIndex(&error), "a last tap with only 219ms of audio remaining is rejected");
    boundaryChart["notes"] = QJsonArray{QJsonObject{{"id", 1}, {"type", "hold"}, {"lane", 0},
                                                  {"timeMs", endMs - 600}, {"endMs", endMs}}};
    check(writeFile(chartPath, QJsonDocument(boundaryChart).toJson()), "write exact hold-tail fixture");
    check(boundarySongs.loadIndex(&error), "a hold tail reserves the same full 220ms window");
    auto notes = malformed["notes"].toArray();
    auto note = notes[0].toObject();
    note["lane"] = 4;
    notes[0] = note;
    malformed["notes"] = notes;
    check(writeFile(chartPath, QJsonDocument(malformed).toJson()), "write invalid lane fixture");
    nc::SongRepository badSongs(resourceCopy.path());
    check(!badSongs.loadIndex(&error) && badSongs.songs().empty(), "invalid lane rejects the entire index");
    auto index = QJsonDocument::fromJson(readFile(copy.filePath("index.json"))).object();
    auto indexSongs = index["songs"].toArray();
    auto indexSong = indexSongs[0].toObject();
    indexSong["audioPath"] = song.audioPath;
    indexSongs[0] = indexSong;
    index["songs"] = indexSongs;
    check(writeFile(copy.filePath("index.json"), QJsonDocument(index).toJson()), "write escaped path fixture");
    check(!badSongs.loadIndex(&error), "resource outside root is rejected");

    std::cout << (failures ? "Storage tests failed: " : "Storage tests passed: ") << failures << '\n';
    return failures ? 1 : 0;
}
