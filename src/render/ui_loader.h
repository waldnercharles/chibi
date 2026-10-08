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
} render_ui_loader;
