#include "assets/project.h"
#include "host/process.h"
#include "support/xalloc.h"
#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#if defined(__linux__) && defined(__x86_64__)
/* `chibi build --native` through a relocated SDK and the build tree's own
 * launcher. Controlled LLVM stand-ins (the driver's stage-tool.sh) cover the
 * process contract; CHIBI_LLVM_BIN's real tools cover the program. */
static char suite[256], built[1024], sdk[1024], launcher[1024], game[1024],
    tools[1024], output[65536], errors[65536];
static char *stage_tool;
/* The whole selector's deadline, in chibi_process_now() seconds. Every build
 * may run until then, so contention between parallel cases cannot fail one
 * case on its own clock. */
static double deadline;
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
    const char *shared[] = { "compiler", "core", "tools/native_aot", "apps" };
    for (size_t i = 0; i < sizeof shared / sizeof *shared; i++) {
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

enum
{
    MAX_OPTIONS = 4
};

/* Runs `chibi build --native` with `options`, up to MAX_OPTIONS and ended by
 * NULL, an empty PATH and no CHIBI_LLVM_BIN. */
static int build_with(const char *program, const char *const *options, const char *llvm)
{
    chibi_process_output out = { .data = output, .capacity = sizeof output };
    chibi_process_output err = { .data = errors, .capacity = sizeof errors };
    const char *argv[MAX_OPTIONS + 7] = { program, "build", "--native" };
    int at = 3;
    for (int i = 0; options[i]; i++) {
        assert(i < MAX_OPTIONS);
        argv[at++] = options[i];
    }
    argv[at++] = "--llvm";
    argv[at++] = llvm;
    argv[at] = game;
    /* The build host recovers the compiler from source when its sources differ
     * from the seed's, which takes minutes; the CI cache holds that recovery. */
    char cache[1100] = "";
    const char *configured = getenv("CHIBI_COMPILER_CACHE");
    if (configured && *configured) {
        snprintf(cache, sizeof cache, "CHIBI_COMPILER_CACHE=%s", configured);
    }
    const char *envp[] = { "PATH=", cache[0] ? cache : NULL, NULL };
    time_t began = time(NULL);
    status = chibi_process_capture(program, argv, suite, envp, deadline, NULL, &out, &err);
    fprintf(
        stderr,
        "native build: %s took %ds (error %d, status %d)\n",
        game,
        (int)(time(NULL) - began),
        (int)status.error,
        status.status
    );
    /* A failed build, expected or not, leaves the child's stderr in the log,
     * so a CI failure says why without a rerun. */
    if (status.error != CHIBI_PROCESS_OK || status.status) {
        fprintf(stderr, "native build: stderr:\n%s\n", errors);
    }
    if (status.error != CHIBI_PROCESS_OK) {
        return -1;
    }
    return status.status;
}

static int build(const char *program, const char *mode, const char *llvm)
{
    const char *options[] = { mode, NULL };
    return build_with(program, options, llvm);
}

/* The program path the launcher printed last, as "native: PATH". */
static bool printed(char program[1024])
{
    const char *line = strstr(errors, "native: /");
    if (!line) {
        return false;
    }
    line += strlen("native: ");
    size_t length = strcspn(line, "\n");
    if (length >= 1024) {
        return false;
    }
    memcpy(program, line, length);
    program[length] = 0;
    return true;
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
    REQUIRE(strstr(before, "\nlinkage dynamic "));
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

/* A program links libc dynamically by default, as Odin's does, and --static
 * keeps the no-CRT static link as an explicit option; the receipt says which. */
TEST_CASE(native_build_links_statically_on_request)
{
    reset("static", SOURCE);
    const char *options[] = { "-O0", "--static", NULL };
    REQUIRE(build_with(launcher, options, tools) == 0);
    char program[1024], receipt[1024];
    REQUIRE(only("O0-", "", program));
    path(receipt, program, "receipt.txt");
    char *text = tool_read(receipt);
    REQUIRE(text && strstr(text, "\nlinkage static\n"));
    free(text);
    return true;
}

/* The IR of the one O0 output whose source.ll does (`lines`) or does not
 * hold line tables, or NULL. */
static char *module_ir(bool lines)
{
    char folder[1024], source[1024];
    path(folder, game, "build/native");
    DIR *listing = opendir(folder);
    char *found = NULL;
    for (struct dirent *entry; listing && (entry = readdir(listing));) {
        if (strncmp(entry->d_name, "O0-", 3) || strchr(entry->d_name, '.')) {
            continue;
        }
        snprintf(source, sizeof source, "%s/%s/source.ll", folder, entry->d_name);
        char *text = tool_read(source);
        if (text && !!strstr(text, "!dbg") == lines && !found) {
            found = text;
            continue;
        }
        free(text);
    }
    if (listing) {
        closedir(listing);
    }
    return found;
}

/* Whether the IR of some O0 output holds `text`. */
static bool any_ir(const char *text)
{
    char folder[1024], source[1024];
    path(folder, game, "build/native");
    DIR *listing = opendir(folder);
    bool found = false;
    for (struct dirent *entry; listing && (entry = readdir(listing)) && !found;) {
        if (strncmp(entry->d_name, "O0-", 3) || strchr(entry->d_name, '.')) {
            continue;
        }
        snprintf(source, sizeof source, "%s/%s/source.ll", folder, entry->d_name);
        char *ir = tool_read(source);
        found = ir && strstr(ir, text);
        free(ir);
    }
    if (listing) {
        closedir(listing);
    }
    return found;
}

/* -g emits line tables, as rustc's -C debuginfo=line-tables-only: each
 * function a subprogram under its source name, each statement at its line and
 * column, the file in the game's folder. It is an output of its own, keyed on
 * that folder, so moving the game rebuilds it and keeps the plain program. */
TEST_CASE(native_build_adds_line_tables_with_g)
{
    reset("lines", SOURCE);
    REQUIRE(build(launcher, "-O0", tools) == 0);
    const char *lines[] = { "-O0", "-g", NULL };
    REQUIRE(build_with(launcher, lines, tools) == 0 && count("O0-") == 2);
    REQUIRE(build_with(launcher, lines, tools) == 0 && strstr(errors, "is up to date"));
    char *plain = module_ir(false), *ir = module_ir(true);
    REQUIRE(plain && !strstr(plain, "!llvm") && ir);
    char file[1200];
    snprintf(file, sizeof file, "!1 = !DIFile(filename: \"Main.chibi\", directory: \"%s\")\n", game);
    static const char *const expected[] = {
        "define i32 @chibi_scalar() #0 !dbg !6 {",
        "emissionKind: LineTablesOnly",
        "!6 = distinct !DISubprogram(name: \"Main\", linkageName: "
        "\"chibi_scalar\", scope: !1, file: !1, line: 2,",
        "!7 = distinct !DISubprogram(name: \"Twice\", linkageName: "
        "\"chibi_scalar.1\", scope: !1, file: !1, "
        "line: 5,",
        "!DILocation(line: 3, column: 5, scope: !6)",
        "!DILocation(line: 6, column: 5, scope: !7)",
    };
    bool placed = strstr(ir, file);
    for (size_t i = 0; i < sizeof expected / sizeof *expected; i++) {
        placed = placed && strstr(ir, expected[i]);
    }
    free(ir);
    free(plain);
    REQUIRE(placed);
    char moved[1024];
    snprintf(moved, sizeof moved, "%s moved", game);
    REQUIRE(!rename(game, moved));
    strcpy(game, moved);
    snprintf(file, sizeof file, "directory: \"%s\")", game);
    REQUIRE(build(launcher, "-O0", tools) == 0 && strstr(errors, "is up to date"));
    REQUIRE(build_with(launcher, lines, tools) == 0 && count("O0-") == 3 && any_ir(file));
    const char *twice[] = { "-g", "-g", NULL };
    REQUIRE(
        build_with(launcher, twice, tools) == 2 &&
        strstr(errors, "Invalid build option; use --help.")
    );
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
    /* A native library has no entry to link a program from. */
    write_at(game, "Main.chibi", "Tools :: library {\n    Seven :: () -> int {\n        return 7;\n    }\n}\n");
    REQUIRE(build(launcher, "-O0", tools) == 1);
    snprintf(expected, sizeof expected, "%s/Main.chibi:a native library has no entry", game);
    REQUIRE(strstr(errors, expected));
    /* Formatting calls the runtime, which only the VM supplies. */
    write_at(game, "Main.chibi", "package scalar\nMain :: () -> int {\n    return text_byte_length(format_text(\"{}\", i64(7)));\n}\n");
    REQUIRE(build(launcher, "-O0", tools) == 1);
    snprintf(expected, sizeof expected, "%s/Main.chibi:a native program cannot call the runtime", game);
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

/* Whether `name` in `folder` holds `text`. */
static bool holds(const char *folder, const char *name, const char *text)
{
    char file[1024];
    path(file, folder, name);
    char *read = tool_read(file);
    bool found = read && strstr(read, text);
    free(read);
    return found;
}

/* Whether the trace `name` in the tools folder is exactly `expected`. */
static bool traced(const char *name, const char *expected)
{
    char file[1024];
    path(file, tools, name);
    char *read = tool_read(file);
    bool same = read && !strcmp(read, expected);
    free(read);
    return same;
}

/* `--target` links a macOS arm64 program with ld64.lld against a libSystem
 * stub, with dyld calling `main`, and a Windows x64 program.exe with lld-link
 * and no C runtime, through an import library for kernel32's ExitProcess.
 * The target is part of the key, and an unknown target or Windows line
 * tables are rejected. */
TEST_CASE(native_build_links_macos_and_windows_programs)
{
    reset("targets", SOURCE);
    const char *mac[] = { "-O0", "--target", "aarch64-apple-darwin", NULL };
    REQUIRE(build_with(launcher, mac, tools) == 0);
    char program[1024], folder[1024], expected[8192];
    REQUIRE(printed(program) && only("O0-", "", folder));
    snprintf(expected, sizeof expected, "%s/program", folder);
    REQUIRE(!strcmp(program, expected));
    snprintf(
        expected,
        sizeof expected,
        "-flavor\ndarwin\n-arch\narm64\n-platform_version\nmacos\n11.0.0\n11.0."
        "0\n-dead_strip\n-e\n_main\n"
        "%s.partial/object.o\n%s.partial/start.o\n%s.partial/"
        "libSystem.tbd\n-o\n%s.partial/program\n",
        folder,
        folder,
        folder,
        folder
    );
    REQUIRE(traced("ld.lld.argv", expected));
    REQUIRE(holds(folder, "libSystem.tbd", "install-name: '/usr/lib/libSystem.B.dylib'\n"));
    REQUIRE(holds(folder, "start.ll", "target triple = \"arm64-apple-macosx11.0.0\""));
    REQUIRE(holds(folder, "start.ll", "define i32 @main() {\n  %status = call i32 @chibi_scalar()"));
    REQUIRE(holds(folder, "source.ll", "target triple = \"arm64-apple-macosx11.0.0\""));
    REQUIRE(build_with(launcher, mac, tools) == 0 && strstr(errors, "/program is up to date"));

    const char *windows[] = { "-O0", "--target", "x86_64-pc-windows-msvc", NULL };
    REQUIRE(build_with(launcher, windows, tools) == 0 && count("O0-") == 2);
    REQUIRE(printed(program) && strlen(program) > strlen("/program.exe"));
    size_t length = strlen(program) - strlen("/program.exe");
    REQUIRE(!strcmp(program + length, "/program.exe"));
    memcpy(folder, program, length);
    folder[length] = 0;
    snprintf(
        expected,
        sizeof expected,
        "-flavor\nlink\n/lib\n/nologo\n/def:%s.partial/kernel32.def\n/"
        "machine:x64\n/out:%s.partial/kernel32.lib\n",
        folder,
        folder
    );
    REQUIRE(traced("ld.lld.lib.argv", expected));
    snprintf(
        expected,
        sizeof expected,
        "-flavor\nlink\n/nologo\n/subsystem:console\n/entry:mainCRTStartup\n/"
        "nodefaultlib\n/opt:ref\n"
        "/out:%s.partial/program.exe\n%s.partial/object.o\n%s.partial/"
        "start.o\n%s.partial/kernel32.lib\n",
        folder,
        folder,
        folder,
        folder
    );
    REQUIRE(traced("ld.lld.argv", expected));
    REQUIRE(holds(folder, "kernel32.def", "LIBRARY kernel32.dll\nEXPORTS\n    ExitProcess\n"));
    REQUIRE(holds(folder, "start.ll", "call void @ExitProcess(i32 %status)"));
    REQUIRE(holds(folder, "start.ll", "module asm \"__chkstk:\""));
    REQUIRE(holds(folder, "start.ll", "@_fltused = dso_local global i32 0"));
    REQUIRE(holds(folder, "source.ll", "\"probe-stack\"=\"__chkstk\""));
    REQUIRE(holds(folder, "receipt.txt", "\nstage import-library\n"));
    REQUIRE(build_with(launcher, windows, tools) == 0 && strstr(errors, "/program.exe is up to date"));

    const char *unknown[] = { "--target", "riscv64gc-unknown-linux-gnu", NULL };
    REQUIRE(build_with(launcher, unknown, tools) == 2);
    REQUIRE(strstr(errors, "unknown target riscv64gc-unknown-linux-gnu; use "));
    const char *lines[] = { "-g", "--target", "x86_64-pc-windows-msvc", NULL };
    REQUIRE(build_with(launcher, lines, tools) == 2);
    REQUIRE(strstr(errors, "line tables are Linux x86-64 only"));
    const char *twice[] = { "--target",
                            "aarch64-apple-darwin",
                            "--target",
                            "aarch64-apple-darwin",
                            NULL };
    REQUIRE(
        build_with(launcher, twice, tools) == 2 &&
        strstr(errors, "Invalid build option; use --help.")
    );
    REQUIRE(count("O0-") == 2);
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

/* Replaces the controlled tools with links to the LLVM tools in `bin`. */
static void real_tools(const char *bin)
{
    const char *names[] = { "opt", "llc", "ld.lld" };
    for (int i = 0; i < 3; i++) {
        char tool[1024], real[1024];
        path(tool, tools, names[i]);
        path(real, bin, names[i]);
        assert(!unlink(tool) && !symlink(real, tool));
    }
}

/* `llvm-symbolizer` in `bin` places `symbol` of `program` at `expected`. */
static bool symbolized(const char *bin, const char *program, const char *symbol, const char *expected)
{
    char symbolizer[1024], object[1100];
    path(symbolizer, bin, "llvm-symbolizer");
    snprintf(object, sizeof object, "--obj=%s", program);
    const char *argv[] = { symbolizer, object, symbol, NULL };
    const char *envp[] = { NULL };
    chibi_process_output out = { .data = output, .capacity = sizeof output },
                         err = { 0 };
    chibi_process_capture_result ran = chibi_process_capture(
        symbolizer,
        argv,
        game,
        envp,
        chibi_process_now() + 30,
        NULL,
        &out,
        &err
    );
    return ran.error == CHIBI_PROCESS_OK && ran.status == 0 &&
           strstr(output, expected);
}

/* The build tree's launcher finds its sibling hosts; real LLVM tools link a
 * program that runs, with no C compiler on an empty PATH. With -g, its line
 * tables place its functions in the source at O0 and O2. */
TEST_CASE(native_build_runs_with_llvm_and_no_c_compiler)
{
    const char *bin = getenv("CHIBI_LLVM_BIN");
    if (!bin || !*bin) {
        fprintf(stderr, "native build: set CHIBI_LLVM_BIN to link with real LLVM tools\n");
        return true;
    }
    reset("llvm", SOURCE);
    real_tools(bin);
    char in_place[1024];
    path(in_place, built, "sdk/chibi");
    for (int i = 0; i < 4; i++) {
        const char *options[] = { i % 2 ? "-O2" : "-O0", i >= 2 ? "-g" : NULL, NULL };
        REQUIRE(build_with(in_place, options, tools) == 0);
        char program[1024];
        REQUIRE(printed(program));
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
        if (i < 2) {
            continue;
        }
        char source[1100], main_line[1200], twice_line[1200];
        snprintf(source, sizeof source, "%s/Main.chibi:", game);
        snprintf(main_line, sizeof main_line, "%s2:0\n", source);
        snprintf(twice_line, sizeof twice_line, "%s5:0\n", source);
        /* O2 inlines Twice, so Main is all that is left to place. */
        REQUIRE(symbolized(bin, program, "chibi_scalar", i == 2 ? main_line : source));
        REQUIRE(i == 3 || symbolized(bin, program, "chibi_scalar.1", twice_line));
    }
    return true;
}

static const char *const FOREIGN_SOURCE =
    "package scalar\n"
    "foreign import probe \"libprobe.a\"\n"
    "foreign probe {\n"
    "    chibi_probe_add :: (a: i32, b: i32) -> i32\n"
    "    chibi_probe_pick :: (n: i32, flag: bool) -> bool\n"
    "    chibi_probe_scale :: (value: f64, by: f64) -> f64\n"
    "}\n"
    "Main :: () -> int {\n"
    "    total := chibi_probe_add(40, 2);\n"
    "    if (chibi_probe_pick(5, true)) { total = total + 1; }\n"
    "    if (chibi_probe_pick(2, true)) { total = total + 100; }\n"
    "    return total + int(chibi_probe_scale(0.5, 2.0));\n"
    "}\n";

/* A program links the prebuilt archive its source imports: the driver finds it
 * beside the source, adds it to the link line and records its hash. The
 * archive is vendored: nothing here compiles its C. A missing archive fails
 * the build and names it. */
TEST_CASE(native_build_links_a_vendored_archive)
{
    const char *bin = getenv("CHIBI_LLVM_BIN");
    if (!bin || !*bin) {
        fprintf(stderr, "native build: set CHIBI_LLVM_BIN to link with real LLVM tools\n");
        return true;
    }
    char in_place[1024], vendored[1024], copied[1024];
    path(in_place, built, "sdk/chibi");
    path(vendored, CHIBI_SOURCE_ROOT, "test/native_aot/foreign/libprobe.a");
    const char *options[] = { "-O0", NULL };
    reset("foreign-missing", FOREIGN_SOURCE);
    real_tools(bin);
    REQUIRE(build_with(in_place, options, tools) != 0 && strstr(errors, "libprobe.a is not a file"));
    reset("foreign", FOREIGN_SOURCE);
    real_tools(bin);
    path(copied, game, "libprobe.a");
    copy(vendored, copied);
    REQUIRE(build_with(in_place, options, tools) == 0);
    char program[1024], receipt[1024];
    REQUIRE(printed(program));
    snprintf(receipt, sizeof receipt, "%.*s/receipt.txt", (int)(strrchr(program, '/') - program), program);
    char *recorded = tool_read(receipt);
    REQUIRE(recorded && strstr(recorded, "\nlibrary-sha256 "));
    free(recorded);
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
    REQUIRE(ran.error == CHIBI_PROCESS_OK && ran.status == 44);
    return true;
}

static const char *const
    PROC_SOURCE = "package scalar\n"
                  "foreign import libc \"system:c\"\n"
                  "foreign libc {\n"
                  "    dlsym :: (handle: rawptr, name: cstring) -> rawptr\n"
                  "}\n"
                  "Main :: () -> int {\n"
                  "    absolute: fn(n: i32) -> i32 = dlsym(0, \"abs\");\n"
                  "    return absolute(-42) + 1;\n"
                  "}\n";

static const char *const
    CALLBACK_SOURCE = "package scalar\n"
                      "foreign import libc \"system:c\"\n"
                      "foreign libc {\n"
                      "    thrd_create :: (thread: u8[], start: fn(argument: "
                      "u64) -> i32, argument: u64) -> i32\n"
                      "    thrd_join :: (thread: u64, result: u8[]) -> i32\n"
                      "}\n"
                      "Main :: () -> int {\n"
                      "    thread: u8[] = [0, 0, 0, 0, 0, 0, 0, 0];\n"
                      "    result: u8[] = [0, 0, 0, 0];\n"
                      "    started := thrd_create(thread, Worker, 41);\n"
                      "    id: u64 = u64(thread[0]) | (u64(thread[1]) << 8) | "
                      "(u64(thread[2]) << 16) | (u64(thread[3]) << 24) |\n"
                      "              (u64(thread[4]) << 32) | (u64(thread[5]) "
                      "<< 40) | (u64(thread[6]) << 48) | (u64(thread[7]) << "
                      "56);\n"
                      "    joined := thrd_join(id, result);\n"
                      "    return int(result[0]) + started + joined;\n"
                      "}\n"
                      "Worker :: (argument: u64) -> i32 {\n"
                      "    return i32(argument) + 1;\n"
                      "}\n";

/* C calls an authored function (Main is first: the scalar proof reads the
 * first function): thrd_create starts a thread that runs Worker
 * through the adapter the compiler makes, thrd_join collects its result. */
TEST_CASE(native_build_gives_c_an_authored_function)
{
    const char *bin = getenv("CHIBI_LLVM_BIN");
    if (!bin || !*bin) {
        fprintf(stderr, "native build: set CHIBI_LLVM_BIN to link with real LLVM tools\n");
        return true;
    }
    char in_place[1024];
    path(in_place, built, "sdk/chibi");
    const char *options[] = { "-O0", NULL };
    reset("callback", CALLBACK_SOURCE);
    real_tools(bin);
    REQUIRE(build_with(in_place, options, tools) == 0);
    char program[1024];
    REQUIRE(printed(program));
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
    return true;
}

/* A program calls a C function through an address: dlsym finds libc's abs, a
 * local of function type holds it, and the call goes through the address. libc
 * is linked dynamically by default, which is where dlsym lives. */
TEST_CASE(native_build_calls_through_a_function_pointer)
{
    const char *bin = getenv("CHIBI_LLVM_BIN");
    if (!bin || !*bin) {
        fprintf(stderr, "native build: set CHIBI_LLVM_BIN to link with real LLVM tools\n");
        return true;
    }
    char in_place[1024];
    path(in_place, built, "sdk/chibi");
    const char *options[] = { "-O0", NULL };
    reset("procedure", PROC_SOURCE);
    real_tools(bin);
    REQUIRE(build_with(in_place, options, tools) == 0);
    char program[1024];
    REQUIRE(printed(program));
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
    REQUIRE(ran.error == CHIBI_PROCESS_OK && ran.status == 43);
    return true;
}

static const char *const BYTES_SOURCE = "package scalar\n"
                                        "foreign import libc \"system:c\"\n"
                                        "foreign libc {\n"
                                        "    memset :: (buffer: u8[], value: "
                                        "i32, count: u64) -> rawptr\n"
                                        "}\n"
                                        "Main :: () -> int {\n"
                                        "    buffer: u8[] = [1, 2, 3, 4];\n"
                                        "    done := memset(buffer, 7, 3);\n"
                                        "    return int(buffer[0]) + "
                                        "int(buffer[2]) + int(buffer[3]);\n"
                                        "}\n";

/* An array of bytes passes to C as the pointer to its elements, and the callee
 * writes through it: libc's memset fills the first three of four bytes. */
TEST_CASE(native_build_passes_a_byte_array_to_c)
{
    const char *bin = getenv("CHIBI_LLVM_BIN");
    if (!bin || !*bin) {
        fprintf(stderr, "native build: set CHIBI_LLVM_BIN to link with real LLVM tools\n");
        return true;
    }
    char in_place[1024];
    path(in_place, built, "sdk/chibi");
    const char *options[] = { "-O0", NULL };
    reset("bytes", BYTES_SOURCE);
    real_tools(bin);
    REQUIRE(build_with(in_place, options, tools) == 0);
    char program[1024];
    REQUIRE(printed(program));
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
    REQUIRE(ran.error == CHIBI_PROCESS_OK && ran.status == 18);
    return true;
}

static const char *const ASSERT_SOURCE = "package scalar\n"
                                         "Main :: () -> int {\n"
                                         "    assert(true, \"fine\");\n"
                                         "    assert(false, \"broken\");\n"
                                         "    return 7;\n"
                                         "}\n";

/* A failed assert ends the program early: main reports where on stderr and
 * exits with status 134, and a passing assert changes nothing. */
TEST_CASE(native_build_reports_a_failed_assert)
{
    const char *bin = getenv("CHIBI_LLVM_BIN");
    if (!bin || !*bin) {
        fprintf(stderr, "native build: set CHIBI_LLVM_BIN to link with real LLVM tools\n");
        return true;
    }
    char in_place[1024];
    path(in_place, built, "sdk/chibi");
    const char *options[] = { "-O0", NULL };
    for (int failing = 0; failing < 2; failing++) {
        reset(failing ? "assert-fails" : "assert-holds", ASSERT_SOURCE);
        if (!failing) {
            write_at(game, "Main.chibi", "package scalar\nMain :: () -> int {\n    assert(true, \"fine\");\n    return 7;\n}\n");
        }
        real_tools(bin);
        REQUIRE(build_with(in_place, options, tools) == 0);
        char program[1024], report[512] = "";
        REQUIRE(printed(program));
        const char *argv[] = { program, NULL };
        const char *envp[] = { NULL };
        chibi_process_output out = { 0 },
                             err = { .data = report, .capacity = sizeof report };
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
        REQUIRE(ran.error == CHIBI_PROCESS_OK && ran.status == (failing ? 134 : 7));
        REQUIRE(!failing || strstr(report, "assertion failed in function"));
    }
    return true;
}

/* A sample's test source as a native program takes it: the @test mark goes,
 * and Calls returns 0, since an authored native function has a result. */
static char *as_program(const char *source)
{
    size_t size = strlen(source);
    char *kept = xmalloc(size + 1);
    size_t at = 0;
    for (const char *line = source; *line;) {
        const char *end = strchr(line, '\n');
        size_t length = end ? (size_t)(end - line) + 1 : strlen(line);
        bool mark = length >= 5 && !strncmp(line, "@test", 5) &&
                    (length == 5 || line[5] == '\n');
        if (!mark) {
            memcpy(kept + at, line, length);
            at += length;
        }
        line += length;
    }
    kept[at] = 0;
    const char *head = strstr(kept, "Calls :: () {");
    char *last = strrchr(kept, '}');
    if (!head || !last) {
        return kept;
    }
    char *program = xmalloc(size + 64);
    snprintf(
        program,
        size + 64,
        "%.*sCalls :: () -> int {%.*s    return 0;\n}\n",
        (int)(head - kept),
        kept,
        (int)(last - (head + strlen("Calls :: () {"))),
        head + strlen("Calls :: () {")
    );
    free(kept);
    return program;
}

/* Copies a sample's package folders into a project, each source as a program. */
static bool copy_sample(const char *from, const char *to)
{
    DIR *directory = opendir(from);
    REQUIRE(directory);
    struct dirent *entry;
    while ((entry = readdir(directory))) {
        if (entry->d_name[0] == '.') {
            continue;
        }
        char source[1100], target[1100];
        path(source, from, entry->d_name);
        path(target, to, entry->d_name);
        struct stat info;
        REQUIRE(!stat(source, &info));
        if (S_ISDIR(info.st_mode)) {
            REQUIRE(!mkdir(target, 0700) && copy_sample(source, target));
            continue;
        }
        char *text = tool_read(source);
        REQUIRE(text);
        char *program = as_program(text);
        write_at(to, entry->d_name, program);
        free(program);
        free(text);
    }
    closedir(directory);
    return true;
}

/* The foreign samples, which the VM skips as native only, run natively: each
 * becomes a project whose Main calls the sample's Calls, builds with
 * `chibi build --native` and runs, and its asserts hold when the program exits
 * 0. libc is the foreign library. */
TEST_CASE(native_build_runs_the_foreign_samples)
{
    const char *bin = getenv("CHIBI_LLVM_BIN");
    if (!bin || !*bin) {
        fprintf(stderr, "native build: set CHIBI_LLVM_BIN to link with real LLVM tools\n");
        return true;
    }
    static const char *const SAMPLES[] = { "foreign_declared",
                                           "foreign_library",
                                           "foreign_posix" };
    char in_place[1024];
    path(in_place, built, "sdk/chibi");
    const char *options[] = { "-O0", NULL };
    for (size_t i = 0; i < sizeof SAMPLES / sizeof *SAMPLES; i++) {
        char from[1100], target[1100], facts[1100], main_text[512];
        snprintf(from, sizeof from, "%s/test/compiler/samples/native/%s", CHIBI_SOURCE_ROOT, SAMPLES[i]);
        reset(SAMPLES[i], "package main\n");
        path(target, game, SAMPLES[i]);
        REQUIRE(!mkdir(target, 0700) && copy_sample(from, target));
        path(facts, target, "facts");
        write_at(facts, "Native.chibi", "package facts\npub Native :: () -> int {\n    return Calls();\n}\n");
        snprintf(
            main_text,
            sizeof main_text,
            "package main\nimport facts \"project:%s/facts\"\nMain :: () -> "
            "int {\n    return facts.Native();\n}\n",
            SAMPLES[i]
        );
        write_at(game, "Main.chibi", main_text);
        real_tools(bin);
        REQUIRE(build_with(in_place, options, tools) == 0);
        char program[1024];
        REQUIRE(printed(program));
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
        REQUIRE(ran.error == CHIBI_PROCESS_OK && ran.status == 0);
    }
    return true;
}

static const char *const LOOP_FIRST = "Loop :: library {\n"
                                      "    Main :: () -> int { return 0; }\n"
                                      "    Frame :: () -> int {\n"
                                      "        count: retain int = 0;\n"
                                      "        count += 1;\n"
                                      "        return 0;\n"
                                      "    }\n"
                                      "}\n",
                         LOOP_EDITED = "Loop :: library {\n"
                                       "    Main :: () -> int { return 9; }\n"
                                       "    Frame :: () -> int {\n"
                                       "        count: retain int = 0;\n"
                                       "        flag: retain int = 7;\n"
                                       "        count += 1;\n"
                                       "        if count < 3 { return 3; }\n"
                                       "        return flag;\n"
                                       "    }\n"
                                       "}\n";

/* Builds the library `source` as a generation and copies it to `destination`. */
static bool generation(const char *llvm, const char *name, const char *source, const char *destination)
{
    char in_place[1024], library[1024];
    path(in_place, built, "sdk/chibi");
    const char *options[] = { "-O0", "--shared", NULL };
    reset(name, source);
    real_tools(llvm);
    REQUIRE(build_with(in_place, options, tools) == 0 && printed(library));
    const char *copying[] = { "/bin/cp", library, destination, NULL };
    char log[1024];
    path(log, suite, "copy.log");
    REQUIRE(!tool_run(copying, suite, log));
    return true;
}

/* Edit and Continue on native code, end to end: a loop host written in Chibi
 * and built natively runs the first generation of a library, Frame after
 * Frame. The library is edited and rebuilt while the host runs, and published
 * as next.so; the host swaps it in at its next Frame. The edited Frame returns
 * the exit status 7 only when the count it had survived (count < 3 gives 3, a
 * restarted count) and its new state started at its initializer, and the
 * edited Main, which would return 9, never runs. */
TEST_CASE(native_build_swaps_a_running_loop_keeping_state)
{
    const char *bin = getenv("CHIBI_LLVM_BIN");
    if (!bin || !*bin) {
        fprintf(stderr, "native build: set CHIBI_LLVM_BIN to link with real LLVM tools\n");
        return true;
    }
    char in_place[1024], host[1024], run[1024], live[1024], pending[1024],
        next[1024], source[1024];
    path(in_place, built, "sdk/chibi");
    const char *options[] = { "-O0", NULL };
    reset("watch-host", "package host\n");
    path(source, CHIBI_SOURCE_ROOT, "apps/host/Host.chibi");
    char main_file[1024], log[1024];
    path(main_file, game, "Main.chibi");
    path(log, suite, "copy.log");
    const char *installing[] = { "/bin/cp", source, main_file, NULL };
    REQUIRE(!tool_run(installing, suite, log));
    real_tools(bin);
    REQUIRE(build_with(in_place, options, tools) == 0 && printed(host));
    path(run, suite, "watch-run");
    REQUIRE(!mkdir(run, 0700));
    path(live, run, "live-0.so");
    path(pending, run, "pending.so");
    path(next, run, "next.so");
    REQUIRE(generation(bin, "watch-first", LOOP_FIRST, live));
    char host_output[4096] = "", host_errors[4096] = "";
    chibi_process_output out = { host_output, sizeof host_output, 0, false },
                         err = { host_errors, sizeof host_errors, 0, false };
    const char *argv[] = { host, NULL };
    const char *envp[] = { NULL };
    chibi_process_job job;
    chibi_process_capture_result begun = chibi_process_begin(
        &job,
        host,
        argv,
        run,
        envp,
        chibi_process_now() + 90,
        NULL,
        &out,
        &err
    );
    REQUIRE(begun.error == CHIBI_PROCESS_OK);
    /* The first generation runs on its own for a while. */
    for (int i = 0; i < 50; i++) {
        chibi_process_service(&job, 1, chibi_process_now() + 90, NULL);
    }
    REQUIRE(chibi_process_running(&job));
    REQUIRE(generation(bin, "watch-edited", LOOP_EDITED, pending));
    REQUIRE(!rename(pending, next));
    while (chibi_process_running(&job)) {
        chibi_process_service(&job, 1, chibi_process_now() + 90, NULL);
    }
    chibi_process_capture_result ended = chibi_process_end(&job);
    fprintf(
        stderr,
        "watch host: error %d status %d\n%s%s",
        (int)ended.error,
        ended.status,
        host_output,
        host_errors
    );
    REQUIRE(ended.error == CHIBI_PROCESS_OK && ended.status == 7);
    return true;
}

/* The other targets' bundle directories and program names, in the
 * driver's target order after Linux. */
static const struct
{
    const char *triple, *directory, *program;
} CROSS[] = {
    { "aarch64-apple-darwin", "macos-arm64", "program" },
    { "x86_64-pc-windows-msvc", "windows-x64", "program.exe" },
};

/* Whether `program` is CROSS[t]'s executable: a 64-bit arm64 Mach-O
 * MH_EXECUTE file, or a PE32+ x86-64 console program. */
static bool executable_for(const char *program, int t)
{
    unsigned char bytes[512] = { 0 };
    FILE *file = fopen(program, "rb");
    size_t read = file ? fread(bytes, 1, sizeof bytes, file) : 0;
    if (file) {
        fclose(file);
    }
    if (read < sizeof bytes) {
        return false;
    }
    if (!t) {
        return !memcmp(bytes, "\xcf\xfa\xed\xfe\x0c\x00\x00\x01", 8) && bytes[12] == 2;
    }
    size_t pe = (size_t)bytes[0x3c] | (size_t)bytes[0x3d] << 8;
    return !memcmp(bytes, "MZ", 2) && pe + 24 + 69 < sizeof bytes &&
           !memcmp(bytes + pe, "PE\0\0", 4) && bytes[pe + 4] == 0x64 &&
           bytes[pe + 5] == 0x86 && bytes[pe + 24] == 0x0b &&
           bytes[pe + 25] == 0x02 && bytes[pe + 24 + 68] == 3;
}

/* With CHIBI_NATIVE_BUNDLE set, copies `program` into CROSS[t]'s bundle
 * directory as `name` and lists it with its exit status 42, for
 * test/native_aot/targets/run.c to run on that target. */
static void bundle_program(const char *program, int t, const char *name)
{
    const char *bundle = getenv("CHIBI_NATIVE_BUNDLE");
    if (!bundle || !*bundle) {
        return;
    }
    char folder[1024], programs[1024], copied[1100], list[1024];
    path(folder, bundle, CROSS[t].directory);
    path(programs, folder, "programs");
    assert((!mkdir(bundle, 0700) || errno == EEXIST) && (!mkdir(folder, 0700) || errno == EEXIST));
    assert(!mkdir(programs, 0700) || errno == EEXIST);
    path(copied, programs, name);
    unlink(copied);
    copy(program, copied);
    path(list, folder, "programs.txt");
    FILE *file = fopen(list, "a");
    assert(file);
    fprintf(file, "program %s 42\n", name);
    fclose(file);
}

/* What each CROSS target's object holds when a frame's probes survive O2:
 * arm64's inline loop, `sub sp, sp, #1, lsl #12`, `cmp sp, x9` and
 * `str xzr, [sp]`, and Windows' symbol for its call to __chkstk. */
static const struct
{
    const char *bytes;
    size_t size;
} PROBES[] = { { "\xff\x07\x40\xd1\xff\x63\x29\xeb\xff\x03\x00\xf9", 12 },
               { "__chkstk", 8 } };

/* Whether the file `name` holds the text `needle`. */
static bool file_holds(const char *name, const char *needle)
{
    FILE *file = fopen(name, "rb");
    assert(file);
    static char data[1 << 20];
    size_t length = fread(data, 1, sizeof data - 1, file);
    fclose(file);
    data[length] = 0;
    for (size_t i = 0; i + strlen(needle) <= length; i++) {
        if (!memcmp(data + i, needle, strlen(needle))) {
            return true;
        }
    }
    return false;
}

/* Whether the object.o published in `out` holds `size` bytes of `bytes`. */
static bool object_holds(const char *out, const char *bytes, size_t size)
{
    char object[1024];
    path(object, out, "object.o");
    FILE *file = fopen(object, "rb");
    assert(file);
    static char data[1 << 16];
    size_t length = fread(data, 1, sizeof data, file);
    assert(feof(file));
    fclose(file);
    for (size_t i = 0; i + size <= length; i++) {
        if (!memcmp(data + i, bytes, size)) {
            return true;
        }
    }
    return false;
}

/* Links `ir` to a program in `out` through the relocated SDK's Driver.Build,
 * with the tools in the tools folder at O2. */
static bool driver_build(const char *ir, const char *out)
{
    char host[1024], share[1024], driver[1024], source[1024], core[1024],
        compiler[1024], opt[1024], llc[1024], lld[1024], collection[1100];
    path(host, sdk, "libexec/chibi/chibi-build-host");
    path(share, sdk, "share/chibi");
    path(driver, share, "tools/native_aot");
    path(source, driver, "Driver.chibi");
    snprintf(collection, sizeof collection, "native_aot=%s/driver", driver);
    path(core, share, "core");
    path(compiler, share, "compiler");
    path(opt, tools, "opt");
    path(llc, tools, "llc");
    path(lld, tools, "ld.lld");
    /* The driver compiles from source, which contention from the other cases
     * slows; it may use what remains of the selector's deadline. */
    char budget[32];
    snprintf(budget, sizeof budget, "%d", (int)((deadline - chibi_process_now()) * 1000));
    const char *argv[] = { host,       "--root",
                           driver,     "--source",
                           source,     "--collection",
                           collection, "--core",
                           core,       "--compiler",
                           compiler,   "--compiler-core",
                           core,       "--entry",
                           "Build",    "--timeout-ms",
                           budget,     "--",
                           ir,         out,
                           opt,        llc,
                           lld,        "2",
                           NULL };
    const char *envp[] = { "PATH=", NULL };
    chibi_process_output out_text = { .data = output, .capacity = sizeof output };
    chibi_process_output err = { .data = errors, .capacity = sizeof errors };
    status = chibi_process_capture(host, argv, suite, envp, deadline, NULL, &out_text, &err);
    return status.error == CHIBI_PROCESS_OK && !status.status;
}

/* `chibi run --native --watch`: the CLI builds the loop host and the first
 * generation of the source, runs the host, and when the source is saved while
 * the loop runs, rebuilds it and publishes the next generation, which the host
 * swaps in between two Frames. The edited Frame's exit status 7 proves the
 * count survived the swap and the new state started at its initializer; its
 * Main, which would return 9, never runs. */
TEST_CASE(native_build_watches_and_swaps_a_running_loop)
{
    const char *bin = getenv("CHIBI_LLVM_BIN");
    if (!bin || !*bin) {
        fprintf(stderr, "native build: set CHIBI_LLVM_BIN to link with real LLVM tools\n");
        return true;
    }
    char in_place[1024], live[1024], source[1024];
    path(in_place, built, "sdk/chibi");
    reset("native-run", LOOP_FIRST);
    real_tools(bin);
    path(live, game, "build/native/run/live-0.so");
    path(source, game, "Main.chibi");
    char cache[1100] = "";
    const char *configured = getenv("CHIBI_COMPILER_CACHE");
    if (configured && *configured) {
        snprintf(cache, sizeof cache, "CHIBI_COMPILER_CACHE=%s", configured);
    }
    const char *argv[] = { in_place,
                           "run",
                           "--native",
                           "--watch",
                           "-O0",
                           "--target",
                           "x86_64-unknown-linux-gnu",
                           "--llvm",
                           tools,
                           game,
                           NULL };
    const char *envp[] = { "PATH=", cache[0] ? cache : NULL, NULL };
    static char watch_output[65536], watch_errors[65536];
    chibi_process_output out = { watch_output, sizeof watch_output, 0, false },
                         err = { watch_errors, sizeof watch_errors, 0, false };
    chibi_process_job job;
    double limit = chibi_process_now() + 600;
    REQUIRE(
        chibi_process_begin(&job, in_place, argv, suite, envp, limit, NULL, &out, &err)
            .error == CHIBI_PROCESS_OK
    );
    /* The first generation is live once the work folder holds it; the host runs
     * it on its own for a moment before the edit is saved. */
    while (chibi_process_running(&job) && !exists(live)) {
        chibi_process_service(&job, 1, limit, NULL);
    }
    for (int i = 0; i < 150; i++) {
        chibi_process_service(&job, 1, limit, NULL);
    }
    REQUIRE(chibi_process_running(&job));
    tool_write(source, LOOP_EDITED);
    while (chibi_process_running(&job)) {
        chibi_process_service(&job, 1, limit, NULL);
    }
    chibi_process_capture_result ended = chibi_process_end(&job);
    fprintf(
        stderr,
        "native run: error %d status %d\n%s%s",
        (int)ended.error,
        ended.status,
        watch_output,
        watch_errors
    );
    REQUIRE(ended.error == CHIBI_PROCESS_OK && ended.status == 7);
    return true;
}

/* Real LLVM tools on Linux link macOS arm64 and Windows x64 programs with no
 * SDK of either system. A 256 KiB frame links too: Windows code calls the
 * start object's __chkstk, and arm64 probes inline. A volatile index keeps O2
 * from shrinking the frame to the one element it touches. With
 * CHIBI_NATIVE_BUNDLE set, the programs go into the bundle that CI's
 * native-targets job runs on macOS and Windows. */
TEST_CASE(native_build_links_other_targets_with_llvm)
{
    const char *bin = getenv("CHIBI_LLVM_BIN");
    if (!bin || !*bin) {
        fprintf(stderr, "native build: set CHIBI_LLVM_BIN to link other targets with real LLVM tools\n");
        return true;
    }
    reset("cross", SOURCE);
    real_tools(bin);
    char in_place[1024], program[1024];
    path(in_place, built, "sdk/chibi");
    for (int t = 0; t < 2; t++) {
        const char *bundle = getenv("CHIBI_NATIVE_BUNDLE");
        if (bundle && *bundle) {
            char list[1024], folder[1024];
            path(folder, bundle, CROSS[t].directory);
            path(list, folder, "programs.txt");
            unlink(list);
        }
        for (int mode = 0; mode < 2; mode++) {
            const char *options[] = { mode ? "-O2" : "-O0", "--target", CROSS[t].triple, NULL };
            REQUIRE(build_with(in_place, options, tools) == 0 && printed(program));
            REQUIRE(executable_for(program, t));
            char name[64];
            snprintf(name, sizeof name, "twice-O%d%s", mode * 2, t ? ".exe" : "");
            bundle_program(program, t, name);
        }
        char ir[1024], out[1024];
        snprintf(ir, sizeof ir, "%s/frame-%s.ll", game, CROSS[t].directory);
        snprintf(out, sizeof out, "%s/frame-%s", game, CROSS[t].directory);
        static const char *const modules[] = {
            "target triple = \"arm64-apple-macosx11.0.0\"\n",
            "target triple = \"x86_64-pc-windows-msvc\"\n"
        };
        static const char *const probes[] = { "inline-asm", "__chkstk" };
        char text[1024];
        snprintf(
            text,
            sizeof text,
            "%s@index = internal global i64 60000\n"
            "define i32 @chibi_scalar() #0 {\n"
            "  %%frame = alloca [65536 x i32]\n"
            "  %%i = load volatile i64, ptr @index\n"
            "  %%at = getelementptr [65536 x i32], ptr %%frame, i64 0, i64 "
            "%%i\n"
            "  store volatile i32 42, ptr %%at\n"
            "  %%value = load volatile i32, ptr %%at\n"
            "  ret i32 %%value\n"
            "}\n"
            "attributes #0 = { \"probe-stack\"=\"%s\" }\n",
            modules[t],
            probes[t]
        );
        tool_write(ir, text);
        REQUIRE(driver_build(ir, out));
        REQUIRE(object_holds(out, PROBES[t].bytes, PROBES[t].size));
        path(program, out, CROSS[t].program);
        REQUIRE(executable_for(program, t));
        bundle_program(program, t, t ? "frame-O2.exe" : "frame-O2");
    }
    return true;
}

static const char *const
    WINDOWS_IMPORT_SOURCE = "package scalar\n"
                            "foreign import k32 \"system:kernel32\"\n"
                            "foreign k32 {\n"
                            "    GetCurrentProcessId :: () -> u32\n"
                            "    GetTickCount :: () -> u32\n"
                            "}\n"
                            "Main :: () -> int {\n"
                            "    if (int(GetCurrentProcessId()) > 0 && "
                            "int(GetTickCount()) >= 0) { return 42; }\n"
                            "    return 1;\n"
                            "}\n";

/* A Windows program imports from a system library its source names: the
 * driver writes the library's import definition from the symbols the program
 * calls and lld-link makes the import library from it, so no Windows SDK is
 * needed. The import table of the PE file names each symbol. Other targets
 * refuse a system library. With CHIBI_NATIVE_BUNDLE set, the program goes
 * into the bundle that CI's native-targets job runs on Windows. */
TEST_CASE(native_build_imports_from_a_windows_system_library)
{
    const char *bin = getenv("CHIBI_LLVM_BIN");
    if (!bin || !*bin) {
        fprintf(stderr, "native build: set CHIBI_LLVM_BIN to link Windows imports with real LLVM tools\n");
        return true;
    }
    reset("windows-imports", WINDOWS_IMPORT_SOURCE);
    real_tools(bin);
    char in_place[1024], program[1024], folder[1024];
    path(in_place, built, "sdk/chibi");
    const char *windows[] = { "-O0", "--target", "x86_64-pc-windows-msvc", NULL };
    REQUIRE(build_with(in_place, windows, tools) == 0 && printed(program));
    REQUIRE(executable_for(program, 1));
    size_t length = strlen(program) - strlen("/program.exe");
    memcpy(folder, program, length);
    folder[length] = 0;
    REQUIRE(
        holds(folder, "kernel32.def", "LIBRARY kernel32.dll\nEXPORTS\n    ExitProcess\n    GetCurrentProcessId\n    GetTickCount\n")
    );
    REQUIRE(file_holds(program, "GetCurrentProcessId") && file_holds(program, "GetTickCount"));
    bundle_program(program, 1, "windows-imports.exe");
    const char *linux_only[] = { "-O0", NULL };
    REQUIRE(
        build_with(in_place, linux_only, tools) != 0 &&
        strstr(errors, "system library kernel32 links on Windows only")
    );
    return true;
}

static const char *const
    WINDOWS_HEAP_SOURCE = "package scalar\n"
                          "Main :: () -> int {\n"
                          "    values: int[] = [40, 1, 1];\n"
                          "    return values[0] + values[1] + values[2];\n"
                          "}\n";

/* A Windows program that allocates links no C library: its start module
 * allocates with HeapAlloc and reports a failed assert with WriteFile, so the
 * program imports only kernel32. With CHIBI_NATIVE_BUNDLE set, the program
 * goes into the bundle that CI's native-targets job runs on Windows. */
TEST_CASE(native_build_allocates_on_windows_without_libc)
{
    const char *bin = getenv("CHIBI_LLVM_BIN");
    if (!bin || !*bin) {
        fprintf(stderr, "native build: set CHIBI_LLVM_BIN to link Windows programs with real LLVM tools\n");
        return true;
    }
    reset("windows-heap", WINDOWS_HEAP_SOURCE);
    real_tools(bin);
    char in_place[1024], program[1024], folder[1024];
    path(in_place, built, "sdk/chibi");
    const char *windows[] = { "-O0", "--target", "x86_64-pc-windows-msvc", NULL };
    REQUIRE(build_with(in_place, windows, tools) == 0 && printed(program));
    REQUIRE(executable_for(program, 1));
    size_t length = strlen(program) - strlen("/program.exe");
    memcpy(folder, program, length);
    folder[length] = 0;
    REQUIRE(holds(folder, "start.ll", "call ptr @HeapAlloc(ptr %heap, i32 8, i64 %size)"));
    REQUIRE(
        holds(folder, "kernel32.def", "    GetProcessHeap\n    HeapAlloc\n    GetStdHandle\n    WriteFile\n")
    );
    REQUIRE(
        file_holds(program, "HeapAlloc") && !file_holds(program, "msvcrt") &&
        !file_holds(program, "ucrtbase")
    );
    bundle_program(program, 1, "windows-heap.exe");
    return true;
}

static const char *const WINDOWS_OS_SOURCE =
    "package scalar\n"
    "import windows \"core:sys/windows\"\n"
    "Main :: () -> int {\n"
    "    assert(windows.MakeDirectory(\"chibi_win_probe\"), \"make\");\n"
    "    assert(windows.Exists(\"chibi_win_probe\"), \"exists\");\n"
    "    assert(windows.Kind(\"chibi_win_probe\") == windows.KindDirectory, "
    "\"directory\");\n"
    "    data: u8[] = [104, 105];\n"
    "    assert(windows.WriteBytes(\"chibi_win_probe\\\\a.txt\", data), "
    "\"write\");\n"
    "    back := windows.ReadBytes(\"chibi_win_probe\\\\a.txt\");\n"
    "    assert(back.ok && length(back.data) == 2 && back.data[0] == 104, "
    "\"read\");\n"
    "    assert(windows.Rename(\"chibi_win_probe\\\\a.txt\", "
    "\"chibi_win_probe\\\\b.txt\"), \"rename\");\n"
    "    assert(windows.Remove(\"chibi_win_probe\\\\b.txt\") && "
    "windows.Remove(\"chibi_win_probe\"), \"remove\");\n"
    "    assert(!windows.Exists(\"chibi_win_probe\"), \"removed\");\n"
    "    return 42;\n"
    "}\n";

/* core:sys/windows over kernel32: files and directories, in a program with
 * no C library. It links on Linux and macOS;
 * with CHIBI_NATIVE_BUNDLE set, CI's native-targets job runs it on Windows. */
TEST_CASE(native_build_links_core_sys_windows)
{
    const char *bin = getenv("CHIBI_LLVM_BIN");
    if (!bin || !*bin) {
        fprintf(stderr, "native build: set CHIBI_LLVM_BIN to link Windows programs with real LLVM tools\n");
        return true;
    }
    reset("windows-os", WINDOWS_OS_SOURCE);
    real_tools(bin);
    char in_place[1024], program[1024], folder[1024];
    path(in_place, built, "sdk/chibi");
    const char *windows[] = { "-O0", "--target", "x86_64-pc-windows-msvc", NULL };
    REQUIRE(build_with(in_place, windows, tools) == 0 && printed(program));
    REQUIRE(executable_for(program, 1));
    size_t length = strlen(program) - strlen("/program.exe");
    memcpy(folder, program, length);
    folder[length] = 0;
    REQUIRE(holds(folder, "kernel32.def", "    CreateFileW\n"));
    REQUIRE(!file_holds(program, "msvcrt") && !file_holds(program, "ucrtbase"));
    bundle_program(program, 1, "windows-os.exe");
    return true;
}

/* Each case reads and writes the file-scope fixture state above, so it runs
 * in its own forked process: the parent keeps no state a case changes. */
static bool run_forked(const char *const names[], bool (*const cases[])(void), int count, int workers)
{
    pid_t running[16];
    int slot_case[16];
    bool passed[16] = { false };
    int started = 0, active = 0, finished = 0;
    assert(count <= 16 && workers <= 16);
    memset(running, 0, sizeof running);
    while (finished < count) {
        for (int slot = 0; slot < workers && started < count && active < workers;
             slot++) {
            if (running[slot]) {
                continue;
            }
            fprintf(stderr, "native build: starting %s\n", names[started]);
            pid_t pid = fork();
            assert(pid >= 0);
            if (!pid) {
                _exit(cases[started]() ? 0 : 1);
            }
            running[slot] = pid;
            slot_case[slot] = started++;
            active++;
        }
        int wstatus;
        pid_t done = waitpid(-1, &wstatus, WNOHANG);
        if (done > 0) {
            for (int slot = 0; slot < workers; slot++) {
                if (running[slot] == done) {
                    passed[slot_case[slot]] = WIFEXITED(wstatus) &&
                                              !WEXITSTATUS(wstatus);
                    running[slot] = 0;
                    active--;
                    finished++;
                }
            }
        } else if (chibi_process_now() > deadline + 5) {
            /* Builds stop at the deadline themselves; this catches a hung case. */
            for (int slot = 0; slot < workers; slot++) {
                if (running[slot]) {
                    kill(running[slot], SIGKILL);
                }
            }
            while (waitpid(-1, &wstatus, 0) > 0) {
            }
            break;
        } else {
            usleep(20000);
        }
    }
    bool ok = true;
    for (int i = 0; i < count; i++) {
        fprintf(stderr, "native build: %s %s\n", names[i], passed[i] ? "passed" : "FAILED");
        ok = ok && passed[i];
    }
    return ok;
}

TEST_CASE(native_build_runs_the_cases_in_parallel)
{
    static const char *const names[] = {
        "native_build_links_statically_on_request",
        "native_build_adds_line_tables_with_g",
        "native_build_keeps_prior_programs_and_never_falls_back",
        "native_build_rejects_missing_and_failing_tools",
        "native_build_links_macos_and_windows_programs",
        "native_build_refuses_what_an_interrupted_build_left",
        "native_build_refuses_an_incomplete_output",
        "native_build_runs_with_llvm_and_no_c_compiler",
        "native_build_links_a_vendored_archive",
        "native_build_calls_through_a_function_pointer",
        "native_build_passes_a_byte_array_to_c",
        "native_build_swaps_a_running_loop_keeping_state",
        "native_build_watches_and_swaps_a_running_loop",
        "native_build_reports_a_failed_assert",
        "native_build_runs_the_foreign_samples",
        "native_build_gives_c_an_authored_function",
        "native_build_links_other_targets_with_llvm",
        "native_build_imports_from_a_windows_system_library",
        "native_build_allocates_on_windows_without_libc",
        "native_build_links_core_sys_windows",
    };
    static bool (*const cases[])(void) = {
        native_build_links_statically_on_request,
        native_build_adds_line_tables_with_g,
        native_build_keeps_prior_programs_and_never_falls_back,
        native_build_rejects_missing_and_failing_tools,
        native_build_links_macos_and_windows_programs,
        native_build_refuses_what_an_interrupted_build_left,
        native_build_refuses_an_incomplete_output,
        native_build_runs_with_llvm_and_no_c_compiler,
        native_build_links_a_vendored_archive,
        native_build_calls_through_a_function_pointer,
        native_build_passes_a_byte_array_to_c,
        native_build_swaps_a_running_loop_keeping_state,
        native_build_watches_and_swaps_a_running_loop,
        native_build_reports_a_failed_assert,
        native_build_runs_the_foreign_samples,
        native_build_gives_c_an_authored_function,
        native_build_links_other_targets_with_llvm,
        native_build_imports_from_a_windows_system_library,
        native_build_allocates_on_windows_without_libc,
        native_build_links_core_sys_windows,
    };
    long cores = sysconf(_SC_NPROCESSORS_ONLN);
    int workers = cores < 1 ? 1 : cores > 4 ? 4 : (int)cores;
    return run_forked(names, cases, (int)(sizeof cases / sizeof cases[0]), workers);
}
#endif

void test_native_build(const char *binary)
{
#if defined(__linux__) && defined(__x86_64__)
    /* A timeout kills this process; unbuffered output keeps the progress it made. */
    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    strcpy(suite, "/tmp/chibi-native-build-XXXXXX");
    assert(mkdtemp(suite));
    char *resolved = realpath(suite, NULL);
    assert(resolved && strlen(resolved) < sizeof suite);
    strcpy(suite, resolved);
    free(resolved);
    stage_tool = tool_read(CHIBI_SOURCE_ROOT "/test/native_aot/driver/fixtures/stage-tool.sh");
    assert(stage_tool);
    relocate(binary);
    deadline = chibi_process_now() + 1200;
    /* The first case recovers the compiler into the shared cache; the rest run
     * after it, in parallel, and reuse that recovery. */
    RUN_TEST_CASE(native_build_links_a_program_and_reuses_it);
    RUN_TEST_CASE(native_build_runs_the_cases_in_parallel);
    free(stage_tool);
    tool_remove_tree(suite);
#else
    (void)binary;
    fprintf(stderr, "native build: ten Linux x86-64 cases unexecuted on this platform; the driver admits only that target\n");
#endif
}
