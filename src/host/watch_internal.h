#ifndef CHIBI_HOST_WATCH_INTERNAL_H
#define CHIBI_HOST_WATCH_INTERNAL_H

#include "host/watch.h"

#include <stdatomic.h>
#include <stdbool.h>

typedef struct
{
    char *path;
    bool directory;
} chibi_watch_root;

struct chibi_watch
{
    chibi_watch_root *roots;
    int count;
    char **suffixes;
    int suffix_count;
    atomic_uint_fast64_t revision;
    void *backend;
};

bool chibi_watch_start(chibi_watch *watch);
void chibi_watch_stop(chibi_watch *watch);
bool chibi_watch_excluded(const char *path);
bool chibi_watch_matches(chibi_watch *watch, const char *path, bool directory);

#endif
