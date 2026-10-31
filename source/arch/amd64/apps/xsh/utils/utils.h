/* XASMOS - xsh utilities: one place declares every cmd_*_main.
 * Path: source/arch/amd64/apps/xsh/utils/utils.h */
#ifndef XSH_UTILS_H
#define XSH_UTILS_H

int cmd_form_main(const char *flag, const char *path);
int cmd_remove_main(const char *path);
int cmd_echo_main(const char *text);
int cmd_mem_main(void);
int cmd_tokens_main(void);
int cmd_stat_main(const char *path);
int cmd_hexdump_main(const char *path);

#endif
