#ifndef BLUEPRINTS_HOST_WATCH_INTERNAL_H
#define BLUEPRINTS_HOST_WATCH_INTERNAL_H

#include "host/watch.h"

#include <stdatomic.h>
#include <stdbool.h>

typedef struct
{
    char *path;
    bool directory;
} bp_watch_root;

struct bp_watch
{
    bp_watch_root *roots;
    int count;
    char **suffixes;
    int suffix_count;
    atomic_uint_fast64_t revision;
    void *backend;
};

bool bp_watch_start(bp_watch *watch);
void bp_watch_stop(bp_watch *watch);
bool bp_watch_excluded(const char *path);
bool bp_watch_matches(bp_watch *watch, const char *path, bool directory);

#endif
