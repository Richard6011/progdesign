#pragma once

#include <QDateTime>
#include <QMap>
#include <QString>
#include <QVector>

#include <array>
#include <vector>

namespace nc {

enum class NoteType { Tap, Hold, ChainStep };
enum class NoteState { Pending, Holding, Hit, Miss };
enum class Judgement { Perfect, Great, Good, Miss };
enum class GameMode { Solo, Lan };
enum class SessionState { Idle, Playing, Paused, ResumeReady, Finished, Aborted };

struct NoteDefinition {
    int id = 0;
    NoteType type = NoteType::Tap;
    int lane = 0;
    qint64 hitTimeMs = 0;
    qint64 endTimeMs = 0;
    int chainId = -1;
    int chordId = -1;
};

struct NoteRuntime {
    NoteState state = NoteState::Pending;
    Judgement headGrade = Judgement::Miss;
    bool finalized = false;
};

struct SongInfo {
    QString id;
    QString title;
    QString artist;
    QString audioPath;
    QMap<QString, QString> difficultyCharts;
};

struct Chart {
    QString id;
    int version = 1;
    QString songId;
    QString difficulty;
    qint64 audioDurationMs = 0;
    qint64 approachMs = 1800;
    std::vector<NoteDefinition> notes;
    QString chartHash;
};

struct JudgeConfig {
    // Version 2 widens the forgiving windows for ordinary keyboard timing.
    qint64 perfectMs = 90;
    qint64 greatMs = 150;
    qint64 goodMs = 220;
    QString rulesVersion = QStringLiteral("2");
};

struct PlayStats {
    int perfect = 0;
    int great = 0;
    int good = 0;
    int miss = 0;
    qint64 earnedUnits = 0;
    int combo = 0;
    int maxCombo = 0;
    int finalizedCount = 0;
};

struct PlayResult {
    int score = 0;
    double accuracy = 0.0;
    QString grade = QStringLiteral("D");
    bool fullCombo = false;
    bool allPerfect = false;
    bool complete = false;
};

struct PlayRecord {
    QString sessionId;
    GameMode mode = GameMode::Solo;
    QString matchId;
    QString playerId;
    QString opponentName;
    int opponentScore = 0;
    QString resultId;
    QString winResult;
    QString playerName;
    QString songId;
    QString chartId;
    int chartVersion = 1;
    QString chartHash;
    QString difficulty;
    QString rulesVersion;
    int noteCount = 0;
    int score = 0;
    PlayStats stats;
    PlayResult result;
    QDateTime finishedAtUtc;
    bool resultConfirmed = false;
};

struct AppSettings {
    int schemaVersion = 2;
    int musicVolume = 70;
    int effectVolume = 70;
    int inputOffsetMs = 0;
    QString playerName = QStringLiteral("玩家");
};

} // namespace nc
