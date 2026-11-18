#ifndef NC_NETWORK_H
#define NC_NETWORK_H
#include "nc.h"
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
/**
 * @brief 同一 Windows x64 版本交换的定长 TCP 消息。
 * 说明：value 表示版本、曲目、准备开关或分数，extra 表示难度。time/echo 用于校时和开局。
 * text 按消息类型存放昵称、两段各 64 字节的资源指纹或对局编号。
 */
typedef struct {
    int32_t type, value, extra, revision;
    int64_t time, echo;
    char text[128];
} NetPacket;
typedef struct {
    uintptr_t listener, socket;
    NetState state;
    bool host, handshake, ready, peer_ready, peer_verified, peer_finished;
    int song, difficulty, revision, peer_score;
    char name[128], peer[128], match[65], fingerprints[128];
    NetPacket rx;
    unsigned char tx[32 * sizeof(NetPacket)];
    size_t rxlen, txlen;
    int64_t last_rx, next_ping, since, target, next_score, offset;
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
 * @brief 创建房间或异步连接指定房主 IPv4 和端口。
 * 返回：监听或异步连接已启动返回 true，失败返回 false。
 * 详见：network.c 的同名函数定义（含参数、步骤及边界说明）。
 */
bool net_open(Network *net, const char *ip, int port, const char *name, char *error);
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
bool net_configure(Network *net, int song, int difficulty, const Chart *chart);
/**
 * @brief 客人核对本地谱面，并向房主确认资源匹配。
 * 返回：客人指纹匹配且消息成功入队返回 true，否则返回 false。
 * 详见：network.c 的同名函数定义（含参数、步骤及边界说明）。
 */
bool net_verify(Network *net, const Chart *chart);
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
 * @brief 以约 100ms 间隔发布本机累计分数。
 * 返回：无返回值。只在演奏阶段且到达发送时刻时入队。
 * 详见：network.c 的同名函数定义（含参数、步骤及边界说明）。
 */
void net_score(Network *net, int score);
/**
 * @brief 提交本机完整成绩并进入等待确认阶段。
 * 返回：成功入队最终分数返回 true；重复提交或入队失败返回 false。
 * 详见：network.c 的同名函数定义（含参数、步骤及边界说明）。
 */
bool net_finish(Network *net, int score);
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
#endif
