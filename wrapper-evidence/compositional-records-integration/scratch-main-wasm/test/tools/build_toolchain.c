#include "host/build_host.h"
#include "support/sha256.h"
#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <unistd.h>

static const char *worker_path, *host_path;
static chibi_build_compiler *build_compiler;
static char suite[256], root[512], program_path[1024];
static char git_program[1024];
static char *program;
static char last_stderr[CHIBI_BUILD_OUTPUT_CAPACITY];
static const char *const environment_names[] = {
    "PATH",
    "CC",
    "CXX",
    "SDKROOT",
    "CMAKE_TOOLCHAIN_FILE",
    "CMAKE_GENERATOR",
    "CFLAGS",
    "CHIBI_DEPENDENCY_PACKAGES",
    "CHIBI_FETCHCONTENT_CACHE_DIR",
    "GH_TOKEN",
};
static char *saved_environment[sizeof environment_names / sizeof *environment_names];

static void path(char out[1024], const char *base, const char *name)
{
    int size = snprintf(out, 1024, "%s/%s", base, name);
    assert(size > 0 && size < 1024);
}
static void write_file(const char *name, const char *text)
{
    char file[1024];
    path(file, root, name);
    tool_write(file, text);
}
static char *read_file(const char *name)
{
    char file[1024];
    path(file, root, name);
    return tool_read(file);
}
static bool file_contains(const char *name, const char *part)
{
    char *text = read_file(name);
    bool found = text && strstr(text, part);
    free(text);
    return found;
}
static int count_in(const char *name, const char *kind)
{
    char *text = read_file(name);
    int count = 0;
    for (const char *p = text; p && (p = strstr(p, kind)); p += strlen(kind)) {
        count++;
    }
    free(text);
    return count;
}
static int trace_count(const char *kind)
{
    return count_in("cmake.trace", kind);
}
static void resolve_git()
{
    char log[1024];
    path(log, suite, "git-program.log");
#ifdef __APPLE__
    const char *arguments[] = { "xcrun", "--find", "git", NULL };
#else
    const char *arguments[] = { "sh", "-c", "command -v git", NULL };
#endif
    assert(!tool_run(arguments, suite, log));
    char *text = tool_read(log);
    assert(text);
    text[strcspn(text, "\r\n")] = 0;
    char *resolved = realpath(text, NULL);
    assert(resolved && strlen(resolved) < sizeof git_program && !access(resolved, X_OK));
    strcpy(git_program, resolved);
    free(resolved);
    free(text);
}
static void reset_fixture(const char *name, int backend)
{
    snprintf(root, sizeof root, "%s/%s %d", suite, name, backend);
    assert(!mkdir(root, 0700));
    char file[1024], bin[1024], search[4096];
    path(bin, root, "bin");
    assert(!mkdir(bin, 0700));
    const char *names[] = { "cmake", "clang", "clang++", "ninja" };
    for (int i = 0; i < 4; i++) {
        path(file, bin, names[i]);
        assert(!symlink(worker_path, file));
    }
    /* Apple's git launcher inspects SDKROOT, which is fake in this fixture.
     * Exec the previously resolved binary so authored probes and setup use it.
     * A symlink would make Git derive its resource paths from the fixture. */
    char git_path[1100];
    snprintf(git_path, sizeof git_path, "%s\n", git_program);
    write_file("bin/git-program", git_path);
    write_file("bin/git", "#!/bin/sh\nIFS= read -r program < \"${0%/*}/git-program\" || exit 1\nexec \"$program\" \"$@\"\n");
    path(file, bin, "git");
    assert(!chmod(file, 0700));
    /* Python, its runtime and sanitizer symbolizers use the original PATH. */
    snprintf(search, sizeof search, "%s:%s", bin, saved_environment[0] ? saved_environment[0] : "/usr/bin:/bin");
    assert(!setenv("PATH", search, 1));
    assert(!unsetenv("CC") && !unsetenv("CXX") && !unsetenv("CMAKE_TOOLCHAIN_FILE"));
    assert(!unsetenv("CFLAGS"));
    assert(!setenv("CMAKE_GENERATOR", "Unix Makefiles", 1));
    path(file, root, "sdk");
    assert(!mkdir(file, 0700));
    assert(!setenv("SDKROOT", file, 1));
    write_file("CMakeLists.txt", "project(controlled)\n");
    write_file("external.cmake", "# loaded input\n");
    write_file("sdk/CMakeLists.txt", "project(controlled_tools)\n");
    write_file("sdk/external.cmake", "# loaded tool input\n");
    path(file, root, "git.log");
    const char *git[] = { "git", "init", "-q", NULL };
    assert(!tool_run(git, root, file));
}
static bool invoke(const char *const *arguments, int count, int backend, int value)
{
    chibi_build_options options = {
        .compiler = build_compiler,
        .root = root,
        .source_path = program_path,
        .source_text = program,
        .entry = "Build",
        .arguments = arguments,
        .argument_count = count,
        .backend = backend ? BEHAVIOR_SLJIT : BEHAVIOR_BYTECODE,
        .deadline = chibi_build_now() + 10,
    };
    chibi_build_result result;
    bool success = chibi_build_invoke(&options, &result);
    snprintf(last_stderr, sizeof last_stderr, "%s", result.stderr_text);
    if (!success || result.value != value) {
        fprintf(
            stderr,
            "toolchain invoke: success=%d value=%d expected=%d "
            "diagnostic=%s\n%s\n%s\n",
            success,
            result.value,
            value,
            result.diagnostic,
            result.stdout_text,
            result.stderr_text
        );
    }
    return success && result.value == value && !result.counts.overflow &&
           !result.counts.inspection_bytecode_fallbacks &&
           !result.counts.wasm_entries &&
           (backend ? result.counts.sljit_entries > 0 && !result.counts.bytecode_entries
                    : result.counts.bytecode_entries > 0 && !result.counts.sljit_entries);
}

TEST_CASE(native_compiler_and_explicit_cache_precedence)
{
    for (int b = 0; b < 2; b++) {
        reset_fixture("precedence", b);
        const char *fresh[] = { "native", "fresh build", "--configure-only", "--" };
        REQUIRE(invoke(fresh, 4, b, 0));
#ifdef __linux__
        REQUIRE(file_contains("cmake.trace", "-DCMAKE_C_COMPILER=clang"));
        REQUIRE(file_contains("cmake.trace", "-DCMAKE_CXX_COMPILER=clang++"));
#else
        REQUIRE(!file_contains("cmake.trace", "-DCMAKE_C_COMPILER=clang"));
#endif
        char compiler[1024], changed[1024], sysroot[1024];
        path(compiler, root, "selected cc");
        path(changed, root, "explicit cc");
        path(sysroot, root, "explicit SDK");
        assert(!mkdir(sysroot, 0700));
        write_file("selected cc", "compiler one\n");
        write_file("explicit cc", "compiler two\n");
        assert(!setenv("CC", compiler, 1));
        assert(!setenv("CXX", compiler, 1));
        const char *environment[] = { "native", "env build", "--configure-only", "--" };
        REQUIRE(invoke(environment, 4, b, 0));
        REQUIRE(file_contains("env build/CMakeCache.txt", compiler));
        assert(!setenv("CC", changed, 1));
        assert(!setenv("CXX", changed, 1));
        REQUIRE(invoke(environment, 4, b, 0));
        REQUIRE(file_contains("env build/CMakeCache.txt", compiler));
        REQUIRE(!file_contains("env build/CMakeCache.txt", changed));
        char cc_definition[1200], sdk_definition[1200];
        snprintf(cc_definition, sizeof cc_definition, "CMAKE_C_COMPILER:FILEPATH=%s", changed);
        snprintf(sdk_definition, sizeof sdk_definition, "-DCMAKE_OSX_SYSROOT:PATH=%s", sysroot);
        const char *explicit[] = {
            "native", "env build",   "--configure-only", "--",
            "-D",     cc_definition, sdk_definition
        };
        REQUIRE(invoke(explicit, 7, b, 0));
        REQUIRE(file_contains("env build/CMakeCache.txt", changed));
        REQUIRE(file_contains("env build/CMakeCache.txt", sysroot));
        assert(!unsetenv("CC") && !unsetenv("CXX"));
        write_file("toolchain.cmake", "# explicit toolchain\n");
        const char *toolchain[] = { "native",           "toolchain build",
                                    "--configure-only", "--",
                                    "--toolchain",      "toolchain.cmake" };
        REQUIRE(invoke(toolchain, 6, b, 0));
        REQUIRE(!file_contains("toolchain build/CMakeCache.txt", "=clang\n"));
        const char *initial[] = {
            "native", "initial build",  "--configure-only", "--",
            "-C",     "toolchain.cmake"
        };
        REQUIRE(invoke(initial, 6, b, 0));
        REQUIRE(!file_contains("initial build/CMakeCache.txt", "=clang\n"));
    }
    return true;
}

TEST_CASE(warm_configuration_tracks_inputs_options_environment_and_compiler)
{
    for (int b = 0; b < 2; b++) {
        reset_fixture("cache", b);
        char compiler[1024];
        path(compiler, root, "compiler");
        write_file("compiler", "compiler identity one\n");
        assert(!chmod(compiler, 0700));
        assert(!setenv("CC", compiler, 1));
        assert(!setenv("CXX", compiler, 1));
        const char *arguments[] = {
            "native",   "cached build",
            "--target", "tool one",
            "--target", "tool-two",
            "--jobs",   "2",
            "--",       "-DNOTE=space \"quote\";$literal*"
        };
        REQUIRE(invoke(arguments, 10, b, 0));
        REQUIRE(trace_count("CONFIGURE\n") == 1 && trace_count("BUILD\n") == 1);
        REQUIRE(file_contains("cmake.trace", "-DNOTE=space \"quote\";$literal*"));
        REQUIRE(file_contains("cmake.trace", "tool one"));
        REQUIRE(invoke(arguments, 10, b, 0));
        REQUIRE(trace_count("CONFIGURE\n") == 1 && trace_count("BUILD\n") == 2);
        write_file("CMakeLists.txt", "project(changed)\n");
        REQUIRE(invoke(arguments, 10, b, 0));
        REQUIRE(trace_count("CONFIGURE\n") == 2);
        write_file("external.cmake", "# changed external input\n");
        REQUIRE(invoke(arguments, 10, b, 0));
        REQUIRE(trace_count("CONFIGURE\n") == 3);
        assert(!setenv("CFLAGS", "-DCHANGED", 1));
        REQUIRE(invoke(arguments, 10, b, 0));
        REQUIRE(trace_count("CONFIGURE\n") == 4);
        const char *changed[] = { "native", "cached build", "--", "-DNOTE=second" };
        REQUIRE(invoke(changed, 4, b, 0));
        REQUIRE(trace_count("CONFIGURE\n") == 5);
        write_file("compiler", "compiler identity changed\n");
        REQUIRE(invoke(changed, 4, b, 0));
        REQUIRE(trace_count("CONFIGURE\n") == 6);
        REQUIRE(invoke(changed, 4, b, 0));
        REQUIRE(trace_count("CONFIGURE\n") == 6 && trace_count("BUILD\n") == 8);
    }
    return true;
}

TEST_CASE(configuration_and_build_failures_propagate_and_tool_host_profile_is_narrow)
{
    for (int b = 0; b < 2; b++) {
        reset_fixture("failures", b);
        const char *arguments[] = { "tool-host", "tools build",
                                    "--target",  "chibi-build-host",
                                    "--jobs",    "2",
                                    "--" };
        write_file("fail-configure", "");
        REQUIRE(invoke(arguments, 7, b, 37));
        REQUIRE(trace_count("CONFIGURE\n") == 1 && !trace_count("BUILD\n"));
        char file[1024];
        path(file, root, "fail-configure");
        REQUIRE(!unlink(file));
        REQUIRE(invoke(arguments, 7, b, 0));
        REQUIRE(trace_count("CONFIGURE\n") == 2 && trace_count("BUILD\n") == 1);
        REQUIRE(file_contains("cmake.trace", "-DCHIBI_SDK_BUILD_COMPILER=ON"));
        REQUIRE(file_contains("cmake.trace", "-DCHIBI_SDK_BUILD_NATIVE=OFF"));
        REQUIRE(file_contains("cmake.trace", "-DCHIBI_SDK_BUILD_DESKTOP=OFF"));
        write_file("fail-build", "");
        REQUIRE(invoke(arguments, 7, b, 38));
        REQUIRE(trace_count("CONFIGURE\n") == 2 && trace_count("BUILD\n") == 2);
    }
    return true;
}

TEST_CASE(configuration_ignores_retired_dependency_switches)
{
    for (int b = 0; b < 2; b++) {
        reset_fixture("retired switches", b);
        /* The dependency package and FetchContent cache are gone; their
         * switches select nothing and configuration reads no cache files. */
        assert(!setenv("CHIBI_DEPENDENCY_PACKAGES", "1", 1));
        assert(!setenv("CHIBI_FETCHCONTENT_CACHE_DIR", "/dev/null/chibi-no-cache", 1));
        const char *arguments[] = { "native", "retired build", "--configure-only", "--" };
        REQUIRE(invoke(arguments, 4, b, 0));
        char cache[1100];
        snprintf(cache, sizeof cache, "-DCHIBI_THIRD_PARTY_CACHE=%s/.git/chibi/third_party", root);
        REQUIRE(file_contains("cmake.trace", cache));
        REQUIRE(!file_contains("cmake.trace", ".chibi-fetchcontent-cache.cmake"));
        REQUIRE(!file_contains("cmake.trace", ".chibi-dependency-package.cmake"));
        REQUIRE(!read_file("retired build/.chibi-dependency-package.cmake"));
        assert(!unsetenv("CHIBI_DEPENDENCY_PACKAGES") && !unsetenv("CHIBI_FETCHCONTENT_CACHE_DIR"));
    }
    return true;
}

/* The archive target third_party/archives.tsv names for this host. */
static void host_target(char out[32])
{
    struct utsname host;
    assert(!uname(&host));
    bool arm = !strcmp(host.machine, "arm64") || !strcmp(host.machine, "aarch64");
    snprintf(out, 32, "%s-%s", !strcmp(host.sysname, "Darwin") ? "macos" : "linux", arm ? "arm64" : "x64");
}
static void write_manifest(const char *target, const char *sha256)
{
    char text[512];
    snprintf(
        text,
        sizeof text,
        "# repository owner/chibi\n# target\tvariant\trelease\tsha256\n"
        "%s\trelease\tfixture-1\t%s\n"
        "windows-x64\trelease\tfixture-1\t%064d\n",
        target,
        sha256,
        1
    );
    write_file("third_party/archives.tsv", text);
}
/* Whether the fixture clone's archive store holds exactly these entries. */
static bool store_holds(const char *const *entries, int count)
{
    char directory[1024];
    path(directory, root, ".git/chibi/third_party");
    DIR *dir = opendir(directory);
    assert(dir);
    int found = 0, total = 0;
    for (struct dirent *item; (item = readdir(dir));) {
        if (!strcmp(item->d_name, ".") || !strcmp(item->d_name, "..")) {
            continue;
        }
        total++;
        for (int i = 0; i < count; i++) {
            found += !strcmp(item->d_name, entries[i]);
        }
    }
    assert(!closedir(dir));
    return found == count && total == count;
}
/* Packs a one-file archive as gh-asset, which the gh stand-in serves, links
 * that stand-in into the fixture's bin and returns the archive's SHA-256. */
static void fixture_archive(char sha256[65])
{
    char file[1024], tree[1024], log[1024];
    path(file, root, "bin/gh");
    assert(!symlink(worker_path, file));
    path(tree, root, "asset tree");
    assert(!mkdir(tree, 0700));
    write_file("asset tree/third-party.cmake", "# fixture archive\n");
    path(file, root, "gh-asset");
    path(log, root, "tar.log");
    const char *pack[] = { "tar", "-czf", file, "-C", tree, ".", NULL };
    assert(!tool_run(pack, root, log));
    FILE *asset = fopen(file, "rb");
    assert(asset);
    static char bytes[65536];
    size_t size = fread(bytes, 1, sizeof bytes, asset);
    assert(size > 0 && size < sizeof bytes && !fclose(asset));
    sha256_hex(bytes, size, sha256);
    path(file, root, "third_party");
    assert(!mkdir(file, 0700));
}

/* gh stands in for the release download (policy_worker copies gh-asset) and
 * records the GH_TOKEN it receives. An archive downloads once for this host's
 * target, unpacks under its SHA-256 and is reused; another target's archive is
 * not fetched; an archive whose SHA-256 differs is refused and leaves nothing
 * behind. */
TEST_CASE(third_party_archives_download_once_and_refuse_another_hash)
{
    for (int b = 0; b < 2; b++) {
        reset_fixture("third-party archives", b);
        char file[1024], target[32], sha256[65], expected[1100];
        fixture_archive(sha256);
        host_target(target);
        write_manifest(target, sha256);
        /* gh authenticates with GH_TOKEN, which reaches it through the host. */
        assert(!setenv("GH_TOKEN", "fixture-token", 1));
        const char *arguments[] = { "native", "archive build", "--configure-only", "--" };
        REQUIRE(invoke(arguments, 4, b, 0));
        snprintf(expected, sizeof expected, "gh release download fixture-1 --repo owner/chibi --pattern third-party-%s-release.tar.gz", target);
        REQUIRE(file_contains("gh.trace", expected));
        REQUIRE(file_contains("gh.trace", " token=fixture-token\n"));
        snprintf(file, sizeof file, ".git/chibi/third_party/%s/third-party.cmake", sha256);
        REQUIRE(file_contains(file, "# fixture archive\n"));
        const char *stored[] = { sha256 };
        REQUIRE(store_holds(stored, 1));
        REQUIRE(invoke(arguments, 4, b, 0));
        REQUIRE(count_in("gh.trace", "gh release download") == 1);

        write_manifest(target, "0000000000000000000000000000000000000000000000000000000000000000");
        REQUIRE(invoke(arguments, 4, b, 1));
        REQUIRE(strstr(last_stderr, "does not match the SHA-256"));
        REQUIRE(count_in("gh.trace", "gh release download") == 2);
        REQUIRE(store_holds(stored, 1));
        assert(!unsetenv("GH_TOKEN"));
    }
    return true;
}

/* For a build the Chibi graph compiles, the third-party mode prepares the
 * archives this host's target pins, once, and runs no CMake. */
TEST_CASE(third_party_mode_prepares_archives_without_cmake)
{
    for (int b = 0; b < 2; b++) {
        reset_fixture("third-party mode", b);
        char file[1024], target[32], sha256[65];
        fixture_archive(sha256);
        host_target(target);
        write_manifest(target, sha256);
        const char *arguments[] = { "third-party", "graph build" };
        REQUIRE(invoke(arguments, 2, b, 0));
        REQUIRE(invoke(arguments, 2, b, 0));
        REQUIRE(count_in("gh.trace", "gh release download") == 1);
        snprintf(file, sizeof file, ".git/chibi/third_party/%s/third-party.cmake", sha256);
        REQUIRE(file_contains(file, "# fixture archive\n"));
        path(file, root, "cmake.trace");
        REQUIRE(access(file, F_OK));
    }
    return true;
}

/* A configuration that starts from CMake alone loads cmake/third_party.cmake,
 * which downloads a missing archive through gh into the clone's store, leaves
 * no partial download behind and reuses the archive in another build. */
TEST_CASE(cmake_loader_downloads_a_missing_archive_once)
{
    reset_fixture("CMake archive", 0);
    char file[1024], gh[1100], sha256[65], target[32], log[1024];
    fixture_archive(sha256);
    host_target(target);
    write_manifest(target, sha256);
    path(file, tool_root(), "cmake/third_party.cmake");
    char *loader = tool_read(file);
    assert(loader);
    path(file, root, "cmake");
    assert(!mkdir(file, 0700));
    write_file("cmake/third_party.cmake", loader);
    free(loader);
    path(file, root, "loader");
    assert(!mkdir(file, 0700));
    write_file("loader/CMakeLists.txt", "cmake_minimum_required(VERSION 3.25)\nproject(loader NONE)\ninclude(../cmake/third_party.cmake)\n");
    /* The real CMake, from the original PATH; gh is the fixture's stand-in. */
    assert(!setenv("PATH", saved_environment[0] ? saved_environment[0] : "/usr/bin:/bin", 1));
    snprintf(gh, sizeof gh, "-DCHIBI_GH=%s/bin/gh", root);
    path(log, root, "loader.log");
    const char *first[] = { "cmake",        "-S", "loader", "-B",
                            "loader build", gh,   NULL };
    REQUIRE(!tool_run(first, root, log));
    snprintf(file, sizeof file, ".git/chibi/third_party/%s/third-party.cmake", sha256);
    REQUIRE(file_contains(file, "# fixture archive\n"));
    const char *stored[] = { sha256 };
    REQUIRE(store_holds(stored, 1));
    const char *second[] = { "cmake",         "-S", "loader", "-B",
                             "another build", gh,   NULL };
    REQUIRE(!tool_run(second, root, log));
    REQUIRE(count_in("gh.trace", "gh release download") == 1);
    return true;
}

/* The archive store keeps what any worktree of the clone pins and a download
 * in progress, and removes the rest. */
TEST_CASE(third_party_store_keeps_what_worktrees_pin)
{
    for (int b = 0; b < 2; b++) {
        reset_fixture("third-party pruning", b);
        char sha256[65], target[32], other[1024], log[1024], pin[65], stray[65],
            partial[96];
        fixture_archive(sha256);
        host_target(target);
        write_manifest(target, sha256);
        path(log, root, "git.log");
        path(other, root, "other worktree");
        const char *commit[] = { "git",
                                 "-c",
                                 "user.name=fixture",
                                 "-c",
                                 "user.email=fixture@example.com",
                                 "commit",
                                 "-q",
                                 "--allow-empty",
                                 "-m",
                                 "fixture",
                                 NULL };
        const char *add[] = { "git",      "worktree", "add", "-q",
                              "--detach", other,      NULL };
        assert(!tool_run(commit, root, log) && !tool_run(add, root, log));
        snprintf(pin, sizeof pin, "%064d", 2);
        snprintf(stray, sizeof stray, "%064d", 3);
        snprintf(partial, sizeof partial, "%s.download.partial", stray);
        char text[256], file[1100], directory[1024];
        snprintf(text, sizeof text, "%s\trelease\tfixture-1\t%s\n", target, pin);
        path(directory, other, "third_party");
        assert(!mkdir(directory, 0700));
        write_file("other worktree/third_party/archives.tsv", text);
        path(directory, root, ".git/chibi");
        assert(!mkdir(directory, 0700));
        path(directory, root, ".git/chibi/third_party");
        assert(!mkdir(directory, 0700));
        const char *seeded[] = { pin, stray, partial };
        for (int i = 0; i < 3; i++) {
            snprintf(file, sizeof file, ".git/chibi/third_party/%s", seeded[i]);
            path(directory, root, file);
            assert(!mkdir(directory, 0700));
            snprintf(file, sizeof file, ".git/chibi/third_party/%s/third-party.cmake", seeded[i]);
            write_file(file, "# cached\n");
        }
        const char *arguments[] = { "native", "pruned build", "--configure-only", "--" };
        REQUIRE(invoke(arguments, 4, b, 0));
        const char *stored[] = { sha256, pin, partial };
        REQUIRE(store_holds(stored, 3));
    }
    return true;
}

#ifdef __APPLE__
static bool configured_sysroot(const char *build, const char *sdk, int backend)
{
    const char *arguments[] = { "native", build, "--configure-only", "--" };
    char cache[1024], expected[2048];
    snprintf(cache, sizeof cache, "%s/CMakeCache.txt", build);
    snprintf(expected, sizeof expected, "CMAKE_OSX_SYSROOT:PATH=%s\n", sdk);
    return invoke(arguments, 4, backend, 0) && file_contains(cache, expected);
}
static bool driver_ran(const char *arguments)
{
    char file[1024];
    path(file, root, "driver.trace");
    return file_contains("driver.trace", arguments) && !unlink(file);
}

/* Without SDKROOT or a cached sysroot, the selected compiler's -### report
 * names the SDK, followed through links; xcrun answers otherwise. CC is one
 * file or shell words. Both stand-ins sit first on PATH. Steps alternate
 * bytecode and SLJIT, so each backend parses quoted reports and falls back. */
TEST_CASE(compiler_driver_report_names_the_sdk_before_xcrun)
{
    reset_fixture("compiler SDK", 0);
    assert(!unsetenv("SDKROOT"));
    char file[1024], directory[1024], sdk[1024], fallback[1024], text[1200];
    const char *shims[] = { "bin/cc", "bin/xcrun", "driver dir/my cc" };
    path(directory, root, "driver dir");
    assert(!mkdir(directory, 0700));
    for (int i = 0; i < 3; i++) {
        path(file, root, shims[i]);
        assert(!symlink(worker_path, file));
    }
    path(directory, root, "SDKs");
    assert(!mkdir(directory, 0700));
    path(sdk, directory, "Fixture \"quoted\" \\ 26.sdk");
    assert(!mkdir(sdk, 0700));
    path(file, directory, "Fixture.sdk");
    assert(!symlink("Fixture \"quoted\" \\ 26.sdk", file));
    write_file("compiler-sysroot", file);
    path(fallback, directory, "Fallback.sdk");
    assert(!mkdir(fallback, 0700));
    snprintf(text, sizeof text, "%s\n", fallback);
    write_file("xcrun-sdk", text);

    REQUIRE(configured_sysroot("default", sdk, 0));
    REQUIRE(driver_ran("arg=cc\narg=-###\narg=-x\narg=c\narg=-fsyntax-only\narg=-\n"));
    assert(!setenv("CC", "cc  -fflag\t'two words' \"say \\\"hi\\\"\" a\\ b", 1));
    REQUIRE(configured_sysroot("words", sdk, 1));
    REQUIRE(driver_ran("arg=cc\narg=-fflag\narg=two words\narg=say \"hi\"\narg=a b\narg=-###\n"));
    path(file, root, "driver dir/my cc");
    assert(!setenv("CC", file, 1));
    REQUIRE(configured_sysroot("file", sdk, 0));
    snprintf(text, sizeof text, "arg=%s\narg=-###\n", file);
    REQUIRE(driver_ran(text));

    path(file, directory, "Missing.sdk");
    write_file("compiler-sysroot", file);
    assert(!unsetenv("CC"));
    REQUIRE(configured_sysroot("missing", fallback, 1));
    REQUIRE(driver_ran("arg=cc\narg=-###\n"));
    path(file, root, "compiler-sysroot");
    assert(!unlink(file));
    REQUIRE(configured_sysroot("unnamed", fallback, 0));
    REQUIRE(driver_ran("arg=cc\narg=-###\n"));
    const char *unusable[] = { "absent-compiler", "cc 'unclosed" };
    for (int i = 0; i < 2; i++) {
        assert(!setenv("CC", unusable[i], 1));
        REQUIRE(configured_sysroot(i ? "unclosed" : "absent", fallback, 1 - i));
        path(file, root, "driver.trace");
        REQUIRE(access(file, F_OK));
    }
    return true;
}
#endif

TEST_CASE(private_cli_forwards_empty_and_special_tail_arguments)
{
    reset_fixture("CLI tail", 0);
    char log[1024];
    path(log, root, "cli.log");
    /* Test the CLI argument boundary without compiling the whole toolchain
     * under its five-second operation deadline. The cases above exercise
     * Build.chibi directly on both backends. */
    const char *arguments[] = {
        host_path,
        "--compiler",
        CHIBI_SOURCE_ROOT "/tools/compiler",
        "--compiler-core",
        CHIBI_SOURCE_ROOT "/packages/core",
        "--root",
        root,
        "--source",
        CHIBI_SOURCE_ROOT "/test/language/BuildCLITail.chibi",
        "--entry",
        "Build",
        "--timeout-ms",
        "5000",
        "--",
        "native",
        "CLI build",
        "--configure-only",
        "--",
        "-DNOTE=space \"quote\";$literal*",
        "",
        NULL,
    };
    REQUIRE(!tool_run(arguments, root, log));
    REQUIRE(trace_count("CONFIGURE\n") == 1 && !trace_count("BUILD\n"));
    REQUIRE(file_contains("cmake.trace", "-DNOTE=space \"quote\";$literal*"));
    REQUIRE(file_contains("cmake.trace", "arg=0:\n"));
    return true;
}

TEST_CASE(make_recovers_missing_or_old_hosts_and_selects_current_protocol)
{
    reset_fixture("Make routing", 0);
    char source[1024], makefile[1024], original[1024], overrides[1024],
        selected[1024], log[1024];
    path(source, root, "source with spaces");
    assert(!mkdir(source, 0700));
    path(makefile, source, "Makefile");
    path(original, tool_root(), "Makefile");
    assert(!symlink(original, makefile));
    path(overrides, root, "overrides with spaces.mk");
    path(selected, root, "selected branch");
    path(log, root, "make.log");
    char recipes[4096];
    snprintf(
        recipes,
        sizeof recipes,
        ".PHONY: stage-zero-build toolchain-build\n"
        "stage-zero-build:\n\t@printf 'recovery\\n' > '%s'\n"
        "toolchain-build:\n\t@printf 'authored\\n' > '%s'\n",
        selected,
        selected
    );
    tool_write(overrides, recipes);
    char old[1024], previous[1024], current[1024], missing[1024];
    path(old, root, "old host");
    path(previous, root, "protocol-13 host");
    path(current, root, "current host");
    path(missing, root, "missing host");
    assert(!symlink(worker_path, old));
    assert(!symlink(worker_path, previous));
    assert(!symlink(host_path, current));
    char recursive_make[4096];
    snprintf(recursive_make, sizeof recursive_make, "MAKE=make -f '%s' -f '%s'", makefile, overrides);
    /* A protocol-13 host loads the compiler but retains native results until
     * teardown, so a warm graph can exhaust storage before rebuilding it. */
    const char *hosts[] = { missing, old, previous, current };
    for (int i = 0; i < 4; i++) {
        char host_option[1200];
        snprintf(host_option, sizeof host_option, "BUILD_HOST=%s", hosts[i]);
        const char *arguments[] = {
            "make",         "-f",        makefile,
            "-f",           overrides,   "build",
            recursive_make, host_option, "MAKEFLAGS=--no-print-directory",
            "TEST_ARGS=",   NULL,
        };
        REQUIRE(!tool_run(arguments, source, log));
        char *choice = tool_read(selected);
        bool correct = choice && !strcmp(choice, i == 3 ? "authored\n" : "recovery\n");
        free(choice);
        REQUIRE(correct);
        REQUIRE(!unlink(selected));
    }
    return true;
}

void test_build_toolchain(const char *worker, const char *host)
{
    host_path = host;
    char error[256] = { 0 };
    build_compiler = chibi_build_compiler_load(
        CHIBI_SOURCE_ROOT "/tools/compiler",
        CHIBI_SOURCE_ROOT "/packages/core",
        error,
        sizeof error
    );
    if (!build_compiler) {
        fprintf(stderr, "build compiler: %s\n", error);
    }
    assert(build_compiler);

    worker_path = worker;
    strcpy(suite, "/tmp/chibi-toolchain-XXXXXX");
    assert(mkdtemp(suite));
    char *resolved = realpath(suite, NULL);
    assert(resolved && strlen(resolved) < sizeof suite);
    strcpy(suite, resolved);
    free(resolved);
    snprintf(program_path, sizeof program_path, "%s/tools/toolchain/Build.chibi", tool_root());
    program = tool_read(program_path);
    assert(program);
    for (size_t i = 0; i < sizeof environment_names / sizeof *environment_names; i++) {
        const char *value = getenv(environment_names[i]);
        saved_environment[i] = value ? strdup(value) : NULL;
    }
    resolve_git();
    RUN_TEST_CASE(native_compiler_and_explicit_cache_precedence);
    RUN_TEST_CASE(warm_configuration_tracks_inputs_options_environment_and_compiler);
    RUN_TEST_CASE(configuration_and_build_failures_propagate_and_tool_host_profile_is_narrow);
    RUN_TEST_CASE(configuration_ignores_retired_dependency_switches);
    RUN_TEST_CASE(third_party_archives_download_once_and_refuse_another_hash);
    RUN_TEST_CASE(third_party_mode_prepares_archives_without_cmake);
    RUN_TEST_CASE(cmake_loader_downloads_a_missing_archive_once);
    RUN_TEST_CASE(third_party_store_keeps_what_worktrees_pin);

#ifdef __APPLE__
    RUN_TEST_CASE(compiler_driver_report_names_the_sdk_before_xcrun);
#endif
    RUN_TEST_CASE(private_cli_forwards_empty_and_special_tail_arguments);
    RUN_TEST_CASE(make_recovers_missing_or_old_hosts_and_selects_current_protocol);
    for (size_t i = 0; i < sizeof environment_names / sizeof *environment_names; i++) {
        if (saved_environment[i]) {
            assert(!setenv(environment_names[i], saved_environment[i], 1));
        } else {
            assert(!unsetenv(environment_names[i]));
        }
        free(saved_environment[i]);
    }
    chibi_build_compiler_free(build_compiler);
    build_compiler = NULL;
    free(program);
    tool_remove_tree(suite);
}
