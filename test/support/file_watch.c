#include "support/file_watch.h"

#include "test/suite_internal.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>

static void write_file(const char *path, const char *text)
{
    FILE *file = fopen(path, "wb");
    assert(file);
    fputs(text, file);
    fclose(file);
}

static void time_file(const char *path, long seconds, long microseconds)
{
    struct timeval stamp[] = { { .tv_sec = seconds, .tv_usec = microseconds },
                               { .tv_sec = seconds, .tv_usec = microseconds } };
    assert(utimes(path, stamp) == 0);
}

void test_file_watch()
{
    char root[] = "/tmp/chibi-file-watch-XXXXXX";
    assert(mkdtemp(root));
    char path[256], other[256];
    snprintf(path, sizeof path, "%s/watched.kdl", root);
    snprintf(other, sizeof other, "%s/other.kdl", root);
    write_file(path, "a: 1\n");

    time_file(path, 10, 1000);

    file_watch w = { 0 };
    test_check(file_watch_changed(&w, path, 0) && file_watch_exists(&w), "file watch reports a new path");
    write_file(path, "b: 2\n");
    time_file(path, 10, 2000);
    test_check(!file_watch_changed(&w, path, 0.5), "file watch checks at most once a second");
    test_check(file_watch_changed(&w, path, 1), "file watch sees same-size edits within one second");

    char replacement[256];
    snprintf(replacement, sizeof replacement, "%s/replacement.kdl", root);
    write_file(replacement, "c: 3\n");
    time_file(replacement, 10, 2000);
    assert(rename(replacement, path) == 0);
    test_check(file_watch_changed(&w, path, 2), "file watch sees atomic replacements with unchanged size and mtime");
    test_check(!file_watch_changed(&w, path, 2), "file watch ignores an unchanged file");
    unlink(path);
    test_check(file_watch_changed(&w, path, 3) && !file_watch_exists(&w), "file watch reports a removed file");
    test_check(file_watch_changed(&w, other, 3) && !file_watch_exists(&w), "file watch reports a different path");
    rmdir(root);
}
