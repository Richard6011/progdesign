#ifndef NC_NETWORK_H
#define NC_NETWORK_H
#include "json.h"
#include "nc.h"
#define NET_BUFFER 65540
typedef enum {
    NET_IDLE,
    NET_LISTEN,
    NET_CONNECT,
    NET_ROOM,
    NET_COUNTDOWN,
    NET_PLAY,
    NET_WAIT,
    NET_DONE,
    NET_ABORT
} NetState;
typedef struct {
    uintptr_t listener, socket;
    NetState state;
    bool host, handshake, ready, peer_ready, configured, peer_verified;
    bool peer_finished, confirmed;
    int song, difficulty, revision;
    char name[128], peer[128], match[65], config_hash[65], audio_hash[65], chart_id[128];
    int count;
    Stats peer_stats;
    int peer_time;
    unsigned char rx[NET_BUFFER], tx[NET_BUFFER * 2];
    size_t rxlen, txlen;
    int send_seq, recv_seq;
    int64_t last_rx, last_ping, since, target, last_stats, ping_time, offset;
    int samples;
    int64_t best_rtt;
    int port;
    char status[NC_ERROR];
} Network;
/**
 * @brief 初始化本进程使用的 Winsock 2.2。
 * 返回：WSAStartup 成功返回 true，否则返回 false。
 * 详见：network.c 的同名函数定义（含参数、步骤及边界说明）。
 */
bool net_init(char *error);
/**
 * @brief 归还本模块持有的一次 Winsock 初始化引用。
 * 返回：无返回值。
 * 详见：network.c 的同名函数定义（含参数、步骤及边界说明）。
 */
void net_shutdown(void);
/**
 * @brief 把未持有资源的会话重置为初始值。
 * 返回：无返回值；状态为 NET_IDLE，socket 为 INVALID_SOCKET，最佳 RTT 为
 * INT64_MAX。
 * 详见：network.c 的同名函数定义（含参数、步骤及边界说明）。
 */
void net_reset(Network *net);
/**
 * @brief 创建只接纳一位客人的 IPv4 TCP 房间。
 * 返回：监听与非阻塞设置成功返回 true，否则返回 false。
 * 详见：network.c 的同名函数定义（含参数、步骤及边界说明）。
 */
bool net_host(Network *net, int port, const char *name, char *error);
/**
 * @brief 异步连接指定房主 IPv4 和端口。
 * 返回：立即成功或已启动非阻塞连接返回 true，参数/创建/立即连接失败返回 false。
 * 详见：network.c 的同名函数定义（含参数、步骤及边界说明）。
 */
bool net_join(Network *net, const char *ip, int port, const char *name, char *error);
/**
 * @brief 关闭监听和对端连接，回到空闲会话。
 * 返回：无返回值；句柄关闭后调用 net_reset 清空全部本局状态。
 * 详见：network.c 的同名函数定义（含参数、步骤及边界说明）。
 */
void net_leave(Network *net);
/**
 * @brief 尽力发送中止通知，然后关闭并中止本地会话。
 * 返回：无返回值，最终状态为 NET_ABORT 且不再确认成绩。
 * 详见：network.c 的同名函数定义（含参数、步骤及边界说明）。
 */
void net_abort(Network *net, const char *reason);
/**
 * @brief 在主循环中非阻塞推进一个完整网络会话。
 * 返回：无返回值；新消息、超时或错误通过 n->state/status/统计字段反映。
 * 详见：network.c 的同名函数定义（含参数、步骤及边界说明）。
 */
void net_poll(Network *net);
/**
 * @brief 由房主公布本轮曲目与资源指纹。
 * 返回：满足状态并成功入队返回 true，否则返回 false。
 * 详见：network.c 的同名函数定义（含参数、步骤及边界说明）。
 */
bool net_configure(Network *net, int song, int difficulty, const Chart *chart, char *error);
/**
 * @brief 客人核对本地谱面，并向房主确认资源匹配。
 * 返回：房主侧直接返回 true；客人指纹与数量匹配并入队成功返回 true，否则返回 false。
 * 详见：network.c 的同名函数定义（含参数、步骤及边界说明）。
 */
bool net_verify(Network *net, const Chart *chart, char *error);
/**
 * @brief 切换本机准备状态并通知对方。
 * 返回：前置条件满足且消息入队成功返回 true，否则返回 false。
 * 详见：network.c 的同名函数定义（含参数、步骤及边界说明）。
 */
bool net_ready(Network *net, char *error);
/**
 * @brief 由房主为已准备的双方安排三秒后的共同起播。
 * 返回：满足条件并将 start 入队返回 true，否则返回 false。
 * 详见：network.c 的同名函数定义（含参数、步骤及边界说明）。
 */
bool net_start(Network *net, char *error);
/**
 * @brief 以约 100ms 间隔发布本机累计比分统计。
 * 返回：无返回值；不满足阶段、已提交结束或未到节流间隔时不发送。
 * 详见：network.c 的同名函数定义（含参数、步骤及边界说明）。
 */
void net_stats(Network *net, const Stats *stats, int time);
/**
 * @brief 提交本机完整成绩并进入等待确认阶段。
 * 返回：成功入队最终统计返回 true；不完整、重复或状态非法返回 false。
 * 详见：network.c 的同名函数定义（含参数、步骤及边界说明）。
 */
bool net_finish(Network *net, const Stats *stats, char *error);
/**
 * @brief 由已完成对局的房主发起再来一局。
 * 返回：合法并将 rematch 请求入队返回 true，否则返回 false。
 * 详见：network.c 的同名函数定义（含参数、步骤及边界说明）。
 */
bool net_rematch(Network *net, char *error);
/**
 * @brief 列出本机主机名解析出的 IPv4 地址供手动加入。
 * 返回：无返回值；失败输出为空字符串，成功为以斜杠分隔的地址列表。
 * 详见：network.c 的同名函数定义（含参数、步骤及边界说明）。
 */
void net_addresses(char *out, size_t capacity);
/* Testable frame codec: big-endian length, bounded UTF-8 JSON body. */
/**
 * @brief 从累计字节中提取一个限长 JSON 消息帧。
 * 返回：返回 0 表示半包，1 表示完整帧，-1 表示非法；成功时 *used=4+正文长度。
 * 详见：network.c 的同名函数定义（含参数、步骤及边界说明）。
 */
int net_frame(const unsigned char *data, size_t length, cJSON **out, size_t *used);
#endif
