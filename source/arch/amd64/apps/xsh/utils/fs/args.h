/******************************************************************************
 * XASMOS - shell helper: splits the next blank-separated word
 * Path: source/arch/amd64/apps/xsh/utils/fs/args.h
 ******************************************************************************/
#ifndef XSH_FS_ARGS_H
#define XSH_FS_ARGS_H

/* Copies the next word of s into out (at most max-1 bytes) and returns a
 * pointer to the text after it. An empty word means nothing was left. */
static inline const char *next_word(const char *s, char *out, int max) {
    while (*s == ' ') s++;
    int n = 0;
    while (*s != ' ' && *s != '\0') {
        if (n < max - 1) out[n++] = *s;
        s++;
    }
    out[n] = '\0';
    return s;
}

#endif /* XSH_FS_ARGS_H */
