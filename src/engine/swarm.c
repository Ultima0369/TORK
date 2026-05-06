#include "swarm.h"
#include "beacon.h"
#include "../learning/distributed.h"

int swarm_init(void) {
    /* 网络服务已在 init_services 中启动，无需额外初始化 */
    return 0;
}

int swarm_beacon_count(void) {
    return beacon_global_count();
}

int swarm_dist_count(void) {
    return dist_peer_count();
}
