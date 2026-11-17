#include "MainWindow.h"
#include "ui/GameWidget.h"

#include <QApplication>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTableWidget>
#include <QVBoxLayout>

namespace nc {
namespace {

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

QVBoxLayout *pageLayout(QStackedWidget *pages, int horizontal = 60, int vertical = 45) {
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(horizontal, vertical, horizontal, vertical);
    pages->addWidget(page);
    return layout;
}

} // namespace

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

    buildHomePage();
    buildPreparePage();
    buildLobbyPage();
    buildGamePage();
    buildResultPage();
    buildRankingPage();
    buildSettingsPage();
    buildHelpPage();
}

void MainWindow::buildHomePage() {
    auto *homeLayout = pageLayout(pages_, 72, 48);
    homeLayout->setSpacing(15);
    homeLayout->addWidget(heading("音符碰撞", 38));
    auto *subtitle = new QLabel("四轨节奏挑战 · D / F / J / K");
    subtitle->setObjectName("muted");
    homeLayout->addWidget(subtitle);
    if (!accountName_.isEmpty())
        homeLayout->addWidget(new QLabel(QStringLiteral("当前账号：%1").arg(accountName_)));
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
    connect(start, &QPushButton::clicked, this, &MainWindow::showPreparation);
    connect(lan, &QPushButton::clicked, this, [this] { enterLan(); });
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
}

void MainWindow::buildPreparePage() {
    auto *prepareLayout = pageLayout(pages_, 60, 42);
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
    form->addRow(accountName_.isEmpty() ? "昵称" : "当前账号", nickname_);
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
    connect(songBox_, &QComboBox::currentIndexChanged, this, &MainWindow::updatePreview);
    connect(difficultyBox_, &QComboBox::currentTextChanged, this, &MainWindow::updatePreview);
    connect(modeBox_, &QComboBox::currentTextChanged, this, [this](const QString &mode) {
        soloStart_->setText(mode == "双人联机" ? "进入联机房间" : "开始演奏");
    });
    connect(soloStart_, &QPushButton::clicked, this, [this] {
        if (modeBox_->currentIndex() == 0)
            startSolo();
        else
            enterLan();
    });
    connect(back, &QPushButton::clicked, this, [this] { showPage(Home); });
    for (const auto &song : songs_.songs())
        songBox_->addItem(song.title, song.id);
    updatePreview();
}

void MainWindow::buildLobbyPage() {
    auto *lobbyLayout = pageLayout(pages_, 50, 35);
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
    for (const auto &song : songs_.songs())
        roomSong_->addItem(song.title, song.id);
    connect(host, &QPushButton::clicked, this, [this] {
        if (lan_.state() != LanSessionService::State::Idle &&
            lan_.state() != LanSessionService::State::Aborted)
            return;
        if (!updatePlayerName())
            return;
        const QString &name = settings_.playerName;
        if (!lan_.createRoom(static_cast<quint16>(port_->value()), name))
            showError("创建房间失败");
        else
            applyRoomSelection();
    });
    connect(join, &QPushButton::clicked, this, [this] {
        if (lan_.state() != LanSessionService::State::Idle &&
            lan_.state() != LanSessionService::State::Aborted)
            return;
        if (!updatePlayerName())
            return;
        const QString &name = settings_.playerName;
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
}

void MainWindow::buildGamePage() {
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
}

void MainWindow::buildResultPage() {
    auto *resultLayout = pageLayout(pages_);
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
}

void MainWindow::buildRankingPage() {
    auto *rankLayout = pageLayout(pages_, 40, 35);
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
    connect(rankRefresh, &QPushButton::clicked, this, &MainWindow::refreshRanking);
    connect(rankMode_, &QComboBox::currentTextChanged, this, &MainWindow::refreshRanking);
    connect(rankSong_, &QComboBox::currentIndexChanged, this, &MainWindow::refreshRanking);
    connect(rankDifficulty_, &QComboBox::currentTextChanged, this, &MainWindow::refreshRanking);
    connect(rankBack, &QPushButton::clicked, this, [this] { showPage(Home); });
    for (const auto &song : songs_.songs())
        rankSong_->addItem(song.title, song.id);
}

void MainWindow::buildSettingsPage() {
    auto *settingsLayout = pageLayout(pages_);
    settingsLayout->addWidget(heading("设置"));
    auto *settingsForm = new QFormLayout;
    settingsName_ = new QLineEdit;
    volume_ = new QSlider(Qt::Horizontal);
    volume_->setRange(0, 100);
    offset_ = new QSpinBox;
    offset_->setRange(-200, 200);
    offset_->setSuffix(" ms");
    settingsForm->addRow(accountName_.isEmpty() ? "昵称" : "当前账号", settingsName_);
    settingsForm->addRow("音乐音量", volume_);
    settingsForm->addRow("输入偏移", offset_);
    settingsLayout->addLayout(settingsForm);
    auto *saveSettingsButton = button("保存", "primary");
    auto *settingsBack = button("返回");
    settingsLayout->addWidget(saveSettingsButton);
    settingsLayout->addWidget(settingsBack);
    settingsLayout->addStretch();
    connect(saveSettingsButton, &QPushButton::clicked, this, &MainWindow::saveSettings);
    connect(settingsBack, &QPushButton::clicked, this, [this] { showPage(Home); });
}

void MainWindow::buildHelpPage() {
    auto *helpLayout = pageLayout(pages_);
    helpLayout->addWidget(heading("操作与规则"));
    const JudgeConfig judge;
    auto *helpText = new QLabel(
        QStringLiteral("D / F / J / K 对应四条轨道。\n在判定线附近按下音符，长按音符保持到尾部。\n"
                       "Perfect ±%1ms，Great ±%2ms，Good ±%3ms；漏击会清零 Combo。\n"
                       "分数按 Perfect 1000、Great 700、Good 400 计算。\n"
                       "单人按 Esc 暂停，继续时请重新按住活动长按。联机失焦或 Esc 会中止本局。\n\n"
                       "初次游玩建议选择《小星星》Easy，每首歌曲都有三份独立难度谱面。")
            .arg(judge.perfectMs).arg(judge.greatMs).arg(judge.goodMs));
    helpText->setWordWrap(true);
    helpLayout->addWidget(helpText);
    helpLayout->addStretch();
    auto *helpBack = button("返回");
    helpLayout->addWidget(helpBack);
    connect(helpBack, &QPushButton::clicked, this, [this] { showPage(Home); });
}

} // namespace nc
