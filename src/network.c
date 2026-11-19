#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mstcpip.h>
#include "network.h"
#include <stdio.h>
#include <string.h>

#define INVALID_NET ((uintptr_t)INVALID_SOCKET)
enum { HELLO = 1, CONFIG, SCORE, EXIT };

_Static_assert(sizeof(NetPacket) == 144, "The Windows x64 LAN packet must be 144 bytes");

bool net_init(char *err) {
    WSADATA data;
    return WSAStartup(MAKEWORD(2, 2), &data) == 0 || nc_error(err, "无法初始化 Winsock");
}

void net_shutdown(void) { WSACleanup(); }

void net_reset(Network *n) {
    *n = (Network){.listener = INVALID_NET, .socket = INVALID_NET};
}

static void close_socket(uintptr_t *s) {
    if (*s != INVALID_NET)
        closesocket((SOCKET)*s);
    *s = INVALID_NET;
}

static void fail(Network *n, const char *reason) {
    close_socket(&n->listener);
    close_socket(&n->socket);
    n->state = NET_ABORT;
    nc_copy(n->status, sizeof(n->status), reason);
}

static bool enqueue(Network *n, NetPacket packet) {
    if (n->txlen + sizeof(packet) > sizeof(n->tx)) {
        fail(n, "发送缓冲区已满，联机断开");
        return false;
    }
    memcpy(n->tx + n->txlen, &packet, sizeof(packet));
    n->txlen += sizeof(packet);
    return true;
}

static bool flush(Network *n) {
    int sent = send((SOCKET)n->socket, (const char *)n->tx, (int)n->txlen, 0);
    if (sent > 0) {
        n->txlen -= (size_t)sent;
        memmove(n->tx, n->tx + sent, n->txlen);
        return true;
    }
    if (sent == 0 || WSAGetLastError() != WSAEWOULDBLOCK)
        fail(n, "发送失败，联机断开");
    return false;
}

void net_leave(Network *n) {
    if (n->state == NET_ROOM && enqueue(n, (NetPacket){.type = EXIT})) {
        // 尽量发送退出消息；来不及发完时，对端仍会感知连接关闭。
        while (n->txlen && flush(n)) {}
    }
    close_socket(&n->listener);
    close_socket(&n->socket);
    net_reset(n);
}

static bool nonblocking(SOCKET s) {
    u_long enabled = 1;
    return ioctlsocket(s, FIONBIO, &enabled) == 0;
}

static void attach(Network *n, SOCKET s) {
    close_socket(&n->listener);
    n->socket = s;
    n->state = NET_ROOM;
    n->since = nc_now();
    BOOL enabled = TRUE;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char *)&enabled, sizeof(enabled));
    // 让 TCP 检测无声断线，不需要游戏消息维持连接。
    struct tcp_keepalive keepalive = {.onoff = 1, .keepalivetime = 15000, .keepaliveinterval = 1000};
    DWORD bytes;
    WSAIoctl(s, SIO_KEEPALIVE_VALS, &keepalive, sizeof(keepalive), NULL, 0, &bytes, NULL, NULL);
    NetPacket hello = {.type = HELLO, .value = NC_PROTOCOL};
    nc_copy(hello.text, sizeof(hello.text), n->name);
    nc_copy(n->status, sizeof(n->status), "已连接，正在交换昵称……");
    enqueue(n, hello);
}

bool net_open(Network *n, const char *ip, int port, const char *name, char *err) {
    net_leave(n);
    struct sockaddr_in addr = {.sin_family = AF_INET, .sin_port = htons((u_short)port)};
    if (port < (ip ? 1 : 0) || port > 65535 ||
        (ip && inet_pton(AF_INET, ip, &addr.sin_addr) != 1))
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
        int len = sizeof(addr);
        if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) || listen(s, 1) ||
            getsockname(s, (struct sockaddr *)&addr, &len)) {
            closesocket(s);
            return nc_error(err, "端口被占用或无法监听");
        }
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
        return nc_error(err, "连接失败，请检查房主地址、端口和防火墙");
    }
    return n->state != NET_ABORT || nc_error(err, "%s", n->status);
}

static bool same_selection(const Network *n, const char *song, int difficulty) {
    return difficulty == n->difficulty && !strcmp(song, n->song);
}

static void select_song(Network *n, const char *song, int difficulty) {
    if (!same_selection(n, song, difficulty)) {
        nc_copy(n->song, sizeof(n->song), song);
        n->difficulty = difficulty;
        n->peer_started = n->peer_final = false;
        n->peer_score = 0;
        n->next_score = 0;
    }
}

bool net_configure(Network *n, const char *song, int difficulty) {
    if (n->state != NET_ROOM || !n->handshake || !n->host || !song[0] ||
        strlen(song) >= sizeof(n->song) || difficulty < 0 || difficulty >= 3)
        return false;
    NetPacket packet = {.type = CONFIG, .difficulty = difficulty};
    nc_copy(packet.text, sizeof(packet.text), song);
    if (!enqueue(n, packet))
        return false;
    select_song(n, song, difficulty);
    return true;
}

void net_score(Network *n, int score, bool final) {
    if (n->state != NET_ROOM || !n->handshake || !n->song[0])
        return;
    int64_t now = nc_now();
    if (final || now >= n->next_score) {
        NetPacket packet = {
            .type = SCORE, .value = score, .difficulty = n->difficulty, .final = final};
        nc_copy(packet.text, sizeof(packet.text), n->song);
        if (enqueue(n, packet))
            n->next_score = now + 100;
    }
}

static bool process(Network *n, const NetPacket *p) {
    if (p->type == EXIT) {
        fail(n, "对方已退出房间");
        return true;
    }
    if (!p->text[0] || !memchr(p->text, 0, sizeof(p->text)))
        return false;

    if (!n->handshake) {
        if (p->type != HELLO || p->value != NC_PROTOCOL)
            return false;
        nc_copy(n->peer, sizeof(n->peer), p->text);
        n->handshake = true;
        nc_copy(n->status, sizeof(n->status),
                n->host ? "已连接，可以选择曲目" : "已连接，等待房主选择曲目");
        return true;
    }
    if (p->difficulty < 0 || p->difficulty >= 3)
        return false;
    switch (p->type) {
    case CONFIG:
        if (n->host)
            return false;
        select_song(n, p->text, p->difficulty);
        nc_copy(n->status, sizeof(n->status), "已收到房主选择");
        return true;
    case SCORE:
        if (p->value < 0 || p->value > 1000000 || (p->final != 0 && p->final != 1))
            return false;
        // 选曲和开始可能交错，只接收当前曲目的比分。
        if (same_selection(n, p->text, p->difficulty) && !n->peer_final) {
            n->peer_started = true;
            n->peer_final = p->final != 0;
            n->peer_score = p->value;
        }
        return true;
    default:
        return false;
    }
}

void net_poll(Network *n) {
    if (n->state == NET_LISTEN) {
        SOCKET peer = accept((SOCKET)n->listener, NULL, NULL);
        if (peer == INVALID_SOCKET) {
            if (WSAGetLastError() != WSAEWOULDBLOCK)
                fail(n, "接受连接失败");
        } else if (nonblocking(peer)) {
            attach(n, peer);
        } else {
            closesocket(peer);
            fail(n, "设置非阻塞连接失败");
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
        else if (nc_now() - n->since > 8000)
            fail(n, "连接超时");
        return;
    }
    if (n->state != NET_ROOM)
        return;

    for (int i = 0; i < 32; i++) {
        int bytes = recv((SOCKET)n->socket, (char *)&n->rx + n->rxlen,
                         (int)(sizeof(n->rx) - n->rxlen), 0);
        if (bytes <= 0) {
            if (bytes < 0 && WSAGetLastError() == WSAEWOULDBLOCK)
                break;
            fail(n, "联机连接已断开");
            return;
        }
        n->rxlen += (size_t)bytes;
        if (n->rxlen < sizeof(n->rx))
            break;
        n->rxlen = 0;
        if (!process(n, &n->rx)) {
            fail(n, n->handshake ? "联机消息无效" : "昵称消息无效或游戏版本不一致");
            return;
        }
        if (n->state == NET_ABORT)
            return;
    }
    if (!n->handshake && nc_now() - n->since > 8000) {
        fail(n, "交换昵称超时，请确认双方使用同一版本");
        return;
    }
    if (n->txlen)
        flush(n);
}

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
