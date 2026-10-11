/******************************************************************************
 * XASMOS - Core Modular Shell Loop (XSH)
 * Path: source|arch|amd64|apps|xsh|core|shell.c
 ******************************************************************************/

#define MAX_INPUT_LEN 128
#define MAX_CMD_LEN   32
#define MAX_ARG_LEN   96

// System kernel and utility declarations
extern void xk_print(const char *str);
extern int  xk_println(const char *str);
extern int  xk_readline(char *buffer, int max_chars);
extern int  xk_strcmp(const char *s1, const char *s2);

// Utility bindings
extern int cmd_remove_main(const char *path);
extern int cmd_form_main(const char *flag, const char *path);
extern void resolve_superdir_path(const char *cwd, const char *target, char *out_path);

// Dynamic shell state (starts at system root or designated workspace)
static char current_cwd[256] = "|"; 
static int  is_superuser     = 0;   // 0 = '@' (Normal), 1 = '%' (Superuser)

static void parse_command_line(const char *line, char *cmd, char *arg) {
    int i = 0, j = 0;
    
    // Extract command token
    while (line[i] != ' ' && line[i] != '\0' && i < MAX_CMD_LEN - 1) {
        cmd[i] = line[i];
        i++;
    }
    cmd[i] = '\0';

    // Skip spaces between command and argument
    while (line[i] == ' ') {
        i++;
    }

    // Extract argument string (preserving absolute or relative | paths)
    while (line[i] != '\0' && j < MAX_ARG_LEN - 1) {
        arg[j] = line[i];
        i++;
        j++;
    }
    arg[j] = '\0';
}

void shell_main(void) {
    char input_line[MAX_INPUT_LEN];
    char command[MAX_CMD_LEN];
    char argument[MAX_ARG_LEN];
    char resolved_path[256];

    xk_println("XASMOS Shell v0.2 (Modular Coreutils Online)");

    while (1) {
        // Print dynamic prompt: [current_cwd][@ or %] 
        xk_print(current_cwd);
        xk_print(is_superuser ? "% " : "@ ");

        // Read input securely from hardware buffer
        int len = xk_readline(input_line, MAX_INPUT_LEN);
        if (len <= 0) continue;

        parse_command_line(input_line, command, argument);

        if (command[0] == '\0') continue;

        // --- COMMAND DISPATCHER ---
        
        if (xk_strcmp(command, "halt") == 0) {
            xk_println("Shutting down XASMOS system...");
            break;
        } 
        else if (xk_strcmp(command, "whereami") == 0) {
            // Equivalent to pwd
            xk_print("Current SuperDir: ");
            xk_println(current_cwd);
        } 
        else if (xk_strcmp(command, "ubicate") == 0) {
            // Equivalent to cd using SuperDirs
            if (argument[0] != '\0') {
                resolve_superdir_path(current_cwd, argument, resolved_path);
                // Copy resolved path back to CWD state
                int k = 0;
                while (resolved_path[k] != '\0') {
                    current_cwd[k] = resolved_path[k];
                    k++;
                }
                current_cwd[k] = '\0';
            } else {
                xk_println("Usage: ubicate <|path>");
            }
        } 
        else if (xk_strcmp(command, "remove") == 0) {
            // Flagless removal: remove |user|auleoxs|file.exb
            if (argument[0] != '\0') {
                resolve_superdir_path(current_cwd, argument, resolved_path);
                cmd_remove_main(resolved_path);
            } else {
                xk_println("Syntax: remove <|path>");
            }
        } 
        else if (xk_strcmp(command, "form") == 0) {
            // Generator: form -file |path|file.exb OR form -dir |path|folder
            // (Expects format: "-file |path" or "-dir |path")
            // A simple sub-parser can handle flags here
            xk_println("Executing form utility...");
        } 
        else {
            xk_print("Command not recognized: ");
            xk_println(command);
        }
    }

    // Halt system loop
    while (1) {
        __asm__ volatile ("hlt");
    }
}
