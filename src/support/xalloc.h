#ifndef CHIBI_XALLOC_H
#define CHIBI_XALLOC_H

#include <stddef.h>
#include <stdint.h>

/* Allocate or abort. These never return NULL; on failure they print a message
 * and abort so the crash happens at the allocation site. */
void *xmalloc(size_t n);
void *xcalloc(size_t count, size_t size);
void *xrealloc(void *p, size_t n);
char *xstrdup(const char *s);

typedef struct
{
    uint64_t calls;
    uint64_t requested_bytes;
} xalloc_stats;

/* Reads process-wide calls made through the xalloc wrappers. Bytes are the
 * requested sizes, not live allocation size. */
void xalloc_stats_get(xalloc_stats *stats);

#endif
