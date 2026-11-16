#include "network/ClockSynchronizer.h"
#include "network/LanSessionService.h"
#include "network/ProtocolCodec.h"
#include "core/ScoreService.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonDocument>
#include <QThread>
#include <QTimer>
#include <QtEndian>
#include <functional>
#include <iostream>

using namespace nc;

namespace {
int failures = 0;
void check(bool condition, const char *name)
{
    std::cout << (condition ? "PASS " : "FAIL ") << name << std::endl;
    if (!condition)
        ++failures;
}

bool until(const std::function<bool()> &condition, int timeout = 2000)
{
    QElapsedTimer timer;
    timer.start();
    while (!condition() && timer.elapsed() < timeout) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(1);
    }
    return condition();
}

QJsonObject envelope(const QString &type = "HEARTBEAT", int sequence = 1)
{
    return {{"protocolVersion", 1}, {"type", type}, {"roomId", "room"}, {"matchId", ""},
            {"seq", sequence}, {"payload", QJsonObject{}}};
}

QJsonObject config()
{
    return {{"songId", "demo"}, {"chartId", "demo-normal"}, {"chartVersion", 1},
        {"difficulty", "Normal"}, {"chartHash", QString(64, 'a')}, {"audioHash", QString(64, 'b')},
        {"rulesVersion", JudgeConfig{}.rulesVersion}, {"noteCount", 6}};
}

void protocolTests()
{
    ProtocolCodec codec;
    const QByteArray first = ProtocolCodec::encode(envelope());
    const QByteArray second = ProtocolCodec::encode(envelope("READY", 2));
    QList<QJsonObject> messages;
    QString error;
    check(codec.feed(first.first(2), messages, &error) && messages.isEmpty(), "partial header buffered");
    check(codec.feed(first.mid(2, 5), messages, &error) && messages.isEmpty(), "partial JSON buffered");
    check(codec.feed(first.mid(7) + second, messages, &error) && messages.size() == 2,
          "partial and coalesced frames decoded");
    check(messages.front() == envelope(), "decoded envelope unchanged");
    QByteArray length(4, 0);
    check(!codec.feed(length, messages, &error), "zero frame rejected");
    qToBigEndian<quint32>(65537, length.data());
    check(!codec.feed(length, messages, &error), "oversized frame rejected");
    const QByteArray invalid = "not JSON";
    qToBigEndian<quint32>(invalid.size(), length.data());
    check(!codec.feed(length + invalid, messages, &error), "malformed JSON rejected");
    auto wrong = envelope();
    wrong["protocolVersion"] = 2;
    check(ProtocolCodec::encode(wrong, &error).isEmpty(), "wrong version rejected");
    wrong = envelope();
    wrong["seq"] = 1.5;
    check(ProtocolCodec::encode(wrong, &error).isEmpty(), "fractional sequence rejected");
    wrong = envelope();
    wrong["payload"] = "string";
    check(ProtocolCodec::encode(wrong, &error).isEmpty(), "non-object payload rejected");
}

void clockTests()
{
    ClockSynchronizer sync;
    check(!sync.sample(100, 1100, 1099, 120), "negative host interval rejected");
    check(sync.sample(100, 1110, 1110, 120), "clock sample accepted");
    sync.sample(200, 1202, 1202, 204);
    sync.sample(300, 1304, 1304, 308);
    sync.sample(400, 1406, 1406, 412);
    sync.sample(500, 1503, 1503, 506);
    check(sync.isReady() && sync.sampleCount() == 5, "five clock samples required");
    check(sync.bestRttMs() == 4 && sync.offsetMs() == 1000 && sync.guestTarget(5000) == 4000,
          "lowest RTT selected and host time mapped");
}

void sessionTests()
{
    LanSessionService host, guest;
    int hostConfirmed = 0, guestConfirmed = 0, guestAborted = 0;
    QString hostWin, guestWin;
    QString hostResultId, guestResultId;
    QObject::connect(&host, &LanSessionService::statusChanged, [](const QString &s) {
        std::cout << "HOST " << s.toStdString() << std::endl;
    });
    QObject::connect(&guest, &LanSessionService::statusChanged, [](const QString &s) {
        std::cout << "GUEST " << s.toStdString() << std::endl;
    });
    QObject::connect(&host, &LanSessionService::resultConfirmed,
        [&](const PlayStats &, const PlayStats &, const QString &id, const QString &win) {
            ++hostConfirmed; hostWin = win; hostResultId = id;
        });
    QObject::connect(&guest, &LanSessionService::resultConfirmed,
        [&](const PlayStats &, const PlayStats &, const QString &id, const QString &win) {
            ++guestConfirmed; guestWin = win; guestResultId = id;
        });
    QObject::connect(&guest, &LanSessionService::aborted, [&](const QString &) { ++guestAborted; });
    QObject::connect(&guest, &LanSessionService::configReceived, [&](const QJsonObject &c) {
        guest.setLocalResources(c, true);
    });
    check(host.createRoom(0, QStringLiteral("房主")), "host listening");
    check(host.setRoomConfig(config()), "host config accepted");
    host.setLocalResources(config(), true);
    check(guest.joinRoom("127.0.0.1", host.listeningPort(), QStringLiteral("客人")), "guest connects asynchronously");
    check(until([&] { return guest.configRevision() == host.configRevision() && !guest.config().isEmpty(); }),
          "handshake and resource config delivered");
    check(!guest.requestStart(), "guest cannot initiate match");
    auto mismatch = config();
    mismatch["audioHash"] = QString(64, 'c');
    guest.setLocalResources(mismatch, true);
    check(!guest.setReady(true), "audio mismatch blocks ready");
    mismatch = config();
    mismatch["rulesVersion"] = "1";
    guest.setLocalResources(mismatch, true);
    check(!guest.setReady(true), "previous judgement rules cannot prepare for a new-rules match");
    guest.setLocalResources(config(), true);
    check(host.setReady(true) && guest.setReady(true), "both ready accepted");
    check(until([&] { return host.peerReady() && guest.peerReady() && guest.clockRttMs() >= 0; }),
          "ready and clock sampling exchanged");

    int hostSchedules = 0, guestSchedules = 0;
    QObject::connect(&host, &LanSessionService::startScheduled, [&](qint64 target, const QString &) {
        ++hostSchedules;
        QTimer::singleShot(qMax<qint64>(0, target - host.localNowMs()), &host, [&] { host.reportStarted(); });
    });
    QObject::connect(&guest, &LanSessionService::startScheduled, [&](qint64 target, const QString &) {
        ++guestSchedules;
        QTimer::singleShot(qMax<qint64>(0, target - guest.localNowMs()), &guest, [&] { guest.reportStarted(); });
    });
    check(host.requestStart(), "host requests synchronized start");
    check(until([&] { return host.state() == LanSessionService::State::Playing
        && guest.state() == LanSessionService::State::Playing; }, 5000), "ARM COMMIT reaches both scheduled starts");
    check(hostSchedules == 1 && guestSchedules == 1 && host.matchId() == guest.matchId(),
          "single match and single schedule per side");
    check(!host.requestStart(), "duplicate start rejected");
    until([&] { return host.localNowMs() > 3200; }, 300);
    PlayStats allPerfect;
    for (int i = 0; i < 6; ++i)
        ScoreService::applyJudgement(allPerfect, Judgement::Perfect);
    int snapshots = 0;
    QObject::connect(&guest, &LanSessionService::peerStatsChanged, [&](const PlayStats &) { ++snapshots; });
    host.publishStats(allPerfect, 9000);
    check(until([&] { return snapshots == 1; }), "valid score snapshot delivered");
    check(host.submitFinish(allPerfect) && guest.submitFinish(allPerfect), "complete stats submitted");
    check(until([&] { return hostConfirmed == 1 && guestConfirmed == 1; }), "three-way final result confirmed");
    check(hostWin == "draw" && guestWin == "draw" && hostResultId == guestResultId,
          "same score yields draw with common result id");
    check(host.submitFinish(allPerfect) && hostConfirmed == 1, "finish submission idempotent");
    host.leaveRoom();
    until([&] { return guest.state() == LanSessionService::State::Finished; });
    check(guestConfirmed == 1 && guestAborted == 0, "disconnect preserves already confirmed result");
}

void interruptionTests()
{
    LanSessionService host, guest, extra;
    int confirmed = 0;
    int aborted = 0;
    QObject::connect(&guest, &LanSessionService::configReceived, [&](const QJsonObject &c) {
        guest.setLocalResources(c, true);
    });
    QObject::connect(&host, &LanSessionService::resultConfirmed,
        [&](const PlayStats &, const PlayStats &, const QString &, const QString &) { ++confirmed; });
    QObject::connect(&guest, &LanSessionService::aborted, [&](const QString &) { ++aborted; });
    host.createRoom(0, "host");
    host.setRoomConfig(config());
    host.setLocalResources(config());
    guest.joinRoom("127.0.0.1", host.listeningPort(), "guest");
    until([&] { return guest.configRevision() == 1; });
    extra.joinRoom("127.0.0.1", host.listeningPort(), "extra");
    check(until([&] { return extra.state() == LanSessionService::State::Aborted; })
        && host.state() == LanSessionService::State::Preparing, "third player rejected without replacing guest");
    host.setReady(true);
    guest.setReady(true);
    check(until([&] { return host.peerReady() && guest.peerReady(); }), "ready before config change");
    auto next = config();
    next["chartVersion"] = 2;
    next["chartHash"] = QString(64, 'c');
    check(host.setRoomConfig(next), "host can replace room config");
    host.setLocalResources(next);
    check(until([&] { return guest.configRevision() == 2; }) && !host.localReady()
        && !host.peerReady() && !guest.localReady() && !guest.peerReady(), "config revision clears both readiness states");
    check(!host.requestStart(), "config change requires readiness again");
    host.setReady(true);
    guest.setReady(true);
    until([&] { return host.peerReady() && guest.peerReady(); });
    check(host.requestStart(), "start accepted after preparing new config");
    check(until([&] { return host.state() == LanSessionService::State::Countdown
        && guest.state() == LanSessionService::State::Countdown; }), "new config has fresh committed start");
    host.abortMatch(QStringLiteral("测试主动退出"));
    check(until([&] { return guest.state() == LanSessionService::State::Aborted; })
        && confirmed == 0 && aborted == 1, "countdown abort reaches peer without official result");
}

void authorityTests()
{
    LanSessionService host;
    host.createRoom(0, "host");
    host.setRoomConfig(config());
    QTcpSocket raw;
    raw.connectToHost(QHostAddress::LocalHost, host.listeningPort());
    check(until([&] { return raw.state() == QAbstractSocket::ConnectedState; }), "raw test peer connects");
    QJsonObject hello = envelope("HELLO");
    hello["roomId"] = "";
    hello["payload"] = QJsonObject{{"nickname", "raw"}};
    raw.write(ProtocolCodec::encode(hello));
    check(until([&] { return host.state() == LanSessionService::State::Preparing; }), "raw peer handshake accepted");
    QJsonObject unauthorized = envelope("ROOM_CONFIG", 2);
    unauthorized["roomId"] = host.roomId();
    unauthorized["configRevision"] = 1;
    unauthorized["payload"] = config();
    raw.write(ProtocolCodec::encode(unauthorized));
    check(until([&] { return host.state() == LanSessionService::State::Aborted; }),
          "guest cannot overwrite host configuration");
}

void preStartSnapshotTests()
{
    LanSessionService host, guest;
    int aborted = 0;
    int hostConfirmed = 0;
    int guestConfirmed = 0;
    int displayedSnapshots = 0;
    bool preStartSnapshotReceived = false;
    qint64 guestTarget = -1;
    QObject::connect(&guest, &LanSessionService::configReceived, [&](const QJsonObject &value) {
        guest.setLocalResources(value, true);
    });
    QObject::connect(&host, &LanSessionService::aborted, [&](const QString &) { ++aborted; });
    QObject::connect(&guest, &LanSessionService::aborted, [&](const QString &) { ++aborted; });
    QObject::connect(&host, &LanSessionService::resultConfirmed,
        [&](const PlayStats &, const PlayStats &, const QString &, const QString &) { ++hostConfirmed; });
    QObject::connect(&guest, &LanSessionService::resultConfirmed,
        [&](const PlayStats &, const PlayStats &, const QString &, const QString &) { ++guestConfirmed; });
    QObject::connect(&guest, &LanSessionService::peerStatsChanged,
        [&](const PlayStats &) { ++displayedSnapshots; });
    QObject::connect(&guest, &LanSessionService::diagnostic, [&](const QString &message) {
        if (message.contains(QStringLiteral("接收 SCORE_SNAPSHOT")))
            preStartSnapshotReceived = true;
    });
    QObject::connect(&host, &LanSessionService::startScheduled, [&](qint64 target, const QString &) {
        QTimer::singleShot(qMax<qint64>(0, target - host.localNowMs()), &host, [&, target] {
            host.reportStarted(target);
            host.publishStats({}, 0);
        });
    });
    QObject::connect(&guest, &LanSessionService::startScheduled, [&](qint64 target, const QString &) {
        // Deliberately leave the guest in Countdown while the host publishes its first snapshot.
        guestTarget = target;
    });

    check(host.createRoom(0, "snapshot-host") && host.setRoomConfig(config()),
        "pre-start snapshot host room configured");
    host.setLocalResources(config(), true);
    guest.joinRoom("127.0.0.1", host.listeningPort(), "snapshot-guest");
    if (!until([&] { return guest.configRevision() == host.configRevision() && !guest.config().isEmpty(); })) {
        check(false, "pre-start snapshot guest receives config");
        return;
    }
    check(host.setReady(true) && guest.setReady(true), "pre-start snapshot peers prepare");
    if (!until([&] { return host.peerReady() && guest.peerReady(); })) {
        check(false, "pre-start snapshot peers exchange readiness");
        return;
    }
    check(host.requestStart(), "pre-start snapshot host schedules match");
    if (!until([&] { return preStartSnapshotReceived || aborted > 0; }, 5000)) {
        check(false, "pre-start snapshot reaches guest before local start");
        return;
    }
    check(preStartSnapshotReceived && aborted == 0 && host.state() == LanSessionService::State::Playing
            && guest.state() == LanSessionService::State::Countdown,
        "valid peer snapshot during local Countdown does not abort match");
    check(displayedSnapshots == 0, "pre-start snapshot is validated and discarded before local playback");
    guest.reportStarted(guestTarget);
    check(until([&] { return guest.state() == LanSessionService::State::Playing; }),
        "delayed local start can still enter normal playback");
    // Allow the guest STARTED frame to arrive before submitting complete statistics.
    QElapsedTimer delivered;
    delivered.start();
    until([&] { return delivered.elapsed() >= 30; }, 100);
    PlayStats allPerfect;
    for (int i = 0; i < 6; ++i)
        ScoreService::applyJudgement(allPerfect, Judgement::Perfect);
    check(host.submitFinish(allPerfect) && guest.submitFinish(allPerfect),
        "snapshot race match accepts both final statistics");
    check(until([&] { return hostConfirmed == 1 && guestConfirmed == 1; }) && aborted == 0,
        "snapshot race match completes the normal result confirmation flow");
}
} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    protocolTests();
    clockTests();
    sessionTests();
    interruptionTests();
    authorityTests();
    preStartSnapshotTests();
    std::cout << "failures=" << failures << std::endl;
    return failures == 0 ? 0 : 1;
}
