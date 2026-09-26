#include "support/file_watch.h"

#include "support/text_overlay.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

bool file_watch_changed(file_watch *w, const char *path, double now)
{
    bool same = strcmp(path, w->path) == 0;
    unsigned long long overlay = text_overlay_revision(path);
    bool edited = overlay != w->overlay;
    w->overlay = overlay;
    if (same && now < w->due && !edited) {
        return false;
    }
    w->due = now + 1;
    struct stat info;
    bool exists = stat(path, &info) == 0;
    long long modified = exists ? (long long)info.st_mtime : 0;
    long long size = exists ? (long long)info.st_size : -1;
    if (same && modified == w->modified && size == w->size && !edited) {
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
