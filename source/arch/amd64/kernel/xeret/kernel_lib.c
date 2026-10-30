/* XASMOS - freestanding string helpers for xeret.
 * Text output lives in fb_console.c and keyboard input in kbd.c. */

int xk_strcmp(const char *s1, const char *s2) {
    int i = 0;
    while (s1[i] != '\0' && s2[i] != '\0') {
        if (s1[i] != s2[i]) {
            return (unsigned char)s1[i] - (unsigned char)s2[i];
        }
        i++;
    }
    return (unsigned char)s1[i] - (unsigned char)s2[i];
}

int xk_strlen(const char *s) {
    int n = 0;
    while (s[n] != '\0') n++;
    return n;
}
