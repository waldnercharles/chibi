#include "assets/project.h"
#include "support/xalloc.h"
#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <unistd.h>

static char root[128], launcher[256], log_path[256], compiler[256], built[256];

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

    const char *implicit_run[] = { launcher, "game with spaces", "--frames", "3", NULL };
    REQUIRE(tool_run(implicit_run, root, log_path) == 0);
    snprintf(
        expected,
        sizeof expected,
        "%s/.chibi/build/game.chibip\n--resource-root\n%s\n--frames\n3\n",
        game,
        game
    );
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
 * older project is reported, not migrated. */
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
    REQUIRE(!mkdir(directory, 0700));
    path(directory, "share/chibi");
    REQUIRE(!mkdir(directory, 0700));
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

/* test hands ROOT, or the working directory, to the compiler's --test and
 * exits with its status. */
TEST_CASE(test_runs_the_compiler_tests)
{
    char file[256];
    path(file, "fail");
    unlink(file);
    const char *named[] = { launcher, "test", "game with spaces", NULL };
    REQUIRE(tool_run(named, root, log_path) == 0);
    REQUIRE(file_is("compile.args", "--project-root\ngame with spaces\n--test\n"));
    const char *here[] = { launcher, "test", NULL };
    REQUIRE(tool_run(here, root, log_path) == 0);
    REQUIRE(file_is("compile.args", "--project-root\n.\n--test\n"));

    write_file("fail", "");
    REQUIRE(tool_run(named, root, log_path) == 37);
    REQUIRE(!unlink(file));
    const char *help[] = { launcher, "test", "--help", NULL };
    REQUIRE(tool_run(help, root, log_path) == 0);
    const char *extra[] = { launcher, "test", "one", "two", NULL };
    REQUIRE(tool_run(extra, root, log_path) == 2);
    return true;
}

static const char
    FACTS[] = "package facts\n"
              "import strings \"core:strings\"\n"
              "\n"
              "@test\n"
              "Passes :: fn() {\n"
              "    expect(strings.ByteLength(\"abc\") == 3, \"three bytes\");\n"
              "}\n"
              "\n"
              "@test\n"
              "Fails :: fn() {\n"
              "    expect(strings.ByteLength(\"abc\") == 4, \"four bytes\");\n"
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
        "Expectation failed: four bytes\n2 tests, 1 failed\n",
        tests
    );
    REQUIRE(file_is("tests.log", expected));

    char *passing = xstrdup(FACTS);
    *strstr(passing, "== 4") = '!';
    write_file("tests/facts/Facts.chibi", passing);
    free(passing);
    REQUIRE(tool_run(run, root, output) == 0);
    REQUIRE(file_is("tests.log", "PASS facts.Passes\nPASS facts.Fails\n2 tests, 0 failed\n"));

    write_file("broken/facts/Facts.chibi", "package facts\nBroken :: fn() -> int { return missing; }\n");
    char broken[256];
    path(broken, "broken");
    const char *invalid[] = { compiler, "--project-root", broken, "--test", NULL };
    REQUIRE(tool_run(invalid, root, output) == 1);
    REQUIRE(file_has("tests.log", "/broken/facts/Facts.chibi:2:"));

    write_file("game/sounds/Sounds.chibi", "package sounds\n@test\nQuiet :: fn() {\n    set_master_volume(0.5);\n}\n");
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

void test_tool_chibi_cli(const char *binary)
{
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
    RUN_TEST_CASE(compiler_check_writes_nothing);
    RUN_TEST_CASE(version_reads_the_build_identity);
    RUN_TEST_CASE(version_of_the_built_launcher);
    RUN_TEST_CASE(doctor_reports_each_capability);
    RUN_TEST_CASE(doctor_of_the_built_launcher);
    RUN_TEST_CASE(test_runs_the_compiler_tests);
    RUN_TEST_CASE(compiler_test_reports_each_test);
    RUN_TEST_CASE(compiler_test_needs_the_core_collection);
    tool_remove_tree(root);
}
