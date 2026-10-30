#include "host/build_host.h"
#include "host/process.h"
#include "support/xalloc.h"
#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static chibi_build_compiler *compiler;
static const char *driver_path;
static char suite[256], root[512], opt[1024], llc[1024], linker[1024],
    input[1024], output[1024];
static char *driver, *contracts, *stage_tool;
static chibi_build_result result;
static volatile sig_atomic_t cancelled;

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

static void write_at(const char *base, const char *name, const char *text)
{
    char file[1024];
    path(file, base, name);
    tool_write(file, text);
}

static bool equals(const char *base, const char *name, const char *expected)
{
    char file[1024];
    path(file, base, name);
    char *text = tool_read(file);
    bool same = text && !strcmp(text, expected);
    free(text);
    return same;
}

/* Contract literals are independent of Driver.chibi and its receipt. The
 * permissive stand-in only records these arguments; it does not validate them. */
static bool optimizer_arguments_match(int mode)
{
    assert(mode == 0 || mode == 2);
    char expected[4096];
    snprintf(
        expected,
        sizeof expected,
        "-S\n-passes=%s\n%s.partial/source.ll\n-o\n%s.partial/module.ll\n",
        mode == 0 ? "verify" : "verify,default<O2>,verify",
        output,
        output
    );
    return equals(root, "opt tool.argv", expected);
}

static void locate()
{
    path(opt, root, "opt tool");
    path(llc, root, "llc tool");
    path(linker, root, "linker tool");
    path(input, root, "input with spaces.ll");
    path(output, root, "output with spaces");
}

static void reset(const char *name)
{
    int size = snprintf(root, sizeof root, "%s/%s", suite, name);
    assert(size > 0 && size < (int)sizeof root && !mkdir(root, 0700));
    locate();
    tool_write(opt, stage_tool);
    tool_write(llc, stage_tool);
    tool_write(linker, stage_tool);
    assert(!chmod(opt, 0700) && !chmod(llc, 0700) && !chmod(linker, 0700));
    write_at(root, "action", "ok\n");
    char *text = tool_read(CHIBI_SOURCE_ROOT "/test/native_aot/driver/fixtures/scalar/Main.ll");
    assert(text);
    tool_write(input, text);
    free(text);
}

static bool invoke(const char *entry, const char *const *arguments, int count, bool adapter)
{
    chibi_build_options options = {
        .compiler = compiler,
        .root = root,
        .source_path = adapter ? CHIBI_SOURCE_ROOT
                           "/test/native_aot/driver/Contracts.chibi"
                               : driver_path,
        .source_text = adapter ? contracts : driver,
        .entry = entry,
        .core = CHIBI_SOURCE_ROOT "/packages/core",
        .arguments = arguments,
        .argument_count = count,
        .backend = BEHAVIOR_BYTECODE,
        .deadline = chibi_build_now() + 10,
        .cancelled = &cancelled,
    };
    bool ok = chibi_build_invoke(&options, &result);
    if (!ok) {
        fprintf(stderr, "native driver: %s\n%s\n", result.diagnostic, result.stderr_text);
    }
    return ok;
}

#ifdef __linux__
static bool run(bool executable, const char *mode, int expected)
{
    const char *arguments[] = {
        input, output, opt, llc, executable ? linker : mode, mode
    };
    return invoke(executable ? "Build" : "Object", arguments, executable ? 6 : 5, false) &&
           result.value == expected && !result.counts.overflow &&
           result.counts.bytecode_entries > 0 &&
           !result.counts.inspection_bytecode_fallbacks;
}

static bool absent()
{
    char partial[1100];
    snprintf(partial, sizeof partial, "%s.partial", output);
    return !exists(output) && !exists(partial);
}

#endif

TEST_CASE(tool_admission_resolves_aliases_and_rejects_unusable_paths)
{
    reset("tool admission");
    char alias[1024], missing[1024], plain[1024], dangling[1024];
    path(alias, root, "relocated tool alias");
    path(missing, root, "missing");
    path(plain, root, "not executable");
    path(dangling, root, "dangling alias");
    tool_write(plain, "not a tool\n");
    REQUIRE(!symlink(opt, alias) && !symlink(missing, dangling));
    const char *valid[] = { alias };
    REQUIRE(invoke("InspectTool", valid, 1, true) && result.value == 0);
    char expected[1100];
    snprintf(expected, sizeof expected, "%s\n", opt);
    REQUIRE(!strcmp(result.stderr_text, expected));
    const char *invalid[] = { missing, plain,           dangling,
                              root,    "relative tool", "/tmp/control\npath" };
    for (int i = 0; i < (int)(sizeof invalid / sizeof *invalid); i++) {
        const char *argument[] = { invalid[i] };
        REQUIRE(invoke("InspectTool", argument, 1, true) && result.value == 0);
        REQUIRE(!strcmp(result.stderr_text, "\n"));
    }
    return true;
}

TEST_CASE(cleanup_removes_only_owned_names_and_preserves_siblings)
{
    reset("cleanup ownership");
    char partial[1024];
    path(partial, root, "reserved.partial");
    REQUIRE(!mkdir(partial, 0700) && !mkdir(output, 0700));
    write_at(output, "receipt.txt", "previous complete output\n");
    const char *names[] = { "source.ll",    "module.ll",    "object.o",
                            "start.ll",     "start.o",      "libSystem.tbd",
                            "kernel32.def", "kernel32.lib", "program",
                            "program.exe",  "receipt.txt" };
    enum
    {
        NAMES = (int)(sizeof names / sizeof *names)
    };
    for (int i = 0; i < NAMES; i++) {
        write_at(partial, names[i], "owned bytes\n");
    }
    write_at(partial, "prior-owner", "keep unknown bytes\n");
    const char *arguments[] = { partial };
    REQUIRE(invoke("InspectCleanup", arguments, 1, true) && result.value == 1);
    REQUIRE(equals(partial, "prior-owner", "keep unknown bytes\n"));
    REQUIRE(equals(output, "receipt.txt", "previous complete output\n"));
    for (int i = 0; i < NAMES; i++) {
        char file[1024];
        path(file, partial, names[i]);
        REQUIRE(!exists(file));
    }
    char marker[1024];
    path(marker, partial, "prior-owner");
    REQUIRE(!unlink(marker));
    REQUIRE(
        invoke("InspectCleanup", arguments, 1, true) && result.value == 0 &&
        !exists(partial)
    );
    return true;
}

/* Driver.Target admits each native_abi target's triple with the layout LLVM
 * 18 gives it, and nothing else: no crossed pair, no other LLVM major's arm64
 * layout, and exactly one of each line. */
TEST_CASE(target_admits_each_triple_with_its_own_layout)
{
    reset("target admission");
    static const char *const triples[] = { "x86_64-unknown-linux-gnu",
                                           "arm64-apple-macosx11.0.0",
                                           "x86_64-pc-windows-msvc" };
    static const char *const layouts[] = {
        "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:"
        "32:64-S128",
        "e-m:o-i64:64-i128:128-n32:64-S128",
        "e-m:w-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:"
        "32:64-S128"
    };
    static const char *const rejected[] = {
        /* LLVM 19's arm64 layout. */
        "target datalayout = \"e-m:o-i64:64-i128:128-n32:64-S128-Fn32\"\n"
        "target triple = \"arm64-apple-macosx11.0.0\"\n",
        "target datalayout = \"e-m:o-i64:64-i128:128-n32:64-S128\"\n"
        "target triple = \"aarch64-apple-darwin\"\n",
        "target datalayout = \"e-m:o-i64:64-i128:128-n32:64-S128\"\n",
        "target triple = \"arm64-apple-macosx11.0.0\"\n",
        "target datalayout = \"e-m:o-i64:64-i128:128-n32:64-S128\"\n"
        "target triple = \"arm64-apple-macosx11.0.0\"\n"
        "target triple = \"arm64-apple-macosx11.0.0\"\n",
    };
    enum
    {
        PAIRS = 9,
        MODULES = PAIRS + (int)(sizeof rejected / sizeof *rejected)
    };
    /* One invocation inspects every module: compiling the driver dominates. */
    char modules[MODULES][1024];
    const char *arguments[MODULES];
    int expected = 0;
    for (int i = 0; i < MODULES; i++) {
        char name[32], text[512];
        snprintf(name, sizeof name, "module %d.ll", i);
        path(modules[i], root, name);
        arguments[i] = modules[i];
        if (i >= PAIRS) {
            tool_write(modules[i], rejected[i - PAIRS]);
            continue;
        }
        snprintf(
            text,
            sizeof text,
            "target datalayout = \"%s\"\ntarget triple = \"%s\"\n",
            layouts[i % 3],
            triples[i / 3]
        );
        tool_write(modules[i], text);
        expected |= i % 3 == i / 3 ? 1 << i : 0;
    }
    REQUIRE(invoke("InspectTargets", arguments, MODULES, true) && result.value == expected);
    return true;
}

/* `chibi export --native` finds library sources by their first tokens:
 * comments and whitespace, a name, `::` and the whole word `library`. */
TEST_CASE(library_names_come_from_the_declaration_alone)
{
    reset("library names");
    static const char *const sources[] = {
        "Mix :: library {\n}\n",
        "// Mixing.\n/* A * block */\n\tMix::library{}\n",
        "Mix\n::\nlibrary /* trailing */ {}\n",
        "Mix :: libraryish {}\n",
        "Mix :: behavior {}\n",
        "Mixer :: library {}\n",
        "/* Mix :: library {} */\n",
        "// Mix :: library {}\n",
        "Mix : library {}\n",
        "Mix ::",
        "",
    };
    enum
    {
        COUNT = (int)(sizeof sources / sizeof *sources)
    };
    char files[COUNT][1024];
    const char *arguments[COUNT];
    for (int i = 0; i < COUNT; i++) {
        char name[32];
        snprintf(name, sizeof name, "source %d.chibi", i);
        path(files[i], root, name);
        tool_write(files[i], sources[i]);
        arguments[i] = files[i];
    }
    REQUIRE(invoke("InspectLibraryNames", arguments, COUNT, true) && result.value == 7);
    return true;
}

static void cancel(int signal)
{
    (void)signal;
    cancelled = 1;
}

// Exercise the same process capability on hosts where Run rejects Linux IR.
// A disabled cancellation pointer must fail on the timeout diagnostic.
TEST_CASE(stage_cancellation_requires_the_cancellation_diagnostic)
{
    reset("stage cancellation");
    write_at(root, "action", "cancel\n");
    struct sigaction action = { .sa_handler = cancel }, previous;
    sigemptyset(&action.sa_mask);
    REQUIRE(!sigaction(SIGUSR1, &action, &previous));
    const char *arguments[] = { llc,    "-filetype=obj",
                                "-O=0", "-relocation-model=pic",
                                input,  "-o",
                                output };
    bool ok = invoke("InspectInterruption", arguments, 7, true);
    assert(!sigaction(SIGUSR1, &previous, NULL));
    bool interrupted = cancelled;
    cancelled = 0;
    REQUIRE(interrupted && !ok);
    REQUIRE(!strcmp(result.diagnostic, "build process cancelled"));
    REQUIRE(equals(root, "output with spaces", "controlled object\n"));
    return true;
}

TEST_CASE(optimizer_trace_distinguishes_passes_and_snapshot_paths)
{
    reset("optimizer argv");
    char partial[1024], snapshot[1024], module[1024];
    path(partial, root, "output with spaces.partial");
    REQUIRE(!mkdir(partial, 0700));
    path(snapshot, partial, "source.ll");
    path(module, partial, "module.ll");
    tool_write(snapshot, "controlled snapshot\n");
    const struct
    {
        const char *passes;
        int mode;
        const char *source, *destination;
        bool matches;
    } controls[] = {
        { "-passes=verify", 0, snapshot, module, true },
        { "-passes=verify,default<O2>,verify", 2, snapshot, module, true },
        { "-passes=", 0, snapshot, module, false },
        { "-passes=default<O2>", 2, snapshot, module, false },
        { "-passes=default<O2>,verify", 2, snapshot, module, false },
        { "-passes=verify,default<O2>", 2, snapshot, module, false },
        { "-passes=verify,verify", 2, snapshot, module, false },
        { "-passes=verify", 0, input, module, false },
        { "-passes=verify", 0, snapshot, output, false },
    };
    for (int i = 0; i < (int)(sizeof controls / sizeof *controls); i++) {
        const char *arguments[] = { opt,
                                    "-S",
                                    controls[i].passes,
                                    controls[i].source,
                                    "-o",
                                    controls[i].destination };
        REQUIRE(invoke("InspectInterruption", arguments, 6, true) && result.value == 0);
        REQUIRE(optimizer_arguments_match(controls[i].mode) == controls[i].matches);
    }
    return true;
}

#ifdef __linux__
TEST_CASE(publication_preserves_existing_files_and_stale_partial_bytes)
{
    for (int executable = 0; executable < 2; executable++) {
        reset(executable ? "build preservation" : "object preservation");
        tool_write(output, "existing final file\n");
        REQUIRE(run(executable, "0", 2));
        REQUIRE(equals(root, "output with spaces", "existing final file\n"));
        REQUIRE(!unlink(output));
        char partial[1100];
        snprintf(partial, sizeof partial, "%s.partial", output);
        REQUIRE(!mkdir(partial, 0700));
        write_at(partial, "object.o", "interrupted object\n");
        write_at(partial, "receipt.txt", "incomplete receipt\n");
        REQUIRE(run(executable, "0", 2) && !exists(output));
        REQUIRE(equals(partial, "object.o", "interrupted object\n"));
        REQUIRE(equals(partial, "receipt.txt", "incomplete receipt\n"));
        char trace[1024];
        path(trace, root, "opt tool.argv");
        REQUIRE(!exists(trace));
    }
    return true;
}

TEST_CASE(object_relocates_tools_with_spaces_without_a_linker_or_path)
{
    reset("relocated object");
    char moved[1024];
    path(moved, suite, "moved tools and IR");
    REQUIRE(!rename(root, moved));
    assert(strlen(moved) < sizeof root);
    strcpy(root, moved);
    locate();
    REQUIRE(!unlink(linker));
    char alias[1024];
    path(alias, root, "opt alias");
    REQUIRE(!symlink(opt, alias));
    strcpy(opt, alias);
    for (int mode = 0; mode < 2; mode++) {
        path(output, root, mode ? "object O2" : "object O0");
        REQUIRE(run(false, mode ? "2" : "0", 0));
        REQUIRE(optimizer_arguments_match(mode ? 2 : 0));
        REQUIRE(equals(output, "object.o", "controlled object\n"));
        char receipt[1024];
        path(receipt, output, "receipt.txt");
        char *text = tool_read(receipt);
        REQUIRE(text);
        bool correct = strstr(text, "output-kind object\n") &&
                       !strstr(text, "linker") && !strstr(text, "stage link\n") &&
                       !strstr(text, "program-sha256");
        char identity[1100];
        snprintf(identity, sizeof identity, "\nopt %s/opt tool\n", root);
        correct = correct && strstr(text, identity) && !strstr(text, "opt alias");
        free(text);
        REQUIRE(correct);
        char file[1024];
        path(file, output, "program");
        REQUIRE(!exists(file));
        char expected[4096];
        snprintf(
            expected,
            sizeof expected,
            "-filetype=obj\n-O=%d\n-relocation-model=pic\n%s.partial/"
            "module.ll\n-o\n%s.partial/object.o\n",
            mode ? 2 : 0,
            output,
            output
        );
        REQUIRE(equals(root, "llc tool.argv", expected));
    }
    return true;
}

static bool receipt_names(const char *expected)
{
    char receipt[1024];
    path(receipt, output, "receipt.txt");
    char *text = tool_read(receipt);
    bool found = text && strstr(text, expected);
    free(text);
    return found;
}

TEST_CASE(executable_links_with_the_named_linker_and_start_object)
{
    reset("linked executable");
    for (int mode = 0; mode < 2; mode++) {
        path(output, root, mode ? "program O2" : "program O0");
        REQUIRE(run(true, mode ? "2" : "0", 0));
        char expected[4096];
        snprintf(
            expected,
            sizeof expected,
            "-filetype=obj\n-O=%d\n-relocation-model=pic\n%s.partial/"
            "start.ll\n-o\n%s.partial/"
            "start.o\n",
            mode ? 2 : 0,
            output,
            output
        );
        REQUIRE(equals(root, "llc tool.start.argv", expected));
        snprintf(
            expected,
            sizeof expected,
            "-flavor\ngnu\n-static\n--gc-sections\n-e\n_start\n%s.partial/"
            "object.o\n%s.partial/start.o\n-o\n%s."
            "partial/program\n",
            output,
            output,
            output
        );
        REQUIRE(equals(root, "linker tool.argv", expected));
        REQUIRE(equals(output, "start.o", "controlled start object\n"));
        snprintf(expected, sizeof expected, "\nlinker %s\n", linker);
        REQUIRE(receipt_names("output-kind executable\n") && receipt_names(expected));
        REQUIRE(receipt_names("\nstart-object-sha256 ") && receipt_names("stage llc-start\n"));
        REQUIRE(receipt_names("stage link\n"));
        char program[1024];
        path(program, output, "program");
        REQUIRE(!access(program, X_OK));
    }
    return true;
}

/* Real LLVM tools, an empty PATH and no C compiler: the program the driver
 * links is the emitted object and its start object alone. */
TEST_CASE(executable_links_llvm_objects_without_a_c_compiler)
{
    const char *bin = getenv("CHIBI_LLVM_BIN");
    if (!bin || !*bin) {
        fprintf(stderr, "native driver: set CHIBI_LLVM_BIN to link real LLVM objects\n");
        return true;
    }
    reset("llvm executable");
    path(opt, bin, "opt");
    path(llc, bin, "llc");
    path(linker, bin, "ld.lld");
    for (int mode = 0; mode < 2; mode++) {
        path(output, root, mode ? "llvm O2" : "llvm O0");
        REQUIRE(run(true, mode ? "2" : "0", 0));
        char program[1024];
        path(program, output, "program");
        const char *argv[] = { program, NULL };
        const char *envp[] = { NULL };
        chibi_process_output out = { 0 }, err = { 0 };
        chibi_process_capture_result exited = chibi_process_capture(
            program,
            argv,
            root,
            envp,
            chibi_build_now() + 10,
            NULL,
            &out,
            &err
        );
        REQUIRE(exited.error == CHIBI_PROCESS_OK && exited.status == 42);
    }
    return true;
}

TEST_CASE(missing_tools_reject_before_reserving_output)
{
    for (int executable = 0; executable < 2; executable++) {
        for (int slot = 0; slot < (executable ? 3 : 2); slot++) {
            char name[64];
            snprintf(name, sizeof name, "missing tool %d %d", executable, slot);
            reset(name);
            REQUIRE(!unlink(slot == 0 ? opt : slot == 1 ? llc : linker));
            REQUIRE(run(executable, "0", 2) && absent());
        }
    }
    return true;
}

TEST_CASE(failed_signalled_and_missing_objects_never_publish)
{
    const char *actions[] = { "fail", "signal", "missing" };
    const int status[] = { 71, -SIGTERM, 1 };
    for (int executable = 0; executable < 2; executable++) {
        for (int i = 0; i < 3; i++) {
            char name[64], action[64];
            snprintf(name, sizeof name, "object failure %d %d", executable, i);
            reset(name);
            snprintf(action, sizeof action, "%s\n", actions[i]);
            write_at(root, "action", action);
            REQUIRE(run(executable, "0", status[i]) && absent());
            REQUIRE(strstr(result.stderr_text, "LLVM object emission failed"));
            char trace[1024];
            path(trace, root, "linker tool.argv");
            REQUIRE(!exists(trace));
        }
    }
    return true;
}

TEST_CASE(changed_tool_identity_prevents_publication_after_object_creation)
{
    const char *actions[] = { "mutate-opt\n", "mutate-llc\n" };
    for (int i = 0; i < 2; i++) {
        reset(i ? "changed llc" : "changed opt");
        write_at(root, "action", actions[i]);
        REQUIRE(run(false, "0", 1) && absent());
        REQUIRE(strstr(result.stderr_text, "tool identity changed during build"));
    }
    return true;
}

TEST_CASE(interrupted_object_remains_unpublished_and_retry_preserves_it)
{
    reset("interrupted emission");
    write_at(root, "action", "cancel\n");
    struct sigaction action = { .sa_handler = cancel }, previous;
    sigemptyset(&action.sa_mask);
    REQUIRE(!sigaction(SIGUSR1, &action, &previous));
    const char *arguments[] = { input, output, opt, llc, "0" };
    bool ok = invoke("Object", arguments, 5, false);
    assert(!sigaction(SIGUSR1, &previous, NULL));
    bool interrupted = cancelled;
    cancelled = 0;
    REQUIRE(interrupted && !ok && !exists(output));
    REQUIRE(!strcmp(result.diagnostic, "build process cancelled"));
    char partial[1100];
    snprintf(partial, sizeof partial, "%s.partial", output);
    REQUIRE(equals(partial, "object.o", "controlled object\n"));
    char receipt[1024];
    path(receipt, partial, "receipt.txt");
    REQUIRE(!exists(receipt));
    REQUIRE(run(false, "0", 2) && !exists(output));
    REQUIRE(equals(partial, "object.o", "controlled object\n"));
    return true;
}
#endif

void test_native_aot_driver(const char *source)
{
    char error[256] = { 0 };
    compiler = chibi_build_compiler_load(
        CHIBI_SOURCE_ROOT "/tools/compiler",
        CHIBI_SOURCE_ROOT "/packages/core",
        NULL,
        error,
        sizeof error
    );
    if (!compiler) {
        fprintf(stderr, "native driver compiler: %s\n", error);
    }
    assert(compiler);
    strcpy(suite, "/tmp/chibi-native-driver-XXXXXX");
    assert(mkdtemp(suite));
    char *resolved = realpath(suite, NULL);
    assert(resolved && strlen(resolved) < sizeof suite);
    strcpy(suite, resolved);
    free(resolved);
    driver_path = source;
    driver = tool_read(driver_path);
    contracts = tool_read(CHIBI_SOURCE_ROOT "/test/native_aot/driver/Contracts.chibi");
    stage_tool = tool_read(CHIBI_SOURCE_ROOT "/test/native_aot/driver/fixtures/stage-tool.sh");
    assert(driver && contracts && stage_tool);
    /* The helpers are private. Append only the adapters to the unchanged driver
     * in memory, keeping production source bytes and capabilities intact. */
    char *adapters = contracts;
    const char *body = strchr(adapters, '\n');
    assert(body);
    size_t size = strlen(driver) + strlen(body) + 1;
    contracts = xmalloc(size);
    snprintf(contracts, size, "%s%s", driver, body);
    free(adapters);
    RUN_TEST_CASE(tool_admission_resolves_aliases_and_rejects_unusable_paths);
    RUN_TEST_CASE(cleanup_removes_only_owned_names_and_preserves_siblings);
    RUN_TEST_CASE(target_admits_each_triple_with_its_own_layout);
    RUN_TEST_CASE(library_names_come_from_the_declaration_alone);
    RUN_TEST_CASE(stage_cancellation_requires_the_cancellation_diagnostic);
    RUN_TEST_CASE(optimizer_trace_distinguishes_passes_and_snapshot_paths);
#ifdef __linux__
    char *saved_path = getenv("PATH") ? strdup(getenv("PATH")) : NULL;
    assert(!setenv("PATH", "", 1));
    RUN_TEST_CASE(publication_preserves_existing_files_and_stale_partial_bytes);
    RUN_TEST_CASE(object_relocates_tools_with_spaces_without_a_linker_or_path);
    RUN_TEST_CASE(executable_links_with_the_named_linker_and_start_object);
    RUN_TEST_CASE(executable_links_llvm_objects_without_a_c_compiler);
    RUN_TEST_CASE(missing_tools_reject_before_reserving_output);
    RUN_TEST_CASE(failed_signalled_and_missing_objects_never_publish);
    RUN_TEST_CASE(changed_tool_identity_prevents_publication_after_object_creation);
    RUN_TEST_CASE(interrupted_object_remains_unpublished_and_retry_preserves_it);
    assert(saved_path ? !setenv("PATH", saved_path, 1) : !unsetenv("PATH"));
    free(saved_path);
#else
    fprintf(stderr, "native driver: eight Linux publication cases unexecuted on this platform\n");
#endif
    free(stage_tool);
    free(contracts);
    free(driver);
    chibi_build_compiler_free(compiler);
    tool_remove_tree(suite);
}
