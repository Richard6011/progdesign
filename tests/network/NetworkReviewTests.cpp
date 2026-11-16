#include "network/LanSessionService.h"
#include "network/ProtocolCodec.h"
#include "core/ScoreService.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonObject>
#include <QThread>
#include <QTimer>

#include <functional>
#include <iostream>

using namespace nc;

namespace {
int failures = 0;

void check(const bool condition, const char* description)
{
    std::cout << (condition ? "PASS " : "FAIL ") << description << std::endl;
    failures += condition ? 0 : 1;
}

bool until(const std::function<bool()>& condition, const int timeoutMs = 2000)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        if (condition()) {
            return true;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(1);
    }
    return condition();
}

QJsonObject roomConfig()
{
    return {{"songId", "demo"}, {"chartId", "demo-normal"}, {"chartVersion", 1},
        {"difficulty", "Normal"}, {"chartHash", QString(64, 'a')}, {"audioHash", QString(64, 'b')},
        {"rulesVersion", JudgeConfig{}.rulesVersion}, {"noteCount", 6}};
}

QJsonObject statistics(const PlayStats& stats)
{
    return {{"perfect", stats.perfect}, {"great", stats.great}, {"good", stats.good},
        {"miss", stats.miss}, {"earnedUnits", stats.earnedUnits}, {"combo", stats.combo},
        {"maxCombo", stats.maxCombo}, {"finalizedCount", stats.finalizedCount}};
}

struct RawGuest {
    explicit RawGuest(LanSessionService& service) : host(service) {}

    bool connect()
    {
        if (!host.createRoom(0, QStringLiteral("房主")) || !host.setRoomConfig(roomConfig())) {
            return false;
        }
        host.setLocalResources(roomConfig(), true);
        socket.connectToHost(QHostAddress::LocalHost, host.listeningPort());
        if (!until([this] { return socket.state() == QAbstractSocket::ConnectedState; })) {
            return false;
        }
        send("HELLO", {{"nickname", QStringLiteral("审查客人")}}, QString{}, QString{}, 0);
        return until([this] { return host.peerName() == QStringLiteral("审查客人"); });
    }

    void send(const QString& type, const QJsonObject& payload,
        const QString& room, const QString& match, const int revision)
    {
        const QJsonObject message{{"protocolVersion", 1}, {"type", type}, {"roomId", room},
            {"matchId", match}, {"seq", ++sequence}, {"configRevision", revision}, {"payload", payload}};
        socket.write(ProtocolCodec::encode(message));
    }

    void send(const QString& type, const QJsonObject& payload = {})
    {
        send(type, payload, host.roomId(), host.matchId(), host.configRevision());
    }

    QList<QJsonObject> drain()
    {
        QList<QJsonObject> messages;
        QString error;
        if (socket.bytesAvailable() > 0 && !codec.feed(socket.readAll(), messages, &error)) {
            check(false, "host frame remains decodable");
        }
        return messages;
    }

    bool receive(const QString& type, QJsonObject& result, const int timeoutMs = 2000)
    {
        return until([&] {
            for (const auto& message : drain()) {
                if (message.value("type").toString() == type) {
                    result = message;
                    return true;
                }
            }
            return false;
        }, timeoutMs);
    }

    LanSessionService& host;
    QTcpSocket socket;
    ProtocolCodec codec;
    int sequence = 0;
};

void testStaleConfigurationAndResourceRetry()
{
    LanSessionService host;
    RawGuest guest(host);
    check(guest.connect(), "raw guest joins a configured room");
    int aborted = 0;
    QObject::connect(&host, &LanSessionService::aborted, [&](const QString&) { ++aborted; });

    const int oldRevision = host.configRevision();
    auto nextConfig = roomConfig();
    nextConfig["chartVersion"] = 2;
    host.setRoomConfig(nextConfig);
    host.setLocalResources(nextConfig, true);
    guest.send("READY", {{"ready", true}, {"clockReady", false}, {"resources", roomConfig()}},
        host.roomId(), "", oldRevision);
    until([&] { guest.drain(); return false; }, 50);
    check(!host.peerReady() && aborted == 0, "old config READY is ignored without disconnecting");

    auto mismatch = nextConfig;
    mismatch["audioHash"] = QString(64, 'c');
    guest.send("READY", {{"ready", true}, {"clockReady", false}, {"resources", mismatch}});
    until([&] { guest.drain(); return false; }, 50);
    check(!host.peerReady() && aborted == 0, "mismatched resources block readiness and keep room open");

    guest.send("READY", {{"ready", true}, {"clockReady", false}, {"resources", nextConfig}});
    check(until([&] { return host.peerReady(); }), "corrected resource READY succeeds on the same connection");
    guest.send("FINISH", {{"stats", QJsonObject{}}}, host.roomId(), "old-match", host.configRevision());
    until([&] { guest.drain(); return false; }, 50);
    check(aborted == 0 && host.peerReady(), "old match FINISH cannot alter current room");
}

void testRepeatedAcknowledgementAndConfirmedDisconnect()
{
    LanSessionService host;
    RawGuest guest(host);
    check(guest.connect(), "result review guest connects");
    int confirmed = 0;
    int aborted = 0;
    QObject::connect(&host, &LanSessionService::resultConfirmed,
        [&](const PlayStats&, const PlayStats&, const QString&, const QString&) { ++confirmed; });
    QObject::connect(&host, &LanSessionService::aborted, [&](const QString&) { ++aborted; });
    check(host.setReady(true), "result review host prepares");
    guest.send("READY", {{"ready", true}, {"clockReady", false}, {"resources", roomConfig()}});
    check(until([&] { return host.peerReady(); }), "result review raw guest prepares");
    for (int i = 0; i < 5; ++i) {
        guest.send("CLOCK_PING", {{"t1", 1000 + i}});
    }
    guest.send("READY", {{"ready", true}, {"clockReady", true}, {"resources", roomConfig()}});
    QJsonObject observed;
    until([&] {
        for (const auto& message : guest.drain()) {
            if (message.value("type").toString() == "ROOM_STATE"
                && message.value("payload").toObject().value("guestReady").toBool()) {
                observed = message;
            }
        }
        return !observed.isEmpty();
    });
    // Ensure the final READY is processed before asking the host to create its plan.
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    check(host.requestStart(), "result review host creates a fresh start plan");
    QJsonObject arm;
    check(guest.receive("START_ARM", arm), "raw guest receives start plan");
    const qint64 target = static_cast<qint64>(arm.value("payload").toObject().value("startHostMs").toDouble());
    QObject::connect(&host, &LanSessionService::startScheduled, [&](qint64 targetMs, const QString&) {
        QTimer::singleShot(qMax<qint64>(0, targetMs - host.localNowMs()), &host, [&] {
            host.reportStarted();
            guest.send("STARTED", {{"actualLocalMs", target}, {"deviationMs", 0}});
        });
    });
    guest.send("ARMED", {{"startHostMs", target}});
    check(until([&] { return host.state() == LanSessionService::State::Playing; }, 5000),
        "raw guest and host commit a scheduled start");
    // Let the STARTED message emitted in the scheduled callback reach the host.
    until([&] { guest.drain(); return false; }, 50);

    PlayStats perfect;
    for (int i = 0; i < 6; ++i) {
        ScoreService::applyJudgement(perfect, Judgement::Perfect);
    }
    guest.send("FINISH", {{"stats", statistics(perfect)}, {"resources", roomConfig()}});
    check(host.submitFinish(perfect), "host submits valid complete statistics");
    QJsonObject result;
    check(guest.receive("MATCH_RESULT", result), "host issues a verified match result");
    const auto resultId = result.value("payload").toObject().value("resultId");
    guest.send("RESULT_ACK", {{"resultId", resultId}});
    check(until([&] { return confirmed == 1; }), "first ACK confirms exactly one host result");
    guest.send("RESULT_ACK", {{"resultId", resultId}});
    QJsonObject confirmation;
    check(guest.receive("RESULT_CONFIRMED", confirmation), "repeated ACK still receives final confirmation");
    check(confirmed == 1 && aborted == 0, "repeated ACK does not emit another saved result");
    guest.socket.abort();
    until([&] { return host.state() == LanSessionService::State::Finished; }, 100);
    check(confirmed == 1 && aborted == 0, "disconnect preserves the confirmed result");
}
} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    testStaleConfigurationAndResourceRetry();
    testRepeatedAcknowledgementAndConfirmedDisconnect();
    std::cout << "network review failures=" << failures << std::endl;
    return failures == 0 ? 0 : 1;
}
