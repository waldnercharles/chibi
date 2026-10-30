/* The Chibi build graph (tools/graph, tools/toolchain/Graph.chibi): rebuilds of
 * only what changed, an archive that links, rejected arguments, toolchain
 * discovery and link response files. */
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
#define GRAPH_RUN_SECONDS 50
#define GRAPH_TOKENS 1024
#define GRAPH_OWNERS 256
#define GRAPH_HEADERS 9

static char host_path[4096];
static char reference[GRAPH_PATH], suite[GRAPH_PATH], built[GRAPH_PATH],
    clone[GRAPH_PATH], log_path[GRAPH_PATH];
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

/* Only explicit rejection cases share an invocation. They return before any
 * discovery, child process or output write. Native values live until the host
 * exits, with its original storage/instruction/time limits, so command-writing
 * and build/rebuild cases still run independently. */
#define GRAPH_BATCH_CASES 14
typedef struct
{
    const char *build_dir;
    const char *const *options, *const *extra;
    int status;
    const char *diagnostic;
} graph_case;

static int run_graph_entry_batch(const graph_case *cases, int count, const char *entry, const char *checks)
{
    assert(count > 0 && count <= GRAPH_BATCH_CASES);
    char path[GRAPH_PATH], source[GRAPH_PATH];
    join(path, CHIBI_SOURCE_ROOT, "tools/toolchain/Graph.chibi");
    char *graph_source = tool_read(path);
    assert(graph_source);
    join(source, suite, "GraphVerification.chibi");
    FILE *file = fopen(source, "wb");
    assert(file);
    assert(fputs(graph_source, file) >= 0);
    free(graph_source);
    /* Length-prefixed argv preserves empty values, missing final values and
     * repetitions exactly; no shell splitting, escaping or sentinel options. */
    assert(
        fprintf(
            file,
            "\nVerifyGraph :: () -> int {\n"
            "%s"
            "    input := build.arguments(); at := 0; index := 0;\n"
            "    while (at < length(input)) {\n"
            "        if (at + 2 > length(input)) { return 3; }\n"
            "        count := Count(input[at]); expected := Count(input[at + "
            "1]);\n"
            "        at = at + 2;\n"
            "        if (count < 1 || at + count > length(input) || index >= "
            "%d) { return 3; }\n"
            "        arguments: string[] = [];\n"
            "        for (i in 0..<count) { arguments.append(input[at + i]); "
            "}\n"
            "        build.report(format_text(\"graph-case {0} begin\", "
            "index));\n"
            "        status := %s(arguments);\n"
            "        build.report(format_text(\"graph-case {0} returned {1}\", "
            "index, status));\n"
            "        if (status != expected) { return 1; }\n"
            "        at = at + count; index = index + 1;\n"
            "    }\n"
            "    if (index == 0) { return 3; }\n"
            "    return 0;\n"
            "}\n",
            checks,
            GRAPH_BATCH_CASES,
            entry
        ) >= 0
    );
    assert(!fclose(file));
    const char *argv[256] = { host_path,
                              "--root",
                              CHIBI_SOURCE_ROOT,
                              "--source",
                              source,
                              "--core",
                              "packages/core",
                              "--collection",
                              "graph=tools/graph",
                              "--entry",
                              "VerifyGraph",
                              "--timeout-ms",
                              "50000",
                              "--" };
    char lengths[GRAPH_BATCH_CASES][16], statuses[GRAPH_BATCH_CASES][16];
    int argc = 14;
    for (int i = 0; i < count; i++) {
        assert(argc + 6 < 256);
        assert(cases[i].status == 0 || cases[i].status == 2);
        int start = argc;
        argv[argc++] = lengths[i];
        snprintf(statuses[i], sizeof statuses[i], "%d", cases[i].status);
        argv[argc++] = statuses[i];
        argv[argc++] = "--build";
        argv[argc++] = cases[i].build_dir;
        argv[argc++] = "--jobs";
        argv[argc++] = "8";
        for (int j = 0; cases[i].options[j]; j++) {
            assert(argc < 255);
            argv[argc++] = cases[i].options[j];
        }
        for (int j = 0; cases[i].extra[j]; j++) {
            assert(argc < 255);
            argv[argc++] = cases[i].extra[j];
        }
        snprintf(lengths[i], sizeof lengths[i], "%d", argc - start - 2);
    }
    argv[argc] = NULL;
    return tool_run_within(argv, CHIBI_SOURCE_ROOT, log_path, GRAPH_RUN_SECONDS);
}

static int run_graph_batch(const graph_case *cases, int count)
{
    return run_graph_entry_batch(cases, count, "LibrariesWithArguments", "");
}

/* Every case owns its diagnostic interval. A previous rejection cannot make a
 * later case pass, and a missing/reordered case cannot match this transcript. */
static bool graph_batch_transcript_matches(const graph_case *cases, int count, const char *log)
{
    const char *at = log;
    bool matches = true;
    for (int i = 0; i < count && matches; i++) {
        char expected[512];
        snprintf(
            expected,
            sizeof expected,
            "graph-case %d begin\n%s%sgraph-case %d returned %d\n",
            i,
            cases[i].diagnostic ? cases[i].diagnostic : "",
            cases[i].diagnostic ? "\n" : "",
            i,
            cases[i].status
        );
        size_t length = strlen(expected);
        matches = !strncmp(at, expected, length);
        if (matches) {
            at += length;
        }
    }
    return matches && !*at;
}
static bool graph_batch_matches(const graph_case *cases, int count)
{
    char *log = tool_read(log_path);
    if (!log) {
        return false;
    }
    bool matches = graph_batch_transcript_matches(cases, count, log);
    if (!matches) {
        fprintf(stderr, "unexpected graph batch transcript: %s\n", log);
    }
    free(log);
    return matches;
}

TEST_CASE(graph_batch_transcripts_are_case_local)
{
    const graph_case cases[] = { { .status = 2, .diagnostic = "first" },
                                 { .status = 2, .diagnostic = "second" } };
    const char
        *complete = "graph-case 0 begin\nfirst\ngraph-case 0 returned 2\n"
                    "graph-case 1 begin\nsecond\ngraph-case 1 returned 2\n";
    REQUIRE(graph_batch_transcript_matches(cases, 2, complete));
    REQUIRE(!graph_batch_transcript_matches(cases, 1, complete));
    REQUIRE(!graph_batch_transcript_matches(cases, 2, ""));
    REQUIRE(
        !graph_batch_transcript_matches(cases, 2, "graph-case 0 begin\nfirst\ngraph-case 0 returned 2\n")
    );
    REQUIRE(!graph_batch_transcript_matches(
        cases,
        2,
        "graph-case 0 begin\nfirst\ngraph-case 0 returned 2\n"
        "graph-case 1 begin\nfirst\ngraph-case 1 returned 2\n"
    ));
    REQUIRE(!graph_batch_transcript_matches(
        cases,
        2,
        "graph-case 1 begin\nsecond\ngraph-case 1 returned 2\n"
        "graph-case 0 begin\nfirst\ngraph-case 0 returned 2\n"
    ));
    REQUIRE(!graph_batch_transcript_matches(
        cases,
        2,
        "graph-case 0 begin\nfirst\ngraph-case 0 returned 0\n"
        "graph-case 1 begin\nsecond\ngraph-case 1 returned 2\n"
    ));
    return true;
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

TEST_CASE(sdk_reflection_rejects_invalid_arguments_before_building)
{
    char build_dir[GRAPH_PATH];
    join(build_dir, suite, "sdk-reflect-arguments");
    const char *const none[] = { NULL };
    const char *const missing_jobs[] = { "--jobs", NULL };
    const char *const missing_build[] = { "--build", NULL };
    const char *const missing_libclang[] = { "--libclang-root", NULL };
    const char *const empty_jobs[] = { "--jobs", "", NULL };
    const char *const empty_build[] = { "--build", "", NULL };
    const char *const empty_libclang[] = { "--libclang-root", "", NULL };
    const char *const unknown_option[] = { "--commands-only", "on", NULL };
    const char *const unknown_definition[] = { "--", "-DUNSUPPORTED=ON", NULL };
    const char *const missing_definition[] = { "--", "-D", NULL };
    const char *const empty_definition[] = { "--", "-DCMAKE_C_COMPILER:FILEPATH=", NULL };
    const char *const typed_compiler[] = {
        "--",
        "-DCMAKE_C_COMPILER:FILEPATH=/usr/bin/true",
        "-DUNSUPPORTED=ON",
        NULL
    };
    const char *const split_compiler[] = {
        "--",
        "-D",
        "CMAKE_C_COMPILER:FILEPATH=/usr/bin/true",
        "-DUNSUPPORTED=ON",
        NULL
    };
    const char *const split_libclang[] = {
        "--",
        "-D",
        "LIBCLANG_ROOT:PATH=/fixture/llvm root",
        "-DUNSUPPORTED=ON",
        NULL
    };
    const char *const split_unknown[] = { "--", "-D", "UNSUPPORTED=ON", NULL };
    const graph_case cases[] = {
        { build_dir, none, missing_jobs, 2, "graph: invalid native reflection option '--jobs'" },
        { build_dir, none, missing_build, 2, "graph: invalid native reflection option '--build'" },
        { build_dir, none, missing_libclang, 2, "graph: invalid native reflection option '--libclang-root'" },
        { build_dir, none, empty_jobs, 2, "graph: invalid native reflection option '--jobs'" },
        { build_dir, none, empty_build, 2, "graph: invalid native reflection option '--build'" },
        { build_dir, none, empty_libclang, 2, "graph: invalid native reflection option '--libclang-root'" },
        { build_dir, none, unknown_option, 2, "graph: invalid native reflection option '--commands-only'" },
        { build_dir, none, unknown_definition, 2, "graph: unsupported native reflection toolchain argument '-DUNSUPPORTED=ON'" },
        { build_dir, none, missing_definition, 2, "graph: -D requires a native toolchain definition" },
        { build_dir, none, empty_definition, 2, "graph: unsupported native reflection toolchain argument '-DCMAKE_C_COMPILER:FILEPATH='" },
        { build_dir, none, typed_compiler, 2, "graph: unsupported native reflection toolchain argument '-DUNSUPPORTED=ON'" },
        { build_dir, none, split_compiler, 2, "graph: unsupported native reflection toolchain argument '-DUNSUPPORTED=ON'" },
        { build_dir, none, split_libclang, 2, "graph: unsupported native reflection toolchain argument '-DUNSUPPORTED=ON'" },
        { build_dir, none, split_unknown, 2, "graph: unsupported native reflection toolchain argument '-DUNSUPPORTED=ON'" }
    };
    /* Check path parsing in the same compiled invocation. Actual-entry
     * cases above accept typed/split values before reaching the final rejection. */
    const char *checks = "    value := \"/fixture/llvm root=value\";\n"
                         "    mapped := "
                         "engine.NativeToolchainOption(strings.Join([\"-"
                         "DLIBCLANG_ROOT:PATH=\", value], \"\"));\n"
                         "    if (length(mapped) != 2 || mapped[0] != "
                         "\"--libclang-root\" || mapped[1] != value) {\n"
                         "        build.report(\"native reflection changed a "
                         "typed path value\");\n"
                         "        return 4;\n"
                         "    }\n"
                         "    if "
                         "(length(engine.NativeToolchainOption(\"-DLIBCLANG_"
                         "ROOT:PATH\")) != 0) {\n"
                         "        build.report(\"native reflection accepted a "
                         "definition without a value\");\n"
                         "        return 4;\n"
                         "    }\n";
    int count = (int)(sizeof cases / sizeof *cases);
    REQUIRE(!run_graph_entry_batch(cases, count, "SDKReflectWithArguments", checks));
    REQUIRE(graph_batch_matches(cases, count));
    REQUIRE(access(build_dir, F_OK) != 0);
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
    join(built, suite, "build");
    join(log_path, suite, "graph.log");
}

void test_build_graph(const char *host)
{
    set_up(host);
    RUN_TEST_CASE(executables_stage_no_headers);
    tool_remove_tree(suite);
}

/* Six fixed rejected invocations: an unknown target alone and beside a
 * known target, a target missing its name at the end or before an option, and
 * asset generator hosts that are relative or missing. They write no build
 * outputs and never reach toolchain discovery. */
TEST_CASE(invalid_arguments_fail_before_writing_commands)
{
    char build_dir[GRAPH_PATH];
    join(build_dir, suite, "invalid targets");
    static const struct
    {
        const char *arguments[8];
        const char *diagnostic;
    } cases[] = {
        { { "--commands-only", "on", "--target", "chibi-clt", NULL },
          "graph: unknown target 'chibi-clt'" },
        { { "--commands-only", "on", "--target", "chibi_support", "--target", "chibi-clt", NULL },
          "graph: unknown target 'chibi-clt'" },
        { { "--commands-only", "on", "--target", NULL },
          "graph: --target requires a target name" },
        { { "--target", "--commands-only", "on", NULL },
          "graph: --target requires a target name" },
        { { "--commands-only", "on", "--asset-generator-hosts", "build-release/sdk", NULL },
          "graph: --asset-generator-hosts requires an absolute directory" },
        { { "--commands-only", "on", "--asset-generator-hosts", NULL },
          "graph: --asset-generator-hosts requires an absolute directory" }
    };
    for (int i = 0; i < (int)(sizeof cases / sizeof *cases); i++) {
        REQUIRE(graph(CHIBI_SOURCE_ROOT, build_dir, warnings, cases[i].arguments) == -1);
        char *log = tool_read(log_path);
        REQUIRE(log && strstr(log, cases[i].diagnostic));
        free(log);
        REQUIRE(access(build_dir, F_OK));
    }
    return true;
}

/* No compiler runs: explicit profiles expose every language's argv, including
 * test assertions, before the opt-in experiment is allowed to build. */
static bool debug_optimization_instrumentation(bool instrumented, int frame_mode)
{
    const char *const options[] = { "--profile",
                                    "explicit",
                                    "--compiler",
                                    "/unused/clang",
                                    "--cxx-compiler",
                                    "/unused/clang++",
                                    "--compiler-id",
                                    "Clang",
                                    "--build-type",
                                    "Debug",
                                    "--sanitizers",
                                    instrumented ? "address,undefined" : "",
                                    "--archiver",
                                    "/unused/ar",
                                    "--ranlib",
                                    "/unused/ranlib",
                                    frame_mode >= 0 ? "--debug-frame-pointers" : NULL,
                                    frame_mode ? "on" : "off",
                                    NULL };
    const char *const levels[] = { "", "0", "1" };
    for (int level = 0; level < 3; level++) {
        char build_dir[GRAPH_PATH], path[GRAPH_PATH];
        char name[64];
        snprintf(
            name,
            sizeof name,
            "%s debug %s frames %d",
            instrumented ? "sanitized" : "plain",
            level ? levels[level] : "default",
            frame_mode
        );
        join(build_dir, suite, name);
        const char *const extra[] = { "--commands-only",
                                      "on",
                                      "--target",
                                      "chibi_support",
                                      "--all-commands",
                                      "on",
                                      level ? "--debug-optimization" : NULL,
                                      levels[level],
                                      level ? "--debug-optimization" : NULL,
                                      "0",
                                      NULL };
        /* Valid repetitions retain Option's existing first-value precedence. */
        REQUIRE(run_graph(CHIBI_SOURCE_ROOT, build_dir, options, extra) == 0);
        join(path, build_dir, "compile_commands.json");
        yyjson_doc *doc = yyjson_read_file(path, 0, NULL, NULL);
        REQUIRE(doc);
        bool saw_test = false, saw_engine = false, saw_sdk = false;
        bool saw_tools = false, saw_language = false;
        yyjson_arr_iter entries = yyjson_arr_iter_with(yyjson_doc_get_root(doc));
        yyjson_val *entry;
        while ((entry = yyjson_arr_iter_next(&entries))) {
            bool debug = false, sanitizer = false, frames = false, assertions = false;
            int optimizations = 0;
            yyjson_arr_iter arguments = yyjson_arr_iter_with(
                yyjson_obj_get(entry, "arguments")
            );
            yyjson_val *value;
            while ((value = yyjson_arr_iter_next(&arguments))) {
                const char *arg = yyjson_get_str(value);
                REQUIRE(arg);
                debug |= !strcmp(arg, "-g");
                if (!strncmp(arg, "-fsanitize=", 11)) {
                    REQUIRE(instrumented && !strcmp(arg, "-fsanitize=address,undefined"));
                    sanitizer = true;
                }
                REQUIRE(strcmp(arg, "-fomit-frame-pointer"));
                frames |= !strcmp(arg, "-fno-omit-frame-pointer");
                assertions |= !strcmp(arg, "-UNDEBUG");
                REQUIRE(strncmp(arg, "-DNDEBUG", 8));
                if (!strncmp(arg, "-O", 2)) {
                    optimizations++;
                    REQUIRE(level && !strcmp(arg + 2, levels[level]));
                }
            }
            REQUIRE(debug);
            REQUIRE(sanitizer == (instrumented != 0));
            REQUIRE(frames == (instrumented || frame_mode == 1));
            REQUIRE(optimizations == (level != 0));
            const char *output = yyjson_get_str(yyjson_obj_get(entry, "output"));
            saw_engine |= strstr(output, "/objects/chibi_events/") != NULL;
            saw_sdk |= strstr(output, "/objects/chibi_compiler/") != NULL;
            if (strstr(output, "/objects/test_host/") ||
                strstr(output, "/objects/policy_worker/")) {
                REQUIRE(assertions);
                saw_test = true;
            }
            if (strstr(output, "/objects/test_tools/")) {
                REQUIRE(assertions);
                saw_tools = true;
            }
            if (strstr(output, "/objects/test_language/")) {
                REQUIRE(assertions);
                saw_language = true;
            }
        }
        REQUIRE(saw_test && saw_engine && saw_sdk);
        REQUIRE(saw_tools && saw_language);
        yyjson_doc_free(doc);
    }
    return true;
}

TEST_CASE(debug_optimization_preserves_instrumentation)
{
    for (int frames = -1; frames <= 1; frames++) {
        REQUIRE(debug_optimization_instrumentation(false, frames));
        REQUIRE(debug_optimization_instrumentation(true, frames));
    }
    return true;
}

TEST_CASE(debug_optimization_rejects_invalid_profiles)
{
    const char *const options[] = { "--profile", "explicit", NULL };
    const char *const cases[][9] = {
        { "--build-type", "Debug", "--debug-optimization", "2", NULL },
        { "--build-type", "Debug", "--debug-optimization", "1", "--debug-optimization", "2", NULL },
        { "--build-type", "Debug", "--debug-optimization", "", NULL },
        { "--build-type", "Debug", "--debug-optimization", NULL },
        { "--build-type", "Debug", "--debug-optimization", "--commands-only", "on", NULL },
        { "--build-type", "Release", "--debug-optimization", "1", NULL },
        { "--build-type", "RelWithDebInfo", "--debug-optimization", "0", NULL },
        { "--build-type", "Debug", "--debug-frame-pointers", "bad", NULL },
        { "--build-type", "Debug", "--debug-frame-pointers", "on", "--debug-frame-pointers", "bad", NULL },
        { "--build-type", "Debug", "--debug-frame-pointers", "", NULL },
        { "--build-type", "Debug", "--debug-frame-pointers", NULL },
        { "--build-type", "Debug", "--debug-frame-pointers", "--commands-only", "on", NULL },
        { "--build-type", "Release", "--debug-frame-pointers", "on", NULL },
        { "--build-type", "RelWithDebInfo", "--debug-frame-pointers", "off", NULL }
    };
    const int count = (int)(sizeof cases / sizeof *cases);
    char build_dirs[sizeof cases / sizeof *cases][GRAPH_PATH];
    for (int first = 0; first < count; first += GRAPH_BATCH_CASES) {
        graph_case batch[GRAPH_BATCH_CASES];
        int size = count - first < GRAPH_BATCH_CASES ? count - first : GRAPH_BATCH_CASES;
        for (int j = 0; j < size; j++) {
            int i = first + j;
            char name[64];
            snprintf(name, sizeof name, "invalid optimization %d", i);
            join(build_dirs[i], suite, name);
            const char *expected = !strcmp(cases[i][2], "--debug-optimization") ? "graph: --debug-optimization requires 0 or 1 and build type Debug"
                                                                                : "graph: --debug-frame-pointers requires on or off and build type Debug";
            batch[j] = (graph_case){ build_dirs[i], options, cases[i], 2, expected };
        }
        REQUIRE(!run_graph_batch(batch, size));
        REQUIRE(graph_batch_matches(batch, size));
        for (int j = 0; j < size; j++) {
            REQUIRE(access(batch[j].build_dir, F_OK));
        }
    }
    /* Keep real Libraries CLI argv-end rejection and exit-status coverage for
     * both options, not just the test-only batched invocation's success. */
    const int cli_cases[] = { 3, 10 };
    for (int j = 0; j < 2; j++) {
        int i = cli_cases[j];
        REQUIRE(graph_status(CHIBI_SOURCE_ROOT, build_dirs[i], options, cases[i]) == 2);
        char *log = tool_read(log_path);
        const char *expected = j == 0
                                 ? "graph: --debug-optimization requires 0 or "
                                   "1 and build type Debug\n"
                                 : "graph: --debug-frame-pointers requires on "
                                   "or off and build type Debug\n";
        bool diagnostic = log && !strcmp(log, expected);
        free(log);
        REQUIRE(diagnostic);
        REQUIRE(access(build_dirs[i], F_OK));
    }
    return true;
}

/* An unexpected result must stop the batch: a later rejection must not hide
 * the first mismatch. The ordinary rejection matrix tests continuation after
 * each expected status 2, with distinct paths and exact per-case diagnostics. */
TEST_CASE(graph_batch_stops_on_unexpected_result)
{
    const char *const options[] = { "--profile", "explicit", NULL };
    const char *const optimization[] = { "--build-type", "Debug", "--debug-optimization", NULL };
    const char *const frames[] = { "--build-type", "Debug", "--debug-frame-pointers", NULL };
    const char *diagnostic = "graph: --debug-optimization requires 0 or 1 and "
                             "build type Debug";
    char rejected[GRAPH_PATH], never[GRAPH_PATH];
    join(rejected, suite, "batch unexpected result");
    join(never, suite, "batch never reached");
    graph_case cases[] = { { rejected, options, optimization, 0, diagnostic },
                           { never, options, frames, 2, NULL } };
    REQUIRE(run_graph_batch(cases, 2) == 1);
    cases[0].status = 2;
    REQUIRE(graph_batch_matches(cases, 1));
    REQUIRE(access(rejected, F_OK));
    REQUIRE(access(never, F_OK));
    return true;
}

/* Parse fixed dependency files through the bytecode CLI, under this route's
 * existing host subprocess and aggregate deadlines. */
TEST_CASE(authored_dependency_files_list_prerequisites)
{
    char short_path[GRAPH_PATH], large_path[GRAPH_PATH], source_path[GRAPH_PATH];
    join(short_path, suite, "primitive.d");
    join(large_path, suite, "primitive-large.d");
    join(source_path, suite, "Dependencies.chibi");
    tool_write(short_path, "out/a.o: a.c b\\ c.h \\\n  d\\#.h \\\r\n e$$f.h\tg\\#h.h\n");
    char large[16384];
    size_t size = (size_t)sprintf(large, "out/a.o: main.c");
    for (int i = 0; i < 128; i++) {
        size += (size_t)sprintf(large + size, " \\\n include/long-enough-to-represent-a-real-header-path/header-%03d.h", i);
    }
    assert(size < sizeof large);
    tool_write(large_path, large);
    tool_write(
        source_path,
        "package dependencies\nimport c \"graph:c\"\nBuild :: () -> int { "
        "arguments := build.arguments(); "
        "if (build.join(c.Dependencies(build.read_file(arguments[0])), \"|\") "
        "!= \"a.c|b c.h|d#.h|e$f.h|g#h.h\") { return 1; } "
        "if (length(c.Dependencies(\"no target\")) != 0 || "
        "length(c.Dependencies(\"out.o: \")) != 0) { return 2; } "
        "if (build.join(c.Dependencies(\"out.o:\\\\\\na.c\"), \"|\") != "
        "\"a.c\" || "
        "build.join(c.Dependencies(\"out.o:\\\\\\r\\nb.c\"), \"|\") != "
        "\"b.c\") { return 4; } "
        "if (build.join(c.Dependencies(\"out.o: a\\\\\\\\\\nb.c\"), \"|\") != "
        "\"a b.c\" || "
        "build.join(c.Dependencies(\"out.o: a\\\\\\\\\\r\\nb.c\"), \"|\") != "
        "\"a b.c\") { return 5; } "
        "paths := c.Dependencies(build.read_file(arguments[1])); "
        "if (length(paths) != 129 || paths[0] != \"main.c\" || "
        "paths[128] != "
        "\"include/long-enough-to-represent-a-real-header-path/header-127.h\") "
        "{ return 3; } return 0; }"
    );
    const char *const argv[] = { host_path,
                                 "--root",
                                 CHIBI_SOURCE_ROOT,
                                 "--source",
                                 source_path,
                                 "--core",
                                 "packages/core",
                                 "--collection",
                                 "graph=tools/graph",
                                 "--entry",
                                 "Build",
                                 "--timeout-ms",
                                 "50000",
                                 "--",
                                 short_path,
                                 large_path,
                                 NULL };
    int status = tool_run_within(argv, CHIBI_SOURCE_ROOT, log_path, GRAPH_RUN_SECONDS);
    if (status) {
        char *log = tool_read(log_path);
        fprintf(stderr, "dependency parser: %s\n", log ? log : "");
        free(log);
    }
    REQUIRE(!status);
    return true;
}

/* Focused pre-merge coverage: emit commands, never compile graph products. */
void test_build_graph_debug_optimization(const char *host)
{
    set_up(host);
    fprintf(stderr, "Graph command tests: host=%s; reference=%s\n", host_path, reference);
    RUN_TEST_CASE(debug_optimization_preserves_instrumentation);
    RUN_TEST_CASE(debug_optimization_rejects_invalid_profiles);
    RUN_TEST_CASE(graph_batch_stops_on_unexpected_result);
    RUN_TEST_CASE(graph_batch_transcripts_are_case_local);
    tool_remove_tree(suite);
}

/* `chibi build` plans the same steps as the retired tools/build.sh build: a non-Release
 * profile first builds the Release hosts it borrows, then runs the third-party
 * and graph steps, and a Release build borrows nothing. --dry-run prints the
 * commands without running them. */
static bool driver_plan(const char *const *options, char **log)
{
    const char *argv[24] = { host_path,
                             "--root",
                             CHIBI_SOURCE_ROOT,
                             "--source",
                             "tools/toolchain/Driver.chibi",
                             "--core",
                             "packages/core",
                             "--entry",
                             "Main",
                             "--timeout-ms",
                             "50000",
                             "--",
                             "build",
                             "--dry-run",
                             "--host",
                             host_path };
    int count = 16;
    for (int i = 0; options[i]; i++) {
        assert(count < 23);
        argv[count++] = options[i];
    }
    argv[count] = NULL;
    REQUIRE(!tool_run_within(argv, CHIBI_SOURCE_ROOT, log_path, GRAPH_RUN_SECONDS));
    *log = tool_read(log_path);
    return *log != NULL;
}

TEST_CASE(driver_plans_the_engine_build_steps)
{
    char *log = NULL;
    const char *const sanitizer[] = { "--target",
                                      "chibi",
                                      "--debug-optimization",
                                      "1",
                                      "--debug-frame-pointers",
                                      "on",
                                      NULL };
    REQUIRE(driver_plan(sanitizer, &log));
    const char *release = strstr(log, "--profile release");
    const char *sanitized = strstr(log, "--profile sanitizer");
    bool planned = release && sanitized && release < sanitized &&
                   strstr(sanitized, "--debug-optimization 1") &&
                   strstr(sanitized, "--debug-frame-pointers on") &&
                   strstr(log, "--target chibi-compiler-host") &&
                   strstr(log, "--target chibi --target chibi-build-host") &&
                   strstr(log, "--asset-generator-hosts") &&
                   strstr(log, "third-party:") && strstr(log, "compile-commands:");
    if (!planned) {
        fprintf(stderr, "unexpected sanitizer plan: %s\n", log);
    }
    free(log);
    REQUIRE(planned);
    const char *const optimized[] = { "--release", "--target",
                                      "chibi",     "--debug-optimization",
                                      "1",         "--debug-frame-pointers",
                                      "on",        NULL };
    REQUIRE(driver_plan(optimized, &log));
    planned = strstr(log, "--profile release") &&
              !strstr(log, "--profile sanitizer") &&
              !strstr(log, "--debug-optimization") &&
              !strstr(log, "--debug-frame-pointers") &&
              !strstr(log, "--asset-generator-hosts") &&
              strstr(log, "build-release/compile_commands.json");
    if (!planned) {
        fprintf(stderr, "unexpected release plan: %s\n", log);
    }
    free(log);
    REQUIRE(planned);
    const char *const everything[] = { "--debug",
                                       "--all",
                                       "--debug-optimization",
                                       "1",
                                       "--debug-frame-pointers",
                                       "on",
                                       NULL };
    REQUIRE(driver_plan(everything, &log));
    planned = strstr(log, "--profile debug") && !strstr(log, "--target chibi ") &&
              strstr(log, "--debug-optimization 1") &&
              strstr(log, "--debug-frame-pointers on");
    free(log);
    REQUIRE(planned);
    return true;
}

/* `chibi build VERB` runs one of the driver's verbs. `test` runs the selectors
 * of tools/tests.tsv; --list names them and --dry-run prints the command each
 * selector would run. */
static bool driver_verb(const char *verb, const char *const *options, char **log)
{
    const char *argv[24] = { host_path,
                             "--root",
                             CHIBI_SOURCE_ROOT,
                             "--source",
                             "tools/toolchain/Driver.chibi",
                             "--core",
                             "packages/core",
                             "--entry",
                             "Main",
                             "--timeout-ms",
                             "50000",
                             "--",
                             "build",
                             verb };
    int count = 14;
    for (int i = 0; options[i]; i++) {
        assert(count < 23);
        argv[count++] = options[i];
    }
    argv[count] = NULL;
    int status = tool_run_within(argv, CHIBI_SOURCE_ROOT, log_path, GRAPH_RUN_SECONDS);
    *log = tool_read(log_path);
    return *log && status == 0;
}

TEST_CASE(driver_lists_and_plans_test_selectors)
{
    char *log = NULL;
    const char *const list[] = { "--list", NULL };
    REQUIRE(driver_verb("test", list, &log));
    bool listed = strstr(log, "tool chibi-cli") &&
                  strstr(log, "group native") && strstr(log, "group language");
    free(log);
    REQUIRE(listed);
    const char *const plan[] = { "--dry-run",       "--build", "out",
                                 "--release-build", "rel",     "chibi-cli",
                                 "projects",        NULL };
    REQUIRE(driver_verb("test", plan, &log));
    bool planned = strstr(log, "chibi-cli: env ") &&
                   strstr(log, "/out/test/tools/test_tools --chibi-cli /") &&
                   strstr(log, "CHIBI_CLI=") && strstr(log, "/rel/sdk/chibi ");
    if (!planned) {
        fprintf(stderr, "unexpected test plan: %s\n", log);
    }
    free(log);
    REQUIRE(planned);
    const char *const unknown[] = { "no-such-selector", NULL };
    REQUIRE(!driver_verb("test", unknown, &log));
    bool rejected = log && strstr(log, "unknown selector 'no-such-selector'");
    free(log);
    REQUIRE(rejected);
    return true;
}

TEST_CASE(driver_plans_the_other_verbs)
{
    const char *const install[] = { "--dry-run", "--prefix", "/prefix", NULL };
    char *log = NULL;
    REQUIRE(driver_verb("install", install, &log));
    bool planned = strstr(log, "--profile release") &&
                   strstr(log, "--entry Install") && strstr(log, "--prefix /prefix");
    free(log);
    REQUIRE(planned);
    const char *const refresh[] = { "--dry-run", "--release-build", "rel", NULL };
    REQUIRE(driver_verb("bootstrap-refresh", refresh, &log));
    planned = strstr(log, "/rel/sdk/chibi-compiler-host --bootstrap tools/compiler/bootstrap") &&
              strstr(log, "--recover ") &&
              strstr(log, "/rel/bootstrap.refresh/compile.json") &&
              strstr(log, "--seed ") && strstr(log, "--revision ");
    free(log);
    REQUIRE(planned);
    const char *const reflect[] = { "--dry-run", "--libclang-root", "/llvm",
                                    "--",        "-DFLAG=1",        NULL };
    REQUIRE(driver_verb("sdk-reflect", reflect, &log));
    planned = strstr(log, "--entry SDKReflect") &&
              strstr(log, "--libclang-root /llvm") && strstr(log, "-- -DFLAG=1");
    free(log);
    REQUIRE(planned);
    const char *const format[] = { "--dry-run", "--check", NULL };
    REQUIRE(driver_verb("format", format, &log));
    planned = strstr(log, "format: sh tools/clang-format.sh --check");
    free(log);
    REQUIRE(planned);
    const char *const clean[] = { "--dry-run", "--build", "out", NULL };
    REQUIRE(driver_verb("clean", clean, &log));
    planned = strstr(log, "clean: rm -rf -- ") && strstr(log, "/out");
    free(log);
    REQUIRE(planned);
    const char *const unknown[] = { "--dry-run", NULL };
    REQUIRE(!driver_verb("no-such-verb", unknown, &log));
    free(log);
    return true;
}

/* A separate route: another commands-only run of the whole graph takes
 * seconds. */
void test_build_graph_programs(const char *host)
{
    set_up(host);
    RUN_TEST_CASE(named_programs_build_what_they_run);
    RUN_TEST_CASE(driver_plans_the_engine_build_steps);
    RUN_TEST_CASE(driver_lists_and_plans_test_selectors);
    RUN_TEST_CASE(driver_plans_the_other_verbs);
    RUN_TEST_CASE(invalid_arguments_fail_before_writing_commands);
    RUN_TEST_CASE(debug_optimization_preserves_instrumentation);
    RUN_TEST_CASE(debug_optimization_rejects_invalid_profiles);
    RUN_TEST_CASE(graph_batch_stops_on_unexpected_result);
    RUN_TEST_CASE(graph_batch_transcripts_are_case_local);
    RUN_TEST_CASE(authored_dependency_files_list_prerequisites);
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

/* A separate route: reflect's compiles take most of a minute's ceiling. */
void test_build_graph_reflect(const char *host)
{
    set_up(host);
    RUN_TEST_CASE(sdk_reflection_rejects_invalid_arguments_before_building);
    tool_remove_tree(suite);
}

TEST_CASE(failed_steps_name_only_the_output)
{
    char source[GRAPH_PATH], output[GRAPH_PATH], record[GRAPH_PATH];
    join(source, suite, "FailedStep.chibi");
    join(output, suite, "expected-step-output.c");
    int size = snprintf(record, sizeof record, "%s.chibi-record", output);
    REQUIRE(size > 0 && size < (int)sizeof record);
    tool_write(
        source,
        "package step_fixture\nimport c \"graph_c:.\"\n"
        "Build :: () -> int { arguments := build.arguments(); "
        "if (c.Step([\"/bin/sh\", \"-c\", \"exit 7\", "
        "\"unlogged-command-token\"], [], arguments[0]) != -1) { return 1; } "
        "return 0; }\n"
    );
    const char *const argv[] = { host_path,
                                 "--root",
                                 CHIBI_SOURCE_ROOT,
                                 "--source",
                                 source,
                                 "--core",
                                 "packages/core",
                                 "--collection",
                                 "graph_c=tools/graph/c",
                                 "--entry",
                                 "Build",
                                 "--timeout-ms",
                                 "30000",
                                 "--",
                                 output,
                                 NULL };
    REQUIRE(tool_run_within(argv, CHIBI_SOURCE_ROOT, log_path, 30) == 0);
    char *log = tool_read(log_path);
    REQUIRE(log);
    char expected[GRAPH_PATH + 32];
    snprintf(expected, sizeof expected, "graph: step failed: %s\n", output);
    bool exact = !strcmp(log, expected);
    printf("controlled step diagnostic: %s", log);
    free(log);
    REQUIRE(exact);
    REQUIRE(access(output, F_OK) != 0);
    REQUIRE(access(record, F_OK) != 0);
    return true;
}

/* The compiler and linked program are real. Only the partial-output failure
 * uses a controlled driver, so quoting and the host's argv limit stay tested
 * independently of the response-file implementation. */
TEST_CASE(link_response_files_preserve_commands_and_recover)
{
    char source[GRAPH_PATH], directory[GRAPH_PATH], libraries[GRAPH_PATH],
        program[GRAPH_PATH], partial[GRAPH_PATH];
    join(directory, suite, "quoted '\"\\ café 雪 directory");
    REQUIRE(!mkdir(directory, 0700));
    char name[201];
    memset(name, 'l', sizeof name - 1);
    name[sizeof name - 1] = 0;
    join(libraries, directory, name);
    REQUIRE(!mkdir(libraries, 0700));
    join(source, suite, "link.c");
    tool_write(source, "int main(void) { return 37; }\n");
    join(partial, suite, "partial-driver");
    tool_write(partial, "#!/bin/sh\nprintf partial > \"${0%/*}/partial-output\"\nexit 9\n");
    REQUIRE(!chmod(partial, 0700));
    join(partial, suite, "record-driver");
    tool_write(
        partial,
        "#!/bin/sh\nset -eu\n"
        "printf '%s\\n' \"$@\" > \"${0%/*}/transport-argv\"\n"
        "IFS= read -r compiler < \"$0.compiler\"\n"
        "exec \"$compiler\" \"$@\"\n"
    );
    REQUIRE(!chmod(partial, 0700));
    join(program, CHIBI_SOURCE_ROOT, "test/tools/GraphLinkResponse.chibi");
    const char *argv[] = {
        host_path,       "--root",       CHIBI_SOURCE_ROOT,
        "--source",      program,        "--core",
        "packages/core", "--collection", "graph_c=tools/graph/c",
        "--entry",       "Build",        "--timeout-ms",
        "50000",         "--",           suite,
        directory,       libraries,      NULL
    };
    REQUIRE(tool_run_within(argv, CHIBI_SOURCE_ROOT, log_path, GRAPH_RUN_SECONDS) == 0);
    char database[GRAPH_PATH];
    join(database, suite, "link_commands.json");
    yyjson_doc *doc = yyjson_read_file(database, 0, NULL, NULL);
    REQUIRE(doc);
    yyjson_val *arguments = yyjson_obj_get(yyjson_doc_get_root(doc), "arguments");
    bool original_argv = yyjson_is_arr(arguments) && yyjson_arr_size(arguments) == 405;
    yyjson_doc_free(doc);
    REQUIRE(original_argv);
    join(database, suite, "empty_link_commands.json");
    doc = yyjson_read_file(database, 0, NULL, NULL);
    REQUIRE(doc);
    arguments = yyjson_obj_get(yyjson_doc_get_root(doc), "arguments");
    yyjson_val *empty = yyjson_arr_get(arguments, 2);
    bool retained_empty = yyjson_arr_size(arguments) == 6 &&
                          yyjson_is_str(empty) && !yyjson_get_len(empty);
    yyjson_doc_free(doc);
    REQUIRE(retained_empty);
    join(database, suite, "operand_link_commands.json");
    doc = yyjson_read_file(database, 0, NULL, NULL);
    REQUIRE(doc);
    arguments = yyjson_obj_get(yyjson_doc_get_root(doc), "arguments");
    bool retained_operand = yyjson_arr_size(arguments) == 16 &&
                            !strcmp(yyjson_get_str(yyjson_arr_get(arguments, 10)), "-B") &&
                            !strcmp(yyjson_get_str(yyjson_arr_get(arguments, 11)), "") &&
                            !strcmp(yyjson_get_str(yyjson_arr_get(arguments, 12)), "");
    yyjson_doc_free(doc);
    REQUIRE(retained_operand);
    char *log = tool_read(log_path);
    REQUIRE(log && strstr(log, "response link: 405 arguments,") && strstr(log, "empty link: direct "));
    fputs(log, stdout);
    free(log);
    return true;
}

void test_build_graph_link_response(const char *host)
{
    assert(strlen(host) < sizeof host_path);
    strcpy(host_path, host);
    strcpy(suite, "/tmp/chibi-link-response-XXXXXX");
    assert(mkdtemp(suite));
    join(log_path, suite, "link.log");
    RUN_TEST_CASE(link_response_files_preserve_commands_and_recover);
    tool_remove_tree(suite);
}

/* Source-registration guard only. Actual success/fallback/failure emitted
 * traces have separate integration assertions; this scan does not execute
 * Generate or prove that any report is reached at runtime. */
TEST_CASE(asset_generator_source_lists_bounded_ordered_phases)
{
    char filename[GRAPH_PATH];
    join(filename, CHIBI_SOURCE_ROOT, "tools/graph/asset_compiler/Generate.chibi");
    char *source = tool_read(filename);
    REQUIRE(source);
    const char *phases[] = { "asset compiler: entered generator",
                             "asset compiler: serialize facade request",
                             "asset compiler: invoke facade compiler",
                             "asset compiler: facade ready",
                             "asset compiler: serialize target request",
                             "asset compiler: invoke target compiler",
                             "asset compiler: serialize packaging request",
                             "asset compiler: invoke packaging",
                             "asset compiler: packaging ready",
                             "asset compiler: embed artifact",
                             "asset compiler: generation complete" };
    const char *next = source;
    size_t bytes = 0;
    bool ordered = true;
    for (size_t i = 0; ordered && i < sizeof phases / sizeof *phases; i++) {
        const char *found = strstr(next, phases[i]);
        ordered = found != NULL;
        if (found) {
            next = found + strlen(phases[i]);
            bytes += strlen(phases[i]) + 1;
        }
    }
    free(source);
    REQUIRE(ordered);
    /* Allow an entire signed 32-bit status instead of the two-byte {}. */
    REQUIRE(bytes + 9 < 1024);
    return true;
}

TEST_CASE(discovery_without_a_compiler_driver)
{
    char root[GRAPH_PATH], driver_root[GRAPH_PATH], driver[GRAPH_PATH],
        resource[GRAPH_PATH], script[GRAPH_PATH + 128];
    join(root, suite, "toolchain");
    assert(!mkdir(root, 0700));
    join(driver_root, root, "multiarch driver");
    assert(!mkdir(driver_root, 0700));
    join(driver, driver_root, "bin");
    assert(!mkdir(driver, 0700));
    join(driver, driver_root, "bin/clang");
    join(resource, driver_root, "resource-directory");
    snprintf(script, sizeof script, "#!/bin/sh\ntest \"$1\" = -print-resource-dir || exit 1\ncat \"%s\"\n", resource);
    tool_write(driver, script);
    assert(!chmod(driver, 0700));
    const char *const argv[] = { host_path,
                                 "--root",
                                 CHIBI_SOURCE_ROOT,
                                 "--source",
                                 "test/tools/GraphDiscovery.chibi",
                                 "--core",
                                 "packages/core",
                                 "--collection",
                                 "graph=tools/graph",
                                 "--entry",
                                 "Verify",
                                 "--timeout-ms",
                                 "30000",
                                 "--",
                                 root,
                                 NULL };
    const char *names[] = { "CXX", "CMAKE_LIBRARY_PATH", "CMAKE_INCLUDE_PATH" };
    char *saved[3];
    for (int i = 0; i < 3; i++) {
        saved[i] = getenv(names[i]) ? strdup(getenv(names[i])) : NULL;
    }
    char path[GRAPH_PATH];
    assert(!unsetenv("CXX"));
    join(path, root, "lib");
    assert(!setenv("CMAKE_LIBRARY_PATH", path, 1));
    join(path, root, "include");
    assert(!setenv("CMAKE_INCLUDE_PATH", path, 1));
    int status = tool_run_within(argv, CHIBI_SOURCE_ROOT, log_path, 30);
    for (int i = 0; i < 3; i++) {
        assert(saved[i] ? !setenv(names[i], saved[i], 1) : !unsetenv(names[i]));
        free(saved[i]);
    }
    REQUIRE(status == 0);
    return true;
}

TEST_CASE(libclang_discovery_honors_environment_prefixes)
{
    char root[GRAPH_PATH], relative[GRAPH_PATH], prefixes[GRAPH_PATH * 5],
        includes[GRAPH_PATH * 2], libraries[GRAPH_PATH * 2];
    snprintf(relative, sizeof relative, ".chibi/build-graph-discovery/%s", strrchr(suite, '/') + 1);
    join(root, CHIBI_SOURCE_ROOT, relative);
    snprintf(
        prefixes,
        sizeof prefixes,
        "CMAKE_PREFIX_PATH=:%s/missing prefix::%s/first prefix:%s/second "
        "prefix:%s/semicolon;prefix:",
        relative,
        relative,
        relative,
        relative
    );
    snprintf(includes, sizeof includes, "CMAKE_INCLUDE_PATH=%s/direct prefix/include/", relative);
    snprintf(libraries, sizeof libraries, "CMAKE_LIBRARY_PATH=%s/direct prefix/lib64/", relative);
    const char *const argv[] = { "env",
                                 prefixes,
                                 includes,
                                 libraries,
                                 host_path,
                                 "--root",
                                 CHIBI_SOURCE_ROOT,
                                 "--source",
                                 "test/tools/GraphLibclangDiscovery.chibi",
                                 "--core",
                                 "packages/core",
                                 "--collection",
                                 "graph=tools/graph",
                                 "--entry",
                                 "Verify",
                                 "--timeout-ms",
                                 "30000",
                                 "--",
                                 root,
                                 NULL };
    int status = tool_run_within(argv, CHIBI_SOURCE_ROOT, log_path, 30);
    if (!status) {
        char source[GRAPH_PATH], core[GRAPH_PATH], compiler_root[GRAPH_PATH],
            collection[GRAPH_PATH + 32];
        join(source, CHIBI_SOURCE_ROOT, "test/tools/GraphLibclangDiscovery.chibi");
        join(core, CHIBI_SOURCE_ROOT, "packages/core");
        join(compiler_root, CHIBI_SOURCE_ROOT, "tools/compiler");
        snprintf(collection, sizeof collection, "graph=%s/tools/graph", CHIBI_SOURCE_ROOT);
        const char *const spelling[] = { host_path,
                                         "--root",
                                         root,
                                         "--source",
                                         source,
                                         "--core",
                                         core,
                                         "--compiler",
                                         compiler_root,
                                         "--compiler-core",
                                         core,
                                         "--collection",
                                         collection,
                                         "--entry",
                                         "VerifyPathSpelling",
                                         "--timeout-ms",
                                         "30000",
                                         NULL };
        status = tool_run_within(spelling, CHIBI_SOURCE_ROOT, log_path, 30);
    }
    if (status) {
        char *log = tool_read(log_path);
        fprintf(stderr, "libclang discovery: %s\n", log ? log : "");
        free(log);
    }
    tool_remove_tree(root);
    REQUIRE(!status);
    return true;
}

TEST_CASE(libclang_discovery_honors_basename_paths)
{
    static const struct
    {
        const char *include, *library, *directory, *prefix;
        bool linked_include;
    } cases[] = { { "include/", "", "lib64", NULL, false },
                  { "", "lib64", "lib64", NULL, false },
                  { "", "lib", "lib", NULL, false },
                  { "include", "lib64", "lib64", NULL, true },
                  { "", "lib/x86_64-linux-gnu/", "lib", NULL, false },
                  { "lib/llvm-18/include", "", "lib64", "lib/llvm-18", false },
                  { "", "lib/llvm-18/lib", "lib", "lib/llvm-18", false },
                  { "", "lib/llvm-18/lib64", "lib64", "lib/llvm-18", false } };
    char source[GRAPH_PATH], core[GRAPH_PATH], compiler_root[GRAPH_PATH],
        collection[GRAPH_PATH + 32];
    join(source, CHIBI_SOURCE_ROOT, "test/tools/GraphLibclangDiscovery.chibi");
    join(core, CHIBI_SOURCE_ROOT, "packages/core");
    join(compiler_root, CHIBI_SOURCE_ROOT, "tools/compiler");
    snprintf(collection, sizeof collection, "graph=%s/tools/graph", CHIBI_SOURCE_ROOT);
    for (int i = 0; i < (int)(sizeof cases / sizeof *cases); i++) {
        char root[GRAPH_PATH], canonical[GRAPH_PATH], name[64],
            path[GRAPH_PATH], includes[256], libraries[256];
        snprintf(name, sizeof name, "basename paths %d", i);
        join(root, suite, name);
        assert(!mkdir(root, 0700));
        REQUIRE(realpath(root, canonical));
        if (cases[i].linked_include) {
            join(path, root, "headers");
            assert(!mkdir(path, 0700));
            join(path, root, "include");
            assert(!symlink("headers", path));
        }
        snprintf(includes, sizeof includes, "CMAKE_INCLUDE_PATH=%s", cases[i].include);
        snprintf(libraries, sizeof libraries, "CMAKE_LIBRARY_PATH=%s", cases[i].library);
        const char *const argv[] = { "env",
                                     "CMAKE_PREFIX_PATH=",
                                     includes,
                                     libraries,
                                     host_path,
                                     "--root",
                                     canonical,
                                     "--source",
                                     source,
                                     "--core",
                                     core,
                                     "--compiler",
                                     compiler_root,
                                     "--compiler-core",
                                     core,
                                     "--collection",
                                     collection,
                                     "--entry",
                                     "VerifyBasenamePaths",
                                     "--timeout-ms",
                                     "30000",
                                     "--",
                                     cases[i].directory,
                                     cases[i].prefix,
                                     NULL };
        int status = tool_run_within(argv, CHIBI_SOURCE_ROOT, log_path, 30);
        if (status) {
            char *log = tool_read(log_path);
            fprintf(stderr, "libclang basename case %d: %s\n", i, log ? log : "");
            free(log);
        }
        REQUIRE(!status);
    }
    return true;
}

/* A separate route: reflect's run and the codec compile take seconds. */
void test_build_graph_generated(const char *host)
{
    set_up(host);
    RUN_TEST_CASE(discovery_without_a_compiler_driver);
    RUN_TEST_CASE(libclang_discovery_honors_environment_prefixes);
    RUN_TEST_CASE(libclang_discovery_honors_basename_paths);
    RUN_TEST_CASE(failed_steps_name_only_the_output);
    RUN_TEST_CASE(asset_generator_source_lists_bounded_ordered_phases);
    tool_remove_tree(suite);
}
