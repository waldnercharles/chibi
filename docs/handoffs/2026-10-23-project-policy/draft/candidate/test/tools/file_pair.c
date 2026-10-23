#include "support/file_pair.h"

#include "host/build_host_internal.h"
#include "support/sha256.h"
#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char root[128], names[4][256];
static chibi_build_compiler *compiler;

static file_pair_plan start(int prior)
{
    tool_remove_tree(root);
    assert(!mkdir(root, 0700));
    static const char *leaves[] = { "stage0", "destination0", "stage1", "destination1" };
    for (int i = 0; i < 4; i++) {
        snprintf(names[i], sizeof names[i], "%s/%s", root, leaves[i]);
    }
    tool_write(names[0], "new binary");
    tool_write(names[2], "new manifest");
    if (prior & 1) {
        tool_write(names[1], "old binary");
    }
    if (prior & 2) {
        tool_write(names[3], "old manifest");
    }
    return (file_pair_plan){ .source = { names[0], names[2] },
                             .destination = { names[1], names[3] },
                             .mode = FILE_PAIR_EXCLUSIVE };
}
static bool bytes(const char *path, const char *expected)
{
    char *text = tool_read(path);
    bool same = text && !strcmp(text, expected);
    free(text);
    return same;
}
static bool absent(const char *path)
{
    struct stat info;
    return lstat(path, &info) && errno == ENOENT;
}
static int entries()
{
    DIR *directory = opendir(root);
    assert(directory);
    int count = 0;
    struct dirent *entry;
    while ((entry = readdir(directory))) {
        count += strcmp(entry->d_name, ".") && strcmp(entry->d_name, "..");
    }
    closedir(directory);
    return count;
}
static bool prior_pair(int prior)
{
    return ((prior & 1) ? bytes(names[1], "old binary") : absent(names[1])) &&
           ((prior & 2) ? bytes(names[3], "old manifest") : absent(names[3]));
}

TEST_CASE(pair_commits_every_prior_presence_combination)
{
    for (int prior = 0; prior < 4; prior++) {
        file_pair_plan plan = start(prior);
        file_pair pair;
        REQUIRE(file_pair_prepare(&pair, &plan));
        REQUIRE(pair.reserved[0] && pair.reserved[1]);
        REQUIRE(!pair.old_moved[0] && !pair.old_moved[1] && prior_pair(prior));
        REQUIRE(file_pair_execute(&pair) == 0);
        REQUIRE(pair.committed && pair.consumed[0] && pair.consumed[1]);
        REQUIRE(bytes(names[1], "new binary") && bytes(names[3], "new manifest"));
        REQUIRE(absent(names[0]) && absent(names[2]) && entries() == 2);
        REQUIRE(!pair.diagnostic_size);
        file_pair_dispose(&pair);
    }
    return true;
}
TEST_CASE(pair_reservation_and_precommit_failures_preserve_prior_pair)
{
    const file_pair_operation operations[] = { FILE_PAIR_RESERVE,
                                               FILE_PAIR_BACKUP,
                                               FILE_PAIR_PUBLISH };
    for (int phase = 0; phase < 3; phase++) {
        for (int entry = 0; entry < 2; entry++) {
            file_pair_plan plan = start(3);
            plan.failures = FILE_PAIR_FAIL(operations[phase], entry);
            file_pair pair;
            bool prepared = file_pair_prepare(&pair, &plan);
            REQUIRE(prepared == (phase != 0));
            int status = prepared ? file_pair_execute(&pair) : pair.status;
            REQUIRE(status == 1 && !pair.committed && prior_pair(3));
            REQUIRE(pair.diagnostic_size && pair.diagnostic_size < pair.diagnostic_capacity);
            REQUIRE(bytes(names[0], "new binary") == (phase == 0));
            REQUIRE(bytes(names[2], "new manifest") == (phase == 0));
            REQUIRE(entries() == (phase == 0 ? 4 : 2));
            file_pair_dispose(&pair);
        }
    }
    return true;
}
TEST_CASE(pair_incomplete_restore_retains_old_bytes_and_full_recovery_path)
{
    for (int entry = 0; entry < 2; entry++) {
        file_pair_plan plan = start(3);
        plan.failures = FILE_PAIR_FAIL(FILE_PAIR_PUBLISH, 1) |
                        FILE_PAIR_FAIL(FILE_PAIR_RESTORE, entry);
        file_pair pair;
        REQUIRE(file_pair_prepare(&pair, &plan));
        REQUIRE(file_pair_execute(&pair) == 2);
        REQUIRE(!pair.committed && pair.old_moved[entry] && pair.retained_backup[entry]);
        REQUIRE(bytes(pair.backup[entry], entry ? "old manifest" : "old binary"));
        REQUIRE(absent(names[entry * 2 + 1]));
        REQUIRE(bytes(names[(1 - entry) * 2 + 1], entry ? "old binary" : "old manifest"));
        REQUIRE(absent(names[0]) && absent(names[2]));
        REQUIRE(strstr(pair.diagnostic, pair.backup[entry]) && entries() == 2);
        char *retained = strdup(pair.backup[entry]);
        REQUIRE(retained);
        file_pair_dispose(&pair);
        REQUIRE(bytes(retained, entry ? "old manifest" : "old binary"));
        free(retained);
    }
    return true;
}
TEST_CASE(pair_failed_removal_does_not_claim_prior_absence_restored)
{
    for (int prior = 0; prior < 2; prior++) {
        file_pair_plan plan = start(prior);
        plan.failures = FILE_PAIR_FAIL(FILE_PAIR_PUBLISH, 1) |
                        FILE_PAIR_FAIL(FILE_PAIR_REMOVE_NEW, 0);
        file_pair pair;
        REQUIRE(file_pair_prepare(&pair, &plan));
        REQUIRE(file_pair_execute(&pair) == 2);
        REQUIRE(pair.existed[0] == !!prior && pair.old_moved[0] == !!prior);
        REQUIRE(pair.published[0] && bytes(names[1], "new binary") && absent(names[3]));
        REQUIRE(absent(names[0]) && absent(names[2]) && entries() == 1 + prior);
        if (prior) {
            REQUIRE(pair.retained_backup[0] && bytes(pair.backup[0], "old binary"));
        }
        file_pair_dispose(&pair);
    }
    return true;
}
TEST_CASE(pair_committed_cleanup_failure_keeps_new_pair)
{
    for (int prior = 0; prior < 4; prior++) {
        for (int entry = 0; entry < 2; entry++) {
            file_pair_plan plan = start(prior);
            plan.failures = FILE_PAIR_FAIL(FILE_PAIR_REMOVE_BACKUP, entry);
            file_pair pair;
            REQUIRE(file_pair_prepare(&pair, &plan));
            REQUIRE(file_pair_execute(&pair) == 3 && pair.committed);
            REQUIRE(bytes(names[1], "new binary") && bytes(names[3], "new manifest"));
            REQUIRE(pair.retained_backup[entry] && entries() == 3);
            REQUIRE(pair.old_moved[entry] == !!(prior & (1 << entry)));
            REQUIRE(strstr(pair.diagnostic, pair.backup[entry]));
            file_pair_dispose(&pair);
        }
    }
    return true;
}
TEST_CASE(pair_failed_staging_cleanup_is_diagnosed_without_undoing_restore)
{
    for (int entry = 0; entry < 2; entry++) {
        file_pair_plan plan = start(3);
        plan.failures = FILE_PAIR_FAIL(FILE_PAIR_PUBLISH, 0) |
                        FILE_PAIR_FAIL(FILE_PAIR_REMOVE_SOURCE, entry);
        file_pair pair;
        REQUIRE(file_pair_prepare(&pair, &plan));
        REQUIRE(file_pair_execute(&pair) == 1 && prior_pair(3));
        REQUIRE(pair.retained_source[entry]);
        REQUIRE(bytes(names[entry * 2], entry ? "new manifest" : "new binary"));
        REQUIRE(strstr(pair.diagnostic, names[entry * 2]));
        REQUIRE(absent(names[(1 - entry) * 2]) && entries() == 3);
        file_pair_dispose(&pair);
    }
    return true;
}
TEST_CASE(native_pair_mode_preserves_relative_names)
{
    start(3);
    char previous[PATH_MAX];
    REQUIRE(getcwd(previous, sizeof previous));
    REQUIRE(!chdir(root));
    file_pair_plan plan = { .source = { "./stage0", "stage1" },
                            .destination = { "destination0", "./destination1" },
                            .mode = FILE_PAIR_NATIVE };
    file_pair pair;
    bool prepared = file_pair_prepare(&pair, &plan);
    int status = prepared ? file_pair_execute(&pair) : pair.status;
    file_pair_dispose(&pair);
    bool restored_cwd = !chdir(previous);
    REQUIRE(restored_cwd && prepared && status == 0);
    REQUIRE(bytes(names[1], "new binary") && bytes(names[3], "new manifest"));
    REQUIRE(entries() == 2);
    return true;
}

static const char *raw_call_options(
    const char *const input[4],
    chibi_build_result *result,
    size_t used,
    const chibi_build_options *options
)
{
    *result = (chibi_build_result){ .stderr_size = used };
    memset(result->stderr_text, 'p', used);
    build_host host = { .options = options,
                        .result = result,
                        .deadline = options->deadline };
    pin_value arguments[4];
    for (int i = 0; i < 4; i++) {
        arguments[i] = (pin_value){
            .type = PIN_STR,
            .text = build_values_text(&host.values, input[i], strlen(input[i]))
        };
        assert(arguments[i].text);
    }
    pin_value value = { 0 };
    const char *error = build_host_publish_pair(&host, arguments, &value);
    result->value = value.i;
    arena_free(&host.values.memory);
    return error;
}
static const char *raw_call(const char *const input[4], chibi_build_result *result, size_t used)
{
    chibi_build_options options = { .deadline = chibi_build_now() + 10 };
    return raw_call_options(input, result, used, &options);
}
TEST_CASE(raw_pair_cancellation_and_deadline_preserve_sources_and_prior_pair)
{
    for (int expired = 0; expired < 2; expired++) {
        start(3);
        const char *input[] = { names[0], names[1], names[2], names[3] };
        volatile sig_atomic_t cancelled = !expired;
        chibi_build_options options = { .cancelled = &cancelled,
                                        .deadline = chibi_build_now() +
                                                    (expired ? -1 : 10) };
        chibi_build_result result;
        const char *error = raw_call_options(input, &result, 0, &options);
        REQUIRE(error && !strcmp(error, expired ? "build process timeout" : "build process cancelled"));
        REQUIRE(prior_pair(3) && bytes(names[0], "new binary") && bytes(names[2], "new manifest"));
        REQUIRE(entries() == 4 && !result.stderr_size);
    }
    return true;
}
TEST_CASE(raw_pair_rejects_aliases_links_and_capacity_before_mutation)
{
    const char *input[] = { names[0], names[1], names[2], names[3] };
    chibi_build_result result;
    for (int scenario = 0; scenario < 6; scenario++) {
        start(3);
        input[0] = names[0];
        input[1] = names[1];
        if (scenario == 0) {
            input[0] = "stage0";
        } else if (scenario == 1) {
            input[0] = names[1];
        } else if (scenario == 2) {
            REQUIRE(!unlink(names[2]) && !link(names[0], names[2]));
        } else if (scenario == 3) {
            REQUIRE(!unlink(names[0]) && !symlink(names[2], names[0]));
        }
        size_t used = scenario == 4 ? CHIBI_BUILD_OUTPUT_CAPACITY - 16
                    : scenario == 5 ? CHIBI_BUILD_OUTPUT_CAPACITY - 700
                                    : 0;
        const char *error = raw_call(input, &result, used);
        REQUIRE(error && prior_pair(3) && entries() == 4);
        REQUIRE(!result.stderr_truncated);
        if (scenario == 4) {
            REQUIRE(
                result.stderr_size == used &&
                strstr(error, "recovery paths exceed capture capacity")
            );
        } else {
            REQUIRE(result.stderr_size > used);
            REQUIRE(strstr(result.stderr_text + used, input[0]));
            REQUIRE(strstr(result.stderr_text + used, input[2]));
        }
    }
    return true;
}
TEST_CASE(raw_pair_missing_case_aliases_cannot_falsely_commit)
{
    start(0);
    char upper[256], lower[256];
    snprintf(upper, sizeof upper, "%s/CasePair", root);
    snprintf(lower, sizeof lower, "%s/casepair", root);
    tool_write(upper, "case witness");
    bool folds = bytes(lower, "case witness");
    REQUIRE(!unlink(upper));
    const char *input[] = { names[0], upper, names[2], lower };
    chibi_build_result result;
    REQUIRE(!raw_call(input, &result, 0));
    if (folds) {
        REQUIRE(result.value == 1 && absent(upper) && absent(lower));
        REQUIRE(absent(names[0]) && absent(names[2]) && entries() == 0);
    } else {
        REQUIRE(result.value == 0 && bytes(upper, "new binary") && bytes(lower, "new manifest"));
        REQUIRE(entries() == 2);
        fprintf(stderr, "case-insensitive missing-alias branch not covered on this filesystem\n");
    }
    return true;
}

/* Keep every historical function unchanged except the identity declaration's
 * name. Supply one fixed identity to isolate ProjectAssemble's publication
 * contract from unrelated SDK preparation. Both policies must compile/run;
 * a missing function or source error cannot pass this regression. */
static char *regression_source(const char *text)
{
    const char *marker = strstr(text, "ProjectIdentity ::");
    assert(marker && !strstr(marker + 1, "ProjectIdentity ::"));
    size_t prefix = (size_t)(marker - text);
    size_t capacity = strlen(text) + strlen(root) * 4 + 2048;
    char *source = malloc(capacity);
    assert(source);
    memcpy(source, text, prefix);
    snprintf(
        source + prefix,
        capacity - prefix,
        "Unused%s\n"
        "ProjectIdentity :: (arguments: string[]) -> string { return "
        "\"fixed\"; }\n"
        "PairRegression :: () -> int { return "
        "ProjectAssemble([\"--route\",\"native\","
        "\"--assembly\",\"%s/assembly\",\"--project-root\",\"%s\"],\"fixed\","
        "\"%s/work\",\"%s/destination0\"); }\n",
        marker,
        root,
        root,
        root,
        root
    );
    return source;
}
static char *current_policy()
{
    char *text = tool_read(CHIBI_SOURCE_ROOT "/tools/toolchain/Project.chibi");
    assert(text);
    char *source = regression_source(text);
    free(text);
    return source;
}
static void assembly_script(const char *path, const char *body)
{
    static const char
        prefix[] = "#!/bin/sh\noutput=\nwhile [ \"$#\" -gt 0 ]; do\n"
                   "if [ \"$1\" = --output ]; then shift; output=$1; fi\n"
                   "shift\ndone\n";
    size_t size = strlen(prefix) + strlen(body) + 1;
    char *script = malloc(size);
    assert(script);
    snprintf(script, size, "%s%s", prefix, body);
    tool_write(path, script);
    free(script);
    assert(!chmod(path, 0700));
}
static bool invoke_compile(
    const char *source,
    const char *assembly,
    const char *sdk,
    const char *cache,
    chibi_build_result *result
)
{
    const char *arguments[28] = { "--route",
                                  cache ? "selfhost" : "native",
                                  "--target",
                                  "native",
                                  "--sdk-root",
                                  sdk,
                                  "--inventory",
                                  "unused.tsv",
                                  "--receipt",
                                  "unused.tsv",
                                  "--build-host",
                                  assembly,
                                  "--assembly",
                                  assembly,
                                  "--invocation-entry",
                                  "Compile",
                                  "--project-root",
                                  root,
                                  "--output",
                                  names[1] };
    int count = 20;
    if (cache) {
        const char *extra[] = { "--compiler-host", assembly,
                                "--cache",         cache,
                                "--budget",        "100000",
                                "--timeout-ms",    "1000" };
        for (int i = 0; i < 8; i++) {
            arguments[count++] = extra[i];
        }
    }
    chibi_build_options options = { .compiler = compiler,
                                    .root = root,
                                    .source_path = "ProjectCompilePair.chibi",
                                    .source_text = source,
                                    .entry = "Compile",
                                    .backend = BEHAVIOR_BYTECODE,
                                    .core = CHIBI_SOURCE_ROOT "/packages/core",
                                    .arguments = arguments,
                                    .argument_count = count,
                                    .deadline = chibi_build_now() + 120 };
    bool ran = chibi_build_invoke(&options, result);
    if (!ran) {
        fprintf(stderr, "policy Compile preparation/runtime error: %s\n", result->diagnostic);
    }
    return ran;
}
static bool no_project_staging()
{
    DIR *directory = opendir(root);
    assert(directory);
    bool clean = true;
    struct dirent *entry;
    while ((entry = readdir(directory))) {
        clean &= strncmp(entry->d_name, "destination0.stage-one.", strlen("destination0.stage-one.")) !=
                 0;
        clean &= strncmp(entry->d_name, "destination0.tmp.", strlen("destination0.tmp.")) !=
                 0;
        clean &= strncmp(
                     entry->d_name,
                     "destination0.resources.json.tmp.",
                     strlen("destination0.resources.json.tmp.")
                 ) != 0;
    }
    closedir(directory);
    return clean;
}
TEST_CASE(project_compile_publishes_both_new_files_and_leaves_no_staging)
{
    start(1);
    char assembly[256], sdk[256], sidecar[256];
    snprintf(assembly, sizeof assembly, "%s/assembly", root);
    snprintf(sdk, sizeof sdk, "%s/sdk", root);
    snprintf(sidecar, sizeof sidecar, "%s/destination0.resources.json", root);
    REQUIRE(!mkdir(sdk, 0700));
    tool_write(sidecar, "old manifest");
    assembly_script(
        assembly,
        "printf 'assembled binary' > \"$output\"\n"
        "printf 'assembled manifest' > \"$output.resources.json\"\n"
    );
    char *source = current_policy();
    chibi_build_result result;
    bool ran = invoke_compile(source, assembly, sdk, NULL, &result);
    free(source);
    REQUIRE(ran && result.value == 0);
    REQUIRE(bytes(names[1], "assembled binary") && bytes(sidecar, "assembled manifest"));
    REQUIRE(no_project_staging());
    return true;
}
TEST_CASE(project_post_child_identity_drift_preserves_both_old_files)
{
    start(1);
    char assembly[256], sdk[256], sidecar[256], marker[256];
    snprintf(assembly, sizeof assembly, "%s/assembly", root);
    snprintf(sdk, sizeof sdk, "%s/sdk", root);
    snprintf(sidecar, sizeof sidecar, "%s/destination0.resources.json", root);
    snprintf(marker, sizeof marker, "%s/drift", root);
    REQUIRE(!mkdir(sdk, 0700));
    tool_write(sidecar, "old manifest");
    char body[1024];
    snprintf(
        body,
        sizeof body,
        "printf 'assembled binary' > \"$output\"\n"
        "printf 'assembled manifest' > \"$output.resources.json\"\n"
        "printf changed > '%s'\n",
        marker
    );
    assembly_script(assembly, body);
    char *base = current_policy();
    const char *stub = strstr(base, "ProjectIdentity :: (arguments: string[]) -> string { return \"fixed\"; }");
    REQUIRE(stub);
    size_t prefix = (size_t)(stub - base);
    size_t capacity = strlen(base) + strlen(marker) + 256;
    char *source = malloc(capacity);
    REQUIRE(source);
    memcpy(source, base, prefix);
    snprintf(
        source + prefix,
        capacity - prefix,
        "ProjectIdentity :: (arguments: string[]) -> string { "
        "if (build.file_exists(\"%s\")) { return \"changed\"; } return "
        "\"fixed\"; }%s",
        marker,
        stub + strlen("ProjectIdentity :: (arguments: string[]) -> string { return \"fixed\"; }")
    );
    free(base);
    chibi_build_result result;
    bool ran = invoke_compile(source, assembly, sdk, NULL, &result);
    free(source);
    REQUIRE(ran && result.value == 1 && bytes(marker, "changed"));
    REQUIRE(bytes(names[1], "old binary") && bytes(sidecar, "old manifest"));
    REQUIRE(no_project_staging());
    return true;
}
TEST_CASE(project_protected_sidecar_aliases_reject_before_assembly)
{
    for (int role = 0; role < 3; role++) {
        start(1);
        char assembly[256], sdk[256], cache[256], sidecar[256], protected[256],
            marker[256];
        snprintf(assembly, sizeof assembly, "%s/assembly", root);
        snprintf(sdk, sizeof sdk, "%s/sdk", root);
        snprintf(cache, sizeof cache, "%s/cache", root);
        snprintf(sidecar, sizeof sidecar, "%s/destination0.resources.json", root);
        snprintf(protected, sizeof protected, "%s/%s/protected", root, role ? "cache" : "sdk");
        snprintf(marker, sizeof marker, "%s/assembled", root);
        REQUIRE(!mkdir(sdk, 0700) && !mkdir(cache, 0700));
        char body[512];
        snprintf(body, sizeof body, "printf ran > '%s'\n", marker);
        assembly_script(assembly, body);
        char *expected = tool_read(assembly);
        REQUIRE(expected);
        if (role == 2) {
            REQUIRE(!link(assembly, sidecar));
        } else {
            tool_write(protected, "protected bytes");
            REQUIRE(!symlink(protected, sidecar));
        }
        struct stat before, after;
        REQUIRE(!lstat(sidecar, &before));
        char *source = current_policy();
        chibi_build_result result;
        bool ran = invoke_compile(source, assembly, sdk, role == 1 ? cache : NULL, &result);
        free(source);
        REQUIRE(ran && result.value == 2 && absent(marker));
        REQUIRE(bytes(names[1], "old binary") && no_project_staging());
        REQUIRE(
            !lstat(sidecar, &after) && before.st_ino == after.st_ino &&
            before.st_dev == after.st_dev
        );
        REQUIRE(bytes(assembly, expected));
        REQUIRE(role == 2 ? bytes(sidecar, expected) : bytes(protected, "protected bytes"));
        free(expected);
        REQUIRE(strstr(result.stderr_text, "output path is unavailable or protected"));
    }
    return true;
}
TEST_CASE(project_missing_sidecar_rejects_actual_f798_publication_behavior)
{
    char *old = tool_read(CHIBI_SOURCE_ROOT "/test/fixtures/toolchain/Project-before-pair.chibi");
    char *current = tool_read(CHIBI_SOURCE_ROOT "/tools/toolchain/Project.chibi");
    REQUIRE(old && current);
    char digest[65];
    sha256_hex(old, strlen(old), digest);
    REQUIRE(!strcmp(digest, "f7987d0577e433844069b6d498b4cb200ac34bdd3aaefbf6d17eda34af7644fb"));
    for (int version = 0; version < 2; version++) {
        start(1);
        char assembly[256], work[256], sidecar[256];
        snprintf(assembly, sizeof assembly, "%s/assembly", root);
        snprintf(work, sizeof work, "%s/work", root);
        snprintf(sidecar, sizeof sidecar, "%s/destination0.resources.json", root);
        tool_write(sidecar, "old manifest");
        tool_write(
            assembly,
            "#!/bin/sh\noutput=\nwhile [ \"$#\" -gt 0 ]; do\n"
            "if [ \"$1\" = --output ]; then shift; output=$1; fi\n"
            "shift\ndone\nprintf 'assembled binary' > \"$output\"\n"
        );
        REQUIRE(!chmod(assembly, 0700) && !mkdir(work, 0700));
        char *source = regression_source(version ? current : old);
        chibi_build_options options = {
            .compiler = compiler,
            .root = root,
            .source_path = "ProjectPairRegression.chibi",
            .source_text = source,
            .entry = "PairRegression",
            .backend = BEHAVIOR_BYTECODE,
            .core = CHIBI_SOURCE_ROOT "/packages/core",
            .deadline = chibi_build_now() + 120
        };
        chibi_build_result result;
        bool ran = chibi_build_invoke(&options, &result);
        free(source);
        if (!ran) {
            fprintf(
                stderr,
                "policy regression preparation/runtime error: %s\n",
                result.diagnostic
            );
        }
        REQUIRE(ran && result.value == (version ? 1 : 0));
        REQUIRE(bytes(names[1], version ? "old binary" : "assembled binary"));
        /* The actual sidecar final must be paired with destination0. */
        REQUIRE(bytes(sidecar, "old manifest"));
    }
    free(old);
    free(current);
    return true;
}

TEST_CASE(project_does_not_clean_a_reused_reservation_after_publish_attempt)
{
    start(1);
    char assembly[256], sdk[256], sidecar[256], next[256], staged[256],
        staged_sidecar[256];
    snprintf(assembly, sizeof assembly, "%s/assembly", root);
    snprintf(sdk, sizeof sdk, "%s/sdk", root);
    snprintf(sidecar, sizeof sidecar, "%s/destination0.resources.json", root);
    snprintf(next, sizeof next, "%s/destination0.stage-one.0/next-owner", root);
    snprintf(staged, sizeof staged, "%s/destination0.stage-one.0.artifact", root);
    snprintf(staged_sidecar, sizeof staged_sidecar, "%s/destination0.stage-one.0.artifact.resources.json", root);
    REQUIRE(!mkdir(sdk, 0700));
    tool_write(sidecar, "old manifest");
    tool_write(
        assembly,
        "#!/bin/sh\noutput=\nwhile [ \"$#\" -gt 0 ]; do\n"
        "if [ \"$1\" = --output ]; then shift; output=$1; fi\n"
        "shift\ndone\nprintf 'assembled binary' > \"$output\"\n"
        "printf 'assembled manifest' > \"$output.resources.json\"\n"
    );
    REQUIRE(!chmod(assembly, 0700));
    char *text = tool_read(CHIBI_SOURCE_ROOT "/tools/toolchain/Project.chibi");
    REQUIRE(text);
    char *base = regression_source(text);
    free(text);
    const char *call = strstr(base, "build.publish_pair(");
    REQUIRE(call && !strstr(call + 1, "build.publish_pair("));
    size_t prefix = (size_t)(call - base);
    size_t capacity = strlen(base) + 2048;
    char *source = malloc(capacity);
    REQUIRE(source);
    memcpy(source, base, prefix);
    snprintf(
        source + prefix,
        capacity - prefix,
        "PairOwnershipPublish(%s\n"
        "PairOwnershipPublish :: (s0: string, d0: string, s1: string, d1: "
        "string) -> int {\n"
        " if (!build.remove(s0) || !build.remove(s1)) { return 5; }\n"
        " work := ProjectJoin([d0,\".stage-one.0\"]);\n"
        " if (!build.reserve_directory(work) || "
        "!build.write_file(ProjectJoin([work,\"/next-owner\"]),\"next\") || "
        "!build.write_file(s0,\"next staged binary\") || "
        "!build.write_file(s1,\"next staged manifest\")) { return 5; }\n"
        " return 1;\n}\n",
        call + strlen("build.publish_pair(")
    );
    free(base);
    /* This authored stand-in models an admitted failure consuming staging,
     * followed immediately by another invocation reserving the freed N.
     * The private helper is replaced only here, not exposed by the host. */
    const char *arguments[] = {
        "--route",        "native",     "--target",           "native",
        "--sdk-root",     sdk,          "--inventory",        "unused.tsv",
        "--receipt",      "unused.tsv", "--build-host",       assembly,
        "--assembly",     assembly,     "--invocation-entry", "Compile",
        "--project-root", root,         "--output",           names[1]
    };
    chibi_build_options options = { .compiler = compiler,
                                    .root = root,
                                    .source_path = "ProjectPairOwnership.chibi",
                                    .source_text = source,
                                    .entry = "Compile",
                                    .backend = BEHAVIOR_BYTECODE,
                                    .core = CHIBI_SOURCE_ROOT "/packages/core",
                                    .arguments = arguments,
                                    .argument_count = sizeof arguments / sizeof *arguments,
                                    .deadline = chibi_build_now() + 120 };
    chibi_build_result result;
    bool ran = chibi_build_invoke(&options, &result);
    free(source);
    if (!ran) {
        fprintf(
            stderr,
            "policy ownership preparation/runtime error: %s\n",
            result.diagnostic
        );
    }
    REQUIRE(ran && result.value == 1);
    REQUIRE(bytes(names[1], "old binary") && bytes(sidecar, "old manifest"));
    REQUIRE(bytes(next, "next") && bytes(staged, "next staged binary"));
    REQUIRE(bytes(staged_sidecar, "next staged manifest"));
    return true;
}

TEST_CASE(project_staged_cleanup_failure_keeps_work_reservation)
{
    start(1);
    char assembly[256], sdk[256], work[256], retained[256], sidecar[256];
    snprintf(assembly, sizeof assembly, "%s/assembly", root);
    snprintf(sdk, sizeof sdk, "%s/sdk", root);
    snprintf(work, sizeof work, "%s/destination0.stage-one.0", root);
    snprintf(retained, sizeof retained, "%s/destination0.stage-one.0.artifact.resources.json/payload", root);
    snprintf(sidecar, sizeof sidecar, "%s/destination0.resources.json", root);
    REQUIRE(!mkdir(sdk, 0700));
    tool_write(sidecar, "old manifest");
    tool_write(
        assembly,
        "#!/bin/sh\noutput=\nwhile [ \"$#\" -gt 0 ]; do\n"
        "if [ \"$1\" = --output ]; then shift; output=$1; fi\n"
        "shift\ndone\nprintf 'assembled binary' > \"$output\"\n"
        "mkdir \"$output.resources.json\"\n"
        "printf 'owned payload' > \"$output.resources.json/payload\"\n"
    );
    REQUIRE(!chmod(assembly, 0700));
    char *text = tool_read(CHIBI_SOURCE_ROOT "/tools/toolchain/Project.chibi");
    REQUIRE(text);
    char *source = regression_source(text);
    free(text);
    const char *arguments[] = {
        "--route",        "native",     "--target",           "native",
        "--sdk-root",     sdk,          "--inventory",        "unused.tsv",
        "--receipt",      "unused.tsv", "--build-host",       assembly,
        "--assembly",     assembly,     "--invocation-entry", "Compile",
        "--project-root", root,         "--output",           names[1]
    };
    chibi_build_options options = { .compiler = compiler,
                                    .root = root,
                                    .source_path = "ProjectPairCleanup.chibi",
                                    .source_text = source,
                                    .entry = "Compile",
                                    .backend = BEHAVIOR_BYTECODE,
                                    .core = CHIBI_SOURCE_ROOT "/packages/core",
                                    .arguments = arguments,
                                    .argument_count = sizeof arguments / sizeof *arguments,
                                    .deadline = chibi_build_now() + 120 };
    chibi_build_result result;
    bool ran = chibi_build_invoke(&options, &result);
    free(source);
    if (!ran) {
        fprintf(stderr, "policy cleanup preparation/runtime error: %s\n", result.diagnostic);
    }
    REQUIRE(ran && result.value == 1);
    REQUIRE(bytes(names[1], "old binary") && bytes(sidecar, "old manifest"));
    struct stat info;
    REQUIRE(!lstat(work, &info) && S_ISDIR(info.st_mode));
    REQUIRE(bytes(retained, "owned payload"));
    char expected[1024];
    snprintf(
        expected,
        sizeof expected,
        "project: assembly failed or installed inputs changed\n"
        "project: cannot remove owned project sidecar: "
        "%s/destination0.stage-one.0.artifact.resources.json\n",
        root
    );
    REQUIRE(!result.stderr_truncated && result.stderr_size == strlen(expected));
    REQUIRE(!strcmp(result.stderr_text, expected));
    return true;
}

void test_tool_file_pair(chibi_build_compiler *build_compiler)
{
    compiler = build_compiler;
    strcpy(root, "/tmp/chibi-file-pair-XXXXXX");
    assert(mkdtemp(root));
    char *canonical = realpath(root, NULL);
    assert(canonical && strlen(canonical) < sizeof root);
    strcpy(root, canonical);
    free(canonical);
    RUN_TEST_CASE(pair_commits_every_prior_presence_combination);
    RUN_TEST_CASE(pair_reservation_and_precommit_failures_preserve_prior_pair);
    RUN_TEST_CASE(pair_incomplete_restore_retains_old_bytes_and_full_recovery_path);
    RUN_TEST_CASE(pair_failed_removal_does_not_claim_prior_absence_restored);
    RUN_TEST_CASE(pair_committed_cleanup_failure_keeps_new_pair);
    RUN_TEST_CASE(pair_failed_staging_cleanup_is_diagnosed_without_undoing_restore);
    RUN_TEST_CASE(native_pair_mode_preserves_relative_names);
    RUN_TEST_CASE(raw_pair_rejects_aliases_links_and_capacity_before_mutation);
    RUN_TEST_CASE(raw_pair_missing_case_aliases_cannot_falsely_commit);
    RUN_TEST_CASE(raw_pair_cancellation_and_deadline_preserve_sources_and_prior_pair);
    RUN_TEST_CASE(project_compile_publishes_both_new_files_and_leaves_no_staging);
    RUN_TEST_CASE(project_post_child_identity_drift_preserves_both_old_files);
    RUN_TEST_CASE(project_protected_sidecar_aliases_reject_before_assembly);
    RUN_TEST_CASE(project_missing_sidecar_rejects_actual_f798_publication_behavior);
    RUN_TEST_CASE(project_does_not_clean_a_reused_reservation_after_publish_attempt);
    RUN_TEST_CASE(project_staged_cleanup_failure_keeps_work_reservation);
    tool_remove_tree(root);
}
