#ifndef CHIBI_XALLOC_H
#define CHIBI_XALLOC_H

#include <stddef.h>

/* Allocate or abort. These never return NULL; on failure they print a message
 * and abort so the crash happens at the allocation site (see stacktrace.h). */
void *xmalloc(size_t n);
void *xcalloc(size_t count, size_t size);
void *xrealloc(void *p, size_t n);
char *xstrdup(const char *s);

#endif
