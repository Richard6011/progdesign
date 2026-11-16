#include "ui/MainWindow.h"

#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFont>
#include <QFontDatabase>
#include <QLabel>
#include <QLineEdit>
#include <QLoggingCategory>
#include <QMessageBox>
#include <QPixmap>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <functional>
#include <iostream>

namespace nc {

// 真实媒体冒烟测试：只使用正常 UI 入口，不伪造媒体结束或推进游戏时钟。
class UiSmokeHarness {
public:
    static int run(const QString &evidenceDirectory) {
        QTemporaryDir userData;
        if (!userData.isValid()) {
            std::cerr << "FAIL: cannot create isolated user-data directory\n";
            return 1;
        }

        MainWindow window(nullptr, userData.path());
        int failures = 0;
        const auto check = [&](bool condition, const char *message) {
            if (!condition) {
                ++failures;
                std::cerr << "FAIL: " << message << '\n';
            }
            return condition;
        };
        const auto waitUntil = [](const std::function<bool()> &condition, int timeoutMs) {
            QElapsedTimer elapsed;
            elapsed.start();
            while (!condition() && elapsed.elapsed() < timeoutMs) {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
                QThread::msleep(5);
            }
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            return condition();
        };
        const auto processFor = [&](int durationMs) {
            QElapsedTimer elapsed;
            elapsed.start();
            waitUntil([&] { return elapsed.elapsed() >= durationMs; }, durationMs + 1000);
        };
        const auto capture = [&](const QString &name) {
            if (evidenceDirectory.isEmpty()) return true;
            QDir().mkpath(evidenceDirectory);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            return window.grab().save(QDir(evidenceDirectory).filePath(name));
        };

        // A modal failure must fail this test instead of leaving an unattended test blocked.
        QTimer closeUnexpectedDialogs;
        closeUnexpectedDialogs.setInterval(100);
        QObject::connect(&closeUnexpectedDialogs, &QTimer::timeout, &window, [&] {
            for (auto *widget : QApplication::topLevelWidgets()) {
                if (auto *dialog = qobject_cast<QMessageBox *>(widget)) {
                    ++failures;
                    std::cerr << "FAIL: unexpected dialog: " << dialog->text().toStdString() << '\n';
                    dialog->accept();
                }
            }
        });
        closeUnexpectedDialogs.start();

        int loadedCount = 0;
        int startedCount = 0;
        int endedCount = 0;
        QString audioError;
        QObject::connect(&window.audio_, &AudioService::loaded, &window, [&] { ++loadedCount; });
        QObject::connect(&window.audio_, &AudioService::started, &window, [&](qint64) { ++startedCount; });
        QObject::connect(&window.audio_, &AudioService::ended, &window, [&] { ++endedCount; });
        QObject::connect(&window.audio_, &AudioService::failed, &window,
                         [&](const QString &message) { audioError = message; });

        window.setAttribute(Qt::WA_ShowWithoutActivating);
        window.show();
        processFor(100);
        check(capture("ui-home.png"), "capture home screenshot");
        if (!check(!window.songs_.songs().empty() && window.songBox_->count() > 0,
                   "song index populates the song selector")) return 1;

        window.settings_.musicVolume = 0;
        window.nickname_->setText("冒烟测试");
        window.showPreparation();
        const int twinkleIndex = window.songBox_->findData("twinkle-star");
        if (!check(twinkleIndex >= 0, "Twinkle Star is available in the real song selector")) return 1;
        window.songBox_->setCurrentIndex(twinkleIndex);
        window.difficultyBox_->setCurrentText("Easy");
        window.startSolo();
        const QString firstSession = window.engine_.sessionId();
        if (!check(waitUntil([&] { return window.phase_ == MainWindow::Phase::Playing; }, 12000),
                   "single-player reaches actual audio Playing state")) {
            std::cerr << "Audio error: " << audioError.toStdString() << '\n';
            std::cerr << "Status: " << window.globalStatus_->text().toStdString() << '\n';
            return 1;
        }
        check(loadedCount == 1 && startedCount >= 1, "Loaded and actual Started signals are observed");
        check(window.audio_.positionMs() >= 0 && window.clock_.running(), "actual playback starts the game clock");
        check(window.engine_.activeHolds().empty(), "pause starts without active holds");

        window.togglePause();
        check(window.phase_ == MainWindow::Phase::Paused, "pause enters Paused");
        const qint64 frozenTime = window.clock_.nowMs();
        const int frozenCount = window.engine_.stats().finalizedCount;
        processFor(300);
        check(window.clock_.nowMs() == frozenTime && window.engine_.stats().finalizedCount == frozenCount,
              "pause freezes time and judgements");
        window.togglePause();
        check(waitUntil([&] { return window.phase_ == MainWindow::Phase::Playing; }, 3000),
              "pause with no active hold resumes immediately");
        processFor(200);
        check(window.clock_.nowMs() > frozenTime && startedCount >= 2, "resumed media restarts the clock");
        check(endedCount == 0, "pause and resume do not synthesize EndOfMedia");

        window.startSolo();
        if (!check(waitUntil([&] { return window.phase_ == MainWindow::Phase::Playing; }, 12000),
                   "restarting the same song reloads and plays")) return 1;
        // beginCountdown creates the session that owns this actual performance.
        const QString completedSession = window.engine_.sessionId();
        check(completedSession != firstSession, "restart creates a new session id");
        check(loadedCount >= 2 && startedCount >= 3, "same-song restart emits Loaded and Started again");
        check(window.engine_.stats().finalizedCount == 0, "restart clears previous judgements");
        // Wait into the note approach window so the screenshot includes the actual four-lane game.
        check(waitUntil([&] { return window.clock_.nowMs() >= 1800; }, 3500), "real audio clock advances");
        check(capture("ui-game.png"), "capture game screenshot");

        if (!check(waitUntil([&] { return window.phase_ == MainWindow::Phase::Result; }, 42000),
                   "Twinkle Star media reaches natural EndOfMedia and the result page")) {
            std::cerr << "Audio error: " << audioError.toStdString() << '\n';
            std::cerr << "Status: " << window.globalStatus_->text().toStdString() << '\n';
            return 1;
        }
        const int noteCount = static_cast<int>(window.chart_.notes.size());
        check(endedCount == 1, "one real natural EndOfMedia signal is observed");
        check(audioError.isEmpty(), "the real audio player reports no errors");
        check(window.mediaEnded_ && window.engine_.finished() && window.finalRecord_.result.complete,
              "formal completion requires natural media end and all notes finalized");
        check(window.finalRecord_.stats.miss == noteCount && window.finalRecord_.stats.finalizedCount == noteCount &&
                  window.finalRecord_.result.score == 0,
              "no-input run produces exactly one Miss per chart note and zero score");
        check(window.finalRecord_.mode == GameMode::Solo && window.finalRecord_.matchId.isEmpty() &&
                  window.finalRecord_.playerId.isEmpty(), "Solo result does not carry LAN identity");
        check(window.saved_ && window.records_.records().size() == 1, "complete result is automatically saved once");
        check(window.finalRecord_.sessionId == completedSession, "saved result belongs to restarted session");
        check(QFile::exists(window.records_.filePath()), "records file exists in isolated data directory");
        check(capture("ui-result.png"), "capture result screenshot");

        RecordRepository reopened(userData.path());
        QString recordError;
        check(reopened.load(&recordError), "saved result can be read by a fresh repository");
        const auto ranking = reopened.queryRanking(window.finalRecord_);
        check(ranking.size() == 1 && ranking[0].rank == 1 && ranking[0].record.playerName == "冒烟测试",
              "saved Solo score appears in the correct leaderboard");
        window.rankMode_->setCurrentIndex(0);
        window.rankSong_->setCurrentIndex(window.rankSong_->findData("twinkle-star"));
        window.rankDifficulty_->setCurrentText("Easy");
        window.refreshRanking();
        check(window.ranking_->rowCount() == 1 && window.ranking_->item(0, 2)->text() == "0",
              "leaderboard UI displays the saved score");
        window.audio_.stop();
        window.hide();
        std::cout << (failures ? "UI smoke tests failed: " : "UI smoke tests passed: ") << failures
                  << " (Loaded=" << loadedCount << ", Started=" << startedCount << ", Ended=" << endedCount << ")\n";
        return failures ? 1 : 0;
    }
};

} // namespace nc

int main(int argc, char **argv) {
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");
    QLoggingCategory::setFilterRules("qt.multimedia.ffmpeg.*=false\nqt.multimedia.*.debug=false");
    QApplication app(argc, argv);
    // offscreen on Windows may omit the native font database; load a system font for QA images.
    for (const auto &fontPath : {QString("C:/Windows/Fonts/msyh.ttc"),
                                 QString("C:/Windows/Fonts/simhei.ttf")}) {
        const int fontId = QFontDatabase::addApplicationFont(fontPath);
        const auto families = QFontDatabase::applicationFontFamilies(fontId);
        if (!families.isEmpty()) {
            app.setFont(QFont(families.front(), 10));
            break;
        }
    }
    app.setQuitOnLastWindowClosed(false);
    QCoreApplication::setApplicationName("NoteCollisionUiSmoke");
    QCoreApplication::setOrganizationName("NoteCollisionTests");
    const QString evidenceDirectory = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();
    return nc::UiSmokeHarness::run(evidenceDirectory);
}
