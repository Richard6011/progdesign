#pragma once

#include "audio/AudioService.h"
#include "core/GameEngine.h"
#include "core/SongClock.h"
#include "network/LanSessionService.h"
#include "storage/RecordRepository.h"
#include "storage/SettingsRepository.h"
#include "storage/SongRepository.h"

#include <QMainWindow>
#include <QTimer>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSlider;
class QSpinBox;
class QStackedWidget;
class QTableWidget;
class QCloseEvent;

namespace nc {
class GameWidget;

// 页面与本局生命周期协调；判定、网络和文件读写分别交给独立服务。
class MainWindow : public QMainWindow {
    Q_OBJECT
  public:
    explicit MainWindow(QWidget *parent = nullptr, const QString &dataDirectory = {});
    ~MainWindow() override = default;

  protected:
    void changeEvent(QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

  private:
    friend class UiSmokeHarness;
    enum Page { Home, Prepare, Lobby, Game, Result, Ranking, Settings, Help };
    enum class Phase {
        Idle,
        Loading,
        Countdown,
        Starting,
        Playing,
        Paused,
        ResumeReady,
        Waiting,
        Result
    };

    void buildUi();
    void showPage(Page page);
    void showPreparation();
    bool loadSelection(int songIndex, const QString &difficulty);
    void updatePreview();
    void startSolo();
    void enterLan(bool host);
    void loadRoomConfig(const QJsonObject &config);
    QJsonObject resourceConfig() const;
    void applyRoomSelection();
    void updateRoom();
    void onAudioLoaded();
    void beginCountdown(qint64 targetMs, const QString &matchId = {});
    void tick();
    void onLanePress(int lane);
    void onLaneRelease(int lane);
    void togglePause();
    void tryResume();
    void finishIfReady();
    void abortRun(const QString &reason, bool notifyLan = true);
    void backToPreparation();
    PlayRecord makeRecord() const;
    void showResult(const QString &headline);
    void saveResult();
    void refreshRanking();
    void saveSettings();
    void showError(const QString &message);

    SongRepository songs_;
    SettingsRepository settingsRepository_;
    RecordRepository records_;
    AppSettings settings_;
    AudioService audio_;
    LanSessionService lan_;
    GameEngine engine_;
    SongClock clock_;
    Chart chart_;
    SongInfo song_;
    PlayStats peerStats_;
    PlayRecord finalRecord_;
    bool isLan_ = false;
    bool mediaEnded_ = false;
    bool finishSubmitted_ = false;
    bool saved_ = false;
    bool roomLoadPending_ = false;
    bool suppressRoomChanges_ = false;
    bool resuming_ = false;
    Phase phase_ = Phase::Idle;
    qint64 targetMs_ = 0;
    qint64 phaseSinceMs_ = 0;
    QTimer timer_;

    QStackedWidget *pages_ = nullptr;
    QLabel *globalStatus_ = nullptr;
    QComboBox *songBox_ = nullptr;
    QComboBox *difficultyBox_ = nullptr;
    QComboBox *modeBox_ = nullptr;
    QLineEdit *nickname_ = nullptr;
    QLineEdit *address_ = nullptr;
    QSpinBox *port_ = nullptr;
    QLabel *preview_ = nullptr;
    QPushButton *soloStart_ = nullptr;
    QComboBox *roomSong_ = nullptr;
    QComboBox *roomDifficulty_ = nullptr;
    QLabel *roomInfo_ = nullptr;
    QLabel *roomStatus_ = nullptr;
    QPushButton *ready_ = nullptr;
    QPushButton *roomStart_ = nullptr;
    GameWidget *game_ = nullptr;
    QLabel *score_ = nullptr;
    QLabel *peerScore_ = nullptr;
    QPushButton *pause_ = nullptr;
    QPushButton *restart_ = nullptr;
    QLabel *resultSummary_ = nullptr;
    QLabel *saveStatus_ = nullptr;
    QPushButton *saveButton_ = nullptr;
    QPushButton *retry_ = nullptr;
    QComboBox *rankSong_ = nullptr;
    QComboBox *rankDifficulty_ = nullptr;
    QComboBox *rankMode_ = nullptr;
    QTableWidget *ranking_ = nullptr;
    QLineEdit *settingsName_ = nullptr;
    QSlider *volume_ = nullptr;
    QSpinBox *offset_ = nullptr;
};

} // namespace nc
