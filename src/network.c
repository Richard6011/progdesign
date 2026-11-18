#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include "network.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#define INVALID_NET ((uintptr_t)INVALID_SOCKET)
static bool queue(Network *n, const char *type, cJSON *payload);
/**
 * @brief 判断当前网络会话是否处在可配置房间阶段。
 * 参数：n 为有效 Network。
 * 返回：只有 state==NET_ROOM 时返回 true。
 * 说明：不检查握手、资源或准备标志，调用者在发配置/准备等操作前仍需核对相应条件；倒计时和游戏阶段均不算房间。
 */
static bool room(const Network *n) { return n->state == NET_ROOM; }
/**
 * @brief 更新网络模块用于界面显示的状态文字。
 * 参数：n 为可修改会话；s 为 UTF-8 状态文本。
 * 返回：无返回值，只替换 n->status。
 * 说明：使用固定 NC_ERROR 容量复制；不改变 NetState、不发消息，也不会中止连接。
 */
static void status(Network *n, const char *s) { nc_copy(n->status, sizeof(n->status), s); }
/**
 * @brief 初始化本进程使用的 Winsock 2.2。
 * 参数：err 为可空错误缓冲区。
 * 返回：WSAStartup 成功返回 true，否则返回 false。
 * 说明：应在首次 socket 操作前调用，并在所有连接清理后配对 net_shutdown；初始化不创建监听器，不访问对端。
 */
bool net_init(char *err) {
    WSADATA data;
    return WSAStartup(MAKEWORD(2, 2), &data) == 0 || nc_error(err, "无法初始化 Winsock");
}
/**
 * @brief 归还本模块持有的一次 Winsock 初始化引用。
 * 参数：无参数。
 * 返回：无返回值。
 * 说明：调用 WSACleanup；需要先 net_leave 关闭实际 socket，不能用本函数代替逐连接资源清理。
 */
void net_shutdown(void) { WSACleanup(); }
/**
 * @brief 把未持有资源的会话重置为初始值。
 * 参数：n 为新对象，或其所有 socket 已经关闭的对象。
 * 返回：无返回值；状态为 NET_IDLE，socket 为 INVALID_SOCKET，最佳 RTT 为
 * INT64_MAX。
 * 说明：只清内存，不关闭已有句柄；对仍连接的会话应调用 net_leave，否则会丢失句柄并泄漏连接资源。
 */
void net_reset(Network *n) {
    memset(n, 0, sizeof(*n));
    n->listener = n->socket = INVALID_NET;
    n->best_rtt = INT64_MAX;
}
/**
 * @brief 关闭一个 socket，并在原位置标记为无效。
 * 参数：s 指向以 uintptr_t 存放的 SOCKET 槽位。
 * 返回：无返回值。
 * 说明：已为 INVALID_NET 时跳过 closesocket，随后总写回无效值；不处理上层状态或通知对方。
 */
static void close_socket(uintptr_t *s) {
    if (*s != INVALID_NET)
        closesocket((SOCKET)*s);
    *s = INVALID_NET;
}
/**
 * @brief 关闭监听和对端连接，回到空闲会话。
 * 参数：n 为经 net_reset 初始化的会话。
 * 返回：无返回值；句柄关闭后调用 net_reset 清空全部本局状态。
 * 说明：不主动发送 abort；若希望告知对端中止原因，先 net_abort。函数不负责停止音乐或清理引擎，这些属于应用层。
 */
void net_leave(Network *n) {
    close_socket(&n->listener);
    close_socket(&n->socket);
    net_reset(n);
}
/**
 * @brief 在网络失败时关闭资源并记录中止状态。
 * 参数：n 为会话；s 为本地可读的失败原因。
 * 返回：无返回值；关闭 socket/监听器、置 NET_ABORT、清 confirmed 并写 status。
 * 说明：并不清空所有历史统计，供界面诊断；confirmed 被清除后应用层不得保存本局。该内部函数不再发送消息。
 */
static void failed(Network *n, const char *s) {
    close_socket(&n->listener);
    close_socket(&n->socket);
    n->state = NET_ABORT;
    n->confirmed = false;
    status(n, s);
}
/**
 * @brief 尝试将待发字节队列尽量写入非阻塞 socket。
 * 参数：n 提供 socket、tx 和 txlen。
 * 返回：无返回值；成功部分从队列头移除；遇暂不可写保留剩余内容。
 * 说明：TCP send 不保证一次发送全部数据，因此循环处理部分写入。WSAEWOULDBLOCK 属正常背压，其余错误调用
 * failed；不会等待对端，避免卡住渲染。
 */
static void flush(Network *n) {
    while (n->socket != INVALID_NET && n->txlen) {
        int sent = send((SOCKET)n->socket, (char *)n->tx, (int)n->txlen, 0);
        if (sent > 0) {
            memmove(n->tx, n->tx + sent, n->txlen - (size_t)sent);
            n->txlen -= (size_t)sent;
        } else {
            int error = WSAGetLastError();
            if (error != WSAEWOULDBLOCK)
                failed(n, "发送失败，未确认对局已中止");
            break;
        }
    }
}
/**
 * @brief 尽力发送中止通知，然后关闭并中止本地会话。
 * 参数：n 为会话；reason 为 UTF-8 中止原因。
 * 返回：无返回值，最终状态为 NET_ABORT 且不再确认成绩。
 * 说明：有连接时先排入 abort 并尝试一次 flush，再调用 failed
 * 关闭；原因保留在本机，对方处理通用中止通知或检测断线。
 */
void net_abort(Network *n, const char *reason) {
    if (n->socket != INVALID_NET) {
        queue(n, "abort", NULL);
        flush(n);
    }
    failed(n, reason);
}
/**
 * @brief 将 socket 切换到非阻塞模式。
 * 参数：s 为有效 SOCKET。
 * 返回：ioctlsocket 成功返回 true，否则返回 false。
 * 说明：FIONBIO=1 使 connect/accept/send/recv
 * 可返回暂不可用，由主循环后续轮询；本函数不取得句柄所有权。
 */
static bool nonblocking(SOCKET s) {
    u_long one = 1;
    return ioctlsocket(s, FIONBIO, &one) == 0;
}
/**
 * @brief 把已连接 socket 接入房间并启动握手。
 * 参数：n 已含本机角色和昵称，当前没有其他对端；s 为已连接且应已非阻塞的 socket。
 * 返回：无返回值；n 接管 s，置 NET_ROOM，初始化计时并发送 hello。
 * 说明：开启 TCP_NODELAY 以减少小消息延迟；排队的 hello 含昵称和房主角色，实际写入由
 * net_poll/flush 完成；握手完成前不视作可开始对局。
 */
static void attach(Network *n, SOCKET s) {
    n->socket = (uintptr_t)s;
    n->state = NET_ROOM;
    n->since = n->last_rx = n->last_ping = nc_now();
    n->ping_time = -1;
    BOOL yes = TRUE;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char *)&yes, sizeof(yes));
    cJSON *p = cJSON_CreateObject();
    cJSON_AddStringToObject(p, "name", n->name);
    cJSON_AddNumberToObject(p, "role", n->host ? 1 : 0);
    if (queue(n, "hello", p))
        status(n, "已连接，正在校验资源与时钟");
}
/**
 * @brief 创建只接纳一位客人的 IPv4 TCP 房间。
 * 参数：n 必须已初始化；port 为 0—65535（0 供测试请求系统分配端口）；name 为昵称；err 可空。
 * 返回：监听与非阻塞设置成功返回 true，否则返回 false。
 * 说明：先 net_leave 关闭旧会话，再绑定 INADDR_ANY、设置独占地址并监听。实际端口写入
 * n->port，成功状态为 NET_LISTEN；失败关闭本次临时 socket。
 */
bool net_host(Network *n, int port, const char *name, char *err) {
    net_leave(n);
    n->host = true;
    nc_copy(n->name, sizeof(n->name), name);
    if (port < 0 || port > 65535)
        return nc_error(err, "端口必须为 1—65535");
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET)
        return nc_error(err, "创建 socket 失败");
    struct sockaddr_in a = {0};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons((u_short)port);
    BOOL exclusive = TRUE;
    setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char *)&exclusive, sizeof(exclusive));
    if (bind(s, (struct sockaddr *)&a, sizeof(a)) || listen(s, 1) || !nonblocking(s)) {
        closesocket(s);
        return nc_error(err, "端口被占用或无法监听");
    }
    int len = sizeof(a);
    getsockname(s, (struct sockaddr *)&a, &len);
    n->port = ntohs(a.sin_port);
    n->listener = (uintptr_t)s;
    n->state = NET_LISTEN;
    n->since = nc_now();
    status(n, "等待另一位玩家加入");
    return true;
}
/**
 * @brief 异步连接指定房主 IPv4 和端口。
 * 参数：n 为初始化会话；ip 为点分 IPv4；port 为 1—65535；name 为昵称；err 可空。
 * 返回：立即成功或已启动非阻塞连接返回 true，参数/创建/立即连接失败返回 false。
 * 说明：true 不等于已入房：连接中状态为 NET_CONNECT，后续 net_poll 用 select 检查结果或 8
 * 秒超时；本函数不解析域名，也不自动发现房间。
 */
bool net_join(Network *n, const char *ip, int port, const char *name, char *err) {
    struct sockaddr_in a = {0};
    a.sin_family = AF_INET;
    a.sin_port = htons((u_short)port);
    if (port < 1 || port > 65535 || inet_pton(AF_INET, ip, &a.sin_addr) != 1)
        return nc_error(err, "请输入有效 IPv4 地址和端口");
    net_leave(n);
    nc_copy(n->name, sizeof(n->name), name);
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET)
        return nc_error(err, "创建 socket 失败");
    if (!nonblocking(s)) {
        closesocket(s);
        return nc_error(err, "设置非阻塞连接失败");
    }
    int rc = connect(s, (struct sockaddr *)&a, sizeof(a));
    if (rc == 0)
        attach(n, s);
    else if (WSAGetLastError() == WSAEWOULDBLOCK) {
        n->socket = (uintptr_t)s;
        n->state = NET_CONNECT;
        n->since = nc_now();
        status(n, "正在连接房主……");
    } else {
        closesocket(s);
        return nc_error(err, "连接失败");
    }
    return true;
}
/**
 * @brief 从累计字节中提取一个限长 JSON 消息帧。
 * 参数：d/len 为当前缓冲区；out 与 used 为非空输出指针。
 * 返回：返回 0 表示半包，1 表示完整帧，-1 表示非法；成功时 *used=4+正文长度。
 * 说明：四字节大端长度限定 1—65536，拒绝嵌入零、非法 UTF-8、非对象根及尾随非空白。成功 *out
 * 的所有权交给调用者 cJSON_Delete；函数不移动输入，也不验证协议状态信封。
 */
int net_frame(const unsigned char *d, size_t len, cJSON **out, size_t *used) {
    *out = NULL;
    *used = 0;
    if (len < 4)
        return 0;
    uint32_t size = (uint32_t)d[0] << 24 | (uint32_t)d[1] << 16 | (uint32_t)d[2] << 8 | d[3];
    if (!size || size > 65536)
        return -1;
    if (len < size + 4)
        return 0;
    if (memchr(d + 4, 0, size))
        return -1;
    wchar_t unicode[65536];
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, (const char *)d + 4, (int)size, unicode,
                             65536))
        return -1;
    const char *end = NULL;
    cJSON *o = cJSON_ParseWithLengthOpts((const char *)d + 4, size, &end, 0);
    if (!cJSON_IsObject(o)) {
        cJSON_Delete(o);
        return -1;
    }
    while (end < (const char *)d + 4 + size &&
           (*end == ' ' || *end == '\n' || *end == '\r' || *end == '\t'))
        end++;
    if (end != (const char *)d + 4 + size) {
        cJSON_Delete(o);
        return -1;
    }
    *out = o;
    *used = size + 4;
    return 1;
}
/**
 * @brief 封装一条协议消息并追加到有限发送队列。
 * 参数：n 为会话；type 为消息名；p 为负载对象或 NULL；传入后 p 所有权即转交本函数。
 * 返回：序列化并入队成功返回 true；分配或队列上限失败返回 false。
 * 说明：添加协议版本、递增 seq、当前 match 和 payload，写入大端长度前缀；NULL
 * 负载转为空对象。序列化失败或超过总队列上限时中止连接；入队后仍需由 flush 实际发送。
 */
static bool queue(Network *n, const char *type, cJSON *p) {
    if (!p)
        p = cJSON_CreateObject();
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "protocol", NC_PROTOCOL);
    cJSON_AddNumberToObject(o, "seq", ++n->send_seq);
    cJSON_AddStringToObject(o, "type", type);
    cJSON_AddStringToObject(o, "match", n->match);
    cJSON_AddItemToObject(o, "payload", p);
    char *text = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    if (!text) {
        failed(n, "无法生成网络消息");
        return false;
    }
    size_t size = strlen(text);
    if (size > 65536 || n->txlen + size + 4 > sizeof(n->tx)) {
        cJSON_free(text);
        failed(n, "发送队列超出限制");
        return false;
    }
    unsigned char *d = n->tx + n->txlen;
    d[0] = (unsigned char)(size >> 24);
    d[1] = (unsigned char)(size >> 16);
    d[2] = (unsigned char)(size >> 8);
    d[3] = (unsigned char)size;
    memcpy(d + 4, text, size);
    n->txlen += size + 4;
    cJSON_free(text);
    return true;
}
/**
 * @brief 由房主公布本轮曲目与资源指纹。
 * 参数：n 为已握手的房主房间；song/difficulty 为本地曲目下标与档位；c 为已加载且音频已预检的谱面；err
 * 可空。
 * 返回：满足状态并成功入队返回 true，否则返回 false。
 * 说明：递增 revision，记录数量/ID/谱面和音频哈希，清除双方准备及对端验证标志，再发送
 * config。修改配置会使旧准备失效，不传输实际音频文件。
 */
bool net_configure(Network *n, int song, int difficulty, const Chart *c, char *err) {
    if (!n->host || !room(n) || !n->handshake)
        return nc_error(err, "需要已连接的房主房间");
    n->song = song;
    n->difficulty = difficulty;
    n->revision++;
    n->count = c->count;
    n->ready = n->peer_ready = false;
    n->peer_verified = false;
    n->configured = true;
    nc_copy(n->config_hash, sizeof(n->config_hash), c->hash);
    nc_copy(n->audio_hash, sizeof(n->audio_hash), c->audio_hash);
    nc_copy(n->chart_id, sizeof(n->chart_id), c->id);
    cJSON *p = cJSON_CreateObject();
    cJSON_AddNumberToObject(p, "song", song);
    cJSON_AddNumberToObject(p, "difficulty", difficulty);
    cJSON_AddNumberToObject(p, "revision", n->revision);
    cJSON_AddNumberToObject(p, "rules", NC_RULES);
    cJSON_AddNumberToObject(p, "count", c->count);
    cJSON_AddStringToObject(p, "chart", c->id);
    cJSON_AddStringToObject(p, "chartHash", c->hash);
    cJSON_AddStringToObject(p, "audioHash", c->audio_hash);
    return queue(n, "config", p) || nc_error(err, "%s", n->status);
}
/**
 * @brief 客人核对本地谱面，并向房主确认资源匹配。
 * 参数：n 为已接收配置的会话；c 为本地完整加载谱面；err 可空。
 * 返回：房主侧直接返回 true；客人指纹与数量匹配并入队成功返回 true，否则返回 false。
 * 说明：客人检查谱面哈希、音频哈希、chart ID、数量；不匹配调用
 * net_abort。这里只核对传入数据，音频设备预加载必须由应用层先完成。
 */
bool net_verify(Network *n, const Chart *c, char *err) {
    if (n->host)
        return true;
    if (!n->configured || strcmp(c->hash, n->config_hash) || strcmp(c->audio_hash, n->audio_hash) ||
        strcmp(c->id, n->chart_id) || c->count != n->count) {
        net_abort(n, "歌曲或谱面资源不一致");
        return nc_error(err, "双方资源不一致，请使用相同 C 版程序和资源");
    }
    cJSON *p = cJSON_CreateObject();
    cJSON_AddNumberToObject(p, "revision", n->revision);
    cJSON_AddStringToObject(p, "chartHash", c->hash);
    cJSON_AddStringToObject(p, "audioHash", c->audio_hash);
    if (!queue(n, "verified", p))
        return nc_error(err, "%s", n->status);
    n->peer_verified = true;
    return true;
}
/**
 * @brief 切换本机准备状态并通知对方。
 * 参数：n 必须处于已配置且双方资源验证就绪的 NET_ROOM；err 可空。
 * 返回：前置条件满足且消息入队成功返回 true，否则返回 false。
 * 说明：在负载中带
 * revision，防止旧配置的准备消息生效；准备状态在本机先切换，真正可开始还需对方准备和时钟采样，不自动触发开局。
 */
bool net_ready(Network *n, char *err) {
    if (!room(n) || !n->configured || !n->peer_verified)
        return nc_error(err, "等待资源校验完成");
    n->ready = !n->ready;
    cJSON *p = cJSON_CreateObject();
    cJSON_AddNumberToObject(p, "revision", n->revision);
    cJSON_AddBoolToObject(p, "ready", n->ready);
    return queue(n, "ready", p);
}
/**
 * @brief 由房主为已准备的双方安排三秒后的共同起播。
 * 参数：n 必须为房主 NET_ROOM，双方已准备、资源一致且至少三个时钟样本；err 可空。
 * 返回：满足条件并将 start 入队返回 true，否则返回 false。
 * 说明：生成新 match 标识，target=本机单调时间+3000，状态进入
 * NET_COUNTDOWN。这里只安排时刻，不播放音频；客人用已估计 offset 映射到自身时钟。
 */
bool net_start(Network *n, char *err) {
    if (!n->host || !room(n) || !n->ready || !n->peer_ready || !n->peer_verified || n->samples < 3)
        return nc_error(err, "双方准备并完成时钟同步后才能开始");
    nc_uuid(n->match);
    n->target = nc_now() + 3000;
    n->state = NET_COUNTDOWN;
    n->since = nc_now();
    cJSON *p = cJSON_CreateObject();
    cJSON_AddNumberToObject(p, "target", (double)n->target);
    cJSON_AddNumberToObject(p, "revision", n->revision);
    return queue(n, "start", p);
}
/**
 * @brief 读取可被 JSON double 精确表示的非负整数时刻。
 * 参数：p 为负载对象；key 为字段名；out 为非空 int64_t 输出。
 * 返回：数值为整数且在 0—2^53-1 时返回 true 并写 out，否则返回 false。
 * 说明：避免先盲目强制转换大浮点数导致溢出或失去毫秒精度；失败不写输出，供 ping/pong/start 解析使用。
 */
static bool number64(const cJSON *p, const char *key, int64_t *out) {
    cJSON *v = cJSON_GetObjectItemCaseSensitive(p, key);
    if (!cJSON_IsNumber(v) || v->valuedouble < 0 || v->valuedouble > 9007199254740991.0 ||
        (double)(int64_t)v->valuedouble != v->valuedouble)
        return false;
    *out = (int64_t)v->valuedouble;
    return true;
}
/**
 * @brief 客人收齐双方最终成绩后向房主发送确认请求。
 * 参数：n 为会话；NET_WAIT 表示本机的 finish 已入队，peer_finished 表示已收到对方的 finish。
 * 返回：无需发送或 result_ack 成功入队时返回 true，入队失败返回 false。
 * 说明：本机提交和对方提交分别触发一次检查，后到的一项使条件成立。TCP 保证房主先收到客人的
 * finish，再收到 result_ack；客人仍需等待房主的 confirmed。
 */
static bool acknowledge_result(Network *n) {
    return n->host || n->state != NET_WAIT || !n->peer_finished || queue(n, "result_ack", NULL);
}
/**
 * @brief 校验并处理一条完整 JSON 协议消息。
 * 参数：n 为当前会话；o 为 net_frame 产出的借用 JSON 根对象，所有权仍归调用者。
 * 返回：信封、角色、时序与负载均合法返回 true；非法类型/顺序/统计返回 false。
 * 说明：检查协议与连续 seq，再按 hello、心跳、同步、配置、准备、开局、比分和结果握手更新状态。对局消息必须匹配
 * match；正常 abort 会置中止但返回 true。失败可能已写部分字段，net_poll 随即关闭会话，不尝试继续。
 */
static bool process(Network *n, cJSON *o) {
    int protocol, seq;
    char type[40], match[65];
    if (!json_int(o, "protocol", NC_PROTOCOL, NC_PROTOCOL, &protocol) ||
        !json_int(o, "seq", 1, 2000000000, &seq) || seq != n->recv_seq + 1 ||
        !json_str(o, "type", type, sizeof(type)))
        return false;
    cJSON *m = cJSON_GetObjectItemCaseSensitive(o, "match");
    if (!cJSON_IsString(m) || strlen(m->valuestring) >= sizeof(match))
        return false;
    nc_copy(match, sizeof(match), m->valuestring);
    cJSON *p = cJSON_GetObjectItemCaseSensitive(o, "payload");
    if (!cJSON_IsObject(p))
        return false;
    n->recv_seq = seq;
    n->last_rx = nc_now();
    if (!strcmp(type, "abort")) {
        failed(n, "对方退出或中止了对局");
        return true;
    }
    if (!strcmp(type, "hello")) {
        int role;
        if (n->handshake || !room(n) || !json_int(p, "role", 0, 1, &role) ||
            role == (n->host ? 1 : 0) || !json_str(p, "name", n->peer, sizeof(n->peer)))
            return false;
        n->handshake = true;
        status(n, "双方已连接，请由房主选择曲目");
        return true;
    }
    if (!n->handshake)
        return false;
    if (!strcmp(type, "heartbeat"))
        return true;
    if (!strcmp(type, "ping")) {
        int64_t sent;
        if (!n->host || !number64(p, "sent", &sent))
            return false;
        cJSON *r = cJSON_CreateObject();
        cJSON_AddNumberToObject(r, "sent", (double)sent);
        cJSON_AddNumberToObject(r, "host", (double)nc_now());
        n->samples++;
        return queue(n, "pong", r);
    }
    if (!strcmp(type, "pong")) {
        int64_t sent, host;
        if (n->host || !number64(p, "sent", &sent) || !number64(p, "host", &host) ||
            sent != n->ping_time)
            return false;
        int64_t rtt = nc_now() - sent;
        if (rtt < 0 || rtt > 5000)
            return false;
        if (rtt < n->best_rtt) {
            n->best_rtt = rtt;
            n->offset = host - (sent + rtt / 2);
        }
        n->samples++;
        n->ping_time = -1;
        return true;
    }
    if (!strcmp(type, "config")) {
        int rules, revision;
        if (n->host || !room(n) || !json_int(p, "revision", 1, 100000, &revision) ||
            revision <= n->revision || !json_int(p, "rules", NC_RULES, NC_RULES, &rules) ||
            !json_int(p, "song", 0, NC_SONGS - 1, &n->song) ||
            !json_int(p, "difficulty", 0, 2, &n->difficulty) ||
            !json_int(p, "count", 1, NC_NOTES, &n->count) ||
            !json_str(p, "chartHash", n->config_hash, sizeof(n->config_hash)) ||
            strlen(n->config_hash) != 64 ||
            !json_str(p, "audioHash", n->audio_hash, sizeof(n->audio_hash)) ||
            strlen(n->audio_hash) != 64 || !json_str(p, "chart", n->chart_id, sizeof(n->chart_id)))
            return false;
        n->revision = revision;
        n->configured = true;
        n->ready = n->peer_ready = n->peer_verified = false;
        return true;
    }
    if (!strcmp(type, "verified")) {
        int revision;
        char hash[65], audio[65];
        if (!n->host || !room(n) || !json_int(p, "revision", n->revision, n->revision, &revision) ||
            !json_str(p, "chartHash", hash, sizeof(hash)) ||
            !json_str(p, "audioHash", audio, sizeof(audio)) || strcmp(hash, n->config_hash) ||
            strcmp(audio, n->audio_hash))
            return false;
        n->peer_verified = true;
        status(n, "资源一致，双方可准备");
        return true;
    }
    if (!strcmp(type, "ready")) {
        int revision;
        cJSON *ready = cJSON_GetObjectItemCaseSensitive(p, "ready");
        if (!room(n) || !n->configured || !n->peer_verified ||
            !json_int(p, "revision", n->revision, n->revision, &revision) || !cJSON_IsBool(ready))
            return false;
        n->peer_ready = cJSON_IsTrue(ready);
        return true;
    }
    if (!strcmp(type, "start")) {
        int revision;
        int64_t target;
        if (n->host || !room(n) || !n->ready || !n->peer_ready || !n->peer_verified ||
            n->samples < 3 || strlen(match) != 32 ||
            !json_int(p, "revision", n->revision, n->revision, &revision) ||
            !number64(p, "target", &target))
            return false;
        target -= n->offset;
        if (target < nc_now() + 200 || target > nc_now() + 6000)
            return false;
        nc_copy(n->match, sizeof(n->match), match);
        n->target = target;
        n->state = NET_COUNTDOWN;
        n->since = nc_now();
        return true;
    }
    if (strcmp(match, n->match) || !n->match[0])
        return false;
    bool finishing = !strcmp(type, "finish");
    if (finishing || !strcmp(type, "stats")) {
        /* 允许对方略早进入演奏并发送实时比分，最终成绩等本地也进入演奏阶段才处理。 */
        if (n->peer_finished || (n->state != NET_PLAY && n->state != NET_WAIT &&
                                 (finishing || n->state != NET_COUNTDOWN)))
            return false;
        if (!finishing) {
            if (!json_int(p, "time", n->peer_time, 3601000, &n->peer_time))
                return false;
            p = cJSON_GetObjectItemCaseSensitive(p, "stats");
        }
        Stats s;
        if (!stats_parse(p, &s, n->count, finishing) || s.perfect < n->peer_stats.perfect ||
            s.great < n->peer_stats.great || s.good < n->peer_stats.good ||
            s.miss < n->peer_stats.miss || s.max_combo < n->peer_stats.max_combo)
            return false;
        n->peer_stats = s;
        n->peer_finished = finishing;
        return !finishing || acknowledge_result(n);
    }
    if (!strcmp(type, n->host ? "result_ack" : "confirmed")) {
        if (n->state != NET_WAIT || !n->peer_finished || (n->host && !queue(n, "confirmed", NULL)))
            return false;
        n->confirmed = true;
        n->state = NET_DONE;
        status(n, "双方成绩已确认");
        return true;
    }
    if (!strcmp(type, n->host ? "rematch_ack" : "rematch")) {
        /* 客人先确认上一局的 match，双方再各自清除上一局数据。 */
        if (n->state != NET_DONE || (!n->host && !queue(n, "rematch_ack", NULL)))
            return false;
        n->confirmed = n->peer_finished = false;
        n->state = NET_ROOM;
        n->ready = n->peer_ready = n->peer_verified = false;
        n->peer_time = 0;
        n->peer_stats = (Stats){0};
        n->match[0] = 0;
        n->revision = 0;
        n->configured = false;
        status(n, n->host ? "房间已重置，重新加载谱面" : "房主请求再来一局");
        return true;
    }
    return false;
}
/**
 * @brief 在主循环中非阻塞推进一个完整网络会话。
 * 参数：n 为已初始化会话，需高频重复调用。
 * 返回：无返回值；新消息、超时或错误通过 n->state/status/统计字段反映。
 * 说明：处理 accept/select、部分收发及半包粘包，每次最多 32 轮 recv；额外客人被拒绝。检查 5
 * 秒握手、8 秒连接、10 秒心跳和 20 秒结算等待，并每 500ms 发送同步或心跳。进入 NET_DONE 后先交给应用层保存，
 * 即使同批数据里已经收到复赛请求，也留到下一次轮询处理，避免提前清掉本局统计。
 */
void net_poll(Network *n) {
    int64_t now = nc_now();
    if (n->listener != INVALID_NET) {
        SOCKET s = accept((SOCKET)n->listener, NULL, NULL);
        if (s != INVALID_SOCKET) {
            if (n->socket == INVALID_NET) {
                nonblocking(s);
                attach(n, s);
            } else
                closesocket(s);
        }
    }
    if (n->state == NET_CONNECT) {
        fd_set writable, errors;
        FD_ZERO(&writable);
        FD_ZERO(&errors);
        FD_SET((SOCKET)n->socket, &writable);
        FD_SET((SOCKET)n->socket, &errors);
        struct timeval timeout = {0, 0};
        if (select(0, NULL, &writable, &errors, &timeout) > 0) {
            int err = 0, len = sizeof(err);
            getsockopt((SOCKET)n->socket, SOL_SOCKET, SO_ERROR, (char *)&err, &len);
            if (err || FD_ISSET((SOCKET)n->socket, &errors))
                failed(n, "连接失败，请检查房主地址、端口和防火墙");
            else
                attach(n, (SOCKET)n->socket);
        }
        if (n->state == NET_CONNECT && now - n->since > 8000)
            failed(n, "连接超时");
        return;
    }
    if (n->socket == INVALID_NET)
        return;
    flush(n);
    if (n->socket == INVALID_NET)
        return;
    NetState previous_state = n->state;
    /* 先解析已缓存的数据，再限制当帧接收次数，避免持续收包拖慢界面。 */
    for (int reads = 0; ; reads++) {
        while (n->rxlen) {
            cJSON *o;
            size_t used;
            int rc = net_frame(n->rx, n->rxlen, &o, &used);
            if (rc == 0)
                break;
            if (rc < 0) {
                failed(n, "收到非法数据帧，连接已中止");
                return;
            }
            bool ok = process(n, o);
            cJSON_Delete(o);
            if (!ok) {
                if (n->state != NET_ABORT)
                    failed(n, "协议版本、状态或数据校验失败");
                return;
            }
            memmove(n->rx, n->rx + used, n->rxlen - used);
            n->rxlen -= used;
            if (n->socket == INVALID_NET)
                return;
            if (previous_state != NET_DONE && n->state == NET_DONE) {
                flush(n);
                return;
            }
        }
        if (n->rxlen == sizeof(n->rx)) {
            failed(n, "接收缓冲超过限制");
            return;
        }
        if (reads == 32)
            break;
        int got =
            recv((SOCKET)n->socket, (char *)n->rx + n->rxlen, (int)(sizeof(n->rx) - n->rxlen), 0);
        if (got == 0) {
            if (n->confirmed) {
                close_socket(&n->socket);
                close_socket(&n->listener);
            } else
                failed(n, "连接已断开，本局不保存");
            return;
        }
        if (got < 0) {
            if (WSAGetLastError() != WSAEWOULDBLOCK) {
                failed(n, "网络连接错误，本局不保存");
                return;
            }
            break;
        }
        n->rxlen += (size_t)got;
    }
    if (now - n->last_rx > 10000) {
        failed(n, "心跳或结算确认超时，本局不保存");
        return;
    }
    if (!n->handshake && now - n->since > 5000) {
        failed(n, "握手超时");
        return;
    }
    if (now - n->last_ping >= 500) {
        n->last_ping = now;
        if (!n->host && n->handshake && n->ping_time < 0) {
            cJSON *p = cJSON_CreateObject();
            n->ping_time = now;
            cJSON_AddNumberToObject(p, "sent", (double)now);
            queue(n, "ping", p);
        } else
            queue(n, "heartbeat", NULL);
    }
    if (n->state == NET_COUNTDOWN && now >= n->target)
        n->state = NET_PLAY;
    if (n->state == NET_WAIT && now - n->since > 20000) {
        failed(n, "等待对方最终成绩超时，本局不保存");
        return;
    }
    flush(n);
}
/**
 * @brief 以约 100ms 间隔发布本机累计比分统计。
 * 参数：n 处于 NET_PLAY；s 为本机当前有效统计；time 为音乐毫秒位置。
 * 返回：无返回值；不满足阶段、已提交结束或未到节流间隔时不发送。
 * 说明：只交换累计统计与时间，不发送每次按键；对方在 process 中检查计数和时间不倒退。本函数不校验 s，预期其来自本机
 * Engine。
 */
void net_stats(Network *n, const Stats *s, int time) {
    if (n->state != NET_PLAY || nc_now() - n->last_stats < 100)
        return;
    n->last_stats = nc_now();
    cJSON *p = cJSON_CreateObject();
    cJSON_AddNumberToObject(p, "time", time);
    cJSON_AddItemToObject(p, "stats", stats_json(s));
    queue(n, "stats", p);
}
/**
 * @brief 提交本机完整成绩并进入等待确认阶段。
 * 参数：n 必须处于 NET_PLAY；s 必须通过完整 stats_valid；err 可空。
 * 返回：最终统计及适用的确认消息成功入队时返回 true；状态、统计或发送出错返回 false。
 * 说明：先入队 finish，再进入 NET_WAIT 并重置等待计时。客人收到房主最终统计后可发送 result_ack，
 * 应用层应等双方确认后保存。
 */
bool net_finish(Network *n, const Stats *s, char *err) {
    if (n->state != NET_PLAY || !stats_valid(s, n->count, true))
        return nc_error(err, "无法提交不完整的联机成绩");
    if (!queue(n, "finish", stats_json(s)))
        return nc_error(err, "最终成绩发送失败");
    n->state = NET_WAIT;
    n->since = nc_now();
    return acknowledge_result(n) || nc_error(err, "最终成绩确认发送失败");
}
/**
 * @brief 由已完成对局的房主发起再来一局。
 * 参数：n 必须为 NET_DONE、confirmed 的房主；err 可空。
 * 返回：合法并将 rematch 请求入队返回 true，否则返回 false。
 * 说明：不立即清空本机旧结果，需客人返回 rematch_ack 后才重置房间；客人也不能主动调用成功。消息沿现有 TCP
 * 连接发送，避免重新建连接。
 */
bool net_rematch(Network *n, char *err) {
    if (!n->host || n->state != NET_DONE)
        return nc_error(err, "等待房主请求再来一局");
    return queue(n, "rematch", NULL) || nc_error(err, "%s", n->status);
}
/**
 * @brief 列出本机主机名解析出的 IPv4 地址供手动加入。
 * 参数：out 为输出数组；cap 必须大于零。
 * 返回：无返回值；失败输出为空字符串，成功为以斜杠分隔的地址列表。
 * 说明：通过 gethostname/getaddrinfo 枚举
 * AF_INET，不保证某个地址对另一台电脑可达，也不执行房间发现；返回前释放解析器结果，超长列表按缓冲容量截断。
 */
void net_addresses(char *out, size_t cap) {
    char host[256];
    out[0] = 0;
    if (gethostname(host, sizeof(host)))
        return;
    struct addrinfo hints = {0}, *addresses = NULL;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, NULL, &hints, &addresses))
        return;
    for (struct addrinfo *i = addresses; i; i = i->ai_next) {
        char ip[64];
        struct sockaddr_in *a = (struct sockaddr_in *)i->ai_addr;
        inet_ntop(AF_INET, &a->sin_addr, ip, sizeof(ip));
        size_t len = strlen(out);
        snprintf(out + len, cap - len, "%s%s", len ? " / " : "", ip);
    }
    freeaddrinfo(addresses);
}
