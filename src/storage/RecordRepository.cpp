#include "storage/RecordRepository.h"
#include "core/ScoreService.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <algorithm>
#include <cmath>

namespace nc {
namespace {
constexpr int CurrentRecordsSchema = 3;

bool fail(QString *error, const QString &message) {
    if (error)
        *error = message;
    return false;
}
bool writeEmptyRecords(const QString &path, QString *error) {
    const QFileInfo info(path);
    if (!QDir().mkpath(info.absolutePath()))
        return fail(error, "无法创建成绩目录：" + info.absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return fail(error, "无法重置成绩文件：" + file.errorString());
    const auto bytes = QJsonDocument(QJsonObject{{"schemaVersion", CurrentRecordsSchema},
                                                {"records", QJsonArray{}}})
                           .toJson(QJsonDocument::Indented);
    if (file.write(bytes) != bytes.size() || !file.commit())
        return fail(error, "无法提交成绩重置：" + file.errorString());
    if (error)
        error->clear();
    return true;
}
QString modeName(GameMode mode) {
    return mode == GameMode::Lan ? QStringLiteral("Lan") : QStringLiteral("Solo");
}
QString recordKey(const PlayRecord &record) {
    return modeName(record.mode) + ":" +
           (record.mode == GameMode::Solo ? record.sessionId
                                          : record.matchId + ":" + record.playerId);
}
bool parseMode(const QJsonValue &value, GameMode *mode) {
    if (!value.isString())
        return false;
    if (value.toString() == "Solo") {
        *mode = GameMode::Solo;
        return true;
    }
    if (value.toString() == "Lan") {
        *mode = GameMode::Lan;
        return true;
    }
    return false;
}
bool intValue(const QJsonObject &object, const char *key, int *value, int minimum = -2147483647,
              int maximum = 2147483647, bool required = true) {
    const auto raw = object[key];
    if (raw.isUndefined() && !required)
        return true;
    if (!raw.isDouble())
        return false;
    const auto d = raw.toDouble();
    if (!std::isfinite(d) || std::floor(d) != d || d < minimum || d > maximum)
        return false;
    *value = static_cast<int>(d);
    return true;
}
bool longValue(const QJsonObject &object, const char *key, qint64 *value,
               qint64 minimum = -9223372036854775807LL, qint64 maximum = 9223372036854775807LL,
               bool required = true) {
    const auto raw = object[key];
    if (raw.isUndefined() && !required)
        return true;
    if (!raw.isDouble())
        return false;
    const auto d = raw.toDouble();
    if (!std::isfinite(d) || std::floor(d) != d || d < static_cast<double>(minimum) ||
        d > static_cast<double>(maximum))
        return false;
    *value = static_cast<qint64>(d);
    return true;
}
QJsonObject statsJson(const PlayStats &stats) {
    return {{"perfect", stats.perfect},
            {"great", stats.great},
            {"good", stats.good},
            {"miss", stats.miss},
            {"earnedUnits", stats.earnedUnits},
            {"combo", stats.combo},
            {"maxCombo", stats.maxCombo},
            {"finalizedCount", stats.finalizedCount}};
}
bool readStats(const QJsonObject &object, PlayStats *stats) {
    return intValue(object, "perfect", &stats->perfect, 0, 20000) &&
           intValue(object, "great", &stats->great, 0, 20000) &&
           intValue(object, "good", &stats->good, 0, 20000) &&
           intValue(object, "miss", &stats->miss, 0, 20000) &&
           longValue(object, "earnedUnits", &stats->earnedUnits, 0, 20000000) &&
           intValue(object, "combo", &stats->combo, 0, 20000) &&
           intValue(object, "maxCombo", &stats->maxCombo, 0, 20000) &&
           intValue(object, "finalizedCount", &stats->finalizedCount, 0, 20000);
}
QJsonObject resultJson(const PlayResult &result) {
    return {{"score", result.score},           {"accuracy", result.accuracy},
            {"grade", result.grade},           {"fullCombo", result.fullCombo},
            {"allPerfect", result.allPerfect}, {"complete", result.complete}};
}
bool readResult(const QJsonObject &object, PlayResult *result) {
    if (!intValue(object, "score", &result->score, 0, 1000000) || !object["accuracy"].isDouble() ||
        !std::isfinite(object["accuracy"].toDouble()) || !object["grade"].isString() ||
        !object["fullCombo"].isBool() || !object["allPerfect"].isBool() ||
        !object["complete"].isBool())
        return false;
    result->accuracy = object["accuracy"].toDouble();
    result->grade = object["grade"].toString();
    result->fullCombo = object["fullCombo"].toBool();
    result->allPerfect = object["allPerfect"].toBool();
    result->complete = object["complete"].toBool();
    return result->accuracy >= 0.0 && result->accuracy <= 1.0 && result->grade.size() == 1;
}
bool text(const QJsonObject &object, const char *key, QString *value, bool required = true) {
    if (!object[key].isString())
        return !required && object[key].isUndefined();
    *value = object[key].toString();
    return !required || !value->isEmpty();
}
QJsonObject recordJson(const PlayRecord &record) {
    return {{"sessionId", record.sessionId},
            {"mode", modeName(record.mode)},
            {"matchId", record.matchId},
            {"playerId", record.playerId},
            {"opponentName", record.opponentName},
            {"opponentScore", record.opponentScore},
            {"resultId", record.resultId},
            {"winResult", record.winResult},
            {"playerName", record.playerName},
            {"songId", record.songId},
            {"chartId", record.chartId},
            {"chartVersion", record.chartVersion},
            {"chartHash", record.chartHash},
            {"difficulty", record.difficulty},
            {"rulesVersion", record.rulesVersion},
            {"noteCount", record.noteCount},
            {"score", record.score},
            {"stats", statsJson(record.stats)},
            {"result", resultJson(record.result)},
            {"finishedAtUtc", record.finishedAtUtc.toUTC().toString(Qt::ISODateWithMs)},
            {"resultConfirmed", record.resultConfirmed}};
}
bool validRecord(const PlayRecord &record, QString *error) {
    if (record.mode != GameMode::Solo && record.mode != GameMode::Lan)
        return fail(error, "成绩游戏模式无效");
    if (record.sessionId.trimmed().isEmpty() || record.playerName.trimmed().isEmpty() ||
        record.playerName.trimmed().size() > 16)
        return fail(error, "成绩缺少有效 sessionId 或昵称");
    if (record.mode == GameMode::Solo) {
        if (record.matchId.size() || record.playerId.size())
            return fail(error, "单人成绩不能带有联机对局及玩家标识");
    } else {
        if (record.matchId.isEmpty() || record.playerId.isEmpty() || !record.resultConfirmed ||
            record.resultId.isEmpty())
            return fail(error, "联机成绩必须有对局标识、玩家标识、结果标识并完成确认");
    }
    if (record.songId.isEmpty() || record.chartId.isEmpty() || record.chartVersion < 1 ||
        record.chartHash.isEmpty() || record.difficulty.isEmpty() ||
        record.rulesVersion.isEmpty() || record.noteCount <= 0 || record.noteCount > 20000 ||
        record.score < 0 || record.score > 1000000)
        return fail(error, "成绩谱面标识或分数范围无效");
    if (!record.finishedAtUtc.isValid() || record.finishedAtUtc.timeSpec() == Qt::LocalTime)
        return fail(error, "成绩完成时间无效");
    if (!ScoreService::isValidStats(record.stats, record.noteCount, true) ||
        !record.result.complete || record.result.score != record.score)
        return fail(error, "成绩统计不完整或分数不一致");
    const auto computed = ScoreService::buildResult(record.stats, record.noteCount);
    if (computed.score != record.result.score || !std::isfinite(record.result.accuracy) ||
        std::abs(computed.accuracy - record.result.accuracy) > 0.000000001 ||
        computed.grade != record.result.grade || computed.fullCombo != record.result.fullCombo ||
        computed.allPerfect != record.result.allPerfect)
        return fail(error, "成绩分数、准确率、评级或全连标记与判定统计不一致");
    if (record.mode == GameMode::Lan) {
        if (record.opponentName.trimmed().isEmpty() || record.opponentName.trimmed().size() > 16 ||
            record.opponentScore < 0 || record.opponentScore > 1000000)
            return fail(error, "联机对手昵称或最终分数无效");
        const auto expected = record.score == record.opponentScore  ? QString("Draw")
                              : record.score > record.opponentScore ? QString("Win")
                                                                    : QString("Lose");
        const auto expectedChinese = record.score == record.opponentScore  ? QString("平")
                                     : record.score > record.opponentScore ? QString("胜")
                                                                           : QString("负");
        if (record.winResult.compare(expected, Qt::CaseInsensitive) != 0 &&
            record.winResult != expectedChinese)
            return fail(error, "联机胜负结果与双方最终分数不一致");
    }
    return true;
}
bool sameBoard(const PlayRecord &a, const PlayRecord &b) {
    return a.mode == b.mode && a.songId == b.songId && a.chartId == b.chartId &&
           a.chartVersion == b.chartVersion && a.chartHash == b.chartHash &&
           a.difficulty == b.difficulty && a.rulesVersion == b.rulesVersion;
}
} // namespace

RecordRepository::RecordRepository(QString dataDir)
    : dataDir_(dataDir.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                                 : QDir::cleanPath(dataDir)) {}
QString RecordRepository::filePath() const {
    return QDir(dataDir_).filePath("records.json");
}

bool RecordRepository::load(QString *error) {
    records_.clear();
    writable_ = true;
    loadError_.clear();
    if (!QFile::exists(filePath())) {
        if (error)
            error->clear();
        return true;
    }
    QFile file(filePath());
    if (!file.open(QIODevice::ReadOnly)) {
        writable_ = false;
        loadError_ = file.errorString();
        return fail(error, loadError_);
    }
    const auto reject = [&](const QString &reason) {
        const QString backup = filePath() + ".corrupt-" +
                               QDateTime::currentDateTimeUtc().toString("yyyyMMddHHmmsszzz");
        const bool copied = QFile::copy(filePath(), backup);
        records_.clear();
        writable_ = false;
        loadError_ = reason + (copied ? "；已备份至：" + backup : "；已保留原文件，禁止覆盖");
        return fail(error, loadError_);
    };
    if (file.size() > 64 * 1024 * 1024)
        return reject("成绩文件超过 64MiB 读取上限");
    QJsonParseError parse;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parse);
    const auto object = document.object();
    int schema = 0;
    if (parse.error != QJsonParseError::NoError || !document.isObject() ||
        !object["records"].isArray() || !intValue(object, "schemaVersion", &schema, 1, CurrentRecordsSchema))
        return reject("成绩文件损坏或版本不受支持");
    if (schema < CurrentRecordsSchema) {
        // 用户明确不保留旧版成绩；只对已识别的旧版本执行原子重置。
        file.close();
        QString resetError;
        if (!writeEmptyRecords(filePath(), &resetError)) {
            writable_ = false;
            loadError_ = "旧版成绩重置失败：" + resetError;
            return fail(error, loadError_);
        }
        if (error)
            error->clear();
        return true;
    }
    std::vector<PlayRecord> loaded;
    QSet<QString> uniqueKeys;
    for (const auto &value : object["records"].toArray()) {
        if (!value.isObject())
            return reject("成绩列表包含非对象成员");
        const auto item = value.toObject();
        PlayRecord record;
        if (!text(item, "sessionId", &record.sessionId) ||
            !parseMode(item["mode"], &record.mode) ||
            !text(item, "matchId", &record.matchId, false) ||
            !text(item, "playerId", &record.playerId, false) ||
            !text(item, "opponentName", &record.opponentName, false) ||
            !intValue(item, "opponentScore", &record.opponentScore, 0, 1000000, false) ||
            !text(item, "resultId", &record.resultId, false) ||
            !text(item, "winResult", &record.winResult, false) ||
            !text(item, "playerName", &record.playerName) ||
            !text(item, "songId", &record.songId) || !text(item, "chartId", &record.chartId) ||
            !intValue(item, "chartVersion", &record.chartVersion, 1) ||
            !text(item, "chartHash", &record.chartHash) ||
            !text(item, "difficulty", &record.difficulty) ||
            !text(item, "rulesVersion", &record.rulesVersion) ||
            !intValue(item, "noteCount", &record.noteCount, 1, 20000) ||
            !intValue(item, "score", &record.score, 0, 1000000) || !item["stats"].isObject() ||
            !readStats(item["stats"].toObject(), &record.stats) || !item["result"].isObject() ||
            !readResult(item["result"].toObject(), &record.result) ||
            !item["finishedAtUtc"].isString()) {
            return reject("成绩字段类型无效");
        }
        record.finishedAtUtc =
            QDateTime::fromString(item["finishedAtUtc"].toString(), Qt::ISODateWithMs).toUTC();
        if (item.contains("resultConfirmed") && !item["resultConfirmed"].isBool())
            return reject("成绩确认字段类型无效");
        record.resultConfirmed = item["resultConfirmed"].isBool() ? item["resultConfirmed"].toBool()
                                                                  : record.mode == GameMode::Solo;
        record.playerName = record.playerName.trimmed();
        record.opponentName = record.opponentName.trimmed();
        QString validation;
        if (!validRecord(record, &validation))
            return reject("成绩校验失败：" + validation);
        const auto key = recordKey(record);
        if (uniqueKeys.contains(key))
            return reject("成绩文件包含重复对局记录");
        uniqueKeys.insert(key);
        loaded.push_back(std::move(record));
    }
    records_ = std::move(loaded);
    if (error)
        error->clear();
    return true;
}

bool RecordRepository::saveOnce(const PlayRecord &record, QString *error) {
    if (!writable_)
        return fail(error, loadError_.isEmpty() ? "成绩仓库不可写" : loadError_);
    // 每次保存前重新读取，防止覆盖另一个程序实例或外部修改过的历史文件。
    if (!load(error))
        return false;
    PlayRecord normalized = record;
    normalized.playerName = normalized.playerName.trimmed();
    normalized.opponentName = normalized.opponentName.trimmed();
    normalized.score = normalized.result.score;
    normalized.finishedAtUtc = normalized.finishedAtUtc.toUTC();
    if (normalized.mode == GameMode::Solo)
        normalized.resultConfirmed = true;
    QString validation;
    if (!validRecord(normalized, &validation))
        return fail(error, validation);
    const auto key = recordKey(normalized);
    for (const auto &existing : records_) {
        const auto oldKey = recordKey(existing);
        if (oldKey == key) {
            if (recordJson(existing) != recordJson(normalized))
                return fail(error, "同一对局标识已存在不同成绩，保留已保存记录");
            if (error)
                error->clear();
            return true;
        }
    }
    auto candidate = records_;
    candidate.push_back(normalized);
    if (!QDir().mkpath(dataDir_))
        return fail(error, "无法创建用户数据目录：" + dataDir_);
    QSaveFile file(filePath());
    if (!file.open(QIODevice::WriteOnly))
        return fail(error, "无法保存成绩：" + file.errorString());
    QJsonArray array;
    for (const auto &item : candidate)
        array.append(recordJson(item));
    const auto bytes = QJsonDocument(QJsonObject{{"schemaVersion", CurrentRecordsSchema}, {"records", array}})
                           .toJson(QJsonDocument::Indented);
    if (file.write(bytes) != bytes.size() || !file.commit())
        return fail(error, "保存成绩失败：" + file.errorString());
    records_ = std::move(candidate);
    if (error)
        error->clear();
    return true;
}

std::vector<RankingEntry> RecordRepository::queryRanking(const PlayRecord &boardKey) const {
    const auto better = [](const PlayRecord &a, const PlayRecord &b) {
        if (a.score != b.score)
            return a.score > b.score;
        if (a.finishedAtUtc != b.finishedAtUtc)
            return a.finishedAtUtc < b.finishedAtUtc;
        return recordKey(a) < recordKey(b);
    };
    QMap<QString, PlayRecord> bestByPlayer;
    for (const auto &record : records_)
        if (sameBoard(record, boardKey)) {
            auto existing = bestByPlayer.find(record.playerName);
            if (existing == bestByPlayer.end() || better(record, existing.value()))
                bestByPlayer.insert(record.playerName, record);
        }

    std::vector<PlayRecord> best;
    best.reserve(static_cast<std::size_t>(bestByPlayer.size()));
    for (auto it = bestByPlayer.cbegin(); it != bestByPlayer.cend(); ++it)
        best.push_back(it.value());

    std::sort(best.begin(), best.end(), better);
    std::vector<RankingEntry> result;
    int rank = 0;
    int previousScore = -1;
    for (const auto &record : best) {
        if (record.score != previousScore)
            rank = static_cast<int>(result.size()) + 1;
        result.push_back({rank, record});
        previousScore = record.score;
        if (result.size() == 20)
            break;
    }
    return result;
}
} // namespace nc
