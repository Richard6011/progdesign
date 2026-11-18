#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include "network.h"
#include <stdio.h>
#include <string.h>

#define INVALID_NET ((uintptr_t)INVALID_SOCKET)
enum {
    HELLO = 1, CONFIG, VERIFIED, READY, PING, PONG, START, SCORE, FINISH, RESULT, REMATCH, ABORT
};

_Static_assert(sizeof(NetPacket) == 160, "The Windows x64 LAN packet must be 160 bytes");

/**
 * @brief 初始化本进程使用的 Winsock 2.2。
 * 参数：err 为可空错误缓冲区。
 * 返回：WSAStartup 成功返回 true，否则返回 false。
 * 说明：应在首次 socket 操作前调用，并在所有连接清理后配对 net_shutdown。
 */
bool net_init(char *err) {
    WSADATA data;
    return WSAStartup(MAKEWORD(2, 2), &data) == 0 || nc_error(err, "无法初始化 Winsock");
}

/**
 * @brief 归还本模块持有的一次 Winsock 初始化引用。
 * 参数：无参数。
 * 返回：无返回值。
 * 说明：调用前应先用 net_leave 关闭实际 socket。
 */
void net_shutdown(void) { WSACleanup(); }

/**
 * @brief 把未持有资源的会话重置为初始值。
 * 参数：n 为新对象，或其所有 socket 已经关闭的对象。
 * 返回：无返回值。状态为 NET_IDLE，收发缓冲、配置和计时清零。
 * 说明：已有连接的会话应先通过 net_leave 释放句柄。
 */
void net_reset(Network *n) {
    *n = (Network){.listener = INVALID_NET, .socket = INVALID_NET, .best_rtt = INT64_MAX};
}

/**
 * @brief 关闭一个可选的 Winsock 句柄。
 * 参数：s 为会话中的监听或连接句柄槽位。
 * 返回：无返回值。关闭后将槽位置为 INVALID_NET。
 */
static void close_socket(uintptr_t *s) {
    if (*s != INVALID_NET)
        closesocket((SOCKET)*s);
    *s = INVALID_NET;
}

/**
 * @brief 关闭监听和对端连接，回到空闲会话。
 * 参数：n 为经 net_reset 初始化的会话。
 * 返回：无返回值。
 * 说明：结束未完赛的对局时可先调用 net_abort，向对端发送中止通知。
 */
void net_leave(Network *n) {
    close_socket(&n->listener);
    close_socket(&n->socket);
    net_reset(n);
}

/**
 * @brief 记录网络失败并释放连接资源。
 * 参数：n 为会话；reason 为界面要显示的原因。
 * 返回：无返回值。会话转入 NET_ABORT。
 */
static void fail(Network *n, const char *reason) {
    close_socket(&n->listener);
    close_socket(&n->socket);
    n->state = NET_ABORT;
    nc_copy(n->status, sizeof(n->status), reason);
}

/**
 * @brief 将一条定长消息添加到待发队列。
 * 参数：n 为已连接会话；packet 为已填写的消息。
 * 返回：入队成功返回 true；队列满时返回 false。
 * 说明：队列保存消息副本，由 net_poll 实际发送。
 */
static bool enqueue(Network *n, NetPacket packet) {
    if (n->txlen + sizeof(packet) > sizeof(n->tx)) {
        fail(n, "发送失败，未确认对局已中止");
        return false;
    }
    memcpy(n->tx + n->txlen, &packet, sizeof(packet));
    n->txlen += sizeof(packet);
    return true;
}

/**
 * @brief 推进待发队列，保留本轮未写完的字节。
 * 参数：n 为会话，包含非阻塞连接和待发数据。
 * 返回：无返回值。暂不可写时留待下一帧，连接错误时结束会话。
 * 说明：一次 send 可以只发送部分字节，剩余字节的顺序必须保持。
 */
static void flush(Network *n) {
    if (n->txlen == 0)
        return;
    int sent = send((SOCKET)n->socket, (char *)n->tx, (int)n->txlen, 0);
    if (sent > 0) {
        n->txlen -= (size_t)sent;
        memmove(n->tx, n->tx + sent, n->txlen);
    } else if (WSAGetLastError() != WSAEWOULDBLOCK) {
        fail(n, "发送失败，未确认对局已中止");
    }
}

/**
 * @brief 通知对端对局中止，再关闭本地连接。
 * 参数：n 为会话；reason 为本地显示的原因。
 * 返回：无返回值。对端可以由中止消息或 TCP 断开得知会话结束。
 */
void net_abort(Network *n, const char *reason) {
    if (n->socket != INVALID_NET && enqueue(n, (NetPacket){.type = ABORT}))
        flush(n);
    fail(n, reason);
}

/**
 * @brief 把新建或接收的 TCP socket 设为非阻塞。
 * 参数：s 为有效的 Winsock 句柄。
 * 返回：ioctlsocket 成功返回 true。
 */
static bool nonblocking(SOCKET s) {
    u_long enabled = 1;
    return ioctlsocket(s, FIONBIO, &enabled) == 0;
}

/**
 * @brief 把已经建立的 TCP 连接接入房间并发送昵称。
 * 参数：n 为会话；s 为已经切到非阻塞模式的对端连接。
 * 返回：无返回值。接管 s 并等待对端 HELLO。
 * 说明：游戏每次只有一位客人，接入后即可关闭监听 socket；TCP_NODELAY 减少比分的发送延迟。
 */
static void attach(Network *n, SOCKET s) {
    close_socket(&n->listener);
    n->socket = s;
    n->state = NET_ROOM;
    n->last_rx = nc_now();
    n->next_ping = n->last_rx + 500;
    BOOL enabled = TRUE;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char *)&enabled, sizeof(enabled));
    NetPacket hello = {.type = HELLO, .value = NC_PROTOCOL};
    nc_copy(hello.text, sizeof(hello.text), n->name);
    nc_copy(n->status, sizeof(n->status), "已连接，正在校验资源与时钟");
    enqueue(n, hello);
}

/**
 * @brief 创建房间或异步连接房主。
 * 参数：n 为已初始化的会话；ip 为空指针时创建房间，否则为房主 IPv4；
 * port 为端口，建房时的 0 允许系统为测试分配端口；name 为昵称；err 可空。
 * 返回：监听成功或连接已开始返回 true。非阻塞连接在 net_poll 中确认结果。
 */
bool net_open(Network *n, const char *ip, int port, const char *name, char *err) {
    net_leave(n);
    struct sockaddr_in addr = {.sin_family = AF_INET, .sin_port = htons((u_short)port)};
    int min_port = ip ? 1 : 0;
    if (port < min_port || port > 65535 || (ip && inet_pton(AF_INET, ip, &addr.sin_addr) != 1))
        return nc_error(err, "请输入有效 IPv4 地址和端口");
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET)
        return nc_error(err, "创建 socket 失败");
    if (!nonblocking(s)) {
        closesocket(s);
        return nc_error(err, "设置非阻塞连接失败");
    }
    n->host = ip == NULL;
    nc_copy(n->name, sizeof(n->name), name);
    if (n->host) {
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) || listen(s, 1)) {
            closesocket(s);
            return nc_error(err, "端口被占用或无法监听");
        }
        int len = sizeof(addr);
        getsockname(s, (struct sockaddr *)&addr, &len);
        n->port = ntohs(addr.sin_port);
        n->listener = s;
        n->state = NET_LISTEN;
        nc_copy(n->status, sizeof(n->status), "等待另一位玩家加入");
    } else if (connect(s, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
        attach(n, s);
    } else if (WSAGetLastError() == WSAEWOULDBLOCK) {
        n->socket = s;
        n->state = NET_CONNECT;
        n->since = nc_now();
        nc_copy(n->status, sizeof(n->status), "正在连接房主……");
    } else {
        closesocket(s);
        return nc_error(err, "连接失败");
    }
    return n->state != NET_ABORT || nc_error(err, "%s", n->status);
}

/**
 * @brief 由房主公布曲目、难度和资源指纹。
 * 参数：n 为已握手房间；song/difficulty 为曲目和难度下标；c 为本地已加载谱面。
 * 返回：配置消息成功入队返回 true。
 * 说明：修改配置会清除旧准备状态。text 的前后各 64 字节分别是谱面和音频摘要。
 */
bool net_configure(Network *n, int song, int difficulty, const Chart *c) {
    NetPacket packet = {.type = CONFIG, .revision = ++n->revision, .value = song,
                        .extra = difficulty};
    n->ready = n->peer_ready = n->peer_verified = false;
    memcpy(packet.text, c->hash, 64);
    memcpy(packet.text + 64, c->audio_hash, 64);
    return enqueue(n, packet);
}

/**
 * @brief 客人对照房主发布的资源指纹，并回复本轮配置已加载。
 * 参数：n 为已接收配置的客人；c 为已加载且音频已打开的本地谱面。
 * 返回：资源一致且确认消息入队返回 true；不一致时退出房间。
 */
bool net_verify(Network *n, const Chart *c) {
    if (memcmp(c->hash, n->fingerprints, 64) || memcmp(c->audio_hash, n->fingerprints + 64, 64)) {
        net_abort(n, "歌曲或谱面资源不一致");
        return false;
    }
    n->peer_verified = enqueue(n, (NetPacket){.type = VERIFIED, .revision = n->revision});
    return n->peer_verified;
}

/**
 * @brief 切换本机准备状态并通知对方。
 * 参数：n 为当前会话；err 为界面错误缓冲区。
 * 返回：已校验资源且消息已入队返回 true。
 * 说明：准备消息携带配置修订号；一方换曲后，旧修订号的准备消息会被忽略。
 */
bool net_ready(Network *n, char *err) {
    if (n->state != NET_ROOM || !n->peer_verified)
        return nc_error(err, "请先完成资源校验");
    n->ready = !n->ready;
    return enqueue(n, (NetPacket){.type = READY, .revision = n->revision, .value = n->ready});
}

/**
 * @brief 由房主为已准备的双方安排三秒后的共同起播。
 * 参数：n 为当前会话；err 为界面错误缓冲区。
 * 返回：已准备且开局消息入队返回 true。
 * 说明：双方以各自的单调时钟起播，客人用时钟采样换算房主的目标时刻。
 */
bool net_start(Network *n, char *err) {
    if (!n->host || n->state != NET_ROOM || !n->ready || !n->peer_ready || n->samples < 3)
        return nc_error(err, "需要双方准备并完成时钟同步");
    nc_uuid(n->match);
    n->target = nc_now() + 3000;
    NetPacket packet = {.type = START, .time = n->target, .revision = n->revision};
    nc_copy(packet.text, sizeof(packet.text), n->match);
    n->state = NET_COUNTDOWN;
    return enqueue(n, packet);
}

/**
 * @brief 约每 100ms 发送一个累计分数。
 * 参数：n 为会话；score 为引擎按当前谱面计算的百万分制分数。
 * 返回：无返回值。
 * 说明：对手界面只显示累计分数，判定计数仍由本机引擎记录。
 */
void net_score(Network *n, int score) {
    int64_t now = nc_now();
    if (n->state == NET_PLAY && now >= n->next_score) {
        n->next_score = now + 100;
        enqueue(n, (NetPacket){.type = SCORE, .value = score});
    }
}

/**
 * @brief 客人收齐双方成绩后发出结果确认。
 * 参数：n 为会话；NET_WAIT 表示本机已提交，peer_finished 表示已收到对方成绩。
 * 返回：无需应答或应答已入队时返回 true。
 * 说明：FINISH 先于 RESULT 入队，房主按 TCP 顺序接收。
 */
static bool acknowledge(Network *n) {
    return n->host || n->state != NET_WAIT || !n->peer_finished ||
           enqueue(n, (NetPacket){.type = RESULT});
}

/**
 * @brief 提交本机完赛分数并等待确认。
 * 参数：n 为会话；score 为引擎完赛分数。
 * 返回：最终比分与适用的确认消息已入队时返回 true。
 * 说明：本机只从 NET_PLAY 提交一次，双方确认后应用层才保存成绩。
 */
bool net_finish(Network *n, int score) {
    if (n->state != NET_PLAY || !enqueue(n, (NetPacket){.type = FINISH, .value = score}))
        return false;
    n->state = NET_WAIT;
    n->since = nc_now();
    return acknowledge(n);
}

/**
 * @brief 复用现有连接，清掉上一局的比分与准备状态。
 * 参数：n 为完成对局的会话。
 * 返回：无返回值。双方回到房间加载同一份新配置。
 * 说明：保留昵称、连接和时钟采样，以便直接进行下一局。
 */
static void reset_room(Network *n) {
    n->state = NET_ROOM;
    n->ready = n->peer_ready = n->peer_verified = n->peer_finished = false;
    n->revision = n->peer_score = 0;
    n->next_score = 0;
    nc_copy(n->status, sizeof(n->status),
            n->host ? "房间已重置，重新加载谱面" : "房主请求再来一局");
}

/**
 * @brief 由房主通知两端回到房间。
 * 参数：n 为房主会话；err 为界面错误缓冲区。
 * 返回：复赛通知成功入队返回 true。
 * 说明：后续配置走同一条 TCP 连接，客人会先收到复赛通知再收到配置。
 */
bool net_rematch(Network *n, char *err) {
    if (!n->host || n->state != NET_DONE || n->socket == INVALID_NET)
        return nc_error(err, "等待房主请求再来一局");
    if (!enqueue(n, (NetPacket){.type = REMATCH}))
        return false;
    reset_room(n);
    return true;
}

/**
 * @brief 处理一条完整的局域网对战消息。
 * 参数：n 为收到消息的一端；p 为累计完整的一条定长消息。
 * 返回：消息已处理返回 true，无法用于当前会话时返回 false。
 * 说明：请求和确认按 TCP 顺序到达。换曲和准备使用 revision 识别旧状态。
 */
static bool process(Network *n, const NetPacket *p) {
    if (p->type == ABORT) {
        fail(n, "对方退出或中止了对局");
        return true;
    }
    if (!n->handshake) {
        if (p->type != HELLO || p->value != NC_PROTOCOL)
            return false;
        memcpy(n->peer, p->text, sizeof(n->peer) - 1);
        n->handshake = true;
        nc_copy(n->status, sizeof(n->status), "双方已连接，请由房主选择曲目");
        return true;
    }

    switch (p->type) {
    case PING:
        if (n->samples < 3)
            n->samples++;
        return enqueue(n, (NetPacket){.type = PONG, .time = nc_now(), .echo = p->time});
    case PONG: {
        int64_t now = nc_now();
        /* 往返最短的采样更接近单向延迟对称的情况。 */
        if (p->echo >= 0 && p->echo <= now && p->time >= 0) {
            int64_t rtt = now - p->echo;
            if (rtt < n->best_rtt) {
                n->best_rtt = rtt;
                n->offset = p->time - (p->echo + rtt / 2);
            }
        }
        return true;
    }
    case CONFIG:
        if (n->host || n->state != NET_ROOM || p->value < 0 || p->value >= NC_SONGS ||
            p->extra < 0 || p->extra >= 3 || p->revision <= n->revision)
            return false;
        n->song = p->value;
        n->difficulty = p->extra;
        n->revision = p->revision;
        n->ready = n->peer_ready = n->peer_verified = false;
        memcpy(n->fingerprints, p->text, sizeof(n->fingerprints));
        nc_copy(n->status, sizeof(n->status), "收到房主配置，正在校验资源");
        return true;
    case VERIFIED:
        if (p->revision == n->revision)
            n->peer_verified = true;
        return true;
    case READY:
        if (p->revision == n->revision)
            n->peer_ready = p->value != 0;
        return true;
    case START: {
        int64_t now = nc_now();
        uint64_t lead = (uint64_t)p->time - (uint64_t)n->offset - (uint64_t)now;
        /* 正常开局计划提前三秒；无符号差值同时过滤过时和超远的计划。 */
        if (n->host || n->state != NET_ROOM || !n->ready || p->revision != n->revision ||
            lead > 10000)
            return false;
        n->target = now + (int64_t)lead;
        memcpy(n->match, p->text, sizeof(n->match) - 1);
        n->state = NET_COUNTDOWN;
        return true;
    }
    case SCORE:
    case FINISH:
        if (n->state < NET_COUNTDOWN || n->state > NET_WAIT || n->peer_finished ||
            p->value < 0 || p->value > 1000000)
            return false;
        n->peer_score = p->value;
        if (p->type == FINISH) {
            n->peer_finished = true;
            return acknowledge(n);
        }
        return true;
    case RESULT:
        if (n->state != NET_WAIT || !n->peer_finished ||
            (n->host && !enqueue(n, (NetPacket){.type = RESULT})))
            return false;
        n->state = NET_DONE;
        nc_copy(n->status, sizeof(n->status), "双方成绩已确认");
        return true;
    case REMATCH:
        if (n->host || n->state != NET_DONE)
            return false;
        reset_room(n);
        return true;
    default:
        return false;
    }
}

/**
 * @brief 非阻塞地推进监听、连接、收发和倒计时。
 * 参数：n 为经 net_reset 初始化的会话，每帧调用一次。
 * 返回：无返回值。连接失败和超时以 NET_ABORT 及状态文案反映。
 * 说明：每次 recv 最多补齐当前消息。不足一条的字节留给下次轮询。进入 NET_DONE 时交还应用层，
 * 让应用先保存成绩，再接着处理可能紧随其后的复赛通知。
 */
void net_poll(Network *n) {
    int64_t now = nc_now();
    if (n->state == NET_LISTEN) {
        SOCKET peer = accept((SOCKET)n->listener, NULL, NULL);
        if (peer != INVALID_SOCKET) {
            if (nonblocking(peer))
                attach(n, peer);
            else {
                closesocket(peer);
                fail(n, "设置非阻塞连接失败");
            }
        }
        return;
    }
    if (n->state == NET_CONNECT) {
        fd_set writes, errors;
        FD_ZERO(&writes);
        FD_ZERO(&errors);
        FD_SET((SOCKET)n->socket, &writes);
        FD_SET((SOCKET)n->socket, &errors);
        struct timeval timeout = {0};
        if (select(0, NULL, &writes, &errors, &timeout) == SOCKET_ERROR ||
            FD_ISSET((SOCKET)n->socket, &errors))
            fail(n, "连接失败，请检查房主地址、端口和防火墙");
        else if (FD_ISSET((SOCKET)n->socket, &writes))
            attach(n, (SOCKET)n->socket);
        else if (now - n->since > 8000)
            fail(n, "连接超时");
        return;
    }
    if (n->socket == INVALID_NET)
        return;

    NetState previous = n->state;
    for (int i = 0; i < 32; i++) {
        int bytes = recv((SOCKET)n->socket, (char *)&n->rx + n->rxlen,
                         (int)(sizeof(n->rx) - n->rxlen), 0);
        if (bytes <= 0) {
            if (bytes < 0 && WSAGetLastError() == WSAEWOULDBLOCK)
                break;
            if (n->state == NET_DONE)
                close_socket(&n->socket);
            else
                fail(n, "连接已断开，本局不保存");
            return;
        }
        n->rxlen += (size_t)bytes;
        if (n->rxlen < sizeof(n->rx))
            break;
        n->rxlen = 0;
        n->last_rx = now;
        if (!process(n, &n->rx) && n->state != NET_ABORT)
            fail(n, "协议版本、状态或数据校验失败");
        if (n->state == NET_ABORT)
            return;
        if (previous != NET_DONE && n->state == NET_DONE)
            break;
    }

    if (now - n->last_rx > 10000 || (n->state == NET_WAIT && now - n->since > 20000)) {
        fail(n, "心跳或结算确认超时，本局不保存");
        return;
    }
    if (!n->host && n->handshake && now >= n->next_ping) {
        n->next_ping = now + 500;
        if (!enqueue(n, (NetPacket){.type = PING, .time = now}))
            return;
    }
    if (n->state == NET_COUNTDOWN && now >= n->target)
        n->state = NET_PLAY;
    flush(n);
}

/**
 * @brief 列出本机主机名解析出的 IPv4 地址供手动加入。
 * 参数：out 为含 cap 字节的输出数组。
 * 返回：无返回值。成功时填入斜杠分隔的地址；解析失败时输出空字符串。
 * 说明：房主仍可按现场网络选择要告知客人的可达 IPv4 地址。
 */
void net_addresses(char *out, size_t cap) {
    char host[256];
    struct addrinfo hints = {.ai_family = AF_INET, .ai_socktype = SOCK_STREAM}, *addresses;
    *out = 0;
    if (gethostname(host, sizeof(host)) || getaddrinfo(host, NULL, &hints, &addresses))
        return;
    for (struct addrinfo *i = addresses; i; i = i->ai_next) {
        char ip[64];
        const struct sockaddr_in *a = (const struct sockaddr_in *)i->ai_addr;
        if (inet_ntop(AF_INET, &a->sin_addr, ip, sizeof(ip))) {
            size_t len = strlen(out);
            snprintf(out + len, cap - len, "%s%s", len ? " / " : "", ip);
        }
    }
    freeaddrinfo(addresses);
}
