/******************************************************************************
 * XASMOS - Core Modular Shell Loop (XSH)
 * Path: source|arch|amd64|apps|xsh|core|shell.c
 ******************************************************************************/

#define MAX_INPUT_LEN 512
#define MAX_CMD_LEN   32
#define MAX_ARG_LEN   480
#define MAX_PATH_LEN  256

#define TYPE_FILE 0
#define TYPE_DIR  1

/* Kernel services */
extern void xk_print(const char *str);
extern int  xk_println(const char *str);
extern int  xk_readline(char *buffer, int max_chars);
extern int  xk_strcmp(const char *s1, const char *s2);
extern void xk_clear(void);

/* Utilities and storage */
extern int  cmd_remove_main(const char *path);
extern int  cmd_form_main(const char *flag, const char *path);
extern void resolve_superdir_path(const char *cwd, const char *target, char *out_path);
#include "exfs.h"   /* EXFS_DATA_MAX, EXFS_ERR_*, prototypes: one definition */

static char current_cwd[MAX_PATH_LEN] = "|";
static int  is_superuser = 0;                  /* 0 = '@', 1 = '%' */

/* ---- helpers ---------------------------------------------------------- */

static void fail(const char *code, const char *detail) {
    xk_print(code);
    xk_print(": ");
    xk_println(detail);
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

/* ---- directives ------------------------------------------------------- */

static int child_counter;
static void count_child(const char *name, int type) {
    (void)name; (void)type;
    child_counter++;
}

static void print_child(const char *name, int type) {
    xk_print("  ");
    xk_print(name);
    if (type == TYPE_DIR) xk_print("|");
    xk_print("\n");
    child_counter++;
}

static void cmd_ver(void) {
    xk_println("XASMOS xeret 0.1 (amd64, UEFI)");
    xk_println("directives: ver clear list form remove locate whereami halt");
}

static void cmd_locate(const char *arg) {
    char path[MAX_PATH_LEN];
    if (arg[0] == '\0') { fail("EXBADARG", "operand required"); return; }

    resolve_operand(arg, path);
    int st = exfs_stat(path);
    if (st < 0)         { fail("EXNOENT", "path unresolved"); return; }
    if (st != TYPE_DIR) { fail("EXNOTDIR", "target is not a directory"); return; }
    copy_str(current_cwd, path, MAX_PATH_LEN);
}

static void cmd_list(const char *arg) {
    char path[MAX_PATH_LEN];
    if (arg[0] == '\0') copy_str(path, current_cwd, MAX_PATH_LEN);
    else                resolve_operand(arg, path);

    int st = exfs_stat(path);
    if (st < 0) { fail("EXNOENT", "path unresolved"); return; }
    if (st == TYPE_FILE) { xk_println(path); return; }

    child_counter = 0;
    exfs_enum(path, print_child);
    if (child_counter == 0) xk_println("  (empty)");
}

static void cmd_form(const char *arg) {
    char flag[MAX_CMD_LEN];
    char path[MAX_PATH_LEN];
    char parent[MAX_PATH_LEN];

    const char *rest = split_first(arg, flag, MAX_CMD_LEN);
    if ((!streq(flag, "-file") && !streq(flag, "-dir")) || rest[0] == '\0') {
        fail("EXBADARG", "usage: form -file|-dir <path>");
        return;
    }

    resolve_operand(rest, path);
    if (is_root(path) || exfs_stat(path) >= 0) {
        fail("EXEXIST", "target already bound");
        return;
    }
    parent_of(path, parent);
    if (exfs_stat(parent) != TYPE_DIR) {
        fail("EXNOENT", "parent unresolved");
        return;
    }
    if (cmd_form_main(flag, path) < 0) {
        fail("EXFULL", "node table exhausted");
    }
}

static void cmd_remove(const char *arg) {
    char path[MAX_PATH_LEN];
    if (arg[0] == '\0') { fail("EXBADARG", "operand required"); return; }

    resolve_operand(arg, path);
    if (is_root(path))  { fail("EXPERM", "root node is immutable"); return; }

    int st = exfs_stat(path);
    if (st < 0) { fail("EXNOENT", "path unresolved"); return; }
    if (st == TYPE_DIR) {
        child_counter = 0;
        exfs_enum(path, count_child);
        if (child_counter > 0) { fail("EXNOTEMPTY", "directory holds live nodes"); return; }
    }
    if (cmd_remove_main(path) < 0) { fail("EXIOERR", "unbind failed"); return; }

    if (streq(path, current_cwd)) parent_of(path, current_cwd);   /* never leave cwd dangling */
}

static void cmd_write(const char *arg) {
    char path_tok[MAX_CMD_LEN];
    char path[MAX_PATH_LEN];

    const char *text = split_first(arg, path_tok, MAX_CMD_LEN);
    if (path_tok[0] == '\0' || text[0] == '\0') {
        fail("EXBADARG", "usage: write <path> <text>");
        return;
    }

    resolve_operand(path_tok, path);
    int st = exfs_stat(path);
    if (st < 0)          { fail("EXNOENT", "path unresolved"); return; }
    if (st == TYPE_DIR)  { fail("EXISDIR", "target is a directory"); return; }

    int rc = exfs_write_data(path, text);
    if      (rc == EXFS_ERR_TRUNC)  fail("EXNOSPC", "text truncated to node capacity");
    else if (rc == EXFS_ERR_NOMEM)  fail("EXNOMEM", "no frame available for node data");
    else if (rc == EXFS_ERR_DENIED) fail("EXPERM",  "token refused the write");
    else if (rc < 0)                fail("EXIOERR", "write failed");
    /* rc == 0: silent success, matches the rest of the directive set */
}

static void cmd_read(const char *arg) {
    char path[MAX_PATH_LEN];
    static char buf[EXFS_DATA_MAX];   /* static: 4 KiB on the stack would need __chkstk */

    if (arg[0] == '\0') { fail("EXBADARG", "operand required"); return; }

    resolve_operand(arg, path);
    int st = exfs_stat(path);
    if (st < 0)          { fail("EXNOENT", "path unresolved"); return; }
    if (st == TYPE_DIR)  { fail("EXISDIR", "target is a directory"); return; }

    int n = exfs_read_data(path, buf, sizeof(buf));
    if (n == EXFS_ERR_DENIED) { fail("EXPERM", "token refused the read"); return; }
    if (n < 0)                { fail("EXIOERR", "read failed"); return; }
    xk_println(buf);
}

/* ---- main loop -------------------------------------------------------- */

void shell_main(void) {
    char input_line[MAX_INPUT_LEN];
    char command[MAX_CMD_LEN];
    char argument[MAX_ARG_LEN];

    xk_println("XASMOS Shell v0.3 -- 'ver' lists directives");

    while (1) {
        xk_print(current_cwd);
        xk_print(is_superuser ? "% " : "@ ");

        int len = xk_readline(input_line, MAX_INPUT_LEN);
        if (len <= 0) continue;

        parse_command_line(input_line, command, argument);
        if (command[0] == '\0') continue;

        if      (streq(command, "halt"))      { xk_println("Halting XASMOS."); break; }
        else if (streq(command, "ver"))       cmd_ver();
        else if (streq(command, "clear"))     xk_clear();
        else if (streq(command, "whereami"))  { xk_print("Current SuperDir: "); xk_println(current_cwd); }
        else if (streq(command, "locate") ||
                 streq(command, "ubicate"))   cmd_locate(argument);   /* legacy alias */
        else if (streq(command, "list"))      cmd_list(argument);
        else if (streq(command, "form"))      cmd_form(argument);
        else if (streq(command, "remove"))    cmd_remove(argument);
        else if (streq(command, "read"))      cmd_read(argument);
        else if (streq(command, "write"))     cmd_write(argument);
        else if (streq(command, "sysinfo") || streq(command, "sprusr"))
            fail("EXNOSYS", "directive unimplemented");
        else {
            xk_print("EXBADCMD: unrecognized directive: ");
            xk_println(command);
        }
    }

    xk_println("System halted.");
    while (1) {
        __asm__ volatile ("cli; hlt");
    }
}
