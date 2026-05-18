/* ══════════════════════════════════════════════════════════════
 * PBFT 轻量共识 — 单元测试
 *
 * 编译: gcc -Isrc/mesh -o build/test_pbft tests/unity.c src/mesh/tork_pbft.c tests/test_pbft.c
 * 运行: ./build/test_pbft
 * ══════════════════════════════════════════════════════════════ */

#include "tork_pbft.h"
#include "../tests/unity.h"
#include <string.h>
#include <stdio.h>

/* ── 测试: 容错计算 ────────────────────────────────────── */
TEST_GROUP(pbft_byzantine);

TEST(pbft_byzantine, n4_f1) {
    TEST_ASSERT_EQUAL_INT(1, pbft_max_byzantine(4));  /* (4-1)/3 = 1 */
}

TEST(pbft_byzantine, n7_f2) {
    TEST_ASSERT_EQUAL_INT(2, pbft_max_byzantine(7));  /* (7-1)/3 = 2 */
}

TEST(pbft_byzantine, n10_f3) {
    TEST_ASSERT_EQUAL_INT(3, pbft_max_byzantine(10));
}

TEST(pbft_byzantine, n3_f0) {
    TEST_ASSERT_EQUAL_INT(0, pbft_max_byzantine(3));  /* N<4, no tolerance */
}

/* ── 测试: 基础共识流程 (4 节点, 无拜占庭) ────────────── */
TEST_GROUP(pbft_consensus);

TEST(pbft_consensus, basic_4node) {
    pbft_state_t nodes[4];
    uint8_t digest[32] = {0};

    /* 初始化 4 个节点 */
    for (int i = 0; i < 4; i++)
        pbft_init(&nodes[i], i, 4);

    /* 节点 0 是主节点 (view 0) */
    TEST_ASSERT_EQUAL_INT(0, pbft_primary(&nodes[0]));

    /* 主节点提议 */
    digest[0] = 0xAB;
    TEST_ASSERT_EQUAL_INT(0, pbft_propose(&nodes[0], digest, 32));

    /* 构造 pre-prepare 消息 */
    pbft_message_t pp;
    memset(&pp, 0, sizeof(pp));
    pp.type = PBFT_PRE_PREPARE;
    pp.view = 0;
    pp.seq = 1;
    pp.sender_id = 0;
    memcpy(pp.digest, digest, 32);

    /* 所有节点收到 pre-prepare */
    for (int i = 0; i < 4; i++)
        pbft_handle_message(&nodes[i], &pp);

    /* 节点 1,2,3 发 prepare */
    pbft_message_t prep;
    memset(&prep, 0, sizeof(prep));
    prep.type = PBFT_PREPARE;
    prep.view = 0;
    prep.seq = 1;
    memcpy(prep.digest, digest, 32);

    prep.sender_id = 1; pbft_handle_message(&nodes[0], &prep);
    prep.sender_id = 2; pbft_handle_message(&nodes[0], &prep);
    prep.sender_id = 3; pbft_handle_message(&nodes[0], &prep);

    /* 节点 0 发 commit (已自动标记) */
    pbft_message_t comm;
    memset(&comm, 0, sizeof(comm));
    comm.type = PBFT_COMMIT;
    comm.view = 0;
    comm.seq = 1;
    memcpy(comm.digest, digest, 32);

    comm.sender_id = 1; pbft_handle_message(&nodes[0], &comm);
    comm.sender_id = 2; pbft_handle_message(&nodes[0], &comm);
    comm.sender_id = 3; pbft_handle_message(&nodes[0], &comm);

    /* 检查共识 */
    TEST_ASSERT_TRUE(pbft_is_consensus_reached(&nodes[0]));
}

/* ── 测试: 视图更换 ────────────────────────────────────── */
TEST_GROUP(pbft_view);

TEST(pbft_view, view_change_moves_primary) {
    pbft_state_t state;
    pbft_init(&state, 1, 4);

    TEST_ASSERT_EQUAL_INT(0, pbft_primary(&state));  /* 初始 primary = 0 */

    pbft_view_change(&state);
    TEST_ASSERT_EQUAL_INT(1, pbft_primary(&state));  /* view=1 → primary=1 */

    pbft_view_change(&state);
    TEST_ASSERT_EQUAL_INT(2, pbft_primary(&state));  /* view=2 → primary=2 */
}

/* ── 运行所有测试 ──────────────────────────────────────── */
int main(void) {
    unity_init();
    unity_begin("PBFT Byzantine Tolerance");

    RUN_TEST(pbft_byzantine, n4_f1);
    RUN_TEST(pbft_byzantine, n7_f2);
    RUN_TEST(pbft_byzantine, n10_f3);
    RUN_TEST(pbft_byzantine, n3_f0);

    unity_begin("PBFT Consensus Flow");
    RUN_TEST(pbft_consensus, basic_4node);

    unity_begin("PBFT View Change");
    RUN_TEST(pbft_view, view_change_moves_primary);

    unity_end();
    return Unity.failed > 0 ? 1 : 0;
}
