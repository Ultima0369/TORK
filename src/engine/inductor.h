#ifndef INDUCTOR_H
#define INDUCTOR_H

#include <stdint.h>

/* Rule shared memory at 0x302000 (third page after blackboard) */
#define RULE_BASE      0x302000
#define RULE_SIZE      4096
#define RULE_MAGIC     0x494E4400  /* "IND\0" */
#define RULE_VERSION   1

#define RULE_OFF_MAGIC    0x000
#define RULE_OFF_VERSION  0x004
#define RULE_OFF_COUNT    0x008
#define RULE_OFF_DATA     0x010

#define RULE_MAX         32
#define RULE_STRUCT_SIZE 96  /* intentional symmetry with soul size */

#define RULE_TYPE_CODE       1
#define RULE_TYPE_TEMP       2
#define RULE_TYPE_INSTINCT   3

/* Confidence thresholds */
#define RULE_CONFIDENCE_ACTIVE   80
#define RULE_CONFIDENCE_RETIRE   30

struct tork_rule {
    uint8_t  active;           /* 1=activated, 0=pending verification */
    uint8_t  type;             /* 1=code, 2=temp, 3=instinct */
    uint8_t  confidence;       /* 0-100 */
    uint8_t  premise_len;
    uint8_t  conclusion_len;
    char     premise[32];
    char     conclusion[32];
    uint16_t from_tick;
    uint16_t apply_count;
    uint16_t fail_count;
    uint8_t  _reserved[21];   /* pad to 96 bytes */
} __attribute__((packed));

/* Initialize rule shared memory. Returns 0 on success. */
int ind_init(void);

/* Extract experiences from blackboard into rule candidates. Returns count. */
int ind_extract_experiences(struct tork_rule *rules, int max_rules);

/* Generalize from existing experiences into a new abstract rule. Returns 0 on success. */
int ind_generalize(const struct tork_rule *existing, int count,
                   struct tork_rule *new_rule);

/* Test a rule against an asm file. Returns 0=success, 1=fail, -1=error. */
int ind_test_rule(struct tork_rule *rule, const char *asm_file);

/* Load all rules from shared memory. Returns count. */
int ind_load_rules(struct tork_rule *out, int max);

/* Save a rule to shared memory. Returns slot index, or -1 on failure. */
int ind_save_rule(const struct tork_rule *rule);

/* Find a pending rule (active=0, confidence>60) for testing. Returns slot or -1. */
int ind_find_pending(void);

/* Find an active rule for application. Returns slot or -1. */
int ind_find_active(void);

/* Update rule in shared memory at given slot. Returns 0 on success. */
int ind_update_rule(int slot, const struct tork_rule *rule);

/* Get count of active rules. */
int ind_active_count(void);

/* Cleanup shared memory. */
void ind_cleanup(void);

#endif
