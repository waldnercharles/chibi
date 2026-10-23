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
#include <sys/utsname.h>
#include <unistd.h>

#define SDK_PATH 4096
#define SDK_MANIFEST "share/chibi/compiler-tools.tsv"
/* Each packaging or bootstrap operation may compile the compiler twice. */
#define SDK_RUN_SECONDS 120
/* ProjectTools exports several prepared entries and checks their execution. */
#define PROJECT_RUN_SECONDS 300
#define PROJECT_SOURCE "share/chibi/tools/toolchain/Project.chibi"
#define PROJECT_ARTIFACT "share/chibi/tools/toolchain/Project.chibip"
#define PROJECT_WITNESS "share/chibi/project-host.tsv"
#define PROJECT_RECEIPT "share/chibi/project-prepare.tsv"

static char host_path[SDK_PATH], build_path[SDK_PATH], archive_path[SDK_PATH];
static char suite[SDK_PATH], log_path[SDK_PATH];
static char project_source[SDK_PATH], project_assembly[SDK_PATH];

static const char *native_build_files[] = {
    "asset_compiler/bootstrap/native64/asset_compiler_blob.inc",
    "asset_compiler/bootstrap/native64/asset_compiler_packaged.c",
    "asset_compiler/bootstrap/wasm32/asset_compiler_blob.inc",
    "asset_compiler/bootstrap/wasm32/asset_compiler_packaged.c",
};

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

/* Native build leaves may remain in a warm cache, but every other regular
 * file belongs to the semantic payload. Reject directory aliases. */
static int semantic_files(const char *path, const char *relative, const char *other)
{
    for (size_t i = 0; i < sizeof native_build_files / sizeof *native_build_files; i++) {
        if (!strcmp(relative, native_build_files[i])) {
            return 0;
        }
    }
    struct stat info;
    if (lstat(path, &info)) {
        return -1;
    }
    if (S_ISREG(info.st_mode)) {
        char peer[SDK_PATH];
        join(peer, other, relative);
        struct stat peer_info;
        return !lstat(peer, &peer_info) && S_ISREG(peer_info.st_mode) &&
                       info.st_size == peer_info.st_size && same_file(path, peer)
                 ? 1
                 : -1;
    }
    if (!S_ISDIR(info.st_mode)) {
        return -1;
    }
    DIR *directory = opendir(path);
    assert(directory);
    int count = 0;
    for (struct dirent *entry; count >= 0 && (entry = readdir(directory));) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) {
            continue;
        }
        char child[SDK_PATH], name[SDK_PATH];
        join(child, path, entry->d_name);
        if (*relative) {
            join(name, relative, entry->d_name);
        } else {
            strcpy(name, entry->d_name);
        }
        int below = semantic_files(child, name, other);
        count = below < 0 ? -1 : count + below;
    }
    assert(!closedir(directory));
    return count;
}

static bool compiler_payload_matches(const char *prefix, const char *build)
{
    char ours[SDK_PATH], source[SDK_PATH];
    join(ours, prefix, "share/chibi/compiler");
    join(source, build, "share/chibi/compiler");
    int expected = semantic_files(source, "", ours);
    REQUIRE(expected > 0 && semantic_files(ours, "", source) == expected);
    for (size_t i = 0; i < sizeof native_build_files / sizeof *native_build_files; i++) {
        char path[SDK_PATH];
        join(path, ours, native_build_files[i]);
        REQUIRE(!exists(path));
    }
    return true;
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
        "--entry",
        "CompilerTools",
        "--timeout-ms",
        "120000",
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

static bool clone_graph(char clone[SDK_PATH], const char *name)
{
    char path[SDK_PATH];
    join(clone, suite, name);
    REQUIRE(!mkdir(clone, 0700));
    static const char *directories[] = { "sdk", "share", "share/chibi" };
    for (size_t i = 0; i < sizeof directories / sizeof *directories; i++) {
        join(path, clone, directories[i]);
        REQUIRE(!mkdir(path, 0700));
    }
    static const char *inputs[] = {
        "sdk/chibi-build-host",    "sdk/chibi-build-host.chibi-record",
        "sdk/chibi-compiler-host", "sdk/chibi-compiler-host.chibi-record",
        "share/chibi/compiler",    "share/chibi/core",
    };
    for (size_t i = 0; i < sizeof inputs / sizeof *inputs; i++) {
        char source[SDK_PATH];
        join(source, build_path, inputs[i]);
        join(path, clone, inputs[i]);
        REQUIRE(clone_file(source, path));
    }
    return true;
}

static bool distribution_source(char root[SDK_PATH])
{
    join(root, suite, "containment source");
    REQUIRE(!mkdir(root, 0700));
    static const char *directories[] = {
        "tools",       "packages",        "tools/toolchain",
        "third_party", "third_party/fmt", "third_party/yyjson",
        "docs",
    };
    for (size_t i = 0; i < sizeof directories / sizeof *directories; i++) {
        char path[SDK_PATH];
        join(path, root, directories[i]);
        REQUIRE(!mkdir(path, 0700));
    }
    char from[SDK_PATH], to[SDK_PATH];
    join(from, CHIBI_SOURCE_ROOT, "tools/compiler");
    join(to, root, "tools/compiler");
    REQUIRE(clone_file(from, to));
    join(from, build_path, "share/chibi/core");
    join(to, root, "packages/core");
    REQUIRE(clone_file(from, to));
    static const char *inputs[] = {
        "tools/graph",
        "tools/toolchain/Build.chibi",
        "tools/toolchain/Graph.chibi",
        "tools/toolchain/SDK.chibi",
        "tools/toolchain/StageOne.chibi",
        "tools/configure_inputs.py",
        "LICENSE.md",
        "COPYING",
        "COPYING.LESSER",
        "COPYING.EXAMPLES",
        "docs/distribution-licensing.md",
        "third_party/fmt/LICENSE",
        "third_party/fmt/README.md",
        "third_party/yyjson/LICENSE",
    };
    for (size_t i = 0; i < sizeof inputs / sizeof *inputs; i++) {
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

static bool stale_payload_refused(const char *build, const char *name)
{
    char output[SDK_PATH], stage[SDK_PATH];
    join(output, suite, name);
    int size = snprintf(stage, sizeof stage, "%s.chibi-stage", output);
    REQUIRE(size > 0 && size < (int)sizeof stage);
    REQUIRE(install(build, output) == 1);
    char *log = tool_read(log_path);
    bool stale = log &&
                 strstr(log, "sdk: graph compiler/core resources are missing or stale");
    free(log);
    REQUIRE(stale && !exists(output) && !exists(stage));
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

    char clone[SDK_PATH], path[SDK_PATH], backup[SDK_PATH];
    REQUIRE(clone_graph(clone, "stale semantic compiler"));
    join(path, clone, "share/chibi/compiler/package_compiler/Compile.chibi");
    join(backup, suite, "compiler source backup");
    REQUIRE(!rename(path, backup));
    REQUIRE(stale_payload_refused(clone, "missing semantic output"));
    tool_write(path, "package changed_sdk_test\n");
    REQUIRE(stale_payload_refused(clone, "changed semantic output"));
    REQUIRE(!unlink(path) && !rename(backup, path));

    /* Only the four declared leaves are inert, including among C inputs. */
    join(path, clone, "share/chibi/compiler/asset_compiler/bootstrap/native64/unrelated-native-input.c");
    tool_write(path, "int unrelated_native_input;\n");
    REQUIRE(stale_payload_refused(clone, "unrelated semantic output"));
    REQUIRE(!unlink(path));

    char source[SDK_PATH], snapshot[SDK_PATH], target[SDK_PATH], raw[SDK_PATH];
    join(raw, CHIBI_SOURCE_ROOT, "tools/compiler");
    join(source, suite, "protected compiler source");
    REQUIRE(clone_file(raw, source));
    join(snapshot, suite, "raw compiler witness");
    REQUIRE(clone_file(source, snapshot));
    join(target, source, "asset_compiler/bootstrap");
    struct stat before, after, root_before, root_after, link_before, link_after;
    REQUIRE(!lstat(source, &root_before) && S_ISDIR(root_before.st_mode));
    REQUIRE(!lstat(target, &before) && S_ISDIR(before.st_mode));
    join(path, clone, "share/chibi/compiler/asset_compiler/bootstrap");
    tool_remove_tree(path);
    REQUIRE(!symlink(target, path) && !lstat(path, &link_before));
    REQUIRE(stale_payload_refused(clone, "parent alias output"));
    char link[SDK_PATH];
    ssize_t length = readlink(path, link, sizeof link - 1);
    REQUIRE(length > 0);
    link[length] = 0;
    REQUIRE(
        !strcmp(link, target) && !lstat(path, &link_after) &&
        S_ISLNK(link_after.st_mode) && link_before.st_dev == link_after.st_dev &&
        link_before.st_ino == link_after.st_ino && !lstat(target, &after) &&
        before.st_dev == after.st_dev && before.st_ino == after.st_ino &&
        !lstat(source, &root_after) && root_before.st_dev == root_after.st_dev &&
        root_before.st_ino == root_after.st_ino && same_tree(source, snapshot)
    );
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

static bool excluded_alias_is_inert(const char *build)
{
    char leaf[SDK_PATH], target[SDK_PATH], output[SDK_PATH], stage[SDK_PATH];
    char compiler[SDK_PATH];
    join(compiler, build, "share/chibi/compiler");
    join(leaf, compiler, native_build_files[0]);
    REQUIRE(!unlink(leaf));
    join(target, suite, "excluded leaf target");
    tool_write(target, "retain the excluded leaf target\n");
    char digest[65], after_digest[65];
    struct stat target_before, target_after, link_before, link_after;
    REQUIRE(file_digest(target, digest) && !lstat(target, &target_before));
    REQUIRE(!symlink(target, leaf) && !lstat(leaf, &link_before));
    join(output, suite, "excluded leaf alias output");
    join(stage, suite, "excluded leaf alias output.chibi-stage");
    REQUIRE(!install(build, output) && !exists(stage));
    REQUIRE(compiler_payload_matches(output, build) && manifest_matches(output));
    char link[SDK_PATH];
    ssize_t length = readlink(leaf, link, sizeof link - 1);
    REQUIRE(length > 0);
    link[length] = 0;
    REQUIRE(
        !strcmp(link, target) && !lstat(leaf, &link_after) &&
        S_ISLNK(link_after.st_mode) && link_before.st_dev == link_after.st_dev &&
        link_before.st_ino == link_after.st_ino && !lstat(target, &target_after) &&
        target_before.st_dev == target_after.st_dev &&
        target_before.st_ino == target_after.st_ino &&
        file_digest(target, after_digest) && !strcmp(digest, after_digest)
    );
    return true;
}

TEST_CASE(compiler_tools_package_and_run_after_relocation)
{
    char original[SDK_PATH], relocated[SDK_PATH], stage[SDK_PATH], warm[SDK_PATH];
    REQUIRE(clone_graph(warm, "warm compiler build"));
    char warm_compiler[SDK_PATH];
    join(warm_compiler, warm, "share/chibi/compiler");
    char legacy_hashes[sizeof native_build_files / sizeof *native_build_files][65];
    for (size_t i = 0; i < sizeof native_build_files / sizeof *native_build_files; i++) {
        char path[SDK_PATH], text[128];
        join(path, warm_compiler, native_build_files[i]);
        if (exists(path)) {
            REQUIRE(!unlink(path));
        }
        int size = snprintf(text, sizeof text, "inert warm native build input %zu\n", i);
        REQUIRE(size > 0 && size < (int)sizeof text);
        tool_write(path, text);
        REQUIRE(file_digest(path, legacy_hashes[i]));
    }
    join(original, suite, "compiler tools");
    join(relocated, suite, "relocated compiler tools");
    join(stage, suite, "compiler tools.chibi-stage");
    REQUIRE(!install(warm, original));
    REQUIRE(exists(original) && !exists(stage));
    for (size_t i = 0; i < sizeof native_build_files / sizeof *native_build_files; i++) {
        char path[SDK_PATH], digest[65];
        struct stat info;
        join(path, warm_compiler, native_build_files[i]);
        REQUIRE(
            !lstat(path, &info) && S_ISREG(info.st_mode) &&
            file_digest(path, digest) && !strcmp(digest, legacy_hashes[i])
        );
    }
    static const char *hosts[] = { "chibi-build-host", "chibi-compiler-host" };
    for (size_t i = 0; i < sizeof hosts / sizeof *hosts; i++) {
        char installed[SDK_PATH], built[SDK_PATH], executable[SDK_PATH];
        join(installed, "libexec/chibi", hosts[i]);
        join(built, "sdk", hosts[i]);
        REQUIRE(copied(original, installed, build_path, built));
        join(executable, original, installed);
        REQUIRE(!access(executable, X_OK));
    }
    REQUIRE(compiler_payload_matches(original, warm));
    REQUIRE(copied(original, "share/chibi/core", build_path, "share/chibi/core"));
    REQUIRE(copied(original, "share/chibi/graph", CHIBI_SOURCE_ROOT, "tools/graph"));
    static const char *programs[] = { "Build.chibi", "Graph.chibi", "SDK.chibi", "StageOne.chibi" };
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
    REQUIRE(compiler_payload_matches(relocated, warm));
    REQUIRE(excluded_alias_is_inert(warm));

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
    yyjson_val *commands_root = yyjson_doc_get_root(doc);
    bool listed = yyjson_arr_size(commands_root) > 0, file_io = false;
    size_t command_index, command_count;
    yyjson_val *command;
    yyjson_arr_foreach(commands_root, command_index, command_count, command)
    {
        const char *directory = yyjson_get_str(yyjson_obj_get(command, "directory"));
        const char *file = yyjson_get_str(yyjson_obj_get(command, "file"));
        const char *compiler = yyjson_get_str(
            yyjson_arr_get(yyjson_obj_get(command, "arguments"), 0)
        );
        static const char prefix[] = CHIBI_SOURCE_ROOT "/src/support/";
        listed = listed && directory && !strcmp(directory, CHIBI_SOURCE_ROOT) &&
                 file && !strncmp(file, prefix, sizeof prefix - 1) &&
                 compiler && !strcmp(compiler, "/usr/bin/false");
        file_io |= file && !strcmp(file, CHIBI_SOURCE_ROOT "/src/support/file_io.c");
    }
    printf("SDK relocated graph: %zu support source commands\n", yyjson_arr_size(commands_root));
    yyjson_doc_free(doc);
    REQUIRE(listed && file_io);
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

static int install_project(const char *build, const char *output, const char *assembly)
{
    const char *argv[] = {
        host_path,
        "--root",
        CHIBI_SOURCE_ROOT,
        "--source",
        CHIBI_SOURCE_ROOT "/tools/toolchain/SDK.chibi",
        "--core",
        CHIBI_SOURCE_ROOT "/packages/core",
        "--entry",
        "ProjectTools",
        "--timeout-ms",
        "300000",
        "--",
        "--build",
        build,
        "--output",
        output,
        "--third-party",
        archive_path,
        "--policy-source",
        project_source,
        "--assembly",
        assembly,
        "--compiler-backend",
        "sljit",
        NULL,
    };
    return tool_run_within(argv, suite, log_path, PROJECT_RUN_SECONDS);
}

TEST_CASE(project_tools_require_executable_assembly_before_staging)
{
    char assembly[SDK_PATH], output[SDK_PATH], stage[SDK_PATH];
    join(assembly, suite, "nonexecutable assembly");
    REQUIRE(clone_file(project_assembly, assembly));
    REQUIRE(!chmod(assembly, 0600));
    join(output, suite, "nonexecutable project tools");
    join(stage, suite, "nonexecutable project tools.chibi-stage");
    REQUIRE(install_project(build_path, output, assembly) == 2);
    char *log = tool_read(log_path);
    bool refused = log && strstr(log, "sdk: ProjectTools requires");
    free(log);
    REQUIRE(refused && !exists(output) && !exists(stage));
    REQUIRE(same_file(assembly, project_assembly));
    return true;
}

TEST_CASE(project_tools_cannot_publish_without_real_prepared_export)
{
    char clone[SDK_PATH], output[SDK_PATH], stage[SDK_PATH], host[SDK_PATH];
    REQUIRE(clone_graph(clone, "project fake build host"));
    join(host, clone, "sdk/chibi-build-host");
    REQUIRE(!unlink(host));
    REQUIRE(clone_file("/usr/bin/true", host));
    join(output, suite, "forged project tools");
    join(stage, suite, "forged project tools.chibi-stage");
    REQUIRE(install_project(clone, output, project_assembly) == 1);
    char *log = tool_read(log_path);
    bool refused = log && strstr(log, "sdk: cannot export prepared project policy");
    free(log);
    REQUIRE(refused && !exists(output) && !exists(stage));
    REQUIRE(same_file(host, "/usr/bin/true"));
    return true;
}

static const char *const project_keys[] = {
    "policy-source",
    "policy-artifact",
    "host-witness",
    "compiler",
    "core",
    "bootstrap",
    "source-sha256",
    "artifact-sha256",
    "build-host-sha256",
    "compiler-host-sha256",
    "assembly-sha256",
    "inputs-sha256",
    "inspect-entry",
    "compile-entry",
    "platform",
    "compiler-backend",
};
static const char *const project_outputs[] = {
    PROJECT_SOURCE,
    PROJECT_ARTIFACT,
    PROJECT_WITNESS,
    PROJECT_RECEIPT,
};
typedef struct
{
    char *path;
    bool directory;
} project_owner;

static bool hex_digest(const char *text)
{
    return text && strlen(text) == 64 && strspn(text, "0123456789abcdef") == 64;
}

/* Read complete LF-terminated text, with no embedded NUL or CR. */
static char *project_table(const char *prefix, const char *relative, const char *header)
{
    char path[SDK_PATH];
    join(path, prefix, relative);
    char *text = tool_read(path);
    struct stat info;
    bool valid = text && !lstat(path, &info) && S_ISREG(info.st_mode) &&
                 info.st_size > 0 && (size_t)info.st_size == strlen(text) &&
                 !strchr(text, '\r') && text[strlen(text) - 1] == '\n' &&
                 !strncmp(text, header, strlen(header));
    if (!valid) {
        free(text);
        return NULL;
    }
    return text;
}

/* Split one TSV record. Blank records and more than three fields are invalid. */
static int project_row(char **cursor, char *fields[3])
{
    char *line = *cursor, *end = strchr(line, '\n');
    if (!end || end == line) {
        return -1;
    }
    *end = 0;
    *cursor = end + 1;
    int count = 1;
    fields[0] = line;
    for (char *tab; (tab = strchr(line, '\t'));) {
        if (count == 3) {
            return -1;
        }
        *tab = 0;
        fields[count++] = line = tab + 1;
    }
    for (int i = 0; i < count; i++) {
        if (!*fields[i]) {
            return -1;
        }
    }
    return count;
}

static bool path_below(const char *path, const char *parent)
{
    size_t length = strlen(parent);
    return !strcmp(path, parent) ||
           (!strncmp(path, parent, length) && path[length] == '/');
}

static bool project_witness_matches(const char *prefix, char *const values[16])
{
    static const int metadata[] = { 11, 6, 7, 8, 9, 10, 12, 13, 14, 15 };
    static const char *checks[] = {
        "prepared-load",
        "prepared-without-source",
        "prepared-entry-selection",
        "reserve-directory",
        "publish-directory",
        "publish-pair",
    };
    static const char header[] = "CHIBI-PROJECT-HOST/2\n";
    char *text = project_table(prefix, PROJECT_WITNESS, header);
    if (!text) {
        return false;
    }
    char *cursor = text + sizeof header - 1;
    bool valid = true;
    for (int i = 0; valid && i < 16; i++) {
        char *fields[3];
        int key = i < 10 ? metadata[i] : 0;
        const char *name = i < 10 ? project_keys[key] : checks[i - 10];
        const char *value = i < 10 ? values[key] : "passed";
        valid = project_row(&cursor, fields) == 2 && !strcmp(fields[0], name) &&
                !strcmp(fields[1], value);
    }
    valid = valid && !*cursor;
    free(text);
    return valid;
}

static bool project_inputs_match(const char *prefix, project_owner *owners, const char *expected)
{
    char *text = project_table(prefix, SDK_MANIFEST, "1\n");
    if (!text) {
        return false;
    }
    sha256 hash;
    sha256_begin(&hash);
    bool valid = true;
    char **listed = NULL;
    for (char *line = text + 2; valid && *line;) {
        char *end = strchr(line, '\n'), *tab = strchr(line, '\t');
        if (!end || !tab || tab > end) {
            valid = false;
            break;
        }
        *tab = 0;
        bool owned = false, output = false;
        for (int i = 0; i < acount(owners); i++) {
            owned |= !strcmp(line, owners[i].path) ||
                     (owners[i].directory && path_below(line, owners[i].path));
        }
        for (int i = 0; i < 4; i++) {
            output |= !strcmp(line, project_outputs[i]);
        }
        *tab = '\t';
        if (!output) {
            sha256_add(&hash, line, (size_t)(end - line) + 1);
        }
        *tab = 0;
        *end = 0;
        char path[SDK_PATH], digest[65];
        join(path, prefix, line);
        valid = owned && file_digest(path, digest) && !strcmp(digest, tab + 1);
        if (valid) {
            apush(listed, line);
        }
        line = end + 1;
    }
    int files = 0;
    for (int i = 0; valid && i < acount(owners); i++) {
        char path[SDK_PATH];
        join(path, prefix, owners[i].path);
        int count = installed_files(path, prefix, listed);
        valid = count >= 0;
        files += count;
    }
    char digest[65];
    sha256_end(&hash, digest);
    valid = valid && files == acount(listed) && !strcmp(digest, expected);
    afree(listed);
    free(text);
    return valid;
}

static bool project_receipt_matches(const char *prefix)
{
    static const char header[] = "CHIBI-PROJECT-PREPARE/1\n";
    char *text = project_table(prefix, PROJECT_RECEIPT, header);
    REQUIRE(text);
    char *values[16] = { 0 }, **outputs = NULL;
    project_owner *owners = NULL;
    char *cursor = text + sizeof header - 1;
    bool valid = true;
    while (valid && *cursor) {
        char *fields[3];
        int count = project_row(&cursor, fields);
        if (count < 2) {
            valid = false;
            break;
        }
        if (count == 3 &&
            (!strcmp(fields[0], "ownership") || !strcmp(fields[0], "output"))) {
            valid = relative_file(fields[2]) &&
                    (!strcmp(fields[1], "file") || !strcmp(fields[1], "directory"));
            if (!valid) {
                break;
            }
            if (!strcmp(fields[0], "output")) {
                valid = !strcmp(fields[1], "file");
                for (int i = 0; valid && i < acount(outputs); i++) {
                    valid = strcmp(outputs[i], fields[2]) != 0;
                }
                apush(outputs, fields[2]);
                continue;
            }
            for (int i = 0; valid && i < acount(owners); i++) {
                valid = !path_below(fields[2], owners[i].path) &&
                        !path_below(owners[i].path, fields[2]);
            }
            project_owner owner = { fields[2], !strcmp(fields[1], "directory") };
            apush(owners, owner);
            continue;
        }
        int key = 0;
        while (key < 16 && strcmp(fields[0], project_keys[key])) {
            key++;
        }
        valid = count == 2 && key < 16 && !values[key];
        if (valid) {
            values[key] = fields[1];
        }
    }
    for (int i = 0; valid && i < 16; i++) {
        valid = values[i] && (i < 6 || i > 11 || hex_digest(values[i]));
    }
    static const char *paths[] = {
        PROJECT_SOURCE,     PROJECT_ARTIFACT,
        PROJECT_WITNESS,    "share/chibi/compiler",
        "share/chibi/core", "share/chibi/compiler/bootstrap",
    };
    for (int i = 0; valid && i < 6; i++) {
        valid = !strcmp(values[i], paths[i]);
    }
    valid = valid && acount(outputs) == 4 && acount(owners) > 0;
    for (int i = 0; valid && i < 4; i++) {
        bool output = false, owner = false;
        for (int j = 0; j < acount(outputs); j++) {
            output |= !strcmp(project_outputs[i], outputs[j]);
        }
        for (int j = 0; j < acount(owners); j++) {
            owner |= !owners[j].directory &&
                     !strcmp(project_outputs[i], owners[j].path);
        }
        valid = output && owner;
    }
    bool compiler = false, core = false;
    for (int i = 0; valid && i < acount(owners); i++) {
        char path[SDK_PATH];
        struct stat info;
        join(path, prefix, owners[i].path);
        valid = !lstat(path, &info) &&
                (owners[i].directory ? S_ISDIR(info.st_mode) : S_ISREG(info.st_mode));
        compiler |= owners[i].directory && !strcmp(owners[i].path, paths[3]);
        core |= owners[i].directory && !strcmp(owners[i].path, paths[4]);
        valid = valid && !path_below(owners[i].path, paths[5]) &&
                strcmp(owners[i].path, SDK_MANIFEST) != 0;
    }
    valid = valid && compiler && core;
    static const char *roles[] = {
        PROJECT_SOURCE,
        PROJECT_ARTIFACT,
        "libexec/chibi/chibi-build-host",
        "libexec/chibi/chibi-compiler-host",
    };
    for (int i = 0; valid && i < 4; i++) {
        char path[SDK_PATH], digest[65];
        join(path, prefix, roles[i]);
        valid = file_digest(path, digest) && !strcmp(digest, values[6 + i]);
    }
    char assembly_digest[65];
    struct utsname platform;
    valid = valid && file_digest(project_assembly, assembly_digest) &&
            !strcmp(assembly_digest, values[10]) &&
            !strcmp(values[12], "Inspect") && !strcmp(values[13], "Compile") &&
            !uname(&platform) && !strcmp(values[14], platform.sysname) &&
            !strcmp(values[15], "sljit") &&
            project_inputs_match(prefix, owners, values[11]) &&
            project_witness_matches(prefix, values);
    afree(owners);
    afree(outputs);
    free(text);
    REQUIRE(valid);
    return true;
}

static bool project_inspect(
    const char *prefix,
    bool selfhost,
    const char *cache,
    bool ready,
    char identity[65]
)
{
    char host[SDK_PATH], artifact[SDK_PATH], compiler[SDK_PATH];
    join(host, prefix, "libexec/chibi/chibi-build-host");
    join(artifact, prefix, PROJECT_ARTIFACT);
    join(compiler, prefix, "libexec/chibi/chibi-compiler-host");
    const char *argv[40] = {
        host,
        "--root",
        suite,
        "--artifact",
        artifact,
        "--entry",
        "Inspect",
        "--timeout-ms",
        "120000",
        "--",
        "--route",
        selfhost ? "selfhost" : "native",
        "--target",
        "native",
        "--sdk-root",
        prefix,
        "--inventory",
        SDK_MANIFEST,
        "--receipt",
        PROJECT_RECEIPT,
        "--build-host",
        host,
        "--assembly",
        project_assembly,
        "--invocation-entry",
        "Inspect",
    };
    int count = 26;
    if (selfhost) {
        argv[count++] = "--compiler-host";
        argv[count++] = compiler;
        argv[count++] = "--cache";
        argv[count++] = cache;
    }
    argv[count] = NULL;
    int status = run_without_toolchain(argv, suite);
    char *text = tool_read(log_path);
    static const char frame[] = "CHIBI-INSPECT/1 ";
    size_t length = text ? strlen(text) : 0, first = sizeof frame - 1;
    bool valid = status == (ready && !selfhost ? 0 : 1) && length > first + 2 &&
                 length <= 2048 && !strncmp(text, frame, first) &&
                 text[length - 1] == '\n' && text[length - 2] == '}' &&
                 strchr(text, '\n') == text + length - 1;
    yyjson_doc *doc = valid ? yyjson_read(text + first, length - first - 1, 0) : NULL;
    yyjson_val *root = doc ? yyjson_doc_get_root(doc) : NULL;
    yyjson_val *roles = yyjson_obj_get(root, "roles");
    yyjson_val *id = yyjson_obj_get(root, "identity");
    const char *availability = !ready   ? "unavailable"
                             : selfhost ? "needs-recovery"
                                        : "ready";
    const char *diagnostic = !ready   ? "invalid-preparation"
                           : selfhost ? "recovery-required"
                                      : "ready";
    valid = valid && yyjson_obj_size(root) == 5 &&
            yyjson_equals_str(yyjson_obj_get(root, "route"), selfhost ? "selfhost" : "native") &&
            yyjson_equals_str(yyjson_obj_get(root, "availability"), availability) &&
            yyjson_equals_str(yyjson_obj_get(root, "diagnostic"), diagnostic) &&
            yyjson_arr_size(roles) == (selfhost ? 3u : 2u) &&
            yyjson_equals_str(yyjson_arr_get(roles, 0), "build-host") &&
            yyjson_equals_str(yyjson_arr_get(roles, selfhost ? 2 : 1), "assembly") &&
            (!selfhost || yyjson_equals_str(yyjson_arr_get(roles, 1), "compiler-host")) &&
            (ready ? hex_digest(yyjson_get_str(id)) : yyjson_is_null(id));
    if (valid) {
        snprintf(identity, 65, "%s", ready ? yyjson_get_str(id) : "");
    }
    yyjson_doc_free(doc);
    free(text);
    REQUIRE(valid);
    return true;
}

/* Preserve every inventory byte except this output's existing digest. */
static bool rehash_project_witness(const char *prefix)
{
    char path[SDK_PATH], digest[65];
    join(path, prefix, PROJECT_WITNESS);
    REQUIRE(file_digest(path, digest));
    char *text = project_table(prefix, SDK_MANIFEST, "1\n");
    REQUIRE(text);
    int matches = 0;
    size_t name_length = strlen(PROJECT_WITNESS);
    for (char *line = text + 2; *line;) {
        char *end = strchr(line, '\n');
        assert(end);
        if ((size_t)(end - line) == name_length + 1 + 64 &&
            !memcmp(line, PROJECT_WITNESS, name_length) && line[name_length] == '\t') {
            memcpy(line + name_length + 1, digest, 64);
            matches++;
        }
        line = end + 1;
    }
    if (matches == 1) {
        join(path, prefix, SDK_MANIFEST);
        tool_write(path, text);
    }
    free(text);
    REQUIRE(matches == 1);
    return true;
}

static bool project_rejects_invalid_witnesses(
    const char *prefix,
    const char *cache,
    const char *native,
    const char *selfhost
)
{
    char witness[SDK_PATH], inventory[SDK_PATH];
    join(witness, prefix, PROJECT_WITNESS);
    join(inventory, prefix, SDK_MANIFEST);
    char *original = project_table(prefix, PROJECT_WITNESS, "CHIBI-PROJECT-HOST/2\n");
    char *manifest = project_table(prefix, SDK_MANIFEST, "1\n");
    REQUIRE(original && manifest);
    static const char directory[] = "publish-directory\tpassed\n";
    static const char pair[] = "publish-pair\tpassed\n";
    static const char
        tail[] = "publish-directory\tpassed\npublish-pair\tpassed\n";
    static const char extra[] = "unexpected-publish-pair\tpassed\n";
    static const char *variants[] = {
        "HOST/1",     "missing pair",   "duplicate pair",
        "extra pair", "reordered pair",
    };
    size_t length = strlen(original), pair_length = sizeof pair - 1;
    REQUIRE(
        length >= sizeof tail - 1 &&
        !memcmp(original + length - (sizeof tail - 1), tail, sizeof tail - 1)
    );
    char *changed = xmalloc(length + sizeof extra + sizeof pair);
    char inventory_hash[65], witness_hash[65], inspected[65];
    REQUIRE(file_digest(inventory, inventory_hash) && file_digest(witness, witness_hash));
    bool valid = true;
    for (int i = 0; valid && i < 5; i++) {
        strcpy(changed, original);
        switch (i) {
            case 0: changed[sizeof "CHIBI-PROJECT-HOST/" - 1] = '1'; break;
            case 1: changed[length - pair_length] = 0; break;
            case 2: strcpy(changed + length, pair); break;
            case 3: strcpy(changed + length, extra); break;
            case 4:
                memcpy(changed + length - (sizeof tail - 1), pair, pair_length);
                memcpy(changed + length - (sizeof directory - 1), directory, sizeof directory - 1);
                break;
            default: assert(false);
        }
        tool_write(witness, changed);
        char mutated_inventory[65], mutated_witness[65], after[65];
        valid = rehash_project_witness(prefix) && manifest_matches(prefix) &&
                file_digest(inventory, mutated_inventory) &&
                file_digest(witness, mutated_witness) &&
                project_inspect(prefix, false, cache, false, inspected) &&
                project_inspect(prefix, true, cache, false, inspected) &&
                !exists(cache) && file_digest(inventory, after) &&
                !strcmp(mutated_inventory, after) && file_digest(witness, after) &&
                !strcmp(mutated_witness, after) && manifest_matches(prefix);
        if (!valid) {
            fprintf(stderr, "prepared Inspect accepted or changed %s witness\n", variants[i]);
        }
    }
    tool_write(witness, original);
    tool_write(inventory, manifest);
    free(changed);
    free(manifest);
    free(original);
    char after[65];
    REQUIRE(
        valid && !exists(cache) && manifest_matches(prefix) &&
        project_receipt_matches(prefix)
    );
    REQUIRE(project_inspect(prefix, false, cache, true, inspected) && !strcmp(native, inspected));
    REQUIRE(project_inspect(prefix, true, cache, true, inspected) && !strcmp(selfhost, inspected));
    REQUIRE(!exists(cache) && manifest_matches(prefix));
    REQUIRE(file_digest(inventory, after) && !strcmp(inventory_hash, after));
    REQUIRE(file_digest(witness, after) && !strcmp(witness_hash, after));
    return true;
}

TEST_CASE(project_tools_preserve_prepared_readiness_after_relocation)
{
    char output[SDK_PATH], relocated[SDK_PATH], stage[SDK_PATH], cache[SDK_PATH];
    join(output, suite, "project tools");
    join(relocated, suite, "relocated project tools");
    join(stage, suite, "project tools.chibi-stage");
    join(cache, suite, "absent project cache");
    REQUIRE(!install_project(build_path, output, project_assembly));
    REQUIRE(!exists(stage) && !exists(cache));
    char source[SDK_PATH];
    join(source, output, PROJECT_SOURCE);
    REQUIRE(same_file(source, project_source));
    REQUIRE(copied(output, "libexec/chibi/chibi-build-host", build_path, "sdk/chibi-build-host"));
    REQUIRE(copied(output, "libexec/chibi/chibi-compiler-host", build_path, "sdk/chibi-compiler-host"));
    char artifact[SDK_PATH];
    struct stat info;
    join(artifact, output, PROJECT_ARTIFACT);
    REQUIRE(!stat(artifact, &info) && info.st_size > 0);
    REQUIRE(project_receipt_matches(output) && manifest_matches(output));
    char inventory[SDK_PATH], before[65], after[65];
    join(inventory, output, SDK_MANIFEST);
    REQUIRE(file_digest(inventory, before));
    char native[65], selfhost[65], inspected[65];
    REQUIRE(project_inspect(output, false, cache, true, native));
    REQUIRE(project_inspect(output, true, cache, true, selfhost));
    REQUIRE(!exists(cache) && manifest_matches(output));
    REQUIRE(file_digest(inventory, after) && !strcmp(before, after));
    REQUIRE(!rename(output, relocated) && !exists(output));
    REQUIRE(
        project_inspect(relocated, false, cache, true, inspected) &&
        !strcmp(native, inspected)
    );
    REQUIRE(
        project_inspect(relocated, true, cache, true, inspected) &&
        !strcmp(selfhost, inspected)
    );
    REQUIRE(!exists(cache) && manifest_matches(relocated));
    REQUIRE(project_rejects_invalid_witnesses(relocated, cache, native, selfhost));

    /* Public SDK files are outside CompilerTools ownership. */
    static const char *public[] = { "include", "bin", "lib" };
    for (int i = 0; i < 3; i++) {
        char directory[SDK_PATH], path[SDK_PATH];
        join(directory, relocated, public[i]);
        REQUIRE(!mkdir(directory, 0700) || errno == EEXIST);
        join(path, directory, "unrelated-sdk-file");
        tool_write(path, "public SDK content\n");
    }
    REQUIRE(
        project_inspect(relocated, false, cache, true, inspected) &&
        !strcmp(native, inspected)
    );
    REQUIRE(
        project_inspect(relocated, true, cache, true, inspected) &&
        !strcmp(selfhost, inspected)
    );
    REQUIRE(!exists(cache) && project_receipt_matches(relocated));
    join(inventory, relocated, SDK_MANIFEST);
    REQUIRE(file_digest(inventory, after) && !strcmp(before, after));
    for (int i = 0; i < 3; i++) {
        char directory[SDK_PATH], path[SDK_PATH];
        join(directory, relocated, public[i]);
        join(path, directory, "unrelated-sdk-file");
        char *text = tool_read(path);
        bool intact = text && !strcmp(text, "public SDK content\n");
        free(text);
        REQUIRE(intact && entries(directory) == 1);
    }

    char extra[SDK_PATH];
    join(extra, relocated, "share/chibi/compiler/UnexpectedSDKTest.chibi");
    REQUIRE(!exists(extra));
    tool_write(extra, "package unexpected_sdk_test\n");
    REQUIRE(project_inspect(relocated, false, cache, false, inspected));
    REQUIRE(project_inspect(relocated, true, cache, false, inspected));
    REQUIRE(!exists(cache) && !unlink(extra));
    REQUIRE(
        project_inspect(relocated, false, cache, true, inspected) &&
        !strcmp(native, inspected)
    );
    REQUIRE(
        project_inspect(relocated, true, cache, true, inspected) &&
        !strcmp(selfhost, inspected)
    );
    REQUIRE(!exists(cache) && project_receipt_matches(relocated));
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
    const char *policy = getenv("CHIBI_SDK_PROJECT_SOURCE");
    const char *assembly = getenv("CHIBI_SDK_PROJECT_ASSEMBLY");
    if (policy && *policy && assembly && *assembly) {
        resolve(project_source, policy);
        resolve(project_assembly, assembly);
        RUN_TEST_CASE(project_tools_require_executable_assembly_before_staging);
        RUN_TEST_CASE(project_tools_cannot_publish_without_real_prepared_export);
        RUN_TEST_CASE(project_tools_preserve_prepared_readiness_after_relocation);
    }
    tool_remove_tree(suite);
}
#endif
