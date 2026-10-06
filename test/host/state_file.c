#include "host/state_file.h"

#include <assert.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static void assert_contents(const char *path, const char *expected)
{
    char *actual = chibi_read_file(path);
    assert(actual && !strcmp(actual, expected));
    free(actual);
}

void test_host_state_file()
{
    char directory[] = "/tmp/chibi-state-file-XXXXXX";
    assert(mkdtemp(directory));
    char path[512], blocked[512];
    snprintf(path, sizeof path, "%s/recovery.kdl", directory);
    snprintf(blocked, sizeof blocked, "%s/blocked", directory);
    assert(!chibi_read_file(path));
    assert(chibi_write_file(path, ""));
    assert_contents(path, "");

    const char *checkpoint = "scene \"Recovery\" {\n    dirty true\n}\n";
    assert(chibi_write_file(path, checkpoint));
    FILE *previous = fopen(path, "rb");
    assert(previous);
    assert(chibi_write_file(path, "new\n"));
    assert_contents(path, "new\n");
    char retained[128] = { 0 };
    assert(fread(retained, 1, sizeof retained - 1, previous) == strlen(checkpoint));
    assert(!strcmp(retained, checkpoint));
    assert(fclose(previous) == 0);

    /* Recovery reads the complete snapshot left by an exited application. */
    pid_t writer = fork();
    assert(writer >= 0);
    if (!writer) {
        _exit(chibi_write_file(path, checkpoint) ? 0 : 1);
    }
    int status;
    assert(waitpid(writer, &status, 0) == writer);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    assert_contents(path, checkpoint);

    /* A failed publish removes its temporary file and preserves its target. */
    assert(mkdir(blocked, 0700) == 0);
    assert(!chibi_write_file(blocked, checkpoint));
    struct stat info;
    assert(stat(blocked, &info) == 0 && S_ISDIR(info.st_mode));
    DIR *entries = opendir(directory);
    assert(entries);
    int count = 0;
    struct dirent *entry;
    while ((entry = readdir(entries))) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) {
            continue;
        }
        assert(!strcmp(entry->d_name, "recovery.kdl") || !strcmp(entry->d_name, "blocked"));
        count++;
    }
    assert(closedir(entries) == 0 && count == 2);
    assert_contents(path, checkpoint);
    assert(unlink(path) == 0 && rmdir(blocked) == 0 && rmdir(directory) == 0);
    puts("PASS: recovery snapshots replace atomically, survive process exit and clean up failed writes");
}
