#include "host/process.h"
#include "support/xalloc.h"
#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef __linux__
/* `chibi build --native` through a relocated SDK and the build tree's own
 * launcher. Controlled LLVM stand-ins (the driver's stage-tool.sh) cover the
 * process contract; CHIBI_LLVM_BIN's real tools cover the program. */
static char suite[256], built[1024], sdk[1024], launcher[1024], game[1024],
    tools[1024], output[65536], errors[65536];
static char *stage_tool;
static chibi_process_capture_result status;

static void path(char out[1024], const char *base, const char *name)
{
    int size = snprintf(out, 1024, "%s/%s", base, name);
    assert(size > 0 && size < 1024);
}

static bool exists(const char *name)
{
    struct stat info;
    return !lstat(name, &info);
}

static void copy(const char *from, const char *to)
{
    char log[1024];
    path(log, suite, "copy.log");
    const char *argv[] = { "/bin/cp", "-R", "--reflink=auto", from, to, NULL };
    assert(!tool_run(argv, suite, log));
}

static void directory(const char *base, const char *name)
{
    char folder[1024];
    path(folder, base, name);
    assert(!mkdir(folder, 0700));
}

static void write_at(const char *base, const char *name, const char *text)
{
    char file[1024];
    path(file, base, name);
    tool_write(file, text);
}

/* An installed SDK's layout, assembled from the build tree beside `binary`. */
static void relocate(const char *binary)
{
    char *bin = xstrdup(binary);
    *strrchr(bin, '/') = 0;
    path(built, bin, "..");
    path(sdk, suite, "sdk with spaces");
    assert(!mkdir(sdk, 0700));
    const char *folders[] = { "bin",   "libexec",     "libexec/chibi",
                              "share", "share/chibi", "share/chibi/tools" };
    for (int i = 0; i < 6; i++) {
        directory(sdk, folders[i]);
    }
    char from[1024], to[1024];
    path(to, sdk, "bin/chibi");
    copy(binary, to);
    const char *hosts[] = { "chibi-build-host", "chibi-compiler-host" };
    for (int i = 0; i < 2; i++) {
        path(from, bin, hosts[i]);
        snprintf(to, sizeof to, "%s/libexec/chibi/%s", sdk, hosts[i]);
        copy(from, to);
    }
    const char *shared[] = { "compiler", "core", "tools/native_aot" };
    for (int i = 0; i < 3; i++) {
        snprintf(from, sizeof from, "%s/share/chibi/%s", built, shared[i]);
        snprintf(to, sizeof to, "%s/share/chibi/%s", sdk, shared[i]);
        copy(from, to);
    }
    path(launcher, sdk, "bin/chibi");
    free(bin);
}

/* A fresh package folder and LLVM folder of controlled tools. */
static void reset(const char *name, const char *source)
{
    char folder[1024];
    path(folder, suite, name);
    assert(!mkdir(folder, 0700));
    path(game, folder, "game with spaces");
    path(tools, folder, "llvm with spaces");
    assert(!mkdir(game, 0700) && !mkdir(tools, 0700));
    write_at(game, "Main.chibi", source);
    const char *names[] = { "opt", "llc", "ld.lld" };
    for (int i = 0; i < 3; i++) {
        char tool[1024];
        path(tool, tools, names[i]);
        tool_write(tool, stage_tool);
        assert(!chmod(tool, 0700));
    }
    write_at(tools, "action", "ok\n");
}

/* Runs `chibi build --native` with an empty PATH and no CHIBI_LLVM_BIN. */
static int build(const char *program, const char *mode, const char *llvm)
{
    chibi_process_output out = { .data = output, .capacity = sizeof output };
    chibi_process_output err = { .data = errors, .capacity = sizeof errors };
    const char *argv[] = { program,  "build", "--native", mode,
                           "--llvm", llvm,    game,       NULL };
    const char *envp[] = { "PATH=", NULL };
    status = chibi_process_capture(program, argv, suite, envp, chibi_process_now() + 120, NULL, &out, &err);
    assert(status.error == CHIBI_PROCESS_OK);
    return status.status;
}

/* The one entry of game/build/native starting with `prefix` and ending with
 * `suffix`, or false when there is not exactly one. */
static bool only(const char *prefix, const char *suffix, char found[1024])
{
    char folder[1024];
    path(folder, game, "build/native");
    DIR *listing = opendir(folder);
    int count = 0;
    for (struct dirent *entry; listing && (entry = readdir(listing));) {
        size_t length = strlen(entry->d_name), tail = strlen(suffix);
        bool named = !strncmp(entry->d_name, prefix, strlen(prefix)) &&
                     length >= tail &&
                     !strcmp(entry->d_name + length - tail, suffix) &&
                     (*suffix || !strchr(entry->d_name, '.'));
        if (named) {
            path(found, folder, entry->d_name);
            count++;
        }
    }
    if (listing) {
        closedir(listing);
    }
    return count == 1;
}

static int count(const char *prefix)
{
    char folder[1024];
    path(folder, game, "build/native");
    DIR *listing = opendir(folder);
    int found = 0;
    for (struct dirent *entry; listing && (entry = readdir(listing));) {
        found += !strncmp(entry->d_name, prefix, strlen(prefix));
    }
    if (listing) {
        closedir(listing);
    }
    return found;
}

static const char *const
    SOURCE = "package scalar\nMain :: () -> int {\n    return Twice(21);\n}\n"
             "Twice :: (x: int) -> int {\n    return x * 2;\n}\n";

TEST_CASE(native_build_links_a_program_and_reuses_it)
{
    reset("cache", SOURCE);
    REQUIRE(build(launcher, "-O0", tools) == 0);
    char program[1024], trace[1024], receipt[1024];
    REQUIRE(only("O0-", "", program));
    path(receipt, program, "receipt.txt");
    char *before = tool_read(receipt);
    REQUIRE(before && strstr(before, "output-kind executable\n"));
    snprintf(trace, sizeof trace, "\nlinker %s/ld.lld\n", tools);
    REQUIRE(strstr(before, trace));
    strcat(program, "/program");
    REQUIRE(!access(program, X_OK) && strstr(errors, program));
    path(trace, tools, "llc.argv");
    REQUIRE(!unlink(trace));
    REQUIRE(build(launcher, "-O0", tools) == 0 && strstr(errors, "is up to date"));
    REQUIRE(!exists(trace));
    char *after = tool_read(receipt);
    REQUIRE(after && !strcmp(before, after));
    free(after);
    free(before);
    REQUIRE(build(launcher, "-O2", tools) == 0 && count("O2-") == 1 && count("O0-") == 1);
    REQUIRE(count("emitter-") == 1);
    return true;
}

TEST_CASE(native_build_keeps_prior_programs_and_never_falls_back)
{
    reset("rejection", SOURCE);
    REQUIRE(build(launcher, "-O0", tools) == 0);
    char prior[1024], receipt[1024];
    REQUIRE(only("O0-", "", prior));
    path(receipt, prior, "receipt.txt");
    char *before = tool_read(receipt);
    REQUIRE(before);
    write_at(game, "Main.chibi", "package scalar\nMain :: () -> int {\n    print(\"x\");\n    return 1;\n}\n");
    REQUIRE(build(launcher, "-O0", tools) == 1);
    char expected[1100];
    snprintf(expected, sizeof expected, "%s/Main.chibi:3:5: Unknown function 'print'", game);
    REQUIRE(strstr(errors, expected));
    char *after = tool_read(receipt);
    bool kept = after && !strcmp(before, after);
    free(after);
    free(before);
    REQUIRE(kept && count("O0-") == 1);
    char state[1024];
    path(state, game, ".chibi");
    REQUIRE(!exists(state) && !only("O0-", ".work", state) && !only("O0-", ".partial", state));
    write_at(game, "Main.chibi", "package scalar\nMain :: () -> int {\n    return 7;\n}\n");
    REQUIRE(build(launcher, "-O0", tools) == 0 && count("O0-") == 2);
    path(receipt, prior, "receipt.txt");
    REQUIRE(exists(receipt));
    return true;
}

TEST_CASE(native_build_rejects_missing_and_failing_tools)
{
    reset("tools", SOURCE);
    char linker[1024], moved[1024], scratch[1024];
    path(linker, tools, "ld.lld");
    path(moved, tools, "linker");
    REQUIRE(!rename(linker, moved));
    REQUIRE(
        build(launcher, "-O0", tools) == 2 &&
        strstr(errors, "must hold LLVM's opt, llc and ld.lld")
    );
    path(scratch, game, "build");
    REQUIRE(!exists(scratch));
    REQUIRE(!rename(moved, linker));
    write_at(tools, "action", "fail\n");
    REQUIRE(build(launcher, "-O0", tools) == 71 && strstr(errors, "LLVM object emission failed"));
    REQUIRE(count("O0-") == 0);
    write_at(tools, "action", "ok\n");
    REQUIRE(build(launcher, "-O0", tools) == 0 && count("O0-") == 1);
    chibi_process_output out = { .data = output, .capacity = sizeof output };
    chibi_process_output err = { .data = errors, .capacity = sizeof errors };
    const char *argv[] = { launcher, "build", "--native", game, NULL };
    const char *envp[] = { "PATH=", NULL };
    status = chibi_process_capture(launcher, argv, suite, envp, chibi_process_now() + 30, NULL, &out, &err);
    REQUIRE(status.error == CHIBI_PROCESS_OK && status.status == 2);
    REQUIRE(strstr(errors, "build --native needs LLVM"));
    return true;
}

TEST_CASE(native_build_refuses_what_an_interrupted_build_left)
{
    reset("interrupted", SOURCE);
    write_at(tools, "action", "terminate\n");
    REQUIRE(build(launcher, "-O0", tools) != 0 && strstr(errors, "build process cancelled"));
    char work[1024], partial[1024], object[1024];
    REQUIRE(only("O0-", ".work", work) && only("O0-", ".partial", partial));
    REQUIRE(count("O0-") == 2);
    path(object, partial, "object.o");
    char *before = tool_read(object);
    REQUIRE(before && !strcmp(before, "controlled object\n"));
    free(before);
    write_at(tools, "action", "ok\n");
    REQUIRE(build(launcher, "-O0", tools) == 1 && strstr(errors, "an interrupted build left"));
    char *after = tool_read(object);
    REQUIRE(after && !strcmp(after, "controlled object\n") && count("O0-") == 2);
    free(after);
    tool_remove_tree(work);
    tool_remove_tree(partial);
    REQUIRE(build(launcher, "-O0", tools) == 0 && count("O0-") == 1);
    return true;
}

/* A folder the key names but that lacks a valid program is refused and kept,
 * never reported up to date or overwritten. */
TEST_CASE(native_build_refuses_an_incomplete_output)
{
    reset("incomplete", SOURCE);
    REQUIRE(build(launcher, "-O0", tools) == 0);
    char output_folder[1024], program[1024], receipt[1024];
    REQUIRE(only("O0-", "", output_folder));
    path(program, output_folder, "program");
    path(receipt, output_folder, "receipt.txt");
    REQUIRE(!chmod(program, 0600));
    REQUIRE(
        build(launcher, "-O0", tools) == 1 &&
        strstr(errors, "has no executable program; remove it")
    );
    REQUIRE(!chmod(program, 0700));
    char *bytes = tool_read(program);
    REQUIRE(bytes);
    tool_write(program, "#!/bin/sh\nexit 7\n");
    REQUIRE(
        build(launcher, "-O0", tools) == 1 &&
        strstr(errors, "does not match its receipt (program-sha256)")
    );
    tool_write(program, bytes);
    free(bytes);
    REQUIRE(!chmod(program, 0700));
    REQUIRE(build(launcher, "-O0", tools) == 0 && strstr(errors, "is up to date"));
    REQUIRE(!unlink(receipt) && !unlink(program));
    REQUIRE(build(launcher, "-O0", tools) == 1 && strstr(errors, "has no receipt"));
    REQUIRE(count("O0-") == 1 && exists(output_folder));
    tool_remove_tree(output_folder);
    REQUIRE(build(launcher, "-O0", tools) == 0 && count("O0-") == 1);
    return true;
}

/* The build tree's launcher finds its sibling hosts; real LLVM tools link a
 * program that runs, with no C compiler on an empty PATH. */
TEST_CASE(native_build_runs_with_llvm_and_no_c_compiler)
{
    const char *bin = getenv("CHIBI_LLVM_BIN");
    if (!bin || !*bin) {
        fprintf(stderr, "native build: set CHIBI_LLVM_BIN to link with real LLVM tools\n");
        return true;
    }
    reset("llvm", SOURCE);
    const char *names[] = { "opt", "llc", "ld.lld" };
    for (int i = 0; i < 3; i++) {
        char tool[1024], real[1024];
        path(tool, tools, names[i]);
        path(real, bin, names[i]);
        REQUIRE(!unlink(tool) && !symlink(real, tool));
    }
    char in_place[1024];
    path(in_place, built, "sdk/chibi");
    for (int i = 0; i < 2; i++) {
        REQUIRE(build(in_place, i ? "-O2" : "-O0", tools) == 0);
        char program[1024];
        REQUIRE(only(i ? "O2-" : "O0-", "", program));
        strcat(program, "/program");
        const char *argv[] = { program, NULL };
        const char *envp[] = { NULL };
        chibi_process_output out = { 0 }, err = { 0 };
        chibi_process_capture_result ran = chibi_process_capture(
            program,
            argv,
            game,
            envp,
            chibi_process_now() + 10,
            NULL,
            &out,
            &err
        );
        REQUIRE(ran.error == CHIBI_PROCESS_OK && ran.status == 42);
    }
    return true;
}

#endif

void test_native_build(const char *binary)
{
#ifdef __linux__
    strcpy(suite, "/tmp/chibi-native-build-XXXXXX");
    assert(mkdtemp(suite));
    char *resolved = realpath(suite, NULL);
    assert(resolved && strlen(resolved) < sizeof suite);
    strcpy(suite, resolved);
    free(resolved);
    stage_tool = tool_read(CHIBI_SOURCE_ROOT "/test/native_aot/driver/fixtures/stage-tool.sh");
    assert(stage_tool);
    relocate(binary);
    RUN_TEST_CASE(native_build_links_a_program_and_reuses_it);
    RUN_TEST_CASE(native_build_keeps_prior_programs_and_never_falls_back);
    RUN_TEST_CASE(native_build_rejects_missing_and_failing_tools);
    RUN_TEST_CASE(native_build_refuses_what_an_interrupted_build_left);
    RUN_TEST_CASE(native_build_refuses_an_incomplete_output);
    RUN_TEST_CASE(native_build_runs_with_llvm_and_no_c_compiler);
    free(stage_tool);
    tool_remove_tree(suite);
#else
    (void)binary;
    fprintf(stderr, "native build: six Linux cases unexecuted on this platform\n");
#endif
}
