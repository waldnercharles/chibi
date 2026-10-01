#include "support/file_watch.h"

#include "support/text_overlay.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static long long modified_ns(const struct stat *info)
{
#ifdef _WIN32
    return (long long)info->st_mtime * 1000000000LL;
#elif defined(__APPLE__)
    return (long long)info->st_mtimespec.tv_sec * 1000000000LL +
           info->st_mtimespec.tv_nsec;
#else
    return (long long)info->st_mtim.tv_sec * 1000000000LL + info->st_mtim.tv_nsec;
#endif
}

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
    long long modified = exists ? modified_ns(&info) : 0;
    long long size = exists ? (long long)info.st_size : -1;
    unsigned long long inode = exists ? (unsigned long long)info.st_ino : 0;
    if (same && modified == w->modified && size == w->size &&
        inode == w->inode && !edited) {
        return false;
    }
    if (!same) {
        snprintf(w->path, sizeof w->path, "%s", path);
    }
    w->modified = modified;
    w->size = size;
    w->inode = inode;
    return true;
}

bool file_watch_exists(const file_watch *w)
{
    return w->size >= 0;
}
