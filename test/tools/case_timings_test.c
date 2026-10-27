#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static const char *driver;
static char root[1024], log_path[1024];
static int prepared, cleaned;

static void pause_briefly()
{
    struct timespec delay = { .tv_nsec = 20000000 };
    while (nanosleep(&delay, &delay)) {
    }
}

static void prepare()
{
    prepared++;
    pause_briefly();
}

static void clean()
{
    cleaned++;
    pause_briefly();
}

TEST_CASE(timing_waits_for_child)
{
    REQUIRE(prepared == cleaned + 1);
    pid_t child = fork();
    REQUIRE(child >= 0);
    if (!child) {
        pause_briefly();
        _exit(0);
    }
    int status;
    REQUIRE(waitpid(child, &status, 0) == child);
    REQUIRE(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    return true;
}

TEST_CASE(timing_empty)
{
    return true;
}
TEST_CASE(timing_interrupted)
{
    _exit(0);
}
TEST_CASE(timing_failure)
{
    return false;
}

int tool_case_timing_probe(const char *mode)
{
    if (!strcmp(mode, "wait")) {
        pu_setup(prepare, clean);
        RUN_TEST_CASE(timing_waits_for_child);
        RUN_TEST_CASE(timing_waits_for_child);
        pu_clear_setup();
        if (prepared != 2 || cleaned != 2) {
            return 2;
        }
    } else if (!strcmp(mode, "fail")) {
        RUN_TEST_CASE(timing_failure);
    } else if (!strcmp(mode, "limit")) {
        pu_display_quiet(true);
        for (int i = 0; i < 4100; i++) {
            RUN_TEST_CASE(timing_empty);
        }
    } else if (!strcmp(mode, "interrupted")) {
        RUN_TEST_CASE(timing_interrupted);
    } else if (!strcmp(mode, "length")) {
        char name[130];
        memset(name, 'x', 128);
        name[128] = 0;
        pu_run_test(name, timing_empty);
        name[128] = 'x';
        name[129] = 0;
        pu_run_test(name, timing_empty);
    } else if (!strcmp(mode, "name")) {
        pu_run_test("not\ta\tcase", timing_empty);
    } else {
        return 2;
    }
    pu_print_stats();
    return pu_test_failed() ? 1 : 0;
}

static void path(char *out, const char *name)
{
    int n = snprintf(out, 1024, "%s/%s", root, name);
    assert(n > 0 && n < 1024);
}

static int run(const char *mode, const char *sink)
{
    const char *value = getenv("CHIBI_TEST_TIMINGS");
    char *saved = value ? strdup(value) : NULL;
    assert(sink ? !setenv("CHIBI_TEST_TIMINGS", sink, 1) : !unsetenv("CHIBI_TEST_TIMINGS"));
    const char *args[] = { driver, "--case-timing-probe", mode, NULL };
    int status = tool_run(args, ".", log_path);
    assert(saved ? !setenv("CHIBI_TEST_TIMINGS", saved, 1) : !unsetenv("CHIBI_TEST_TIMINGS"));
    free(saved);
    return status;
}

static char *trace(const char *directory, int *count)
{
    DIR *dir = opendir(directory);
    assert(dir);
    char *result = NULL;
    *count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir))) {
        if (strncmp(entry->d_name, "cases-", 6)) {
            continue;
        }
        char file[2048];
        int n = snprintf(file, sizeof file, "%s/%s", directory, entry->d_name);
        assert(n > 0 && (size_t)n < sizeof file);
        struct stat status;
        assert(!stat(file, &status));
        assert((status.st_mode & 0777) == 0600);
        assert(status.st_size < 2 * 1024 * 1024);
        free(result);
        result = tool_read(file);
        (*count)++;
    }
    closedir(dir);
    return result;
}

TEST_CASE(case_timings_preserve_output_and_measure_children_and_hooks)
{
    REQUIRE(run("wait", NULL) == 0);
    char *plain = tool_read(log_path);
    char directory[1024];
    path(directory, "wait");
    REQUIRE(!mkdir(directory, 0700));
    REQUIRE(run("wait", directory) == 0);
    char *timed = tool_read(log_path);
    REQUIRE(plain && timed && !strcmp(plain, timed));
    free(plain);
    free(timed);
    int count;
    char *text = trace(directory, &count);
    REQUIRE(count == 1 && text);
    char *first = strstr(text, "end\t1\ttiming_waits_for_child\t");
    char *second = strstr(text, "end\t2\ttiming_waits_for_child\t");
    long long wall;
    REQUIRE(first && second);
    REQUIRE(sscanf(first, "end\t1\ttiming_waits_for_child\t%lld", &wall) == 1 && wall >= 60000000);
    REQUIRE(sscanf(second, "end\t2\ttiming_waits_for_child\t%lld", &wall) == 1 && wall >= 60000000);
    REQUIRE(strstr(text, "suite/startup residual unmeasured"));
    free(text);
    REQUIRE(run("wait", directory) == 0);
    text = trace(directory, &count);
    REQUIRE(count == 2);
    free(text);
    return true;
}

TEST_CASE(case_timings_preserve_failed_case_result)
{
    char directory[1024];
    path(directory, "failure");
    REQUIRE(!mkdir(directory, 0700));
    REQUIRE(run("fail", NULL) == 1);
    char *plain = tool_read(log_path);
    REQUIRE(run("fail", directory) == 1);
    char *timed = tool_read(log_path);
    REQUIRE(plain && timed && !strcmp(plain, timed));
    free(plain);
    free(timed);
    int count;
    char *text = trace(directory, &count);
    REQUIRE(count == 1 && text && strstr(text, "end\t1\ttiming_failure\t"));
    free(text);
    return true;
}

TEST_CASE(case_timings_sink_failure_is_observational)
{
    char directory[1024];
    path(directory, "absent");
    REQUIRE(run("wait", directory) == 0);
    char *text = tool_read(log_path);
    REQUIRE(text && strstr(text, "cannot create private sink"));
    REQUIRE(strstr(text, "remaining cases unmeasured"));
    free(text);
    return true;
}

TEST_CASE(case_timings_identify_interrupted_cases)
{
    char directory[1024];
    path(directory, "interrupted");
    REQUIRE(!mkdir(directory, 0700));
    REQUIRE(run("interrupted", directory) == 0);
    int count;
    char *text = trace(directory, &count);
    REQUIRE(count == 1 && text);
    REQUIRE(strstr(text, "begin\t1\ttiming_interrupted\t0\n"));
    REQUIRE(!strstr(text, "end\t1\ttiming_interrupted"));
    free(text);
    return true;
}

TEST_CASE(case_timings_bound_records_and_names)
{
    char directory[1024];
    path(directory, "limit");
    REQUIRE(!mkdir(directory, 0700));
    REQUIRE(run("limit", directory) == 0);
    int count;
    char *text = trace(directory, &count);
    REQUIRE(count == 1 && text);
    REQUIRE(strstr(text, "4096\ttiming_empty\t"));
    REQUIRE(!strstr(text, "4097\ttiming_empty\t"));
    REQUIRE(strstr(text, "# truncated: case limit reached"));
    free(text);
    path(directory, "name");
    REQUIRE(!mkdir(directory, 0700));
    REQUIRE(run("name", directory) == 0);
    text = trace(directory, &count);
    REQUIRE(count == 1 && text && strstr(text, "# truncated: unsupported case name"));
    free(text);
    path(directory, "length");
    REQUIRE(!mkdir(directory, 0700));
    REQUIRE(run("length", directory) == 0);
    text = trace(directory, &count);
    REQUIRE(count == 1 && text && strstr(text, "# truncated: unsupported case name"));
    char name[129];
    long long wall;
    char *end = strstr(text, "end\t1\t");
    REQUIRE(end && sscanf(end, "end\t1\t%128s\t%lld", name, &wall) == 2);
    REQUIRE(strlen(name) == 128 && wall >= 0);
    REQUIRE(!strstr(text, "begin\t2\t"));
    free(text);
    return true;
}

void test_tool_case_timings(const char *self)
{
    driver = self;
    strcpy(root, "/tmp/chibi-case-timings-XXXXXX");
    assert(mkdtemp(root));
    path(log_path, "probe.log");
    RUN_TEST_CASE(case_timings_preserve_output_and_measure_children_and_hooks);
    RUN_TEST_CASE(case_timings_preserve_failed_case_result);
    RUN_TEST_CASE(case_timings_sink_failure_is_observational);
    RUN_TEST_CASE(case_timings_bound_records_and_names);
    RUN_TEST_CASE(case_timings_identify_interrupted_cases);
    tool_remove_tree(root);
}
