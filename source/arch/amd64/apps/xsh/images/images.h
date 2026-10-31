/******************************************************************************
 * XASMOS - embedded program images for xsh
 * Path: source/arch/amd64/apps/xsh/images/images.h
 ******************************************************************************/
#ifndef XSH_IMAGES_H
#define XSH_IMAGES_H

typedef struct {
    const char          *name;
    const unsigned char *data;
    unsigned long        len;
} XshImage;

/* Returns the image with this name, or 0. */
const XshImage *xsh_image_find(const char *name);
int             xsh_image_count(void);
const XshImage *xsh_image_at(int i);

#endif /* XSH_IMAGES_H */
