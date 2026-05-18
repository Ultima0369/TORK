#include "mcts_persist.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>

/* ── 持久化文件头 ────────────────────────────────────────── */
typedef struct __attribute__((packed)) {
    uint32_t magic;              /* 0x4D435453 ("MCTS") */
    uint8_t  version;            /* 格式版本 */
    uint32_t node_count;         /* 保存的节点数 */
    float    exploration;        /* 探索常数 */
    int      min_iterations;     /* 最小迭代数 */
    int      tuning_count;       /* 自动调优次数 */
    uint32_t crc32;              /* 校验和 */
} mcts_persist_header_t;

#define MCTS_PERSIST_MAGIC  0x4D435453
#define MCTS_PERSIST_HEADER_SIZE sizeof(mcts_persist_header_t)

/* 简易 CRC32 (与 soul 模块对齐) */
static uint32_t crc32_simple(const unsigned char *data, size_t len) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++)
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
    }
    return ~crc;
}

/* ── 保存 ──────────────────────────────────────────────── */
int mcts_persist_save(const char *path) {
    if (!path) path = MCTS_PERSIST_PATH;

    /* 存储中间数据: 节点计数、调优参数等 */
    mcts_persist_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic = MCTS_PERSIST_MAGIC;
    hdr.version = MCTS_PERSIST_VERSION;
    hdr.exploration = mcts_get_exploration();
    hdr.min_iterations = mcts_get_min_iterations();
    hdr.tuning_count = mcts_tuning_count();
    hdr.node_count = 0;  /* 当前只存调优参数，不存完整树 */

    /* 计算校验和 */
    hdr.crc32 = crc32_simple((const unsigned char*)&hdr,
                             MCTS_PERSIST_HEADER_SIZE - 4);

    /* 确保目录存在 */
    char dir[256];
    snprintf(dir, sizeof(dir), "%s", path);
    char *slash = strrchr(dir, '/');
    if (slash) {
        *slash = '\0';
        mkdir(dir, 0755);
    }

    FILE *f = fopen(path, "wb");
    if (!f) return -1;

    size_t written = fwrite(&hdr, 1, sizeof(hdr), f);
    fclose(f);

    return (written == sizeof(hdr)) ? 0 : -1;
}

/* ── 加载 ──────────────────────────────────────────────── */
int mcts_persist_load(const char *path) {
    if (!path) path = MCTS_PERSIST_PATH;

    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    mcts_persist_header_t hdr;
    size_t n = fread(&hdr, 1, sizeof(hdr), f);
    fclose(f);

    if (n != sizeof(hdr)) return -1;
    if (hdr.magic != MCTS_PERSIST_MAGIC) return -1;
    if (hdr.version != MCTS_PERSIST_VERSION) return -1;

    /* 验证校验和 */
    uint32_t saved_crc = hdr.crc32;
    hdr.crc32 = 0;
    uint32_t calc_crc = crc32_simple((const unsigned char*)&hdr,
                                     MCTS_PERSIST_HEADER_SIZE - 4);
    if (calc_crc != saved_crc) return -1;

    /* 恢复调优参数 */
    if (hdr.exploration > 0.1f && hdr.exploration < 10.0f)
        mcts_set_exploration(hdr.exploration);

    /* 注意: min_iterations 无法直接设置，通过 auto_tune 间接恢复 */
    /* tuning_count 保存但不暴露设置接口 */

    return 0;
}

/* ── 重置 ──────────────────────────────────────────────── */
void mcts_persist_reset(void) {
    /* 重置调优参数到默认值 */
    mcts_set_exploration(1.414f);  /* 默认探索常数 */
    unlink(MCTS_PERSIST_PATH);
}
