#include "support/file_watch.h"

#include "test/suite_internal.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static void write_file(const char *path, const char *text)
{
    FILE *file = fopen(path, "wb");
    assert(file);
    fputs(text, file);
    fclose(file);
}

void test_file_watch()
{
    char root[] = "/tmp/chibi-file-watch-XXXXXX";
    assert(mkdtemp(root));
    char path[256], other[256];
    snprintf(path, sizeof path, "%s/watched.yaml", root);
    snprintf(other, sizeof other, "%s/other.yaml", root);
    write_file(path, "a: 1\n");

    file_watch w = { 0 };
    test_check(file_watch_changed(&w, path, 0) && file_watch_exists(&w), "file watch reports a new path");
    write_file(path, "a: 12\n");
    test_check(!file_watch_changed(&w, path, 0.5), "file watch checks at most once a second");
    test_check(file_watch_changed(&w, path, 1), "file watch reports a changed file");
    test_check(!file_watch_changed(&w, path, 2), "file watch ignores an unchanged file");
    unlink(path);
    test_check(file_watch_changed(&w, path, 3) && !file_watch_exists(&w), "file watch reports a removed file");
    test_check(file_watch_changed(&w, other, 3) && !file_watch_exists(&w), "file watch reports a different path");
    rmdir(root);
}
