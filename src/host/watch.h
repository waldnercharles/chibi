#ifndef CHIBI_HOST_WATCH_H
#define CHIBI_HOST_WATCH_H

#include <stdint.h>

typedef struct chibi_watch chibi_watch;

/* Paths must exist and may name files or recursive directory trees. Returns
 * NULL with errno on OS failure. Revisions coalesce events, not file contents. */
chibi_watch *chibi_watch_create(
    const char *const *paths,
    int count,
    const char *const *suffixes,
    int suffix_count
);
uint64_t chibi_watch_revision(chibi_watch *watch);
/* Accepts NULL for partially constructed host cleanup. */
void chibi_watch_free(chibi_watch *watch);

#endif
