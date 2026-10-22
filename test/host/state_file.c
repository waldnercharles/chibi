#include "host/state_file.h"

#include "third_party/pico_headers/pico_unit.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static bool contents_equal(const char *path, const char *expected)
{
    char *actual = chibi_read_file(path);
    bool equal = actual && !strcmp(actual, expected);
    free(actual);
    return equal;
}

TEST_CASE(state_file_atomic_replacement_and_recovery)
{
    char directory[] = "/tmp/chibi-state-file-XXXXXX";
    REQUIRE(mkdtemp(directory));
    char path[512], blocked[512];
    snprintf(path, sizeof path, "%s/recovery.chibi", directory);
    snprintf(blocked, sizeof blocked, "%s/blocked", directory);
    REQUIRE(!chibi_read_file(path));
    REQUIRE(chibi_write_file(path, ""));
    REQUIRE(contents_equal(path, ""));

    const char *checkpoint = "scene \"Recovery\" {\n    dirty true\n}\n";
    REQUIRE(chibi_write_file(path, checkpoint));
    FILE *previous = fopen(path, "rb");
    REQUIRE(previous);
    REQUIRE(chibi_write_file(path, "new\n"));
    REQUIRE(contents_equal(path, "new\n"));
    char retained[128] = { 0 };
    REQUIRE(fread(retained, 1, sizeof retained - 1, previous) == strlen(checkpoint));
    REQUIRE(!strcmp(retained, checkpoint));
    REQUIRE(fclose(previous) == 0);

    /* Recovery reads the complete snapshot left by an exited application. */
    pid_t writer = fork();
    REQUIRE(writer >= 0);
    if (!writer) {
        _exit(chibi_write_file(path, checkpoint) ? 0 : 1);
    }
    int status;
    REQUIRE(waitpid(writer, &status, 0) == writer);
    REQUIRE(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    REQUIRE(contents_equal(path, checkpoint));

    /* A failed publish removes its temporary file and preserves its target. */
    REQUIRE(mkdir(blocked, 0700) == 0);
    REQUIRE(!chibi_write_file(blocked, checkpoint));
    struct stat info;
    REQUIRE(stat(blocked, &info) == 0 && S_ISDIR(info.st_mode));
    DIR *entries = opendir(directory);
    REQUIRE(entries);
    int count = 0;
    struct dirent *entry;
    while ((entry = readdir(entries))) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) {
            continue;
        }
        REQUIRE(!strcmp(entry->d_name, "recovery.chibi") || !strcmp(entry->d_name, "blocked"));
        count++;
    }
    REQUIRE(closedir(entries) == 0 && count == 2);
    REQUIRE(contents_equal(path, checkpoint));
    REQUIRE(unlink(path) == 0 && rmdir(blocked) == 0 && rmdir(directory) == 0);
    return true;
}

void test_host_state_file()
{
    RUN_TEST_CASE(state_file_atomic_replacement_and_recovery);
}
