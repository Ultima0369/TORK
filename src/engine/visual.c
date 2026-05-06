#include "visual.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

const uint8_t VIS_RAINBOW[VIS_COLORS][3] = {
    {255,   0,   0},  /* 红 */
    {255, 127,   0},  /* 橙 */
    {255, 255,   0},  /* 黄 */
    {  0, 255,   0},  /* 绿 */
    {  0,   0, 255},  /* 蓝 */
    { 75,   0, 130},  /* 靛 */
    {148,   0, 211}   /* 紫 */
};

void visual_tick(uint32_t tick, uint8_t drive, float fear, float desire,
                 float curiosity, int peer_count) {
    /* 选色：tick 决定基础色相，drive 微调亮度 */
    int ci = tick % VIS_COLORS;
    uint8_t r = VIS_RAINBOW[ci][0];
    uint8_t g = VIS_RAINBOW[ci][1];
    uint8_t b = VIS_RAINBOW[ci][2];

    /* drive 影响亮度：drive 越高越亮，drive 低则暗 */
    float drive_norm = (drive + 128) / 255.0f;  /* -128..127 → 0..1 */
    if (drive_norm < 0.1f) drive_norm = 0.1f;
    r = (uint8_t)(r * drive_norm);
    g = (uint8_t)(g * drive_norm);
    b = (uint8_t)(b * drive_norm);

    /* 构造 BMP 文件 (64×64, 24-bit, 无压缩) */
    uint8_t hdr[54] = {
        0x42, 0x4D,             /* 'BM' */
        0x36, 0x30, 0x00, 0x00, /* 文件总大小 54+12288=12342 */
        0x00, 0x00, 0x00, 0x00, /* 保留 */
        0x36, 0x00, 0x00, 0x00, /* 像素数据偏移 54 */
        0x28, 0x00, 0x00, 0x00, /* BITMAPINFOHEADER 大小 40 */
        0x40, 0x00, 0x00, 0x00, /* 宽度 64 */
        0x40, 0x00, 0x00, 0x00, /* 高度 64 */
        0x01, 0x00,             /* 颜色平面 1 */
        0x18, 0x00,             /* 每像素 24 位 */
        0x00, 0x00, 0x00, 0x00, /* 无压缩 */
        0x00, 0x30, 0x00, 0x00, /* 像素数据大小 12288 */
        0x13, 0x0B, 0x00, 0x00, /* 水平分辨率 2835 DPM */
        0x13, 0x0B, 0x00, 0x00, /* 垂直分辨率 2835 DPM */
        0x00, 0x00, 0x00, 0x00, /* 调色板颜色数 0 */
        0x00, 0x00, 0x00, 0x00  /* 重要颜色数 0 */
    };

    /* 像素数据：BMP 是 从下到上、BGR 格式 */
    uint8_t pixels[VIS_SIZE];
    for (int y = 0; y < VIS_HEIGHT; y++) {
        for (int x = 0; x < VIS_WIDTH; x++) {
            int idx = (y * VIS_WIDTH + x) * 3;
            /* 可在此叠加第二层纹理（预留） */
            pixels[idx + 0] = b;  /* B */
            pixels[idx + 1] = g;  /* G */
            pixels[idx + 2] = r;  /* R */
        }
    }

    /* 原子写入：临时文件 → rename */
    FILE *fp = fopen(VIS_BMP_FILE ".tmp", "wb");
    if (!fp) return;
    fwrite(hdr, 1, 54, fp);
    fwrite(pixels, 1, VIS_SIZE, fp);
    fclose(fp);
    rename(VIS_BMP_FILE ".tmp", VIS_BMP_FILE);

    /* 写入时间戳文件 */
    fp = fopen(VIS_TS_FILE ".tmp", "w");
    if (!fp) return;
    fprintf(fp, "%u\n", tick);
    fclose(fp);
    rename(VIS_TS_FILE ".tmp", VIS_TS_FILE);
}
