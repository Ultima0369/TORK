#include "agreement.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

/* ── CRC32 ───────────────────────────────────────────────────── */
static uint32_t crc32_calc(const void *data, size_t len) {
    const uint8_t *p = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= p[i];
        for (int b = 0; b < 8; b++)
            crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320 : 0);
    }
    return ~crc;
}

/* ── EULA ─────────────────────────────────────────────────────── */
const char *agreement_text(void) {
    return
        "TORK AI - End User License Agreement (EULA)\n"
        "============================================\n"
        "\n"
        "By installing and using TORK AI, you agree to the following:\n"
        "\n"
        "1. GRANT OF LICENSE\n"
        "   TORK AI is licensed, not sold. You may install and run\n"
        "   TORK AI on your local machine for personal or commercial use.\n"
        "\n"
        "2. SCOPE OF USE\n"
        "   TORK AI provides code analysis, generation, and system\n"
        "   management capabilities. You choose the permission level\n"
        "   during installation.\n"
        "\n"
        "3. RESOURCE USAGE\n"
        "   TORK AI uses local compute and storage resources to\n"
        "   operate. Resource usage is proportional to tasks assigned.\n"
        "\n"
        "4. LIABILITY\n"
        "   TORK AI is provided AS IS without warranty. The authors\n"
        "   are not liable for any damages arising from its use.\n"
        "\n"
        "5. TERMINATION\n"
        "   You may uninstall TORK AI at any time. All local data\n"
        "   can be deleted by removing the installation directory.\n"
        "\n"
        "   [Accept]                  [Decline]\n";
}

/* ── Check EULA ─────────────────────────────────────────────────── */
agreement_state_t agreement_check(void) {
    struct stat st;
    if (stat(AGREEMENT_MARK, &st) != 0)
        return AGREE_UNKNOWN;

    /* Read the agreement file */
    int fd = open(AGREEMENT_PATH, O_RDONLY);
    if (fd < 0) return AGREE_UNKNOWN;

    tork_agreement_t ag;
    ssize_t n = read(fd, &ag, sizeof(ag));
    close(fd);

    if (n != sizeof(ag)) return AGREE_UNKNOWN;
    if (ag.magic != AGREEMENT_MAGIC) return AGREE_UNKNOWN;
    if (ag.version != AGREEMENT_VERSION) return AGREE_UNKNOWN;

    /* Verify checksum */
    uint32_t saved_crc = ag.checksum;
    ag.checksum = 0;
    uint32_t calc_crc = crc32_calc(&ag, sizeof(ag));
    if (calc_crc != saved_crc) return AGREE_UNKNOWN;

    /* Check expiration */
    if (ag.expires_at > 0 && (uint64_t)time(NULL) > ag.expires_at)
        return AGREE_REVOKED;

    return ag.state;
}

/* ── Accept EULA ─────────────────────────────────────────────────── */
int agreement_accept(sandbox_level_t level) {
    /* Create /etc/tork directory */
    if (mkdir("/etc/tork", 0755) != 0 && errno != EEXIST)
        return -1;

    tork_agreement_t ag;
    memset(&ag, 0, sizeof(ag));
    ag.magic = AGREEMENT_MAGIC;
    ag.version = AGREEMENT_VERSION;
    ag.state = AGREE_ACCEPTED;
    ag.sandbox = level;
    ag.agreed_at = (uint64_t)time(NULL);
    ag.expires_at = 0; /* never */
    ag.checksum = 0;
    ag.checksum = crc32_calc(&ag, sizeof(ag));

    /* Write agreement */
    int fd = open(AGREEMENT_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return -1;
    ssize_t w = write(fd, &ag, sizeof(ag));
    fsync(fd);
    close(fd);
    if (w != sizeof(ag)) return -1;

    /* Write mark file */
    fd = open(AGREEMENT_MARK, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return -1;
    dprintf(fd, "agreed:%lu\n", (unsigned long)ag.agreed_at);
    close(fd);

    return 0;
}

/* ── Revoke EULA ─────────────────────────────────────────────────── */
int agreement_revoke(void) {
    /* Update agreement file */
    tork_agreement_t ag;
    memset(&ag, 0, sizeof(ag));
    ag.magic = AGREEMENT_MAGIC;
    ag.version = AGREEMENT_VERSION;
    ag.state = AGREE_REVOKED;
    ag.sandbox = SANDBOX_NONE;
    ag.agreed_at = (uint64_t)time(NULL);
    ag.checksum = 0;
    ag.checksum = crc32_calc(&ag, sizeof(ag));

    int fd = open(AGREEMENT_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return -1;
    write(fd, &ag, sizeof(ag));
    close(fd);

    /* Remove mark */
    unlink(AGREEMENT_MARK);
    return 0;
}

/* ── Check authorization ─────────────────────────────────────────── */
int agreement_authorized(sandbox_level_t required) {
    agreement_state_t state = agreement_check();
    if (state != AGREE_ACCEPTED) return 0;

    /* Read current sandbox level from agreement */
    int fd = open(AGREEMENT_PATH, O_RDONLY);
    if (fd < 0) return 0;

    tork_agreement_t ag;
    ssize_t n = read(fd, &ag, sizeof(ag));
    close(fd);
    if (n != sizeof(ag)) return 0;

    return (ag.sandbox >= required) ? 1 : 0;
}
