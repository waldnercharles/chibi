#include "assets/project.h"
#include "host/process.h"
#include "support/xalloc.h"
#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef CHIBI_TEST_NATIVE_RUNTIME
#include "ecs/ecs.h"
#include "execution/backend.h"
#include "host/native_default.h"
#include "host/native_libraries.h"
#include "runtime/component_catalog.h"
#include "runtime/engine.h"
#include "runtime/native_prepared.h"
#include "runtime/player_prepared.h"
#endif

#if defined(__linux__) && defined(__x86_64__)
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
    const char *envp[] = { "PATH=", NULL };
    status = chibi_process_capture(program, argv, suite, envp, chibi_process_now() + 120, NULL, &out, &err);
    assert(status.error == CHIBI_PROCESS_OK);
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
    REQUIRE(build_with(launcher, twice, tools) == 2 && strstr(errors, "Invalid build option: -g"));
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
        strstr(errors, "Invalid build option: --target")
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

/* The relocated SDK's player, compiler and engine files, which export
 * copies, beside what the native driver needs. */
static void exportable()
{
    char from[1024], to[1024];
    const char *programs[] = { "chibi-compile", "chibi-player" };
    for (int i = 0; i < 2; i++) {
        snprintf(from, sizeof from, "%s/sdk/%s", built, programs[i]);
        snprintf(to, sizeof to, "%s/bin/%s", sdk, programs[i]);
        copy(from, to);
    }
    const char *engine[] = { "shaders", "engine.json" };
    for (int i = 0; i < 2; i++) {
        snprintf(from, sizeof from, "%s/share/chibi/%s", built, engine[i]);
        snprintf(to, sizeof to, "%s/share/chibi/%s", sdk, engine[i]);
        copy(from, to);
    }
    directory(sdk, "share/chibi/licenses");
    write_at(sdk, "share/chibi/licenses/LICENSE.txt", "license\n");
}

/* A game whose Probe calls Mix, which native code holds, and Effects, whose
 * function returns nothing and so stays in the VM. */
static void native_game(const char *root)
{
    char manifest[256];
    snprintf(
        manifest,
        sizeof manifest,
        "version %d\nname \"Native Game\"\ncontent \"content\"\n"
        "start_scene \"scenes/Start.kdl\"\n",
        CHIBI_PROJECT_VERSION
    );
    const char *folders[] = { "",
                              "content",
                              "content/scenes",
                              "content/prototypes",
                              "content/behaviors",
                              "content/libraries",
                              "content/libraries/.backup",
                              "content/examples" };
    for (int i = 0; i < 8; i++) {
        directory(root, folders[i]);
    }
    write_at(root, "project.kdl", manifest);
    write_at(
        root,
        "content/scenes/Start.kdl",
        "version 1\nname \"Start\"\n(array)entities {\n    - {\n"
        "        name \"Probe\"\n        prototype \"Probe\"\n"
        "        (array)behaviors \"Probe\"\n    }\n}\n"
    );
    write_at(root, "content/prototypes/Probe.kdl", "name \"Probe\"\ncomponents {\n    (object)Transform\n}\n");
    write_at(
        root,
        "content/behaviors/Probe.chibi",
        "Probe :: behavior {\n    x: number = 0\n    on Tick :: () {\n"
        "        x = Mix.Step(x, 3)\n        Effects.Mark(x)\n"
        "        self.Transform = vec2(x, 0)\n    }\n}\n"
    );
    write_at(
        root,
        "content/libraries/Mix.chibi",
        "Mix :: library {\n    Step :: (value: number, count: int) -> number "
        "{\n"
        "        return value * 2 + count\n    }\n}\n"
    );
    /* Neither compiles into the game: the export must not ship them. */
    write_at(root, "content/examples/Demo.chibi", "Demo :: library {\n    One :: () -> int {\n        return 1\n    }\n}\n");
    write_at(root, "content/libraries/.backup/Old.chibi", "Old :: library {\n    One :: () -> int {\n        return 1\n    }\n}\n");
    write_at(root, "content/libraries/Effects.chibi", "Effects :: library {\n    Mark :: (value: number) {\n        return\n    }\n}\n");
}

#ifdef CHIBI_TEST_NATIVE_RUNTIME
/* The CPU host keeps no graphics handles. */
static void *stage_cpu(void *, const player_asset *, int, size_t *, char *, size_t)
{
    static int resources;
    return &resources;
}
static bool cpu_scene(void *, void *, const scene *, char *, size_t)
{
    return true;
}
static void commit_cpu(void *, void *, world *) {}
static void release_cpu(void *, void *) {}

static unsigned char *read_bytes(const char *name, size_t *length)
{
    FILE *file = fopen(name, "rb");
    assert(file && !fseek(file, 0, SEEK_END));
    long size = ftell(file);
    assert(size > 0 && !fseek(file, 0, SEEK_SET));
    unsigned char *bytes = xmalloc((size_t)size);
    *length = fread(bytes, 1, (size_t)size, file);
    assert(*length == (size_t)size && !fclose(file));
    return bytes;
}

void test_native_catalog_enable();

/* The ECS hash of the exported game in `data` after a few frames, with the
 * libraries in its natives folder bound when `native`, or 0 when it does not
 * play. Bound, it needs exactly one library. */
static uint64_t played(const char *data, bool native)
{
    char artifact[1024], natives[1024], error[512] = "";
    path(artifact, data, "game.chibip");
    path(natives, data, "natives");
    size_t length;
    unsigned char *bytes = read_bytes(artifact, &length);
    native_libraries *libraries = native
                                    ? native_libraries_open(natives, error, sizeof error)
                                    : NULL;
    int count = 0;
    const execution_native_manifest *const
        *manifests = libraries ? native_libraries_manifests(libraries, &count) : NULL;
    native_capabilities *catalog = host_native_default_catalog_new(NULL, 0, NULL, 0, error, sizeof error);
    assert(catalog);
    native_prepared_context context = { .native = catalog };
    execution_prepared_linker linker = native_capabilities_prepared_linker(&context);
    player_host host = { .components = { REGISTRY_COMPONENTS, REGISTRY_COMPONENT_COUNT },
                         .linker = &linker,
                         .natives = manifests,
                         .native_count = count,
                         .resources = { .stage = stage_cpu,
                                        .scene = cpu_scene,
                                        .commit = commit_cpu,
                                        .discard = release_cpu,
                                        .retire = release_cpu } };
    player *runtime = native && count != 1 ? NULL
                                           : player_prepared_read(
                                                 bytes,
                                                 length,
                                                 &host,
                                                 behavior_default_backend(),
                                                 error,
                                                 sizeof error
                                             );
    engine *engine = runtime ? world_engine(player_world(runtime)) : NULL;
    bool ran = engine && engine_set_focus(engine, ENGINE_FOCUS_VIEWPORT) == 0 &&
               engine_set_playing(engine, true) == 0;
    for (int frame = 0; ran && frame < 4; frame++) {
        ran = engine_frame(engine, (frame + .5) * ENGINE_FIXED_SECONDS) == 0;
    }
    if (!ran) {
        fprintf(stderr, "exported game: %s\n", engine && engine_error(engine) ? engine_error(engine) : error);
    }
    uint64_t hash = ran ? ecs_world_hash(world_ecs(player_world(runtime))) : 0;
    if (runtime) {
        player_free(runtime);
    }
    native_capabilities_free(catalog);
    native_libraries_close(libraries);
    free(bytes);
    return hash;
}
#endif

/* `chibi export --native` with real LLVM tools ships each library native
 * code holds as natives/<Library>.so and says why another stays in the VM.
 * The player binds what it ships, which plays as the VM does. */
TEST_CASE(native_export_ships_libraries_the_player_binds)
{
    const char *bin = getenv("CHIBI_LLVM_BIN");
    if (!bin || !*bin) {
        fprintf(stderr, "native build: set CHIBI_LLVM_BIN to export native libraries with real LLVM tools\n");
        return true;
    }
    exportable();
    char project[1024], exported[1024], data[1024], library[1024];
    path(project, suite, "native game");
    path(exported, suite, "exported game");
    path(data, exported, "data");
    native_game(project);
    chibi_process_output out = { .data = output, .capacity = sizeof output };
    chibi_process_output err = { .data = errors, .capacity = sizeof errors };
    const char *envp[] = { "PATH=", NULL };
    const char *vm_only[] = { launcher,        "export", project, "--output",
                              "exported game", "--llvm", bin,     NULL };
    status = chibi_process_capture(launcher, vm_only, suite, envp, chibi_process_now() + 60, NULL, &out, &err);
    REQUIRE(status.error == CHIBI_PROCESS_OK && status.status == 2);
    REQUIRE(strstr(errors, "export --llvm needs --native") && !exists(exported));
    /* A relative --output: the driver is given absolute paths. */
    const char *argv[] = {
        launcher,   "export", project, "--output", "exported game",
        "--native", "--llvm", bin,     NULL
    };
    status = chibi_process_capture(launcher, argv, suite, envp, chibi_process_now() + 300, NULL, &out, &err);
    if (status.error != CHIBI_PROCESS_OK || status.status) {
        fprintf(stderr, "%s", errors);
    }
    REQUIRE(status.error == CHIBI_PROCESS_OK && status.status == 0);
    REQUIRE(
        strstr(errors, "libraries/Effects.chibi:2:5: ") &&
        strstr(errors, "; the library stays in the VM")
    );
    path(library, data, "natives/Mix.so");
    REQUIRE(exists(library));
    static const char *const skipped[] = { "natives/Effects.so",
                                           "natives/Demo.so",
                                           "natives/Old.so" };
    for (size_t i = 0; i < sizeof skipped / sizeof *skipped; i++) {
        path(library, data, skipped[i]);
        REQUIRE(!exists(library));
    }
#ifdef CHIBI_TEST_NATIVE_RUNTIME
    test_native_catalog_enable(); /* The default natives' records need test_tools' types. */
    uint64_t vm = played(data, false);
    REQUIRE(vm && played(data, true) == vm);
#else
    fprintf(stderr, "native build: playing the exported game needs the native runtime test build\n");
#endif
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
        compiler[1024], opt[1024], llc[1024], lld[1024];
    path(host, sdk, "libexec/chibi/chibi-build-host");
    path(share, sdk, "share/chibi");
    path(driver, share, "tools/native_aot");
    path(source, driver, "Driver.chibi");
    path(core, share, "core");
    path(compiler, share, "compiler");
    path(opt, tools, "opt");
    path(llc, tools, "llc");
    path(lld, tools, "ld.lld");
    const char *argv[] = { host,     "--root",
                           driver,   "--source",
                           source,   "--core",
                           core,     "--compiler",
                           compiler, "--compiler-core",
                           core,     "--entry",
                           "Build",  "--timeout-ms",
                           "120000", "--",
                           ir,       out,
                           opt,      llc,
                           lld,      "2",
                           NULL };
    const char *envp[] = { "PATH=", NULL };
    chibi_process_output out_text = { .data = output, .capacity = sizeof output };
    chibi_process_output err = { .data = errors, .capacity = sizeof errors };
    status = chibi_process_capture(host, argv, suite, envp, chibi_process_now() + 180, NULL, &out_text, &err);
    return status.error == CHIBI_PROCESS_OK && !status.status;
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
#endif

void test_native_build(const char *binary)
{
#if defined(__linux__) && defined(__x86_64__)
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
    RUN_TEST_CASE(native_build_adds_line_tables_with_g);
    RUN_TEST_CASE(native_build_keeps_prior_programs_and_never_falls_back);
    RUN_TEST_CASE(native_build_rejects_missing_and_failing_tools);
    RUN_TEST_CASE(native_build_links_macos_and_windows_programs);
    RUN_TEST_CASE(native_build_refuses_what_an_interrupted_build_left);
    RUN_TEST_CASE(native_build_refuses_an_incomplete_output);
    RUN_TEST_CASE(native_build_runs_with_llvm_and_no_c_compiler);
    RUN_TEST_CASE(native_build_links_other_targets_with_llvm);
    RUN_TEST_CASE(native_export_ships_libraries_the_player_binds);
    free(stage_tool);
    tool_remove_tree(suite);
#else
    (void)binary;
    fprintf(stderr, "native build: ten Linux x86-64 cases unexecuted on this platform; the driver admits only that target\n");
#endif
}
