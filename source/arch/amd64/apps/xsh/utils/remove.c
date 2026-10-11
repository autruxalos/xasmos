/******************************************************************************
 * XASMOS - Coreutil: remove
 * Path: source|arch|amd64|apps|xsh|utils|remove.c
 ******************************************************************************/

#define MAX_PATH_LENGTH 256

// External reference to the exokernel storage/object table lookup
extern int exfs_delete_object(const char *raw_path);

int cmd_remove_main(const char *args) {
    if (!args || args[0] == '\0') {
        // Simple write to video or stdout buffer
        return -1;
    }

    // Pass the absolute | delimited path directly to the storage subsystem
    int status = exfs_delete_object(args);
    return status;
}
