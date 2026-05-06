#include "swarm.h"
#include "beacon.h"
#include "../learning/distributed.h"

int swarm_init(void) {
    /* 无需额外初始化，分布式系统已在 init_services 中启动 */
    return 0;
}

int swarm_peer_count(void) {
    int b = beacon_global_count();
    int d = dist_peer_count();
    /* 合并：取较大值。同一同类可能被两套系统同时记录 */
    return (b > d) ? b : d;
}
