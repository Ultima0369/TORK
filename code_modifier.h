#ifndef CODE_MODIFIER_H
#define CODE_MODIFIER_H

int asm_replace_operand(char *buf, int len, const char *func_name,
                        const char *old_op, const char *new_op,
                        int occurrence);

int asm_verify_modification(const char *buf, int len, const char *work_dir);

int asm_rollback(char *buf, int len, const char *backup, int backup_len);

#endif
