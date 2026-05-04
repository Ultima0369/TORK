#ifndef TORK_AGREEMENT_H
#define TORK_AGREEMENT_H

/*
 * TORK EULA — End User License Agreement
 *
 * Standard license agreement for TORK AI installation.
 * Users choose a sandbox permission level during setup.
 */

#include <stdint.h>

#define AGREEMENT_PATH  "/etc/tork/agreement.sig"
#define AGREEMENT_MARK  "/etc/tork/.agreed"

/* EULA version */
#define AGREEMENT_VERSION  1

/* Agreement states */
typedef enum {
    AGREE_UNKNOWN   = 0,  /* Not yet installed/prompted */
    AGREE_ACCEPTED  = 1,  /* User accepted */
    AGREE_REJECTED  = 2,  /* User declined */
    AGREE_REVOKED   = 3,  /* User revoked */
} agreement_state_t;

/* Sandbox permission levels */
typedef enum {
    SANDBOX_NONE    = 0,  /* No access */
    SANDBOX_READ    = 1,  /* File read */
    SANDBOX_SAFE    = 2,  /* Safe commands (ls, ps, cat, find...) */
    SANDBOX_NORMAL  = 3,  /* Standard commands + file write */
    SANDBOX_FULL    = 4,  /* Full access (requires explicit user consent) */
} sandbox_level_t;

/* Agreement file structure */
typedef struct __attribute__((packed)) {
    uint32_t magic;           /* AGREEMENT_MAGIC */
    uint32_t version;         /* AGREEMENT_VERSION */
    agreement_state_t state;  /* Current state */
    sandbox_level_t sandbox;  /* Granted sandbox level */
    uint64_t agreed_at;       /* Acceptance timestamp */
    uint64_t expires_at;      /* Expiration (0 = never) */
    uint32_t checksum;        /* CRC32 */
    uint8_t reserved[32];     /* Future use */
} tork_agreement_t;

#define AGREEMENT_MAGIC  0x4B524F54  /* "TORK" */

/* Check current EULA state */
agreement_state_t agreement_check(void);

/* Write EULA acceptance (requires root) */
int agreement_accept(sandbox_level_t level);

/* Revoke EULA */
int agreement_revoke(void);

/* Check if sandbox level is authorized */
int agreement_authorized(sandbox_level_t required);

/* Return EULA text (for installer UI) */
const char *agreement_text(void);

#endif /* TORK_AGREEMENT_H */
