#ifndef BLUEPRINTS_HOST_WATCH_H
#define BLUEPRINTS_HOST_WATCH_H

#include <stdint.h>

typedef struct bp_watch bp_watch;

/* Paths must exist and may name files or recursive directory trees. Returns
 * NULL with errno on OS failure. Revisions coalesce events, not file contents. */
bp_watch *bp_watch_create(const char *const *paths, int count, const char *const *suffixes, int suffix_count);
uint64_t bp_watch_revision(bp_watch *watch);
/* Accepts NULL for partially constructed host cleanup. */
void bp_watch_free(bp_watch *watch);

#endif
