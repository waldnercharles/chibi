/* Links and runs, on macOS arm64 or Windows x64, the objects that
 * language-scalar-native builds for that target when CHIBI_NATIVE_BUNDLE
 * names a directory (see README.md). Each module's O0 or O2 object becomes a
 * shared library through the platform linker, and each listed call must give
 * the value both VMs and the native Linux runs gave. Each program that
 * native-build links for the target on Linux must exit with its listed
 * status. The engine's test
 * drivers do not build on Windows yet, so this file stands alone: clang
 * builds it with no other source.
 *
 * Usage: run BUNDLE LINKER, where BUNDLE is the bundle's root and LINKER is
 * clang on macOS and lld-link on Windows. */
#define PICO_UNIT_IMPLEMENTATION
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__APPLE__) && defined(__aarch64__)
#include <dlfcn.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
extern char **environ;
static const char TARGET[] = "macos-arm64", LIBRARY[] = "dylib";
#elif defined(_WIN64) && defined(__x86_64__)
#include <process.h>
#include <windows.h>
static const char TARGET[] = "windows-x64", LIBRARY[] = "dll";
#else
#error "the bundle runner runs on macOS arm64 or Windows x64"
#endif

enum
{
    MAX_ARGUMENTS = 3,
    MAX_CALLS = 1024,
    PATH_BYTES = 1024,
    SYMBOL_BYTES = 64
};

typedef struct bundle_call
{
    char symbol[SYMBOL_BYTES];
    int64_t expected;
    int count;
    int64_t arguments[MAX_ARGUMENTS];
} bundle_call;

typedef int32_t (*scalar_entry)(void);
typedef int64_t (*scalar_adapter)(const int64_t *);

static const char *bundle, *linker;
static char directory[PATH_BYTES];
static int modules, calls_run;

/* Runs argv, an executable and its arguments, and gives its exit status. */
static int run(const char *const argv[])
{
#ifdef _WIN32
    /* _spawnv joins its arguments with spaces, so each is quoted. */
    char quoted[64][PATH_BYTES + 2];
    const char *quoted_argv[64];
    int count = 0;
    for (; argv[count]; count++) {
        assert(count < 63 && !strchr(argv[count], '"'));
        snprintf(quoted[count], sizeof quoted[count], "\"%s\"", argv[count]);
        quoted_argv[count] = quoted[count];
    }
    quoted_argv[count] = NULL;
    fflush(stdout);
    return (int)_spawnv(_P_WAIT, argv[0], quoted_argv);
#else
    pid_t child;
    if (posix_spawn(&child, argv[0], NULL, NULL, (char *const *)argv, environ)) {
        return -1;
    }
    int status;
    if (waitpid(child, &status, 0) != child || !WIFEXITED(status)) {
        return -1;
    }
    return WEXITSTATUS(status);
#endif
}

/* Links `object`, whose calls name its exported symbols, as `library`. */
static bool link_library(const char *object, const bundle_call *calls, int count, const char *library)
{
#ifdef _WIN32
    char runtime[PATH_BYTES], output[PATH_BYTES + 8];
    char exports[MAX_CALLS][SYMBOL_BYTES + 8];
    const char *argv[MAX_CALLS + 16] = { linker,          "/nologo",
                                         "/dll",          "/noentry",
                                         "/nodefaultlib", "/machine:x64" };
    int n = 6, exported = 0;
    snprintf(output, sizeof output, "/out:%s", library);
    snprintf(runtime, sizeof runtime, "%s/runtime.o", directory);
    argv[n++] = output;
    argv[n++] = object;
    /* The _fltused that objects using floating point reference and the
     * __chkstk that frames larger than a page call. */
    argv[n++] = runtime;
    for (int i = 0; i < count; i++) {
        bool listed = false;
        for (int j = 0; j < i; j++) {
            listed = listed || !strcmp(calls[i].symbol, calls[j].symbol);
        }
        if (!listed) {
            snprintf(
                exports[exported],
                sizeof exports[exported],
                "/export:%s",
                calls[i].symbol
            );
            argv[n++] = exports[exported++];
        }
    }
    argv[n] = NULL;
#else
    (void)calls;
    (void)count;
    const char *const argv[] = { linker, "-dynamiclib", object,
                                 "-o",   library,       NULL };
#endif
    int status = run(argv);
    if (status) {
        fprintf(stderr, "%s: linking %s gave status %d\n", TARGET, object, status);
    }
    return !status;
}

/* Loads `library` and gives each call's value, or false when it cannot. */
static bool call_library(const char *library, const bundle_call *calls, int count, int64_t *values)
{
#ifdef _WIN32
    HMODULE opened = LoadLibraryA(library);
#else
    void *opened = dlopen(library, RTLD_NOW | RTLD_LOCAL);
#endif
    if (!opened) {
        fprintf(stderr, "%s: cannot load %s\n", TARGET, library);
        return false;
    }
    bool found = true;
    for (int i = 0; found && i < count; i++) {
#ifdef _WIN32
        void *function = (void *)GetProcAddress(opened, calls[i].symbol);
#else
        void *function = dlsym(opened, calls[i].symbol);
#endif
        found = function;
        if (!found) {
            fprintf(
                stderr,
                "%s: %s has no symbol %s\n",
                TARGET,
                library,
                calls[i].symbol
            );
            break;
        }
        values[i] = strncmp(calls[i].symbol, "probe_", strlen("probe_"))
                      ? ((scalar_entry)function)()
                      : ((scalar_adapter)function)(calls[i].arguments);
    }
#ifdef _WIN32
    FreeLibrary(opened);
#else
    dlclose(opened);
#endif
    return found;
}

/* Links, loads and calls module `name`, then checks each call's value. */
static bool module_runs(const char *name, const bundle_call *calls, int count)
{
    char object[PATH_BYTES], library[PATH_BYTES];
    snprintf(object, sizeof object, "%s/%s", directory, name);
    snprintf(library, sizeof library, "%s/%.*s.%s", directory, (int)(strlen(name) - 2), name, LIBRARY);
    REQUIRE(link_library(object, calls, count, library));
    int64_t *values = malloc(sizeof *values * (size_t)count);
    assert(values);
    bool called = call_library(library, calls, count, values);
    bool agreed = called;
    for (int i = 0; called && i < count; i++) {
        if (values[i] != calls[i].expected) {
            fprintf(
                stderr,
                "%s %s: %s gave %" PRId64 ", expected %" PRId64 "\n",
                TARGET,
                name,
                calls[i].symbol,
                values[i],
                calls[i].expected
            );
            agreed = false;
        }
    }
    free(values);
    remove(library);
    REQUIRE(agreed);
    modules++;
    calls_run += count;
    return true;
}

/* Reads one call line after its "call " prefix. */
static bool read_call(const char *line, bundle_call *call)
{
    int used = 0;
    REQUIRE(
        sscanf(line, "%63s %" SCNd64 " %d%n", call->symbol, &call->expected, &call->count, &used) == 3
    );
    REQUIRE(call->count >= 0 && call->count <= MAX_ARGUMENTS);
    for (int a = 0; a < call->count; a++) {
        int more = 0;
        REQUIRE(sscanf(line + used, " %" SCNd64 "%n", &call->arguments[a], &more) == 1);
        used += more;
    }
    return true;
}

TEST_CASE(every_module_runs_as_on_linux)
{
    char list[PATH_BYTES];
    snprintf(directory, sizeof directory, "%s/%s", bundle, TARGET);
    snprintf(list, sizeof list, "%s/modules.txt", directory);
    FILE *file = fopen(list, "r");
    REQUIRE(file);
    bundle_call *calls = malloc(sizeof *calls * MAX_CALLS);
    assert(calls);
    char line[512], module[256] = "";
    int count = 0;
    bool ran = true;
    while (ran && fgets(line, sizeof line, file)) {
        REQUIRE(strchr(line, '\n'));
        if (!strncmp(line, "call ", 5)) {
            ran = *module && count < MAX_CALLS &&
                  read_call(line + 5, &calls[count++]);
            continue;
        }
        ran = !strncmp(line, "module ", 7) &&
              (!*module || module_runs(module, calls, count));
        snprintf(module, sizeof module, "%.*s", (int)strcspn(line + 7, "\n"), line + 7);
        count = 0;
    }
    ran = ran && *module && module_runs(module, calls, count);
    fclose(file);
    free(calls);
    printf("%s: %d modules and %d calls ran\n", TARGET, modules, calls_run);
    REQUIRE(ran);
    return true;
}

/* Runs each program in the bundle's programs.txt, a line `program NAME
 * STATUS` each, and checks its exit status. */
TEST_CASE(every_program_exits_as_listed)
{
    char list[PATH_BYTES];
    snprintf(directory, sizeof directory, "%s/%s", bundle, TARGET);
    snprintf(list, sizeof list, "%s/programs.txt", directory);
    FILE *file = fopen(list, "r");
    REQUIRE(file);
    char line[512];
    int programs = 0;
    bool exited = true;
    while (exited && fgets(line, sizeof line, file)) {
        char name[256], program[PATH_BYTES];
        int expected;
        REQUIRE(sscanf(line, "program %255s %d", name, &expected) == 2);
        snprintf(program, sizeof program, "%s/programs/%s", directory, name);
#ifndef _WIN32
        /* Artifacts keep no file modes. */
        REQUIRE(!chmod(program, 0755));
#endif
        const char *const argv[] = { program, NULL };
        int status = run(argv);
        if (status != expected) {
            fprintf(stderr, "%s: %s exited with %d, expected %d\n", TARGET, name, status, expected);
        }
        exited = status == expected;
        programs++;
    }
    fclose(file);
    printf("%s: %d programs ran\n", TARGET, programs);
    REQUIRE(exited && programs);
    return true;
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: %s BUNDLE LINKER\n", argv[0]);
        return 2;
    }
    bundle = argv[1];
    linker = argv[2];
    pu_display_time(true);
    RUN_TEST_CASE(every_module_runs_as_on_linux);
    RUN_TEST_CASE(every_program_exits_as_listed);
    pu_print_stats();
    return pu_test_failed() ? 1 : 0;
}
