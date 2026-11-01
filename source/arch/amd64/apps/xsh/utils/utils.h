#ifndef XSH_UTILS_H
#define XSH_UTILS_H

/* Every coreutil in source/arch/amd64/apps/xsh/utils/ is declared here,
 * once. shell.c includes this header and nothing else for these names. */
int cmd_form_main(const char *flag, const char *path);
int cmd_remove_main(const char *path);
int cmd_echo_main(const char *text);
int cmd_mem_main(void);
int cmd_tokens_main(void);
int cmd_stat_main(const char *path);
int cmd_hexdump_main(const char *path);
int cmd_run_main(const char *name);
int cmd_pci_main(void);
int cmd_nvme_main(void);

#endif
