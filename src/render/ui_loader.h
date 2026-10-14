#pragma once
#include "ui/document.h"

#include <stdbool.h>
#include <stddef.h>
/* Optional authored loader. Hooks are copied; their code outlives the mount or
 * page. open returns owned state, close releases it. read returns 1 with an
 * owned expanded document, 0 unchanged, or -1 with an error. Failure leaves out
 * empty; the renderer keeps its last valid document. No provider means prepared
 * resident records only. */
typedef struct
{
    void *(*open)(const char *content_root, const char *name, bool page);
    int (*read)(void *, double time, ui_document *out, char *error, size_t size);
    void (*close)(void *);
    /* Explicit isolated provider. Context is borrowed until all loaded pages
     * and mounts close. This entry takes precedence over resident documents;
     * only one open entry may be supplied. read/close own the returned state. */
    void *context;
    void *(*open_context)(void *, const char *content_root, const char *name, bool page);
    bool resident_images; /* Explicit provider still resolves only prepared images. */
} render_ui_loader;
