#include "MainWindow.h"

#include "core/ScoreService.h"
#include "ui/GameWidget.h"

#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>

namespace nc {
namespace {

QString assetRoot() {
    const QStringList candidates = {
        QDir(QCoreApplication::applicationDirPath()).filePath("assets/songs"),
        QDir(QCoreApplication::applicationDirPath()).filePath("../assets/songs"),
        QDir(QCoreApplication::applicationDirPath()).filePath("../../assets/songs"),
        QDir::current().filePath("assets/songs"),
        QDir(QFileInfo(QString::fromUtf8(__FILE__)).absolutePath()).filePath("../../assets/songs")};
    for (const auto &candidate : candidates) {
        if (QFileInfo::exists(QDir(candidate).filePath("index.json")))
            return QDir::cleanPath(candidate);
    }
    return QDir::cleanPath(candidates.front());
}

} // namespace

MainWindow::MainWindow(QWidget *parent, const QString &dataDirectory, const QString &accountName)
    : QMainWindow(parent), songs_(assetRoot()), settingsRepository_(dataDirectory),
      records_(dataDirectory), accountName_(accountName.trimmed()) {
#ifdef Q_OS_WIN
    // 注册已安装的中文字体，使原生及 offscreen 平台都能正确绘制中文。
    static const int fontId = QFontDatabase::addApplicationFont(
        QDir(qEnvironmentVariable("WINDIR", "C:/Windows")).filePath("Fonts/msyh.ttc"));
    if (fontId >= 0) {
        const auto families = QFontDatabase::applicationFontFamilies(fontId);
        if (!families.isEmpty())
            QApplication::setFont(QFont(families.first(), 10));
    }
#endif
    setWindowTitle(QStringLiteral("音符碰撞"));
    resize(1000, 720);
    QString settingsWarning;
    settings_ = settingsRepository_.load(&settingsWarning);
    if (!accountName_.isEmpty())
        settings_.playerName = accountName_;
    QString recordsError;
    records_.load(&recordsError);
    QString songsError;
    songs_.loadIndex(&songsError);
    buildUi();
    if (!accountName_.isEmpty()) {
        setWindowTitle(QStringLiteral("音符碰撞 · %1").arg(accountName_));
        nickname_->setReadOnly(true);
        settingsName_->setReadOnly(true);
    }
    globalStatus_->setText(
        QStringList{songsError, recordsError, settingsWarning}.join(" ").trimmed());
    connect(&timer_, &QTimer::timeout, this, &MainWindow::tick);
    timer_.setInterval(16);
    timer_.start();
    connect(&audio_, &AudioService::loaded, this, &MainWindow::onAudioLoaded);
    connect(&audio_, &AudioService::started, this, [this](qint64 positionMs) {
        if (phase_ != Phase::Starting)
            return;
        if (resuming_ && engine_.state() != SessionState::Finished && !engine_.resume()) {
            audio_.pause();
            phase_ = Phase::ResumeReady;
            game_->setOverlay("请重新按住活动长按，再恢复演奏");
            return;
        }
        resuming_ = false;
        const qint64 anchor = clock_.nowMs() > 0 ? clock_.nowMs() : positionMs;
        clock_.start(anchor);
        phase_ = Phase::Playing;
        game_->setOverlay({});
        game_->setFocus();
        if (isLan_)
            lan_.reportStarted();
    });
    connect(&audio_, &AudioService::ended, this, [this] {
        if (phase_ != Phase::Playing)
            return;
        mediaEnded_ = true;
        qint64 finalNoteMs = 0;
        for (const auto &note : chart_.notes)
            finalNoteMs = qMax(finalNoteMs, note.endTimeMs);
        if (clock_.nowMs() + 100 < finalNoteMs) {
            abortRun("音乐提前结束，尚有未到达的音符");
            return;
        }
        engine_.advanceTo(clock_.nowMs());
        finishIfReady();
    });
    connect(&audio_, &AudioService::failed, this, [this](const QString &reason) {
        if (roomLoadPending_ || (phase_ != Phase::Idle && phase_ != Phase::Result))
            abortRun("音频播放失败：" + reason);
    });
    connect(&audio_, &AudioService::bufferingInterrupted, this, [this] {
        if (isLan_)
            abortRun("媒体缓冲中断");
        else if (phase_ == Phase::Playing)
            togglePause();
    });
    connect(game_, &GameWidget::escapePressed, this, &MainWindow::togglePause);
    connect(game_, &GameWidget::lanePressed, this, &MainWindow::onLanePress);
    connect(game_, &GameWidget::laneReleased, this, &MainWindow::onLaneRelease);
    connect(&lan_, &LanSessionService::statusChanged, this, [this](const QString &message) {
        roomStatus_->setText(message);
        globalStatus_->setText(message);
    });
    connect(&lan_, &LanSessionService::roomChanged, this, &MainWindow::updateRoom);
    connect(&lan_, &LanSessionService::configReceived, this, &MainWindow::loadRoomConfig);
    connect(&lan_, &LanSessionService::startScheduled, this, &MainWindow::beginCountdown);
    connect(&lan_, &LanSessionService::peerStatsChanged, this, [this](const PlayStats &stats) {
        peerStats_ = stats;
        peerScore_->setText(QStringLiteral("对手 %1 分 · Combo %2")
                                .arg(ScoreService::buildResult(stats, chart_.notes.size()).score)
                                .arg(stats.combo));
    });
    connect(&lan_, &LanSessionService::resultConfirmed, this,
            [this](const PlayStats &local, const PlayStats &remote, const QString &resultId,
                   const QString &win) {
                finalRecord_.stats = local;
                finalRecord_.result = ScoreService::buildResult(local, chart_.notes.size());
                finalRecord_.score = finalRecord_.result.score;
                finalRecord_.resultId = resultId;
                finalRecord_.winResult = win == "loss" ? "Lose" : win == "win" ? "Win" : "Draw";
                finalRecord_.opponentScore =
                    ScoreService::buildResult(remote, chart_.notes.size()).score;
                finalRecord_.resultConfirmed = true;
                showResult(win.compare("draw", Qt::CaseInsensitive) == 0  ? "平局"
                           : win.compare("win", Qt::CaseInsensitive) == 0 ? "你赢了"
                                                                          : "对手获胜");
            });
    connect(&lan_, &LanSessionService::aborted, this,
            [this](const QString &reason) { abortRun("联机已中止：" + reason, false); });
    showPage(Home);
}

void MainWindow::showPage(Page page) {
    pages_->setCurrentIndex(static_cast<int>(page));
}

void MainWindow::showPreparation() {
    isLan_ = false;
    showPage(Prepare);
}

void MainWindow::updatePreview() {
    if (songBox_->currentIndex() < 0 ||
        songBox_->currentIndex() >= static_cast<int>(songs_.songs().size())) {
        preview_->clear();
        return;
    }
    const auto &song = songs_.songs()[songBox_->currentIndex()];
    Chart chart;
    QString error;
    if (!songs_.loadChart(song, difficultyBox_->currentText(), &chart, &error)) {
        preview_->setText(error);
        return;
    }
    preview_->setText(QStringLiteral("%1 · %2 · %3 个判定单元 · 接近时间 %4ms")
                          .arg(song.artist, chart.difficulty)
                          .arg(chart.notes.size())
                          .arg(chart.approachMs));
}

bool MainWindow::loadSelection(int songIndex, const QString &difficulty) {
    if (songIndex < 0 || songIndex >= static_cast<int>(songs_.songs().size()))
        return false;
    song_ = songs_.songs()[songIndex];
    QString error;
    if (!songs_.loadChart(song_, difficulty, &chart_, &error)) {
        showError(error);
        return false;
    }
    if (!engine_.startSession(chart_, JudgeConfig{}, settings_.inputOffsetMs)) {
        showError("谱面无法启动");
        return false;
    }
    game_->setEngine(&engine_);
    game_->setOverlay({});
    mediaEnded_ = false;
    finishSubmitted_ = false;
    saved_ = false;
    peerStats_ = {};
    audio_.setVolume(settings_.musicVolume);
    return true;
}

bool MainWindow::updatePlayerName() {
    const QString name = accountName_.isEmpty() ? nickname_->text().trimmed() : accountName_;
    if (name.isEmpty() || name.size() > 16) {
        showError("昵称应为 1—16 个字符");
        return false;
    }
    settings_.playerName = name;
    return true;
}

void MainWindow::startSolo() {
    lan_.leaveRoom();
    isLan_ = false;
    if (!updatePlayerName())
        return;
    if (!loadSelection(songBox_->currentIndex(), difficultyBox_->currentText()))
        return;
    clock_.reset();
    phase_ = Phase::Loading;
    resuming_ = false;
    phaseSinceMs_ = lan_.localNowMs();
    score_->setText("分数 0 · Combo 0");
    peerScore_->setText("单人模式");
    pause_->setVisible(true);
    restart_->setVisible(true);
    pause_->setEnabled(false);
    pause_->setText("暂停");
    game_->setOverlay("正在加载音乐…");
    showPage(Game);
    game_->setFocus();
    audio_.load(song_.audioPath);
}

void MainWindow::onAudioLoaded() {
    qint64 lastMs = 0;
    for (const auto &note : chart_.notes)
        lastMs = qMax(lastMs, note.endTimeMs);
    if (audio_.durationMs() < lastMs + JudgeConfig{}.goodMs) {
        abortRun("音频实际时长不足以覆盖完整谱面");
        return;
    }
    if (roomLoadPending_) {
        roomLoadPending_ = false;
        lan_.setLocalResources(resourceConfig(), true);
        roomStatus_->setText("本地音频已预加载；资源一致后可以准备");
        return;
    }
    if (phase_ == Phase::Loading && !isLan_)
        beginCountdown(lan_.localNowMs() + 2500);
}

void MainWindow::tick() {
    const qint64 monotonicNow = lan_.localNowMs();
    if ((phase_ == Phase::Loading || roomLoadPending_) && monotonicNow - phaseSinceMs_ > 30000) {
        roomLoadPending_ = false;
        abortRun("音频加载超时");
        return;
    }
    if (phase_ == Phase::Starting && monotonicNow - phaseSinceMs_ > 1000) {
        abortRun("音频未能及时进入播放状态");
        return;
    }
    if (phase_ == Phase::Countdown) {
        const qint64 remaining = targetMs_ - monotonicNow;
        game_->setOverlay(QStringLiteral("%1\n%2 · %3")
                              .arg(qMax<qint64>(1, (remaining + 999) / 1000))
                              .arg(song_.title, chart_.difficulty));
        if (remaining <= 0) {
            phase_ = Phase::Starting;
            phaseSinceMs_ = monotonicNow;
            audio_.play();
        }
    }
    if (phase_ != Phase::Playing)
        return;
    pause_->setEnabled(true);
    const qint64 now = clock_.nowMs();
    engine_.advanceTo(now);
    game_->setMusicTime(now);
    for (const auto &event : engine_.takeEvents())
        game_->showJudgement(event.judgement);
    const auto result =
        ScoreService::buildResult(engine_.stats(), static_cast<int>(chart_.notes.size()));
    score_->setText(QStringLiteral("分数 %1 · Combo %2 · 准确率 %3% · %4 / %5 秒")
                        .arg(result.score)
                        .arg(engine_.stats().combo)
                        .arg(QString::number(result.accuracy * 100.0, 'f', 2))
                        .arg(now / 1000)
                        .arg(chart_.audioDurationMs / 1000));
    if (isLan_ && lan_.state() == LanSessionService::State::Playing)
        lan_.publishStats(engine_.stats(), now);
    if (mediaEnded_)
        finishIfReady();
    if (!mediaEnded_ && now > audio_.durationMs() + 2000)
        abortRun("音乐时钟与媒体状态不一致");
    game_->update();
}

void MainWindow::onLanePress(int lane) {
    const qint64 now = clock_.nowMs();
    if (phase_ == Phase::ResumeReady || (phase_ == Phase::Starting && resuming_)) {
        engine_.resumeLanePress(lane);
        if (phase_ == Phase::ResumeReady && engine_.resumeReady())
            tryResume();
        return;
    }
    if (phase_ != Phase::Playing)
        return;
    if (engine_.handleKeyPress(lane, now)) {
        for (const auto &event : engine_.takeEvents())
            game_->showJudgement(event.judgement);
    }
}

void MainWindow::onLaneRelease(int lane) {
    if (phase_ == Phase::ResumeReady || phase_ == Phase::Paused ||
        (phase_ == Phase::Starting && resuming_)) {
        engine_.resumeLaneRelease(lane);
        return;
    }
    if (phase_ == Phase::Playing)
        engine_.handleKeyRelease(lane, clock_.nowMs());
}

void MainWindow::togglePause() {
    if (isLan_) {
        if (phase_ == Phase::Playing || phase_ == Phase::Countdown || phase_ == Phase::Waiting)
            abortRun("已退出联机对局");
        return;
    }
    if (phase_ == Phase::Playing) {
        engine_.advanceTo(clock_.nowMs());
        clock_.pause();
        audio_.pause();
        engine_.pause();
        phase_ = Phase::Paused;
        game_->setOverlay("已暂停\n按继续或 Esc 恢复\n返回选曲与重新开始可在下方操作");
        pause_->setText("继续");
    } else if (phase_ == Phase::Paused) {
        engine_.prepareResume();
        phase_ = Phase::ResumeReady;
        game_->setOverlay("请重新按住正在进行的长按音符\n全部按住后自动恢复");
        pause_->setText("继续");
        game_->setFocus();
        tryResume();
    } else if (phase_ == Phase::ResumeReady) {
        // 再次取消继续，保持冻结时间和原有长按集合。
        engine_.pause();
        phase_ = Phase::Paused;
        game_->setOverlay("已暂停");
    }
}

void MainWindow::tryResume() {
    if (engine_.state() != SessionState::Finished && !engine_.resumeReady())
        return;
    resuming_ = true;
    phase_ = Phase::Starting;
    phaseSinceMs_ = lan_.localNowMs();
    pause_->setText("暂停");
    game_->setFocus();
    audio_.play();
}

void MainWindow::finishIfReady() {
    if (finishSubmitted_ || !mediaEnded_ || !engine_.finished() || phase_ != Phase::Playing)
        return;
    finishSubmitted_ = true;
    finalRecord_ = makeRecord();
    clock_.pause();
    if (isLan_) {
        phase_ = Phase::Waiting;
        game_->setOverlay("本机演奏完成\n正在等待双方最终成绩确认…\nEsc 可中止等待");
        if (!lan_.submitFinish(finalRecord_.stats))
            abortRun("最终成绩提交失败");
    } else
        showResult("演奏完成");
}

PlayRecord MainWindow::makeRecord() const {
    PlayRecord record;
    record.sessionId = engine_.sessionId();
    record.mode = isLan_ ? GameMode::Lan : GameMode::Solo;
    if (isLan_) {
        record.matchId = lan_.matchId();
        record.playerId = lan_.localPlayerId();
        record.opponentName = lan_.peerName();
    }
    record.playerName = settings_.playerName.trimmed();
    record.songId = chart_.songId;
    record.chartId = chart_.id;
    record.chartVersion = chart_.version;
    record.chartHash = chart_.chartHash;
    record.difficulty = chart_.difficulty;
    record.rulesVersion = JudgeConfig{}.rulesVersion;
    record.noteCount = static_cast<int>(chart_.notes.size());
    record.stats = engine_.stats();
    record.result = ScoreService::buildResult(record.stats, record.noteCount);
    record.score = record.result.score;
    record.finishedAtUtc = QDateTime::currentDateTimeUtc();
    record.resultConfirmed = !isLan_;
    return record;
}

void MainWindow::showResult(const QString &headline) {
    phase_ = Phase::Result;
    audio_.stop();
    const auto result = finalRecord_.result;
    resultSummary_->setText(QStringLiteral("%1\n\n%2 分 · %3  %10\n准确率 %4% · 最大连击 "
                                           "%5\nPerfect %6   Great %7   Good %8   Miss %9")
                                .arg(headline)
                                .arg(result.score)
                                .arg(result.grade)
                                .arg(QString::number(result.accuracy * 100.0, 'f', 2))
                                .arg(finalRecord_.stats.maxCombo)
                                .arg(finalRecord_.stats.perfect)
                                .arg(finalRecord_.stats.great)
                                .arg(finalRecord_.stats.good)
                                .arg(finalRecord_.stats.miss)
                                .arg(result.allPerfect  ? "AP"
                                     : result.fullCombo ? "FC"
                                                        : ""));
    if (isLan_)
        resultSummary_->setText(
            resultSummary_->text() +
            QStringLiteral("\n对手：%1 分 · 结果已确认").arg(finalRecord_.opponentScore));
    saveStatus_->setText(result.complete ? "成绩完整，可以保存。" : "成绩不完整，不能保存。");
    saveButton_->setEnabled(result.complete && (!isLan_ || finalRecord_.resultConfirmed));
    retry_->setText(isLan_ ? "返回房间再战" : "再来一次");
    showPage(Result);
    saveResult();
}

void MainWindow::saveResult() {
    QString error;
    if (!records_.saveOnce(finalRecord_, &error)) {
        saveStatus_->setText("保存失败：" + error);
        return;
    }
    saved_ = true;
    saveStatus_->setText("保存成功，已加入对应排行榜。");
    saveButton_->setEnabled(false);
}

void MainWindow::refreshRanking() {
    ranking_->setRowCount(0);
    if (rankSong_->currentIndex() < 0 ||
        rankSong_->currentIndex() >= static_cast<int>(songs_.songs().size()))
        return;
    const auto &song = songs_.songs()[rankSong_->currentIndex()];
    Chart chart;
    QString error;
    if (!songs_.loadChart(song, rankDifficulty_->currentText(), &chart, &error))
        return;
    PlayRecord key;
    key.mode = rankMode_->currentIndex() == 0 ? GameMode::Solo : GameMode::Lan;
    key.songId = chart.songId;
    key.chartId = chart.id;
    key.chartVersion = chart.version;
    key.chartHash = chart.chartHash;
    key.difficulty = chart.difficulty;
    key.rulesVersion = JudgeConfig{}.rulesVersion;
    const auto entries = records_.queryRanking(key);
    ranking_->setRowCount(static_cast<int>(entries.size()));
    for (int row = 0; row < static_cast<int>(entries.size()); ++row) {
        const auto &entry = entries[row];
        const auto result = entry.record.result;
        ranking_->setItem(row, 0, new QTableWidgetItem(QString::number(entry.rank)));
        ranking_->setItem(row, 1, new QTableWidgetItem(entry.record.playerName));
        ranking_->setItem(row, 2, new QTableWidgetItem(QString::number(result.score)));
        ranking_->setItem(
            row, 3, new QTableWidgetItem(QString::number(result.accuracy * 100.0, 'f', 2) + "%"));
        ranking_->setItem(row, 4, new QTableWidgetItem(result.grade));
        ranking_->setItem(row, 5,
                          new QTableWidgetItem(QString::number(entry.record.stats.maxCombo)));
    }
}

void MainWindow::saveSettings() {
    AppSettings next = settings_;
    next.playerName = accountName_.isEmpty() ? settingsName_->text().trimmed() : accountName_;
    next.musicVolume = volume_->value();
    next.inputOffsetMs = offset_->value();
    QString error;
    if (!settingsRepository_.save(next, &error)) {
        showError(error);
        return;
    }
    settings_ = next;
    nickname_->setText(next.playerName);
    globalStatus_->setText("设置已保存");
    showPage(Home);
}

void MainWindow::showError(const QString &message) {
    QMessageBox::warning(this, "音符碰撞", message);
}

void MainWindow::enterLan() {
    isLan_ = true;
    phase_ = Phase::Idle;
    roomLoadPending_ = false;
    suppressRoomChanges_ = true;
    roomSong_->setCurrentIndex(songBox_->currentIndex());
    roomDifficulty_->setCurrentText(difficultyBox_->currentText());
    suppressRoomChanges_ = false;
    showPage(Lobby);
    updateRoom();
}

QJsonObject MainWindow::resourceConfig() const {
    QString audioError;
    const QString audioHash = songs_.audioHash(song_, &audioError);
    return {{"songId", chart_.songId},
            {"chartId", chart_.id},
            {"chartVersion", chart_.version},
            {"difficulty", chart_.difficulty},
            {"chartHash", chart_.chartHash},
            {"audioHash", audioHash},
            {"rulesVersion", JudgeConfig{}.rulesVersion},
            {"noteCount", static_cast<int>(chart_.notes.size())}};
}

void MainWindow::applyRoomSelection() {
    if (suppressRoomChanges_ || lan_.role() != LanSessionService::Role::Host)
        return;
    if (roomSong_->currentIndex() < 0 ||
        roomSong_->currentIndex() >= static_cast<int>(songs_.songs().size()))
        return;
    song_ = songs_.songs()[roomSong_->currentIndex()];
    QString error;
    if (!songs_.loadChart(song_, roomDifficulty_->currentText(), &chart_, &error)) {
        roomStatus_->setText(error);
        return;
    }
    lan_.setRoomConfig(resourceConfig());
}

void MainWindow::loadRoomConfig(const QJsonObject &config) {
    int selected = -1;
    for (int i = 0; i < roomSong_->count(); ++i)
        if (roomSong_->itemData(i).toString() == config.value("songId").toString())
            selected = i;
    if (selected < 0) {
        lan_.setLocalResources({}, false);
        roomStatus_->setText("本机没有房主选择的歌曲");
        return;
    }
    suppressRoomChanges_ = true;
    roomSong_->setCurrentIndex(selected);
    roomDifficulty_->setCurrentText(config.value("difficulty").toString());
    suppressRoomChanges_ = false;
    song_ = songs_.songs()[selected];
    QString error;
    if (!songs_.loadChart(song_, roomDifficulty_->currentText(), &chart_, &error)) {
        lan_.setLocalResources({}, false);
        roomStatus_->setText(error);
        return;
    }
    lan_.setLocalResources(resourceConfig(), false);
    roomLoadPending_ = true;
    phaseSinceMs_ = lan_.localNowMs();
    audio_.setVolume(settings_.musicVolume);
    audio_.load(song_.audioPath);
}

void MainWindow::beginCountdown(qint64 targetMs, const QString &matchId) {
    Q_UNUSED(matchId);
    if (!audio_.ready() || chart_.notes.empty() || (isLan_ && resourceConfig() != lan_.config())) {
        abortRun("本地媒体未就绪或资源与房间不一致");
        return;
    }
    engine_.startSession(chart_, JudgeConfig{}, settings_.inputOffsetMs);
    game_->setEngine(&engine_);
    game_->setMusicTime(0);
    clock_.reset();
    mediaEnded_ = false;
    resuming_ = false;
    finishSubmitted_ = false;
    saved_ = false;
    targetMs_ = targetMs;
    phase_ = Phase::Countdown;
    game_->setOverlay("准备…");
    pause_->setVisible(!isLan_);
    restart_->setVisible(!isLan_);
    pause_->setEnabled(false);
    score_->setText("分数 0 · Combo 0");
    peerScore_->setText(isLan_ ? "等待对手比分" : "单人模式");
    showPage(Game);
    game_->setFocus();
}

void MainWindow::updateRoom() {
    roomSong_->setEnabled(lan_.role() == LanSessionService::Role::Host &&
                          (lan_.state() == LanSessionService::State::Listening ||
                           lan_.state() == LanSessionService::State::Room ||
                           lan_.state() == LanSessionService::State::Preparing ||
                           lan_.state() == LanSessionService::State::Finished));
    roomDifficulty_->setEnabled(roomSong_->isEnabled());
    if (!roomInfo_)
        return;
    roomInfo_->setText(
        QStringLiteral("房间 %1\n本机：%2\n对手：%3\n配置版本：%4\n状态：%5\n局域网 IPv4：%6")
            .arg(lan_.roomId().isEmpty() ? "未连接" : lan_.roomId(), lan_.localPlayerId(),
                 lan_.peerName().isEmpty() ? "等待加入" : lan_.peerName())
            .arg(lan_.configRevision())
            .arg(lan_.localReady() ? (lan_.peerReady() ? "双方已准备" : "等待对手准备") : "请准备")
            .arg(LanSessionService::localAddresses().join(" / ")));
    ready_->setText(lan_.localReady() ? "取消准备" : "准备");
    roomStart_->setEnabled(lan_.role() == LanSessionService::Role::Host && lan_.localReady() &&
                           lan_.peerReady());
}

void MainWindow::backToPreparation() {
    const bool wasLan = isLan_;
    abortRun(wasLan ? "已退出联机对局" : "已返回选曲", true);
    if (wasLan)
        lan_.leaveRoom();
    showPage(wasLan ? Home : Prepare);
}

void MainWindow::abortRun(const QString &reason, bool notifyLan) {
    if (notifyLan && isLan_)
        lan_.abortMatch(reason);
    engine_.abort();
    audio_.stop();
    clock_.reset();
    phase_ = Phase::Idle;
    roomLoadPending_ = false;
    resuming_ = false;
    finishSubmitted_ = true;
    saved_ = false;
    globalStatus_->setText(reason);
    if (isLan_)
        roomStatus_->setText(reason);
    else
        preview_->setText(reason);
    if (pages_->currentIndex() == static_cast<int>(Game))
        showPage(isLan_ ? Lobby : Prepare);
}

void MainWindow::changeEvent(QEvent *event) {
    if (event->type() == QEvent::WindowDeactivate) {
        if (isLan_ && (phase_ == Phase::Playing || phase_ == Phase::Countdown ||
                       phase_ == Phase::Starting || phase_ == Phase::Waiting))
            abortRun("联机失去焦点，本局已中止");
        else if (!isLan_ && phase_ == Phase::Playing)
            togglePause();
        else if (!isLan_ && phase_ == Phase::ResumeReady) {
            engine_.pause();
            phase_ = Phase::Paused;
            game_->setOverlay("已暂停");
        } else if (!isLan_ && (phase_ == Phase::Countdown || phase_ == Phase::Starting))
            abortRun("已取消开始");
    }
    QMainWindow::changeEvent(event);
}

void MainWindow::closeEvent(QCloseEvent *event) {
    audio_.stop();
    lan_.leaveRoom();
    engine_.abort();
    event->accept();
}

} // namespace nc
