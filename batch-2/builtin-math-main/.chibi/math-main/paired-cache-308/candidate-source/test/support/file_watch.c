#include "support/file_watch.h"

#include "third_party/pico_headers/pico_unit.h"

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

TEST_CASE(file_watch_sees_edits_replacements_and_removals)
{
    char root[] = "/tmp/chibi-file-watch-XXXXXX";
    assert(mkdtemp(root));
    char path[256], other[256];
    snprintf(path, sizeof path, "%s/watched.chibi", root);
    snprintf(other, sizeof other, "%s/other.chibi", root);
    write_file(path, "a: 1\n");

    time_file(path, 10, 1000);

    file_watch w = { 0 };
    /* File watch reports a new path. */
    REQUIRE(file_watch_changed(&w, path, 0) && file_watch_exists(&w));
    write_file(path, "b: 2\n");
    time_file(path, 10, 2000);
    /* File watch checks at most once a second. */
    REQUIRE(!file_watch_changed(&w, path, 0.5));
    /* File watch sees same-size edits within one second. */
    REQUIRE(file_watch_changed(&w, path, 1));

    char replacement[256];
    snprintf(replacement, sizeof replacement, "%s/replacement.chibi", root);
    write_file(replacement, "c: 3\n");
    time_file(replacement, 10, 2000);
    assert(rename(replacement, path) == 0);
    /* File watch sees atomic replacements with unchanged size and mtime. */
    REQUIRE(file_watch_changed(&w, path, 2));
    /* File watch ignores an unchanged file. */
    REQUIRE(!file_watch_changed(&w, path, 2));
    unlink(path);
    /* File watch reports a removed file. */
    REQUIRE(file_watch_changed(&w, path, 3) && !file_watch_exists(&w));
    /* File watch reports a different path. */
    REQUIRE(file_watch_changed(&w, other, 3) && !file_watch_exists(&w));
    rmdir(root);
    return true;
}

void test_file_watch()
{
    RUN_TEST_CASE(file_watch_sees_edits_replacements_and_removals);
}
