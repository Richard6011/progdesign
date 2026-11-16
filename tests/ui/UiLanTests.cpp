#include "ui/MainWindow.h"

#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QLoggingCategory>
#include <QMessageBox>
#include <QPushButton>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>

#include <functional>
#include <iostream>

namespace nc {

// An independent executable deliberately shares the friend name used by UiSmokeTests.
// Both clients use actual local audio and real TCP, with isolated persistent storage.
class UiSmokeHarness {
public:
    static int run()
    {
        QTemporaryDir hostData;
        QTemporaryDir guestData;
        if (!hostData.isValid() || !guestData.isValid()) {
            std::cerr << "FAIL isolated LAN data directories could not be created\n";
            return 1;
        }
        MainWindow host(nullptr, hostData.path());
        MainWindow guest(nullptr, guestData.path());
        int failures = 0;
        const auto check = [&](const bool condition, const char* description) {
            std::cout << (condition ? "PASS " : "FAIL ") << description << std::endl;
            failures += condition ? 0 : 1;
            return condition;
        };
        const auto waitUntil = [](const std::function<bool()>& condition, const int timeoutMs) {
            QElapsedTimer elapsed;
            elapsed.start();
            while (elapsed.elapsed() < timeoutMs) {
                if (condition()) {
                    return true;
                }
                QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
                QThread::msleep(5);
            }
            return condition();
        };
        const auto reportFailureState = [&] {
            std::cerr << "HOST phase=" << static_cast<int>(host.phase_)
                      << " lan=" << static_cast<int>(host.lan_.state())
                      << " status=" << host.globalStatus_->text().toStdString()
                      << " room=" << host.roomStatus_->text().toStdString() << '\n';
            std::cerr << "GUEST phase=" << static_cast<int>(guest.phase_)
                      << " lan=" << static_cast<int>(guest.lan_.state())
                      << " status=" << guest.globalStatus_->text().toStdString()
                      << " room=" << guest.roomStatus_->text().toStdString() << '\n';
        };

        QTimer closeUnexpectedDialogs;
        closeUnexpectedDialogs.setInterval(100);
        QObject::connect(&closeUnexpectedDialogs, &QTimer::timeout, &host, [&] {
            for (auto* widget : QApplication::topLevelWidgets()) {
                if (auto* dialog = qobject_cast<QMessageBox*>(widget)) {
                    ++failures;
                    std::cerr << "FAIL unexpected modal dialog: " << dialog->text().toStdString() << '\n';
                    dialog->accept();
                }
            }
        });
        closeUnexpectedDialogs.start();

        int hostLoaded = 0;
        int guestLoaded = 0;
        int hostStarted = 0;
        int guestStarted = 0;
        int hostEnded = 0;
        int guestEnded = 0;
        int hostConfirmed = 0;
        int guestConfirmed = 0;
        int hostAborted = 0;
        int guestAborted = 0;
        QString hostAudioError;
        QString guestAudioError;
        QObject::connect(&host.audio_, &AudioService::loaded, &host, [&] { ++hostLoaded; });
        QObject::connect(&guest.audio_, &AudioService::loaded, &guest, [&] { ++guestLoaded; });
        QObject::connect(&host.audio_, &AudioService::started, &host, [&](qint64) { ++hostStarted; });
        QObject::connect(&guest.audio_, &AudioService::started, &guest, [&](qint64) { ++guestStarted; });
        QObject::connect(&host.audio_, &AudioService::ended, &host, [&] { ++hostEnded; });
        QObject::connect(&guest.audio_, &AudioService::ended, &guest, [&] { ++guestEnded; });
        QObject::connect(&host.audio_, &AudioService::failed, &host,
            [&](const QString& error) { hostAudioError = error; });
        QObject::connect(&guest.audio_, &AudioService::failed, &guest,
            [&](const QString& error) { guestAudioError = error; });
        QObject::connect(&host.lan_, &LanSessionService::resultConfirmed, &host,
            [&](const PlayStats&, const PlayStats&, const QString&, const QString&) { ++hostConfirmed; });
        QObject::connect(&guest.lan_, &LanSessionService::resultConfirmed, &guest,
            [&](const PlayStats&, const PlayStats&, const QString&, const QString&) { ++guestConfirmed; });
        QObject::connect(&host.lan_, &LanSessionService::aborted, &host,
            [&](const QString&) { ++hostAborted; });
        QObject::connect(&guest.lan_, &LanSessionService::aborted, &guest,
            [&](const QString&) { ++guestAborted; });

        // Hidden offscreen windows avoid manufacturing WindowDeactivate events between clients.
        host.settings_.musicVolume = guest.settings_.musicVolume = 0;
        host.settings_.playerName = QStringLiteral("界面房主");
        guest.settings_.playerName = QStringLiteral("界面客人");
        host.nickname_->setText(host.settings_.playerName);
        guest.nickname_->setText(guest.settings_.playerName);
        host.difficultyBox_->setCurrentText("Normal");
        guest.difficultyBox_->setCurrentText("Normal");
        if (!check(!host.songs_.songs().empty() && !guest.songs_.songs().empty(),
                "both UI clients load the real song index")) {
            return 1;
        }
        host.enterLan(false);
        guest.enterLan(false);
        if (!check(host.lan_.createRoom(0, host.settings_.playerName), "UI host creates a TCP room")) {
            return 1;
        }
        host.applyRoomSelection();
        if (!check(guest.lan_.joinRoom("127.0.0.1", host.lan_.listeningPort(), guest.settings_.playerName),
                "UI guest joins through real loopback TCP")) {
            return 1;
        }
        if (!check(waitUntil([&] {
                return hostLoaded >= 1 && guestLoaded >= 1 && !host.roomLoadPending_
                    && !guest.roomLoadPending_ && host.audio_.ready() && guest.audio_.ready()
                    && host.lan_.configRevision() == guest.lan_.configRevision();
            }, 12000), "both UI clients preload real media through configReceived")) {
            reportFailureState();
            return 1;
        }
        check(host.resourceConfig() == guest.resourceConfig() && host.resourceConfig() == host.lan_.config(),
            "both clients verify matching chart, audio and rules resources");
        if (!check(host.lan_.setReady(true) && guest.lan_.setReady(true),
                "actual preloaded media permits both clients to prepare")) {
            reportFailureState();
            return 1;
        }
        if (!check(waitUntil([&] { return host.lan_.peerReady() && guest.lan_.peerReady(); }, 3000),
                "both ready states reach the UI services")
            || !check(host.lan_.requestStart(), "UI host requests the synchronized start")) {
            reportFailureState();
            return 1;
        }
        if (!check(waitUntil([&] {
                return host.phase_ == MainWindow::Phase::Playing && guest.phase_ == MainWindow::Phase::Playing
                    && host.lan_.state() == LanSessionService::State::Playing
                    && guest.lan_.state() == LanSessionService::State::Playing;
            }, 10000), "real AudioService Started transitions both LAN UI clients into Playing")) {
            reportFailureState();
            return 1;
        }
        const QString firstMatch = host.lan_.matchId();
        const QString hostSession = host.engine_.sessionId();
        const QString guestSession = guest.engine_.sessionId();
        check(!firstMatch.isEmpty() && firstMatch == guest.lan_.matchId(), "both UI clients share one match ID");
        check(hostStarted == 1 && guestStarted == 1 && host.clock_.running() && guest.clock_.running(),
            "actual audio starts each local monotonic game clock once");
        check(host.records_.records().empty() && guest.records_.records().empty(),
            "uncompleted LAN matches have no formal saved records");

        if (!check(waitUntil([&] {
                return host.phase_ == MainWindow::Phase::Result && guest.phase_ == MainWindow::Phase::Result
                    && hostConfirmed == 1 && guestConfirmed == 1;
            }, 43000), "both UI clients naturally finish the full song and confirm the final result")) {
            reportFailureState();
            return 1;
        }
        check(hostAudioError.isEmpty() && guestAudioError.isEmpty(), "real players report no audio error");
        check(hostEnded == 1 && guestEnded == 1, "each player reports one natural EndOfMedia");
        check(hostAborted == 0 && guestAborted == 0, "the complete LAN run does not enter an abort flow");
        check(host.finalRecord_.mode == GameMode::Lan && guest.finalRecord_.mode == GameMode::Lan
                && host.finalRecord_.matchId == firstMatch && guest.finalRecord_.matchId == firstMatch
                && host.finalRecord_.playerId == "host" && guest.finalRecord_.playerId == "guest",
            "confirmed UI records preserve LAN identity and local ownership");
        check(host.finalRecord_.sessionId == hostSession && guest.finalRecord_.sessionId == guestSession
                && host.finalRecord_.resultConfirmed && guest.finalRecord_.resultConfirmed
                && host.finalRecord_.resultId == guest.finalRecord_.resultId,
            "UI records use the completed sessions and shared confirmed result ID");
        check(host.finalRecord_.winResult == "Draw" && guest.finalRecord_.winResult == "Draw"
                && host.finalRecord_.result.score == 0 && guest.finalRecord_.result.score == 0,
            "equal no-input scores produce a confirmed draw");
        check(host.finalRecord_.stats.miss == static_cast<int>(host.chart_.notes.size())
                && guest.finalRecord_.stats.miss == static_cast<int>(guest.chart_.notes.size()),
            "each real no-input play produces one Miss per note");
        check(host.saved_ && guest.saved_ && host.records_.records().size() == 1
                && guest.records_.records().size() == 1,
            "each isolated client automatically saves exactly its own confirmed record");
        host.saveResult();
        guest.saveResult();
        check(host.records_.records().size() == 1 && guest.records_.records().size() == 1,
            "repeated UI save requests remain idempotent");
        check(host.records_.filePath() != guest.records_.filePath()
                && QFile::exists(host.records_.filePath()) && QFile::exists(guest.records_.filePath()),
            "host and guest records are persisted in separate temporary directories");

        RecordRepository reopenedHost(hostData.path());
        RecordRepository reopenedGuest(guestData.path());
        QString loadError;
        check(reopenedHost.load(&loadError) && reopenedGuest.load(&loadError)
                && reopenedHost.records().size() == 1 && reopenedGuest.records().size() == 1,
            "fresh repositories can reopen both confirmed LAN records");
        const auto hostRanking = reopenedHost.queryRanking(host.finalRecord_);
        const auto guestRanking = reopenedGuest.queryRanking(guest.finalRecord_);
        check(hostRanking.size() == 1 && guestRanking.size() == 1
                && hostRanking[0].record.playerName == host.settings_.playerName
                && guestRanking[0].record.playerName == guest.settings_.playerName,
            "each LAN leaderboard contains the local player's own saved score");
        PlayRecord soloKey = host.finalRecord_;
        soloKey.mode = GameMode::Solo;
        check(reopenedHost.queryRanking(soloKey).empty() && reopenedGuest.queryRanking(soloKey).empty(),
            "LAN records do not appear in the Solo leaderboard");

        host.retry_->click();
        guest.retry_->click();
        check(host.pages_->currentIndex() == MainWindow::Lobby
                && guest.pages_->currentIndex() == MainWindow::Lobby,
            "the result-page rematch buttons return both players to the room");
        // Reapply the same song through the host selection operation to require fresh preload signals.
        const int hostLoadedBefore = hostLoaded;
        const int guestLoadedBefore = guestLoaded;
        const int previousRevision = host.lan_.configRevision();
        host.applyRoomSelection();
        if (!check(waitUntil([&] {
                return hostLoaded > hostLoadedBefore && guestLoaded > guestLoadedBefore
                    && !host.roomLoadPending_ && !guest.roomLoadPending_
                    && host.lan_.configRevision() > previousRevision
                    && host.lan_.configRevision() == guest.lan_.configRevision();
            }, 12000), "same-song rematch emits fresh Loaded signals in both UI clients")) {
            reportFailureState();
            return 1;
        }
        check(!host.lan_.localReady() && !guest.lan_.localReady()
                && !host.lan_.peerReady() && !guest.lan_.peerReady(),
            "same-song reconfiguration clears both ready flags");
        if (!check(host.lan_.setReady(true) && guest.lan_.setReady(true), "rematch prepares both clients")
            || !check(waitUntil([&] { return host.lan_.peerReady() && guest.lan_.peerReady(); }, 3000),
                "rematch ready flags synchronize")
            || !check(host.lan_.requestStart(), "rematch requests a new start plan")
            || !check(waitUntil([&] {
                return host.phase_ == MainWindow::Phase::Countdown
                    && guest.phase_ == MainWindow::Phase::Countdown;
            }, 4000), "rematch reaches both UI countdowns")) {
            reportFailureState();
            return 1;
        }
        check(!host.lan_.matchId().isEmpty() && host.lan_.matchId() == guest.lan_.matchId()
                && host.lan_.matchId() != firstMatch && host.engine_.sessionId() != hostSession
                && guest.engine_.sessionId() != guestSession,
            "rematch uses fresh shared match and local session IDs");
        check(host.engine_.stats().finalizedCount == 0 && guest.engine_.stats().finalizedCount == 0
                && host.clock_.nowMs() == 0 && guest.clock_.nowMs() == 0,
            "rematch countdown starts with fresh statistics and frozen zero clocks");

        guest.backToPreparation();
        check(waitUntil([&] {
            return host.phase_ == MainWindow::Phase::Idle
                && host.lan_.state() == LanSessionService::State::Aborted;
        }, 3000), "disconnect during UI countdown aborts the unfinished rematch");
        check(hostConfirmed == 1 && guestConfirmed == 1
                && host.records_.records().size() == 1 && guest.records_.records().size() == 1,
            "aborted rematch neither confirms nor saves another formal result");
        check(hostStarted == 1 && guestStarted == 1, "countdown disconnect prevents a second audio start");

        host.audio_.stop();
        guest.audio_.stop();
        host.lan_.leaveRoom();
        std::cout << "UI LAN failures=" << failures << " Loaded=" << hostLoaded << '/' << guestLoaded
                  << " Started=" << hostStarted << '/' << guestStarted
                  << " Ended=" << hostEnded << '/' << guestEnded << std::endl;
        return failures == 0 ? 0 : 1;
    }
};
} // namespace nc

int main(int argc, char** argv)
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    QLoggingCategory::setFilterRules("qt.multimedia.ffmpeg.*=false\nqt.multimedia.*.debug=false");
    QApplication application(argc, argv);
    application.setQuitOnLastWindowClosed(false);
    QCoreApplication::setApplicationName("NoteCollisionUiLan");
    QCoreApplication::setOrganizationName("NoteCollisionTests");
    return nc::UiSmokeHarness::run();
}
