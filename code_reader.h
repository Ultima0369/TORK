#ifndef CODE_READER_H
#define CODE_READER_H

int asm_read_file(const char *path, char *buf, int buf_size);

int asm_count_insns_in_func(const char *buf, int len, const char *func_name);

int asm_extract_opcodes(const char *buf, int len, const char *func_name,
                        char opcodes[][8], int max_count);

int asm_classify_insns(const char *buf, int len, const char *func_name,
                       int *count_mov, int *count_arith,
                       int *count_control, int *count_other);

#endif
