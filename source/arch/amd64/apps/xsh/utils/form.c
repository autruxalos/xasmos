/******************************************************************************
 * XASMOS - Coreutil: form (File/Directory Generator)
 * Path: source|arch|amd64|apps|xsh|utils|form.c
 ******************************************************************************/

#define TYPE_FILE 0
#define TYPE_DIR  1

extern int exfs_create_object(const char *raw_path, int type);

// Basic string comparison utility for arguments
static int string_equals(const char *s1, const char *s2) {
    int i = 0;
    while (s1[i] != '\0' && s2[i] != '\0') {
        if (s1[i] != s2[i]) return 0;
        i++;
    }
    return s1[i] == s2[i];
}

int cmd_form_main(const char *flag, const char *path) {
    if (!flag || !path) return -1;

    int obj_type = -1;

    if (string_equals(flag, "-file")) {
        obj_type = TYPE_FILE;
    } else if (string_equals(flag, "-dir")) {
        obj_type = TYPE_DIR;
    } else {
        return -1; // Invalid flag syntax
    }

    // Creates either a standard data node or an executable binary node (.exb)
    return exfs_create_object(path, obj_type);
}
