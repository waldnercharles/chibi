#ifndef _WIN32
#include "support/array.h"
#include "support/sha256.h"
#include "support/xalloc.h"
#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"
#include "third_party/yyjson/yyjson.h"

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define SDK_PATH 4096
#define SDK_MANIFEST "share/chibi/compiler-tools.tsv"
/* Each packaging or bootstrap operation may compile the compiler twice. */
#define SDK_RUN_SECONDS 300

static char host_path[SDK_PATH], build_path[SDK_PATH], archive_path[SDK_PATH];
static char suite[SDK_PATH], log_path[SDK_PATH];

static void join(char out[SDK_PATH], const char *base, const char *name)
{
    int size = snprintf(out, SDK_PATH, "%s/%s", base, name);
    assert(size > 0 && size < SDK_PATH);
}

static bool exists(const char *path)
{
    struct stat info;
    if (!lstat(path, &info)) {
        return true;
    }
    assert(errno == ENOENT);
    return false;
}

static int entries(const char *path)
{
    DIR *directory = opendir(path);
    if (!directory) {
        return -1;
    }
    int count = 0;
    for (struct dirent *entry; (entry = readdir(directory));) {
        count += strcmp(entry->d_name, ".") && strcmp(entry->d_name, "..");
    }
    assert(!closedir(directory));
    return count;
}

static bool same_file(const char *ours, const char *source)
{
    FILE *left = fopen(ours, "rb"), *right = fopen(source, "rb");
    bool same = left && right;
    unsigned char a[8192], b[8192];
    while (same) {
        size_t an = fread(a, 1, sizeof a, left);
        size_t bn = fread(b, 1, sizeof b, right);
        same = an == bn && !memcmp(a, b, an);
        if (!an || !bn) {
            break;
        }
    }
    same = same && !ferror(left) && !ferror(right);
    if (left) {
        assert(!fclose(left));
    }
    if (right) {
        assert(!fclose(right));
    }
    if (!same) {
        fprintf(stderr, "installed file differs: %s, source %s\n", ours, source);
    }
    return same;
}

/* Count both trees, so a missing or unexpected resource fails the comparison. */
static bool same_tree(const char *ours, const char *source)
{
    struct stat left, right;
    if (lstat(ours, &left) || lstat(source, &right)) {
        fprintf(stderr, "missing SDK resource: %s, source %s\n", ours, source);
        return false;
    }
    if (S_ISREG(left.st_mode) && S_ISREG(right.st_mode)) {
        return left.st_size == right.st_size && same_file(ours, source);
    }
    if (!S_ISDIR(left.st_mode) || !S_ISDIR(right.st_mode) ||
        entries(ours) != entries(source)) {
        fprintf(stderr, "SDK resource inventory differs: %s\n", ours);
        return false;
    }
    DIR *directory = opendir(source);
    assert(directory);
    bool same = true;
    for (struct dirent *entry; same && (entry = readdir(directory));) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) {
            continue;
        }
        char mine[SDK_PATH], theirs[SDK_PATH];
        join(mine, ours, entry->d_name);
        join(theirs, source, entry->d_name);
        same = same_tree(mine, theirs);
    }
    assert(!closedir(directory));
    return same;
}

static bool file_digest(const char *path, char digest[65])
{
    FILE *file = fopen(path, "rb");
    if (!file) {
        return false;
    }
    sha256 hash;
    sha256_begin(&hash);
    unsigned char bytes[8192];
    for (size_t count; (count = fread(bytes, 1, sizeof bytes, file));) {
        sha256_add(&hash, bytes, count);
    }
    bool read = !ferror(file);
    assert(!fclose(file));
    sha256_end(&hash, digest);
    return read;
}

static bool relative_file(const char *path)
{
    if (!*path || *path == '/' || strchr(path, '\\')) {
        return false;
    }
    while (*path) {
        size_t length = strcspn(path, "/");
        if (!length || (length == 1 && path[0] == '.') ||
            (length == 2 && path[0] == '.' && path[1] == '.')) {
            return false;
        }
        path += length;
        if (!*path) {
            return true;
        }
        path++;
    }
    return false;
}

/* Walk the actual names independently of the manifest's record order. */
static int installed_files(const char *path, const char *prefix, char **listed)
{
    struct stat info;
    if (lstat(path, &info)) {
        return -1;
    }
    if (S_ISREG(info.st_mode)) {
        const char *relative = path + strlen(prefix) + 1;
        if (!strcmp(relative, SDK_MANIFEST)) {
            return 0;
        }
        for (int i = 0; i < acount(listed); i++) {
            if (!strcmp(relative, listed[i])) {
                return 1;
            }
        }
        return -1;
    }
    if (!S_ISDIR(info.st_mode)) {
        return -1;
    }
    DIR *directory = opendir(path);
    if (!directory) {
        return -1;
    }
    int count = 0;
    for (struct dirent *entry; count >= 0 && (entry = readdir(directory));) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) {
            continue;
        }
        char child[SDK_PATH];
        join(child, path, entry->d_name);
        int below = installed_files(child, prefix, listed);
        count = below < 0 ? -1 : count + below;
    }
    assert(!closedir(directory));
    return count;
}

static bool manifest_matches(const char *prefix)
{
    char manifest[SDK_PATH];
    join(manifest, prefix, SDK_MANIFEST);
    char *text = tool_read(manifest);
    REQUIRE(text);
    struct stat info;
    bool valid = !lstat(manifest, &info) && S_ISREG(info.st_mode) &&
                 info.st_size >= 0 && (size_t)info.st_size == strlen(text) &&
                 !strncmp(text, "1\n", 2);
    char **paths = NULL;
    char *line = valid ? text + 2 : text;
    while (valid && *line) {
        char *end = strchr(line, '\n');
        if (!end) {
            valid = false;
            break;
        }
        *end = 0;
        char *expected = strchr(line, '\t');
        if (!expected) {
            valid = false;
            break;
        }
        *expected++ = 0;
        valid = relative_file(line) && strcmp(line, SDK_MANIFEST) &&
                strlen(expected) == 64 && strspn(expected, "0123456789abcdef") == 64;
        for (int i = 0; valid && i < acount(paths); i++) {
            valid = strcmp(line, paths[i]) != 0;
        }
        if (!valid) {
            break;
        }
        char path[SDK_PATH], digest[65];
        join(path, prefix, line);
        valid = !lstat(path, &info) && S_ISREG(info.st_mode) &&
                file_digest(path, digest) && !strcmp(digest, expected);
        if (valid) {
            apush(paths, line);
        }
        line = end + 1;
    }
    /* Every actual file must have a record. Unique records and equal counts
     * also reject extra aliases, including on case-insensitive filesystems. */
    valid = valid && acount(paths) > 0 &&
            installed_files(prefix, prefix, paths) == acount(paths);
    if (!valid) {
        fprintf(stderr, "installed SDK manifest does not match %s\n", prefix);
    }
    afree(paths);
    free(text);
    REQUIRE(valid);
    return true;
}

static bool copied(const char *prefix, const char *installed, const char *root, const char *source)
{
    char ours[SDK_PATH], theirs[SDK_PATH];
    join(ours, prefix, installed);
    join(theirs, root, source);
    return same_tree(ours, theirs);
}

static int install_from(const char *source_root, const char *build, const char *output)
{
    const char *argv[] = {
        host_path,
        "--root",
        source_root,
        "--source",
        CHIBI_SOURCE_ROOT "/tools/toolchain/SDK.chibi",
        "--core",
        CHIBI_SOURCE_ROOT "/packages/core",
        "--collection",
        "graph=" CHIBI_SOURCE_ROOT "/tools/graph",
        "--entry",
        "CompilerTools",
        "--timeout-ms",
        "240000",
        "--",
        "--build",
        build,
        "--output",
        output,
        "--third-party",
        archive_path,
        NULL,
    };
    return tool_run_within(argv, suite, log_path, SDK_RUN_SECONDS);
}

static int install(const char *build, const char *output)
{
    return install_from(CHIBI_SOURCE_ROOT, build, output);
}

static bool clone_file(const char *source, const char *destination)
{
#ifdef __APPLE__
    const char *argv[] = { "/bin/cp", "-cR", source, destination, NULL };
#else
    const char *argv[] = { "/bin/cp", "-R",        "--reflink=auto",
                           source,    destination, NULL };
#endif
    return !tool_run_within(argv, suite, log_path, SDK_RUN_SECONDS);
}

static bool distribution_source(char root[SDK_PATH])
{
    join(root, suite, "containment source");
    REQUIRE(!mkdir(root, 0700));
    static const char *directories[] = {
        "tools",
        "packages",
        "tools/toolchain",
        "third_party",
        "third_party/fmt",
        "third_party/yyjson",
        "third_party/ckdl",
        "docs",
    };
    for (size_t i = 0; i < sizeof directories / sizeof *directories; i++) {
        char path[SDK_PATH];
        join(path, root, directories[i]);
        REQUIRE(!mkdir(path, 0700));
    }
    static const char *resources[][2] = {
        { "share/chibi/compiler", "tools/compiler" },
        { "share/chibi/core", "packages/core" },
    };
    for (size_t i = 0; i < sizeof resources / sizeof *resources; i++) {
        char from[SDK_PATH], to[SDK_PATH];
        join(from, build_path, resources[i][0]);
        join(to, root, resources[i][1]);
        REQUIRE(clone_file(from, to));
    }
    static const char *inputs[] = {
        "tools/graph",
        "tools/toolchain/Build.chibi",
        "tools/toolchain/Graph.chibi",
        "tools/toolchain/SDK.chibi",
        "tools/configure_inputs.py",
        "LICENSE.md",
        "COPYING",
        "COPYING.LESSER",
        "COPYING.EXAMPLES",
        "docs/distribution-licensing.md",
        "third_party/fmt/LICENSE",
        "third_party/fmt/README.md",
        "third_party/yyjson/LICENSE",
        "third_party/ckdl/COPYING",
    };
    for (size_t i = 0; i < sizeof inputs / sizeof *inputs; i++) {
        char from[SDK_PATH], to[SDK_PATH];
        join(from, CHIBI_SOURCE_ROOT, inputs[i]);
        join(to, root, inputs[i]);
        REQUIRE(clone_file(from, to));
    }
    return true;
}

static bool containment_refused(const char *source, const char *output)
{
    char stage[SDK_PATH];
    int length = snprintf(stage, sizeof stage, "%s.chibi-stage", output);
    REQUIRE(length > 0 && length < (int)sizeof stage);
    REQUIRE(!exists(output) && !exists(stage));
    int status = install_from(source, build_path, output);
    char *log = tool_read(log_path);
    bool refused = status == 2 && log &&
                   strstr(log, "sdk: output or staging lies inside a distribution input");
    free(log);
    REQUIRE(refused && !exists(output) && !exists(stage));
    return true;
}

TEST_CASE(distribution_inputs_cannot_contain_output_or_staging)
{
    char source[SDK_PATH], tools[SDK_PATH], graph[SDK_PATH], sentinel[SDK_PATH];
    REQUIRE(distribution_source(source));
    join(tools, source, "tools");
    join(graph, tools, "graph");
    join(sentinel, graph, "containment-sentinel");
    tool_write(sentinel, "preserve the source input\n");
    char alias[SDK_PATH];
    join(alias, source, "graph-alias");
    REQUIRE(!symlink(graph, alias));

    /* Run this witness first. Without containment, valid inputs reach the
     * already-reserved stage and return 1 before any recursive copying. */
    char output[SDK_PATH], stage[SDK_PATH], reserved[SDK_PATH];
    join(output, graph, "reserved-output");
    join(stage, graph, "reserved-output.chibi-stage");
    REQUIRE(!mkdir(stage, 0700));
    join(reserved, stage, "sentinel");
    tool_write(reserved, "preserve the caller's stage\n");
    int graph_entries = entries(graph), tool_entries = entries(tools),
        source_entries = entries(source);
    int status = install_from(source, build_path, output);
    char *log = tool_read(log_path);
    bool refused = status == 2 && log &&
                   strstr(log, "sdk: output or staging lies inside a distribution input");
    free(log);
    REQUIRE(refused && !exists(output) && entries(stage) == 1);
    char *text = tool_read(reserved);
    bool retained = text && !strcmp(text, "preserve the caller's stage\n");
    free(text);
    REQUIRE(retained && entries(graph) == graph_entries);

    static const char *paths[] = {
        "tools/graph/direct-output",
        "tools/graph/missing/nested/output",
        "graph-alias/alias-output",
        "tools/graph/../graph/dot-output",
        "tools/never-created/../graph/dot-missing-output",
    };
    for (size_t i = 0; i < sizeof paths / sizeof *paths; i++) {
        char output[SDK_PATH];
        join(output, source, paths[i]);
        REQUIRE(containment_refused(source, output));
    }
    char missing[SDK_PATH];
    join(missing, graph, "missing");
    REQUIRE(!exists(missing));
    join(missing, tools, "never-created");
    REQUIRE(!exists(missing));

    /* The same guard covers graph-built compiler and core resources. The
     * unique child names are never created in these real build inputs. */
    static const char *resources[] = { "share/chibi/compiler", "share/chibi/core" };
    const char *unique = strrchr(suite, '/') + 1;
    for (size_t i = 0; i < sizeof resources / sizeof *resources; i++) {
        char input[SDK_PATH], output[SDK_PATH];
        join(input, build_path, resources[i]);
        int before = entries(input);
        REQUIRE(before > 0);
        join(output, input, unique);
        REQUIRE(containment_refused(source, output));
        REQUIRE(entries(input) == before);
    }

    char case_alias[SDK_PATH];
    join(case_alias, source, "TOOLS/GRAPH");
    struct stat actual, alternate;
    REQUIRE(!stat(graph, &actual));
    if (!stat(case_alias, &alternate)) {
        REQUIRE(actual.st_dev == alternate.st_dev && actual.st_ino == alternate.st_ino);
        char output[SDK_PATH];
        join(output, case_alias, "case-output");
        REQUIRE(containment_refused(source, output));
    } else {
        REQUIRE(errno == ENOENT);
        fprintf(stderr, "SDK case-alias containment requires a case-insensitive filesystem\n");
    }

    /* Only staging resolves inside the input. The pre-existing link belongs
     * to the caller and must retain its identity and target. */
    join(output, suite, "stage-alias-output");
    join(stage, suite, "stage-alias-output.chibi-stage");
    REQUIRE(!symlink(graph, stage));
    struct stat before, after;
    REQUIRE(!lstat(stage, &before) && S_ISLNK(before.st_mode));
    status = install_from(source, build_path, output);
    log = tool_read(log_path);
    refused = status == 2 && log &&
              strstr(log, "sdk: output or staging lies inside a distribution input");
    free(log);
    REQUIRE(refused && !exists(output));
    REQUIRE(!lstat(stage, &after) && S_ISLNK(after.st_mode));
    REQUIRE(before.st_dev == after.st_dev && before.st_ino == after.st_ino);
    char target[SDK_PATH];
    ssize_t count = readlink(stage, target, sizeof target);
    REQUIRE(count == (ssize_t)strlen(graph) && !memcmp(target, graph, (size_t)count));

    /* A later '..' cannot turn a file or unresolved link into a directory. */
    char ancestors[SDK_PATH], regular[SDK_PATH], dangling[SDK_PATH];
    join(ancestors, suite, "invalid ancestors");
    REQUIRE(!mkdir(ancestors, 0700));
    join(regular, ancestors, "file");
    join(dangling, ancestors, "dangling");
    tool_write(regular, "preserve the regular file\n");
    REQUIRE(!symlink("missing-target", dangling));
    struct stat file_before, link_before, file_after, link_after;
    REQUIRE(!lstat(regular, &file_before) && S_ISREG(file_before.st_mode));
    REQUIRE(!lstat(dangling, &link_before) && S_ISLNK(link_before.st_mode));
    static const char *invalid[][2] = {
        { "file/../outside-output", "outside-output" },
        { "dangling/../other-output", "other-output" },
    };
    for (size_t i = 0; i < sizeof invalid / sizeof *invalid; i++) {
        char outside[SDK_PATH];
        join(output, ancestors, invalid[i][0]);
        join(outside, ancestors, invalid[i][1]);
        int length = snprintf(stage, sizeof stage, "%s.chibi-stage", outside);
        REQUIRE(length > 0 && length < (int)sizeof stage);
        REQUIRE(!exists(outside) && !exists(stage));
        status = install_from(source, build_path, output);
        log = tool_read(log_path);
        bool invalid_ancestor = status == 2 && log &&
                                strstr(log, "sdk: output has an invalid directory ancestor");
        free(log);
        REQUIRE(invalid_ancestor && !exists(outside) && !exists(stage));
    }
    REQUIRE(!lstat(regular, &file_after) && S_ISREG(file_after.st_mode));
    REQUIRE(!lstat(dangling, &link_after) && S_ISLNK(link_after.st_mode));
    REQUIRE(
        file_before.st_dev == file_after.st_dev &&
        file_before.st_ino == file_after.st_ino
    );
    REQUIRE(
        link_before.st_dev == link_after.st_dev &&
        link_before.st_ino == link_after.st_ino
    );
    text = tool_read(regular);
    retained = text && !strcmp(text, "preserve the regular file\n");
    free(text);
    count = readlink(dangling, target, sizeof target);
    REQUIRE(
        retained && count == (ssize_t)strlen("missing-target") &&
        !memcmp(target, "missing-target", (size_t)count)
    );
    REQUIRE(entries(ancestors) == 2);

    /* Sharing a directory-name prefix does not make a sibling an input. */
    join(output, tools, "graph-extra/output");
    join(stage, tools, "graph-extra/output.chibi-stage");
    /* An empty build makes this control stop at resource validation. */
    char missing_build[SDK_PATH];
    join(missing_build, suite, "containment missing build");
    REQUIRE(!mkdir(missing_build, 0700));
    status = install_from(source, missing_build, output);
    log = tool_read(log_path);
    bool checked_resources = status == 1 && log &&
                             strstr(log, "sdk: graph compiler/core resources are missing or stale") &&
                             !strstr(log, "sdk: output or staging lies inside a distribution input");
    free(log);
    REQUIRE(checked_resources && !exists(output) && !exists(stage));
    REQUIRE(entries(missing_build) == 0);
    join(missing, tools, "graph-extra");
    REQUIRE(!exists(missing));

    text = tool_read(sentinel);
    retained = text && !strcmp(text, "preserve the source input\n");
    free(text);
    REQUIRE(
        retained && entries(graph) == graph_entries &&
        entries(tools) == tool_entries && entries(source) == source_entries
    );
    return true;
}

TEST_CASE(missing_graph_outputs_fail_before_creating_staging)
{
    char empty[SDK_PATH], output[SDK_PATH], stage[SDK_PATH];
    join(empty, suite, "missing build");
    join(output, suite, "missing output");
    join(stage, suite, "missing output.chibi-stage");
    REQUIRE(!mkdir(empty, 0700));
    REQUIRE(install(empty, output) == 1);
    REQUIRE(!exists(output) && !exists(stage));
    REQUIRE(entries(empty) == 0);
    return true;
}

TEST_CASE(existing_prefix_is_never_overwritten)
{
    char output[SDK_PATH], stage[SDK_PATH], sentinel[SDK_PATH];
    join(output, suite, "existing output");
    join(stage, suite, "existing output.chibi-stage");
    join(sentinel, output, "sentinel");
    REQUIRE(!mkdir(output, 0700));
    REQUIRE(install(build_path, output) == 2);
    REQUIRE(entries(output) == 0 && !exists(stage));
    tool_write(sentinel, "retain the existing prefix\n");
    REQUIRE(install(build_path, output) == 2);
    char *text = tool_read(sentinel);
    bool retained = text && !strcmp(text, "retain the existing prefix\n");
    free(text);
    REQUIRE(retained && entries(output) == 1 && !exists(stage));
    return true;
}

TEST_CASE(existing_staging_directory_is_never_claimed)
{
    char output[SDK_PATH], stage[SDK_PATH], sentinel[SDK_PATH];
    join(output, suite, "reserved output");
    join(stage, suite, "reserved output.chibi-stage");
    join(sentinel, stage, "sentinel");
    REQUIRE(!mkdir(stage, 0700));
    tool_write(sentinel, "retain someone else's staging directory\n");
    REQUIRE(install(build_path, output) == 1);
    char *text = tool_read(sentinel);
    bool retained = text && !strcmp(text, "retain someone else's staging directory\n");
    free(text);
    REQUIRE(retained && entries(stage) == 1 && !exists(output));
    return true;
}

static bool clone_graph(char clone[SDK_PATH], const char *name)
{
    char path[SDK_PATH];
    join(clone, suite, name);
    REQUIRE(!mkdir(clone, 0700));
    static const char *directories[] = { "sdk", "share", "share/chibi", "share/chibi/tools" };
    for (size_t i = 0; i < sizeof directories / sizeof *directories; i++) {
        join(path, clone, directories[i]);
        REQUIRE(!mkdir(path, 0700));
    }
    static const char *inputs[] = {
        "sdk/chibi-build-host",         "sdk/chibi-build-host.chibi-record",
        "sdk/chibi-compiler-host",      "sdk/chibi-compiler-host.chibi-record",
        "share/chibi/compiler",         "share/chibi/core",
        "share/chibi/tools/native_aot",
    };
    for (size_t i = 0; i < sizeof inputs / sizeof *inputs; i++) {
        char source[SDK_PATH];
        join(source, build_path, inputs[i]);
        join(path, clone, inputs[i]);
        REQUIRE(clone_file(source, path));
    }
    return true;
}

TEST_CASE(failed_staged_compiler_validation_removes_owned_staging)
{
    char clone[SDK_PATH], output[SDK_PATH], stage[SDK_PATH], path[SDK_PATH];
    REQUIRE(clone_graph(clone, "corrupt build"));
    join(output, suite, "corrupt output");
    join(stage, suite, "corrupt output.chibi-stage");
    /* A build host starts successfully but rejects the compiler's --bootstrap
     * arguments. Prepare has copied its files before this validation fails. */
    join(path, clone, "sdk/chibi-compiler-host");
    REQUIRE(!unlink(path));
    char wrong_host[SDK_PATH];
    join(wrong_host, build_path, "sdk/chibi-build-host");
    REQUIRE(clone_file(wrong_host, path));
    REQUIRE(install(clone, output) == 1);
    char *log = tool_read(log_path);
    bool validated = log &&
                     strstr(log, "sdk: staged compiler resources failed recovery");
    free(log);
    REQUIRE(validated && !exists(output) && !exists(stage));
    return true;
}

TEST_CASE(failed_staged_build_host_verification_removes_owned_staging)
{
    char clone[SDK_PATH], output[SDK_PATH], stage[SDK_PATH], path[SDK_PATH];
    REQUIRE(clone_graph(clone, "unsupported build host"));
    join(output, suite, "unsupported output");
    join(stage, suite, "unsupported output.chibi-stage");
    /* Compiler recovery still succeeds. A different valid C executable cannot
     * compile and execute the installed SDK's capability verification entry. */
    join(path, clone, "sdk/chibi-build-host");
    REQUIRE(!unlink(path));
    char wrong_host[SDK_PATH];
    join(wrong_host, build_path, "sdk/chibi-compiler-host");
    REQUIRE(clone_file(wrong_host, path));
    REQUIRE(install(clone, output) == 1);
    char *log = tool_read(log_path);
    bool verified = log &&
                    strstr(log, "sdk: staged build host failed capability verification");
    free(log);
    REQUIRE(verified && !exists(output) && !exists(stage));
    REQUIRE(same_file(path, wrong_host));
    return true;
}

/* The supplied archive still occurs as a substring of every changed token.
 * Only equality with the whole argument distinguishes these wrong paths. */
static bool prefix_archive_paths(const char *path)
{
    char *record = tool_read(path);
    REQUIRE(record);
    static const char prefix[] = "/other";
    size_t archive_length = strlen(archive_path), count = 0;
    for (const char *at = record; (at = strstr(at, archive_path)); at += archive_length) {
        count++;
    }
    if (!count) {
        free(record);
        return false;
    }
    char *changed = xmalloc(strlen(record) + count * (sizeof prefix - 1) + 1);
    char *out = changed;
    const char *at = record, *match;
    while ((match = strstr(at, archive_path))) {
        size_t bytes = (size_t)(match - at);
        memcpy(out, at, bytes);
        out += bytes;
        memcpy(out, prefix, sizeof prefix - 1);
        out += sizeof prefix - 1;
        memcpy(out, match, archive_length);
        out += archive_length;
        at = match + archive_length;
    }
    strcpy(out, at);
    tool_write(path, changed);
    free(changed);
    free(record);
    return true;
}

TEST_CASE(dependency_link_records_require_whole_archive_paths)
{
    char clone[SDK_PATH], output[SDK_PATH], stage[SDK_PATH];
    REQUIRE(clone_graph(clone, "wrong dependency records"));
    static const char *records[] = {
        "sdk/chibi-build-host.chibi-record",
        "sdk/chibi-compiler-host.chibi-record",
    };
    for (size_t i = 0; i < sizeof records / sizeof *records; i++) {
        char path[SDK_PATH];
        join(path, clone, records[i]);
        REQUIRE(prefix_archive_paths(path));
    }
    join(output, suite, "wrong dependency output");
    join(stage, suite, "wrong dependency output.chibi-stage");
    REQUIRE(install(clone, output) == 1);
    char *log = tool_read(log_path);
    bool refused = log &&
                   strstr(log, "sdk: dependency archive differs from the graph executable link record");
    free(log);
    REQUIRE(refused && !exists(output) && !exists(stage));
    return true;
}

/* Validate the source receipts against installed bytes after the prefix moves. */
static bool receipts_match(const char *prefix)
{
    char path[SDK_PATH], compiler[SDK_PATH], core[SDK_PATH];
    join(compiler, prefix, "share/chibi/compiler");
    join(core, prefix, "share/chibi/core");
    join(path, compiler, "bootstrap/bootstrap.json");
    yyjson_doc *doc = yyjson_read_file(path, 0, NULL, NULL);
    REQUIRE(doc);
    yyjson_val *root = yyjson_doc_get_root(doc);
    const char *recorded_core = yyjson_get_str(yyjson_obj_get(root, "core"));
    yyjson_val *sources = yyjson_obj_get(root, "sources");
    bool same = recorded_core && yyjson_arr_size(sources) > 0;
    bool has_compiler = false, has_core = false;
    size_t core_length = recorded_core ? strlen(recorded_core) : 0;
    for (size_t i = 0; same && i < yyjson_arr_size(sources); i++) {
        yyjson_val *source = yyjson_arr_get(sources, i);
        const char *name = yyjson_get_str(yyjson_obj_get(source, "path"));
        const char *expected = yyjson_get_str(yyjson_obj_get(source, "sha256"));
        same = name && expected;
        if (!same) {
            break;
        }
        bool in_core = !strncmp(name, recorded_core, core_length) &&
                       name[core_length] == '/';
        has_core |= in_core;
        has_compiler |= !in_core;
        join(path, in_core ? core : compiler, in_core ? name + core_length + 1 : name);
        char digest[65];
        same = file_digest(path, digest) && !strcmp(digest, expected);
        if (!same) {
            fprintf(stderr, "installed compiler source differs from its receipt: %s\n", path);
        }
    }
    yyjson_doc_free(doc);
    REQUIRE(same && has_compiler && has_core);
    return true;
}

static int run_without_toolchain(const char *const *argv, const char *cwd)
{
    const char *names[] = { "PATH", "CC", "CXX" };
    const char *values[] = { "", "/usr/bin/false", "/usr/bin/false" };
    char *saved[3];
    for (int i = 0; i < 3; i++) {
        const char *value = getenv(names[i]);
        saved[i] = value ? strdup(value) : NULL;
        assert(!value || saved[i]);
        assert(!setenv(names[i], values[i], 1));
    }
    int status = tool_run_within(argv, cwd, log_path, SDK_RUN_SECONDS);
    for (int i = 0; i < 3; i++) {
        assert(saved[i] ? !setenv(names[i], saved[i], 1) : !unsetenv(names[i]));
        free(saved[i]);
    }
    return status;
}

TEST_CASE(compiler_tools_package_and_run_after_relocation)
{
    char original[SDK_PATH], relocated[SDK_PATH], stage[SDK_PATH];
    join(original, suite, "compiler tools");
    join(relocated, suite, "relocated compiler tools");
    join(stage, suite, "compiler tools.chibi-stage");
    REQUIRE(!install(build_path, original));
    REQUIRE(exists(original) && !exists(stage));
    static const char *hosts[] = { "chibi-build-host", "chibi-compiler-host" };
    for (size_t i = 0; i < sizeof hosts / sizeof *hosts; i++) {
        char installed[SDK_PATH], built[SDK_PATH], executable[SDK_PATH];
        join(installed, "libexec/chibi", hosts[i]);
        join(built, "sdk", hosts[i]);
        REQUIRE(copied(original, installed, build_path, built));
        join(executable, original, installed);
        REQUIRE(!access(executable, X_OK));
    }
    REQUIRE(copied(original, "share/chibi/compiler", build_path, "share/chibi/compiler"));
    REQUIRE(copied(original, "share/chibi/core", build_path, "share/chibi/core"));
    REQUIRE(
        copied(original, "share/chibi/tools/native_aot", build_path, "share/chibi/tools/native_aot")
    );
    REQUIRE(copied(original, "share/chibi/graph", CHIBI_SOURCE_ROOT, "tools/graph"));
    static const char *programs[] = { "Build.chibi", "Graph.chibi", "SDK.chibi" };
    for (size_t i = 0; i < sizeof programs / sizeof *programs; i++) {
        char installed[SDK_PATH], source[SDK_PATH];
        join(installed, "share/chibi/tools/toolchain", programs[i]);
        join(source, "tools/toolchain", programs[i]);
        REQUIRE(copied(original, installed, CHIBI_SOURCE_ROOT, source));
    }
    REQUIRE(
        copied(original, "share/chibi/tools/configure_inputs.py", CHIBI_SOURCE_ROOT, "tools/configure_inputs.py")
    );
    static const char *licenses[] = {
        "LICENSE.md",
        "COPYING",
        "COPYING.LESSER",
        "COPYING.EXAMPLES",
        "docs/distribution-licensing.md",
    };
    for (size_t i = 0; i < sizeof licenses / sizeof *licenses; i++) {
        char installed[SDK_PATH];
        join(installed, "share/chibi/licenses/chibi", licenses[i]);
        REQUIRE(copied(original, installed, CHIBI_SOURCE_ROOT, licenses[i]));
    }
    static const char *dependencies[] = {
        "fmt/LICENSE",
        "fmt/README.md",
        "yyjson/LICENSE",
        "ckdl/COPYING",
    };
    for (size_t i = 0; i < sizeof dependencies / sizeof *dependencies; i++) {
        char installed[SDK_PATH], source[SDK_PATH];
        join(installed, "share/chibi/licenses", dependencies[i]);
        join(source, "third_party", dependencies[i]);
        REQUIRE(copied(original, installed, CHIBI_SOURCE_ROOT, source));
    }
    static const char *vendor[] = { "tree_sitter/LICENSE", "sljit/LICENSE" };
    for (size_t i = 0; i < sizeof vendor / sizeof *vendor; i++) {
        char installed[SDK_PATH], source[SDK_PATH];
        join(installed, "share/chibi/licenses/vendor", vendor[i]);
        join(source, "share/chibi/licenses/vendor", vendor[i]);
        REQUIRE(copied(original, installed, archive_path, source));
    }
    REQUIRE(!rename(original, relocated));
    REQUIRE(!exists(original) && receipts_match(relocated));
    REQUIRE(manifest_matches(relocated));

    char consumer[SDK_PATH], program[SDK_PATH], host[SDK_PATH], core[SDK_PATH];
    join(consumer, suite, "unrelated consumer");
    REQUIRE(!mkdir(consumer, 0700));
    join(program, consumer, "Consumer.chibi");
    tool_write(
        program,
        "package consumer\nimport strings \"core:strings\"\n"
        "Build :: () -> int {\n"
        "    value := strings.Join([\"installed\", \"compiler\"], \" \");\n"
        "    if (value != \"installed compiler\") { return 1; }\n"
        "    if (!build.write_file(\"result.txt\", value)) { return 2; }\n"
        "    return 0;\n}\n"
    );
    join(host, relocated, "libexec/chibi/chibi-build-host");
    join(core, relocated, "share/chibi/core");
    const char *run[] = {
        host, "--root",  consumer, "--source",     "Consumer.chibi", "--core",
        core, "--entry", "Build",  "--timeout-ms", "120000",         NULL,
    };
    REQUIRE(!run_without_toolchain(run, consumer));
    join(program, consumer, "result.txt");
    char *result = tool_read(program);
    bool executed = result && !strcmp(result, "installed compiler");
    free(result);
    REQUIRE(executed);

    char compiler[SDK_PATH], bootstrap[SDK_PATH];
    join(host, relocated, "libexec/chibi/chibi-compiler-host");
    join(compiler, relocated, "share/chibi/compiler");
    join(bootstrap, compiler, "bootstrap");
    const char *check[] = {
        host,     "--bootstrap", bootstrap, "--compiler",
        compiler, "--core",      core,      NULL,
    };
    REQUIRE(!run_without_toolchain(check, consumer));
    REQUIRE(receipts_match(relocated));
    REQUIRE(
        copied(relocated, "share/chibi/compiler/bootstrap/compiler.chibip", build_path, "share/chibi/compiler/bootstrap/compiler.chibip")
    );

    char graph[SDK_PATH], collection[SDK_PATH], graph_build[SDK_PATH];
    join(host, relocated, "libexec/chibi/chibi-build-host");
    join(graph, relocated, "share/chibi/tools/toolchain/Graph.chibi");
    int size = snprintf(collection, sizeof collection, "graph=%s/share/chibi/graph", relocated);
    REQUIRE(size > 0 && size < (int)sizeof collection);
    join(graph_build, suite, "installed graph commands");
    const char *commands[] = {
        host,
        "--root",
        CHIBI_SOURCE_ROOT,
        "--source",
        graph,
        "--core",
        core,
        "--collection",
        collection,
        "--entry",
        "Libraries",
        "--timeout-ms",
        "120000",
        "--",
        "--build",
        graph_build,
        "--target",
        "chibi_support",
        "--commands-only",
        "on",
        "--profile",
        "explicit",
        "--compiler",
        "/usr/bin/false",
        "--cxx-compiler",
        "/usr/bin/false",
        "--compiler-id",
        "Clang",
        "--build-type",
        "Release",
        "--warnings",
        "off",
        "--architecture",
        "",
        "--sysroot",
        "",
        "--sanitizers",
        "",
        "--debug-compilation-dir",
        "",
        "--launcher",
        "",
        "--archiver",
        "/usr/bin/false",
        "--ranlib",
        "/usr/bin/false",
        "--third-party",
        archive_path,
        "--libclang-include",
        "",
        "--libclang-library",
        "",
        NULL,
    };
    REQUIRE(!run_without_toolchain(commands, consumer));
    join(program, graph_build, "compile_commands.json");
    yyjson_doc *doc = yyjson_read_file(program, 0, NULL, NULL);
    REQUIRE(doc);
    bool listed = yyjson_arr_size(yyjson_doc_get_root(doc)) == 5;
    yyjson_doc_free(doc);
    REQUIRE(listed);
    /* An explicit profile must reject an omitted archiver even when PATH
     * contains a usable ar. No machine default may enter this profile. */
    char tools[SDK_PATH], fake_ar[SDK_PATH], missing_build[SDK_PATH];
    join(tools, suite, "discovery tools");
    REQUIRE(!mkdir(tools, 0700));
    join(fake_ar, tools, "ar");
    REQUIRE(!symlink("/usr/bin/false", fake_ar));
    join(missing_build, suite, "missing explicit archiver");
    const char *omitted[sizeof commands / sizeof *commands];
    int count = 0;
    for (int i = 0; commands[i]; i++) {
        if (!strcmp(commands[i], "--archiver") || !strcmp(commands[i], "--launcher")) {
            i++;
            continue;
        }
        omitted[count++] = !strcmp(commands[i], graph_build) ? missing_build
                                                             : commands[i];
    }
    omitted[count] = NULL;
    const char *search = getenv("PATH");
    char *saved_search = search ? strdup(search) : NULL;
    assert(!search || saved_search);
    assert(!setenv("PATH", tools, 1));
    int status = tool_run_within(omitted, consumer, log_path, SDK_RUN_SECONDS);
    assert(saved_search ? !setenv("PATH", saved_search, 1) : !unsetenv("PATH"));
    free(saved_search);
    REQUIRE(status == 2 && !exists(missing_build));
    return true;
}

static void resolve(char out[SDK_PATH], const char *path)
{
    char *absolute = realpath(path, NULL);
    assert(absolute && strlen(absolute) < SDK_PATH);
    strcpy(out, absolute);
    free(absolute);
}

static void resolve_archive(const char *path)
{
    if (*path) {
        resolve(archive_path, path);
        return;
    }
    char record_path[SDK_PATH];
    join(record_path, build_path, "sdk/chibi-build-host.chibi-record");
    char *record = tool_read(record_path);
    assert(record);
    static const char
        suffix[] = "/lib/chibi-vendor/libchibi_vendor_tree_sitter.a";
    size_t suffix_length = sizeof suffix - 1;
    bool found = false;
    for (char *line = record; *line;) {
        char *end = strchr(line, '\n');
        size_t length = end ? (size_t)(end - line) : strlen(line);
        if (length > suffix_length && line[0] == '/' &&
            !memcmp(line + length - suffix_length, suffix, suffix_length)) {
            size_t prefix = length - suffix_length;
            assert(prefix < sizeof archive_path);
            memcpy(archive_path, line, prefix);
            archive_path[prefix] = 0;
            found = true;
            break;
        }
        if (!end) {
            break;
        }
        line = end + 1;
    }
    free(record);
    assert(found);
    char absolute[SDK_PATH];
    resolve(absolute, archive_path);
    strcpy(archive_path, absolute);
}

void test_build_sdk(const char *host, const char *build, const char *archive)
{
    resolve(host_path, host);
    resolve(build_path, build);
    resolve_archive(archive);
    strcpy(suite, "/tmp/chibi-sdk-XXXXXX");
    assert(mkdtemp(suite));
    char absolute[SDK_PATH];
    resolve(absolute, suite);
    strcpy(suite, absolute);
    join(log_path, suite, "sdk.log");
    RUN_TEST_CASE(distribution_inputs_cannot_contain_output_or_staging);
    RUN_TEST_CASE(missing_graph_outputs_fail_before_creating_staging);
    RUN_TEST_CASE(existing_prefix_is_never_overwritten);
    RUN_TEST_CASE(existing_staging_directory_is_never_claimed);
    RUN_TEST_CASE(failed_staged_compiler_validation_removes_owned_staging);
    RUN_TEST_CASE(failed_staged_build_host_verification_removes_owned_staging);
    RUN_TEST_CASE(dependency_link_records_require_whole_archive_paths);
    RUN_TEST_CASE(compiler_tools_package_and_run_after_relocation);
    tool_remove_tree(suite);
}
#endif
