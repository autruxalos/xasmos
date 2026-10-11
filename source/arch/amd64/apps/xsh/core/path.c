/******************************************************************************
 * XASMOS - SuperDirs Path Resolution Engine (Strict '|' Delimiter)
 * Path: source|arch|amd64|apps|xsh|core|path.c
 ******************************************************************************/

#define MAX_PATH_LENGTH 256

/**
 * Resolves a target path against the current working directory (CWD)
 * using '|' as the sole separator. No UNIX '/' translations or assumptions.
 */
void resolve_superdir_path(const char *cwd, const char *target, char *out_path) {
    if (!target || target[0] == '\0') {
        // Fallback to CWD if target is empty
        int i = 0;
        while (cwd[i] != '\0' && i < MAX_PATH_LENGTH - 1) {
            out_path[i] = cwd[i];
            i++;
        }
        out_path[i] = '\0';
        return;
    }

    if (target[0] == '|') {
        // Absolute path from the root '|'
        int i = 0;
        while (target[i] != '\0' && i < MAX_PATH_LENGTH - 1) {
            out_path[i] = target[i];
            i++;
        }
        out_path[i] = '\0';
    } else {
        // Relative path: append target to CWD cleanly
        int i = 0;
        while (cwd[i] != '\0' && i < MAX_PATH_LENGTH - 1) {
            out_path[i] = cwd[i];
            i++;
        }
        
        // Ensure a trailing delimiter if CWD doesn't end with '|'
        if (i > 0 && out_path[i - 1] != '|' && i < MAX_PATH_LENGTH - 1) {
            out_path[i++] = '|';
        }

        int j = 0;
        while (target[j] != '\0' && i < MAX_PATH_LENGTH - 1) {
            out_path[i++] = target[j++];
        }
        out_path[i] = '\0';
    }
}
