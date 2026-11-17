#include "LanSessionService.h"

#include "core/ScoreService.h"

#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkInterface>
#include <QRegularExpression>
#include <QUuid>
#include <cmath>

namespace nc {
namespace {
const QStringList configKeys = {"songId",    "chartId",   "chartVersion", "difficulty",
                                "chartHash", "audioHash", "rulesVersion", "noteCount"};
const QStringList matchTypes = {"ARMED",  "START_COMMIT", "STARTED",    "SCORE_SNAPSHOT",
                                "FINISH", "MATCH_RESULT", "RESULT_ACK", "RESULT_CONFIRMED"};

QString newId() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

bool integral(const QJsonValue &v, qint64 minimum, qint64 maximum) {
    return v.isDouble() && std::isfinite(v.toDouble()) && v.toDouble() >= minimum &&
           v.toDouble() <= maximum && std::floor(v.toDouble()) == v.toDouble();
}

QJsonObject normalizedConfig(const QJsonObject &source) {
    QJsonObject result;
    for (const auto &key : configKeys)
        result.insert(key, source.value(key));
    return result;
}

bool validConfig(const QJsonObject &c) {
    for (const auto &key :
         {"songId", "chartId", "difficulty", "chartHash", "audioHash", "rulesVersion"}) {
        if (!c.value(key).isString() || c.value(key).toString().isEmpty() ||
            c.value(key).toString().size() > 128)
            return false;
    }
    const QRegularExpression hash(QStringLiteral("^[0-9a-fA-F]{64}$"));
    return integral(c.value("chartVersion"), 1, 2147483647) &&
           integral(c.value("noteCount"), 1, 20000) &&
           QStringList{"Easy", "Normal", "Hard"}.contains(c.value("difficulty").toString()) &&
           hash.match(c.value("chartHash").toString()).hasMatch() &&
           hash.match(c.value("audioHash").toString()).hasMatch();
}

QJsonObject statsJson(const PlayStats &s) {
    return {{"perfect", s.perfect},
            {"great", s.great},
            {"good", s.good},
            {"miss", s.miss},
            {"combo", s.combo},
            {"maxCombo", s.maxCombo},
            {"finalizedCount", s.finalizedCount},
            {"earnedUnits", s.earnedUnits}};
}

bool readStats(const QJsonObject &o, int noteCount, bool complete, PlayStats &out) {
    for (const auto &key :
         {"perfect", "great", "good", "miss", "combo", "maxCombo", "finalizedCount"}) {
        if (!integral(o.value(key), 0, noteCount))
            return false;
    }
    if (!integral(o.value("earnedUnits"), 0, 1000LL * noteCount))
        return false;
    PlayStats s{};
    s.perfect = o.value("perfect").toInt();
    s.great = o.value("great").toInt();
    s.good = o.value("good").toInt();
    s.miss = o.value("miss").toInt();
    s.combo = o.value("combo").toInt();
    s.maxCombo = o.value("maxCombo").toInt();
    s.finalizedCount = o.value("finalizedCount").toInt();
    s.earnedUnits = static_cast<qint64>(o.value("earnedUnits").toDouble());
    if (!ScoreService::isValidStats(s, noteCount, complete))
        return false;
    out = s;
    return true;
}

bool monotonicStats(const PlayStats &old, const PlayStats &now) {
    return now.perfect >= old.perfect && now.great >= old.great && now.good >= old.good &&
           now.miss >= old.miss && now.finalizedCount >= old.finalizedCount &&
           now.maxCombo >= old.maxCombo;
}

bool validName(const QString &name) {
    return !name.trimmed().isEmpty() && name.trimmed().size() <= 16;
}
} // namespace

LanSessionService::LanSessionService(QObject *parent) : QObject(parent) {
    m_clock.start();
    m_timer.setInterval(50);
    connect(&m_timer, &QTimer::timeout, this, &LanSessionService::tick);
    m_timer.start();
    connect(&m_server, &QTcpServer::newConnection, this, [this] {
        while (m_server.hasPendingConnections()) {
            QTcpSocket *incoming = m_server.nextPendingConnection();
            if (m_socket) {
                // 第三名玩家不会替换当前客人，也不会影响已开始的对局。
                const QJsonObject busy{
                    {"protocolVersion", ProtocolCodec::ProtocolVersion},
                    {"type", "ABORT"},
                    {"roomId", ""},
                    {"matchId", ""},
                    {"seq", 1},
                    {"payload", QJsonObject{{"reason", QStringLiteral("房间已满")}}}};
                incoming->write(ProtocolCodec::encode(busy));
                incoming->disconnectFromHost();
                connect(incoming, &QTcpSocket::disconnected, incoming, &QObject::deleteLater);
                continue;
            }
            changeState(State::Connecting, QStringLiteral("客人已连接，等待握手"));
            attachSocket(incoming);
        }
    });
}

LanSessionService::~LanSessionService() {
    if (m_socket)
        m_socket->disconnect(this);
}

QStringList LanSessionService::localAddresses() {
    QStringList addresses;
    for (const QHostAddress &address : QNetworkInterface::allAddresses()) {
        if (address.protocol() == QAbstractSocket::IPv4Protocol && !address.isLoopback() &&
            address != QHostAddress::AnyIPv4)
            addresses.append(address.toString());
    }
    addresses.removeDuplicates();
    return addresses;
}

void LanSessionService::changeState(State state, const QString &status) {
    m_state = state;
    m_stateSinceMs = localNowMs();
    if (!status.isEmpty())
        emit statusChanged(status);
    emit roomChanged();
}

bool LanSessionService::roomStage() const {
    return m_state == State::Room || m_state == State::Preparing || m_state == State::Finished;
}

bool LanSessionService::activeMatch() const {
    return m_state == State::Armed || m_state == State::Countdown || m_state == State::Playing ||
           m_state == State::WaitingResult;
}

void LanSessionService::resetMatch() {
    m_matchId.clear();
    m_targetMs = m_hostTargetMs = -1;
    m_localStarted = m_peerStarted = false;
    m_localFinished = m_peerFinished = m_confirmed = false;
    m_localFinal = {};
    m_peerFinal = {};
    m_peerStats = {};
    m_peerMusicTimeMs = -1;
    m_lastSnapshotMs = -100;
    m_result = {};
    m_startRequested = false;
}

void LanSessionService::resetConnection() {
    if (m_socket) {
        m_socket->disconnect(this);
        m_socket->abort();
        m_socket->deleteLater();
        m_socket = nullptr;
    }
    m_codec.reset();
    m_sync.reset();
    m_handshake = m_localReady = m_peerReady = m_peerClockReady = false;
    m_peerClockPingCount = 0;
    m_sendSeq = m_lastSeq = 0;
    m_pendingPingMs = -1;
    m_peerName.clear();
    m_usedMatchIds.clear();
    resetMatch();
}

bool LanSessionService::createRoom(quint16 port, const QString &nickname) {
    if (!validName(nickname)) {
        emit statusChanged(QStringLiteral("昵称须为 1—16 个字符"));
        return false;
    }
    leaveRoom();
    m_role = Role::Host;
    m_nickname = nickname.trimmed();
    m_roomId = newId();
    if (!m_server.listen(QHostAddress::AnyIPv4, port)) {
        const QString reason = QStringLiteral("无法创建房间：") + m_server.errorString();
        m_role = Role::None;
        emit statusChanged(reason);
        return false;
    }
    changeState(State::Listening,
                QStringLiteral("房间已创建，端口 %1，等待客人").arg(m_server.serverPort()));
    return true;
}

bool LanSessionService::joinRoom(const QString &ipv4, quint16 port, const QString &nickname) {
    QHostAddress address;
    if (!validName(nickname) || port == 0 || !address.setAddress(ipv4.trimmed()) ||
        address.protocol() != QAbstractSocket::IPv4Protocol || address.isNull() ||
        address == QHostAddress::AnyIPv4 || address.isMulticast()) {
        emit statusChanged(QStringLiteral("请输入有效的房主 IPv4、端口及 1—16 字昵称"));
        return false;
    }
    leaveRoom();
    m_role = Role::Guest;
    m_nickname = nickname.trimmed();
    attachSocket(new QTcpSocket(this));
    changeState(State::Connecting, QStringLiteral("正在连接房主…"));
    m_socket->connectToHost(address, port);
    return true;
}

void LanSessionService::attachSocket(QTcpSocket *socket) {
    m_socket = socket;
    socket->setParent(this);
    socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
    socket->setReadBufferSize(128 * 1024);
    m_lastReceivedMs = m_lastHeartbeatMs = localNowMs();
    connect(socket, &QTcpSocket::readyRead, this, &LanSessionService::receive);
    connect(socket, &QTcpSocket::connected, this, [this] {
        if (m_role == Role::Guest)
            send("HELLO", {{"nickname", m_nickname}});
    });
    connect(socket, &QTcpSocket::disconnected, this,
            [this] { disconnectWithReason(QStringLiteral("与对方的连接已断开"), false); });
    connect(socket, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        if (m_socket)
            disconnectWithReason(QStringLiteral("网络错误：") + m_socket->errorString(), false);
    });
    if (socket->bytesAvailable() > 0)
        receive();
}

void LanSessionService::leaveRoom() {
    if (m_socket && m_socket->state() == QAbstractSocket::ConnectedState)
        send("ABORT", {{"reason", QStringLiteral("玩家离开房间")}});
    m_server.close();
    resetConnection();
    m_role = Role::None;
    m_roomId.clear();
    m_config = {};
    m_revision = 0;
    changeState(State::Idle);
}

void LanSessionService::disconnectWithReason(const QString &reason, bool notifyPeer) {
    if (m_state == State::Idle || m_state == State::Aborted)
        return;
    const bool confirmed = m_confirmed;
    if (notifyPeer)
        send("ABORT", {{"reason", reason.left(256)}});
    m_server.close();
    if (m_socket) {
        m_socket->disconnect(this);
        m_socket->disconnectFromHost();
        m_socket->deleteLater();
        m_socket = nullptr;
    }
    m_handshake = m_localReady = m_peerReady = false;
    m_startRequested = false;
    m_targetMs = -1;
    m_codec.reset();
    m_sync.reset();
    m_peerClockReady = false;
    m_peerClockPingCount = 0;
    m_pendingPingMs = -1;
    m_peerStats = {};
    m_peerMusicTimeMs = -1;
    emit peerStatsChanged(m_peerStats);
    if (!confirmed) {
        // 保留 roomId / matchId 供错误日志定位，运行期统计与起播计划全部失效。
        m_hostTargetMs = -1;
        m_localStarted = m_peerStarted = false;
        m_localFinished = m_peerFinished = false;
        m_localFinal = m_peerFinal = {};
        m_result = {};
    }
    changeState(confirmed ? State::Finished : State::Aborted, reason);
    // 最终确认后断线不能撤销已经发出的结果或触发“中止局不保存”。
    if (!confirmed)
        emit aborted(reason);
}

void LanSessionService::abortMatch(const QString &reason) {
    disconnectWithReason(reason.isEmpty() ? QStringLiteral("对局已中止") : reason, true);
}

void LanSessionService::failProtocol(const QString &reason) {
    disconnectWithReason(QStringLiteral("协议错误：") + reason, true);
}

bool LanSessionService::send(const QString &type, const QJsonObject &payload) {
    if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState)
        return false;
    QJsonObject message{{"protocolVersion", ProtocolCodec::ProtocolVersion},
                        {"type", type},
                        {"roomId", m_roomId},
                        {"matchId", m_matchId},
                        {"seq", ++m_sendSeq},
                        {"configRevision", m_revision},
                        {"payload", payload}};
    QString error;
    const QByteArray frame = ProtocolCodec::encode(message, &error);
    if (frame.isEmpty()) {
        emit diagnostic(error);
        return false;
    }
    emit diagnostic(QStringLiteral("发送 %1 seq=%2 room=%3 match=%4 revision=%5")
                        .arg(type)
                        .arg(m_sendSeq)
                        .arg(m_roomId, m_matchId)
                        .arg(m_revision));
    return m_socket->write(frame) == frame.size();
}

void LanSessionService::receive() {
    while (m_socket && m_socket->bytesAvailable() > 0) {
        QList<QJsonObject> messages;
        QString error;
        if (!m_codec.feed(m_socket->read(64 * 1024), messages, &error)) {
            failProtocol(error);
            return;
        }
        for (const auto &message : messages) {
            process(message);
            if (!m_socket)
                return;
        }
    }
}

bool LanSessionService::setRoomConfig(const QJsonObject &config) {
    if (m_role != Role::Host || (!roomStage() && m_state != State::Listening) ||
        !validConfig(config)) {
        emit statusChanged(QStringLiteral("仅房主可在房间中设置有效曲目配置"));
        return false;
    }
    m_config = normalizedConfig(config);
    ++m_revision;
    m_localReady = m_peerReady = m_peerClockReady = false;
    m_peerClockPingCount = 0;
    m_sync.reset();
    resetMatch();
    if (m_handshake) {
        send("ROOM_CONFIG", m_config);
        changeState(State::Preparing, QStringLiteral("曲目已更新，请重新加载资源并准备"));
    }
    emit configReceived(m_config);
    emit roomChanged();
    return true;
}

void LanSessionService::setLocalResources(const QJsonObject &config, bool mediaReady) {
    m_localResources = normalizedConfig(config);
    m_mediaReady = mediaReady && validConfig(config);
    if (!resourcesMatch() && m_localReady)
        setReady(false);
    emit roomChanged();
}

bool LanSessionService::resourcesMatch() const {
    return m_mediaReady && validConfig(m_config) && m_localResources == m_config;
}

void LanSessionService::sendRoomState() {
    send("ROOM_STATE", {{"hostNickname", m_nickname},
                        {"guestNickname", m_peerName},
                        {"playerId", "guest"},
                        {"hostReady", m_localReady},
                        {"guestReady", m_peerReady}});
}

bool LanSessionService::setReady(bool ready) {
    if (!m_handshake || !roomStage() || (ready && !resourcesMatch())) {
        emit statusChanged(QStringLiteral("资源未就绪、资源不一致或当前不能准备"));
        return false;
    }
    if (m_state == State::Finished) {
        resetMatch();
        m_peerClockReady = false;
        m_sync.reset();
    }
    m_localReady = ready;
    if (!ready) {
        m_peerClockReady = false;
        m_sync.reset();
        m_pendingPingMs = -1;
        m_peerClockPingCount = 0;
        m_startRequested = false;
    }
    send("READY",
         {{"ready", ready}, {"resources", m_localResources}, {"clockReady", m_sync.isReady()}});
    changeState(State::Preparing, ready ? QStringLiteral("已准备") : QStringLiteral("已取消准备"));
    if (m_role == Role::Host)
        sendRoomState();
    beginClockSamples();
    return true;
}

void LanSessionService::beginClockSamples() {
    if (m_role == Role::Guest && m_localReady && m_peerReady && !m_sync.isReady() &&
        m_pendingPingMs < 0 && roomStage())
        sendClockPing();
}

void LanSessionService::sendClockPing() {
    m_pendingPingMs = localNowMs();
    send("CLOCK_PING", {{"t1", m_pendingPingMs}});
}

bool LanSessionService::requestStart() {
    if (m_role != Role::Host || !roomStage() || !m_handshake || !m_localReady || !m_peerReady ||
        !resourcesMatch()) {
        emit statusChanged(QStringLiteral("双方资源一致并准备后，房主才能开始"));
        return false;
    }
    if (!m_peerClockReady) {
        m_startRequested = true;
        emit statusChanged(QStringLiteral("等待五次时钟测量完成…"));
        return true;
    }
    resetMatch();
    m_matchId = newId();
    m_usedMatchIds.insert(m_matchId);
    m_hostTargetMs = m_targetMs = localNowMs() + 3000;
    changeState(State::Armed, QStringLiteral("等待对方确认开局计划"));
    return send("START_ARM", {{"startHostMs", m_hostTargetMs}});
}

void LanSessionService::reportStarted(qint64 actualLocalMs) {
    if ((m_state != State::Countdown && m_state != State::Playing) || m_localStarted)
        return;
    const qint64 actual = actualLocalMs < 0 ? localNowMs() : actualLocalMs;
    if (std::llabs(actual - m_targetMs) > 150) {
        abortMatch(QStringLiteral("本机音频启动偏差超过 150ms，本局中止"));
        return;
    }
    m_localStarted = true;
    send("STARTED", {{"actualLocalMs", actual}, {"deviationMs", actual - m_targetMs}});
    emit diagnostic(QStringLiteral("本机起播目标=%1 实际=%2 偏差=%3ms")
                        .arg(m_targetMs)
                        .arg(actual)
                        .arg(actual - m_targetMs));
    changeState(State::Playing, QStringLiteral("联机演奏中"));
}

void LanSessionService::publishStats(const PlayStats &stats, qint64 musicTimeMs) {
    if (m_state != State::Playing || !m_localStarted || musicTimeMs < 0 ||
        localNowMs() - m_lastSnapshotMs < 100 || !m_socket || m_socket->bytesToWrite() > 32 * 1024)
        return;
    PlayStats checked{};
    if (!readStats(statsJson(stats), m_config.value("noteCount").toInt(), false, checked))
        return;
    m_lastSnapshotMs = localNowMs();
    send("SCORE_SNAPSHOT", {{"stats", statsJson(stats)}, {"musicTimeMs", musicTimeMs}});
}

bool LanSessionService::submitFinish(const PlayStats &stats) {
    if (m_localFinished)
        return m_confirmed || m_state == State::WaitingResult;
    PlayStats checked{};
    if (m_state != State::Playing || !m_localStarted || !m_peerStarted ||
        !readStats(statsJson(stats), m_config.value("noteCount").toInt(), true, checked)) {
        emit statusChanged(QStringLiteral("本局尚未正常启动或最终统计不完整"));
        return false;
    }
    m_localFinal = checked;
    m_localFinished = true;
    send("FINISH", {{"stats", statsJson(stats)}, {"resources", m_config}});
    changeState(State::WaitingResult, QStringLiteral("本机完成，等待双方结果确认…"));
    maybeBuildResult();
    return true;
}

void LanSessionService::maybeBuildResult() {
    if (m_role != Role::Host || !m_localFinished || !m_peerFinished || !m_result.isEmpty())
        return;
    const int count = m_config.value("noteCount").toInt();
    const auto host = ScoreService::buildResult(m_localFinal, count);
    const auto guest = ScoreService::buildResult(m_peerFinal, count);
    const QString winner =
        host.score == guest.score ? "draw" : (host.score > guest.score ? "host" : "guest");
    m_result = {{"resultId", newId()},
                {"hostStats", statsJson(m_localFinal)},
                {"guestStats", statsJson(m_peerFinal)},
                {"hostScore", host.score},
                {"guestScore", guest.score},
                {"winner", winner},
                {"resources", m_config}};
    send("MATCH_RESULT", m_result);
    m_lastResultSentMs = localNowMs();
}

void LanSessionService::confirmResult() {
    if (m_confirmed)
        return;
    m_confirmed = true;
    const QString winner = m_result.value("winner").toString();
    const QString win = winner == "draw" ? "draw" : (winner == localPlayerId() ? "win" : "loss");
    m_localReady = m_peerReady = m_peerClockReady = false;
    m_peerClockPingCount = 0;
    changeState(State::Finished, QStringLiteral("双方结果已确认"));
    emit resultConfirmed(m_localFinal, m_peerFinal, m_result.value("resultId").toString(), win);
}

void LanSessionService::tick() {
    const qint64 now = localNowMs();
    if (m_state == State::Connecting && now - m_stateSinceMs >= 5000) {
        disconnectWithReason(QStringLiteral("连接或握手超时"), false);
        return;
    }
    if (!m_socket || !m_handshake)
        return;
    if (now - m_lastReceivedMs >= 5000) {
        disconnectWithReason(QStringLiteral("连续 5 秒未收到有效消息，连接已失效"), false);
        return;
    }
    if (now - m_lastHeartbeatMs >= 1000) {
        send("HEARTBEAT");
        m_lastHeartbeatMs = now;
    }
    if (m_state == State::Preparing && (!m_localReady || !m_peerReady) &&
        now - m_stateSinceMs >= 30000) {
        disconnectWithReason(QStringLiteral("资源加载或准备超时（30 秒）"), true);
        return;
    }
    if (m_state == State::Armed && now >= m_targetMs - 500) {
        abortMatch(QStringLiteral("开局确认超时"));
        return;
    }
    if ((m_state == State::Countdown || m_state == State::Playing) && now > m_targetMs + 1000 &&
        (!m_localStarted || !m_peerStarted)) {
        abortMatch(QStringLiteral("音频启动报告超时"));
        return;
    }
    if (m_state == State::WaitingResult) {
        if (now - m_stateSinceMs >= 15000) {
            abortMatch(QStringLiteral("最终结果未确认：交换超时"));
            return;
        }
        if (!m_result.isEmpty() && now - m_lastResultSentMs >= 1000) {
            send(m_role == Role::Host ? "MATCH_RESULT" : "RESULT_ACK",
                 m_role == Role::Host ? m_result
                                      : QJsonObject{{"resultId", m_result.value("resultId")}});
            m_lastResultSentMs = now;
        }
    }
}

void LanSessionService::process(const QJsonObject &message) {
    const QString type = message.value("type").toString();
    const auto payload = message.value("payload").toObject();
    const qint64 sequence = static_cast<qint64>(message.value("seq").toDouble());
    if (sequence <= m_lastSeq)
        return;
    m_lastSeq = sequence;
    if (type == "ABORT" && !m_handshake) {
        disconnectWithReason(payload.value("reason").toString(QStringLiteral("对方拒绝连接")),
                             false);
        return;
    }
    if (!m_handshake) {
        if (m_role == Role::Host && type == "HELLO" &&
            message.value("roomId").toString().isEmpty() &&
            message.value("matchId").toString().isEmpty() && payload.value("nickname").isString() &&
            validName(payload.value("nickname").toString())) {
            m_peerName = payload.value("nickname").toString().trimmed();
            m_handshake = true;
            m_lastReceivedMs = localNowMs();
            changeState(State::Room, QStringLiteral("客人 %1 已加入").arg(m_peerName));
            sendRoomState();
            if (!m_config.isEmpty()) {
                send("ROOM_CONFIG", m_config);
                changeState(State::Preparing, QStringLiteral("请加载曲目资源并准备"));
            }
            return;
        }
        if (m_role == Role::Guest && type == "ROOM_STATE" &&
            !message.value("roomId").toString().isEmpty() &&
            payload.value("hostNickname").isString() &&
            validName(payload.value("hostNickname").toString()) &&
            payload.value("playerId").toString() == "guest") {
            m_roomId = message.value("roomId").toString();
            m_peerName = payload.value("hostNickname").toString();
            m_handshake = true;
            m_lastReceivedMs = localNowMs();
            changeState(State::Room, QStringLiteral("已加入 %1 的房间").arg(m_peerName));
            return;
        }
        failProtocol(QStringLiteral("握手消息或字段不正确"));
        return;
    }
    if (message.value("roomId").toString() != m_roomId) {
        failProtocol(QStringLiteral("房间标识不匹配"));
        return;
    }
    if (type == "ABORT" && !message.value("matchId").toString().isEmpty() &&
        message.value("matchId").toString() != m_matchId)
        return;
    if (matchTypes.contains(type) && message.value("matchId").toString() != m_matchId)
        return; // 旧对局迟到消息不覆盖当前状态。
    if (type != "ROOM_CONFIG" && type != "HEARTBEAT" && type != "ABORT" &&
        (!integral(message.value("configRevision"), 0, 2147483647) ||
         message.value("configRevision").toInt() != m_revision))
        return; // 已废弃配置的准备和开始消息不能继续使用。
    m_lastReceivedMs = localNowMs();
    emit diagnostic(QStringLiteral("接收 %1 seq=%2 room=%3 match=%4 revision=%5")
                        .arg(type)
                        .arg(sequence)
                        .arg(m_roomId, m_matchId)
                        .arg(m_revision));

    if (type == "HEARTBEAT")
        return;
    if (type == "ABORT") {
        if (!payload.value("reason").isString())
            failProtocol(QStringLiteral("中止原因缺失"));
        else
            disconnectWithReason(
                QStringLiteral("对方中止：") + payload.value("reason").toString().left(256), false);
        return;
    }
    if (type == "ROOM_STATE") {
        if (m_role != Role::Guest || !roomStage() || !payload.value("hostReady").isBool() ||
            !payload.value("guestReady").isBool()) {
            failProtocol(QStringLiteral("无权发送房间状态或状态不正确"));
            return;
        }
        m_peerReady = payload.value("hostReady").toBool();
        emit roomChanged();
        beginClockSamples();
        return;
    }
    if (type == "ROOM_CONFIG") {
        if (m_role != Role::Guest || !roomStage() || !validConfig(payload) ||
            !integral(message.value("configRevision"), 1, 2147483647)) {
            failProtocol(QStringLiteral("无权设置配置、配置字段错误或状态不正确"));
            return;
        }
        if (message.value("configRevision").toInt() <= m_revision)
            return;
        m_revision = message.value("configRevision").toInt();
        m_config = normalizedConfig(payload);
        m_localReady = m_peerReady = false;
        m_sync.reset();
        m_pendingPingMs = -1;
        resetMatch();
        changeState(State::Preparing, QStringLiteral("收到新曲目配置，请校验并加载媒体"));
        emit configReceived(m_config);
        return;
    }
    if (type == "READY") {
        if (!roomStage() || !payload.value("ready").isBool() ||
            !payload.value("clockReady").isBool() || !payload.value("resources").isObject()) {
            failProtocol(QStringLiteral("准备消息字段或状态不正确"));
            return;
        }
        const bool ready = payload.value("ready").toBool();
        if (ready && normalizedConfig(payload.value("resources").toObject()) != m_config) {
            m_peerReady = false;
            emit statusChanged(QStringLiteral("对方资源与房间不一致，不能准备"));
            emit roomChanged();
            return;
        }
        if (m_role == Role::Host && ready && payload.value("clockReady").toBool() &&
            m_peerClockPingCount < ClockSynchronizer::RequiredSamples) {
            failProtocol(QStringLiteral("客人在五次时钟采样前报告完成"));
            return;
        }
        m_peerReady = ready;
        if (m_role == Role::Host)
            m_peerClockReady = ready && payload.value("clockReady").toBool();
        if (!ready) {
            m_sync.reset();
            m_peerClockReady = false;
            m_pendingPingMs = -1;
            m_peerClockPingCount = 0;
            m_startRequested = false;
        }
        emit roomChanged();
        beginClockSamples();
        if (m_role == Role::Host) {
            sendRoomState();
            if (m_startRequested && m_peerClockReady)
                requestStart();
        }
        return;
    }
    if (type == "CLOCK_PING") {
        if (m_role != Role::Host || !roomStage() || !m_localReady || !m_peerReady ||
            !integral(payload.value("t1"), 0, 9007199254740991LL)) {
            failProtocol(QStringLiteral("时钟请求状态或字段不正确"));
            return;
        }
        const qint64 received = localNowMs();
        ++m_peerClockPingCount;
        send("CLOCK_PONG", {{"t1", payload.value("t1")}, {"t2", received}, {"t3", localNowMs()}});
        return;
    }
    if (type == "CLOCK_PONG") {
        if (m_role != Role::Guest || !roomStage() || !m_localReady || !m_peerReady ||
            !integral(payload.value("t1"), 0, 9007199254740991LL) ||
            !integral(payload.value("t2"), 0, 9007199254740991LL) ||
            !integral(payload.value("t3"), 0, 9007199254740991LL) ||
            static_cast<qint64>(payload.value("t1").toDouble()) != m_pendingPingMs ||
            !m_sync.sample(m_pendingPingMs, static_cast<qint64>(payload.value("t2").toDouble()),
                           static_cast<qint64>(payload.value("t3").toDouble()), localNowMs())) {
            failProtocol(QStringLiteral("时钟应答字段或状态不正确"));
            return;
        }
        m_pendingPingMs = -1;
        if (m_sync.isReady()) {
            send("READY", {{"ready", true}, {"resources", m_localResources}, {"clockReady", true}});
            emit statusChanged(QStringLiteral("时钟校准完成，RTT=%1ms，偏移=%2ms")
                                   .arg(m_sync.bestRttMs())
                                   .arg(m_sync.offsetMs()));
        } else {
            sendClockPing();
        }
        return;
    }
    if (type == "START_ARM") {
        if (m_role != Role::Guest || !roomStage() || !m_localReady || !m_peerReady ||
            !resourcesMatch() || !m_sync.isReady() ||
            message.value("matchId").toString().isEmpty() ||
            m_usedMatchIds.contains(message.value("matchId").toString()) ||
            !integral(payload.value("startHostMs"), 0, 9007199254740991LL)) {
            failProtocol(QStringLiteral("无权发起开局或开局条件不满足"));
            return;
        }
        resetMatch();
        m_matchId = message.value("matchId").toString();
        m_usedMatchIds.insert(m_matchId);
        m_hostTargetMs = static_cast<qint64>(payload.value("startHostMs").toDouble());
        m_targetMs = m_sync.guestTarget(m_hostTargetMs);
        if (m_targetMs - localNowMs() < 500 || m_targetMs - localNowMs() > 10000) {
            abortMatch(QStringLiteral("开局计划已经过期或提前量不合理"));
            return;
        }
        changeState(State::Armed, QStringLiteral("开局计划已确认，等待房主提交"));
        send("ARMED", {{"startHostMs", m_hostTargetMs}});
        return;
    }
    if (type == "ARMED") {
        if (m_role != Role::Host || m_state != State::Armed ||
            payload.value("startHostMs") != QJsonValue(m_hostTargetMs) ||
            localNowMs() >= m_targetMs - 500) {
            failProtocol(QStringLiteral("开局回执无效或已过截止时间"));
            return;
        }
        send("START_COMMIT", {{"startHostMs", m_hostTargetMs}});
        changeState(State::Countdown, QStringLiteral("开局已提交，倒计时中"));
        emit startScheduled(m_targetMs, m_matchId);
        return;
    }
    if (type == "START_COMMIT") {
        if (m_role != Role::Guest || m_state != State::Armed ||
            payload.value("startHostMs") != QJsonValue(m_hostTargetMs) ||
            localNowMs() >= m_targetMs - 150) {
            failProtocol(QStringLiteral("无效或过期的开局提交"));
            return;
        }
        changeState(State::Countdown, QStringLiteral("开局已提交，倒计时中"));
        emit startScheduled(m_targetMs, m_matchId);
        return;
    }
    if (type == "STARTED") {
        if ((m_state != State::Countdown && m_state != State::Playing) ||
            !integral(payload.value("actualLocalMs"), 0, 9007199254740991LL) ||
            !integral(payload.value("deviationMs"), -150, 150)) {
            failProtocol(QStringLiteral("对方音频启动状态或偏差不合格"));
            return;
        }
        m_peerStarted = true;
        emit roomChanged();
        return;
    }
    if (type == "SCORE_SNAPSHOT") {
        PlayStats stats{};
        if ((m_state != State::Countdown && m_state != State::Playing &&
             m_state != State::WaitingResult) ||
            !m_peerStarted || !payload.value("stats").isObject() ||
            !integral(payload.value("musicTimeMs"), 0, 86400000) ||
            !readStats(payload.value("stats").toObject(), m_config.value("noteCount").toInt(),
                       false, stats)) {
            failProtocol(QStringLiteral("比分统计或状态无效"));
            return;
        }
        // 对端可能先进入实际播放态；它的首个有效快照可早于本机音频启动回调。
        // 仍校验对局、发送状态及统计，但本机起播前不更新显示，也不干预本地判定。
        if (m_state == State::Countdown)
            return;
        const qint64 music = static_cast<qint64>(payload.value("musicTimeMs").toDouble());
        if (music < m_peerMusicTimeMs || !monotonicStats(m_peerStats, stats))
            return;
        m_peerStats = stats;
        m_peerMusicTimeMs = music;
        emit peerStatsChanged(stats);
        return;
    }
    if (type == "FINISH") {
        PlayStats stats{};
        if ((m_state != State::Playing && m_state != State::WaitingResult &&
             m_state != State::Finished) ||
            !m_peerStarted || normalizedConfig(payload.value("resources").toObject()) != m_config ||
            !payload.value("stats").isObject() ||
            !readStats(payload.value("stats").toObject(), m_config.value("noteCount").toInt(), true,
                       stats) ||
            !monotonicStats(m_peerStats, stats)) {
            failProtocol(QStringLiteral("最终统计、资源标识或状态无效"));
            return;
        }
        if (m_peerFinished && statsJson(stats) != statsJson(m_peerFinal)) {
            failProtocol(QStringLiteral("重复提交的最终统计不一致"));
            return;
        }
        m_peerFinal = stats;
        m_peerFinished = true;
        maybeBuildResult();
        if (!m_result.isEmpty() && m_role == Role::Host)
            send("MATCH_RESULT", m_result);
        return;
    }
    if (type == "MATCH_RESULT") {
        const int count = m_config.value("noteCount").toInt();
        PlayStats host{}, guest{};
        if (m_role != Role::Guest ||
            (m_state != State::WaitingResult && m_state != State::Finished) || !m_localFinished ||
            !m_peerFinished || !payload.value("resultId").isString() ||
            payload.value("resultId").toString().isEmpty() ||
            payload.value("resultId").toString().size() > 64 ||
            normalizedConfig(payload.value("resources").toObject()) != m_config ||
            !readStats(payload.value("hostStats").toObject(), count, true, host) ||
            !readStats(payload.value("guestStats").toObject(), count, true, guest) ||
            statsJson(host) != statsJson(m_peerFinal) ||
            statsJson(guest) != statsJson(m_localFinal)) {
            failProtocol(QStringLiteral("最终结果不能与双方统计相互核对"));
            return;
        }
        const auto hostResult = ScoreService::buildResult(host, count);
        const auto guestResult = ScoreService::buildResult(guest, count);
        const QString winner = hostResult.score == guestResult.score
                                   ? "draw"
                                   : (hostResult.score > guestResult.score ? "host" : "guest");
        if (payload.value("hostScore") != QJsonValue(hostResult.score) ||
            payload.value("guestScore") != QJsonValue(guestResult.score) ||
            payload.value("winner").toString() != winner ||
            (!m_result.isEmpty() && payload != m_result)) {
            failProtocol(QStringLiteral("最终分数、胜负或重复结果不一致"));
            return;
        }
        m_result = payload;
        send("RESULT_ACK", {{"resultId", m_result.value("resultId")}});
        m_lastResultSentMs = localNowMs();
        return;
    }
    if (type == "RESULT_ACK") {
        if (m_role != Role::Host ||
            (m_state != State::WaitingResult && m_state != State::Finished) || m_result.isEmpty() ||
            payload.value("resultId") != m_result.value("resultId")) {
            failProtocol(QStringLiteral("结果回执不匹配"));
            return;
        }
        // 先确认本端，再发最后确认；最后一包丢失时只保留已经确认的一端。
        send("RESULT_CONFIRMED", {{"resultId", m_result.value("resultId")}});
        confirmResult();
        return;
    }
    if (type == "RESULT_CONFIRMED") {
        if (m_role != Role::Guest ||
            (m_state != State::WaitingResult && m_state != State::Finished) || m_result.isEmpty() ||
            payload.value("resultId") != m_result.value("resultId")) {
            failProtocol(QStringLiteral("最终确认标识不匹配"));
            return;
        }
        confirmResult();
        return;
    }
    failProtocol(QStringLiteral("未知消息或当前发送方无权发送：") + type);
}

} // namespace nc
