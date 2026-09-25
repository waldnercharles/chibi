#ifndef CHIBI_FILE_WATCH_H
#define CHIBI_FILE_WATCH_H

#include <stdbool.h>

/* Polls one file by path, modification time and size, at most once a second
 * per path; a different path is checked at once. Zero-initialize; nothing to
 * free. */
typedef struct file_watch
{
    char path[1024];
    long long modified, size; /* size is -1 while the file is missing */
    double due;               /* next check, in the caller's seconds */
} file_watch;

/* True when `path` names a different file than the last check or the file
 * appeared, disappeared or changed. `now` is in seconds. */
bool file_watch_changed(file_watch *w, const char *path, double now);

/* Whether the file existed at the last check. */
bool file_watch_exists(const file_watch *w);

#endif
