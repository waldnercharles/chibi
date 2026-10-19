#include "assets/project.h"
#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char root[128], launcher[256], log_path[256], compiler[256];

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

/* check hands the project to the compiler's --check and writes nothing; an
 * older project is reported, not migrated. */
TEST_CASE(check_prepares_without_writing)
{
    project("checked", CHIBI_PROJECT_VERSION);
    const char *check[] = { launcher, "check", "checked", NULL };
    REQUIRE(tool_run(check, root, log_path) == 0);
    char expected[512];
    snprintf(expected, sizeof expected, "--project-root\n%s/checked\n--check\n", root);
    REQUIRE(file_is("compile.args", expected));
    REQUIRE(!exists("checked/.chibi"));

    write_file("fail", "");
    REQUIRE(tool_run(check, root, log_path) == 37);
    char file[256];
    path(file, "fail");
    REQUIRE(!unlink(file));
    REQUIRE(!exists("checked/.chibi"));

    project("old", CHIBI_PROJECT_VERSION - 1);
    char manifest[256];
    path(file, "old/project.kdl");
    char *before = tool_read(file);
    snprintf(manifest, sizeof manifest, "%s", before);
    free(before);
    const char *old[] = { launcher, "check", "old", NULL };
    REQUIRE(tool_run(old, root, log_path) == 2);
    REQUIRE(file_is("old/project.kdl", manifest));
    REQUIRE(!exists("old/.chibi"));

    const char *help[] = { launcher, "check", "--help", NULL };
    REQUIRE(tool_run(help, root, log_path) == 0);
    const char *extra[] = { launcher, "check", "checked", "more", NULL };
    REQUIRE(tool_run(extra, root, log_path) == 2);
    return true;
}

/* The real compiler's --check prepares a project, fails an invalid one and
 * leaves both, and an older one, exactly as they were. */
TEST_CASE(compiler_check_writes_nothing)
{
    char game[256];
    path(game, "checked");
    const char *valid[] = { compiler, "--project-root", game, "--check", NULL };
    REQUIRE(tool_run(valid, root, log_path) == 0);
    REQUIRE(!exists("checked/.chibi"));

    write_file("checked/content/scenes/Start.kdl", "version 1\nname \"Start\"\n(array)entities {\n");
    REQUIRE(tool_run(valid, root, log_path) == 1);
    REQUIRE(
        file_is("checked/content/scenes/Start.kdl", "version 1\nname \"Start\"\n(array)entities {\n")
    );

    char old[256], file[256];
    path(old, "old");
    path(file, "old/project.kdl");
    char *before = tool_read(file);
    const char *older[] = { compiler, "--project-root", old, "--check", NULL };
    REQUIRE(tool_run(older, root, log_path) == 1);
    bool kept = file_is("old/project.kdl", before);
    free(before);
    REQUIRE(kept);

    const char *both[] = { compiler,  "--project-root", game,
                           "--check", "--output",       file,
                           NULL };
    REQUIRE(tool_run(both, root, log_path) == 2);
    REQUIRE(!exists("checked/.chibi"));
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
    free(directory);
    RUN_TEST_CASE(build_and_run_use_the_prepared_project);
    RUN_TEST_CASE(check_prepares_without_writing);
    RUN_TEST_CASE(compiler_check_writes_nothing);
    tool_remove_tree(root);
}
