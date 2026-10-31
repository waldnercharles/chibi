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

/* How long a launcher may take to compile a program nothing has compiled
 * before: 8 to 10 s for a small one on a workstation, more with several
 * selectors sharing a CI runner, and 0.5 s from the kept closure. Every wait on
 * a program's first output, a first compile or a command that runs one uses it;
 * a passing case takes no longer, a failing one takes this long. */
#define FIRST_COMPILE_SECONDS 120

/* Runs a command with a bound for a launcher that compiles on first use: about
 * 8 s for a small package on a workstation, longer on a CI runner; the same
 * sources start from the kept closure afterwards. A failing case takes as long
 * as the bound, a passing one does not. */
static int cli_run(const char *const argv[], const char *cwd, const char *log)
{
    return tool_run_within(argv, cwd, log, FIRST_COMPILE_SECONDS);
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
    return cli_run(args, root, log_path) == 0;
}

/* The launcher compiles its application on a first start, which takes longer
 * than a case may run; start it once, with time to finish, so every case after
 * it starts from the kept closure. Its status does not matter. */
static void warm_launcher()
{
    const char *version[] = { launcher, "version", NULL };
    char output[256];
    path(output, "warm.log");
    tool_run_within(version, root, output, 300);
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
    assert(!cli_run(copy, root, log_path));
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
    /* The stub counts its calls and can be made to fail, and otherwise runs the
     * real compiler, which finds the core collection beside its own folder. */
    char *built_directory = realpath(binary, NULL);
    assert(built_directory);
    *strrchr(built_directory, '/') = 0;
    char stub[1024];
    snprintf(
        stub,
        sizeof stub,
        "#!/bin/sh\nset -eu\n"
        "base=$(dirname \"$0\")/..\n"
        "printf 'compile\\n' >> \"$base/compiles\"\n"
        "[ ! -f \"$base/fail\" ] || exit 37\n"
        "exec \"%s/chibi-compile\" \"$@\"\n",
        built_directory
    );
    free(built_directory);
    write_file("bin/chibi-compile", stub);
    char program[256];
    path(program, "bin/chibi-compile");
    assert(!chmod(program, 0700));
    warm_launcher();
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

/* version prints the build identity document beside the shaders, with the
 * build ID packaging adds, and fails without a valid one. */
TEST_CASE(version_reads_the_build_identity)
{
    char output[256];
    path(output, "version.log");
    const char *version[] = { launcher, "version", NULL };
    REQUIRE(cli_run(version, root, output) == 1);

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
    REQUIRE(cli_run(version, root, output) == 0);
    char expected[1024];
    snprintf(expected, sizeof expected, "chibi 9.8.7\nbuild: unpackaged\ncompiler: %s/bin/chibi-compile\nproject format: 25\nartifact formats: execution 15, player 3\n", root);
    REQUIRE(file_is("version.log", expected));

    snprintf(document, sizeof document, "{\"manifest_format\": 1, \"build_id\": \"fixture-build\", \"sdk_version\": \"9.8.7\", \"project_version\": 25, %s}\n", formats);
    write_file("share/chibi/engine.json", document);
    REQUIRE(cli_run(version, root, output) == 0);
    snprintf(expected, sizeof expected, "chibi 9.8.7\nbuild: fixture-build\ncompiler: %s/bin/chibi-compile\nproject format: 25\nartifact formats: execution 15, player 3\n", root);
    REQUIRE(file_is("version.log", expected));

    write_file("share/chibi/engine.json", "{\"sdk_version\": \"9.8.7\", \"project_version\": 25, \"artifact_formats\": {\"execution\": {}}}\n");
    REQUIRE(cli_run(version, root, output) == 1);
    const char *help[] = { launcher, "version", "--help", NULL };
    REQUIRE(cli_run(help, root, output) == 0);
    const char *extra[] = { launcher, "version", "more", NULL };
    REQUIRE(cli_run(extra, root, output) == 2);
    return true;
}

/* The launcher in place reads the identity its build generated. */
TEST_CASE(version_of_the_built_launcher)
{
    char output[256], expected[64];
    path(output, "built.log");
    const char *version[] = { built, "version", NULL };
    REQUIRE(cli_run(version, root, output) == 0);
    char *text = tool_read(output);
    snprintf(expected, sizeof expected, "\nproject format: %d\n", CHIBI_PROJECT_VERSION);
    bool reported = text && !strncmp(text, "chibi ", 6) && strstr(text, expected) &&
                    strstr(text, "\nartifact formats: ") &&
                    strstr(text, " execution ");
    free(text);
    REQUIRE(reported);
    return true;
}

/* The number of closures kept under `cache`. */
static int closures_kept(const char *cache)
{
    char folder[512];
    snprintf(folder, sizeof folder, "%s/closures", cache);
    DIR *open = opendir(folder);
    int count = 0;
    for (struct dirent *entry; open && (entry = readdir(open));) {
        count += !strncmp(entry->d_name, "closure-", 8);
    }
    if (open) {
        closedir(open);
    }
    return count;
}

static void append_comment(const char *relative)
{
    char file[256];
    path(file, relative);
    FILE *stream = fopen(file, "a");
    assert(stream);
    fputs("// edited\n", stream);
    fclose(stream);
}

/* The launcher compiles its application once per set of sources: a second start
 * reuses the kept closure, and an edit to the application or to a core package
 * it imports compiles another. */
TEST_CASE(launcher_keeps_its_compiled_application)
{
    char cache[256];
    path(cache, "closure-cache");
    const char *previous = getenv("CHIBI_ASSET_CACHE_DIR");
    char *saved = previous ? xstrdup(previous) : NULL;
    REQUIRE(!setenv("CHIBI_ASSET_CACHE_DIR", cache, 1));
    const char *version[] = { launcher, "version", NULL };
    bool compiled = cli_run(version, root, log_path) == 0 && closures_kept(cache) == 1;
    bool reused = cli_run(version, root, log_path) == 0 && closures_kept(cache) == 1;
    append_comment("share/chibi/apps/chibi/Main.chibi");
    bool application = cli_run(version, root, log_path) == 0 &&
                       closures_kept(cache) == 2;
    append_comment("share/chibi/core/strings/Strings.chibi");
    bool core = cli_run(version, root, log_path) == 0 && closures_kept(cache) == 3;
    if (saved) {
        setenv("CHIBI_ASSET_CACHE_DIR", saved, 1);
    } else {
        unsetenv("CHIBI_ASSET_CACHE_DIR");
    }
    free(saved);
    REQUIRE(compiled && reused && application && core);
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
static int doctor(const char *extra, const char *programs)
{
    char output[256], saved[4096];
    path(output, "doctor.log");
    const char *previous = getenv("PATH");
    assert(previous && strlen(previous) < sizeof saved);
    snprintf(saved, sizeof saved, "%s", previous);
    setenv("PATH", programs, 1);
    const char *args[] = { launcher, "doctor", extra, NULL };
    int status = cli_run(args, root, output);
    setenv("PATH", saved, 1);
    return status;
}

/* doctor reports each capability and fails only for a broken one it checks. */
TEST_CASE(doctor_reports_each_capability)
{
    char file[256], expected[512];
    path(file, "fail");
    unlink(file);
    const char *tools = "/usr/bin:/bin";

    identity(NULL, "\"bytecode\", \"sljit\"", true);
    REQUIRE(doctor(NULL, tools) == 0);
    REQUIRE(file_has("doctor.log", "sdk: supported (chibi 9.8.7, build unpackaged)\n"));
    snprintf(expected, sizeof expected, "compiler: supported (%s/bin/chibi-compile)\n", root);
    REQUIRE(file_has("doctor.log", expected));
    REQUIRE(file_has("doctor.log", "platform: supported ("));
    REQUIRE(file_has("doctor.log", "native backend: supported (SLJIT)\n"));

    identity(NULL, "\"bytecode\"", true);
    REQUIRE(doctor(NULL, tools) == 0);
    REQUIRE(
        file_has("doctor.log", "native backend: unavailable (built for the bytecode interpreter only)\n")
    );

    /* A platform mismatch is reported; whether the SDK runs is the compiler's
     * question. */
    identity("Plan9", "\"bytecode\"", true);
    REQUIRE(doctor(NULL, tools) == 0);
    REQUIRE(file_has("doctor.log", "platform: unavailable (built for Plan9 "));
    identity(NULL, "\"bytecode\"", false);
    REQUIRE(doctor(NULL, tools) == 1);
    REQUIRE(file_has("doctor.log", "platform: broken (the build identity names no platform)\n"));
    identity(NULL, "\"bytecode\"", true);

    write_file("fail", "");
    REQUIRE(doctor(NULL, tools) == 1);
    snprintf(expected, sizeof expected, "compiler: broken (%s/bin/chibi-compile --help failed)\n", root);
    REQUIRE(file_has("doctor.log", expected));
    path(file, "fail");
    REQUIRE(!unlink(file));

    REQUIRE(doctor("--help", tools) == 0);
    REQUIRE(doctor("--frames", tools) == 2);
    return true;
}

/* The built launcher in place finds its real compiler. */
TEST_CASE(doctor_of_the_built_launcher)
{
    char output[256];
    path(output, "built-doctor.log");
    const char *args[] = { built, "doctor", NULL };
    REQUIRE(cli_run(args, root, output) == 0);
    REQUIRE(file_has("built-doctor.log", "\ncompiler: supported ("));
    REQUIRE(file_has("built-doctor.log", "\nplatform: supported ("));
    return true;
}

TEST_CASE(test_runs_shared_package_tests)
{
    char folder[256];
    path(folder, "package-tests");
    REQUIRE(!mkdir(folder, 0700));
    /* Tests run without a program service (#880): they print, and nothing else. */
    const char *passing = "package facts\n"
                          "@test\nPasses :: () {\n"
                          " print_string(\"a test prints as it runs\")\n"
                          " two: int = 2\n"
                          " assert(two + two == 4, \"addition\")\n}\n"
                          "@test\nPureOne :: () {\n two: int = 2\n assert(two "
                          "+ two == 4, \"addition\")\n}\n"
                          "@test\nPureTwo :: () {\n three: int = 3\n "
                          "assert(three * three == 9, \"multiplication\")\n}\n";
    write_file("package-tests/Facts.chibi", passing);
    const char *named[] = { launcher, "test", "package-tests", NULL };
    REQUIRE(cli_run(named, root, log_path) == 0);
    REQUIRE(file_has("output.log", "PASS project.Passes\n"));
    REQUIRE(file_has("output.log", "3 tests, 0 failed\n"));
    const char *here[] = { launcher, "test", NULL };
    REQUIRE(cli_run(here, folder, log_path) == 0);
    REQUIRE(file_has("output.log", "PASS project.Passes\n"));
    const char *previous = getenv("CHIBI_TEST_WORKERS");
    char *saved_workers = previous ? xstrdup(previous) : NULL;
    const char *workers[] = { "1", "2" };
    bool worker_settings = true;
    for (int i = 0; i < 2; i++) {
        worker_settings &= !setenv("CHIBI_TEST_WORKERS", workers[i], 1);
        worker_settings &= cli_run(named, root, log_path) == 0;
        worker_settings &= file_is("output.log", "a test prints as it runs\nPASS project.Passes\nPASS project.PureOne\nPASS project.PureTwo\n3 tests, 0 failed\n");
    }
    worker_settings &= !setenv("CHIBI_TEST_WORKERS", "0", 1);
    worker_settings &= cli_run(named, root, log_path) == 1;
    worker_settings &= file_has("output.log", "CHIBI_TEST_WORKERS");
    const char *general[] = { launcher, "--help", NULL };
    worker_settings &= cli_run(general, root, log_path) == 0;
    if (saved_workers) {
        worker_settings &= !setenv("CHIBI_TEST_WORKERS", saved_workers, 1);
    } else {
        worker_settings &= !unsetenv("CHIBI_TEST_WORKERS");
    }
    free(saved_workers);
    REQUIRE(worker_settings);
    write_file("package-tests/Facts.chibi", "package facts\n@test\nFails :: () { assert(false, \"shared test failure\") }\n");
    REQUIRE(cli_run(named, root, log_path) == 1);
    REQUIRE(file_has("output.log", "FAIL project.Fails\n"));
    REQUIRE(file_has("output.log", "Facts.chibi:3:"));
    REQUIRE(file_has("output.log", "shared test failure"));
    write_file("package-tests/Facts.chibi", "package facts\n@test\nFails :: () { missing() }\n");
    REQUIRE(cli_run(named, root, log_path) == 1);
    REQUIRE(file_has("output.log", "Facts.chibi:3:"));
    REQUIRE(file_has("output.log", "missing"));
    write_file("package-tests/Facts.chibi", passing);
    const char *help[] = { launcher, "test", "--help", NULL };
    REQUIRE(cli_run(help, root, log_path) == 0);
    const char *extra[] = { launcher, "test", "one", "two", NULL };
    REQUIRE(cli_run(extra, root, log_path) == 2);
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
    REQUIRE(cli_run(run, root, output) == 1);
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
        worker_settings &= cli_run(run, root, output) == 1;
        worker_settings &= file_is("tests.log", expected);
    }
    const char *invalid_workers[] = {
        "", "0", "33", "-1", "+2", " 2", "2x", "9999999999999999999999999999"
    };
    for (size_t i = 0; i < sizeof invalid_workers / sizeof *invalid_workers; i++) {
        worker_settings &= !setenv("CHIBI_TEST_WORKERS", invalid_workers[i], 1);
        worker_settings &= cli_run(run, root, output) == 1;
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
    REQUIRE(cli_run(run, root, output) == 0);
    REQUIRE(file_is("tests.log", "PASS facts.Passes\nPASS facts.Fails\n2 tests, 0 failed\n"));

    /* A package is compiled when it declares a test. */
    write_file("broken/facts/Facts.chibi", "package facts\n@test\nOk :: () {}\nBroken :: () -> int { return missing; }\n");
    char broken[256];
    path(broken, "broken");
    const char *invalid[] = { compiler, "--project-root", broken, "--test", NULL };
    REQUIRE(cli_run(invalid, root, output) == 1);
    REQUIRE(file_has("tests.log", "/broken/facts/Facts.chibi:4:"));

    write_file("game/sounds/Sounds.chibi", "package sounds\n@test\nQuiet :: () {\n    set_master_volume(0.5);\n}\n");
    char game[256];
    path(game, "game");
    const char *service[] = { compiler, "--project-root", game, "--test", NULL };
    REQUIRE(cli_run(service, root, output) == 1);
    REQUIRE(file_has("tests.log", "/game/sounds/Sounds.chibi:4:5: "));

    char missing[256];
    path(missing, "missing");
    const char *absent[] = { compiler, "--project-root", missing, "--test", NULL };
    REQUIRE(cli_run(absent, root, output) == 1);
    const char *scene[] = { compiler, "--project-root", tests,
                            "--test", "--scene",        "x",
                            NULL };
    REQUIRE(cli_run(scene, root, output) == 2);
    const char *both[] = { compiler, "--project-root", tests,
                           "--test", "--check",        NULL };
    REQUIRE(cli_run(both, root, output) == 2);
    return true;
}

TEST_CASE(compiler_runs_cost_explanation_regressions)
{
    const char *run[] = { compiler,
                          "--project-root",
                          CHIBI_SOURCE_ROOT "/tools/compiler/cost_explanations",
                          "--test",
                          NULL };
    REQUIRE(cli_run(run, root, log_path) == 0);
    REQUIRE(file_has("output.log", "PASS project.EmptyReport\n"));
    REQUIRE(file_has("output.log", "PASS project.FullWidthIdentifiersAndJsonEscaping\n"));
    REQUIRE(file_has("output.log", "6 tests, 0 failed\n"));
    return true;
}

/* A package that declares `foreign import`, or imports one that does, is native
 * only: the VM cannot prepare it, so the runner reports it as skipped. */
TEST_CASE(compiler_test_skips_native_only_packages)
{
    const char *run[] = { compiler,
                          "--project-root",
                          CHIBI_SOURCE_ROOT "/test/compiler/samples/native",
                          "--test",
                          NULL };
    REQUIRE(cli_run(run, root, log_path) == 0);
    REQUIRE(file_is(
        "output.log",
        "SKIP foreign_declared/facts (native only: foreign import)\n"
        "SKIP foreign_library/facts (native only: foreign import)\n"
        "SKIP foreign_posix/facts (native only: foreign import)\n"
        "SKIP foreign_vendor/facts (native only: foreign import)\n"
        "PASS plain_package/facts.Calls\n"
        "1 tests, 0 failed, 4 skipped\n"
    ));
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
    REQUIRE(cli_run(clone, root, log_path) == 0);
    path(output, "core.log");
    const char *run[] = { copy, "--project-root", root, "--test", NULL };
    REQUIRE(cli_run(run, root, output) == 1);
    REQUIRE(file_has("core.log", "The SDK has no core collection at "));
    REQUIRE(!unlink(copy));
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
    REQUIRE(cli_run(args, root, log_path) == 0);
    REQUIRE(file_is("output.log", "native-driver\n"));
    char expected[4096];
    int length = snprintf(
        expected,
        sizeof expected,
        "--root\n%s/share/chibi/tools/native_aot\n"
        "--source\n%s/share/chibi/tools/native_aot/Driver.chibi\n"
        "--collection\nnative_aot=%s/share/chibi/tools/native_aot/driver\n"
        "--collection\nnative_link=%s/share/chibi/compiler/native_link\n"
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
        root,
        root,
        package,
        llvm
    );
    REQUIRE(length > 0 && length < (int)sizeof expected);
    REQUIRE(file_is("native.args", expected));
    write_file("native.fail", "");
    REQUIRE(cli_run(args, root, log_path) == 37);
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
    char folder[256], source[256];
    path(folder, "standalone dispatch");
    path(source, "standalone dispatch/Hello.chibi");
    char *before = contents("compiles");
    const char *commands[][4] = {
        { launcher, "run", "standalone dispatch/Hello.chibi", NULL },
        { launcher, "standalone dispatch/Hello.chibi", NULL },
        { launcher, "run", "standalone dispatch", NULL },
        { launcher, "standalone dispatch", NULL },
    };
    for (int i = 0; i < 4; i++) {
        REQUIRE(cli_run(commands[i], root, log_path) == 0);
        REQUIRE(file_is("output.log", "dispatch-main\n"));
        REQUIRE(!exists("standalone dispatch/.chibi"));
    }
    const char *here[] = { launcher, ".", NULL };
    REQUIRE(cli_run(here, folder, log_path) == 0);
    REQUIRE(file_is("output.log", "dispatch-main\n"));
    const char *implicit[] = { launcher, "run", NULL };
    REQUIRE(cli_run(implicit, folder, log_path) == 0);
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
        REQUIRE(cli_run(watch_games[i], root, log_path) == 2);
    }
    const char *extra[] = { launcher, "run", source, "--", "extra", NULL };
    REQUIRE(cli_run(extra, root, log_path) == 0);
    REQUIRE(file_is("output.log", "dispatch-main\n"));
    const char *unnamed_extra[] = { launcher, source, "extra", NULL };
    REQUIRE(cli_run(unnamed_extra, root, log_path) == 0);
    REQUIRE(file_is("output.log", "dispatch-main\n"));
    write_file("fail", "");
    REQUIRE(cli_run(commands[0], root, log_path) == 0);
    REQUIRE(file_is("output.log", "dispatch-main\n"));
    char failure[256];
    path(failure, "fail");
    REQUIRE(!unlink(failure));
    REQUIRE(file_is("compiles", before));
    free(before);
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
    return cli_run(args, root, log_path);
}

TEST_CASE(program_main_runs_from_files_and_directories)
{
    directory("run-main");
    write_file("run-main/Hello.chibi", HELLO_PROGRAM);
    REQUIRE(run_program("run-main/Hello.chibi") == 0);
    REQUIRE(file_is("output.log", "Hello, world!\n"));
    const char *file[] = { built, "run-main/Hello.chibi", NULL };
    REQUIRE(cli_run(file, root, log_path) == 0);
    REQUIRE(file_is("output.log", "Hello, world!\n"));
    REQUIRE(run_program("run-main") == 0);
    REQUIRE(file_is("output.log", "Hello, world!\n"));
    const char *folder[] = { built, "run-main", NULL };
    REQUIRE(cli_run(folder, root, log_path) == 0);
    REQUIRE(file_is("output.log", "Hello, world!\n"));
    char cwd[256], source[256];
    path(cwd, "run-main");
    path(source, "run-main/Hello.chibi");
    const char *here[] = { built, ".", NULL };
    REQUIRE(cli_run(here, cwd, log_path) == 0);
    REQUIRE(file_is("output.log", "Hello, world!\n"));
    const char *implicit[] = { built, "run", NULL };
    REQUIRE(cli_run(implicit, cwd, log_path) == 0);
    REQUIRE(file_is("output.log", "Hello, world!\n"));
    const char *direct[] = { compiler, "--project-root", source, "--run", NULL };
    REQUIRE(cli_run(direct, root, log_path) == 2);
    REQUIRE(!file_has("output.log", "Hello, world!"));
    const char *conflict[] = { compiler, "--project-root", source,
                               "--run",  "--test",         NULL };
    REQUIRE(cli_run(conflict, root, log_path) == 2);

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
        " c := program.call(first); d := program.call(second)\n"
        " assert(c.status == 0 && c.result == 142, \"retained reload\")\n"
        " assert(d.status == 0 && d.result == 202, \"independent "
        "generation\")\n"
        " assert(files.write(path, \"package counter\\nMain :: () { missing() "
        "}\\n\"), \"write invalid edit\")\n"
        " assert(program.update(first) == -1, \"reject invalid edit\")\n"
        " assert(program.diagnostic(first) != \"\", \"source diagnostic\")\n"
        " e := program.call(first)\n"
        " assert(e.status == 0 && e.result == 143, \"last valid state\")\n"
        " program.close(first)\n"
        " f := program.call(second)\n"
        " assert(f.status == 0 && f.result == 203, \"other handle remains "
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
    REQUIRE(cli_run(args, root, log_path) == 0);
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
    return cli_run(args, root, log_path) == 0;
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
    REQUIRE(cli_run(args, cwd, log_path) == 0);
    REQUIRE(file_is("output.log", "Hello, world!\n"));
    /* The public CLI itself runs through the ordinary source-session API. */
    const char *self[] = { relocated, application, "--", "version", NULL };
    REQUIRE(cli_run(self, cwd, log_path) == 0);
    REQUIRE(file_has("output.log", "chibi "));
    /* Replacing the whole package controls every command without C fallback. */
    tool_remove_tree(application);
    directory("program-sdk/share/chibi/apps/chibi");
    write_file("program-sdk/share/chibi/apps/chibi/Main.chibi", "package chibi\nMain :: () -> int { return 42 }\n");
    REQUIRE(cli_run(args, cwd, log_path) == 42);
    REQUIRE(file_is("output.log", ""));
    const char *version[] = { relocated, "version", NULL };
    REQUIRE(cli_run(version, cwd, log_path) == 42);
    REQUIRE(file_is("output.log", ""));
    tool_remove_tree(application);
    REQUIRE(cli_run(args, cwd, log_path) == 1);
    REQUIRE(file_has("output.log", "apps/chibi"));
    REQUIRE(!file_has("output.log", "Hello, world!"));
    directory("program-sdk/share/chibi/apps/chibi");
    write_file("program-sdk/share/chibi/apps/chibi/Main.chibi", "package chibi\nMain :: ( {\n");
    REQUIRE(cli_run(args, cwd, log_path) == 1);
    REQUIRE(file_has("output.log", "Main.chibi"));
    REQUIRE(!file_has("output.log", "Hello, world!"));
    tool_remove_tree(application);
    REQUIRE(copy_resource(built, "apps", share));
    REQUIRE(cli_run(args, cwd, log_path) == 0);
    REQUIRE(file_is("output.log", "Hello, world!\n"));
    REQUIRE(!exists("program-sdk/.chibi"));
    REQUIRE(!exists("unrelated-program-cwd/.chibi"));
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
    REQUIRE(file_has("output.log", "Unknown function 'runner.snapshot'"));
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

/* A watched program compiles before it prints, and again after each edit it
 * picks up: 8 s for a small one on a workstation, longer on a loaded CI runner,
 * so its output gets the bound cli_run gives a first compile. */
static bool watch_output(chibi_process *process, size_t after, const char *expected)
{
    double deadline = chibi_process_now() + FIRST_COMPILE_SECONDS;
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
    double deadline = chibi_process_now() + FIRST_COMPILE_SECONDS;
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
    RUN_TEST_CASE(version_reads_the_build_identity);
    RUN_TEST_CASE(version_of_the_built_launcher);
    RUN_TEST_CASE(doctor_reports_each_capability);
    RUN_TEST_CASE(doctor_of_the_built_launcher);
    RUN_TEST_CASE(launcher_keeps_its_compiled_application);
    RUN_TEST_CASE(test_runs_shared_package_tests);
    RUN_TEST_CASE(compiler_test_reports_each_test);
    RUN_TEST_CASE(compiler_runs_cost_explanation_regressions);
    RUN_TEST_CASE(compiler_test_skips_native_only_packages);
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
    REQUIRE(cli_run(version, root, output) == 0);
    REQUIRE(file_has("installed.log", "chibi ") && !file_has("installed.log", "build: unpackaged"));
    const char *doctor[] = { launcher, "doctor", NULL };
    REQUIRE(cli_run(doctor, root, output) == 0);
    REQUIRE(file_has("installed.log", "\ncompiler: supported ("));
    return true;
}

TEST_CASE(installed_package_commands)
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
    REQUIRE(cli_run(run, root, output) == 1);
    REQUIRE(file_has("installed.log", "PASS facts.Passes\nFAIL facts.Fails\n"));
    REQUIRE(file_has("installed.log", "Assertion failed: four bytes\n2 tests, 1 failed\n"));

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
    warm_launcher();
}

TEST_CASE(raw_sdk_build_finds_its_application_and_companions)
{
    char output[256];
    path(output, "raw-sdk.log");
    const char *help[] = { built, "--help", NULL };
    REQUIRE(cli_run(help, root, output) == 0);
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
    REQUIRE(cli_run(native, root, output) == 2);
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
    tool_remove_tree(root);
}

void test_tool_chibi_cli_export(const char *binary)
{
    setup_installed(binary);
    RUN_TEST_CASE(installed_identity_and_doctor);
    tool_remove_tree(root);
}

void test_tool_chibi_cli_installed(const char *binary, const char *self)
{
    locate_driver(self);
    setup_installed(binary);
    RUN_TEST_CASE(installed_identity_and_doctor);
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
    RUN_TEST_CASE(installed_package_commands);
    tool_remove_tree(root);
}
