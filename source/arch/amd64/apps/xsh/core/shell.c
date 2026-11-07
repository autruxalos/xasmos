/******************************************************************************
 * XASMOS - Core Modular Shell Loop (XSH)
 * Path: source/arch/amd64/apps/xsh/core/shell.c
 *
 * Identity model (no authentication or persistence yet: this is a session
 * label, not a security boundary):
 *   xent     default account
 *   sprusr   superuser account, both a command and an account:
 *              sprusr            login as superuser (like 'su -')
 *              sprusr <cmd> ...  run one command as superuser, then return
 *                                (like 'sudo'); the current cwd is kept
 *              xent              login back as xent (like 'su - xent')
 *
 * Volumes are prepared before the shell starts (see init/exit). Commands that
 * act on volumes (mount, unmount, format, sync, install) name their device.
 ******************************************************************************/

/* One command line carries at most MAX_INPUT_LEN-1 characters. */
/* Every directive implemented in utils/ is declared once, in utils.h */
#include "../utils/utils.h"
#include "../utils/common.h"

#define MAX_INPUT_LEN 512
#define MAX_CMD_LEN   32
#define MAX_ARG_LEN   480
#define MAX_PATH_LEN  256
#define MAX_USER_LEN  16
#define READ_MAX      4096

#define DEFAULT_USER "xent"
#define SUPER_USER   "sprusr"

/* Kernel console and keyboard */
extern void xk_print(const char *str);
extern int  xk_println(const char *str);
extern int  xk_readline(char *buffer, int max_chars);
extern int  xk_strcmp(const char *s1, const char *s2);
extern void xk_clear(void);

/* Path resolution (path.c) */
extern void resolve_superdir_path(const char *cwd, const char *target, char *out_path);

/* EXFS: codes and prototypes come from exfs.h */
#include "exfs.h"

/* Every directive implemented in utils/ is declared once, in utils.h */
#include "../utils/utils.h"

static char current_cwd[MAX_PATH_LEN]  = "|";
static char current_user[MAX_USER_LEN] = DEFAULT_USER;
static int  is_superuser = 0;

static int dispatch(const char *command, const char *argument);

/* ---- helpers ---------------------------------------------------------- */

static void fail(const char *code, const char *detail) {
    xk_print(code);
    xk_print(": ");
    xk_println(detail);
}

/* Maps an EXFS error code to the shell's message. */
static void report(int rc) {
    switch (rc) {
    case EXFS_ENOENT:    fail("EXNOENT", "path unresolved"); break;
    case EXFS_EEXIST:    fail("EXEXIST", "target already bound"); break;
    case EXFS_ENOSPC:    fail("EXFULL", "no free space or node table full"); break;
    case EXFS_ENOMEM:    fail("EXNOMEM", "extent table or pool exhausted"); break;
    case EXFS_EISDIR:    fail("EXISDIR", "target is a directory"); break;
    case EXFS_ENOTEMPTY: fail("EXNOTEMPTY", "directory holds live nodes"); break;
    case EXFS_EINVAL:    fail("EXBADARG", "invalid name"); break;
    case EXFS_ENOFS:     fail("EXNOFS", "no volume mounted"); break;
    case EXFS_EBROKEN:   fail("EXBROKEN", "volume failed a commit: remount"); break;
    default:             fail("EXIOERR", "operation failed"); break;
    }
}

static int streq(const char *a, const char *b) {
    return xk_strcmp(a, b) == 0;
}

static void copy_str(char *dst, const char *src, int max) {
    int i = 0;
    while (src[i] != '\0' && i < max - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int str_len(const char *s) {
    int n = 0;
    while (s[n] != '\0') n++;
    return n;
}

static int is_root(const char *path) {
    return path[0] == '|' && path[1] == '\0';
}

/* Drops one trailing '|' (except for the root itself). */
static void normalize_path(char *p) {
    int n = str_len(p);
    if (n > 1 && p[n - 1] == '|') p[n - 1] = '\0';
}

static void parent_of(const char *path, char *out) {
    int last = 0;
    for (int i = 0; path[i] != '\0'; i++) {
        if (path[i] == '|') last = i;
    }
    if (last == 0) {
        out[0] = '|';
        out[1] = '\0';
        return;
    }
    int i = 0;
    for (; i < last; i++) out[i] = path[i];
    out[i] = '\0';
}

static void resolve_operand(const char *arg, char *out) {
    resolve_superdir_path(current_cwd, arg, out);
    normalize_path(out);
}

/* Splits "first rest..." into first (copied) and a pointer to rest. */
static const char *split_first(const char *s, char *first, int max) {
    int i = 0;
    while (s[i] != ' ' && s[i] != '\0' && i < max - 1) {
        first[i] = s[i];
        i++;
    }
    first[i] = '\0';
    while (s[i] == ' ') i++;
    return s + i;
}

static void parse_command_line(const char *line, char *cmd, char *arg) {
    const char *rest = split_first(line, cmd, MAX_CMD_LEN);
    copy_str(arg, rest, MAX_ARG_LEN);
    int n = str_len(arg);
    while (n > 0 && arg[n - 1] == ' ') arg[--n] = '\0';   /* trim trailing blanks */
}

static void set_identity(const char *name, int super) {
    copy_str(current_user, name, MAX_USER_LEN);
    is_superuser = super;
}

/* ---- directives ------------------------------------------------------- */

static int child_counter;

static void print_child(const char *name, int type) {
    xk_print("  ");
    xk_print(name);
    if (type == EXFS_TYPE_DIR) xk_print("|");
    xk_print("\n");
    child_counter++;
}

static void cmd_ver(void) {
    xk_println("XASMOS xeret 0.05 (amd64, UEFI)");
    xk_println("directives: ver clear list form remove locate whereami read write echo mem tokens stat hexdump run mount unmount format sync install pci nvme whoami sprusr xent halt");
}

static void cmd_whoami(void) {
    xk_print(current_user);
    xk_println(is_superuser ? " (superuser)" : "");
}

/* sprusr: login (no argument) or one-shot command (sudo-style). */
static int cmd_sprusr(const char *arg) {
    if (arg[0] == '\0') {
        set_identity(SUPER_USER, 1);
        copy_str(current_cwd, "|", MAX_PATH_LEN);      /* login resets to the root */
        xk_println("login: sprusr (superuser). 'xent' returns to the default account.");
        return 0;
    }

    char saved_user[MAX_USER_LEN];
    int  saved_super = is_superuser;
    copy_str(saved_user, current_user, MAX_USER_LEN);

    char cmd[MAX_CMD_LEN];
    const char *rest = split_first(arg, cmd, MAX_CMD_LEN);
    if (cmd[0] == '\0') {
        fail("EXBADARG", "usage: sprusr [command [args...]]");
        return 0;
    }

    set_identity(SUPER_USER, 1);
    int halted = dispatch(cmd, rest);              /* cwd is deliberately not touched */
    set_identity(saved_user, saved_super);
    return halted;
}

static void cmd_xent(void) {
    set_identity(DEFAULT_USER, 0);
    copy_str(current_cwd, "|", MAX_PATH_LEN);
    xk_println("login: xent");
}

static void cmd_locate(const char *arg) {
    char path[MAX_PATH_LEN];
    if (arg[0] == '\0') { fail("EXBADARG", "operand required"); return; }

    resolve_operand(arg, path);
    int st = exfs_stat(path);
    if (st < 0)              { report(st); return; }
    if (st != EXFS_TYPE_DIR) { fail("EXNOTDIR", "target is not a directory"); return; }
    copy_str(current_cwd, path, MAX_PATH_LEN);
}

static void cmd_list(const char *arg) {
    char path[MAX_PATH_LEN];
    if (arg[0] == '\0') copy_str(path, current_cwd, MAX_PATH_LEN);
    else                resolve_operand(arg, path);

    int st = exfs_stat(path);
    if (st < 0)               { report(st); return; }
    if (st == EXFS_TYPE_FILE) { xk_println(path); return; }

    child_counter = 0;
    exfs_list(path, print_child);
    if (child_counter == 0) xk_println("  (empty)");
}

/* form -file|-dir <path>: creates a file or a directory. */
static void cmd_form(const char *arg) {
    char flag[MAX_CMD_LEN];
    char path[MAX_PATH_LEN];

    const char *rest = split_first(arg, flag, MAX_CMD_LEN);
    if ((!streq(flag, "-file") && !streq(flag, "-dir")) || rest[0] == '\0') {
        fail("EXBADARG", "usage: form -file|-dir <path>");
        return;
    }

    resolve_operand(rest, path);
    int rc = cmd_form_main(flag, path);
    if (rc != EXFS_OK) report(rc);
}

static void cmd_remove(const char *arg) {
    char path[MAX_PATH_LEN];
    if (arg[0] == '\0') { fail("EXBADARG", "operand required"); return; }

    resolve_operand(arg, path);
    if (is_root(path)) { fail("EXPERM", "root node is immutable"); return; }

    int rc = cmd_remove_main(path);
    if (rc != EXFS_OK) { report(rc); return; }

    if (streq(path, current_cwd)) parent_of(path, current_cwd);   /* never leave cwd dangling */
}

static void cmd_write(const char *arg) {
    char path_tok[MAX_PATH_LEN];
    char path[MAX_PATH_LEN];

    const char *text = split_first(arg, path_tok, MAX_PATH_LEN);
    if (path_tok[0] == '\0' || text[0] == '\0') {
        fail("EXBADARG", "usage: write <path> <text>");
        return;
    }

    resolve_operand(path_tok, path);
    int rc = exfs_write(path, text, (uint64_t)str_len(text));
    if (rc != EXFS_OK) report(rc);
}

static void cmd_read(const char *arg) {
    char path[MAX_PATH_LEN];
    static char buf[READ_MAX + 1];   /* static: a large stack buffer would need __chkstk */

    if (arg[0] == '\0') { fail("EXBADARG", "operand required"); return; }

    resolve_operand(arg, path);
    int64_t size = exfs_size(path);
    if (size < 0) { report((int)size); return; }

    int64_t n = exfs_read(path, buf, READ_MAX);
    if (n < 0) { report((int)n); return; }
    buf[n] = '\0';
    xk_println(buf);

    if (size > n) {
        xk_print("EXTRUNC: showing the first ");
        put_dec((unsigned long long)n);
        xk_println(" bytes");
    }
}

static void cmd_stat(const char *arg) {
    char path[MAX_PATH_LEN];
    if (arg[0] == '\0') { fail("EXBADARG", "usage: stat <path>"); return; }
    resolve_operand(arg, path);
    int rc = cmd_stat_main(path);
    if (rc < 0) report(rc);
}

static void cmd_hexdump(const char *arg) {
    char path[MAX_PATH_LEN];
    if (arg[0] == '\0') { fail("EXBADARG", "usage: hexdump <path>"); return; }
    resolve_operand(arg, path);
    int rc = cmd_hexdump_main(path);
    if (rc < 0) report(rc);
}

/* ---- dispatcher ------------------------------------------------------- */

/* Runs one directive. Returns 1 if the shell must halt, 0 otherwise. */
static int dispatch(const char *command, const char *argument) {
    if      (streq(command, "halt"))      { xk_println("Halting XASMOS."); return 1; }
    else if (streq(command, "ver"))       cmd_ver();
    else if (streq(command, "clear"))     xk_clear();
    else if (streq(command, "whereami"))  { xk_print("Current SuperDir: "); xk_println(current_cwd); }
    else if (streq(command, "whoami"))    cmd_whoami();
    else if (streq(command, "sprusr"))    return cmd_sprusr(argument);
    else if (streq(command, "xent"))      cmd_xent();
    else if (streq(command, "locate") ||
             streq(command, "ubicate"))   cmd_locate(argument);   /* legacy alias */
    else if (streq(command, "list"))      cmd_list(argument);
    else if (streq(command, "form"))      cmd_form(argument);
    else if (streq(command, "remove"))    cmd_remove(argument);
    else if (streq(command, "read"))      cmd_read(argument);
    else if (streq(command, "write"))     cmd_write(argument);
    else if (streq(command, "stat"))      cmd_stat(argument);
    else if (streq(command, "hexdump"))   cmd_hexdump(argument);
    else if (streq(command, "echo"))      cmd_echo_main(argument);
    else if (streq(command, "mem"))       cmd_mem_main();
    else if (streq(command, "tokens"))    cmd_tokens_main();
    else if (streq(command, "run"))       cmd_run_main(argument);
    else if (streq(command, "mount"))     cmd_mount_main(argument);
    else if (streq(command, "unmount"))   cmd_unmount_main(argument);
    else if (streq(command, "format"))    cmd_format_main(argument);
    else if (streq(command, "sync"))      cmd_sync_main(argument);
    else if (streq(command, "install"))   cmd_install_main(argument);
    else if (streq(command, "pci"))       cmd_pci_main();
    else if (streq(command, "nvme"))      cmd_nvme_main(argument);
    else if (streq(command, "sysinfo") || streq(command, "exofetch"))
        fail("EXNOSYS", "directive unimplemented");
    else {
        xk_print("EXBADCMD: unrecognized directive: ");
        xk_println(command);
    }
    return 0;
}

/* ---- main loop -------------------------------------------------------- */

void shell_main(void) {
    char input_line[MAX_INPUT_LEN];
    char command[MAX_CMD_LEN];
    char argument[MAX_ARG_LEN];

    xk_println("XASMOS Shell v0.05 -- 'ver' lists directives");

    while (1) {
        xk_print(current_user);
        xk_print(":");
        xk_print(current_cwd);
        xk_print(is_superuser ? "% " : "@ ");

        int len = xk_readline(input_line, MAX_INPUT_LEN);
        if (len <= 0) continue;

        parse_command_line(input_line, command, argument);
        if (command[0] == '\0') continue;

        if (dispatch(command, argument)) break;
    }

    xk_println("System halted.");
    while (1) {
        __asm__ volatile ("cli; hlt");
    }
}
