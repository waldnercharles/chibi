/* The Chibi build graph (tools/graph, tools/toolchain/Graph.chibi) against the
 * CMake build that configured these tests: the same compile commands for every
 * library it owns, and for chibi_support rebuilds of only what changed and an
 * archive that links. */
#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"
#include "yyjson.h"

#include <assert.h>
#include <dirent.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define GRAPH_PATH 4096
#define GRAPH_RUN_SECONDS 30

static char host_path[4096];
static char reference[GRAPH_PATH], suite[GRAPH_PATH], built[GRAPH_PATH],
    clone[GRAPH_PATH], log_path[GRAPH_PATH];
/* The profile root CMakeLists.txt read from its cache, as graph options. */
static char compiler[GRAPH_PATH], compiler_id[64], build_type[64],
    architecture[64], sysroot[GRAPH_PATH], sanitizers[128],
    debug_dir[GRAPH_PATH], archiver[GRAPH_PATH], ranlib[GRAPH_PATH];
static bool warnings;
static const char *const support_sources[] = { "arena", "file_io", "text_edit", "xalloc", "rcss_map" };
#define SUPPORT_COUNT (int)(sizeof support_sources / sizeof *support_sources)

static void join(char out[GRAPH_PATH], const char *base, const char *name)
{
    int size = snprintf(out, GRAPH_PATH, "%s/%s", base, name);
    assert(size > 0 && size < GRAPH_PATH);
}
/* The value of NAME:TYPE=VALUE in a CMake cache, or "". */
static void cache_value(const char *cache, const char *name, char *out, size_t size)
{
    out[0] = 0;
    size_t length = strlen(name);
    for (const char *line = cache; line && *line;
         line = strchr(line, '\n') ? strchr(line, '\n') + 1 : NULL) {
        if (strncmp(line, name, length) || line[length] != ':') {
            continue;
        }
        const char *value = strchr(line, '=') + 1;
        size_t count = strcspn(value, "\n");
        assert(count < size);
        memcpy(out, value, count);
        out[count] = 0;
        return;
    }
}
/* The quoted value of set(NAME "VALUE") in CMake's compiler file, or "". */
static void compiler_value(const char *text, const char *name, char *out, size_t size)
{
    char key[128];
    snprintf(key, sizeof key, "set(%s \"", name);
    const char *at = strstr(text, key);
    out[0] = 0;
    if (!at) {
        return;
    }
    at += strlen(key);
    size_t count = strcspn(at, "\"");
    assert(count < size);
    memcpy(out, at, count);
    out[count] = 0;
}
static char *compiler_file()
{
    char directory[GRAPH_PATH];
    join(directory, reference, "CMakeFiles");
    DIR *dir = opendir(directory);
    assert(dir);
    char *text = NULL;
    for (struct dirent *entry; !text && (entry = readdir(dir));) {
        char file[GRAPH_PATH];
        snprintf(
            file,
            sizeof file,
            "%s/%s/CMakeCCompiler.cmake",
            directory,
            entry->d_name
        );
        if (entry->d_name[0] != '.' && !access(file, R_OK)) {
            text = tool_read(file);
        }
    }
    closedir(dir);
    assert(text);
    return text;
}
/* What CMake names debug info's directory in a ccache build in the tree. */
static void ccache_debug_dir(const char *cache)
{
    char ccache[GRAPH_PATH];
    cache_value(cache, "CCACHE_PROGRAM", ccache, sizeof ccache);
    size_t root = strlen(CHIBI_SOURCE_ROOT);
    debug_dir[0] = 0;
    if (!ccache[0] || strstr(ccache, "NOTFOUND") ||
        strncmp(reference, CHIBI_SOURCE_ROOT, root) || reference[root] != '/') {
        return;
    }
    char *out = debug_dir;
    for (const char *part = reference + root + 1; *part;) {
        size_t count = strcspn(part, "/");
        out += sprintf(out, "%sbuild", out == debug_dir ? "" : "/");
        part += count + (part[count] == '/');
    }
}
static void read_profile()
{
    char path[GRAPH_PATH], value[GRAPH_PATH];
    join(path, reference, "CMakeCache.txt");
    char *cache = tool_read(path);
    assert(cache);
    char *file = compiler_file();
    compiler_value(file, "CMAKE_C_COMPILER", compiler, sizeof compiler);
    compiler_value(file, "CMAKE_C_COMPILER_ID", compiler_id, sizeof compiler_id);
    cache_value(cache, "CMAKE_BUILD_TYPE", build_type, sizeof build_type);
    cache_value(cache, "CMAKE_OSX_SYSROOT", sysroot, sizeof sysroot);
    cache_value(cache, "CMAKE_OSX_ARCHITECTURES", architecture, sizeof architecture);
#ifdef __APPLE__
    if (!architecture[0]) {
        compiler_value(file, "CMAKE_C_COMPILER_ARCHITECTURE_ID", architecture, sizeof architecture);
    }
#else
    architecture[0] = sysroot[0] = 0;
#endif
    cache_value(cache, "CHIBI_ENABLE_WARNINGS", value, sizeof value);
    warnings = strcmp(value, "OFF") != 0;
    cache_value(cache, "CHIBI_ENABLE_SANITIZERS", value, sizeof value);
    sanitizers[0] = 0;
    if (!strcmp(value, "ON")) {
        cache_value(cache, "CHIBI_SANITIZERS", sanitizers, sizeof sanitizers);
    }
    cache_value(cache, "CMAKE_AR", archiver, sizeof archiver);
    cache_value(cache, "CMAKE_RANLIB", ranlib, sizeof ranlib);
    ccache_debug_dir(cache);
    free(file);
    free(cache);
}

/* Runs the graph's Libraries entry over root into build_dir with the options
 * after it; the number of sources it compiled, 0 when it only wrote commands,
 * or -1 when it failed. */
static int graph(const char *root, const char *build_dir, bool warn, const char *const *extra)
{
    const char *argv[64] = { host_path,
                             "--root",
                             root,
                             "--source",
                             "tools/toolchain/Graph.chibi",
                             "--core",
                             "packages/core",
                             "--collection",
                             "graph=tools/graph",
                             "--entry",
                             "Libraries",
                             "--timeout-ms",
                             "30000",
                             "--",
                             "--build",
                             build_dir,
                             "--compiler",
                             compiler,
                             "--compiler-id",
                             compiler_id,
                             "--build-type",
                             build_type,
                             "--warnings",
                             warn ? "on" : "off",
                             "--archiver",
                             archiver,
                             "--ranlib",
                             ranlib,
                             "--jobs",
                             "4" };
    int count = 30;
    const char *optional[][2] = { { "--architecture", architecture },
                                  { "--sysroot", sysroot },
                                  { "--sanitizers", sanitizers },
                                  { "--debug-compilation-dir", debug_dir } };
    for (int i = 0; i < 4; i++) {
        if (optional[i][1][0]) {
            argv[count++] = optional[i][0];
            argv[count++] = optional[i][1];
        }
    }
    for (int i = 0; extra[i]; i++) {
        argv[count++] = extra[i];
    }
    argv[count] = NULL;
    /* A run compiles up to five sources with the profile's sanitizers. */
    int status = tool_run_within(argv, root, log_path, GRAPH_RUN_SECONDS);
    char *text = tool_read(log_path);
    if (status) {
        fprintf(stderr, "graph failed: %s\n", text ? text : "");
        free(text);
        return -1;
    }
    const char *at = text ? strstr(text, "compiled ") : NULL;
    int compiled = at ? atoi(at + strlen("compiled ")) : 0;
    free(text);
    return compiled;
}
static const char *const support_only[] = { "--target", "chibi_support", NULL };

/* Whether the graph's arguments match CMake's command, except for -o's value,
 * with the graph's build directory standing for CMake's. */
static bool same_command(yyjson_val *ours, char *theirs)
{
    size_t built_length = strlen(built), index = 0;
    const char *previous = "";
    for (char *token = strtok(theirs, " "); token; token = strtok(NULL, " "), index++) {
        const char *argument = yyjson_get_str(yyjson_arr_get(ours, index));
        if (!argument) {
            return false;
        }
        char expected[GRAPH_PATH];
        const char *at = strstr(argument, built);
        if (at) {
            snprintf(expected, sizeof expected, "%.*s%s%s", (int)(at - argument), argument, reference, at + built_length);
        } else {
            snprintf(expected, sizeof expected, "%s", argument);
        }
        if (strcmp(previous, "-o") && strcmp(expected, token)) {
            fprintf(stderr, "graph argument %zu: %s, CMake: %s\n", index, expected, token);
            return false;
        }
        previous = token;
    }
    return index == yyjson_arr_size(ours);
}

/* Every compile command the graph writes equals CMake's for the same source. */
TEST_CASE(commands_match_cmake)
{
    const char *const commands_only[] = { "--commands-only", "on", NULL };
    REQUIRE(graph(CHIBI_SOURCE_ROOT, built, warnings, commands_only) == 0);
    char path[GRAPH_PATH];
    join(path, built, "compile_commands.json");
    yyjson_doc *graph_doc = yyjson_read_file(path, 0, NULL, NULL);
    join(path, reference, "compile_commands.json");
    yyjson_doc *cmake_doc = yyjson_read_file(path, 0, NULL, NULL);
    REQUIRE(graph_doc && cmake_doc);
    yyjson_val *ours = yyjson_doc_get_root(graph_doc);
    /* The libraries the graph owns are those its objects belong to. */
    char owners[64][128];
    int owner_count = 0;
    yyjson_val *entry;
    yyjson_arr_iter mine_iter = yyjson_arr_iter_with(ours);
    while ((entry = yyjson_arr_iter_next(&mine_iter))) {
        const char *output = strstr(yyjson_get_str(yyjson_obj_get(entry, "output")), "/objects/");
        REQUIRE(output);
        output += strlen("/objects/");
        char owner[128];
        snprintf(owner, sizeof owner, "/%.*s.dir/", (int)strcspn(output, "/"), output);
        bool seen = false;
        for (int i = 0; i < owner_count; i++) {
            seen |= !strcmp(owners[i], owner);
        }
        if (!seen) {
            assert(owner_count < 64);
            strcpy(owners[owner_count++], owner);
        }
    }
    int matched = 0, expected = 0;
    yyjson_arr_iter iter = yyjson_arr_iter_with(yyjson_doc_get_root(cmake_doc));
    while ((entry = yyjson_arr_iter_next(&iter))) {
        const char *command = yyjson_get_str(yyjson_obj_get(entry, "command"));
        const char *file = yyjson_get_str(yyjson_obj_get(entry, "file"));
        bool owned = false;
        for (int i = 0; i < owner_count; i++) {
            owned |= command && strstr(command, owners[i]) != NULL;
        }
        if (!owned) {
            continue;
        }
        expected++;
        yyjson_val *mine;
        yyjson_arr_iter ours_iter = yyjson_arr_iter_with(ours);
        while ((mine = yyjson_arr_iter_next(&ours_iter))) {
            /* A generated source is below the graph's build directory. */
            const char *source = yyjson_get_str(yyjson_obj_get(mine, "file"));
            char expected_file[GRAPH_PATH];
            if (!strncmp(source, built, strlen(built))) {
                snprintf(expected_file, sizeof expected_file, "%s%s", reference, source + strlen(built));
            } else {
                snprintf(expected_file, sizeof expected_file, "%s", source);
            }
            if (strcmp(expected_file, file)) {
                continue;
            }
            char *copy = strdup(command);
            matched += same_command(yyjson_obj_get(mine, "arguments"), copy);
            free(copy);
        }
    }
    fprintf(stderr, "graph commands: %d of %d match CMake's in %d libraries\n", matched, expected, owner_count);
    REQUIRE(expected > 0 && matched == expected && (size_t)expected == yyjson_arr_size(ours));
    yyjson_doc_free(graph_doc);
    yyjson_doc_free(cmake_doc);
    return true;
}

static void append(const char *root, const char *name, const char *text)
{
    char path[GRAPH_PATH];
    join(path, root, name);
    char *old = tool_read(path);
    assert(old);
    size_t size = strlen(old) + strlen(text) + 1;
    char *changed = malloc(size);
    snprintf(changed, size, "%s%s", old, text);
    tool_write(path, changed);
    free(changed);
    free(old);
}
static void object_record(char out[GRAPH_PATH], const char *build_dir, const char *source)
{
    snprintf(out, GRAPH_PATH, "%s/objects/chibi_support/src/support/%s.c.o.chibi-record", build_dir, source);
}
/* How many chibi_support objects the records say include a staged header. */
static int including(const char *build_dir, const char *header)
{
    int count = 0;
    for (int s = 0; s < SUPPORT_COUNT; s++) {
        char record[GRAPH_PATH];
        object_record(record, build_dir, support_sources[s]);
        char *text = tool_read(record);
        count += text && strstr(text, header);
        free(text);
    }
    return count;
}

TEST_CASE(support_rebuilds_only_what_changed)
{
    char build_dir[GRAPH_PATH], stale[GRAPH_PATH];
    join(build_dir, suite, "incremental");
    REQUIRE(graph(clone, build_dir, warnings, support_only) == SUPPORT_COUNT);
    REQUIRE(graph(clone, build_dir, warnings, support_only) == 0);
    append(clone, "src/support/xalloc.c", "/* changed */\n");
    REQUIRE(graph(clone, build_dir, warnings, support_only) == 1);
    int dependents = including(build_dir, "sdk/public/support/support/arena.h");
    REQUIRE(dependents >= 1 && dependents < SUPPORT_COUNT);
    append(clone, "src/support/arena.h", "/* changed */\n");
    REQUIRE(graph(clone, build_dir, warnings, support_only) == dependents);
    char object[GRAPH_PATH];
    snprintf(object, sizeof object, "%s/objects/chibi_support/src/support/text_edit.c.o", build_dir);
    REQUIRE(!unlink(object));
    /* A header nothing publishes leaves the staged include root. */
    snprintf(stale, sizeof stale, "%s/sdk/public/support/support/moved.h", build_dir);
    tool_write(stale, "#error moved\n");
    REQUIRE(graph(clone, build_dir, warnings, support_only) == 1);
    REQUIRE(access(stale, F_OK));
    /* Other flags make every command differ. */
    REQUIRE(graph(clone, build_dir, !warnings, support_only) == SUPPORT_COUNT);
    return true;
}

/* A source that fails to compile fails the graph and keeps no record, so the
 * next run retries it alone. Runs on the build the previous case left, whose
 * warning flags are the profile's opposite. */
TEST_CASE(support_failed_compile_is_retried)
{
    char build_dir[GRAPH_PATH], path[GRAPH_PATH], record[GRAPH_PATH];
    join(build_dir, suite, "incremental");
    REQUIRE(graph(clone, build_dir, !warnings, support_only) == 0);
    join(path, clone, "src/support/xalloc.c");
    char *original = tool_read(path);
    REQUIRE(original);
    append(clone, "src/support/xalloc.c", "this is not C;\n");
    REQUIRE(graph(clone, build_dir, !warnings, support_only) == -1);
    object_record(record, build_dir, "xalloc");
    REQUIRE(access(record, F_OK));
    object_record(record, build_dir, "arena");
    REQUIRE(!access(record, F_OK));
    tool_write(path, original);
    free(original);
    REQUIRE(graph(clone, build_dir, !warnings, support_only) == 1);
    return true;
}

/* A program that calls into the graph's archive links and runs. */
TEST_CASE(support_archive_links)
{
    char program[GRAPH_PATH], binary[GRAPH_PATH], include[GRAPH_PATH],
        library[GRAPH_PATH];
    join(program, suite, "main.c");
    join(binary, suite, "main");
    snprintf(include, sizeof include, "-I%s/incremental/sdk/public/support", suite);
    snprintf(library, sizeof library, "%s/incremental/lib/libchibi_support.a", suite);
    tool_write(
        program,
        "#include \"support/xalloc.h\"\n#include <stdlib.h>\n"
        "int main(void) { char *p = xmalloc(8); p[0] = 0; free(p); return p == "
        "NULL; }\n"
    );
    char sanitize[160];
    snprintf(sanitize, sizeof sanitize, "-fsanitize=%s", sanitizers);
    const char *argv[16] = { compiler, "-std=gnu23", include, program,
                             library,  "-o",         binary };
    int count = 7;
    if (sanitizers[0]) {
        argv[count++] = sanitize;
    }
    if (sysroot[0]) {
        argv[count++] = "-isysroot";
        argv[count++] = sysroot;
    }
    argv[count] = NULL;
    REQUIRE(!tool_run(argv, suite, log_path));
    const char *run[] = { binary, NULL };
    REQUIRE(!tool_run(run, suite, log_path));
    return true;
}

/* Copies what the graph reads into a disposable root. */
static void make_clone()
{
    join(clone, suite, "source");
    const char *const parts[] = { "src/support", "tools/graph", "tools/toolchain", "packages/core" };
    for (size_t i = 0; i < sizeof parts / sizeof *parts; i++) {
        char from[GRAPH_PATH], to[GRAPH_PATH];
        join(from, CHIBI_SOURCE_ROOT, parts[i]);
        join(to, clone, parts[i]);
        char parent[GRAPH_PATH];
        snprintf(parent, sizeof parent, "%s", to);
        const char *mkdir_argv[] = { "/bin/mkdir", "-p", dirname(parent), NULL };
        assert(!tool_run(mkdir_argv, suite, log_path));
#ifdef __APPLE__
        const char *copy[] = { "/bin/cp", "-cR", from, to, NULL };
#else
        const char *copy[] = { "/bin/cp", "-R", "--reflink=auto", from, to, NULL };
#endif
        assert(!tool_run(copy, suite, log_path));
    }
}

void test_build_graph(const char *host)
{
    /* The host sits in the configured build's sdk directory. */
    char *resolved = realpath(host, NULL);
    assert(resolved);
    snprintf(host_path, sizeof host_path, "%s", resolved);
    snprintf(reference, sizeof reference, "%s", dirname(dirname(resolved)));
    free(resolved);
    strcpy(suite, "/tmp/chibi-graph-XXXXXX");
    assert(mkdtemp(suite));
    resolved = realpath(suite, NULL);
    snprintf(suite, sizeof suite, "%s", resolved);
    free(resolved);
    join(built, suite, "build");
    join(log_path, suite, "graph.log");
    read_profile();
    make_clone();
    RUN_TEST_CASE(commands_match_cmake);
    RUN_TEST_CASE(support_rebuilds_only_what_changed);
    RUN_TEST_CASE(support_failed_compile_is_retried);
    RUN_TEST_CASE(support_archive_links);
    tool_remove_tree(suite);
}
