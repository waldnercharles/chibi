/* The Chibi build graph (tools/graph, tools/toolchain/Graph.chibi): rebuilds of
 * only what changed, an archive that links and rejected arguments. */
#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"
#include "yyjson.h"

#include <assert.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define GRAPH_PATH 4096
#define GRAPH_RUN_SECONDS 50

static char host_path[4096];
static char suite[GRAPH_PATH], clone[GRAPH_PATH], log_path[GRAPH_PATH];
/* The Debug profile warns; support tests flip this to force a rebuild. */
static const bool warnings = true;
static const char *const support_sources[] = { "arena", "file_io", "text_edit", "xalloc", "rcss_map" };
#define SUPPORT_COUNT (int)(sizeof support_sources / sizeof *support_sources)

static void join(char out[GRAPH_PATH], const char *base, const char *name)
{
    int size = snprintf(out, GRAPH_PATH, "%s/%s", base, name);
    assert(size > 0 && size < GRAPH_PATH);
}
/* Runs the production Libraries entry and preserves its exact CLI status. */
static int graph_status(
    const char *root,
    const char *build_dir,
    const char *const *options,
    const char *const *extra
)
{
    const char *argv[80] = { host_path,
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
                             "50000",
                             "--",
                             "--build",
                             build_dir,
                             "--jobs",
                             "8" };
    int count = 18;
    for (int i = 0; options[i]; i++) {
        argv[count++] = options[i];
    }
    for (int i = 0; extra[i]; i++) {
        argv[count++] = extra[i];
    }
    argv[count] = NULL;
    /* A run compiles up to 27 sources with the profile's sanitizers. */
    return tool_run_within(argv, root, log_path, GRAPH_RUN_SECONDS);
}
/* The number of sources compiled, 0 for commands-only, or -1 on failure. */
static int run_graph(
    const char *root,
    const char *build_dir,
    const char *const *options,
    const char *const *extra
)
{
    int status = graph_status(root, build_dir, options, extra);
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

/* A run with the developer Debug profile; the graph discovers the toolchain. */
static int graph(const char *root, const char *build_dir, bool warn, const char *const *extra)
{
    const char *const options[] = { "--profile", "debug", "--warnings", warn ? "on" : "off", NULL };
    return run_graph(root, build_dir, options, extra);
}
static const char *const support_only[] = { "--target", "chibi_support", NULL };

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
    const char *argv[] = { getenv("CC") ? getenv("CC") : "cc",
                           "-std=gnu23",
                           include,
                           program,
                           library,
                           "-o",
                           binary,
                           NULL };
    REQUIRE(!tool_run(argv, suite, log_path));
    const char *run[] = { binary, NULL };
    REQUIRE(!tool_run(run, suite, log_path));
    return true;
}

/* An executable publishes no headers, so building one leaves every header
 * its libraries staged in place. A compiler that always fails stops the run
 * after staging. */
TEST_CASE(executables_stage_no_headers)
{
    char build_dir[GRAPH_PATH], header[GRAPH_PATH];
    join(build_dir, suite, "launcher");
    /* The configured profile, so the run stages from the archive CMake
     * unpacked; Option takes the first --compiler. */
    const char *const failing[] = { "--profile", "debug", "--compiler", "/usr/bin/false", NULL };
    const char *const launcher_only[] = { "--target", "chibi", NULL };
    REQUIRE(run_graph(CHIBI_SOURCE_ROOT, build_dir, failing, launcher_only) == -1);
    join(header, build_dir, "sdk/public/support/support/arena.h");
    REQUIRE(!access(header, R_OK));
    return true;
}

static const char *base(const char *path)
{
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

/* Naming test_tools and the launcher builds the programs they run, as CMake's
 * add_dependencies does (test_tools compiles their paths in, and the launcher
 * runs the compiler and player beside it), and no other program. */
TEST_CASE(named_programs_build_what_they_run)
{
    char build_dir[GRAPH_PATH], path[GRAPH_PATH];
    join(build_dir, suite, "runs");
    const char *const tools_only[] = {
        "--commands-only", "on",    "--target", "test_tools",
        "--target",        "chibi", NULL
    };
    REQUIRE(graph(CHIBI_SOURCE_ROOT, build_dir, warnings, tools_only) == 0);
    join(path, build_dir, "link_commands.json");
    yyjson_doc *doc = yyjson_read_file(path, 0, NULL, NULL);
    REQUIRE(doc);
    static const char *const programs[] = {
        "test/tools/test_tools",   "test/tools/policy_worker",
        "sdk/chibi-compile",       "sdk/chibi-build-host",
        "sdk/chibi-compiler-host", "sdk/chibi"
    };
    int found = 0;
    yyjson_val *entry;
    yyjson_arr_iter iter = yyjson_arr_iter_with(yyjson_doc_get_root(doc));
    while ((entry = yyjson_arr_iter_next(&iter))) {
        const char *output = yyjson_get_str(yyjson_obj_get(entry, "output"));
        REQUIRE(strcmp(base(output), "test_host"));
        for (int i = 0; i < (int)(sizeof programs / sizeof *programs); i++) {
            snprintf(path, sizeof path, "%s/%s", build_dir, programs[i]);
            found += !strcmp(output, path);
        }
    }
    yyjson_doc_free(doc);
    REQUIRE(found == (int)(sizeof programs / sizeof *programs));
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

/* The host runs the graph. */
static void set_up(const char *host)
{
    char *resolved = realpath(host, NULL);
    assert(resolved);
    snprintf(host_path, sizeof host_path, "%s", resolved);
    free(resolved);
    strcpy(suite, "/tmp/chibi-graph-XXXXXX");
    assert(mkdtemp(suite));
    resolved = realpath(suite, NULL);
    snprintf(suite, sizeof suite, "%s", resolved);
    free(resolved);
    join(log_path, suite, "graph.log");
}

void test_build_graph(const char *host)
{
    set_up(host);
    RUN_TEST_CASE(executables_stage_no_headers);
    tool_remove_tree(suite);
}

/* A separate route: another commands-only run of the whole graph takes
 * seconds. */
void test_build_graph_programs(const char *host)
{
    set_up(host);
    RUN_TEST_CASE(named_programs_build_what_they_run);
    tool_remove_tree(suite);
}

/* A separate route: about ten graph runs over a copied root. */
void test_build_graph_support(const char *host)
{
    set_up(host);
    make_clone();
    RUN_TEST_CASE(support_rebuilds_only_what_changed);
    RUN_TEST_CASE(support_failed_compile_is_retried);
    RUN_TEST_CASE(support_archive_links);
    tool_remove_tree(suite);
}
