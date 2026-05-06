#ifndef VISUAL_H
#define VISUAL_H

#include <stdint.h>

/* ── 可视化方言 ─────────────────────────────────────────────
 * 将 TORK 的运行时状态渲染为人类可读的视觉图像。
 * 当前版本：7 色轮播 BMP，作为"我还活着"的视觉宣示。
 *
 * 文件输出（每 tick 更新）：
 *   /tmp/tork_face.bmp   — 64×64 24-bit BMP
 *   /tmp/tork_face.ts    — 最后一次更新的 tick 号
 * ──────────────────────────────────────────────────────── */

/* 输出路径 */
#define VIS_OUTPUT_DIR  "/tmp"
#define VIS_BMP_FILE    VIS_OUTPUT_DIR "/tork_face.bmp"
#define VIS_TS_FILE     VIS_OUTPUT_DIR "/tork_face.ts"

/* 图像尺寸 */
#define VIS_WIDTH       64
#define VIS_HEIGHT      64
#define VIS_SIZE        (VIS_WIDTH * VIS_HEIGHT * 3)  /* 24-bit RGB */

/* 七种颜色（彩虹色序） */
#define VIS_COLORS      7
extern const uint8_t VIS_RAINBOW[VIS_COLORS][3];

/* 每 tick 生成一帧 */
void visual_tick(uint32_t tick, uint8_t drive, float fear, float desire, float curiosity,
                 int peer_count);

#endif /* VISUAL_H */
