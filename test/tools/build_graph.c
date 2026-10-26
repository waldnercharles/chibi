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
#define GRAPH_RUN_SECONDS 50
#define GRAPH_TOKENS 1024
#define GRAPH_OWNERS 256
#define GRAPH_HEADERS 9

static char host_path[4096];
static char reference[GRAPH_PATH], suite[GRAPH_PATH], built[GRAPH_PATH],
    clone[GRAPH_PATH], log_path[GRAPH_PATH];
/* The profile root CMakeLists.txt read from its cache, as graph options. */
static char compiler[GRAPH_PATH], cxx_compiler[GRAPH_PATH], compiler_id[64],
    build_type[64], architecture[64], sysroot[GRAPH_PATH], sanitizers[128],
    debug_dir[GRAPH_PATH], archiver[GRAPH_PATH], ranlib[GRAPH_PATH],
    third_party[GRAPH_PATH];
static bool warnings;
static char debug_optimization[2];
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
/* The text of CMake's CMakeLANGCompiler.cmake for a language, C or CXX. */
static char *compiler_file(const char *language)
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
            "%s/%s/CMake%sCompiler.cmake",
            directory,
            entry->d_name,
            language
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
    char *file = compiler_file("C");
    compiler_value(file, "CMAKE_C_COMPILER", compiler, sizeof compiler);
    char *cxx_file = compiler_file("CXX");
    compiler_value(cxx_file, "CMAKE_CXX_COMPILER", cxx_compiler, sizeof cxx_compiler);
    free(cxx_file);
    compiler_value(file, "CMAKE_C_COMPILER_ID", compiler_id, sizeof compiler_id);
    cache_value(cache, "CMAKE_BUILD_TYPE", build_type, sizeof build_type);
    debug_optimization[0] = 0;
    cache_value(cache, "CMAKE_C_FLAGS_DEBUG", value, sizeof value);
    if (!strcmp(build_type, "Debug") &&
        (!strcmp(value, "-g -O0") || !strcmp(value, "-g -O1"))) {
        debug_optimization[0] = value[strlen(value) - 1];
        debug_optimization[1] = 0;
    }
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
    cache_value(cache, "CHIBI_THIRD_PARTY_DIR", third_party, sizeof third_party);
    ccache_debug_dir(cache);
    free(file);
    free(cache);
}

/* The libclang the configured build found, which reflect's commands name. */
static void libclang(char include[GRAPH_PATH], char library[GRAPH_PATH])
{
    char path[GRAPH_PATH];
    join(path, reference, "CMakeCache.txt");
    char *cache = tool_read(path);
    assert(cache);
    cache_value(cache, "LIBCLANG_INCLUDE_DIR", include, GRAPH_PATH);
    cache_value(cache, "LIBCLANG_LIBRARY", library, GRAPH_PATH);
    free(cache);
}

/* Runs the graph's Libraries entry over root into build_dir with the options
 * after it; the number of sources it compiled, 0 when it only wrote commands,
 * or -1 when it failed. */
/* Runs the graph's Libraries entry over root into build_dir with the options
 * in options and then extra; the number of sources it compiled, 0 when it
 * only wrote commands, or -1 when it failed. */
static int run_graph(
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
/* A run with every profile value given, so nothing is discovered, and no
 * launcher. */
static int graph(const char *root, const char *build_dir, bool warn, const char *const *extra)
{
    const char *const options[] = {
        "--compiler",        compiler,        "--cxx-compiler",
        cxx_compiler,        "--compiler-id", compiler_id,
        "--build-type",      build_type,      "--warnings",
        warn ? "on" : "off", "--archiver",    archiver,
        "--ranlib",          ranlib,          "--architecture",
        architecture,        "--sysroot",     sysroot,
        "--sanitizers",      sanitizers,      "--debug-compilation-dir",
        debug_dir,           "--launcher",    "",
        "--third-party",     third_party,
        debug_optimization[0] ? "--debug-optimization" : NULL,
        debug_optimization, NULL,
    };
    return run_graph(root, build_dir, options, extra);
}
/* A run that names only the Makefile profile the configured build used, with
 * its compiler as CC, so the graph finds the rest. The debug directory depends
 * on where the build is, so it is given. */
static int discovered_graph(const char *root, const char *build_dir, const char *const *extra)
{
    const char *profile = sanitizers[0]                  ? "sanitizer"
                        : !strcmp(build_type, "Release") ? "release"
                                                         : "debug";
    const char *const options[] = {
        "--profile", profile,      "--debug-compilation-dir",
        debug_dir,   "--warnings", warnings ? "on" : "off",
        debug_optimization[0] ? "--debug-optimization" : NULL,
        debug_optimization, NULL
    };
    /* Without CXX, the C++ compiler is the one beside CC, as CMake's. */
    char *saved = getenv("CC") ? strdup(getenv("CC")) : NULL;
    char *saved_cxx = getenv("CXX") ? strdup(getenv("CXX")) : NULL;
    assert(!setenv("CC", compiler, 1) && !unsetenv("CXX"));
    int compiled = run_graph(root, build_dir, options, extra);
    if (saved) {
        assert(!setenv("CC", saved, 1));
    } else {
        assert(!unsetenv("CC"));
    }
    free(saved);
    if (saved_cxx) {
        assert(!setenv("CXX", saved_cxx, 1));
    }
    free(saved_cxx);
    return compiled;
}
static const char *const support_only[] = { "--target", "chibi_support", NULL };

/* The next word of a command line as CMake's Unix escaping writes it: double
 * quotes group, and a backslash takes the next byte literally. Writes the word
 * over the text in place and returns it, or NULL at the end. */
static char *shell_word(char **cursor)
{
    char *in = *cursor;
    while (*in == ' ') {
        in++;
    }
    if (!*in) {
        return NULL;
    }
    char *word = in, *out = in;
    bool quoted = false;
    for (; *in && (quoted || *in != ' '); in++) {
        if (*in == '\\' && in[1]) {
            *out++ = *++in;
        } else if (*in == '"') {
            quoted = !quoted;
        } else {
            *out++ = *in;
        }
    }
    *cursor = *in ? in + 1 : in;
    *out = 0;
    return word;
}

/* Whether the graph's arguments match CMake's command, except for -o's value,
 * with the graph's build directory standing for CMake's. CMake writes the
 * command for the shell, so `-DName="two words"` is one argument. */
static bool same_command(yyjson_val *ours, char *theirs)
{
    size_t built_length = strlen(built), index = 0;
    const char *previous = "";
    char *cursor = theirs;
    for (char *token; (token = shell_word(&cursor)); index++) {
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

/* Every compile command the graph writes, from the profile it finds, equals
 * CMake's for the same source. */
TEST_CASE(commands_match_cmake)
{
    /* Only the profile's name: the graph finds its compiler, SDK, libclang and
     * flags as CMake did. */
    const char *const commands_only[] = { "--commands-only", "on", NULL };
    REQUIRE(discovered_graph(CHIBI_SOURCE_ROOT, built, commands_only) == 0);
    char path[GRAPH_PATH];
    join(path, built, "compile_commands.json");
    yyjson_doc *graph_doc = yyjson_read_file(path, 0, NULL, NULL);
    join(path, reference, "compile_commands.json");
    yyjson_doc *cmake_doc = yyjson_read_file(path, 0, NULL, NULL);
    REQUIRE(graph_doc && cmake_doc);
    yyjson_val *ours = yyjson_doc_get_root(graph_doc);
    /* The libraries the graph owns are those its objects belong to. */
    char owners[GRAPH_OWNERS][128];
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
            assert(owner_count < GRAPH_OWNERS);
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
            /* A source two targets compile is compared with its own target's. */
            const char *objects = strstr(yyjson_get_str(yyjson_obj_get(mine, "output")), "/objects/");
            char owner[128];
            snprintf(owner, sizeof owner, "/%.*s.dir/", (int)strcspn(objects + 9, "/"), objects + 9);
            if (strcmp(expected_file, file) || !strstr(command, owner)) {
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

/* An executable publishes no headers, so building one leaves every header
 * its libraries staged in place. A compiler that always fails stops the run
 * after staging. */
TEST_CASE(executables_stage_no_headers)
{
    char build_dir[GRAPH_PATH], header[GRAPH_PATH];
    join(build_dir, suite, "launcher");
    /* The configured profile, so the run stages from the archive CMake
     * unpacked; Option takes the first --compiler. */
    const char *const failing[] = { "--compiler",
                                    "/usr/bin/false",
                                    "--compiler-id",
                                    compiler_id,
                                    "--sanitizers",
                                    sanitizers,
                                    "--third-party",
                                    third_party,
                                    "--archiver",
                                    archiver,
                                    "--ranlib",
                                    ranlib,
                                    NULL };
    const char *const launcher_only[] = { "--target", "chibi", NULL };
    REQUIRE(run_graph(CHIBI_SOURCE_ROOT, build_dir, failing, launcher_only) == -1);
    join(header, build_dir, "sdk/public/support/support/arena.h");
    REQUIRE(!access(header, R_OK));
    return true;
}

/* A variable of the `build NAME:` statement in CMake's build.ninja, as an
 * owned copy the caller splits into tokens. */
static char *ninja_variable(const char *ninja, const char *name, const char *variable)
{
    char head[256], key[128];
    snprintf(head, sizeof head, "\nbuild %s: ", name);
    snprintf(key, sizeof key, "\n  %s = ", variable);
    const char *block = strstr(ninja, head);
    assert(block);
    const char *end = strstr(block + 1, "\n\n");
    /* A variable the statement does not set is empty, as LINK_LIBRARIES is
     * for an executable that links no library. */
    const char *at = strstr(block, key);
    if (!at || (end && at > end)) {
        return strdup("");
    }
    at += strlen(key);
    return strndup(at, strcspn(at, "\n"));
}
static int split_into(char *text, char *tokens[GRAPH_TOKENS], int count)
{
    for (char *token = strtok(text, " "); token; token = strtok(NULL, " ")) {
        assert(count < GRAPH_TOKENS);
        tokens[count++] = token;
    }
    return count;
}
static const char *base(const char *path)
{
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

/* Whether a link command of the graph's equals CMake's build.ninja statement
 * for the executable at path: the compiler, FLAGS, LINK_FLAGS, the objects, -o
 * and the output, then LINK_LIBRARIES, apart from where objects, the output
 * and archives sit. */
static bool archive(const char *path)
{
    size_t size = strlen(path);
    return size > 2 && !strcmp(path + size - 2, ".a");
}
static bool same_link(const char *ninja, const char *path, char *const *ours, int ours_count)
{
    char *flags = ninja_variable(ninja, path, "FLAGS"),
         *link_flags = ninja_variable(ninja, path, "LINK_FLAGS"),
         *libraries = ninja_variable(ninja, path, "LINK_LIBRARIES"),
         *theirs[GRAPH_TOKENS];
    char head[GRAPH_PATH];
    snprintf(head, sizeof head, "\nbuild %s: ", path);
    const char *statement = strstr(ninja, head);
    assert(statement);
    /* CMake links with the C++ compiler when a library has C++ sources. */
    bool cxx = !strncmp(statement + strlen(head), "CXX_EXECUTABLE_LINKER", 21);
    int count = 0;
    theirs[count++] = cxx ? cxx_compiler : compiler;
    count = split_into(flags, theirs, count);
    count = split_into(link_flags, theirs, count);
    int objects = count;
    char *line = strndup(statement + 1, strcspn(statement + 1, "\n"));
    for (char *token = strtok(line, " "); token && strcmp(token, "|");
         token = strtok(NULL, " ")) {
        if (strstr(token, ".o")) {
            assert(count < GRAPH_TOKENS);
            theirs[count++] = token;
        }
    }
    int objects_end = count;
    assert(count + 2 < GRAPH_TOKENS);
    theirs[count++] = "-o";
    theirs[count++] = (char *)path;
    count = split_into(libraries, theirs, count);
    bool same = ours_count == count;
    for (int i = 0; same && i < count; i++) {
        bool placed = (i >= objects && i < objects_end) ||
                      (i && !strcmp(theirs[i - 1], "-o")) || archive(theirs[i]);
        same = placed ? !strcmp(base(ours[i]), base(theirs[i]))
                      : !strcmp(ours[i], theirs[i]);
        if (!same) {
            fprintf(stderr, "graph link argument %d of %s: %s, CMake: %s\n", i, path, ours[i], theirs[i]);
        }
    }
    if (ours_count != count) {
        fprintf(stderr, "graph link of %s has %d arguments, CMake's %d\n", path, ours_count, count);
    }
    free(line);
    free(flags);
    free(link_flags);
    free(libraries);
    return same;
}

/* Naming test_tools and the launcher builds the programs they run, as CMake's
 * add_dependencies does (test_tools compiles their paths in, and the launcher
 * runs the compiler and player beside it), and no other program. */
TEST_CASE(named_programs_build_what_they_run)
{
#ifdef __APPLE__
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
    static const char *const programs[] = { "test/tools/test_tools",
                                            "test/tools/policy_worker",
                                            "bin/chibi_ctl",
                                            "sdk/chibi-compile",
                                            "sdk/chibi-build-host",
                                            "sdk/chibi-compiler-host",
                                            "sdk/chibi",
                                            "sdk/chibi-player" };
    int found = 0;
    yyjson_val *entry;
    yyjson_arr_iter iter = yyjson_arr_iter_with(yyjson_doc_get_root(doc));
    while ((entry = yyjson_arr_iter_next(&iter))) {
        const char *output = yyjson_get_str(yyjson_obj_get(entry, "output"));
        REQUIRE(strcmp(base(output), "test_host"));
        for (int i = 0; i < 8; i++) {
            snprintf(path, sizeof path, "%s/%s", build_dir, programs[i]);
            found += !strcmp(output, path);
        }
    }
    yyjson_doc_free(doc);
    REQUIRE(found == 8);
#endif
    return true;
}

/* Every link command the graph writes, from the profile it finds, equals
 * CMake's for the same executable. Runs after commands_match_cmake, which
 * writes them. */
TEST_CASE(links_match_cmake)
{
    char path[GRAPH_PATH];
    join(path, reference, "build.ninja");
    char *ninja = tool_read(path);
    join(path, built, "link_commands.json");
    yyjson_doc *doc = yyjson_read_file(path, 0, NULL, NULL);
    REQUIRE(ninja && doc);
    int matched = 0;
    yyjson_val *entry;
    yyjson_arr_iter iter = yyjson_arr_iter_with(yyjson_doc_get_root(doc));
    while ((entry = yyjson_arr_iter_next(&iter))) {
        const char *name = base(yyjson_get_str(yyjson_obj_get(entry, "output")));
        /* The output CMake's link statement for the executable names, found
         * by its linker rule, since a bare name may be a phony alias. */
        char rule[GRAPH_PATH], cmake_path[GRAPH_PATH];
        snprintf(rule, sizeof rule, "_EXECUTABLE_LINKER__%s_%s ", name, build_type);
        const char *found = strstr(ninja, rule);
        REQUIRE(found);
        const char *line = found;
        while (line > ninja && line[-1] != '\n') {
            line--;
        }
        REQUIRE(!strncmp(line, "build ", 6));
        line += strlen("build ");
        snprintf(cmake_path, sizeof cmake_path, "%.*s", (int)strcspn(line, ":"), line);
        yyjson_val *arguments = yyjson_obj_get(entry, "arguments");
        char *ours[GRAPH_TOKENS];
        int count = 0;
        yyjson_val *argument;
        yyjson_arr_iter arguments_iter = yyjson_arr_iter_with(arguments);
        while ((argument = yyjson_arr_iter_next(&arguments_iter))) {
            assert(count < GRAPH_TOKENS);
            ours[count++] = (char *)yyjson_get_str(argument);
        }
        matched += same_link(ninja, cmake_path, ours, count);
    }
    fprintf(
        stderr,
        "graph links: %d of %zu match CMake's\n",
        matched,
        yyjson_arr_size(yyjson_doc_get_root(doc))
    );
/* reflect, the build host, the compiler host, the launcher and four test
 * drivers, and on macOS the player, chibi-compile, chibi_ctl, test_language
 * and test_tools. */
#ifdef __APPLE__
    size_t programs = 13;
#else
    size_t programs = 8;
#endif
    REQUIRE((size_t)matched == programs && yyjson_arr_size(yyjson_doc_get_root(doc)) == programs);
    yyjson_doc_free(doc);
    free(ninja);
    return true;
}

/* The graph links reflect with CMake's command, and the program it links
 * writes the same reflected sources. */
TEST_CASE(reflect_links_as_cmake_and_writes_the_same_sources)
{
    char path[GRAPH_PATH], include[GRAPH_PATH], library[GRAPH_PATH],
        build_dir[GRAPH_PATH];
    libclang(include, library);
    join(build_dir, suite, "reflect");
    const char *const reflect_only[] = { "--target",
                                         "reflect",
                                         "--target",
                                         "chibi_math",
                                         "--libclang-include",
                                         include,
                                         "--libclang-library",
                                         library,
                                         NULL };
    REQUIRE(graph(CHIBI_SOURCE_ROOT, build_dir, warnings, reflect_only) > 0);
    REQUIRE(graph(CHIBI_SOURCE_ROOT, build_dir, warnings, reflect_only) == 0);
    join(path, reference, "build.ninja");
    char *ninja = tool_read(path);
    REQUIRE(ninja);
    join(path, build_dir, "reflect.chibi-record");
    char *record = tool_read(path);
    REQUIRE(record);
    *strstr(record, "\n\n") = 0;
    int ours_count = 0;
    char *ours[GRAPH_TOKENS];
    for (char *token = strtok(record, "\n"); token; token = strtok(NULL, "\n")) {
        ours[ours_count++] = token;
    }
    REQUIRE(same_link(ninja, "reflect", ours, ours_count));
    free(record);
    free(ninja);
    /* Run as CMake runs it, with the math headers the graph staged from the
     * third-party archive. */
    char program[GRAPH_PATH], types[GRAPH_PATH], runtime[GRAPH_PATH],
        nodes[GRAPH_PATH], source[GRAPH_PATH], math[GRAPH_PATH],
        manifest[GRAPH_PATH];
    join(program, build_dir, "reflect");
    join(types, build_dir, "native_types.c");
    join(runtime, build_dir, "native_capabilities.c");
    join(nodes, CHIBI_SOURCE_ROOT, "src/runtime/native_nodes.c");
    join(source, CHIBI_SOURCE_ROOT, "src");
    join(math, build_dir, "sdk/public/math");
    join(manifest, CHIBI_SOURCE_ROOT, "src/runtime/component_ids.tsv");
    const char *argv[24] = {
        program,          nodes,   "--prefix",         "",
        "--types-output", types,   "--runtime-output", runtime,
        "--include",      source,  "--include",        math,
        "--manifest",     manifest
    };
    int argc = 14;
    if (sysroot[0]) {
        argv[argc++] = "--sysroot";
        argv[argc++] = sysroot;
    }
    argv[argc] = NULL;
    REQUIRE(!tool_run(argv, suite, log_path));
    const char *const outputs[][2] = {
        { "native_types.c", "generated/native_types.c" },
        { "native_capabilities.c", "generated/native_capabilities.c" }
    };
    for (int i = 0; i < 2; i++) {
        char ours_path[GRAPH_PATH], theirs_path[GRAPH_PATH];
        join(ours_path, build_dir, outputs[i][0]);
        join(theirs_path, reference, outputs[i][1]);
        char *mine = tool_read(ours_path), *cmake = tool_read(theirs_path);
        REQUIRE(mine && cmake && !strcmp(mine, cmake));
        free(mine);
        free(cmake);
    }
    return true;
}

/* The graph compiles over a third-party archive's headers and stages them:
 * the Cute math headers that chibi_vector_capabilities includes as system
 * headers, and the task pool over the archive's prebuilt enkiTS, with its
 * shared private/physics headers. A second run compiles nothing, and a run
 * whose archive is not unpacked, or that has C++ sources and no C++ compiler,
 * says so before it compiles. */
TEST_CASE(archive_headers_compile_and_stage)
{
    char build_dir[GRAPH_PATH], path[GRAPH_PATH], theirs[GRAPH_PATH];
    join(build_dir, suite, "archive");
    const char *const archive_targets[] = { "--target",
                                            "chibi_vector_capabilities",
                                            "--target",
                                            "chibi_task_pool_runtime",
                                            NULL };
    char missing[GRAPH_PATH];
    join(missing, suite, "no archive");
    const char *const unpacked[] = { "--third-party", missing, NULL };
    REQUIRE(run_graph(CHIBI_SOURCE_ROOT, build_dir, unpacked, archive_targets) == -1);
    char *log = tool_read(log_path);
    REQUIRE(log && strstr(log, "the third-party archive is not unpacked"));
    free(log);
    /* The UI host's C++ sources need a C++ compiler. */
    const char *const no_cxx[] = { "--cxx-compiler", "none", NULL };
    const char *const cxx_target[] = { "--target", "chibi_ui_host", NULL };
    REQUIRE(run_graph(CHIBI_SOURCE_ROOT, build_dir, no_cxx, cxx_target) == -1);
    log = tool_read(log_path);
    REQUIRE(log && strstr(log, "no C++ compiler was found"));
    free(log);
    join(path, build_dir, "objects");
    REQUIRE(access(path, F_OK));
    REQUIRE(graph(CHIBI_SOURCE_ROOT, build_dir, warnings, archive_targets) == 3);
    REQUIRE(graph(CHIBI_SOURCE_ROOT, build_dir, warnings, archive_targets) == 0);
    join(path, build_dir, "lib/libchibi_task_pool_runtime.a");
    REQUIRE(!access(path, R_OK));
    /* The math headers and the private/physics directory the task pool shares
     * with physics are staged as CMake stages them. */
    const char *const staged[] = { "sdk/public/math/cute_math.h",
                                   "sdk/private/physics/diagnostics/tracy.h" };
    for (int i = 0; i < 2; i++) {
        join(path, build_dir, staged[i]);
        join(theirs, reference, staged[i]);
        char *mine = tool_read(path), *cmake = tool_read(theirs);
        REQUIRE(mine && cmake && !strcmp(mine, cmake));
        free(mine);
        free(cmake);
    }
    return true;
}

/* A library that compiles reflect's output runs reflect first, as CMake's
 * generate_runtime_registry does, over the math headers the graph staged from
 * the third-party archive; here the configured build's reflect, as a cross
 * build names one, since building reflect takes most of a minute's ceiling.
 * The sources it writes equal CMake's byte for byte, and an unchanged run
 * neither reruns reflect nor compiles; one that lost an output reruns it. */
TEST_CASE(native_types_generate_as_cmake)
{
    char build_dir[GRAPH_PATH], program[GRAPH_PATH];
    join(build_dir, suite, "native types");
    join(program, reference, "reflect");
    const char *const types_only[] = { "--target",
                                       "chibi_native_types",
                                       "--reflect",
                                       program,
                                       NULL };
    REQUIRE(graph(CHIBI_SOURCE_ROOT, build_dir, warnings, types_only) == 1);
    char *log = tool_read(log_path);
    REQUIRE(log && strstr(log, "reflect:"));
    free(log);
    const char *const outputs[] = { "generated/native_types.c",
                                    "generated/native_capabilities.c" };
    for (int i = 0; i < 2; i++) {
        char ours_path[GRAPH_PATH], theirs_path[GRAPH_PATH];
        join(ours_path, build_dir, outputs[i]);
        join(theirs_path, reference, outputs[i]);
        char *mine = tool_read(ours_path), *cmake = tool_read(theirs_path);
        REQUIRE(mine && cmake && !strcmp(mine, cmake));
        free(mine);
        free(cmake);
    }
    /* reflect lists what it reflects; an up-to-date run prints nothing of it,
     * and losing the bindings reruns it without recompiling the types. */
    REQUIRE(graph(CHIBI_SOURCE_ROOT, build_dir, warnings, types_only) == 0);
    log = tool_read(log_path);
    REQUIRE(log && !strstr(log, "reflect:"));
    free(log);
    char bindings[GRAPH_PATH];
    join(bindings, build_dir, "generated/native_capabilities.c");
    REQUIRE(!unlink(bindings));
    REQUIRE(graph(CHIBI_SOURCE_ROOT, build_dir, warnings, types_only) == 0);
    log = tool_read(log_path);
    REQUIRE(log && strstr(log, "reflect:") && !access(bindings, R_OK));
    free(log);
    return true;
}

/* A library whose private headers a step rewrites, as sdk/images.cmake gives
 * Cute's codecs private symbols: the rewritten headers equal CMake's, staging
 * keeps them, an unchanged run compiles nothing and leaves their stamps, and
 * a missing input fails the build. */
TEST_CASE(rewritten_codec_headers_match_cmake)
{
    char build_dir[GRAPH_PATH], ours[GRAPH_PATH], theirs[GRAPH_PATH];
    join(build_dir, suite, "codecs");
    const char *const codecs_only[] = { "--target", "chibi_image_preparation", NULL };
    REQUIRE(graph(CHIBI_SOURCE_ROOT, build_dir, warnings, codecs_only) == 1);
    const char *const headers[] = {
        "sdk/private/image_preparation/vendor/image_png.h",
        "sdk/private/image_preparation/vendor/image_jpg.h"
    };
    struct stat before[2], after;
    for (int i = 0; i < 2; i++) {
        join(ours, build_dir, headers[i]);
        join(theirs, reference, headers[i]);
        char *mine = tool_read(ours), *cmake = tool_read(theirs);
        REQUIRE(mine && cmake && !strcmp(mine, cmake) && !stat(ours, &before[i]));
        free(mine);
        free(cmake);
    }
    REQUIRE(graph(CHIBI_SOURCE_ROOT, build_dir, warnings, codecs_only) == 0);
    for (int i = 0; i < 2; i++) {
        join(ours, build_dir, headers[i]);
        REQUIRE(
            !stat(ours, &after) && after.st_ino == before[i].st_ino &&
            after.st_mtime == before[i].st_mtime
        );
    }
    /* An archive without the codecs fails the build and leaves the headers. */
    char archive[GRAPH_PATH], marker[GRAPH_PATH];
    join(archive, suite, "codec-less archive");
    assert(!mkdir(archive, 0700));
    join(marker, archive, "third-party.cmake");
    tool_write(marker, "# no codecs\n");
    const char *const options[] = { "--third-party", archive, NULL };
    REQUIRE(run_graph(CHIBI_SOURCE_ROOT, build_dir, options, codecs_only) == -1);
    char *text = tool_read(log_path);
    REQUIRE(text && strstr(text, "staging chibi_image_preparation's headers failed"));
    free(text);
    join(ours, build_dir, headers[0]);
    REQUIRE(!stat(ours, &after) && after.st_ino == before[0].st_ino);
    return true;
}

#ifdef __APPLE__
/* Building only TARGET, one source over a header set, writes each of its
 * headers below DIRECTORY equal to CMake's, and an unchanged run compiles
 * nothing and leaves their stamps. */
static bool configures_as_cmake(
    const char *target,
    const char *directory,
    const char *const *headers,
    int count
)
{
    char build_dir[GRAPH_PATH], ours[GRAPH_PATH], theirs[GRAPH_PATH],
        header[GRAPH_PATH];
    join(build_dir, suite, target);
    const char *const target_only[] = { "--target", target, NULL };
    REQUIRE(graph(CHIBI_SOURCE_ROOT, build_dir, warnings, target_only) == 1);
    struct stat before[GRAPH_HEADERS], after;
    assert(count <= GRAPH_HEADERS);
    for (int i = 0; i < count; i++) {
        snprintf(header, sizeof header, "%s/%s", directory, headers[i]);
        join(ours, build_dir, header);
        join(theirs, reference, header);
        char *mine = tool_read(ours), *cmake = tool_read(theirs);
        bool same = mine && cmake && !strcmp(mine, cmake) && !stat(ours, &before[i]);
        free(mine);
        free(cmake);
        REQUIRE(same);
    }
    REQUIRE(graph(CHIBI_SOURCE_ROOT, build_dir, warnings, target_only) == 0);
    for (int i = 0; i < count; i++) {
        snprintf(header, sizeof header, "%s/%s", directory, headers[i]);
        join(ours, build_dir, header);
        REQUIRE(!stat(ours, &after) && after.st_ino == before[i].st_ino);
    }
    return true;
}
#endif

/* The language tests' generated headers, as CMake configures them: Chibi
 * sources as byte initializers, the build workflow as C string lines and plain
 * copies. */
TEST_CASE(configured_headers_match_cmake)
{
#ifdef __APPLE__
    const char *const headers[] = { "package_core_source.h",
                                    "build_workflow_source.h",
                                    "package_compiler_chibi_source.h",
                                    "third_party/pico_headers/pico_unit.h",
                                    "test/prepared_fixture.h" };
    REQUIRE(
        configures_as_cmake("test_behavior_consumer", "test/language/testing/include", headers, 5)
    );
#endif
    return true;
}

/* The tooling tests' SDK contract consumers see only staged SDK headers and
 * the fixtures copied beside pico_unit; C export cases compile with the
 * profile's compiler and C flags, which export_compiler.h lists. */
TEST_CASE(tool_testing_headers_match_cmake)
{
#ifdef __APPLE__
    const char *const headers[] = { "third_party/pico_headers/pico_unit.h",
                                    "test/prepared_fixture.h",
                                    "test/tools/fixture.h",
                                    "test/graph/packs.h",
                                    "test/graph/fixture.h",
                                    "test/render/hidden_app.h",
                                    "test/support/reflected_types.h",
                                    "test/support/gltf_fixture.h",
                                    "export_compiler.h" };
    REQUIRE(
        configures_as_cmake("chibi_prepared_consumer", "test/tools/testing/include", headers, 9)
    );
#endif
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

/* The host runs the graph; the reference is the configured CMake build the
 * graph is compared with. */
static void set_up(const char *host, const char *cmake_build)
{
    char *resolved = realpath(host, NULL);
    assert(resolved);
    snprintf(host_path, sizeof host_path, "%s", resolved);
    free(resolved);
    resolved = realpath(cmake_build, NULL);
    assert(resolved);
    snprintf(reference, sizeof reference, "%s", resolved);
    free(resolved);
    strcpy(suite, "/tmp/chibi-graph-XXXXXX");
    assert(mkdtemp(suite));
    resolved = realpath(suite, NULL);
    snprintf(suite, sizeof suite, "%s", resolved);
    free(resolved);
    join(built, suite, "build");
    join(log_path, suite, "graph.log");
    read_profile();
}

void test_build_graph(const char *host, const char *cmake_build)
{
    set_up(host, cmake_build);
    RUN_TEST_CASE(commands_match_cmake);
    RUN_TEST_CASE(links_match_cmake);
    RUN_TEST_CASE(executables_stage_no_headers);
    tool_remove_tree(suite);
}

/* The entries of a compile database. */
static size_t commands_in(const char *build_dir)
{
    char path[GRAPH_PATH];
    join(path, build_dir, "compile_commands.json");
    yyjson_doc *doc = yyjson_read_file(path, 0, NULL, NULL);
    assert(doc);
    size_t count = yyjson_arr_size(yyjson_doc_get_root(doc));
    yyjson_doc_free(doc);
    return count;
}

/* A run that names a target lists only the compiles it builds, unless
 * --all-commands asks for every target's, as many as CMake's database. */
TEST_CASE(all_commands_list_every_target)
{
    char build_dir[GRAPH_PATH];
    join(build_dir, suite, "all commands");
    const char *const named[] = { "--commands-only", "on", "--target", "chibi_support", NULL };
    REQUIRE(graph(CHIBI_SOURCE_ROOT, build_dir, warnings, named) == 0);
    size_t selected = commands_in(build_dir);
    const char *const every[] = {
        "--commands-only", "on", "--target", "chibi_support",
        "--all-commands",  "on", NULL
    };
    REQUIRE(graph(CHIBI_SOURCE_ROOT, build_dir, warnings, every) == 0);
    REQUIRE(selected > 0 && selected < commands_in(build_dir));
#ifdef __APPLE__
    /* Elsewhere CMake builds executables the graph does not yet. */
    REQUIRE(commands_in(build_dir) == commands_in(reference));
#endif
    return true;
}

/* Four fixed rejected invocations: an unknown target alone and beside a
 * known target, and a target missing its name at the end or before an option.
 * They write no build outputs and never reach toolchain discovery. */
TEST_CASE(invalid_targets_fail_before_writing_commands)
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
        { { "--target", "--commands-only", "on", NULL }, "graph: --target requires a target name" }
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
TEST_CASE(debug_optimization_preserves_instrumentation)
{
    const char *const options[] = {
        "--profile", "explicit", "--compiler", "/unused/clang",
        "--cxx-compiler", "/unused/clang++", "--compiler-id", "Clang",
        "--build-type", "Debug", "--sanitizers", "address,undefined",
        "--archiver", "/unused/ar", "--ranlib", "/unused/ranlib", NULL
    };
    const char *const levels[] = { "", "0", "1" };
    for (int level = 0; level < 3; level++) {
        char build_dir[GRAPH_PATH], path[GRAPH_PATH];
        join(build_dir, suite, level == 0 ? "default debug" : levels[level]);
        const char *const extra[] = {
            "--commands-only", "on", "--target", "chibi_support",
            "--all-commands", "on", level ? "--debug-optimization" : NULL,
            levels[level], level ? "--debug-optimization" : NULL, "0", NULL
        };
        /* Valid repetitions retain Option's existing first-value precedence. */
        REQUIRE(run_graph(CHIBI_SOURCE_ROOT, build_dir, options, extra) == 0);
        join(path, build_dir, "compile_commands.json");
        yyjson_doc *doc = yyjson_read_file(path, 0, NULL, NULL);
        REQUIRE(doc);
        bool saw_test = false, saw_cxx = false, saw_engine = false, saw_sdk = false;
#ifdef __APPLE__
        bool saw_tools = false, saw_language = false, saw_objc = false;
#endif
        yyjson_arr_iter entries = yyjson_arr_iter_with(yyjson_doc_get_root(doc));
        yyjson_val *entry;
        while ((entry = yyjson_arr_iter_next(&entries))) {
            bool debug = false, sanitizer = false, frames = false, assertions = false;
            int optimizations = 0;
            yyjson_arr_iter arguments = yyjson_arr_iter_with(yyjson_obj_get(entry, "arguments"));
            yyjson_val *value;
            while ((value = yyjson_arr_iter_next(&arguments))) {
                const char *arg = yyjson_get_str(value);
                REQUIRE(arg);
                debug |= !strcmp(arg, "-g");
                sanitizer |= !strcmp(arg, "-fsanitize=address,undefined");
                frames |= !strcmp(arg, "-fno-omit-frame-pointer");
                assertions |= !strcmp(arg, "-UNDEBUG");
                REQUIRE(strncmp(arg, "-DNDEBUG", 8));
                if (!strncmp(arg, "-O", 2)) {
                    optimizations++;
                    REQUIRE(level && !strcmp(arg + 2, levels[level]));
                }
            }
            REQUIRE(debug && sanitizer && frames);
            REQUIRE(optimizations == (level != 0));
            const char *output = yyjson_get_str(yyjson_obj_get(entry, "output"));
            saw_engine |= strstr(output, "/objects/chibi_engine/") != NULL;
            saw_sdk |= strstr(output, "/objects/chibi_compiler/") != NULL;
            if (strstr(output, "/objects/test_host/") || strstr(output, "/objects/policy_worker/")) {
                REQUIRE(assertions);
                saw_test = true;
            }
            const char *file = yyjson_get_str(yyjson_obj_get(entry, "file"));
            saw_cxx |= strstr(file, ".cpp") != NULL;
#ifdef __APPLE__
            if (strstr(output, "/objects/test_tools/")) {
                REQUIRE(assertions);
                saw_tools = true;
            }
            if (strstr(output, "/objects/test_language/")) {
                REQUIRE(assertions);
                saw_language = true;
            }
            saw_objc |= !strcmp(file + strlen(file) - 2, ".m");
#endif
        }
        REQUIRE(saw_test && saw_cxx && saw_engine && saw_sdk);
#ifdef __APPLE__
        REQUIRE(saw_tools && saw_language && saw_objc);
#endif
        yyjson_doc_free(doc);
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
        { "--build-type", "RelWithDebInfo", "--debug-optimization", "0", NULL }
    };
    for (int i = 0; i < (int)(sizeof cases / sizeof *cases); i++) {
        char build_dir[GRAPH_PATH];
        join(build_dir, suite, "invalid optimization");
        REQUIRE(run_graph(CHIBI_SOURCE_ROOT, build_dir, options, cases[i]) == -1);
        char *log = tool_read(log_path);
        REQUIRE(log && strstr(log, "--debug-optimization requires 0 or 1 and build type Debug"));
        free(log);
        REQUIRE(access(build_dir, F_OK));
    }
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

/* The number of files below ours, each equal to the file of the same path
 * below theirs, or -1 when ours is missing or one differs. */
static int same_tree(const char *ours, const char *theirs)
{
    struct stat status;
    if (stat(ours, &status)) {
        fprintf(stderr, "graph resource missing: %s\n", ours);
        return -1;
    }
    if (!S_ISDIR(status.st_mode)) {
        char *mine = tool_read(ours), *cmake = tool_read(theirs);
        bool same = mine && cmake && !strcmp(mine, cmake);
        if (!same) {
            fprintf(stderr, "graph resource differs from CMake's: %s\n", ours);
        }
        free(mine);
        free(cmake);
        return same ? 1 : -1;
    }
    DIR *directory = opendir(ours);
    assert(directory);
    int count = 0;
    for (struct dirent *entry; count >= 0 && (entry = readdir(directory));) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) {
            continue;
        }
        char mine[GRAPH_PATH], cmake[GRAPH_PATH];
        join(mine, ours, entry->d_name);
        join(cmake, theirs, entry->d_name);
        int below = same_tree(mine, cmake);
        count = below < 0 ? -1 : count + below;
    }
    closedir(directory);
    return count;
}

/* The launcher's share/chibi and chibi_ctl's resources equal what CMake
 * configures: the build identity, the core and documentation sources and the
 * shaders. They are written before any compile, so a compiler that always
 * fails still writes them, and an unchanged run leaves their stamps. */
TEST_CASE(resources_install_as_cmake)
{
#ifdef __APPLE__
    char build_dir[GRAPH_PATH], ours[GRAPH_PATH], theirs[GRAPH_PATH],
        program[GRAPH_PATH];
    join(build_dir, suite, "resources");
    join(program, reference, "reflect");
    const char *const failing[] = {
        "--compiler",    "/usr/bin/false", "--compiler-id",
        compiler_id,     "--sanitizers",   sanitizers,
        "--third-party", third_party,      "--archiver",
        archiver,        "--ranlib",       ranlib,
        "--reflect",     program,          NULL
    };
    const char *const installs[] = { "--target", "chibi", "--target", "chibi_ctl", NULL };
    REQUIRE(run_graph(CHIBI_SOURCE_ROOT, build_dir, failing, installs) == -1);
    /* The identity, seven sources and the shaders below share/chibi, and the
     * identity and the shaders again for chibi_ctl. The shaders are compared
     * with the sources CMake copies, since it copies chibi_ctl's only when it
     * configures. */
    const char *const trees[][2] = {
        { "share/chibi/engine.json", "build/share/chibi/engine.json" },
        { "share/chibi/core", "build/share/chibi/core" },
        { "share/chibi/docs", "build/share/chibi/docs" },
        { "share/chibi/shaders", "src/render/shaders" },
        { "controller_resources/engine.json", "build/controller_resources/engine.json" },
        { "controller_resources/shaders", "src/render/shaders" },
    };
    int files = 0;
    for (int i = 0; i < 6; i++) {
        join(ours, build_dir, trees[i][0]);
        if (!strncmp(trees[i][1], "build/", 6)) {
            join(theirs, reference, trees[i][1] + 6);
        } else {
            join(theirs, CHIBI_SOURCE_ROOT, trees[i][1]);
        }
        int count = same_tree(ours, theirs);
        REQUIRE(count > 0);
        files += count;
    }
    REQUIRE(files == 213);
    struct stat before, after, identity_before, identity_after;
    char identity[GRAPH_PATH];
    join(ours, build_dir, "share/chibi/engine.json");
    join(identity, build_dir, "sdk/chibi_engine_metadata.json");
    REQUIRE(!stat(ours, &before) && !stat(identity, &identity_before));
    const char *const launcher_only[] = { "--target", "chibi", NULL };
    REQUIRE(run_graph(CHIBI_SOURCE_ROOT, build_dir, failing, launcher_only) == -1);
    REQUIRE(
        !stat(ours, &after) && after.st_ino == before.st_ino &&
        after.st_mtime == before.st_mtime
    );
    REQUIRE(
        !stat(identity, &identity_after) &&
        identity_after.st_ino == identity_before.st_ino &&
        identity_after.st_mtime == identity_before.st_mtime
    );
#endif
    return true;
}

/* The language tests' corpus lists every fixture source below test/fixtures
 * by absolute path, sorted, without the migration originals, as CMake writes
 * it; like resources, before any compile. */
TEST_CASE(language_corpus_lists_as_cmake)
{
#ifdef __APPLE__
    char build_dir[GRAPH_PATH], ours[GRAPH_PATH], theirs[GRAPH_PATH],
        program[GRAPH_PATH];
    join(build_dir, suite, "corpus");
    join(program, reference, "reflect");
    const char *const failing[] = {
        "--compiler",    "/usr/bin/false", "--compiler-id",
        compiler_id,     "--sanitizers",   sanitizers,
        "--third-party", third_party,      "--archiver",
        archiver,        "--ranlib",       ranlib,
        "--reflect",     program,          NULL
    };
    const char *const language_only[] = { "--target", "test_language", NULL };
    REQUIRE(run_graph(CHIBI_SOURCE_ROOT, build_dir, failing, language_only) == -1);
    join(ours, build_dir, "test/language/corpus.txt");
    join(theirs, reference, "test/language/corpus.txt");
    REQUIRE(same_tree(ours, theirs) == 1);
#endif
    return true;
}

/* A separate route: another commands-only run of the whole graph takes
 * seconds. */
void test_build_graph_programs(const char *host, const char *cmake_build)
{
    set_up(host, cmake_build);
    RUN_TEST_CASE(named_programs_build_what_they_run);
    RUN_TEST_CASE(all_commands_list_every_target);
    RUN_TEST_CASE(invalid_targets_fail_before_writing_commands);
    RUN_TEST_CASE(debug_optimization_preserves_instrumentation);
    RUN_TEST_CASE(debug_optimization_rejects_invalid_profiles);
    RUN_TEST_CASE(authored_dependency_files_list_prerequisites);
    tool_remove_tree(suite);
}

/* A separate route: staging chibi_ctl's headers takes seconds. */
void test_build_graph_resources(const char *host, const char *cmake_build)
{
    set_up(host, cmake_build);
    RUN_TEST_CASE(resources_install_as_cmake);
    RUN_TEST_CASE(language_corpus_lists_as_cmake);
    tool_remove_tree(suite);
}

/* A separate route: about ten graph runs over a copied root. */
void test_build_graph_support(const char *host, const char *cmake_build)
{
    set_up(host, cmake_build);
    make_clone();
    RUN_TEST_CASE(support_rebuilds_only_what_changed);
    RUN_TEST_CASE(support_failed_compile_is_retried);
    RUN_TEST_CASE(support_archive_links);
    tool_remove_tree(suite);
}

/* A separate route: reflect's compiles take most of a minute's ceiling. */
void test_build_graph_reflect(const char *host, const char *cmake_build)
{
    set_up(host, cmake_build);
    RUN_TEST_CASE(reflect_links_as_cmake_and_writes_the_same_sources);
    tool_remove_tree(suite);
}

/* A separate route: compiles over the archive's prebuilt libraries and
 * staged headers take seconds. */
void test_build_graph_archive(const char *host, const char *cmake_build)
{
    set_up(host, cmake_build);
    RUN_TEST_CASE(archive_headers_compile_and_stage);
    tool_remove_tree(suite);
}

/* Cheap source registration guard for this delivery's new unconditional
 * dispatcher calls. Keep these exact contracts together with their recipes. */
static bool registration(const char *file, const char *needle)
{
    char filename[GRAPH_PATH];
    snprintf(filename, sizeof filename, "%s/%s", CHIBI_SOURCE_ROOT, file);
    char *text = tool_read(filename);
    REQUIRE(text);
    bool found = strstr(text, needle) != NULL;
    free(text);
    REQUIRE(found);
    return true;
}
TEST_CASE(language_delivery_registrations_match)
{
    const char *calls[] = { "test_language_execution_scope",
                            "test_language_asset_packaging_chibi",
                            "test_language_shader_source_chibi" };
    const char *sources[] = { "execution_scope.c",
                              "asset_packaging_chibi.c",
                              "shader_source_chibi.c" };
    for (int i = 0; i < 3; i++) {
        REQUIRE(registration("test/language/main.c", calls[i]));
        char source[128];
        snprintf(source, sizeof source, "test/language/%s", sources[i]);
        REQUIRE(registration("tools/graph/engine/Targets.chibi", source));
    }
    REQUIRE(registration("test/language/CMakeLists.txt", "execution_scope.c"));
    REQUIRE(registration("test/language/CMakeLists.txt", "include(asset_packaging_chibi.cmake)"));
    REQUIRE(registration("test/language/CMakeLists.txt", "include(shader_source_chibi.cmake)"));
    const char *names[] = { "asset_packaging", "shader_source" };
    for (int i = 0; i < 2; i++) {
        char file[128], mapping[256];
        snprintf(file, sizeof file, "test/language/%s_chibi.cmake", names[i]);
        snprintf(mapping, sizeof mapping, "target_sources(test_language PRIVATE %s_chibi.c)", names[i]);
        REQUIRE(registration(file, mapping));
        snprintf(
            mapping,
            sizeof mapping,
            "%s_chibi_source.h=test/language/%s_chibi_source.h.in",
            names[i],
            names[i]
        );
        REQUIRE(registration("tools/graph/engine/Targets.chibi", mapping));
    }
    REQUIRE(
        registration("tools/graph/engine/Targets.chibi", "CHIBI_ASSET_PACKAGING_BYTES=bytes:tools/compiler/asset_packaging/Packaging.chibi")
    );
    REQUIRE(
        registration("tools/graph/engine/Targets.chibi", "CHIBI_SHADER_LOWER_BYTES=bytes:tools/compiler/shader_source/Lower.chibi")
    );
    REQUIRE(registration("test/language/asset_packaging_chibi.cmake", "CHIBI_ASSET_PACKAGING_BYTES"));
    REQUIRE(registration("test/language/shader_source_chibi.cmake", "CHIBI_${name}_BYTES"));
    return true;
}

TEST_CASE(asset_generator_uses_its_own_collection)
{
    REQUIRE(
        registration("tools/toolchain/AssetCompiler.chibi", "import generator \"asset_generator:.\"")
    );
    REQUIRE(
        registration("sdk/asset_compiler_host.cmake", "--collection asset_generator=tools/graph/asset_compiler")
    );
    REQUIRE(
        registration("tools/graph/engine/Engine.chibi", "\"--collection\", \"asset_generator=tools/graph/asset_compiler\"")
    );
    return true;
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
                             "asset compiler: serialize bootstrap request",
                             "asset compiler: bootstrap request ready",
                             "asset compiler: bootstrap reuse status {}",
                             "asset compiler: invoke bootstrap fallback",
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

/* A separate route: reflect's run and the codec compile take seconds. */
void test_build_graph_generated(const char *host, const char *cmake_build)
{
    set_up(host, cmake_build);
    RUN_TEST_CASE(language_delivery_registrations_match);
    RUN_TEST_CASE(asset_generator_uses_its_own_collection);
    RUN_TEST_CASE(failed_steps_name_only_the_output);
    RUN_TEST_CASE(asset_generator_source_lists_bounded_ordered_phases);
    RUN_TEST_CASE(native_types_generate_as_cmake);
    RUN_TEST_CASE(rewritten_codec_headers_match_cmake);
    RUN_TEST_CASE(configured_headers_match_cmake);
    RUN_TEST_CASE(tool_testing_headers_match_cmake);
    tool_remove_tree(suite);
}
