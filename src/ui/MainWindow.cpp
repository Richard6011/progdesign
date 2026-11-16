#include "MainWindow.h"

#include "core/ScoreService.h"
#include "ui/GameWidget.h"

#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
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
#include <QVBoxLayout>

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

QPushButton *button(const QString &text, const QString &objectName = {}) {
    auto *result = new QPushButton(text);
    result->setMinimumHeight(40);
    if (!objectName.isEmpty())
        result->setObjectName(objectName);
    return result;
}

QLabel *heading(const QString &text, int size = 22) {
    auto *label = new QLabel(text);
    QFont font = label->font();
    font.setPixelSize(size);
    font.setBold(true);
    label->setFont(font);
    label->setObjectName("heading");
    label->setStyleSheet(QStringLiteral("font-size:%1px; font-weight:700;").arg(size));
    return label;
}

} // namespace

MainWindow::MainWindow(QWidget *parent, const QString &dataDirectory)
    : QMainWindow(parent), songs_(assetRoot()), settingsRepository_(dataDirectory),
      records_(dataDirectory) {
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
    QString recordsError;
    records_.load(&recordsError);
    QString songsError;
    songs_.loadIndex(&songsError);
    buildUi();
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

void MainWindow::buildUi() {
    setStyleSheet(R"(
        QMainWindow { background:#0b1020; color:#ecf2ff; }
        QWidget { color:#ecf2ff; font-size:14px; }
        QLabel#heading { color:#f5f8ff; margin:4px 0 12px; }
        QLabel#muted { color:#93a4c6; }
        QPushButton { background:#1a2745; border:1px solid #32456e; border-radius:6px; padding:8px 16px; }
        QPushButton:hover { background:#263961; border-color:#7c94ff; }
        QPushButton#primary { background:#4667d8; border-color:#6987f4; font-weight:bold; }
        QComboBox, QLineEdit, QSpinBox { background:#111a30; border:1px solid #344566; border-radius:5px; padding:7px; }
        QComboBox QAbstractItemView { background:#111a30; color:#ecf2ff; selection-background-color:#4667d8; }
        QSlider::groove:horizontal { height:5px; background:#263653; }
        QSlider::handle:horizontal { width:16px; margin:-6px 0; border-radius:8px; background:#7c94ff; }
        QTableWidget { background:#10182b; gridline-color:#273552; border:1px solid #2c3e60; }
        QHeaderView::section { background:#192641; color:#cfdaef; padding:7px; border:0; }
        QGroupBox { border:1px solid #283b5d; border-radius:7px; margin-top:12px; padding:16px 10px 10px; }
        QGroupBox::title { subcontrol-origin:margin; left:12px; padding:0 5px; color:#98abce; }
    )");
    pages_ = new QStackedWidget(this);
    setCentralWidget(pages_);
    globalStatus_ = new QLabel;
    globalStatus_->setObjectName("muted");
    globalStatus_->setAlignment(Qt::AlignCenter);

    auto *home = new QWidget;
    auto *homeLayout = new QVBoxLayout(home);
    homeLayout->setContentsMargins(72, 48, 72, 48);
    homeLayout->setSpacing(15);
    homeLayout->addWidget(heading("音符碰撞", 38));
    auto *subtitle = new QLabel("四轨节奏挑战 · D / F / J / K");
    subtitle->setObjectName("muted");
    homeLayout->addWidget(subtitle);
    homeLayout->addSpacing(24);
    auto *start = button("开始游戏", "primary");
    homeLayout->addWidget(start);
    auto *lan = button("局域网双人");
    homeLayout->addWidget(lan);
    auto *rank = button("排行榜");
    homeLayout->addWidget(rank);
    auto *settings = button("设置");
    homeLayout->addWidget(settings);
    auto *help = button("操作与规则");
    homeLayout->addWidget(help);
    auto *quit = button("退出");
    homeLayout->addWidget(quit);
    homeLayout->addStretch();
    homeLayout->addWidget(globalStatus_);
    pages_->addWidget(home);
    connect(start, &QPushButton::clicked, this, &MainWindow::showPreparation);
    connect(lan, &QPushButton::clicked, this, [this] { enterLan(false); });
    connect(rank, &QPushButton::clicked, this, [this] {
        refreshRanking();
        showPage(Ranking);
    });
    connect(settings, &QPushButton::clicked, this, [this] {
        settingsName_->setText(settings_.playerName);
        volume_->setValue(settings_.musicVolume);
        offset_->setValue(settings_.inputOffsetMs);
        showPage(Settings);
    });
    connect(help, &QPushButton::clicked, this, [this] { showPage(Help); });
    connect(quit, &QPushButton::clicked, qApp, &QApplication::quit);

    auto *prepare = new QWidget;
    auto *prepareLayout = new QVBoxLayout(prepare);
    prepareLayout->setContentsMargins(60, 42, 60, 42);
    prepareLayout->addWidget(heading("准备演奏"));
    auto *form = new QFormLayout;
    songBox_ = new QComboBox;
    difficultyBox_ = new QComboBox;
    difficultyBox_->addItems({"Easy", "Normal", "Hard"});
    modeBox_ = new QComboBox;
    modeBox_->addItems({"单人", "双人联机"});
    nickname_ = new QLineEdit(settings_.playerName);
    form->addRow("模式", modeBox_);
    form->addRow("曲目", songBox_);
    form->addRow("难度", difficultyBox_);
    form->addRow("昵称", nickname_);
    prepareLayout->addLayout(form);
    preview_ = new QLabel;
    preview_->setObjectName("muted");
    preview_->setWordWrap(true);
    prepareLayout->addWidget(preview_);
    soloStart_ = button("开始演奏", "primary");
    auto *back = button("返回");
    prepareLayout->addWidget(soloStart_);
    prepareLayout->addWidget(back);
    prepareLayout->addStretch();
    pages_->addWidget(prepare);
    connect(songBox_, &QComboBox::currentIndexChanged, this, &MainWindow::updatePreview);
    connect(difficultyBox_, &QComboBox::currentTextChanged, this, &MainWindow::updatePreview);
    connect(modeBox_, &QComboBox::currentTextChanged, this, [this](const QString &mode) {
        soloStart_->setText(mode == "双人联机" ? "进入联机房间" : "开始演奏");
    });
    connect(soloStart_, &QPushButton::clicked, this, [this] {
        if (modeBox_->currentIndex() == 0)
            startSolo();
        else
            enterLan(false);
    });
    connect(back, &QPushButton::clicked, this, [this] { showPage(Home); });
    for (const auto &song : songs_.songs())
        songBox_->addItem(song.title, song.id);
    updatePreview();

    auto *lobby = new QWidget;
    auto *lobbyLayout = new QVBoxLayout(lobby);
    lobbyLayout->setContentsMargins(50, 35, 50, 35);
    lobbyLayout->addWidget(heading("局域网房间"));
    auto *networkForm = new QFormLayout;
    address_ = new QLineEdit;
    address_->setPlaceholderText("例如 192.168.1.100");
    port_ = new QSpinBox;
    port_->setRange(1024, 65535);
    port_->setValue(38765);
    networkForm->addRow("房主 IPv4", address_);
    networkForm->addRow("端口", port_);
    lobbyLayout->addLayout(networkForm);
    auto *host = button("创建房间", "primary");
    auto *join = button("加入房间");
    lobbyLayout->addWidget(host);
    lobbyLayout->addWidget(join);
    auto *roomGroup = new QGroupBox("房间配置");
    auto *roomForm = new QFormLayout(roomGroup);
    roomSong_ = new QComboBox;
    roomDifficulty_ = new QComboBox;
    roomDifficulty_->addItems({"Easy", "Normal", "Hard"});
    roomForm->addRow("曲目", roomSong_);
    roomForm->addRow("难度", roomDifficulty_);
    lobbyLayout->addWidget(roomGroup);
    ready_ = button("准备");
    roomStart_ = button("房主开始");
    lobbyLayout->addWidget(ready_);
    lobbyLayout->addWidget(roomStart_);
    roomInfo_ = new QLabel;
    roomInfo_->setObjectName("muted");
    roomInfo_->setWordWrap(true);
    lobbyLayout->addWidget(roomInfo_);
    roomStatus_ = new QLabel;
    roomStatus_->setObjectName("muted");
    lobbyLayout->addWidget(roomStatus_);
    auto *lobbyBack = button("返回");
    lobbyLayout->addWidget(lobbyBack);
    lobbyLayout->addStretch();
    pages_->addWidget(lobby);
    for (const auto &song : songs_.songs())
        roomSong_->addItem(song.title, song.id);
    connect(host, &QPushButton::clicked, this, [this] {
        if (lan_.state() != LanSessionService::State::Idle &&
            lan_.state() != LanSessionService::State::Aborted)
            return;
        const QString name = nickname_->text().trimmed();
        if (name.isEmpty() || name.size() > 16) {
            showError("昵称应为 1—16 个字符");
            return;
        }
        settings_.playerName = name;
        if (!lan_.createRoom(static_cast<quint16>(port_->value()), name))
            showError("创建房间失败");
        else
            applyRoomSelection();
    });
    connect(join, &QPushButton::clicked, this, [this] {
        if (lan_.state() != LanSessionService::State::Idle &&
            lan_.state() != LanSessionService::State::Aborted)
            return;
        const QString name = nickname_->text().trimmed();
        if (name.isEmpty() || name.size() > 16) {
            showError("昵称应为 1—16 个字符");
            return;
        }
        settings_.playerName = name;
        if (!lan_.joinRoom(address_->text().trimmed(), static_cast<quint16>(port_->value()), name))
            showError("加入房间失败");
    });
    connect(ready_, &QPushButton::clicked, this, [this] {
        lan_.setReady(!lan_.localReady());
        updateRoom();
    });
    connect(roomStart_, &QPushButton::clicked, this, [this] { lan_.requestStart(); });
    connect(roomSong_, &QComboBox::currentIndexChanged, this, &MainWindow::applyRoomSelection);
    connect(roomDifficulty_, &QComboBox::currentTextChanged, this,
            [this] { applyRoomSelection(); });
    connect(lobbyBack, &QPushButton::clicked, this, [this] {
        lan_.leaveRoom();
        showPage(Home);
    });

    auto *gamePage = new QWidget;
    auto *gameLayout = new QVBoxLayout(gamePage);
    auto *gameTop = new QHBoxLayout;
    score_ = new QLabel("分数 0 · Combo 0");
    peerScore_ = new QLabel("对手未连接");
    gameTop->addWidget(score_);
    gameTop->addStretch();
    gameTop->addWidget(peerScore_);
    pause_ = button("暂停");
    pause_->setMaximumWidth(100);
    gameTop->addWidget(pause_);
    gameLayout->addLayout(gameTop);
    game_ = new GameWidget;
    gameLayout->addWidget(game_, 1);
    auto *gameActions = new QHBoxLayout;
    restart_ = button("重新开始");
    auto *leaveGame = button("返回选曲 / 退出联机");
    gameActions->addWidget(restart_);
    gameActions->addWidget(leaveGame);
    gameLayout->addLayout(gameActions);
    connect(restart_, &QPushButton::clicked, this, [this] {
        if (!isLan_)
            startSolo();
    });
    connect(leaveGame, &QPushButton::clicked, this, &MainWindow::backToPreparation);
    pages_->addWidget(gamePage);
    connect(pause_, &QPushButton::clicked, this, &MainWindow::togglePause);

    auto *resultPage = new QWidget;
    auto *resultLayout = new QVBoxLayout(resultPage);
    resultLayout->setContentsMargins(60, 45, 60, 45);
    resultSummary_ = heading("结算", 30);
    resultLayout->addWidget(resultSummary_);
    saveStatus_ = new QLabel;
    saveStatus_->setObjectName("muted");
    saveStatus_->setWordWrap(true);
    resultLayout->addWidget(saveStatus_);
    saveButton_ = button("保存成绩", "primary");
    retry_ = button("再来一次");
    auto *resultHome = button("返回主页");
    resultLayout->addWidget(saveButton_);
    resultLayout->addWidget(retry_);
    resultLayout->addWidget(resultHome);
    resultLayout->addStretch();
    pages_->addWidget(resultPage);
    connect(saveButton_, &QPushButton::clicked, this, &MainWindow::saveResult);
    connect(retry_, &QPushButton::clicked, this, [this] {
        if (isLan_) {
            showPage(Lobby);
            updateRoom();
        } else
            startSolo();
    });
    connect(resultHome, &QPushButton::clicked, this, [this] {
        lan_.leaveRoom();
        phase_ = Phase::Idle;
        showPage(Home);
    });

    auto *rankPage = new QWidget;
    auto *rankLayout = new QVBoxLayout(rankPage);
    rankLayout->setContentsMargins(40, 35, 40, 35);
    rankLayout->addWidget(heading("排行榜"));
    auto *rankControls = new QHBoxLayout;
    rankMode_ = new QComboBox;
    rankMode_->addItems({"单人", "双人联机"});
    rankSong_ = new QComboBox;
    rankDifficulty_ = new QComboBox;
    rankDifficulty_->addItems({"Easy", "Normal", "Hard"});
    rankControls->addWidget(rankMode_);
    rankControls->addWidget(rankSong_);
    rankControls->addWidget(rankDifficulty_);
    auto *rankRefresh = button("刷新");
    rankControls->addWidget(rankRefresh);
    rankLayout->addLayout(rankControls);
    ranking_ = new QTableWidget(0, 6);
    ranking_->setHorizontalHeaderLabels({"名次", "昵称", "分数", "准确率", "评级", "最大连击"});
    ranking_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ranking_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    rankLayout->addWidget(ranking_);
    auto *rankBack = button("返回");
    rankLayout->addWidget(rankBack);
    pages_->addWidget(rankPage);
    connect(rankRefresh, &QPushButton::clicked, this, &MainWindow::refreshRanking);
    connect(rankMode_, &QComboBox::currentTextChanged, this, &MainWindow::refreshRanking);
    connect(rankSong_, &QComboBox::currentIndexChanged, this, &MainWindow::refreshRanking);
    connect(rankDifficulty_, &QComboBox::currentTextChanged, this, &MainWindow::refreshRanking);
    connect(rankBack, &QPushButton::clicked, this, [this] { showPage(Home); });
    for (const auto &song : songs_.songs())
        rankSong_->addItem(song.title, song.id);

    auto *settingsPage = new QWidget;
    auto *settingsLayout = new QVBoxLayout(settingsPage);
    settingsLayout->setContentsMargins(60, 45, 60, 45);
    settingsLayout->addWidget(heading("设置"));
    auto *settingsForm = new QFormLayout;
    settingsName_ = new QLineEdit;
    volume_ = new QSlider(Qt::Horizontal);
    volume_->setRange(0, 100);
    offset_ = new QSpinBox;
    offset_->setRange(-200, 200);
    offset_->setSuffix(" ms");
    settingsForm->addRow("昵称", settingsName_);
    settingsForm->addRow("音乐音量", volume_);
    settingsForm->addRow("输入偏移", offset_);
    settingsLayout->addLayout(settingsForm);
    auto *saveSettingsButton = button("保存", "primary");
    auto *settingsBack = button("返回");
    settingsLayout->addWidget(saveSettingsButton);
    settingsLayout->addWidget(settingsBack);
    settingsLayout->addStretch();
    pages_->addWidget(settingsPage);
    connect(saveSettingsButton, &QPushButton::clicked, this, &MainWindow::saveSettings);
    connect(settingsBack, &QPushButton::clicked, this, [this] { showPage(Home); });

    auto *helpPage = new QWidget;
    auto *helpLayout = new QVBoxLayout(helpPage);
    helpLayout->setContentsMargins(60, 45, 60, 45);
    helpLayout->addWidget(heading("操作与规则"));
    const JudgeConfig judge;
    auto *helpText = new QLabel(
        QStringLiteral("D / F / J / K 对应四条轨道。\n在判定线附近按下音符，长按音符保持到尾部。\n"
                       "Perfect ±%1ms，Great ±%2ms，Good ±%3ms；漏击会清零 Combo。\n"
                       "分数按 Perfect 1000、Great 700、Good 400 计算。\n"
                       "单人按 Esc 暂停，继续时请重新按住活动长按。联机失焦或 Esc 会中止本局。\n\n"
                       "初次游玩建议选择《小星星》Easy，每首歌曲都有三份独立难度谱面。")
            .arg(judge.perfectMs).arg(judge.greatMs).arg(judge.goodMs), helpPage);
    helpText->setWordWrap(true);
    helpLayout->addWidget(helpText);
    helpLayout->addStretch();
    auto *helpBack = button("返回");
    helpLayout->addWidget(helpBack);
    pages_->addWidget(helpPage);
    connect(helpBack, &QPushButton::clicked, this, [this] { showPage(Home); });
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

void MainWindow::startSolo() {
    lan_.leaveRoom();
    isLan_ = false;
    const QString name = nickname_->text().trimmed();
    if (name.isEmpty() || name.size() > 16) {
        showError("昵称应为 1—16 个字符");
        return;
    }
    settings_.playerName = name;
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
    next.playerName = settingsName_->text().trimmed();
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

void MainWindow::enterLan(bool host) {
    isLan_ = true;
    phase_ = Phase::Idle;
    roomLoadPending_ = false;
    suppressRoomChanges_ = true;
    roomSong_->setCurrentIndex(songBox_->currentIndex());
    roomDifficulty_->setCurrentText(difficultyBox_->currentText());
    suppressRoomChanges_ = false;
    showPage(Lobby);
    updateRoom();
    if (host) {
        const QString name = nickname_->text().trimmed();
        lan_.createRoom(static_cast<quint16>(port_->value()),
                        name.isEmpty() ? settings_.playerName : name);
    }
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
