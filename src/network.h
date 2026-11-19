#ifndef NC_NETWORK_H
#define NC_NETWORK_H
#include "nc.h"

typedef enum { NET_IDLE, NET_LISTEN, NET_CONNECT, NET_ROOM, NET_ABORT } NetState;

typedef struct {
    int32_t type, value, difficulty, final;
    char text[128];
} NetPacket;

typedef struct {
    uintptr_t listener, socket;
    NetState state;
    bool host, handshake, peer_started, peer_final;
    int port, difficulty, peer_score;
    char name[128], peer[128], song[128], status[NC_ERROR];
    NetPacket rx;
    unsigned char tx[32 * sizeof(NetPacket)];
    size_t rxlen, txlen;
    int64_t since, next_score;
} Network;

bool net_init(char *error);
void net_shutdown(void);
void net_reset(Network *net);
bool net_open(Network *net, const char *ip, int port, const char *name, char *error);
void net_leave(Network *net);
void net_poll(Network *net);
bool net_configure(Network *net, const char *song, int difficulty);
void net_score(Network *net, int score, bool final);
void net_addresses(char *out, size_t capacity);
#endif
