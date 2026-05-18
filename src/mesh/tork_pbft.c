/* ══════════════════════════════════════════════════════════════
 * TORK 轻量 PBFT 共识 — 实现
 *
 * 实现简化版 PBFT:
 *   1. Pre-Prepare → 主节点提议
 *   2. Prepare → 各节点验证并广播
 *   3. Commit → 2f+1 prepare 后广播 commit
 *   4. Reply → 2f+1 commit 后达成共识
 *
 * 视图超时: 5 秒无进展 → view change
 * 最大节点: 16 (足够 T2T Mesh 使用)
 * ══════════════════════════════════════════════════════════════ */

#include "tork_pbft.h"
#include <string.h>
#include <stdio.h>

/* ── 容错计算 ──────────────────────────────────────────── */
int pbft_max_byzantine(int total_nodes) {
    if (total_nodes < 4) return 0;  /* N<4 无法容错 */
    return (total_nodes - 1) / 3;
}

/* ── 初始化 ────────────────────────────────────────────── */
void pbft_init(pbft_state_t *state, uint8_t node_id, uint8_t total_nodes) {
    memset(state, 0, sizeof(pbft_state_t));
    state->node_id = node_id;
    state->total_nodes = total_nodes;
    state->current_view = 0;
    state->seq_num = 0;
    state->primary_id = 0;       /* 视图 0 的主节点是节点 0 */
    state->view_timeout_ms = 5000;  /* 5 秒超时 */
    state->consensus_reached = 0;
}

/* ── 获取主节点 ────────────────────────────────────────── */
uint8_t pbft_primary(pbft_state_t *state) {
    return state->current_view % state->total_nodes;
}

/* ── 主节点发起提议 ────────────────────────────────────── */
int pbft_propose(pbft_state_t *state, const uint8_t *digest, uint32_t digest_len) {
    if (state->node_id != pbft_primary(state))
        return -1;  /* 不是主节点 */

    if (digest_len > 32) digest_len = 32;
    memset(state->final_digest, 0, 32);
    memcpy(state->final_digest, digest, digest_len);

    state->seq_num++;
    state->prepared_bitmap = 0;
    state->committed_bitmap = 0;
    state->consensus_reached = 0;
    state->view_start_ms = 0; /* 调用者需要设置时间基准 */

    /* 清空 prepare/commit 计数 */
    memset(state->prepare_count, 0, sizeof(state->prepare_count));
    memset(state->commit_count, 0, sizeof(state->commit_count));

    printf("  PBFT: propose seq=%d view=%d digest=", state->seq_num, state->current_view);
    for (int i = 0; i < 4; i++) printf("%02x", state->final_digest[i]);
    printf("...\n");

    return 0;
}

/* ── 处理消息 ──────────────────────────────────────────── */
int pbft_handle_message(pbft_state_t *state, const pbft_message_t *msg) {
    if (!state || !msg) return -1;
    if (msg->view != state->current_view)
        return -2;  /* 视图不匹配 */
    if (msg->sender_id >= state->total_nodes)
        return -3;  /* 无效节点 */

    int f = pbft_max_byzantine(state->total_nodes);
    int quorum = 2 * f + 1;  /* 需要的最少确认数 */

    switch (msg->type) {
        case PBFT_PRE_PREPARE: {
            /* 只有主节点发 pre-prepare */
            if (msg->sender_id != pbft_primary(state))
                return -4;

            /* 存储摘要 */
            memcpy(state->final_digest, msg->digest, 32);
            printf("  PBFT: receive PRE-PREPARE from node %d, seq=%d\n",
                   msg->sender_id, msg->seq);
            return 0;
        }

        case PBFT_PREPARE: {
            /* 标记该节点已 prepare */
            state->prepared_bitmap |= (1U << msg->sender_id);
            state->prepare_count[msg->sender_id]++;

            int count = 0;
            for (int i = 0; i < state->total_nodes; i++)
                if (state->prepared_bitmap & (1U << i)) count++;

            printf("  PBFT: PREPARE from node %d (%d/%d)\n",
                   msg->sender_id, count, quorum);

            if (count >= quorum && !(state->committed_bitmap & (1U << state->node_id))) {
                /* 收到足够 prepare → 自动 commit */
                state->committed_bitmap |= (1U << state->node_id);
                printf("  PBFT: COMMIT sent by node %d\n", state->node_id);
                return 1;  /* 提示调用者广播 commit */
            }
            return 0;
        }

        case PBFT_COMMIT: {
            state->committed_bitmap |= (1U << msg->sender_id);
            state->commit_count[msg->sender_id]++;

            int count = 0;
            for (int i = 0; i < state->total_nodes; i++)
                if (state->committed_bitmap & (1U << i)) count++;

            printf("  PBFT: COMMIT from node %d (%d/%d)\n",
                   msg->sender_id, count, quorum);

            if (count >= quorum && !state->consensus_reached) {
                state->consensus_reached = 1;
                printf("  PBFT: ✅ CONSENSUS REACHED! seq=%d view=%d\n",
                       state->seq_num, state->current_view);
                return 1;  /* 共识达成 */
            }
            return 0;
        }

        case PBFT_VIEW_CHANGE: {
            printf("  PBFT: VIEW-CHANGE from node %d, new view=%d\n",
                   msg->sender_id, msg->view + 1);
            return 0;
        }

        default:
            return -5;
    }
}

/* ── 检查共识 ──────────────────────────────────────────── */
int pbft_is_consensus_reached(pbft_state_t *state) {
    return state->consensus_reached ? 1 : 0;
}

/* ── 视图更换 ──────────────────────────────────────────── */
int pbft_view_change(pbft_state_t *state) {
    state->current_view++;
    state->primary_id = pbft_primary(state);
    state->view_start_ms = 0;  /* 调用者负责更新时间 */

    /* 重置共识状态 (保留 seq_num) */
    state->prepared_bitmap = 0;
    state->committed_bitmap = 0;
    state->consensus_reached = 0;
    memset(state->prepare_count, 0, sizeof(state->prepare_count));
    memset(state->commit_count, 0, sizeof(state->commit_count));

    printf("  PBFT: view change → view=%d, new primary=node %d\n",
           state->current_view, state->primary_id);
    return 0;
}

/* ── 重置 ──────────────────────────────────────────────── */
void pbft_reset(pbft_state_t *state) {
    state->seq_num++;
    state->prepared_bitmap = 0;
    state->committed_bitmap = 0;
    state->consensus_reached = 0;
    memset(state->prepare_count, 0, sizeof(state->prepare_count));
    memset(state->commit_count, 0, sizeof(state->commit_count));
    state->view_start_ms = 0;
}
