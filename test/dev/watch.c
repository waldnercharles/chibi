#include "host/watch.h"

#include "test/support/report.h"
#ifdef __APPLE__
#include "host/watch_macos_flags.h"
#endif

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static void pause_events()
{
    struct timespec delay = { .tv_nsec = 10000000 };
    nanosleep(&delay, NULL);
}

static void write_file(const char *path)
{
    FILE *file = fopen(path, "w");
    assert(file);
    assert(fputs("changed\n", file) >= 0);
    assert(fclose(file) == 0);
}

static void await_change(chibi_watch *watch, uint64_t before)
{
    for (int i = 0; i < 300 && chibi_watch_revision(watch) == before; i++) {
        pause_events();
    }
    assert(chibi_watch_revision(watch) > before);
}

static uint64_t settled(chibi_watch *watch)
{
    uint64_t revision = chibi_watch_revision(watch);
    int quiet = 0;
    for (int i = 0; i < 300 && quiet < 30; i++) {
        pause_events();
        uint64_t next = chibi_watch_revision(watch);
        quiet = next == revision ? quiet + 1 : 0;
        revision = next;
    }
    assert(quiet == 30);
    return revision;
}

void test_dev_watch()
{
#ifdef __APPLE__
    unsigned metadata = kFSEventStreamEventFlagItemChangeOwner |
                        kFSEventStreamEventFlagItemInodeMetaMod |
                        kFSEventStreamEventFlagItemXattrMod;
    assert(!chibi_watch_macos_content_event(metadata | kFSEventStreamEventFlagItemIsFile));
    assert(!chibi_watch_macos_content_event(
        metadata | kFSEventStreamEventFlagItemIsDir | kFSEventStreamEventFlagItemCreated
    ));
    assert(chibi_watch_macos_content_event(metadata | kFSEventStreamEventFlagItemModified));
    assert(chibi_watch_macos_content_event(kFSEventStreamEventFlagItemCreated));
    assert(chibi_watch_macos_content_event(
        kFSEventStreamEventFlagItemRemoved | kFSEventStreamEventFlagItemIsDir
    ));
    assert(chibi_watch_macos_content_event(kFSEventStreamEventFlagItemRenamed));
#endif
    char root[] = "/tmp/chibi-watch-XXXXXX";
    assert(mkdtemp(root));
    char source[512], nested[512], file[512], ignored[512], single[512], other[512];
    snprintf(source, sizeof source, "%s/src", root);
    snprintf(nested, sizeof nested, "%s/src/new", root);
    snprintf(file, sizeof file, "%s/src/new/code.c", root);
    snprintf(ignored, sizeof ignored, "%s/src/notes.md", root);
    snprintf(single, sizeof single, "%s/Makefile", root);
    snprintf(other, sizeof other, "%s/other.c", root);
    assert(mkdir(source, 0700) == 0);
    write_file(single);
    char build[512], dependency[512], dependency_code[512];
    snprintf(build, sizeof build, "%s/build-dev", root);
    snprintf(dependency, sizeof dependency, "%s/build-dev/dependency", root);
    snprintf(dependency_code, sizeof dependency_code, "%s/build-dev/dependency/code.c", root);
    assert(mkdir(build, 0700) == 0 && mkdir(dependency, 0700) == 0);
    const char *paths[] = { source, single, dependency };
    const char *suffixes[] = { ".c", ".h" };
    chibi_watch *watch = chibi_watch_create(paths, 3, suffixes, 2);
    assert(watch);
    uint64_t revision = settled(watch);
    /* Explicit dependency roots remain observable inside a build directory. */
    write_file(dependency_code);
    await_change(watch, revision);
    revision = settled(watch);
    assert(mkdir(nested, 0700) == 0);
    write_file(file);
    await_change(watch, revision);
    revision = settled(watch);
    write_file(file);
    await_change(watch, revision);
    revision = settled(watch);
    /* Directory permission changes do not change topology. File metadata flags
     * are checked above: FSEvents can combine chmod on a fresh fixture with old
     * Created/Modified flags, which must still invalidate source contents. */
    assert(chmod(source, 0750) == 0 && chmod(nested, 0750) == 0);
    assert(settled(watch) == revision);
    write_file(ignored);
    write_file(other);
    assert(settled(watch) == revision);
    const char *exclusions[] = { ".git",        ".chibi",     "build",
                                 "build-debug", "CMakeFiles", "__pycache__" };
    for (size_t i = 0; i < sizeof exclusions / sizeof *exclusions; i++) {
        char directory[512], excluded[512];
        snprintf(directory, sizeof directory, "%s/src/%s", root, exclusions[i]);
        snprintf(excluded, sizeof excluded, "%s/src/%s/ignored.c", root, exclusions[i]);
        assert(mkdir(directory, 0700) == 0);
        write_file(excluded);
        assert(settled(watch) == revision);
        assert(unlink(excluded) == 0);
        assert(rmdir(directory) == 0);
    }
    assert(settled(watch) == revision);
    /* Atomic-save replacement of an exact file keeps the parent registered. */
    assert(rename(other, single) == 0);
    await_change(watch, revision);
    revision = settled(watch);
    write_file(single);
    await_change(watch, revision);
    assert(unlink(file) == 0);
    assert(rmdir(nested) == 0);
    revision = settled(watch);
    assert(mkdir(nested, 0700) == 0);
    write_file(file);
    await_change(watch, revision);
    revision = settled(watch);
    write_file(file);
    await_change(watch, revision);
    char moved[512], replacement[512];
    snprintf(moved, sizeof moved, "%s/moved", root);
    snprintf(replacement, sizeof replacement, "%s/src/replacement.c", root);
    assert(rename(source, moved) == 0 && mkdir(source, 0700) == 0);
    revision = settled(watch);
    write_file(replacement);
    await_change(watch, revision);
    assert(unlink(replacement) == 0 && rmdir(source) == 0 && rename(moved, source) == 0);
    chibi_watch_free(watch);
    assert(unlink(dependency_code) == 0 && rmdir(dependency) == 0 && rmdir(build) == 0);
    assert(unlink(file) == 0);
    assert(unlink(ignored) == 0);
    assert(unlink(single) == 0);
    assert(rmdir(nested) == 0);
    assert(rmdir(source) == 0);
    assert(rmdir(root) == 0);
    test_note("PASS: native file events cover recursion, topology, exact files, atomic saves and exclusions\n");
}
