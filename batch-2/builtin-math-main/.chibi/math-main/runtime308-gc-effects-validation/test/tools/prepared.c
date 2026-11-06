#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *producer, *consumer;
static char directory[] = "/tmp/chibi-prepared-XXXXXX";
static bool owns_directory;
static void cleanup()
{
    if (owns_directory) {
        tool_remove_tree(directory);
        owns_directory = false;
    }
}
static bool run(const char *const argv[], const char *log)
{
    int status = tool_run(argv, tool_root(), log);
    if (status) {
        return false; /* tool_run already printed the failed child output. */
    }
    char *output = tool_read(log);
    if (!output) {
        return false;
    }
    fputs(output, stdout);
    free(output);
    return true;
}
TEST_CASE(prepared_artifact_crosses_producer_process_and_runtime_only_link)
{
    REQUIRE(mkdtemp(directory));
    owns_directory = true;
    char log[sizeof directory + 32];
    int n = snprintf(log, sizeof log, "%s/producer.log", directory);
    REQUIRE(n > 0 && n < (int)sizeof log);
    const char *write[] = { producer, "--prepared-write", directory, NULL };
    REQUIRE(run(write, log));
    /* The producer has exited and all compiler products are gone before read. */
    n = snprintf(log, sizeof log, "%s/runtime.log", directory);
    REQUIRE(n > 0 && n < (int)sizeof log);
    const char *read[] = { consumer, "--prepared-read", directory, NULL };
    REQUIRE(run(read, log));
    return true;
}
void test_tool_prepared(const char *source_binary, const char *runtime_binary)
{
    producer = source_binary;
    consumer = runtime_binary;
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(prepared_artifact_crosses_producer_process_and_runtime_only_link);
    pu_clear_setup();
}

TEST_CASE(record_artifacts_cross_installed_runtime_boundary)
{
    REQUIRE(mkdtemp(directory));
    owns_directory = true;
    char log[sizeof directory + 32];
    int n = snprintf(log, sizeof log, "%s/producer.log", directory);
    REQUIRE(n > 0 && n < (int)sizeof log);
    const char *write[] = { producer, "--record-produce", directory, NULL };
    REQUIRE(run(write, log));
    n = snprintf(log, sizeof log, "%s/runtime.log", directory);
    REQUIRE(n > 0 && n < (int)sizeof log);
    const char *read[] = { consumer, "--record-read", directory, NULL };
    REQUIRE(run(read, log));
    return true;
}
void test_tool_record_prepared(const char *source_binary, const char *runtime_binary)
{
    producer = source_binary;
    consumer = runtime_binary;
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(record_artifacts_cross_installed_runtime_boundary);
    pu_clear_setup();
}
