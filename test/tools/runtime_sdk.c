#ifndef _WIN32
#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define RUNTIME_SDK_PATH 4096
#define RUNTIME_SDK_RUN_SECONDS 120
#define RUNTIME_SDK_SOURCES 2

static char host_path[RUNTIME_SDK_PATH], suite[RUNTIME_SDK_PATH],
    log_path[RUNTIME_SDK_PATH];

static void join(char out[RUNTIME_SDK_PATH], const char *base, const char *name)
{
    int size = snprintf(out, RUNTIME_SDK_PATH, "%s/%s", base, name);
    assert(size > 0 && size < RUNTIME_SDK_PATH);
}

/* Runs one authored check against the checked-in graph collection. */
static int run_program(const char *source, const char *work)
{
    const char *const argv[] = { host_path,
                                 "--root",
                                 CHIBI_SOURCE_ROOT,
                                 "--source",
                                 source,
                                 "--core",
                                 "packages/core",
                                 "--collection",
                                 "graph=tools/graph",
                                 "--entry",
                                 "Build",
                                 "--timeout-ms",
                                 "100000",
                                 "--",
                                 work,
                                 NULL };
    int status = tool_run_within(argv, CHIBI_SOURCE_ROOT, log_path, RUNTIME_SDK_RUN_SECONDS);
    if (status) {
        char *log = tool_read(log_path);
        fprintf(stderr, "%s\n", log ? log : "no output");
        free(log);
    }
    return status;
}

static bool run_authored(const char *name)
{
    char source[RUNTIME_SDK_PATH], work[RUNTIME_SDK_PATH], file[RUNTIME_SDK_PATH];
    snprintf(file, sizeof file, "test/tools/RuntimeSDK%s.chibi", name);
    join(work, suite, name);
    int made = mkdir(work, 0700);
    assert(!made);
    join(source, CHIBI_SOURCE_ROOT, file);
    return !run_program(source, work);
}

TEST_CASE(canonical_json_matches_the_python_manifest_encoder)
{
    REQUIRE(run_authored("JSON"));
    return true;
}

#ifdef __APPLE__
/* Build-host programs import collections, not sibling files, so the installer
 * entry and its check become one source, exactly as `chibi_ctl` would run it. */
static void write_installer_program(const char *path)
{
    char installer[RUNTIME_SDK_PATH], check[RUNTIME_SDK_PATH];
    join(installer, CHIBI_SOURCE_ROOT, "tools/toolchain/RuntimeSDK.chibi");
    join(check, CHIBI_SOURCE_ROOT, "test/tools/RuntimeSDKInstall.chibi");
    char *sources[RUNTIME_SDK_SOURCES] = { tool_read(installer), tool_read(check) };
    assert(sources[0] && sources[1]);
    /* The check repeats the installer's package and import lines. */
    const char *body = sources[1];
    for (const char *import = body; (import = strstr(import, "\nimport ")); import++) {
        body = strchr(import + 1, '\n');
        assert(body);
    }
    size_t size = strlen(sources[0]) + strlen(body) + 1;
    char *program = malloc(size);
    assert(program);
    snprintf(program, size, "%s%s", sources[0], body);
    tool_write(path, program);
    free(program);
    free(sources[0]);
    free(sources[1]);
}

TEST_CASE(installer_verifies_replaces_and_rolls_back_packages)
{
    char program[RUNTIME_SDK_PATH], work[RUNTIME_SDK_PATH];
    join(program, suite, "Install.chibi");
    join(work, suite, "install");
    assert(!mkdir(work, 0700));
    write_installer_program(program);
    REQUIRE(!run_program(program, work));
    return true;
}

TEST_CASE(graph_evidence_binds_records_tools_and_configuration)
{
    REQUIRE(run_authored("Evidence"));
    return true;
}
#endif

void test_runtime_sdk(const char *host)
{
    char *resolved = realpath(host, NULL);
    assert(resolved && strlen(resolved) < sizeof host_path);
    strcpy(host_path, resolved);
    free(resolved);
    strcpy(suite, "/tmp/chibi-runtime-sdk-XXXXXX");
    assert(mkdtemp(suite));
    resolved = realpath(suite, NULL);
    assert(resolved && strlen(resolved) < sizeof suite);
    strcpy(suite, resolved);
    free(resolved);
    join(log_path, suite, "runtime-sdk.log");
    RUN_TEST_CASE(canonical_json_matches_the_python_manifest_encoder);
#ifdef __APPLE__
    RUN_TEST_CASE(installer_verifies_replaces_and_rolls_back_packages);
    RUN_TEST_CASE(graph_evidence_binds_records_tools_and_configuration);
#endif
    tool_remove_tree(suite);
}
#endif
