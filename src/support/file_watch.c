#include "support/file_watch.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

bool file_watch_changed(file_watch *w, const char *path, double now)
{
    bool same = strcmp(path, w->path) == 0;
    if (same && now < w->due) {
        return false;
    }
    w->due = now + 1;
    struct stat info;
    bool exists = stat(path, &info) == 0;
    long long modified = exists ? (long long)info.st_mtime : 0;
    long long size = exists ? (long long)info.st_size : -1;
    if (same && modified == w->modified && size == w->size) {
        return false;
    }
    if (!same) {
        snprintf(w->path, sizeof w->path, "%s", path);
    }
    w->modified = modified;
    w->size = size;
    return true;
}

bool file_watch_exists(const file_watch *w)
{
    return w->size >= 0;
}
