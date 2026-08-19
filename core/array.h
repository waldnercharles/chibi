#ifndef CBLUEPRINT_ARRAY_H
#define CBLUEPRINT_ARRAY_H

/* Dynamic arrays (stretchy buffers): apush / alen / acap / afree / asetlen...
 * Backed by dyna.h with allocations routed through the abort-on-OOM
 * allocator in xalloc.h. Include this instead of dyna.h directly. */
#include "xalloc.h" // IWYU pragma: keep (xrealloc in DYNA_REALLOC below)

#include <stdlib.h>

#define DYNA_REALLOC(ptr, sz) xrealloc(ptr, sz)
#define DYNA_FREE(ptr) free(ptr)
#include "dyna.h" // IWYU pragma: keep (re-exported macros)

#endif
