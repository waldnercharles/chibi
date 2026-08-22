#include "xalloc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void xalloc_oom(void)
{
    fputs("out of memory\n", stderr);
    abort();
}

void *xmalloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p) {
        xalloc_oom();
    }
    return p;
}

void *xcalloc(size_t count, size_t size)
{
    void *p = calloc(count ? count : 1, size ? size : 1);
    if (!p) {
        xalloc_oom();
    }
    return p;
}

void *xrealloc(void *p, size_t n)
{
    void *q = realloc(p, n ? n : 1);
    if (!q) {
        xalloc_oom();
    }
    return q;
}

char *xstrdup(const char *s)
{
    size_t len = strlen(s);
    char *p = (char *)xmalloc(len + 1);
    memcpy(p, s, len + 1);
    return p;
}
