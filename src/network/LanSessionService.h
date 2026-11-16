#pragma once

#include "ClockSynchronizer.h"
#include "ProtocolCodec.h"
#include "model/Models.h"

#include <QElapsedTimer>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

namespace nc {

// 房间与对局协调层；只传统计和计划，不参与本机输入判定或音乐时间推进。
class LanSessionService : public QObject {
    Q_OBJECT
  public:
    enum class Role { None, Host, Guest };
    Q_ENUM(Role)
    enum class State {
        Idle,
        Listening,
        Connecting,
        Room,
        Preparing,
        Armed,
        Countdown,
        Playing,
        WaitingResult,
        Finished,
        Aborted
    };
    Q_ENUM(State)

    explicit LanSessionService(QObject *parent = nullptr);
    ~LanSessionService() override;

    bool createRoom(quint16 port, const QString &nickname);
    bool joinRoom(const QString &ipv4, quint16 port, const QString &nickname);
    void leaveRoom();
    bool setRoomConfig(const QJsonObject &config);
    // 必须由调用方在资源校验及媒体预加载成功后提交；不把收到的配置当作本地加载成功。
    void setLocalResources(const QJsonObject &config, bool mediaReady = true);
    bool setReady(bool ready);
    bool requestStart();
    void reportStarted(qint64 actualLocalMs = -1);
    void publishStats(const PlayStats &stats, qint64 musicTimeMs);
    bool submitFinish(const PlayStats &stats);
    void abortMatch(const QString &reason);

    qint64 localNowMs() const {
        return m_clock.elapsed();
    }
    Role role() const {
        return m_role;
    }
    State state() const {
        return m_state;
    }
    QJsonObject config() const {
        return m_config;
    }
    int configRevision() const {
        return m_revision;
    }
    bool localReady() const {
        return m_localReady;
    }
    bool peerReady() const {
        return m_peerReady;
    }
    QString peerName() const {
        return m_peerName;
    }
    QString localPlayerId() const {
        return m_role == Role::Host ? "host" : "guest";
    }
    QString matchId() const {
        return m_matchId;
    }
    QString roomId() const {
        return m_roomId;
    }
    quint16 listeningPort() const {
        return m_server.serverPort();
    }
    double clockOffsetMs() const {
        return m_sync.offsetMs();
    }
    qint64 clockRttMs() const {
        return m_sync.bestRttMs();
    }
    static QStringList localAddresses();

  signals:
    void roomChanged();
    void statusChanged(const QString &message);
    void configReceived(const QJsonObject &config);
    void startScheduled(qint64 localTargetMs, const QString &matchId);
    void peerStatsChanged(const nc::PlayStats &stats);
    void resultConfirmed(const nc::PlayStats &local, const nc::PlayStats &remote,
                         const QString &resultId, const QString &winResult);
    void aborted(const QString &reason);
    void diagnostic(const QString &message);

  private:
    void attachSocket(QTcpSocket *socket);
    void receive();
    void process(const QJsonObject &message);
    bool send(const QString &type, const QJsonObject &payload = {});
    void tick();
    void changeState(State state, const QString &status = {});
    void failProtocol(const QString &reason);
    void disconnectWithReason(const QString &reason, bool notifyPeer);
    void resetMatch();
    void resetConnection();
    bool resourcesMatch() const;
    bool roomStage() const;
    bool activeMatch() const;
    void sendRoomState();
    void beginClockSamples();
    void sendClockPing();
    void maybeBuildResult();
    void confirmResult();

    QTcpServer m_server;            // 房主监听器，只接纳一名客人。
    QTcpSocket *m_socket = nullptr; // 当前唯一对端，由本对象拥有。
    QTimer m_timer;                 // 定期检查超时和心跳，不阻塞游戏循环。
    QElapsedTimer m_clock;          // 生命周期固定原点的单调毫秒时钟。
    ProtocolCodec m_codec;
    ClockSynchronizer m_sync;
    Role m_role = Role::None;
    State m_state = State::Idle;
    QString m_nickname;
    QString m_peerName;
    QString m_roomId;
    QString m_matchId;
    QSet<QString> m_usedMatchIds; // 同一连接中已使用的对局，阻止重复比赛标识再次开局。
    QJsonObject m_config;
    QJsonObject m_localResources;
    bool m_mediaReady = false;
    int m_revision = 0;
    bool m_handshake = false;
    bool m_localReady = false;
    bool m_peerReady = false;
    bool m_peerClockReady = false;
    bool m_startRequested = false;
    int m_peerClockPingCount = 0; // 房主观测到的本轮有效时钟请求数量。
    qint64 m_sendSeq = 0;
    qint64 m_lastSeq = 0;
    qint64 m_lastReceivedMs = 0;
    qint64 m_lastHeartbeatMs = 0;
    qint64 m_stateSinceMs = 0;
    qint64 m_pendingPingMs = -1;
    qint64 m_targetMs = -1;
    qint64 m_hostTargetMs = -1;
    bool m_localStarted = false;
    bool m_peerStarted = false;
    qint64 m_lastSnapshotMs = -100;
    qint64 m_peerMusicTimeMs = -1;
    PlayStats m_peerStats{};
    PlayStats m_localFinal{};
    PlayStats m_peerFinal{};
    bool m_localFinished = false;
    bool m_peerFinished = false;
    bool m_confirmed = false;
    QJsonObject m_result;
    qint64 m_lastResultSentMs = 0;
};

} // namespace nc
