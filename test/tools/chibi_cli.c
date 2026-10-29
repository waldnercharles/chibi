#if defined(__linux__) && !defined(_XOPEN_SOURCE)
/* glibc declares the pseudo-terminal functions only for XSI conformance. */
#define _XOPEN_SOURCE 700
#endif

#include "assets/project.h"
#include "host/process.h"
#include "support/xalloc.h"
#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"

#ifdef CHIBI_TEST_IMAGE_PREPARATION
#include "authoring/image_prepare.h"
#endif

#include <arpa/inet.h>
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#include <yyjson.h>

static char root[128], launcher[256], log_path[256], compiler[256], built[256];
static char driver[1024];

static void locate_driver(const char *self)
{
    char *resolved = realpath(self, NULL);
    assert(resolved && strlen(resolved) < sizeof driver);
    strcpy(driver, resolved);
    free(resolved);
}

static void path(char *out, const char *relative)
{
    int length = snprintf(out, 256, "%s/%s", root, relative);
    assert(length > 0 && length < 256);
}

static void write_file(const char *relative, const char *text)
{
    char file[256];
    path(file, relative);
    tool_write(file, text);
}

static bool file_is(const char *relative, const char *expected)
{
    char file[256];
    path(file, relative);
    char *text = tool_read(file);
    bool equal = text && !strcmp(text, expected);
    free(text);
    return equal;
}

static bool exists(const char *relative)
{
    char file[256];
    path(file, relative);
    return access(file, F_OK) == 0;
}

static bool copy_resource(const char *binary, const char *name, const char *destination)
{
    const char *slash = strrchr(binary, '/');
    assert(slash);
    char source[1024];
    int length = snprintf(source, sizeof source, "%.*s/../share/chibi/%s", (int)(slash - binary), binary, name);
    assert(length > 0 && length < (int)sizeof source);
    const char *args[] = { "/bin/cp", "-R", source, destination, NULL };
    return tool_run(args, root, log_path) == 0;
}

static void setup(const char *binary)
{
    strcpy(root, "/tmp/chibi-cli-XXXXXX");
    assert(mkdtemp(root));
    char *resolved = realpath(root, NULL);
    assert(resolved && strlen(resolved) < sizeof root);
    strcpy(root, resolved);
    free(resolved);
    const char *directories[] = { "bin",
                                  "game with spaces",
                                  "game with spaces/content",
                                  "game with spaces/content/scenes" };
    for (int i = 0; i < 4; i++) {
        char directory[256];
        path(directory, directories[i]);
        assert(!mkdir(directory, 0700));
    }
    path(launcher, "bin/chibi");
    path(log_path, "output.log");
    const char *copy[] = { "/bin/cp", binary, launcher, NULL };
    assert(!tool_run(copy, root, log_path));
    assert(!chmod(launcher, 0700));
    const char *resources[] = { "share", "share/chibi" };
    char destination[256];
    for (size_t i = 0; i < sizeof resources / sizeof *resources; i++) {
        path(destination, resources[i]);
        assert(!mkdir(destination, 0700));
    }
    assert(copy_resource(binary, "apps", destination));
    assert(copy_resource(binary, "core", destination));
    char manifest[256];
    snprintf(manifest, sizeof manifest, "version %d\nname \"CLI proof\"\ncontent \"content\"\nstart_scene \"scenes/Start.kdl\"\n", CHIBI_PROJECT_VERSION);
    write_file("game with spaces/project.kdl", manifest);
    write_file("game with spaces/content/scenes/Start.kdl", "version 1\nname \"Start\"\n(array)entities\n");
    write_file(
        "bin/chibi-compile",
        "#!/bin/sh\nset -eu\n"
        "base=$(dirname \"$0\")/..\n"
        "printf 'compile\\n' >> \"$base/compiles\"\n"
        "printf '%s\\n' \"$@\" > \"$base/compile.args\"\n"
        "[ ! -f \"$base/fail\" ] || exit 37\n"
        "output=\nwhile [ \"$#\" -gt 0 ]; do\n"
        "  if [ \"$1\" = --output ]; then output=$2; shift; fi\n"
        "  shift\ndone\n"
        "[ -z \"$output\" ] || printf 'artifact' > \"$output\"\n"
    );
    write_file(
        "bin/chibi-player",
        "#!/bin/sh\nset -eu\n"
        "base=$(dirname \"$0\")/..\n"
        "printf '%s\\n' \"$@\" > \"$base/player.args\"\n"
    );
    char program[256];
    path(program, "bin/chibi-compile");
    assert(!chmod(program, 0700));
    path(program, "bin/chibi-player");
    assert(!chmod(program, 0700));
}

TEST_CASE(build_and_run_use_the_prepared_project)
{
    const char *build[] = { launcher, "build", "game with spaces", NULL };
    REQUIRE(tool_run(build, root, log_path) == 0);
    REQUIRE(file_is("game with spaces/.chibi/build/game.chibip", "artifact"));
    REQUIRE(file_is("compiles", "compile\n"));
    REQUIRE(!exists("player.args"));

    REQUIRE(tool_run(build, root, log_path) == 0);
    REQUIRE(file_is("compiles", "compile\n"));
    REQUIRE(!exists("player.args"));

    char game[256];
    path(game, "game with spaces");
    const char *implicit[] = { launcher, "build", NULL };
    REQUIRE(tool_run(implicit, game, log_path) == 0);
    REQUIRE(file_is("compiles", "compile\n"));

    const char *run[] = { launcher,   "run", "game with spaces",          "--",
                          "--frames", "2",   "space \"quote\";$literal*", "",
                          NULL };
    REQUIRE(tool_run(run, root, log_path) == 0);
    char expected[1024];
    snprintf(expected, sizeof expected, "%s/.chibi/build/game.chibip\n--resource-root\n%s\n--frames\n2\nspace \"quote\";$literal*\n\n", game, game);
    REQUIRE(file_is("player.args", expected));
    REQUIRE(file_is("compiles", "compile\n"));

    /* A project path is shorthand for run, including player options. */
    const char *unnamed[] = { launcher, "game with spaces", "--frames", "3", NULL };
    REQUIRE(tool_run(unnamed, root, log_path) == 0);
    snprintf(
        expected,
        sizeof expected,
        "%s/.chibi/build/game.chibip\n--resource-root\n%s\n--frames\n3\n",
        game,
        game
    );
    REQUIRE(file_is("player.args", expected));
    const char *bare[] = { launcher, NULL };
    REQUIRE(tool_run(bare, root, log_path) == 2);
    REQUIRE(file_is("player.args", expected));
    REQUIRE(file_is("compiles", "compile\n"));

    write_file("fail", "");
    char file[256];
    path(file, "game with spaces/.chibi/build/game.chibip");
    REQUIRE(!unlink(file));
    path(file, "player.args");
    REQUIRE(!unlink(file));
    REQUIRE(tool_run(build, root, log_path) == 37);
    REQUIRE(tool_run(run, root, log_path) == 37);
    REQUIRE(file_is("compiles", "compile\ncompile\ncompile\n"));
    REQUIRE(!exists("player.args"));
    REQUIRE(!exists("game with spaces/.chibi/build/game.chibip"));
    path(file, "fail");
    REQUIRE(!unlink(file));
    return true;
}

/* A project at `relative` whose manifest has format `version`. */
static void project(const char *relative, int version)
{
    char file[256];
    const char *directories[] = { "", "/content", "/content/scenes" };
    for (int i = 0; i < 3; i++) {
        snprintf(file, sizeof file, "%s/%s%s", root, relative, directories[i]);
        assert(!mkdir(file, 0700));
    }
    char manifest[256];
    snprintf(manifest, sizeof manifest, "version %d\nname \"Checked\"\ncontent \"content\"\nstart_scene \"scenes/Start.kdl\"\n", version);
    snprintf(file, sizeof file, "%s/project.kdl", relative);
    write_file(file, manifest);
    snprintf(file, sizeof file, "%s/content/scenes/Start.kdl", relative);
    write_file(file, "version 1\nname \"Start\"\n(array)entities\n");
}

/* The text of the file at `relative`, which must exist. */
static char *contents(const char *relative)
{
    char file[256];
    path(file, relative);
    char *text = tool_read(file);
    assert(text);
    return text;
}

/* check hands the project to the compiler's --check and writes nothing; an
 * older project is refused. */
TEST_CASE(check_prepares_without_writing)
{
    char file[256];
    path(file, "fail");
    unlink(file);
    project("checked", CHIBI_PROJECT_VERSION);
    const char *check[] = { launcher, "check", "checked", NULL };
    REQUIRE(tool_run(check, root, log_path) == 0);
    char expected[512];
    snprintf(expected, sizeof expected, "--project-root\n%s/checked\n--check\n", root);
    REQUIRE(file_is("compile.args", expected));
    REQUIRE(!exists("checked/.chibi"));

    /* Without PROJECT, the project at or above the working directory. */
    char game[256];
    path(game, "checked/content");
    const char *found[] = { launcher, "check", NULL };
    REQUIRE(tool_run(found, game, log_path) == 0);
    REQUIRE(file_is("compile.args", expected));

    write_file("fail", "");
    REQUIRE(tool_run(check, root, log_path) == 37);
    REQUIRE(!unlink(file));
    REQUIRE(!exists("checked/.chibi"));

    project("old", CHIBI_PROJECT_VERSION - 1);
    char *manifest = contents("old/project.kdl");
    const char *old[] = { launcher, "check", "old", NULL };
    REQUIRE(tool_run(old, root, log_path) == 2);
    bool kept = file_is("old/project.kdl", manifest);
    free(manifest);
    REQUIRE(kept);
    REQUIRE(!exists("old/.chibi"));

    char *compiles = contents("compiles");
    const char *help[] = { launcher, "check", "--help", NULL };
    REQUIRE(tool_run(help, root, log_path) == 0);
    const char *extra[] = { launcher, "check", "checked", "more", NULL };
    REQUIRE(tool_run(extra, root, log_path) == 2);
    bool unrun = file_is("compiles", compiles);
    free(compiles);
    REQUIRE(unrun);
    return true;
}

TEST_CASE(check_forwards_cost_explanations)
{
    const char *explain[] = { launcher, "check", "checked", "--explain-costs", NULL };
    REQUIRE(tool_run(explain, root, log_path) == 0);
    char expected[512];
    snprintf(expected, sizeof expected, "--project-root\n%s/checked\n--check\n--explain-costs\n", root);
    REQUIRE(file_is("compile.args", expected));

    char game[256];
    path(game, "checked/content");
    const char *implicit[] = { launcher, "check", "--json", "--explain-costs", NULL };
    REQUIRE(tool_run(implicit, game, log_path) == 0);
    snprintf(expected, sizeof expected, "--project-root\n%s/checked\n--check\n--explain-costs\n--json\n", root);
    REQUIRE(file_is("compile.args", expected));
    const char *json[] = { launcher,  "check",  "--explain-costs",
                           "checked", "--json", NULL };
    REQUIRE(tool_run(json, root, log_path) == 0);
    REQUIRE(file_is("compile.args", expected));
    REQUIRE(!exists("checked/.chibi"));

    char *compiles = contents("compiles");
    const char *help[] = { launcher, "check", "--help", NULL };
    REQUIRE(tool_run(help, root, log_path) == 0);
    char *text = contents("output.log");
    bool documented = strstr(text, "--explain-costs [--json]") != NULL;
    free(text);
    REQUIRE(documented);
    const char *invalid[][8] = {
        { launcher, "check", "checked", "--json", NULL },
        { launcher, "check", "checked", "--explain-costs", "--explain-costs", NULL },
        { launcher, "check", "checked", "--explain-costs", "--json", "--json", NULL },
        { launcher, "check", "checked", "--explain-costs", "--format", NULL },
        { launcher, "check", "checked", "--explain-costs", "--test", NULL },
        { launcher, "check", "checked", "--explain-costs", "--docs", NULL },
        { launcher, "check", "checked", "more", "--explain-costs", NULL },
        { launcher, "build", "checked", "--explain-costs", NULL },
        { launcher, "test", "checked", "--explain-costs", NULL },
        { launcher, "fmt", "checked", "--explain-costs", NULL },
        { launcher, "doc", "checked", "--explain-costs", NULL }
    };
    for (int i = 0; i < (int)(sizeof invalid / sizeof *invalid); i++) {
        REQUIRE(tool_run(invalid[i], root, log_path) == 2);
    }
    bool unrun = file_is("compiles", compiles);
    free(compiles);
    REQUIRE(unrun);
    return true;
}

/* The real compiler's --check prepares a project, fails an invalid one and
 * leaves both, and an older one, exactly as they were. */
TEST_CASE(compiler_check_writes_nothing)
{
    project("real", CHIBI_PROJECT_VERSION);
    char game[256];
    path(game, "real");
    const char *valid[] = { compiler, "--project-root", game, "--check", NULL };
    REQUIRE(tool_run(valid, root, log_path) == 0);
    REQUIRE(!exists("real/.chibi"));

    char output[256];
    path(output, "out/game.chibip");
    const char *both[] = { compiler,   "--project-root", game, "--check",
                           "--output", output,           NULL };
    REQUIRE(tool_run(both, root, log_path) == 2);
    REQUIRE(!exists("out"));

    const char *invalid = "version 1\nname \"Start\"\n(array)entities {\n";
    write_file("real/content/scenes/Start.kdl", invalid);
    REQUIRE(tool_run(valid, root, log_path) == 1);
    REQUIRE(file_is("real/content/scenes/Start.kdl", invalid));
    REQUIRE(!exists("real/.chibi"));

    project("real-old", CHIBI_PROJECT_VERSION - 1);
    char *manifest = contents("real-old/project.kdl");
    char old[256];
    path(old, "real-old");
    const char *older[] = { compiler, "--project-root", old, "--check", NULL };
    REQUIRE(tool_run(older, root, log_path) == 1);
    bool kept = file_is("real-old/project.kdl", manifest);
    free(manifest);
    REQUIRE(kept);
    return true;
}

TEST_CASE(compiler_explains_costs_without_writing)
{
    project("costs", CHIBI_PROJECT_VERSION);
    char game[256], folder[256];
    path(game, "costs");
    path(folder, "costs/content/behaviors");
    REQUIRE(!mkdir(folder, 0700));
    path(folder, "costs/content/libraries");
    REQUIRE(!mkdir(folder, 0700));
    const char *source = "Costs :: behavior {\n"
                         "    seen: int = 0;\n"
                         "    on Begin :: () {\n"
                         "        values: int[] = [1, 2];\n"
                         "        seen = values[0];\n"
                         "    }\n"
                         "    on Tick :: () { seen = Shared.Value(); }\n"
                         "}\n";
    write_file("costs/content/behaviors/Costs.chibi", source);
    const char *library = "Shared :: library {\n"
                          "    Value :: () -> int {\n"
                          "        values: int[] = [3, 4];\n"
                          "        return values[0];\n"
                          "    }\n"
                          "}\n";
    write_file("costs/content/libraries/Shared.chibi", library);
    char *manifest = contents("costs/project.kdl");
    char *scene = contents("costs/content/scenes/Start.kdl");
    const char *explain[] = { compiler,  "--project-root",  game,
                              "--check", "--explain-costs", NULL };
    REQUIRE(tool_run(explain, root, log_path) == 0);
    char *text = contents("output.log");
    bool named = strstr(text, "Costs.chibi") && strstr(text, "allocation");
    free(text);
    REQUIRE(named);

    const char *json[] = { compiler,          "--project-root", game, "--check",
                           "--explain-costs", "--json",         NULL };
    REQUIRE(tool_run(json, root, log_path) == 0);
    yyjson_doc *document = yyjson_read_file(log_path, 0, NULL, NULL);
    REQUIRE(document);
    yyjson_val *report = yyjson_doc_get_root(document);
    bool valid = yyjson_get_int(yyjson_obj_get(report, "version")) == 1 &&
                 yyjson_equals_str(yyjson_obj_get(report, "mode"), "static");
    yyjson_val *functions = yyjson_obj_get(report, "functions");
    valid &= yyjson_is_arr(functions) && yyjson_arr_size(functions) > 0;
    bool found = false, linked = false;
    size_t index, count;
    yyjson_val *function;
    yyjson_arr_foreach(functions, index, count, function)
    {
        valid &= yyjson_is_bool(yyjson_obj_get(function, "traversal_complete"));
        bool tick = yyjson_equals_str(yyjson_obj_get(function, "name"), "Tick");
        yyjson_val *costs = yyjson_obj_get(function, "costs");
        size_t cost_index, cost_count;
        yyjson_val *cost;
        yyjson_arr_foreach(costs, cost_index, cost_count, cost)
        {
            yyjson_val *origin = yyjson_obj_get(cost, "origin");
            yyjson_val *location = yyjson_obj_get(origin, "source");
            const char *file = yyjson_get_str(yyjson_obj_get(location, "path"));
            found |= yyjson_equals_str(yyjson_obj_get(cost, "kind"), "allocation") &&
                     yyjson_equals_str(yyjson_obj_get(cost, "status"), "possible") &&
                     yyjson_is_str(yyjson_obj_get(cost, "reason")) && file &&
                     strstr(file, "Costs.chibi") &&
                     yyjson_get_int(yyjson_obj_get(location, "line")) > 0;
            linked |= tick && file && strstr(file, "Shared.chibi") &&
                      yyjson_equals_str(yyjson_obj_get(cost, "kind"), "allocation") &&
                      yyjson_equals_str(yyjson_obj_get(cost, "status"), "possible") &&
                      yyjson_get_int(yyjson_obj_get(location, "line")) == 3;
        }
    }
    yyjson_doc_free(document);
    REQUIRE(valid && found && linked);

    const char *help[] = { compiler, "--help", NULL };
    REQUIRE(tool_run(help, root, log_path) == 0);
    text = contents("output.log");
    bool documented = strstr(text, "--explain-costs [--json]") != NULL;
    free(text);
    REQUIRE(documented);
    const char *invalid[][10] = {
        { compiler, "--project-root", game, "--explain-costs", NULL },
        { compiler, "--project-root", game, "--check", "--json", NULL },
        { compiler, "--project-root", game, "--check", "--explain-costs", "--explain-costs", NULL },
        { compiler, "--project-root", game, "--check", "--explain-costs", "--json", "--json", NULL },
        { compiler, "--project-root", game, "--check", "--explain-costs", "--format", NULL },
        { compiler, "--project-root", game, "--explain-costs", "--test", NULL },
        { compiler, "--project-root", game, "--explain-costs", "--docs", NULL },
        { compiler, "--project-root", game, "--check", "--explain-costs", "--output", "out/game.chibip", NULL }
    };
    for (int i = 0; i < (int)(sizeof invalid / sizeof *invalid); i++) {
        REQUIRE(tool_run(invalid[i], root, log_path) == 2);
    }
    REQUIRE(file_is("costs/project.kdl", manifest));
    REQUIRE(file_is("costs/content/scenes/Start.kdl", scene));
    REQUIRE(file_is("costs/content/behaviors/Costs.chibi", source));
    REQUIRE(file_is("costs/content/libraries/Shared.chibi", library));
    free(manifest);
    free(scene);
    REQUIRE(!exists("costs/.chibi") && !exists("out"));

    const char
        *invalid_scene = "version 1\nname \"Start\"\n(array)entities {\n";
    write_file("costs/content/scenes/Start.kdl", invalid_scene);
    REQUIRE(tool_run(json, root, log_path) == 1);
    REQUIRE(file_is("costs/content/scenes/Start.kdl", invalid_scene));
    REQUIRE(!exists("costs/.chibi"));

    char older[256];
    path(older, "real-old");
    char *old_manifest = contents("real-old/project.kdl");
    const char *old[] = { compiler,  "--project-root",  older,
                          "--check", "--explain-costs", NULL };
    REQUIRE(tool_run(old, root, log_path) == 1);
    bool kept = file_is("real-old/project.kdl", old_manifest);
    free(old_manifest);
    REQUIRE(kept && !exists("real-old/.chibi"));
    return true;
}

/* version prints the build identity document beside the shaders, with the
 * build ID packaging adds, and fails without a valid one. */
TEST_CASE(version_reads_the_build_identity)
{
    char output[256];
    path(output, "version.log");
    const char *version[] = { launcher, "version", NULL };
    REQUIRE(tool_run(version, root, output) == 1);

    char directory[256];
    path(directory, "share");
    REQUIRE(!mkdir(directory, 0700) || errno == EEXIST);
    path(directory, "share/chibi");
    REQUIRE(!mkdir(directory, 0700) || errno == EEXIST);
    /* Out of name order, as a build tree writes them; output sorts by name. */
    const char *formats = "\"artifact_formats\": {\"player\": {\"version\": "
                          "3}, \"execution\": "
                          "{\"version\": 15, \"source\": "
                          "\"src/execution/prepared.c\"}}";
    char document[512];
    snprintf(document, sizeof document, "{\"sdk_version\": \"9.8.7\", \"project_version\": 25, %s}\n", formats);
    write_file("share/chibi/engine.json", document);
    REQUIRE(tool_run(version, root, output) == 0);
    char expected[1024];
    snprintf(expected, sizeof expected, "chibi 9.8.7\nbuild: unpackaged\ncompiler: %s/bin/chibi-compile\nproject format: 25\nartifact formats: execution 15, player 3\n", root);
    REQUIRE(file_is("version.log", expected));

    snprintf(document, sizeof document, "{\"manifest_format\": 1, \"build_id\": \"fixture-build\", \"sdk_version\": \"9.8.7\", \"project_version\": 25, %s}\n", formats);
    write_file("share/chibi/engine.json", document);
    REQUIRE(tool_run(version, root, output) == 0);
    snprintf(expected, sizeof expected, "chibi 9.8.7\nbuild: fixture-build\ncompiler: %s/bin/chibi-compile\nproject format: 25\nartifact formats: execution 15, player 3\n", root);
    REQUIRE(file_is("version.log", expected));

    write_file("share/chibi/engine.json", "{\"sdk_version\": \"9.8.7\", \"project_version\": 25, \"artifact_formats\": {\"execution\": {}}}\n");
    REQUIRE(tool_run(version, root, output) == 1);
    const char *help[] = { launcher, "version", "--help", NULL };
    REQUIRE(tool_run(help, root, output) == 0);
    const char *extra[] = { launcher, "version", "more", NULL };
    REQUIRE(tool_run(extra, root, output) == 2);
    return true;
}

/* The launcher in place reads the identity its build generated. */
TEST_CASE(version_of_the_built_launcher)
{
    char output[256], expected[64];
    path(output, "built.log");
    const char *version[] = { built, "version", NULL };
    REQUIRE(tool_run(version, root, output) == 0);
    char *text = tool_read(output);
    snprintf(expected, sizeof expected, "\nproject format: %d\n", CHIBI_PROJECT_VERSION);
    bool reported = text && !strncmp(text, "chibi ", 6) && strstr(text, expected) &&
                    strstr(text, "\nartifact formats: ") &&
                    strstr(text, " execution ");
    free(text);
    REQUIRE(reported);
    return true;
}

/* Whether the file at `relative` contains `text`. */
static bool file_has(const char *relative, const char *text)
{
    char file[256];
    path(file, relative);
    char *contents = tool_read(file);
    bool found = contents && strstr(contents, text);
    free(contents);
    return found;
}

/* An identity document for this host, or for `system`, with `backends`;
 * `platform` false leaves the platform out. */
static void identity(const char *system, const char *backends, bool platform)
{
    struct utsname host;
    assert(!uname(&host));
    char named[256] = "", document[1024];
    if (platform) {
        snprintf(
            named,
            sizeof named,
            "\"system\": \"%s\", \"processors\": [\"%s\"], ",
            system ? system : host.sysname,
            host.machine
        );
    }
    snprintf(
        document,
        sizeof document,
        "{\"sdk_version\": \"9.8.7\", %s\"backends\": [%s], "
        "\"project_version\": 25, \"artifact_formats\": {\"execution\": "
        "{\"version\": 15}}}\n",
        named,
        backends
    );
    write_file("share/chibi/engine.json", document);
}

/* Runs `chibi doctor` with `extra` arguments and PATH `programs`. */
static int doctor(const char *extra, const char *web, const char *programs)
{
    char output[256], saved[4096];
    path(output, "doctor.log");
    const char *previous = getenv("PATH");
    assert(previous && strlen(previous) < sizeof saved);
    snprintf(saved, sizeof saved, "%s", previous);
    setenv("PATH", programs, 1);
    const char *args[] = { launcher, "doctor", extra, web, NULL };
    int status = tool_run(args, root, output);
    setenv("PATH", saved, 1);
    return status;
}

/* doctor reports each capability and fails only for a broken one it checks;
 * browser export is checked only for --web. */
TEST_CASE(doctor_reports_each_capability)
{
    char file[256], expected[512], bin[256], programs[512], tools[512];
    path(file, "fail");
    unlink(file);
    path(bin, "node-bin");
    REQUIRE(!mkdir(bin, 0700));
    write_file("node-bin/node", "#!/bin/sh\nexit 0\n");
    path(file, "node-bin/node");
    REQUIRE(!chmod(file, 0700));
    snprintf(tools, sizeof tools, "/usr/bin:/bin");
    snprintf(programs, sizeof programs, "%s:/usr/bin:/bin", bin);

    identity(NULL, "\"bytecode\", \"sljit\"", true);
    REQUIRE(doctor(NULL, NULL, tools) == 0);
    REQUIRE(file_has("doctor.log", "sdk: supported (chibi 9.8.7, build unpackaged)\n"));
    snprintf(expected, sizeof expected, "compiler: supported (%s/bin/chibi-compile)\n", root);
    REQUIRE(file_has("doctor.log", expected));
    snprintf(expected, sizeof expected, "player: supported (%s/bin/chibi-player)\n", root);
    REQUIRE(file_has("doctor.log", expected));
    REQUIRE(file_has("doctor.log", "platform: supported ("));
    REQUIRE(file_has("doctor.log", "native backend: supported (SLJIT)\n"));
    REQUIRE(file_has("doctor.log", "browser export: unavailable ("));

    identity(NULL, "\"bytecode\"", true);
    REQUIRE(doctor(NULL, NULL, tools) == 0);
    REQUIRE(
        file_has("doctor.log", "native backend: unavailable (built for the bytecode interpreter only)\n")
    );

    /* A platform mismatch is reported; whether the SDK runs is the compiler's
     * and player's question. */
    identity("Plan9", "\"bytecode\"", true);
    REQUIRE(doctor(NULL, NULL, tools) == 0);
    REQUIRE(file_has("doctor.log", "platform: unavailable (built for Plan9 "));
    identity(NULL, "\"bytecode\"", false);
    REQUIRE(doctor(NULL, NULL, tools) == 1);
    REQUIRE(file_has("doctor.log", "platform: broken (the build identity names no platform)\n"));
    identity(NULL, "\"bytecode\"", true);

    write_file("fail", "");
    REQUIRE(doctor(NULL, NULL, tools) == 1);
    snprintf(expected, sizeof expected, "compiler: broken (%s/bin/chibi-compile --help failed)\n", root);
    REQUIRE(file_has("doctor.log", expected));
    path(file, "fail");
    REQUIRE(!unlink(file));

    char player[256], moved[256];
    path(player, "bin/chibi-player");
    path(moved, "chibi-player.moved");
    REQUIRE(!rename(player, moved));
    REQUIRE(doctor(NULL, NULL, tools) == 1);
    snprintf(expected, sizeof expected, "player: broken (missing %s/bin/chibi-player)\n", root);
    REQUIRE(file_has("doctor.log", expected));
    REQUIRE(!rename(moved, player));

    char web[256];
    path(web, "web");
    REQUIRE(!mkdir(web, 0700));
    path(file, "web/compiler");
    REQUIRE(!mkdir(file, 0700));
    write_file("web/index.html", "");
    write_file("web/chibi-player.js", "");
    REQUIRE(doctor("--web", web, programs) == 1);
    snprintf(expected, sizeof expected, "browser export: broken (%s lacks chibi-player.wasm)\n", web);
    REQUIRE(file_has("doctor.log", expected));
    write_file("web/chibi-player.wasm", "");
    write_file("web/compiler/chibi-compile.js", "");
    REQUIRE(doctor("--web", web, programs) == 1);
    snprintf(expected, sizeof expected, "browser export: broken (%s lacks compiler/chibi-compile.wasm)\n", web);
    REQUIRE(file_has("doctor.log", expected));
    write_file("web/compiler/chibi-compile.wasm", "");
    REQUIRE(doctor("--web", web, programs) == 0);
    snprintf(expected, sizeof expected, "browser export: supported (%s with %s/node)\n", web, bin);
    REQUIRE(file_has("doctor.log", expected));
    const char *payloads[] = { "index.html",
                               "chibi-player.js",
                               "chibi-player.wasm",
                               "compiler/chibi-compile.js",
                               "compiler/chibi-compile.wasm" };
    bool unreadable = true;
    for (size_t i = 0; i < sizeof payloads / sizeof *payloads; i++) {
        int length = snprintf(file, sizeof file, "%s/%s", web, payloads[i]);
        REQUIRE(length > 0 && length < (int)sizeof file);
        struct stat info;
        REQUIRE(!stat(file, &info));
        REQUIRE(!chmod(file, 0));
        int status = doctor("--web", web, programs);
        bool broken = status == 1 &&
                      file_has("doctor.log", "browser export: broken (") &&
                      file_has("doctor.log", payloads[i]) &&
                      !file_has("doctor.log", "browser export: supported (");
        bool restored = !chmod(file, info.st_mode & 07777);
        REQUIRE(restored);
        if (!broken) {
            fprintf(stderr, "unreadable browser payload %s: doctor status=%d\n", payloads[i], status);
        }
        unreadable &= broken;
        REQUIRE(doctor("--web", web, programs) == 0);
    }
    REQUIRE(unreadable);
    REQUIRE(doctor("--web", web, tools) == 1);
    REQUIRE(file_has("doctor.log", "browser export: broken (node, which runs "));

    REQUIRE(doctor("--help", NULL, tools) == 0);
    REQUIRE(doctor("--frames", NULL, tools) == 2);
    return true;
}

/* The built launcher in place finds its real compiler and player. */
TEST_CASE(doctor_of_the_built_launcher)
{
    char output[256];
    path(output, "built-doctor.log");
    const char *args[] = { built, "doctor", NULL };
    REQUIRE(tool_run(args, root, output) == 0);
    REQUIRE(file_has("built-doctor.log", "\ncompiler: supported ("));
    REQUIRE(file_has("built-doctor.log", "\nplayer: supported ("));
    REQUIRE(file_has("built-doctor.log", "\nplatform: supported ("));
    return true;
}

/* Public tests use the same configured source/session capabilities as normal
 * applications, including when testing the CLI's own authored package. */
TEST_CASE(test_runs_shared_package_tests)
{
    char folder[256];
    path(folder, "package-tests");
    REQUIRE(!mkdir(folder, 0700));
    const char *passing =
        "package facts\nimport strings \"core:strings\"\n"
        "@test\nPasses :: () {\n"
        " args := application.arguments()\n"
        " assert(length(args) == 1 && strings.HasSuffix(args[0], "
        "\"/package-tests/Facts.chibi\"), \"test source argv\")\n"
        " child := program.open(args[0], \"Helper\", [\"nested-test\"])\n"
        " assert(program.update(child) == 1, \"nested test source prepares\")\n"
        " called := program.call(child)\n"
        " assert(called.status == 0 && called.result == 17, \"test program "
        "service\")\n"
        " program.close(child)\n}\n"
        "Helper :: () -> int {\n"
        " args := application.arguments()\n"
        " assert(length(args) == 1 && args[0] == \"nested-test\", \"nested "
        "test argv\")\n"
        " return 17\n}\n"
        "@test\nPureOne :: () { assert(2 + 2 == 4, \"addition\") }\n"
        "@test\nPureTwo :: () { assert(3 * 3 == 9, \"multiplication\") }\n";
    write_file("package-tests/Facts.chibi", passing);
    const char *named[] = { launcher, "test", "package-tests", NULL };
    REQUIRE(tool_run(named, root, log_path) == 0);
    REQUIRE(file_has("output.log", "PASS facts.Passes\n"));
    REQUIRE(file_has("output.log", "3 tests, 0 failed\n"));
    const char *here[] = { launcher, "test", NULL };
    REQUIRE(tool_run(here, folder, log_path) == 0);
    REQUIRE(file_has("output.log", "PASS facts.Passes\n"));
    const char *previous = getenv("CHIBI_TEST_WORKERS");
    char *saved_workers = previous ? xstrdup(previous) : NULL;
    const char *workers[] = { "1", "2" };
    bool worker_settings = true;
    for (int i = 0; i < 2; i++) {
        worker_settings &= !setenv("CHIBI_TEST_WORKERS", workers[i], 1);
        worker_settings &= tool_run(named, root, log_path) == 0;
        worker_settings &= file_is("output.log", "PASS facts.Passes\nPASS facts.PureOne\nPASS facts.PureTwo\n3 tests, 0 failed\n");
    }
    worker_settings &= !setenv("CHIBI_TEST_WORKERS", "0", 1);
    worker_settings &= tool_run(named, root, log_path) == 1;
    worker_settings &= file_has("output.log", "CHIBI_TEST_WORKERS");
    const char *general[] = { launcher, "--help", NULL };
    worker_settings &= tool_run(general, root, log_path) == 0;
    if (saved_workers) {
        worker_settings &= !setenv("CHIBI_TEST_WORKERS", saved_workers, 1);
    } else {
        worker_settings &= !unsetenv("CHIBI_TEST_WORKERS");
    }
    free(saved_workers);
    REQUIRE(worker_settings);
    write_file("package-tests/Facts.chibi", "package facts\n@test\nFails :: () { assert(false, \"shared test failure\") }\n");
    REQUIRE(tool_run(named, root, log_path) == 1);
    REQUIRE(file_has("output.log", "FAIL facts.Fails\n"));
    REQUIRE(file_has("output.log", "Facts.chibi:3:"));
    REQUIRE(file_has("output.log", "shared test failure"));
    write_file("package-tests/Facts.chibi", "package facts\n@test\nFails :: () { missing() }\n");
    REQUIRE(tool_run(named, root, log_path) == 1);
    REQUIRE(file_has("output.log", "Facts.chibi:3:"));
    REQUIRE(file_has("output.log", "missing"));
    write_file("package-tests/Facts.chibi", passing);
    const char *help[] = { launcher, "test", "--help", NULL };
    REQUIRE(tool_run(help, root, log_path) == 0);
    const char *extra[] = { launcher, "test", "one", "two", NULL };
    REQUIRE(tool_run(extra, root, log_path) == 2);
    char application[1024];
    const char *slash = strrchr(built, '/');
    REQUIRE(slash);
    int length = snprintf(
        application,
        sizeof application,
        "%.*s/../share/chibi/apps/chibi",
        (int)(slash - built),
        built
    );
    REQUIRE(length > 0 && length < (int)sizeof application);
    const char *self[] = { launcher, "test", application, NULL };
    REQUIRE(tool_run(self, root, log_path) == 0);
    REQUIRE(file_has("output.log", "PASS chibi_cli.InterruptedExportKeepsSignalStatus\n"));
    REQUIRE(file_has("output.log", "0 failed\n"));
    REQUIRE(!exists("package-tests/.chibi"));
    return true;
}

static const char
    FACTS[] = "package facts\n"
              "import strings \"core:strings\"\n"
              "\n"
              "@test\n"
              "Passes :: () {\n"
              "    assert(strings.ByteLength(\"abc\") == 3, \"three bytes\");\n"
              "}\n"
              "\n"
              "@test\n"
              "Fails :: () {\n"
              "    assert(strings.ByteLength(\"abc\") == 4, \"four bytes\");\n"
              "}\n";

/* The real compiler's --test runs each test of the packages below the root
 * with the build's core collection, reports where a failure stopped and fails
 * for a failed test, an invalid package, a native that needs a running game
 * or a missing root. */
TEST_CASE(compiler_test_reports_each_test)
{
    const char *directories[] = { "tests",        "tests/facts", "broken",
                                  "broken/facts", "game",        "game/sounds" };
    for (int i = 0; i < 6; i++) {
        char directory[256];
        path(directory, directories[i]);
        REQUIRE(!mkdir(directory, 0700));
    }
    write_file("tests/facts/Facts.chibi", FACTS);
    char tests[256], output[256], expected[512];
    path(tests, "tests");
    path(output, "tests.log");
    const char *run[] = { compiler, "--project-root", tests, "--test", NULL };
    REQUIRE(tool_run(run, root, output) == 1);
    snprintf(
        expected,
        sizeof expected,
        "PASS facts.Passes\nFAIL facts.Fails\n  %s/facts/Facts.chibi:11:5: "
        "Assertion failed: four bytes\n2 tests, 1 failed\n",
        tests
    );
    REQUIRE(file_is("tests.log", expected));

    /* The explicit serial and parallel budgets retain exactly the same
     * ordered diagnostics. Invalid settings fail before preparing packages. */
    const char *previous = getenv("CHIBI_TEST_WORKERS");
    char *saved_workers = previous ? xstrdup(previous) : NULL;
    bool worker_settings = true;
    const char *workers[] = { "1", "2" };
    for (int i = 0; i < 2; i++) {
        worker_settings &= !setenv("CHIBI_TEST_WORKERS", workers[i], 1);
        worker_settings &= tool_run(run, root, output) == 1;
        worker_settings &= file_is("tests.log", expected);
    }
    const char *invalid_workers[] = {
        "", "0", "33", "-1", "+2", " 2", "2x", "9999999999999999999999999999"
    };
    for (size_t i = 0; i < sizeof invalid_workers / sizeof *invalid_workers; i++) {
        worker_settings &= !setenv("CHIBI_TEST_WORKERS", invalid_workers[i], 1);
        worker_settings &= tool_run(run, root, output) == 1;
        worker_settings &= file_is("tests.log", "CHIBI_TEST_WORKERS must be an integer from 1 to 32\n");
    }
    if (saved_workers) {
        worker_settings &= !setenv("CHIBI_TEST_WORKERS", saved_workers, 1);
    } else {
        worker_settings &= !unsetenv("CHIBI_TEST_WORKERS");
    }
    free(saved_workers);
    REQUIRE(worker_settings);

    char *passing = xstrdup(FACTS);
    *strstr(passing, "== 4") = '!';
    write_file("tests/facts/Facts.chibi", passing);
    free(passing);
    REQUIRE(tool_run(run, root, output) == 0);
    REQUIRE(file_is("tests.log", "PASS facts.Passes\nPASS facts.Fails\n2 tests, 0 failed\n"));

    write_file("broken/facts/Facts.chibi", "package facts\nBroken :: () -> int { return missing; }\n");
    char broken[256];
    path(broken, "broken");
    const char *invalid[] = { compiler, "--project-root", broken, "--test", NULL };
    REQUIRE(tool_run(invalid, root, output) == 1);
    REQUIRE(file_has("tests.log", "/broken/facts/Facts.chibi:2:"));

    write_file("game/sounds/Sounds.chibi", "package sounds\n@test\nQuiet :: () {\n    set_master_volume(0.5);\n}\n");
    char game[256];
    path(game, "game");
    const char *service[] = { compiler, "--project-root", game, "--test", NULL };
    REQUIRE(tool_run(service, root, output) == 1);
    REQUIRE(file_has("tests.log", "/game/sounds/Sounds.chibi:4:5: "));

    char missing[256];
    path(missing, "missing");
    const char *absent[] = { compiler, "--project-root", missing, "--test", NULL };
    REQUIRE(tool_run(absent, root, output) == 1);
    const char *scene[] = { compiler, "--project-root", tests,
                            "--test", "--scene",        "x",
                            NULL };
    REQUIRE(tool_run(scene, root, output) == 2);
    const char *both[] = { compiler, "--project-root", tests,
                           "--test", "--check",        NULL };
    REQUIRE(tool_run(both, root, output) == 2);
    return true;
}

/* A compiler whose SDK lacks the core collection says so and fails. */
/* The compiler's own packages read raw syntax and JSON strings through the
 * compiler's bridges, so --test links both. */
static const char COMPILER_FACTS
    [] = "package words\n"
         "\n"
         "@test\n"
         "Decodes :: () {\n"
         "    decoded := compiler_json_string(\"\\\"hi\\\"\");\n"
         "    assert(decoded.valid && decoded.value == \"hi\", \"decodes\");\n"
         "}\n"
         "\n"
         "@test\n"
         "Parses :: () {\n"
         "    tree := compiler_syntax_parse(\"package words\\n\");\n"
         "    assert(tree.root >= 0, \"parses\");\n"
         "}\n";
TEST_CASE(compiler_test_links_the_compiler_bridges)
{
    const char *directories[] = { "bridged", "bridged/words" };
    for (int i = 0; i < 2; i++) {
        char directory[256];
        path(directory, directories[i]);
        REQUIRE(!mkdir(directory, 0700));
    }
    write_file("bridged/words/Words.chibi", COMPILER_FACTS);
    char tests[256], output[256];
    path(tests, "bridged");
    path(output, "bridged.log");
    const char *run[] = { compiler, "--project-root", tests, "--test", NULL };
    int status = tool_run(run, root, output);
    if (status) {
        char *log = contents("bridged.log");
        fprintf(stderr, "compiler bridges: %s\n", log);
        free(log);
    }
    REQUIRE(status == 0);
    REQUIRE(file_is("bridged.log", "PASS words.Decodes\nPASS words.Parses\n2 tests, 0 failed\n"));
    const char *public_run[] = { built, "test", tests, NULL };
    REQUIRE(tool_run(public_run, root, output) == 0);
    REQUIRE(file_is("bridged.log", "PASS words.Decodes\nPASS words.Parses\n2 tests, 0 failed\n"));
    return true;
}
TEST_CASE(compiler_runs_cost_explanation_regressions)
{
    const char *run[] = { compiler,
                          "--project-root",
                          CHIBI_SOURCE_ROOT "/tools/compiler/cost_explanations",
                          "--test",
                          NULL };
    REQUIRE(tool_run(run, root, log_path) == 0);
    REQUIRE(file_has("output.log", "PASS cost_explanations.EmptyReport\n"));
    REQUIRE(file_has("output.log", "PASS cost_explanations.FullWidthIdentifiersAndJsonEscaping\n"));
    REQUIRE(file_has("output.log", "6 tests, 0 failed\n"));
    return true;
}

TEST_CASE(compiler_test_needs_the_core_collection)
{
    const char *directories[] = { "sdk", "sdk/bin" };
    for (int i = 0; i < 2; i++) {
        char directory[256];
        path(directory, directories[i]);
        REQUIRE(!mkdir(directory, 0700));
    }
    char copy[256], output[256];
    path(copy, "sdk/bin/chibi-compile");
#ifdef __APPLE__
    const char *clone[] = { "/bin/cp", "-c", compiler, copy, NULL };
#else
    const char *clone[] = { "/bin/cp", "--reflink=auto", compiler, copy, NULL };
#endif
    REQUIRE(tool_run(clone, root, log_path) == 0);
    path(output, "core.log");
    const char *run[] = { copy, "--project-root", root, "--test", NULL };
    REQUIRE(tool_run(run, root, output) == 1);
    REQUIRE(file_has("core.log", "The SDK has no core collection at "));
    REQUIRE(!unlink(copy));
    return true;
}

/* fmt hands each project, or the one at or above the working directory, to
 * the compiler's --format, with --check when asked, and fails when one does;
 * an older project is refused. A folder without a project.kdl
 * goes to the compiler as given, which formats its packages. */
TEST_CASE(fmt_formats_each_project)
{
    char file[256], expected[1024];
    path(file, "fail");
    unlink(file);
    project("tidy", CHIBI_PROJECT_VERSION);
    project("neat", CHIBI_PROJECT_VERSION);
    const char *one[] = { launcher, "fmt", "tidy", NULL };
    REQUIRE(tool_run(one, root, log_path) == 0);
    snprintf(expected, sizeof expected, "--project-root\n%s/tidy\n--format\n", root);
    REQUIRE(file_is("compile.args", expected));

    char *before = contents("compiles");
    const char *both[] = { launcher, "fmt", "--check", "tidy", "neat", NULL };
    REQUIRE(tool_run(both, root, log_path) == 0);
    snprintf(expected, sizeof expected, "%scompile\ncompile\n", before);
    free(before);
    REQUIRE(file_is("compiles", expected));
    snprintf(expected, sizeof expected, "--project-root\n%s/neat\n--format\n--check\n", root);
    REQUIRE(file_is("compile.args", expected));

    char game[256];
    path(game, "tidy/content");
    const char *found[] = { launcher, "fmt", NULL };
    REQUIRE(tool_run(found, game, log_path) == 0);
    snprintf(expected, sizeof expected, "--project-root\n%s/tidy\n--format\n", root);
    REQUIRE(file_is("compile.args", expected));

    write_file("fail", "");
    REQUIRE(tool_run(one, root, log_path) == 37);
    REQUIRE(!unlink(file));
    project("stale", CHIBI_PROJECT_VERSION - 1);
    char *manifest = contents("stale/project.kdl");
    const char *old[] = { launcher, "fmt", "stale", NULL };
    REQUIRE(tool_run(old, root, log_path) == 2);
    bool kept = file_is("stale/project.kdl", manifest);
    free(manifest);
    REQUIRE(kept);
    char loose[256];
    path(loose, "loose");
    REQUIRE(!mkdir(loose, 0700));
    const char *packages[] = { launcher, "fmt", "--check", "loose", NULL };
    REQUIRE(tool_run(packages, root, log_path) == 0);
    REQUIRE(file_is("compile.args", "--project-root\nloose\n--format\n--check\n"));
    const char *missing[] = { launcher, "fmt", "absent", NULL };
    REQUIRE(tool_run(missing, root, log_path) == 2);
    const char *help[] = { launcher, "fmt", "--help", NULL };
    REQUIRE(tool_run(help, root, log_path) == 0);
    const char *unknown[] = { launcher, "fmt", "--quick", NULL };
    REQUIRE(tool_run(unknown, root, log_path) == 2);
    return true;
}

/* The repository's own package trees, the core collection, the docs generator
 * and the compiler, are as the formatter writes them. */
TEST_CASE(repository_packages_are_canonical)
{
    const char *const trees[] = { "apps", "packages", "tools/docs", "tools/compiler", "tools/graph" };
    for (size_t i = 0; i < sizeof trees / sizeof *trees; i++) {
        char tree[512], output[256];
        snprintf(tree, sizeof tree, "%s/%s", CHIBI_SOURCE_ROOT, trees[i]);
        path(output, "canonical.log");
        const char *check[] = { compiler,   "--project-root", tree,
                                "--format", "--check",        NULL };
        int status = tool_run(check, root, output);
        if (status) {
            char *log = contents("canonical.log");
            fprintf(stderr, "%s is not canonical: %s\n", trees[i], log);
            free(log);
        }
        REQUIRE(status == 0);
    }
    return true;
}

static const char
    MESSY_SCENE[] = "version 1\nname   \"Start\"\n(array)entities\n";
static const char
    MESSY_SOURCE[] = "Spin :: behavior {\n  angle: number = 0.0;\n"
                     "  on Tick :: (dt: number s) {   angle = "
                     "angle + dt; }\n}\n";

/* The real compiler's --format --check names the assets that are not
 * canonical and changes nothing; --format rewrites them as their savers
 * write them. It refuses prototypes that share a name, as the player does,
 * and an older project. */
TEST_CASE(compiler_format_rewrites_noncanonical_assets)
{
    project("messy", CHIBI_PROJECT_VERSION);
    char directory[256], messy[256], output[256];
    path(directory, "messy/content/behaviors");
    REQUIRE(!mkdir(directory, 0700));
    write_file("messy/content/scenes/Start.kdl", MESSY_SCENE);
    write_file("messy/content/behaviors/Spin.chibi", MESSY_SOURCE);
    path(messy, "messy");
    path(output, "format.log");
    const char *check[] = { compiler,   "--project-root", messy,
                            "--format", "--check",        NULL };
    REQUIRE(tool_run(check, root, output) == 1);
    REQUIRE(file_has("format.log", "not canonical: content/behaviors/Spin.chibi\n"));
    REQUIRE(file_has("format.log", "not canonical: content/scenes/Start.kdl\n"));
    REQUIRE(file_is("messy/content/scenes/Start.kdl", MESSY_SCENE));
    REQUIRE(file_is("messy/content/behaviors/Spin.chibi", MESSY_SOURCE));

    const char *format[] = { compiler, "--project-root", messy, "--format", NULL };
    REQUIRE(tool_run(format, root, output) == 0);
    REQUIRE(file_has("format.log", "formatted content/scenes/Start.kdl\n"));
    REQUIRE(file_is("messy/content/scenes/Start.kdl", "version 1\nname \"Start\"\n(array)entities\n"));
    REQUIRE(file_is(
        "messy/content/behaviors/Spin.chibi",
        "Spin :: behavior {\n    angle := 0.0\n    on Tick :: (dt: number "
        "s) {\n"
        "        angle = angle + dt\n    }\n}\n"
    ));
    REQUIRE(tool_run(check, root, output) == 0);

    static const char
        HULL[] = "name \"Hull\"\ncomponents {\n    (object)Transform\n}\n";
    path(directory, "messy/content/prototypes");
    REQUIRE(!mkdir(directory, 0700));
    write_file("messy/content/prototypes/Hull.kdl", HULL);
    REQUIRE(tool_run(check, root, output) == 0);
    write_file("messy/content/prototypes/Copy.kdl", HULL);
    REQUIRE(tool_run(check, root, output) == 1);
    REQUIRE(file_has("format.log", "two prototypes declare Hull"));
    char copy[256];
    path(copy, "messy/content/prototypes/Copy.kdl");
    REQUIRE(!unlink(copy));

    project("messy-old", CHIBI_PROJECT_VERSION - 1);
    write_file("messy-old/content/scenes/Start.kdl", MESSY_SCENE);
    char older[256];
    path(older, "messy-old");
    const char *old[] = { compiler, "--project-root", older, "--format", NULL };
    REQUIRE(tool_run(old, root, output) == 1);
    REQUIRE(file_is("messy-old/content/scenes/Start.kdl", MESSY_SCENE));

    char target[256];
    path(target, "out/game.chibip");
    const char *both[] = { compiler,   "--project-root", messy, "--format",
                           "--output", target,           NULL };
    REQUIRE(tool_run(both, root, output) == 2);
    const char *tests[] = { compiler,   "--project-root", messy,
                            "--format", "--test",         NULL };
    REQUIRE(tool_run(tests, root, output) == 2);
    const char *scene[] = { compiler,   "--project-root", messy,
                            "--format", "--scene",        "x",
                            NULL };
    REQUIRE(tool_run(scene, root, output) == 2);
    return true;
}

/* The folder at `relative`, which may already exist. */
static void directory(const char *relative)
{
    char folder[256];
    path(folder, relative);
    assert(!mkdir(folder, 0700) || errno == EEXIST);
}

/* The real LLVM driver integration is Linux-only. This portable process
 * boundary still checks authored CLI routing and argument preservation. */
TEST_CASE(native_build_forwards_driver_arguments)
{
    directory("native package");
    directory("llvm tools");
    write_file("native package/Main.chibi", "package example\nMain :: () -> int { return 0 }\n");
    write_file(
        "bin/chibi-build-host",
        "#!/bin/sh\nset -eu\n"
        "base=$(dirname \"$0\")/..\n"
        "printf '%s\\n' \"$@\" > \"$base/native.args\"\n"
        "printf 'native-driver\\n'\n"
        "[ ! -f \"$base/native.fail\" ] || exit 37\n"
    );
    char host[256], package[256], llvm[256];
    path(host, "bin/chibi-build-host");
    REQUIRE(!chmod(host, 0700));
    path(package, "native package");
    path(llvm, "llvm tools");
    const char *args[] = { launcher,
                           "build",
                           "--native",
                           "-O0",
                           "-g",
                           "--target",
                           "arm64-apple-darwin",
                           "--llvm",
                           llvm,
                           package,
                           NULL };
    REQUIRE(tool_run(args, root, log_path) == 0);
    REQUIRE(file_is("output.log", "native-driver\n"));
    char expected[4096];
    int length = snprintf(
        expected,
        sizeof expected,
        "--root\n%s/share/chibi/tools/native_aot\n"
        "--source\n%s/share/chibi/tools/native_aot/Driver.chibi\n"
        "--core\n%s/share/chibi/core\n--compiler\n%s/share/chibi/compiler\n"
        "--compiler-core\n%s/share/chibi/"
        "core\n--entry\nNative\n--timeout-ms\n900000\n--\n"
        "%s/bin/../libexec/chibi/chibi-compiler-host\n%s/share/chibi/"
        "compiler\n%s/share/chibi/core\n"
        "%s\n%s\n0\nlines\narm64-apple-darwin\n",
        root,
        root,
        root,
        root,
        root,
        root,
        root,
        root,
        package,
        llvm
    );
    REQUIRE(length > 0 && length < (int)sizeof expected);
    REQUIRE(file_is("native.args", expected));
    write_file("native.fail", "");
    REQUIRE(tool_run(args, root, log_path) == 37);
    char failure[256];
    path(failure, "native.fail");
    REQUIRE(!unlink(failure));
    REQUIRE(!exists("native package/.chibi"));
    return true;
}

static bool watch_output(chibi_process *, size_t after, const char *expected);
static bool watch_stop(chibi_process *);
static void watch_failure_log(bool passed);

/* The authored CLI selects standalone source sessions without invoking its
 * compiler or player companions. */
TEST_CASE(run_dispatches_standalone_programs)
{
    directory("standalone dispatch");
    write_file("standalone dispatch/Hello.chibi", "package hello\nMain :: () { print_string(\"dispatch-main\") }\n");
    char folder[256], source[256], player[256];
    path(folder, "standalone dispatch");
    path(source, "standalone dispatch/Hello.chibi");
    path(player, "player.args");
    unlink(player);
    char *before = contents("compiles");
    const char *commands[][4] = {
        { launcher, "run", "standalone dispatch/Hello.chibi", NULL },
        { launcher, "standalone dispatch/Hello.chibi", NULL },
        { launcher, "run", "standalone dispatch", NULL },
        { launcher, "standalone dispatch", NULL },
    };
    for (int i = 0; i < 4; i++) {
        REQUIRE(tool_run(commands[i], root, log_path) == 0);
        REQUIRE(file_is("output.log", "dispatch-main\n"));
        REQUIRE(!exists("player.args"));
        REQUIRE(!exists("standalone dispatch/.chibi"));
    }
    const char *here[] = { launcher, ".", NULL };
    REQUIRE(tool_run(here, folder, log_path) == 0);
    REQUIRE(file_is("output.log", "dispatch-main\n"));
    const char *implicit[] = { launcher, "run", NULL };
    REQUIRE(tool_run(implicit, folder, log_path) == 0);
    REQUIRE(file_is("output.log", "dispatch-main\n"));

    const char *watch_commands[][6] = {
        { launcher, "--watch", source, NULL },
        { launcher, "run", "--watch", source, NULL },
        { launcher, source, "--watch", NULL },
        { launcher, "run", source, "--watch", NULL },
        { launcher, "--watch", folder, NULL },
        { launcher, "run", "--watch", folder, NULL },
        { launcher, folder, "--watch", NULL },
        { launcher, "run", folder, "--watch", NULL },
        { launcher, ".", "--watch", NULL },
        { launcher, "run", "--watch", NULL },
    };
    for (int i = 0; i < 10; i++) {
        chibi_process process;
        REQUIRE(chibi_process_start(&process, watch_commands[i], i < 8 ? root : folder, log_path));
        tool_own_process(process.pid);
        bool passed = watch_output(&process, 0, "dispatch-main\n");
        bool stopped = watch_stop(&process);
        watch_failure_log(passed && stopped);
        REQUIRE(passed && stopped);
    }
    const char *watch_games[][5] = {
        { launcher, "--watch", "game with spaces", NULL },
        { launcher, "run", "--watch", "game with spaces", NULL },
        { launcher, "game with spaces", "--watch", NULL },
    };
    for (int i = 0; i < 3; i++) {
        REQUIRE(tool_run(watch_games[i], root, log_path) == 2);
    }
    const char *extra[] = { launcher, "run", source, "--", "extra", NULL };
    REQUIRE(tool_run(extra, root, log_path) == 0);
    REQUIRE(file_is("output.log", "dispatch-main\n"));
    const char *unnamed_extra[] = { launcher, source, "extra", NULL };
    REQUIRE(tool_run(unnamed_extra, root, log_path) == 0);
    REQUIRE(file_is("output.log", "dispatch-main\n"));
    write_file("fail", "");
    REQUIRE(tool_run(commands[0], root, log_path) == 0);
    REQUIRE(file_is("output.log", "dispatch-main\n"));
    char failure[256];
    path(failure, "fail");
    REQUIRE(!unlink(failure));
    REQUIRE(file_is("compiles", before));
    free(before);
    REQUIRE(!exists("player.args"));
    REQUIRE(!exists("standalone dispatch/.chibi"));
    return true;
}

static const char HELLO_PROGRAM[] = "package hello\n"
                                    "Main :: () {\n"
                                    "    print_string(\"Hello, world!\")\n"
                                    "}\n";

/* Runs a program through the actual authored CLI. Installed SDK
 * coverage uses the same cases with the installed binaries. */
static int run_program(const char *target)
{
    const char *args[] = { built, "run", target, NULL };
    return tool_run(args, root, log_path);
}

TEST_CASE(program_main_runs_from_files_and_directories)
{
    directory("run-main");
    write_file("run-main/Hello.chibi", HELLO_PROGRAM);
    REQUIRE(run_program("run-main/Hello.chibi") == 0);
    REQUIRE(file_is("output.log", "Hello, world!\n"));
    const char *file[] = { built, "run-main/Hello.chibi", NULL };
    REQUIRE(tool_run(file, root, log_path) == 0);
    REQUIRE(file_is("output.log", "Hello, world!\n"));
    REQUIRE(run_program("run-main") == 0);
    REQUIRE(file_is("output.log", "Hello, world!\n"));
    const char *folder[] = { built, "run-main", NULL };
    REQUIRE(tool_run(folder, root, log_path) == 0);
    REQUIRE(file_is("output.log", "Hello, world!\n"));
    char cwd[256], source[256];
    path(cwd, "run-main");
    path(source, "run-main/Hello.chibi");
    const char *here[] = { built, ".", NULL };
    REQUIRE(tool_run(here, cwd, log_path) == 0);
    REQUIRE(file_is("output.log", "Hello, world!\n"));
    const char *implicit[] = { built, "run", NULL };
    REQUIRE(tool_run(implicit, cwd, log_path) == 0);
    REQUIRE(file_is("output.log", "Hello, world!\n"));
    const char *direct[] = { compiler, "--project-root", source, "--run", NULL };
    REQUIRE(tool_run(direct, root, log_path) == 2);
    REQUIRE(!file_has("output.log", "Hello, world!"));
    const char *conflict[] = { compiler, "--project-root", source,
                               "--run",  "--test",         NULL };
    REQUIRE(tool_run(conflict, root, log_path) == 2);

    write_file("run-main/Hello.chibi", "package hello\nMain :: () -> int { return 23 }\n");
    REQUIRE(run_program("run-main/Hello.chibi") == 23);
    REQUIRE(file_is("output.log", ""));
    REQUIRE(file_is("run-main/Hello.chibi", "package hello\nMain :: () -> int { return 23 }\n"));
    REQUIRE(!exists("run-main/.chibi"));
    return true;
}

TEST_CASE(authored_applications_use_independent_program_sessions)
{
    directory("public-programs");
    const char *counter = "package counter\nMain :: () -> int {\n"
                          " count: retain int = 0; count += 1\n"
                          " args := application.arguments()\n"
                          " assert(length(args) == 1, \"session arguments\")\n"
                          " base := 0\n"
                          " if args[0] == \"first\" { base = 100 } else {\n"
                          "  assert(args[0] == \"second\", \"session argument "
                          "value\"); base = 200\n"
                          " }\n return base + count\n}\n";
    write_file("public-programs/Counter.chibi", counter);
    const char *entry = strstr(counter, "return base + count");
    assert(entry);
    char replacement[1024];
    int size = snprintf(
        replacement,
        sizeof replacement,
        "%.*sreturn base + count + 40\n}\n",
        (int)(entry - counter),
        counter
    );
    assert(size > 0 && size < (int)sizeof replacement);
    write_file("public-programs/Reload.chibi", replacement);
    write_file(
        "public-programs/Controller.chibi",
        "package controller\nMain :: () {\n"
        " args := application.arguments()\n"
        " assert(length(args) == 3, \"application arguments\")\n"
        " path := args[1]\n"
        " first := program.open(path, \"Main\", [\"first\"])\n"
        " second := program.open(path, \"Main\", [\"second\"])\n"
        " assert(first != second, \"distinct program handles\")\n"
        " assert(program.update(first) == 1, \"first session prepares\")\n"
        " assert(program.update(second) == 1, \"second session prepares\")\n"
        " a := program.call(first); b := program.call(second)\n"
        " assert(a.status == 0 && a.result == 101, \"first session\")\n"
        " assert(b.status == 0 && b.result == 201, \"second session\")\n"
        " source := files.read(args[2])\n"
        " assert(files.write(path, source), \"write valid edit\")\n"
        " assert(program.update(first) == 1, \"first session prepares\")\n"
        " a = program.call(first); b = program.call(second)\n"
        " assert(a.status == 0 && a.result == 142, \"retained reload\")\n"
        " assert(b.status == 0 && b.result == 202, \"independent "
        "generation\")\n"
        " assert(files.write(path, \"package counter\\nMain :: () { missing() "
        "}\\n\"), \"write invalid edit\")\n"
        " assert(program.update(first) == -1, \"reject invalid edit\")\n"
        " assert(program.diagnostic(first) != \"\", \"source diagnostic\")\n"
        " a = program.call(first)\n"
        " assert(a.status == 0 && a.result == 143, \"last valid state\")\n"
        " program.close(first)\n"
        " b = program.call(second)\n"
        " assert(b.status == 0 && b.result == 203, \"other handle remains "
        "alive\")\n"
        " program.close(second)\n"
        " print_string(\"public-programs-ok\")\n}\n"
    );
    char source[256], reload[256];
    path(source, "public-programs/Counter.chibi");
    path(reload, "public-programs/Reload.chibi");
    const char *args[] = { built,  "public-programs/Controller.chibi",
                           "--",   source,
                           reload, NULL };
    REQUIRE(tool_run(args, root, log_path) == 0);
    REQUIRE(file_is("output.log", "public-programs-ok\n"));
    REQUIRE(!exists("public-programs/.chibi"));
    return true;
}

TEST_CASE(program_sources_link_packages_and_select_the_root_entry)
{
    directory("run-file");
    write_file(
        "run-file/Hello.chibi",
        "package hello\nimport strings \"core:strings\"\n"
        "Main :: () { print_string(strings.Join([\"Hello\", \"world!\"], \", "
        "\")) }\n"
    );
    /* Selecting a source file must not parse a neighboring source. */
    write_file("run-file/Broken.chibi", "this is not valid Chibi\n");
    REQUIRE(run_program("run-file/Hello.chibi") == 0);
    REQUIRE(file_is("output.log", "Hello, world!\n"));
    REQUIRE(!exists("run-file/.chibi"));

    directory("run-packages");
    directory("run-packages/words");
    write_file(
        "run-packages/Hello.chibi",
        "package hello\nimport words \"project:words\"\n"
        "Main :: () { print_string(Greeting(words.Message())) }\n"
    );
    write_file(
        "run-packages/Greeting.chibi",
        "package hello\n"
        "Greeting :: (message: string) -> string { return message }\n"
    );
    write_file(
        "run-packages/words/Words.chibi",
        "package words\n"
        "pub Message :: () -> string { return \"Local packages\" }\n"
        "Main :: () -> int { return 31 }\n"
    );
    REQUIRE(run_program("run-packages") == 0);
    REQUIRE(file_is("output.log", "Local packages\n"));
    /* The helper is available only when the directory is the target. */
    REQUIRE(run_program("run-packages/Greeting.chibi") == 1);
    REQUIRE(file_has("output.log", "Main"));
    REQUIRE(!exists("run-packages/.chibi"));
    REQUIRE(!exists("run-packages/words/.chibi"));
    return true;
}

static bool clone_program_file(const char *source, const char *relative)
{
    char destination[256];
    path(destination, relative);
#ifdef __APPLE__
    const char *args[] = { "/bin/cp", "-c", source, destination, NULL };
#else
    const char *args[] = { "/bin/cp", "--reflink=auto", source, destination, NULL };
#endif
    return tool_run(args, root, log_path) == 0;
}

TEST_CASE(public_cli_is_a_relocatable_ordinary_application)
{
    const char *directories[] = { "program-sdk",
                                  "program-sdk/bin",
                                  "program-sdk/share",
                                  "program-sdk/share/chibi",
                                  "unrelated-program-cwd" };
    for (size_t i = 0; i < sizeof directories / sizeof *directories; i++) {
        directory(directories[i]);
    }
    REQUIRE(clone_program_file(built, "program-sdk/bin/chibi"));
    REQUIRE(clone_program_file(compiler, "program-sdk/bin/chibi-compile"));
    char share[256], application[256], relocated[256], source[256], cwd[256];
    path(share, "program-sdk/share/chibi");
    REQUIRE(copy_resource(built, "apps", share));
    REQUIRE(copy_resource(built, "core", share));
    REQUIRE(copy_resource(built, "engine.json", share));
    path(application, "program-sdk/share/chibi/apps/chibi");
    path(relocated, "program-sdk/bin/chibi");
    path(source, "Hello.chibi");
    path(cwd, "unrelated-program-cwd");
    write_file("Hello.chibi", HELLO_PROGRAM);
    const char *args[] = { relocated, source, NULL };
    REQUIRE(tool_run(args, cwd, log_path) == 0);
    REQUIRE(file_is("output.log", "Hello, world!\n"));
    /* The public CLI itself runs through the ordinary source-session API. */
    const char *self[] = { relocated, application, "--", "version", NULL };
    REQUIRE(tool_run(self, cwd, log_path) == 0);
    REQUIRE(file_has("output.log", "chibi "));
    /* Replacing the whole package controls every command without C fallback. */
    tool_remove_tree(application);
    directory("program-sdk/share/chibi/apps/chibi");
    write_file("program-sdk/share/chibi/apps/chibi/Main.chibi", "package chibi\nMain :: () -> int { return 42 }\n");
    REQUIRE(tool_run(args, cwd, log_path) == 42);
    REQUIRE(file_is("output.log", ""));
    const char *version[] = { relocated, "version", NULL };
    REQUIRE(tool_run(version, cwd, log_path) == 42);
    REQUIRE(file_is("output.log", ""));
    tool_remove_tree(application);
    REQUIRE(tool_run(args, cwd, log_path) == 1);
    REQUIRE(file_has("output.log", "apps/chibi"));
    REQUIRE(!file_has("output.log", "Hello, world!"));
    directory("program-sdk/share/chibi/apps/chibi");
    write_file("program-sdk/share/chibi/apps/chibi/Main.chibi", "package chibi\nMain :: ( {\n");
    REQUIRE(tool_run(args, cwd, log_path) == 1);
    REQUIRE(file_has("output.log", "Main.chibi"));
    REQUIRE(!file_has("output.log", "Hello, world!"));
    tool_remove_tree(application);
    REQUIRE(copy_resource(built, "apps", share));
    REQUIRE(tool_run(args, cwd, log_path) == 0);
    REQUIRE(file_is("output.log", "Hello, world!\n"));
    REQUIRE(!exists("program-sdk/.chibi"));
    REQUIRE(!exists("unrelated-program-cwd/.chibi"));

    /* A valued option must not select run mode because its value is --run. */
    project("--run", CHIBI_PROJECT_VERSION);
    const char *check[] = { compiler, "--project-root", "--run", "--check", NULL };
    REQUIRE(tool_run(check, root, log_path) == 0);
    REQUIRE(!exists("--run/.chibi"));
    return true;
}

TEST_CASE(program_failures_report_entry_and_source_errors)
{
    directory("run-errors");
    const char *invalid_entries[] = {
        "package hello\nHelper :: () {}\n",
        "package hello\nMain :: (value: int) {}\n",
        "package hello\nMain :: (value: $T) {}\n",
        "package hello\nMain :: () -> string { return \"wrong\" }\n",
        "package hello\nMain :: () -> int? { return 0 }\n",
        "package hello\nMain :: () -> (a: int, b: int) { return(1, 2) }\n",
        "package hello\nMain :: () -> int { return 256 }\n",
        "package hello\nMain :: () -> int { return -1 }\n",
    };
    for (size_t i = 0; i < sizeof invalid_entries / sizeof *invalid_entries; i++) {
        write_file("run-errors/Hello.chibi", invalid_entries[i]);
        REQUIRE(run_program("run-errors/Hello.chibi") == 1);
        REQUIRE(file_has("output.log", "Main"));
    }
    write_file(
        "run-errors/Hello.chibi",
        "package hello\nFail :: () {\n"
        "    assert(false, \"nested program failure\")\n}\nMain :: () { Fail() "
        "}\n"
    );
    REQUIRE(run_program("run-errors/Hello.chibi") == 1);
    REQUIRE(file_has("output.log", "/run-errors/Hello.chibi:3:5:"));
    REQUIRE(file_has("output.log", "Assertion failed: nested program failure"));
    write_file("run-errors/Hello.chibi", "package hello\nMain :: () { missing() }\n");
    REQUIRE(run_program("run-errors/Hello.chibi") == 1);
    REQUIRE(file_has("output.log", "/run-errors/Hello.chibi:2:"));
    write_file("run-errors/Hello.chibi", "package hello\nMain :: () { runner.snapshot() }\n");
    REQUIRE(run_program("run-errors/Hello.chibi") == 1);
    REQUIRE(file_has("output.log", "Unknown value 'runner'"));
    write_file("run-errors/Hello.chibi", "Main :: () {}\n");
    REQUIRE(run_program("run-errors/Hello.chibi") == 1);
    REQUIRE(file_has("output.log", "/run-errors/Hello.chibi:1:1:"));
    write_file("run-errors/Hello.chibi", "package hello\nMain :: () { await delay(1) }\n");
    REQUIRE(run_program("run-errors/Hello.chibi") == 1);
    REQUIRE(file_has("output.log", "waiting"));
    write_file(
        "run-errors/Hello.chibi",
        "package hello\nWait :: () { await delay(1) }\n"
        "Main :: () { Wait() }\n"
    );
    REQUIRE(run_program("run-errors/Hello.chibi") == 1);
    REQUIRE(file_has("output.log", "task"));

    /* An imported package's Main cannot replace the root's missing entry. */
    directory("run-errors/words");
    write_file(
        "run-errors/Hello.chibi",
        "package hello\nimport words \"project:words\"\n"
        "Value :: words.Value\n"
    );
    write_file(
        "run-errors/words/Words.chibi",
        "package words\npub Value :: 1\n"
        "Main :: () { print_string(\"wrong entry\") }\n"
    );
    REQUIRE(run_program("run-errors") == 1);
    REQUIRE(file_has("output.log", "Main"));
    REQUIRE(!file_has("output.log", "wrong entry"));
    REQUIRE(!exists("run-errors/.chibi"));
    return true;
}

/* Each handshake checks output written after that edit. Failures still reach
 * process cleanup before an assertion can return from the test. */
static size_t watch_output_size()
{
    char *text = tool_read(log_path);
    size_t length = text ? strlen(text) : 0;
    free(text);
    return length;
}

static bool watch_output(chibi_process *process, size_t after, const char *expected)
{
    double deadline = chibi_process_now() + TOOL_RUN_SECONDS;
    do {
        char *text = tool_read(log_path);
        bool found = text && strlen(text) >= after && strstr(text + after, expected);
        free(text);
        if (found) {
            return !chibi_process_poll(process);
        }
        if (chibi_process_poll(process)) {
            break;
        }
        usleep(10000);
    } while (chibi_process_now() < deadline);
    fprintf(stderr, "watch output did not contain %s\n", expected);
    return false;
}

static bool stop_process(chibi_process *process, int expected, bool allow_signal, int interrupt)
{
    if (interrupt == SIGTERM) {
        chibi_process_stop(process);
    } else if (!process->finished) {
        kill(-process->pid, interrupt);
    }
    double deadline = chibi_process_now() + 5;
    while (!chibi_process_poll(process) && chibi_process_now() < deadline) {
        usleep(10000);
    }
    if (!process->finished) {
        kill(-process->pid, SIGKILL);
        deadline = chibi_process_now() + 1;
        while (!chibi_process_poll(process) && chibi_process_now() < deadline) {
            usleep(1000);
        }
    }
    bool stopped = process->finished &&
                   (process->status == expected ||
                    (allow_signal && process->status == 128 + SIGTERM));
    deadline = chibi_process_now() + 2;
    while (!kill(-process->pid, 0) && chibi_process_now() < deadline) {
        usleep(10000);
    }
    bool empty = kill(-process->pid, 0) < 0 && errno == ESRCH;
    if (!empty) {
        kill(-process->pid, SIGKILL);
    }
    tool_release_process(process->pid);
    if (!stopped || !empty) {
        fprintf(
            stderr,
            "program shutdown status=%d process-group-empty=%d\n",
            process->status,
            empty
        );
    }
    return stopped && empty;
}

static bool watch_stop(chibi_process *process)
{
    return stop_process(process, 0, true, SIGTERM);
}

static void watch_failure_log(bool passed)
{
    if (passed) {
        return;
    }
    char *text = tool_read(log_path);
    if (text) {
        fputs(text, stderr);
        free(text);
    }
}

TEST_CASE(program_cancellation_stops_busy_main_and_tests)
{
    directory("cancel-main");
    directory("cancel-tests");
    write_file("cancel-main/Busy.chibi", "package busy\nMain :: () { print_string(\"busy-main-ready\"); while true {} }\n");
    write_file("cancel-tests/Busy.chibi", "package busy_tests\n@test\nBusy :: () { print_string(\"busy-test-ready\"); while true {} }\n");
    const char *commands[][5] = {
        { built, "cancel-main/Busy.chibi", NULL },
        { built, "cancel-main/Busy.chibi", NULL },
        { built, "--watch", "cancel-main/Busy.chibi", NULL },
        { built, "--watch", "cancel-main/Busy.chibi", NULL },
        { built, "test", "cancel-tests", NULL },
        { built, "test", "cancel-tests", NULL },
    };
    for (int i = 0; i < 6; i++) {
        const char *marker = i < 4 ? "busy-main-ready\n" : "busy-test-ready\n";
        chibi_process process;
        REQUIRE(chibi_process_start(&process, commands[i], root, log_path));
        tool_own_process(process.pid);
        bool passed = watch_output(&process, 0, marker);
        int interrupt = i % 2 ? SIGTERM : SIGINT;
        int expected = i == 2 || i == 3 ? 0 : 128 + interrupt;
        bool stopped = stop_process(&process, expected, false, interrupt);
        watch_failure_log(passed && stopped);
        REQUIRE(passed && stopped);
        if (i < 4) {
            REQUIRE(file_is("output.log", marker));
        }
    }
    REQUIRE(!exists("cancel-main/.chibi"));
    REQUIRE(!exists("cancel-tests/.chibi"));
    return true;
}

#ifdef __linux__
/* An orphaned process that was killed stays a zombie until its new parent
 * reaps it, which a CI runner's subreaper may never do. A zombie is not running. */
static bool process_state(int pid, char *state, int *group)
{
    char name[64], text[512];
    snprintf(name, sizeof name, "/proc/%d/stat", pid);
    FILE *file = fopen(name, "r");
    if (!file) {
        return false;
    }
    size_t size = fread(text, 1, sizeof text - 1, file);
    fclose(file);
    text[size] = 0;
    const char *end = strrchr(text, ')');
    int parent;
    return end && sscanf(end + 1, " %c %d %d", state, &parent, group) == 3;
}

static bool pid_gone(int pid)
{
    assert(pid > 0);
    char state;
    int group;
    return !process_state(pid, &state, &group) || state == 'Z' || state == 'X';
}

static bool group_gone(int pid)
{
    assert(pid > 0);
    DIR *directory = opendir("/proc");
    assert(directory);
    bool gone = true;
    for (struct dirent *entry; gone && (entry = readdir(directory));) {
        int member = atoi(entry->d_name);
        char state;
        int group;
        gone = member <= 0 || !process_state(member, &state, &group) ||
               group != pid || state == 'Z' || state == 'X';
    }
    closedir(directory);
    return gone;
}
#else
static bool pid_gone(int pid)
{
    assert(pid > 0);
    return kill(pid, 0) < 0 && errno == ESRCH;
}

static bool group_gone(int pid)
{
    assert(pid > 0);
    return kill(-pid, 0) < 0 && errno == ESRCH;
}
#endif

/* Signal only the public CLI PID. Its owned process.run child may ignore
 * signals or retain inherited streams through a descendant. */
TEST_CASE(program_process_run_stops_children_when_only_parent_is_signalled)
{
    directory("process-interrupt");
    const char *scripts[] = {
        "printf 'CHILD_READY %s READY_END\n' \"$$\"; exec /bin/sleep 30",
        "trap '' INT TERM; /bin/sh -c 'trap \"\" INT TERM; while :; do "
        "/bin/sleep 1; done' & "
        "descendant=$!; printf 'CHILD_READY %s %s READY_END\n' \"$$\" "
        "\"$descendant\"; "
        "wait",
    };
    for (int i = 0; i < 4; i++) {
        yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
        yyjson_mut_doc_set_root(doc, yyjson_mut_strcpy(doc, scripts[i / 2]));
        char *literal = yyjson_mut_write(doc, 0, NULL);
        REQUIRE(literal);
        char source[4096];
        int size = snprintf(source, sizeof source, "package process_interrupt\nMain :: () -> int { return process.run([\"/bin/sh\", \"-c\", %s]) }\n", literal);
        free(literal);
        yyjson_mut_doc_free(doc);
        REQUIRE(size > 0 && size < (int)sizeof source);
        write_file("process-interrupt/Main.chibi", source);
        const char *args[] = { built, "process-interrupt/Main.chibi", NULL };
        chibi_process process;
        REQUIRE(chibi_process_start(&process, args, root, log_path));
        tool_own_process(process.pid);
        bool ready = watch_output(&process, 0, "READY_END\n");
        int child = 0, descendant = 0, child_group = 0;
        char *text = tool_read(log_path);
        const char *marker = text ? strstr(text, "CHILD_READY ") : NULL;
        int parsed = marker ? sscanf(marker, "CHILD_READY %d %d", &child, &descendant)
                            : 0;
        bool known = ready && parsed == (i < 2 ? 1 : 2) && child > 0 &&
                     child != process.pid && (i < 2 || descendant > 0);
        free(text);
        if (known) {
            child_group = getpgid(child);
            known = (child_group == child || child_group == process.pid) &&
                    (!descendant || getpgid(descendant) == child_group);
        }
        int interrupt = i % 2 ? SIGTERM : SIGINT;
        bool signalled = known && !kill(process.pid, interrupt);
        double deadline = chibi_process_now() + 5;
        while (!chibi_process_poll(&process) && chibi_process_now() < deadline) {
            usleep(10000);
        }
        bool exited = process.finished && process.status == 128 + interrupt;
        deadline = chibi_process_now() + 2;
        bool child_retired = false, descendant_retired = !descendant;
        bool group_retired = false, root_retired = false;
        bool children_gone = false;
        do {
            child_retired |= known && pid_gone(child);
            descendant_retired |= known && descendant && pid_gone(descendant);
            group_retired |= known && group_gone(child_group);
            root_retired |= group_gone(process.pid);
            children_gone = known && child_retired && descendant_retired &&
                            (child_group != child || group_retired);
            if (children_gone) {
                break;
            }
            usleep(10000);
        } while (chibi_process_now() < deadline);
        /* Retire observed identities permanently. The fixture's children
         * stay in the verified group, including on a failed red run. */
        if (known && !children_gone && !group_retired) {
            kill(-child_group, SIGKILL);
        }
        if (!process.finished && !root_retired) {
            kill(-process.pid, SIGKILL);
        }
        deadline = chibi_process_now() + 3;
        while (!chibi_process_poll(&process) && chibi_process_now() < deadline) {
            usleep(10000);
        }
        deadline = chibi_process_now() + 2;
        do {
            root_retired |= group_gone(process.pid);
            child_retired |= known && pid_gone(child);
            descendant_retired |= known && descendant && pid_gone(descendant);
            group_retired |= known && group_gone(child_group);
            if (root_retired && (!known || (child_retired && descendant_retired &&
                                            (child_group != child || group_retired)))) {
                break;
            }
            usleep(10000);
        } while (chibi_process_now() < deadline);
        bool cleanup = process.finished && root_retired &&
                       (!known || (child_retired && descendant_retired &&
                                   (child_group != child || group_retired)));
        tool_release_process(process.pid);
        bool passed = known && signalled && exited && children_gone && cleanup;
        if (!passed) {
            fprintf(
                stderr,
                "parent-only signal=%d ready=%d child=%d descendant=%d "
                "child-group=%d CLI-status=%d exited=%d children-gone=%d "
                "cleanup=%d\n",
                interrupt,
                ready,
                child,
                descendant,
                child_group,
                process.status,
                exited,
                children_gone,
                cleanup
            );
        }
        watch_failure_log(passed);
        REQUIRE(passed);
    }
    return true;
}

static bool pty_output(chibi_process *process, int master, char *text, size_t capacity, const char *expected)
{
    double deadline = chibi_process_now() + TOOL_RUN_SECONDS;
    do {
        size_t used = strlen(text);
        if (used + 1 == capacity) {
            return false;
        }
        ssize_t count = read(master, text + used, capacity - used - 1);
        if (count > 0) {
            text[used + (size_t)count] = 0;
        }
        if (strstr(text, expected)) {
            return true;
        }
        if (chibi_process_poll(process) && count <= 0) {
            return false;
        }
        usleep(10000);
    } while (chibi_process_now() < deadline);
    return false;
}

static int start_pty(chibi_process *process, const char *const *arguments, bool redirect_input)
{
    int master = posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (master < 0) {
        return -1;
    }
    if (grantpt(master) || unlockpt(master)) {
        close(master);
        return -1;
    }
    const char *name = ptsname(master);
    if (!name) {
        close(master);
        return -1;
    }
    char slave_name[1024];
    int length = snprintf(slave_name, sizeof slave_name, "%s", name);
    assert(length > 0 && length < (int)sizeof slave_name);
    pid_t pid = fork();
    if (pid < 0) {
        close(master);
        return -1;
    }
    if (!pid) {
        close(master);
        if (setsid() < 0) {
            _exit(125);
        }
        int slave = open(slave_name, O_RDWR);
        if (slave < 0) {
            _exit(125);
        }
        /* Opening the slave after setsid establishes the controlling tty.
         * Reset inherited job-control dispositions so missing handoff fails. */
        ioctl(slave, TIOCSCTTY, 0);
        struct sigaction normal = { .sa_handler = SIG_DFL, .sa_flags = SA_RESTART };
        sigemptyset(&normal.sa_mask);
        sigaction(SIGTTIN, &normal, NULL);
        sigaction(SIGTTOU, &normal, NULL);
        sigaction(SIGTSTP, &normal, NULL);
        sigaction(SIGCONT, &normal, NULL);
        sigset_t blocked, clear;
        sigemptyset(&blocked);
        sigaddset(&blocked, SIGTTOU);
        sigprocmask(SIG_BLOCK, &blocked, NULL);
        struct termios terminal;
        if (tcgetattr(slave, &terminal)) {
            _exit(125);
        }
        terminal.c_lflag &= ~(ECHO | ECHONL);
        terminal.c_lflag |= ICANON | ISIG;
        terminal.c_oflag &= ~ONLCR;
        terminal.c_cc[VINTR] = 3;
        terminal.c_cc[VSUSP] = 26;
        if (tcsetattr(slave, TCSANOW, &terminal) || tcsetpgrp(slave, getpgrp()) ||
            dup2(slave, STDIN_FILENO) < 0 || dup2(slave, STDOUT_FILENO) < 0 ||
            dup2(slave, STDERR_FILENO) < 0 || chdir(root)) {
            _exit(125);
        }
        if (slave > STDERR_FILENO) {
            close(slave);
        }
        if (redirect_input) {
            int input = open("/dev/null", O_RDONLY);
            if (input < 0 || dup2(input, STDIN_FILENO) < 0) {
                _exit(125);
            }
            if (input > STDERR_FILENO) {
                close(input);
            }
        }
        sigemptyset(&clear);
        sigprocmask(SIG_SETMASK, &clear, NULL);
        execv(arguments[0], (char *const *)arguments);
        _exit(126);
    }
    *process = (chibi_process){ .pid = pid };
    tool_own_process(pid);
    return master;
}

static bool pty_stopped(chibi_process *process)
{
    double deadline = chibi_process_now() + 5;
    do {
        siginfo_t notice = { 0 };
        int error = waitid(P_PID, (id_t)process->pid, &notice, WSTOPPED | WNOHANG | WNOWAIT);
        if (error < 0 && errno != EINTR) {
            return false;
        }
        if (!error && notice.si_pid == process->pid && notice.si_code == CLD_STOPPED) {
            /* Consume only the observed stop. Exit status remains for poll. */
            int state = 0;
            pid_t observed;
            do {
                observed = waitpid(process->pid, &state, WUNTRACED | WNOHANG);
            } while (observed < 0 && errno == EINTR);
            return observed == process->pid && WIFSTOPPED(state);
        }
        usleep(10000);
    } while (chibi_process_now() < deadline);
    return false;
}

/* This mode shares the tooling executable. Darwin foreground changes may
 * interrupt reads even with default SA_RESTART, so retry EINTR explicitly. */
int tool_tty_echo_child(bool terminal)
{
    if (terminal && isatty(STDIN_FILENO)) {
        return 125;
    }
    int input = terminal ? open("/dev/tty", O_RDONLY) : STDIN_FILENO;
    if (input < 0) {
        return 125;
    }
#ifdef __APPLE__
    /* A foreground Darwin launch must acquire its tty before child code runs. */
    if (tcgetpgrp(input) != getpgrp()) {
        return 125;
    }
#endif
    char bytes[256];
    for (;;) {
        ssize_t count = read(input, bytes, sizeof bytes);
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            return 125;
        }
        if (!count) {
            return 0;
        }
        for (ssize_t offset = 0; offset < count;) {
            ssize_t written = write(STDOUT_FILENO, bytes + offset, (size_t)(count - offset));
            if (written < 0 && errno == EINTR) {
                continue;
            }
            if (written <= 0) {
                return 125;
            }
            offset += written;
        }
    }
}

TEST_CASE(program_process_run_hands_the_terminal_to_its_child)
{
    directory("pty-process");
    for (int i = 0; i < 6; i++) {
        bool suspend = i >= 2 && i < 5, watching = i == 1 || i == 3, redirect = i == 4;
        /* A child that already exited must hand the terminal back for the next. */
        bool after_exit = i == 5;
        yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
        yyjson_mut_doc_set_root(doc, yyjson_mut_strcpy(doc, suspend ? driver : "/bin/cat"));
        char *executable = yyjson_mut_write(doc, 0, NULL);
        REQUIRE(executable);
        char source[2048];
        int length = snprintf(
            source,
            sizeof source,
            "package pty_process\nMain :: () -> int { "
            "%s"
            "print_string(\"CAT_READY\"); "
            "return process.run([%s%s]) }\n",
            after_exit ? "process.run([\"/usr/bin/true\"]); " : "",
            executable,
            redirect  ? ", \"--tty-echo-child\", \"--tty\""
            : suspend ? ", \"--tty-echo-child\""
                      : ""
        );
        free(executable);
        yyjson_mut_doc_free(doc);
        REQUIRE(length > 0 && length < (int)sizeof source);
        write_file("pty-process/Main.chibi", source);
        const char *ordinary[] = { built, "pty-process/Main.chibi", NULL };
        const char *watch[] = { built, "--watch", "pty-process/Main.chibi", NULL };
        chibi_process process;
        int master = start_pty(&process, watching ? watch : ordinary, redirect);
        REQUIRE(master >= 0);
        char output[4096] = { 0 };
        bool ready = pty_output(&process, master, output, sizeof output, "CAT_READY\n");
        int child = 0;
        double handoff = chibi_process_now() + 5;
        while (ready && !chibi_process_poll(&process) && chibi_process_now() < handoff) {
            child = tcgetpgrp(master);
            if (child > 0 && child != process.pid) {
                break;
            }
            usleep(10000);
        }
        int group = child > 0 ? getpgid(child) : 0;
        bool known = ready && child > 0 && child != process.pid && group == child;
        const char line[] = "PTY_LINE_TOKEN\n";
        bool input = known && write(master, line, sizeof line - 1) ==
                                  (ssize_t)(sizeof line - 1);
        bool read_line = input &&
                         pty_output(&process, master, output, sizeof output, line);
        bool foreground = read_line && known && tcgetpgrp(master) == child;
        bool suspended = !suspend || (foreground && write(master, "\032", 1) == 1);
        bool stopped = !suspend || (suspended && pty_stopped(&process));
        bool restored = !suspend || (stopped && tcgetpgrp(master) == process.pid);
        bool resumed = !suspend || (restored && !kill(-process.pid, SIGCONT));
        const char second[] = "PTY_AFTER_FG_TOKEN\n";
        bool second_input = resumed && write(master, second, sizeof second - 1) ==
                                           (ssize_t)(sizeof second - 1);
        bool read_second = second_input &&
                           pty_output(&process, master, output, sizeof output, second);
        bool foreground_again = read_second && tcgetpgrp(master) == child;
        bool interrupted = foreground_again && write(master, "\003", 1) == 1;
        double deadline = chibi_process_now() + 5;
        while (interrupted && !chibi_process_poll(&process) &&
               chibi_process_now() < deadline) {
            usleep(10000);
        }
        bool exited = process.finished &&
                      process.status == (watching ? 0 : 128 + SIGINT);
        bool child_retired = false, group_retired = false, root_retired = false;
        deadline = chibi_process_now() + (exited ? 2 : 0);
        do {
            child_retired |= known && pid_gone(child);
            group_retired |= known && group_gone(group);
            root_retired |= group_gone(process.pid);
            if (root_retired &&
                (!known || (child_retired && (group != child || group_retired)))) {
                break;
            }
            usleep(10000);
        } while (chibi_process_now() < deadline);
        if (known && !group_retired) {
            kill(-group, SIGKILL);
        }
        if (!process.finished && !root_retired) {
            kill(-process.pid, SIGKILL);
        }
        deadline = chibi_process_now() + 3;
        while (!chibi_process_poll(&process) && chibi_process_now() < deadline) {
            usleep(10000);
        }
        close(master);
        deadline = chibi_process_now() + 2;
        do {
            root_retired |= group_gone(process.pid);
            child_retired |= known && pid_gone(child);
            group_retired |= known && group_gone(group);
            if (root_retired &&
                (!known || (child_retired && (group != child || group_retired)))) {
                break;
            }
            usleep(10000);
        } while (chibi_process_now() < deadline);
        bool cleanup = process.finished && root_retired &&
                       (!known ||
                        (child_retired && (group != child || group_retired)));
        tool_release_process(process.pid);
        bool passed = known && foreground && stopped && restored && resumed &&
                      foreground_again && interrupted && exited && cleanup;
        if (!passed) {
            fprintf(
                stderr,
                "PTY mode=%d ready=%d child=%d group=%d input=%d read-line=%d "
                "foreground=%d stopped=%d restored=%d resumed=%d "
                "second-line=%d "
                "foreground-again=%d interrupted=%d status=%d cleanup=%d\n%s\n",
                i,
                ready,
                child,
                group,
                input,
                read_line,
                foreground,
                stopped,
                restored,
                resumed,
                read_second,
                foreground_again,
                interrupted,
                process.status,
                cleanup,
                output
            );
        }
        REQUIRE(passed);
    }
    return true;
}

static int watch_count(const char *text)
{
    char *log = tool_read(log_path);
    int count = 0;
    for (const char *at = log; at && (at = strstr(at, text)); at++) {
        count++;
    }
    free(log);
    return count;
}

TEST_CASE(program_watch_patches_a_running_frame_and_keeps_state)
{
    directory("watch-file");
    write_file(
        "watch-file/Hello.chibi",
        "package watch_file\nMain :: () { print_string(\"watch-main\") }\n"
        "Frame :: () {\n"
        "    count: retain int = 0\n"
        "    count += 1\n"
        "    ordinary: int = 0\n"
        "    ordinary += 1\n"
        "    assert(ordinary == 1, \"ordinary Frame local\")\n"
        "    if count % 10 == 0 { print_string(format_text(\"watch-first {}\", "
        "count)) }\n}\n"
    );
    const char *args[] = { built, "--watch", "watch-file/Hello.chibi", NULL };
    chibi_process process;
    REQUIRE(chibi_process_start(&process, args, root, log_path));
    tool_own_process(process.pid);
    bool passed = watch_output(&process, 0, "watch-first 10\n");
    if (passed) {
        /* A file target neither compiles nor watches its sibling sources. */
        write_file("watch-file/Broken.chibi", "not valid Chibi\n");
        size_t after = watch_output_size();
        write_file(
            "watch-file/Hello.chibi",
            "package watch_file\nMain :: () { print_string(\"watch-main\") }\n"
            "Frame :: () {\n"
            "    count: retain int = 0\n"
            "    count += 1\n"
            "    assert(count >= 10, \"retained Frame count\")\n"
            "    if count % 10 == 0 { print_string(format_text(\"watch-second "
            "{}\", count)) }\n}\n"
        );
        passed = watch_output(&process, after, "watch-second ");
    }
    if (passed) {
        size_t after = watch_output_size();
        /* Conversion uses the runtime's existing numeric-state migration, and a
         * binding added by the edit starts from its initializer. */
        write_file(
            "watch-file/Hello.chibi",
            "package watch_file\n"
            "Frame :: () {\n"
            "    count: retain number = 0\n"
            "    printed: retain bool = false\n"
            "    count += 1\n"
            "    assert(count >= 20, \"converted Frame count\")\n"
            "    if !printed { printed = true; print_string(\"watch-third\") "
            "}\n}\n"
        );
        passed = watch_output(&process, after, "watch-third");
    }
    const char *invalid[] = {
        "package watch_file\nFrame :: ( {\n",
        "package watch_file\nHelper :: () {}\n",
        "package watch_file\nFrame :: (value: int) {}\n"
    };
    const char *errors[] = { "/watch-file/Hello.chibi:", "No runnable Frame", "No runnable Frame" };
    for (int i = 0; passed && i < 3; i++) {
        size_t after = watch_output_size();
        write_file("watch-file/Hello.chibi", invalid[i]);
        passed = watch_output(&process, after, errors[i]);
    }
    if (passed) {
        /* A removed Frame drops its state; restoring it resumes the frames. */
        size_t after = watch_output_size();
        write_file(
            "watch-file/Hello.chibi",
            "package watch_file\nFrame :: () {\n"
            "    count: retain int = 0\n"
            "    count += 1\n"
            "    if count == 3 { print_string(\"watch-fourth\") }\n}\n"
        );
        passed = watch_output(&process, after, "watch-fourth");
    }
    bool stopped = watch_stop(&process);
    watch_failure_log(passed && stopped);
    REQUIRE(passed);
    REQUIRE(stopped);
    REQUIRE(watch_count("watch-main") == 1);
    REQUIRE(!exists("watch-file/.chibi"));
    return true;
}

static void write_watch_sibling(const char *marker)
{
    char text[512];
    int length = snprintf(
        text,
        sizeof text,
        "package watch_directory\nMarker :: () -> string { return \"%s\" }\n",
        marker
    );
    assert(length > 0 && length < (int)sizeof text);
    write_file("watch-directory/Values.chibi", text);
}

static void write_watch_import(const char *marker)
{
    char text[768];
    int length = snprintf(
        text,
        sizeof text,
        "package words\n"
        "pub Next :: () -> int {\n"
        "    count: retain int = 0\n"
        "    count += 1\n"
        "    return count\n}\n"
        "pub Marker :: () -> string { return \"%s\" }\n",
        marker
    );
    assert(length > 0 && length < (int)sizeof text);
    write_file("watch-directory/words/Words.chibi", text);
}

TEST_CASE(program_watch_tracks_siblings_and_keeps_imported_state)
{
    directory("watch-directory");
    directory("watch-directory/words");
    write_file(
        "watch-directory/Main.chibi",
        "package watch_directory\nimport words \"project:words\"\n"
        "Main :: () { print_string(\"watch-directory-main\") }\n"
        "Frame :: () {\n"
        "    last: retain int = 0\n"
        "    now := words.Next()\n"
        "    assert(now == last + 1, \"imported retained count\")\n"
        "    last = now\n"
        "    if now % 10 == 0 {\n"
        "        print_string(Marker())\n"
        "        print_string(words.Marker())\n"
        "    }\n}\n"
    );
    write_watch_sibling("watch-directory-first");
    write_watch_import("watch-import-first");
    char cwd[256];
    path(cwd, "watch-directory");
    const char *args[] = { built, "run", "--watch", ".", NULL };
    chibi_process process;
    REQUIRE(chibi_process_start(&process, args, cwd, log_path));
    tool_own_process(process.pid);
    bool passed = watch_output(&process, 0, "watch-import-first\n");
    if (passed) {
        size_t after = watch_output_size();
        write_watch_sibling("watch-directory-second");
        passed = watch_output(&process, after, "watch-directory-second\n");
    }
    if (passed) {
        size_t after = watch_output_size();
        write_watch_import("watch-import-third");
        passed = watch_output(&process, after, "watch-import-third\n");
    }
    if (passed) {
        size_t after = watch_output_size();
        write_file("watch-directory/words/Words.chibi", "package words\npub Next :: ( {\n");
        passed = watch_output(&process, after, "/watch-directory/words/Words.chibi:");
    }
    if (passed) {
        size_t after = watch_output_size();
        write_watch_import("watch-import-fourth");
        passed = watch_output(&process, after, "watch-import-fourth\n");
    }
    bool stopped = watch_stop(&process);
    watch_failure_log(passed && stopped);
    REQUIRE(passed);
    REQUIRE(stopped);
    REQUIRE(watch_count("watch-directory-main") == 1);
    REQUIRE(!exists("watch-directory/.chibi"));
    REQUIRE(!exists("watch-directory/words/.chibi"));
    return true;
}

static const char MESSY_PACKAGE[] = "package lib\nDouble :: (x: int) -> int "
                                    "{   return x*2; }\n";

/* Below a folder without a project.kdl, the real compiler's --format formats
 * each package source, naming them as it does a project's. A file that is
 * not a package, or a tree the package loader refuses, fails the run before
 * anything is written, and so does a folder with no sources. */
TEST_CASE(compiler_format_rewrites_package_sources)
{
    directory("tree");
    directory("tree/lib");
    directory("tree/more");
    write_file("tree/lib/Lib.chibi", MESSY_PACKAGE);
    write_file("tree/more/More.chibi", "package more\n");
    char tree[256], output[256];
    path(tree, "tree");
    path(output, "packages.log");
    const char *check[] = { compiler,   "--project-root", tree,
                            "--format", "--check",        NULL };
    REQUIRE(tool_run(check, root, output) == 1);
    REQUIRE(file_is("packages.log", "not canonical: lib/Lib.chibi\n"));
    REQUIRE(file_is("tree/lib/Lib.chibi", MESSY_PACKAGE));

    directory("tree/z");
    write_file("tree/z/Spin.chibi", MESSY_SOURCE);
    const char *format[] = { compiler, "--project-root", tree, "--format", NULL };
    REQUIRE(tool_run(format, root, output) == 1);
    REQUIRE(file_has("packages.log", "z/Spin.chibi: it does not declare a package"));
    REQUIRE(file_is("tree/lib/Lib.chibi", MESSY_PACKAGE));
    char spin[256];
    path(spin, "tree/z/Spin.chibi");
    REQUIRE(!unlink(spin));
    char link[256];
    path(link, "tree/z/Link.chibi");
    REQUIRE(!symlink("../lib/Lib.chibi", link));
    REQUIRE(tool_run(format, root, output) == 1);
    REQUIRE(file_has("packages.log", "Package descendants must not be symlinks"));
    REQUIRE(file_is("tree/lib/Lib.chibi", MESSY_PACKAGE));
    REQUIRE(!unlink(link));

    REQUIRE(tool_run(format, root, output) == 0);
    REQUIRE(file_is("packages.log", "formatted lib/Lib.chibi\n"));
    REQUIRE(
        file_is("tree/lib/Lib.chibi", "package lib\nDouble :: (x: int) -> int {\n    return x * 2;\n}\n")
    );
    REQUIRE(tool_run(check, root, output) == 0);

    write_file("tree/z/Broken.chibi", "package z\nBroken :: ( {\n");
    REQUIRE(tool_run(check, root, output) == 1);
    REQUIRE(file_has("packages.log", "z/Broken.chibi: "));

    directory("bare");
    char bare[256];
    path(bare, "bare");
    const char *empty[] = { compiler, "--project-root", bare, "--format", NULL };
    REQUIRE(tool_run(empty, root, output) == 1);
    REQUIRE(file_has("packages.log", "holds no project.kdl or package files"));
    return true;
}

/* The whole response to a GET of target from 127.0.0.1:port naming host;
 * NULL when nothing answers. */
static char *fetch(int port, const char *method, const char *target, const char *host)
{
    char request[512];
    int length = snprintf(request, sizeof request, "%s %s HTTP/1.1\r\nHost: %s\r\n\r\n", method, target, host);
    assert(length > 0 && length < (int)sizeof request);
    int client = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in address = { .sin_family = AF_INET,
                                   .sin_port = htons((uint16_t)port),
                                   .sin_addr.s_addr = htonl(INADDR_LOOPBACK) };
    struct timeval timeout = { .tv_sec = 5 };
    if (client < 0 ||
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout) ||
        connect(client, (struct sockaddr *)&address, sizeof address) ||
        send(client, request, (size_t)length, 0) != length) {
        if (client >= 0) {
            close(client);
        }
        return NULL;
    }
    size_t used = 0, capacity = 1024;
    char *response = xmalloc(capacity);
    for (ssize_t count;
         (count = recv(client, response + used, capacity - used - 1, 0)) > 0;) {
        used += (size_t)count;
        if (used + 1 == capacity) {
            capacity *= 2;
            response = xrealloc(response, capacity);
        }
    }
    response[used] = '\0';
    close(client);
    return response;
}

/* Whether a GET of target from this computer answers with `first` and, when
 * given, `second`. */
static bool answers(int port, const char *target, const char *first, const char *second)
{
    char *response = fetch(port, "GET", target, "127.0.0.1");
    bool found = response && strstr(response, first) &&
                 (!second || strstr(response, second));
    free(response);
    return found;
}

/* Entries in the folder at `relative`, other than . and ... */
static int entries(const char *relative)
{
    char folder[256];
    path(folder, relative);
    DIR *directory = opendir(folder);
    assert(directory);
    int count = 0;
    struct dirent *entry;
    while ((entry = readdir(directory))) {
        count += strcmp(entry->d_name, ".") && strcmp(entry->d_name, "..");
    }
    assert(!closedir(directory));
    return count;
}

/* A fake browser player whose compiler runs under a fake node, and the
 * engine files an export copies. */
static void web_fixture()
{
    const char *folders[] = { "player",
                              "player/compiler",
                              "node",
                              "tmp",
                              "share",
                              "share/chibi",
                              "share/chibi/shaders",
                              "share/chibi/licenses" };
    for (size_t i = 0; i < sizeof folders / sizeof *folders; i++) {
        directory(folders[i]);
    }
    write_file("player/index.html", "<title>{{TITLE}}</title>");
    write_file("player/chibi-player.js", "script");
    write_file("player/chibi-player.wasm", "module");
    write_file(
        "player/compiler/chibi-compile.js",
        "[ ! -f \"$2/fail\" ] || exit 3\nprintf artifact > \"$4\"\n"
        "printf '{\"external\":[]}' > \"$4.resources.json\"\n"
    );
    write_file("node/node", "#!/bin/sh\nscript=$1\nshift\nexec /bin/sh \"$script\" \"$@\"\n");
    char file[256];
    path(file, "node/node");
    assert(!chmod(file, 0700));
    write_file("share/chibi/engine.json", "{}\n");
    write_file("share/chibi/shaders/a.glsl", "shader");
    write_file("share/chibi/licenses/LICENSE.txt", "license");
}

static bool loopback_available(int port)
{
    int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    assert(socket_fd >= 0);
    int enabled = 1;
    assert(!setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof enabled));
    struct sockaddr_in address = { .sin_family = AF_INET,
                                   .sin_port = htons((uint16_t)port),
                                   .sin_addr.s_addr = htonl(INADDR_LOOPBACK) };
    bool available = !bind(socket_fd, (struct sockaddr *)&address, sizeof address);
    close(socket_fd);
    return available;
}

static int unused_loopback_port()
{
    int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    assert(socket_fd >= 0);
    struct sockaddr_in address = { .sin_family = AF_INET,
                                   .sin_addr.s_addr = htonl(INADDR_LOOPBACK) };
    assert(!bind(socket_fd, (struct sockaddr *)&address, sizeof address));
    socklen_t size = sizeof address;
    assert(!getsockname(socket_fd, (struct sockaddr *)&address, &size));
    close(socket_fd);
    return ntohs(address.sin_port);
}

TEST_CASE(export_interruption_reaps_children_and_removes_owned_resources)
{
    const char *directories[] = { "cancel-sdk",
                                  "cancel-sdk/bin",
                                  "cancel-sdk/share",
                                  "cancel-sdk/share/chibi",
                                  "cancel-sdk/share/chibi/shaders",
                                  "cancel-sdk/share/chibi/licenses",
                                  "cancel-player",
                                  "cancel-player/compiler",
                                  "cancel-output",
                                  "cancel-temporary" };
    for (size_t i = 0; i < sizeof directories / sizeof *directories; i++) {
        directory(directories[i]);
    }
    REQUIRE(clone_program_file(built, "cancel-sdk/bin/chibi"));
    char share[256], cli[256], bin[256], temporary[256], player[256], game[256],
        output[256];
    path(share, "cancel-sdk/share/chibi");
    REQUIRE(copy_resource(built, "apps", share));
    REQUIRE(copy_resource(built, "core", share));
    path(cli, "cancel-sdk/bin/chibi");
    path(bin, "cancel-sdk/bin");
    path(temporary, "cancel-temporary");
    path(player, "cancel-player");
    path(output, "cancel-output/Game.app");
    project("cancel-export-game", CHIBI_PROJECT_VERSION);
    path(game, "cancel-export-game");
    const char *sleeping = "#!/bin/sh\ntrap '' INT TERM\n"
                           "/bin/sh -c 'trap \"\" INT TERM; while :; do "
                           "/bin/sleep 1; done' &\n"
                           "descendant=$!\nprintf 'EXPORT_READY %s %s "
                           "READY_END\\n' \"$$\" \"$descendant\"\nwait\n";
    write_file("cancel-sdk/bin/chibi-compile", sleeping);
    write_file("cancel-player/compiler/chibi-compile.js", sleeping);
    write_file("cancel-sdk/bin/chibi-player", "#!/bin/sh\nexit 0\n");
    write_file("cancel-sdk/bin/node", "#!/bin/sh\nscript=$1\nshift\nexec /bin/sh \"$script\" \"$@\"\n");
    write_file("cancel-sdk/share/chibi/engine.json", "{}\n");
    write_file("cancel-sdk/share/chibi/shaders/fixture.glsl", "shader\n");
    write_file("cancel-sdk/share/chibi/licenses/fixture.txt", "license\n");
    write_file("cancel-player/index.html", "<title>cancel</title>\n");
    write_file("cancel-player/chibi-player.js", "script\n");
    write_file("cancel-player/chibi-player.wasm", "module\n");
    const char *executables[] = { "cancel-sdk/bin/chibi-compile",
                                  "cancel-sdk/bin/chibi-player",
                                  "cancel-sdk/bin/node" };
    for (size_t i = 0; i < sizeof executables / sizeof *executables; i++) {
        char executable[256];
        path(executable, executables[i]);
        REQUIRE(!chmod(executable, 0700));
    }
    for (int i = 0; i < 4; i++) {
        bool web_export = i >= 2;
        int port = web_export ? unused_loopback_port() : 0;
        char port_text[16];
        snprintf(port_text, sizeof port_text, "%d", port);
        const char *export_args[] = { cli,        "export", game,
                                      "--output", output,   NULL };
        const char *web_args[] = { cli,    "web",    game,      "--player",
                                   player, "--port", port_text, NULL };
        const char *old_path = getenv("PATH"), *old_tmp = getenv("TMPDIR");
        char *saved_path = old_path ? xstrdup(old_path) : NULL;
        char *saved_tmp = old_tmp ? xstrdup(old_tmp) : NULL;
        assert(!setenv("PATH", bin, 1) && !setenv("TMPDIR", temporary, 1));
        chibi_process process;
        bool started = chibi_process_start(&process, web_export ? web_args : export_args, root, log_path);
        if (saved_path) {
            assert(!setenv("PATH", saved_path, 1));
        } else {
            assert(!unsetenv("PATH"));
        }
        if (saved_tmp) {
            assert(!setenv("TMPDIR", saved_tmp, 1));
        } else {
            assert(!unsetenv("TMPDIR"));
        }
        free(saved_path);
        free(saved_tmp);
        REQUIRE(started);
        tool_own_process(process.pid);
        bool ready = watch_output(&process, 0, "READY_END\n");
        int child = 0, descendant = 0;
        char *text = tool_read(log_path);
        const char *marker = text ? strstr(text, "EXPORT_READY ") : NULL;
        bool known = ready && marker &&
                     sscanf(marker, "EXPORT_READY %d %d", &child, &descendant) == 2 &&
                     child > 0 && descendant > 0;
        free(text);
        int group = known ? getpgid(child) : 0;
        known = known && (group == child || group == process.pid) &&
                getpgid(descendant) == group;
        bool prepared = known && (web_export ? entries("cancel-temporary") == 1 &&
                                                   !loopback_available(port)
                                             : entries("cancel-output") == 1);
        int interrupt = i % 2 ? SIGTERM : SIGINT;
        bool signalled = prepared && !kill(process.pid, interrupt);
        double deadline = chibi_process_now() + 5;
        while (signalled && !chibi_process_poll(&process) &&
               chibi_process_now() < deadline) {
            usleep(10000);
        }
        bool exited = process.finished && process.status == 128 + interrupt;
        deadline = chibi_process_now() + 2;
        bool child_retired = false, descendant_retired = false;
        bool group_retired = false, root_retired = false, gone = false;
        do {
            child_retired |= known && pid_gone(child);
            descendant_retired |= known && pid_gone(descendant);
            group_retired |= known && group_gone(group);
            root_retired |= group_gone(process.pid);
            gone = known && child_retired && descendant_retired &&
                   (group != child || group_retired);
            if (gone) {
                break;
            }
            usleep(10000);
        } while (chibi_process_now() < deadline);
        bool resources = (web_export ? entries("cancel-temporary") == 0 && loopback_available(port) : entries("cancel-output") == 0);
        if (known && !gone && !group_retired) {
            kill(-group, SIGKILL);
        }
        if (!process.finished && !root_retired) {
            kill(-process.pid, SIGKILL);
        }
        deadline = chibi_process_now() + 3;
        while (!chibi_process_poll(&process) && chibi_process_now() < deadline) {
            usleep(10000);
        }
        deadline = chibi_process_now() + 2;
        do {
            root_retired |= group_gone(process.pid);
            child_retired |= known && pid_gone(child);
            descendant_retired |= known && pid_gone(descendant);
            group_retired |= known && group_gone(group);
            if (root_retired && (!known || (child_retired && descendant_retired &&
                                            (group != child || group_retired)))) {
                break;
            }
            usleep(10000);
        } while (chibi_process_now() < deadline);
        bool cleanup = process.finished && root_retired &&
                       (!known || (child_retired && descendant_retired &&
                                   (group != child || group_retired)));
        tool_release_process(process.pid);
        bool passed = prepared && signalled && exited && gone && resources && cleanup;
        if (!passed) {
            fprintf(
                stderr,
                "export web=%d signal=%d ready=%d child=%d descendant=%d "
                "group=%d prepared=%d status=%d exited=%d gone=%d resources=%d "
                "cleanup=%d\n",
                web_export,
                interrupt,
                ready,
                child,
                descendant,
                group,
                prepared,
                process.status,
                exited,
                gone,
                resources,
                cleanup
            );
        }
        watch_failure_log(passed);
        REQUIRE(passed);
    }
    return true;
}

/* chibi web for the CLI project on port, with the fake node first on PATH and
 * TMPDIR in the fixture, either started into *process or run to its status. */
static int web(const char *port, chibi_process *process)
{
    char *saved_path = xstrdup(getenv("PATH"));
    char *saved_tmp = getenv("TMPDIR") ? xstrdup(getenv("TMPDIR")) : NULL;
    char node[4096], tmp[256], player[256], log[256];
    int length = snprintf(node, sizeof node, "%s/node:%s", root, saved_path);
    assert(length > 0 && length < (int)sizeof node);
    path(tmp, "tmp");
    path(player, "player");
    path(log, "web.log");
    setenv("PATH", node, 1);
    setenv("TMPDIR", tmp, 1);
    const char *args[] = { launcher,   "web",  "game with spaces",
                           "--player", player, "--port",
                           port,       NULL };
    int status = process ? !chibi_process_start(process, args, root, log)
                         : tool_run(args, root, log);
    setenv("PATH", saved_path, 1);
    if (saved_tmp) {
        setenv("TMPDIR", saved_tmp, 1);
    } else {
        unsetenv("TMPDIR");
    }
    free(saved_path);
    free(saved_tmp);
    return status;
}

/* web exports the browser site into a folder of its own under TMPDIR, serves
 * it on loopback to requests that name this computer, refusing paths that
 * leave it, until stopped, and then removes the folder. */
TEST_CASE(web_serves_the_export_until_stopped)
{
    web_fixture();
    chibi_process process;
    REQUIRE(web("0", &process) == 0);
    int port = 0;
    char log[256];
    path(log, "web.log");
    for (int i = 0; i < 100 && !port && !chibi_process_poll(&process); i++) {
        usleep(50000);
        char *text = tool_read(log);
        const char *url = text ? strstr(text, "http://127.0.0.1:") : NULL;
        port = url ? atoi(url + strlen("http://127.0.0.1:")) : 0;
        free(text);
    }
    char *rebound = port ? fetch(port, "GET", "/", "evil.example:8000") : NULL;
    char *head = port ? fetch(port, "HEAD", "/index.html", "localhost:8000") : NULL;
    bool served = port && entries("tmp") == 1 &&
                  answers(port, "/", "200 OK", "<title>CLI proof</title>") &&
                  answers(port, "/data/game.chibip", "\r\n\r\nartifact", NULL) &&
                  answers(port, "/chibi-player.wasm", "Content-Type: application/wasm", NULL) &&
                  answers(port, "/data/../../tmp", "400 Bad Request", NULL) &&
                  answers(port, "/%2e%2e/web.log", "400 Bad Request", NULL) &&
                  answers(port, "/missing.js", "404 Not Found", NULL) &&
                  rebound && strstr(rebound, "400 Bad Request") && head &&
                  strstr(head, "Content-Length: 24\r\n") && !strstr(head, "<title>");
    free(rebound);
    free(head);
    chibi_process_stop(&process);
    for (int i = 0; i < 100 && !chibi_process_poll(&process); i++) {
        usleep(50000);
    }
    REQUIRE(served);
    REQUIRE(process.finished && process.status == 0);
    REQUIRE(entries("tmp") == 0);
    return true;
}

/* web fails, leaving nothing behind, when its port is taken or the export
 * fails; it needs --player and a valid port. */
TEST_CASE(web_fails_without_leaving_a_folder)
{
    web_fixture();
    int bound = 0, taken = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in address = { .sin_family = AF_INET,
                                   .sin_addr.s_addr = htonl(INADDR_LOOPBACK) };
    socklen_t size = sizeof address;
    REQUIRE(taken >= 0 && !bind(taken, (struct sockaddr *)&address, sizeof address));
    REQUIRE(!listen(taken, 1) && !getsockname(taken, (struct sockaddr *)&address, &size));
    bound = ntohs(address.sin_port);
    char port[16];
    snprintf(port, sizeof port, "%d", bound);
    int status = web(port, NULL);
    close(taken);
    REQUIRE(status == 1 && entries("tmp") == 0);

    write_file("game with spaces/fail", "");
    status = web("0", NULL);
    char file[256];
    path(file, "game with spaces/fail");
    REQUIRE(!unlink(file));
    REQUIRE(status == 1 && entries("tmp") == 0);

    char player[256];
    path(player, "player");
    const char *missing[] = { launcher, "web", "game with spaces", NULL };
    REQUIRE(tool_run(missing, root, log_path) == 2);
    const char *range[] = { launcher, "web",   "--player", player,
                            "--port", "70000", NULL };
    REQUIRE(tool_run(range, root, log_path) == 2);
    const char *twice[] = { launcher, "web",    "--player", player, "--port",
                            "1",      "--port", "2",        NULL };
    REQUIRE(tool_run(twice, root, log_path) == 2);
    const char *help[] = { launcher, "web", "--help", NULL };
    REQUIRE(tool_run(help, root, log_path) == 0);
    return true;
}

/* A site manifest with a home page and a guide whose output is `guide`. */
static void site_manifest(const char *relative, const char *guide_source, const char *guide_output)
{
    char manifest[1024];
    snprintf(
        manifest,
        sizeof manifest,
        "title \"Fixture\"\n(array)sections {\n    - {\n        name "
        "\"Overview\"\n"
        "        (array)pages {\n            - {\n                title "
        "\"Home\"\n"
        "                source \"README.md\"\n                output "
        "\"index.html\"\n"
        "            }\n        }\n    }\n    - {\n        name \"Learn\"\n"
        "        (array)pages {\n            - {\n                title "
        "\"Guide\"\n"
        "                source \"%s\"\n                output \"%s\"\n        "
        "    }\n"
        "        }\n    }\n}\n",
        guide_source,
        guide_output
    );
    write_file(relative, manifest);
}

/* A documentation root with two published pages and one private one. */
static void site_fixture(const char *relative)
{
    char folder[256];
    snprintf(folder, sizeof folder, "%s/docs", relative);
    directory(relative);
    directory(folder);
    char file[256];
    snprintf(file, sizeof file, "%s/README.md", relative);
    write_file(file, "# Home\n\nRead [the guide](docs/guide.md) and [plans](PLAN.md).\n");
    snprintf(file, sizeof file, "%s/docs/guide.md", relative);
    write_file(file, "# Guide\n\nBack [home](../README.md).\n\n```chibi\npackage sample\nValue :: 1\n```\n");
    snprintf(file, sizeof file, "%s/docs/private.md", relative);
    write_file(file, "# Private\n");
    snprintf(file, sizeof file, "%s/docs/site.kdl", relative);
    site_manifest(file, "docs/guide.md", "guide.html");
}

/* The real compiler's --docs writes exactly the pages docs/site.kdl lists,
 * linked to one another, with the stylesheet, into a folder as readable as
 * mkdir makes one, names each link it writes as text, and refuses a manifest
 * whose pages leave the root or the site folder, collide or are missing. */
TEST_CASE(compiler_docs_writes_the_listed_pages)
{
    site_fixture("site");
    char site[256], output[256];
    path(site, "site");
    path(output, "docs.log");
    const char *docs[] = { compiler, "--project-root", site, "--docs", NULL };
    REQUIRE(tool_run(docs, root, output) == 0);
    REQUIRE(
        file_has("site/build/docs/index.html", "Read <a href=\"guide.html\">the guide</a> and plans.</p>")
    );
    REQUIRE(file_has("docs.log", "README.md:3: link to PLAN.md is not published; written as text\n"));
    REQUIRE(file_has("site/build/docs/index.html", "<title>Home · Fixture</title>"));
    REQUIRE(file_has("site/build/docs/guide.html", "Back <a href=\"index.html\">home</a>."));
    REQUIRE(
        file_has("site/build/docs/guide.html", "<a href=\"guide.html\" aria-current=\"page\">Guide</a>")
    );
    REQUIRE(exists("site/build/docs/style.css") && !exists("site/build/docs/private.html"));
    char *style = contents("site/build/docs/style.css");
    const char *dark = strstr(style, "@media (prefers-color-scheme: dark)");
    const char *categories[] = { "keyword", "string", "number", "comment" };
    bool palettes = dark != NULL;
    for (int i = 0; i < 4; i++) {
        char variable[64], selector[128];
        snprintf(variable, sizeof variable, "--syntax-%s:", categories[i]);
        snprintf(
            selector,
            sizeof selector,
            "pre .chibi-%s { color: var(--syntax-%s); }",
            categories[i],
            categories[i]
        );
        const char *light = strstr(style, variable);
        palettes = palettes && light && light < dark &&
                   strstr(dark, variable) && strstr(style, selector);
    }
    free(style);
    REQUIRE(palettes);
    char published[256];
    path(published, "site/build/docs");
    struct stat info;
    mode_t mask = umask(0);
    umask(mask);
    REQUIRE(!stat(published, &info) && (info.st_mode & 0777) == (0777 & ~mask));

    /* A page the manifest no longer lists goes with the next build. */
    write_file("site/build/docs/old.html", "stale");
    REQUIRE(tool_run(docs, root, output) == 0 && !exists("site/build/docs/old.html"));

    char outside[256];
    path(outside, "outside.md");
    write_file("outside.md", "# Outside\n");
    char link[256];
    path(link, "site/docs/linked.md");
    REQUIRE(!symlink(outside, link));
    const char *refused[][3] = {
        { "docs/guide.md", "../guide.html", "needs a title, an .html output and one of source" },
        { "../outside.md", "guide.html", "../outside.md is not a readable file inside" },
        { "docs/linked.md", "guide.html", "docs/linked.md is not a readable file inside" },
        { "docs/missing.md", "guide.html", "docs/missing.md is not a readable file inside" },
        { "docs", "guide.html", "docs is not a readable file inside" },
        { "docs/guide.md", "index.html", "two pages write index.html" },
    };
    for (size_t i = 0; i < sizeof refused / sizeof *refused; i++) {
        site_manifest("site/docs/site.kdl", refused[i][0], refused[i][1]);
        REQUIRE(tool_run(docs, root, output) == 1 && file_has("docs.log", refused[i][2]));
        REQUIRE(file_has("site/build/docs/guide.html", "Back <a href=\"index.html\">home</a>."));
    }

    /* A published chibi example must parse, and a whole package analyze. */
    site_manifest("site/docs/site.kdl", "docs/guide.md", "guide.html");
    write_file(
        "site/docs/guide.md",
        "# Guide\n"
        "\n"
        "```chibi\n"
        "package sample\n"
        "/* first <&>\n"
        "\tsecond café */\n"
        "Title :: \"café <&> 🧭\"\n"
        "Ratio :: 1.5\n"
        " \t\n"
        "Double :: (n: int) -> int {\n"
        "\treturn n * 2  // kept <&>\n"
        "}\n"
        "```\n"
        "\n"
        "```code\n"
        "package plain\n"
        "return 2 < 3\n"
        "```\n"
    );
    REQUIRE(tool_run(docs, root, output) == 0);
    /* Exact slices preserve UTF-8, blank lines, tabs, comments and implicit
     * semicolons while escaping every token and gap as HTML text. */
    REQUIRE(file_has(
        "site/build/docs/guide.html",
        "<pre><code class=\"language-chibi\">"
        "<span class=\"chibi-keyword\">package</span>"
        " "
        "<span class=\"chibi-namespace\">sample</span>"
        "\n"
        "<span class=\"chibi-comment\">/* first &lt;&amp;&gt;\n\tsecond café "
        "*/</span>"
        "\n"
        "<span class=\"chibi-constant\">Title</span>"
        " "
        "<span class=\"chibi-operator\">::</span>"
        " "
        "<span class=\"chibi-string\">&quot;café &lt;&amp;&gt; 🧭&quot;</span>"
        "\n"
        "<span class=\"chibi-constant\">Ratio</span>"
        " "
        "<span class=\"chibi-operator\">::</span>"
        " "
        "<span class=\"chibi-number\">1.5</span>"
        "\n"
        " \t\n"
        "<span class=\"chibi-function\">Double</span>"
        " "
        "<span class=\"chibi-operator\">::</span>"
        " (n: "
        "<span class=\"chibi-builtin\">int</span>"
        ") "
        "<span class=\"chibi-operator\">-&gt;</span>"
        " "
        "<span class=\"chibi-builtin\">int</span>"
        " {\n"
        "\t"
        "<span class=\"chibi-keyword\">return</span>"
        " n "
        "<span class=\"chibi-operator\">*</span>"
        " "
        "<span class=\"chibi-number\">2</span>"
        "  "
        "<span class=\"chibi-comment\">// kept &lt;&amp;&gt;</span>"
        "\n"
        "}\n</code></pre>\n"
    ));
    REQUIRE(
        file_has("site/build/docs/guide.html", "<pre><code class=\"language-code\">package plain\nreturn 2 &lt; 3\n</code></pre>\n")
    );
    /* Package fences share this page's virtual workspace and real core imports. */
    write_file(
        "site/docs/guide.md",
        "# Guide\n\n```chibi\n// score/Bonus.chibi\npackage score\n"
        "Twice :: (n: int) -> int { return n * 2 }\n"
        "pub Reward :: () -> int { return Twice(5) }\n```\n"
        "```chibi\npackage score\npub More :: () -> int { return Twice(6) "
        "}\n```\n"
        "```chibi\npackage rewards\nimport score \"project:score\"\n"
        "import strings \"core:strings\"\n"
        "pub Ready :: () -> bool { return strings.HasPrefix(\"reward\", "
        "\"re\") && score.Reward() > 0 }\n```\n"
        "```chibi\npackage relative\nimport score \"../score\"\n"
        "pub Points :: () -> int { return score.More() }\n```\n"
    );
    REQUIRE(tool_run(docs, root, output) == 0);
    REQUIRE(file_has("site/build/docs/guide.html", "core:strings"));
    write_file(
        "site/docs/guide.md",
        "# Guide\n\n```chibi\npackage score\nSecret :: () -> int { return 10 "
        "}\n```\n"
        "```chibi\npackage rewards\nimport score \"project:score\"\n"
        "pub Points :: () -> int { return score.Secret() }\n```\n"
    );
    REQUIRE(tool_run(docs, root, output) == 1);
    REQUIRE(file_has("docs.log", "docs/guide.md:10:"));
    REQUIRE(file_has("docs.log", "'score.Secret' is private"));
    write_file("site/docs/guide.md", "# Guide\n\n```chibi\npackage rewards\nimport missing \"project:missing\"\n```\n");
    REQUIRE(tool_run(docs, root, output) == 1);
    REQUIRE(file_has("docs.log", "docs/guide.md:5:"));
    REQUIRE(file_has("docs.log", "Imported package is missing"));
    write_file("site/docs/guide.md", "# Guide\n\n```chibi\npackage sample\nTwice :: () {\n    Missing();\n}\n```\n");
    REQUIRE(tool_run(docs, root, output) == 1);
    REQUIRE(file_has("docs.log", "docs/guide.md:6:5: example: Unknown callable 'Missing'"));
    /* The private parser is available only to the generator, never examples. */
    write_file("site/docs/guide.md", "# Guide\n\n```chibi\npackage sample\nProbe :: () {\n    compiler_syntax_parse(\"package hidden\");\n}\n```\n");
    REQUIRE(tool_run(docs, root, output) == 1);
    REQUIRE(
        file_has("docs.log", "docs/guide.md:6:5: example: Unknown callable 'compiler_syntax_parse'")
    );
    write_file("site/docs/guide.md", "# Guide\n\n```chibi\nsystem {\n```\n");
    REQUIRE(tool_run(docs, root, output) == 1 && file_has("docs.log", "docs/guide.md:4:"));
    char bare[256];
    path(bare, "bare");
    directory("bare");
    const char *none[] = { compiler, "--project-root", bare, "--docs", NULL };
    REQUIRE(tool_run(none, root, output) == 1);
    REQUIRE(file_has("docs.log", "cannot read the site manifest"));
    return true;
}

/* A site with a Markdown page, a package reference and a grammar page; its
 * reference page reads `packages` with `collection`, when given. */
static void generated_site(const char *packages, const char *collection)
{
    char manifest[1024], reference[256] = "";
    if (collection) {
        snprintf(reference, sizeof reference, "                collection \"%s\"\n", collection);
    }
    snprintf(
        manifest,
        sizeof manifest,
        "title \"Generated\"\n(array)sections {\n    - {\n        name "
        "\"Overview\"\n"
        "        (array)pages {\n            - {\n                title "
        "\"Home\"\n"
        "                source \"README.md\"\n                output "
        "\"index.html\"\n            }\n"
        "            - {\n                title \"Packages\"\n                "
        "packages \"%s\"\n%s"
        "                output \"packages.html\"\n            }\n"
        "            - {\n                title \"Grammar\"\n                "
        "grammar \"grammar.js\"\n"
        "                output \"grammar.html\"\n            }\n        }\n   "
        " }\n}\n",
        packages,
        reference
    );
    write_file("generated/docs/site.kdl", manifest);
}

/* The real compiler's --docs generates a package reference from the
 * packages of a folder and a grammar page from a grammar's rules, and names
 * a link written as text at the line of its source file. */
TEST_CASE(compiler_docs_generates_reference_pages)
{
    const char *folders[] = { "generated", "generated/docs", "generated/lib", "generated/lib/shapes" };
    for (size_t i = 0; i < sizeof folders / sizeof *folders; i++) {
        directory(folders[i]);
    }
    write_file("generated/README.md", "# Home\n\nSee [the grammar](grammar.js).\n");
    write_file("generated/lib/shapes/Shapes.chibi", "package shapes\n\n// Four [sides](sides.md).\npub Sides: int : 4;\n");
    write_file("generated/grammar.js", "  rules: {\n    // One [item](item.md).\n    item: ($) => \"x\",\n  },\n");
    generated_site("lib", "lib");
    char site[256], output[256];
    path(site, "generated");
    path(output, "generated.log");
    const char *docs[] = { compiler, "--project-root", site, "--docs", NULL };
    REQUIRE(tool_run(docs, root, output) == 0);
    REQUIRE(file_has("generated/build/docs/packages.html", "Import it as <code>lib:shapes</code>."));
    REQUIRE(file_has(
        "generated/build/docs/packages.html",
        "<span class=\"chibi-keyword\">pub</span>"
        " "
        "<span class=\"chibi-constant\">Sides</span>"
        ": "
        "<span class=\"chibi-builtin\">int</span>"
        " : "
        "<span class=\"chibi-number\">4</span>"
        ";\n</code></pre>\n<p>Four sides.</p>"
    ));
    REQUIRE(
        file_has("generated/build/docs/grammar.html", "<h3 id=\"item\">item</h3>\n<p>One item.</p>")
    );
    REQUIRE(
        file_has("generated/build/docs/index.html", "See <a href=\"grammar.html\">the grammar</a>.")
    );
    REQUIRE(
        file_has("generated.log", "lib/shapes/Shapes.chibi:3: link to sides.md is not published; written as text\n")
    );
    REQUIRE(
        file_has("generated.log", "grammar.js:2: link to item.md is not published; written as text\n")
    );

    generated_site("lib", NULL);
    REQUIRE(tool_run(docs, root, output) == 1);
    REQUIRE(file_has("generated.log", "one of source, grammar or packages with its collection"));
    generated_site("absent", "lib");
    REQUIRE(
        tool_run(docs, root, output) == 1 &&
        file_has("generated.log", "absent is not a readable folder")
    );
    /* The escaped target exists and contains a real package; rejection must
     * be containment, not a dangling link or a missing directory. */
    directory("tests");
    directory("tests/facts");
    write_file("tests/facts/Facts.chibi", "package facts\nValue :: 1\n");
    generated_site("../tests", "lib");
    REQUIRE(
        tool_run(docs, root, output) == 1 &&
        file_has("generated.log", "../tests is not a readable folder")
    );
    char outside[256], link[256];
    path(outside, "tests");
    path(link, "generated/linked");
    REQUIRE(!symlink(outside, link));
    generated_site("linked", "lib");
    REQUIRE(
        tool_run(docs, root, output) == 1 &&
        file_has("generated.log", "linked is not a readable folder")
    );
    directory("generated/empty");
    generated_site("empty", "lib");
    REQUIRE(
        tool_run(docs, root, output) == 1 &&
        file_has("generated.log", "empty holds no package files")
    );
    write_file("generated/grammar.js", "rules: {\n}\n");
    generated_site("lib", "lib");
    REQUIRE(
        tool_run(docs, root, output) == 1 &&
        file_has("generated.log", "grammar.js has no `  rules: {` block")
    );
    return true;
}

/* doc hands ROOT, or the working directory, to the compiler's --docs, and
 * with --serve then serves ROOT/build/docs until stopped. */
TEST_CASE(doc_runs_the_compiler_docs)
{
    char file[256];
    path(file, "fail");
    unlink(file);
    const char *named[] = { launcher, "doc", "game with spaces", NULL };
    REQUIRE(tool_run(named, root, log_path) == 0);
    REQUIRE(file_is("compile.args", "--project-root\ngame with spaces\n--docs\n"));
    const char *here[] = { launcher, "doc", NULL };
    REQUIRE(tool_run(here, root, log_path) == 0);
    REQUIRE(file_is("compile.args", "--project-root\n.\n--docs\n"));
    write_file("fail", "");
    REQUIRE(tool_run(named, root, log_path) == 37);
    REQUIRE(!unlink(file));
    const char *help[] = { launcher, "doc", "--help", NULL };
    REQUIRE(tool_run(help, root, log_path) == 0);
    const char *unknown[] = { launcher, "doc", "--quick", NULL };
    REQUIRE(tool_run(unknown, root, log_path) == 2);
    const char *twice[] = { launcher, "doc",    "--serve", "--port",
                            "1",      "--port", "2",       NULL };
    REQUIRE(tool_run(twice, root, log_path) == 2);
    const char *unserved[] = { launcher, "doc", "--port", "1", NULL };
    REQUIRE(tool_run(unserved, root, log_path) == 2);

    directory("game with spaces/build");
    directory("game with spaces/build/docs");
    write_file("game with spaces/build/docs/index.html", "<p>docs</p>");
    char log[256];
    path(log, "doc.log");
    const char *serve[] = { launcher,  "doc",    "game with spaces",
                            "--serve", "--port", "0",
                            NULL };
    /* A failed build serves nothing. */
    write_file("fail", "");
    REQUIRE(tool_run(serve, root, log) == 37 && !file_has("doc.log", "http://"));
    REQUIRE(!unlink(file));
    chibi_process process;
    REQUIRE(chibi_process_start(&process, serve, root, log));
    int port = 0;
    for (int i = 0; i < 100 && !port && !chibi_process_poll(&process); i++) {
        usleep(50000);
        char *text = tool_read(log);
        const char *url = text ? strstr(text, "http://127.0.0.1:") : NULL;
        port = url ? atoi(url + strlen("http://127.0.0.1:")) : 0;
        free(text);
    }
    bool served = port && answers(port, "/", "200 OK", "<p>docs</p>");
    chibi_process_stop(&process);
    for (int i = 0; i < 100 && !chibi_process_poll(&process); i++) {
        usleep(50000);
    }
    REQUIRE(served);
    REQUIRE(process.finished && process.status == 0);
    return true;
}

/* Without its compiler, each command that needs one says so and fails
 * rather than pretending the project is fine. */
TEST_CASE(commands_fail_without_the_compiler)
{
    char program[256], aside[256], output[256];
    path(program, "bin/chibi-compile");
    path(aside, "bin/chibi-compile.aside");
    path(output, "missing.log");
    REQUIRE(!rename(program, aside));
    const char *commands[][4] = {
        { launcher, "check", "game with spaces", NULL },
        { launcher, "build", "game with spaces", NULL },
        { launcher, "fmt", "game with spaces", NULL },
        { launcher, "doc", "game with spaces", NULL },
    };
    bool failed = true;
    for (size_t i = 0; i < sizeof commands / sizeof *commands; i++) {
        failed = failed && tool_run(commands[i], root, output) != 0 &&
                 file_has("missing.log", "chibi-compile");
    }
    const char *tests[] = { launcher, "test", "package-tests", NULL };
    bool shared = tool_run(tests, root, output) == 0 &&
                  file_has("missing.log", "PASS facts.Passes");
    REQUIRE(!rename(aside, program));
    REQUIRE(failed && shared);
    return true;
}

/* Compiler-only documentation coverage also runs in a headless SDK build. */
void test_tool_chibi_docs(const char *binary)
{
    strcpy(root, "/tmp/chibi-docs-XXXXXX");
    assert(mkdtemp(root));
    char *resolved = realpath(root, NULL);
    assert(resolved && strlen(resolved) < sizeof root);
    strcpy(root, resolved);
    free(resolved);
    char *program = realpath(binary, NULL);
    assert(program && strlen(program) < sizeof compiler);
    strcpy(compiler, program);
    free(program);
    path(log_path, "output.log");
    RUN_TEST_CASE(compiler_docs_writes_the_listed_pages);
    RUN_TEST_CASE(compiler_docs_generates_reference_pages);
    tool_remove_tree(root);
}

void test_tool_chibi_cli(const char *binary, const char *self)
{
    locate_driver(self);
    setup(binary);
    /* The real compiler is the launcher's built sibling. */
    char *directory = realpath(binary, NULL);
    assert(directory);
    *strrchr(directory, '/') = 0;
    int length = snprintf(compiler, sizeof compiler, "%s/chibi-compile", directory);
    assert(length > 0 && length < (int)sizeof compiler);
    length = snprintf(built, sizeof built, "%s/chibi", directory);
    assert(length > 0 && length < (int)sizeof built);
    free(directory);
    RUN_TEST_CASE(build_and_run_use_the_prepared_project);
    RUN_TEST_CASE(check_prepares_without_writing);
    RUN_TEST_CASE(check_forwards_cost_explanations);
    RUN_TEST_CASE(compiler_check_writes_nothing);
    RUN_TEST_CASE(compiler_explains_costs_without_writing);
    RUN_TEST_CASE(version_reads_the_build_identity);
    RUN_TEST_CASE(version_of_the_built_launcher);
    RUN_TEST_CASE(doctor_reports_each_capability);
    RUN_TEST_CASE(doctor_of_the_built_launcher);
    RUN_TEST_CASE(test_runs_shared_package_tests);
    RUN_TEST_CASE(compiler_test_reports_each_test);
    RUN_TEST_CASE(compiler_test_links_the_compiler_bridges);
    RUN_TEST_CASE(compiler_runs_cost_explanation_regressions);
    RUN_TEST_CASE(compiler_test_needs_the_core_collection);
    RUN_TEST_CASE(native_build_forwards_driver_arguments);
    RUN_TEST_CASE(run_dispatches_standalone_programs);
    RUN_TEST_CASE(program_main_runs_from_files_and_directories);
    RUN_TEST_CASE(authored_applications_use_independent_program_sessions);
    RUN_TEST_CASE(program_sources_link_packages_and_select_the_root_entry);
    RUN_TEST_CASE(public_cli_is_a_relocatable_ordinary_application);
    RUN_TEST_CASE(program_failures_report_entry_and_source_errors);
    RUN_TEST_CASE(program_watch_patches_a_running_frame_and_keeps_state);
    RUN_TEST_CASE(program_watch_tracks_siblings_and_keeps_imported_state);
    RUN_TEST_CASE(program_cancellation_stops_busy_main_and_tests);
    RUN_TEST_CASE(program_process_run_stops_children_when_only_parent_is_signalled);
    RUN_TEST_CASE(program_process_run_hands_the_terminal_to_its_child);
    RUN_TEST_CASE(export_interruption_reaps_children_and_removes_owned_resources);
    RUN_TEST_CASE(fmt_formats_each_project);
    RUN_TEST_CASE(compiler_format_rewrites_noncanonical_assets);
    RUN_TEST_CASE(compiler_format_rewrites_package_sources);
    RUN_TEST_CASE(repository_packages_are_canonical);
    RUN_TEST_CASE(web_serves_the_export_until_stopped);
    RUN_TEST_CASE(web_fails_without_leaving_a_folder);
    RUN_TEST_CASE(doc_runs_the_compiler_docs);
    RUN_TEST_CASE(commands_fail_without_the_compiler);
    tool_remove_tree(root);
}

/* An installed SDK, run in place: identity, diagnosis, and each project and
 * package command on fresh fixtures, with its own compiler and core
 * collection. */
TEST_CASE(installed_identity_and_doctor)
{
    char output[256];
    path(output, "installed.log");
    const char *version[] = { launcher, "version", NULL };
    REQUIRE(tool_run(version, root, output) == 0);
    REQUIRE(file_has("installed.log", "chibi ") && !file_has("installed.log", "build: unpackaged"));
    const char *doctor[] = { launcher, "doctor", NULL };
    REQUIRE(tool_run(doctor, root, output) == 0);
    REQUIRE(file_has("installed.log", "\ncompiler: supported ("));
    REQUIRE(file_has("installed.log", "\nplayer: supported ("));
    return true;
}

/* Copy the guide's complete project, rather than repairing its version or
 * filling in missing assets in the test. */
static bool documented_project(const char *relative)
{
    char *guide = tool_read(CHIBI_SOURCE_ROOT "/docs/guide/toolchain.md");
    if (!guide) {
        return false;
    }
    project(relative, CHIBI_PROJECT_VERSION);
    char folder[256];
    snprintf(folder, sizeof folder, "%s/content/prototypes", relative);
    directory(folder);
    snprintf(folder, sizeof folder, "%s/content/behaviors", relative);
    directory(folder);
    static const char *const files[] = { "project.kdl",
                                         "content/scenes/Start.kdl",
                                         "content/prototypes/Player.kdl",
                                         "content/prototypes/View.kdl",
                                         "content/input.kdl",
                                         "content/behaviors/Move.chibi" };
    bool copied = true;
    for (size_t i = 0; copied && i < sizeof files / sizeof *files; i++) {
        char heading[256];
        snprintf(heading, sizeof heading, "\n### %s\n", files[i]);
        const char *section = strstr(guide, heading);
        const char *fence = section ? strstr(section, "\n```") : NULL;
        const char *begin = fence ? strchr(fence + 4, '\n') : NULL;
        const char *end = begin ? strstr(++begin, "\n```") : NULL;
        copied = end != NULL;
        if (!copied) {
            fprintf(stderr, "toolchain guide lacks a complete %s example\n", files[i]);
            break;
        }
        size_t size = (size_t)(end - begin) + 1;
        char *text = xmalloc(size + 1);
        memcpy(text, begin, size);
        text[size] = 0;
        char file[256];
        snprintf(file, sizeof file, "%s/%s", relative, files[i]);
        write_file(file, text);
        free(text);
    }
    free(guide);
    return copied;
}

TEST_CASE(installed_project_commands)
{
    REQUIRE(documented_project("installed"));
    char game[256], output[256], site[256];
    path(game, "installed");
    path(output, "installed.log");
    const char *check[] = { launcher, "check", game, NULL };
    REQUIRE(tool_run(check, root, output) == 0);
    REQUIRE(!exists("installed/.chibi"));
    const char *format[] = { launcher, "fmt", "--check", game, NULL };
    REQUIRE(tool_run(format, root, output) == 0);
    write_file("installed/content/scenes/Start.kdl", MESSY_SCENE);
    REQUIRE(tool_run(format, root, output) == 1);
    const char *rewrite[] = { launcher, "fmt", game, NULL };
    REQUIRE(tool_run(rewrite, root, output) == 0 && tool_run(format, root, output) == 0);
    const char *build[] = { launcher, "build", game, NULL };
    REQUIRE(tool_run(build, root, output) == 0);
    REQUIRE(exists("installed/.chibi/build/game.chibip"));
    path(site, "exported");
    const char *export[] = { launcher, "export", game, "--output", site, NULL };
    REQUIRE(tool_run_within(export, root, output, 30) == 0);
#ifdef __APPLE__
    REQUIRE(exists("exported.app/Contents/Resources/game.chibip"));
#else
    REQUIRE(exists("exported/data/game.chibip"));
#endif
    return true;
}

TEST_CASE(installed_package_commands_and_docs)
{
    const char *directories[] = { "packages", "packages/facts" };
    for (int i = 0; i < 2; i++) {
        directory(directories[i]);
    }
    write_file("packages/facts/Facts.chibi", FACTS);
    char tree[256], output[256];
    path(tree, "packages");
    path(output, "installed.log");
    const char *run[] = { launcher, "test", tree, NULL };
    REQUIRE(tool_run(run, root, output) == 1);
    REQUIRE(file_has("installed.log", "PASS facts.Passes\nFAIL facts.Fails\n"));
    REQUIRE(file_has("installed.log", "Assertion failed: four bytes\n2 tests, 1 failed\n"));

    directory("packages/lib");
    write_file("packages/lib/Lib.chibi", MESSY_PACKAGE);
    const char *format[] = { launcher, "fmt", tree, NULL };
    REQUIRE(tool_run(format, root, output) == 0);
    REQUIRE(file_has("packages/lib/Lib.chibi", "    return x * 2;\n"));
    const char *check[] = { launcher, "fmt", "--check", tree, NULL };
    REQUIRE(tool_run(check, root, output) == 0);

    site_fixture("site");
    char site[256];
    path(site, "site");
    const char *docs[] = { launcher, "doc", site, NULL };
    REQUIRE(tool_run(docs, root, output) == 0);
    REQUIRE(file_has("site/build/docs/index.html", "<a href=\"guide.html\">the guide</a>"));
    REQUIRE(file_has(
        "site/build/docs/guide.html",
        "<span class=\"chibi-constant\">Value</span>"
        " "
        "<span class=\"chibi-operator\">::</span>"
        " "
        "<span class=\"chibi-number\">1</span>"
        "\n"
    ));
    return true;
}

static void setup_installed(const char *binary)
{
    strcpy(root, "/tmp/chibi-installed-XXXXXX");
    assert(mkdtemp(root));
    char *resolved = realpath(root, NULL);
    assert(resolved && strlen(resolved) < sizeof root);
    strcpy(root, resolved);
    free(resolved);
    char *program = realpath(binary, NULL);
    assert(program && strlen(program) < sizeof launcher);
    strcpy(launcher, program);
    strcpy(built, program);
    *strrchr(program, '/') = 0;
    int length = snprintf(compiler, sizeof compiler, "%s/chibi-compile", program);
    assert(length > 0 && length < (int)sizeof compiler);
    free(program);
    path(log_path, "output.log");
}

TEST_CASE(raw_sdk_build_finds_its_application_and_companions)
{
    char output[256];
    path(output, "raw-sdk.log");
    const char *help[] = { built, "--help", NULL };
    REQUIRE(tool_run(help, root, output) == 0);
    REQUIRE(file_has("raw-sdk.log", "Usage:"));
    const char *slash = strrchr(built, '/');
    REQUIRE(slash);
    const char *companions[] = { "chibi-compile",
                                 "chibi-player",
                                 "chibi-build-host",
                                 "chibi-compiler-host" };
    for (size_t i = 0; i < sizeof companions / sizeof *companions; i++) {
        char executable[1024];
        int length = snprintf(
            executable,
            sizeof executable,
            "%.*s/%s",
            (int)(slash - built),
            built,
            companions[i]
        );
        REQUIRE(length > 0 && length < (int)sizeof executable);
        REQUIRE(!access(executable, X_OK));
    }
    directory("raw-game");
    directory("raw-llvm");
    write_file("raw-game/Main.chibi", "Main :: () -> int { return 0 }\n");
    char game[256], llvm[256];
    path(game, "raw-game");
    path(llvm, "raw-llvm");
    const char *native[] = { built, "build", "--native", "--llvm",
                             llvm,  game,    NULL };
    REQUIRE(tool_run(native, root, output) == 2);
    REQUIRE(file_has("raw-sdk.log", "must hold LLVM's opt, llc and ld.lld"));
    REQUIRE(!file_has("raw-sdk.log", "Cannot run"));
    return true;
}

void test_tool_chibi_cli_build(const char *binary)
{
    setup_installed(binary);
    RUN_TEST_CASE(raw_sdk_build_finds_its_application_and_companions);
    RUN_TEST_CASE(version_of_the_built_launcher);
    RUN_TEST_CASE(doctor_of_the_built_launcher);
    RUN_TEST_CASE(program_main_runs_from_files_and_directories);
    tool_remove_tree(root);
}

void test_tool_chibi_cli_doctor(const char *binary)
{
    setup(binary);
    RUN_TEST_CASE(doctor_reports_each_capability);
    tool_remove_tree(root);
}

void test_tool_chibi_cli_process(const char *binary, const char *self)
{
    locate_driver(self);
    setup_installed(binary);
    RUN_TEST_CASE(program_process_run_stops_children_when_only_parent_is_signalled);
    RUN_TEST_CASE(program_process_run_hands_the_terminal_to_its_child);
    RUN_TEST_CASE(export_interruption_reaps_children_and_removes_owned_resources);
    tool_remove_tree(root);
}

/* The guide's idle game draws one centered 24x24 blue square. Decode the
 * pixels independently of the player, so a blank or wrong scene cannot pass. */
static bool documented_frame(const char *relative)
{
#ifdef CHIBI_TEST_IMAGE_PREPARATION
    char key[256], error[256];
    snprintf(key, sizeof key, "content/%s", relative);
    if (!render_image_mount_content(root, error, sizeof error)) {
        return false;
    }
    render_image_loader loader = render_images_loader();
    shader_image_view image = { 0 };
    bool loaded = loader.read(loader.context, key, &image, error, sizeof error);
    bool matches = loaded && image.width == 320 && image.height == 180;
    for (int y = 0; matches && y < image.height; y++) {
        for (int x = 0; matches && x < image.width; x++) {
            bool square = x >= 148 && x < 172 && y >= 78 && y < 102;
            const unsigned char *pixel = image.pixels + (y * image.width + x) * 4;
            matches = abs(pixel[0] - (square ? 91 : 0)) <= 1 &&
                      abs(pixel[1] - (square ? 110 : 0)) <= 1 &&
                      abs(pixel[2] - (square ? 225 : 0)) <= 1 && pixel[3] == 255;
        }
    }
    free((void *)image.pixels);
    loader.clear(loader.context);
    render_images_set_loader((render_image_loader){ 0 });
    return matches;
#else
    (void)relative;
    fprintf(stderr, "Export pixel assertions require an image-preparation tooling driver\n");
    return false;
#endif
}

/* A separate selector because these launches need a native graphics context.
 * Only prerequisites below this test's mkdtemp root are removed. The caller's
 * installation, source checkout and SDK build tree are never modified. */
TEST_CASE(installed_export_runs_without_the_project)
{
    directory("prerequisites");
    char prefix[256], sdk[256], cli[256], prerequisites[256];
    strcpy(prefix, launcher);
    *strrchr(prefix, '/') = 0;
    *strrchr(prefix, '/') = 0;
    path(prerequisites, "prerequisites");
    path(sdk, "prerequisites/sdk");
#ifdef __APPLE__
    const char *clone[] = { "/bin/cp", "-cR", prefix, sdk, NULL };
#else
    const char *clone[] = { "/bin/cp", "-R", "--reflink=auto", prefix, sdk, NULL };
#endif
    REQUIRE(tool_run_within(clone, root, log_path, 30) == 0);
    path(cli, "prerequisites/sdk/bin/chibi");
    REQUIRE(documented_project("prerequisites/documented game"));
    char game[256], output[256], destination[256], capture[256];
    path(game, "prerequisites/documented game");
    path(output, "export.log");
    const char *check[] = { cli, "check", game, NULL };
    REQUIRE(tool_run(check, root, output) == 0);
    REQUIRE(!exists("prerequisites/documented game/.chibi"));
    path(capture, "before.png");
    const char *run[] = { cli,           "run",      game,      "--",
                          "--hidden",    "--frames", "2",       "--fixed-dt",
                          "0.016666667", "--size",   "320x180", "--capture",
                          capture,       NULL };
    REQUIRE(tool_run_within(run, root, output, 30) == 0);
    REQUIRE(exists("prerequisites/documented game/.chibi/build/game.chibip"));
    REQUIRE(file_has("export.log", "frames=2 steps=1\n"));
    REQUIRE(documented_frame("before.png"));
    path(destination, "exported game");
    const char *export[] = { cli,         "export",    game, "--output",
                             destination, "--archive", NULL };
    REQUIRE(tool_run_within(export, root, output, 30) == 0);
    directory("relocated");
    char archive[256], original[256], extracted[256], relocated[256],
        executable[256], cwd[256];
#ifdef __APPLE__
    path(archive, "exported game.app.tar.gz");
    path(original, "exported game.app");
    path(extracted, "relocated/exported game.app");
    path(relocated, "relocated/Renamed.app");
    path(executable, "relocated/Renamed.app/Contents/MacOS/My Game");
#else
    path(archive, "exported game.tar.gz");
    path(original, "exported game");
    path(extracted, "relocated/exported game");
    path(relocated, "relocated/Renamed");
    path(executable, "relocated/Renamed/My Game");
#endif
    path(cwd, "relocated");
    const char *extract[] = { "tar", "-xzf", archive, "-C", cwd, NULL };
    REQUIRE(tool_run_within(extract, root, output, 30) == 0);
    REQUIRE(!rename(extracted, relocated));
    tool_remove_tree(original);
    REQUIRE(access(original, F_OK) != 0);
#ifdef __APPLE__
    const char *verify[] = { "codesign", "--verify", "--deep",
                             "--strict", relocated,  NULL };
    REQUIRE(tool_run(verify, root, output) == 0);
#endif
    tool_remove_tree(prerequisites);
    REQUIRE(!exists("prerequisites"));
    REQUIRE(access(cli, F_OK) != 0 && access(game, F_OK) != 0);
    REQUIRE(!exists("prerequisites/documented game/.chibi/build"));
    directory("unrelated");
    path(cwd, "unrelated");
    path(capture, "export.png");
    const char *play[] = { executable,   "--hidden",    "--frames", "2",
                           "--fixed-dt", "0.016666667", "--size",   "320x180",
                           "--capture",  capture,       NULL };
    REQUIRE(tool_run_within(play, cwd, output, 30) == 0);
    REQUIRE(file_has("export.log", "frames=2 steps=1\n"));
    REQUIRE(file_has("export.log", "size=320x180 hidden=true"));
    REQUIRE(documented_frame("export.png"));
    return true;
}

void test_tool_chibi_cli_export(const char *binary)
{
    setup_installed(binary);
    RUN_TEST_CASE(installed_identity_and_doctor);
    RUN_TEST_CASE(installed_export_runs_without_the_project);
    tool_remove_tree(root);
}

void test_tool_chibi_cli_installed(const char *binary, const char *self)
{
    locate_driver(self);
    setup_installed(binary);
    RUN_TEST_CASE(installed_identity_and_doctor);
    RUN_TEST_CASE(installed_project_commands);
    RUN_TEST_CASE(test_runs_shared_package_tests);
    RUN_TEST_CASE(program_main_runs_from_files_and_directories);
    RUN_TEST_CASE(authored_applications_use_independent_program_sessions);
    RUN_TEST_CASE(program_sources_link_packages_and_select_the_root_entry);
    RUN_TEST_CASE(public_cli_is_a_relocatable_ordinary_application);
    RUN_TEST_CASE(program_failures_report_entry_and_source_errors);
    RUN_TEST_CASE(program_watch_patches_a_running_frame_and_keeps_state);
    RUN_TEST_CASE(program_watch_tracks_siblings_and_keeps_imported_state);
    RUN_TEST_CASE(program_cancellation_stops_busy_main_and_tests);
    RUN_TEST_CASE(program_process_run_stops_children_when_only_parent_is_signalled);
    RUN_TEST_CASE(program_process_run_hands_the_terminal_to_its_child);
    RUN_TEST_CASE(export_interruption_reaps_children_and_removes_owned_resources);
    RUN_TEST_CASE(installed_package_commands_and_docs);
    tool_remove_tree(root);
}
