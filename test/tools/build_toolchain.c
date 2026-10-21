#include "host/build_host.h"
#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *worker_path, *host_path;
static char suite[256], root[512], program_path[1024];
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
static int trace_count(const char *kind)
{
    char *text = read_file("cmake.trace");
    int count = 0;
    for (const char *p = text; p && (p = strstr(p, kind)); p += strlen(kind)) {
        count++;
    }
    free(text);
    return count;
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
    /* CMake is the only executable shim selected. Python and its runtime,
     * sanitizer symbolizers, and git come from the original explicit PATH. */
    snprintf(search, sizeof search, "%s:%s", bin, saved_environment[0] ? saved_environment[0] : "/usr/bin:/bin");
    assert(!setenv("PATH", search, 1));
    assert(!unsetenv("CC") && !unsetenv("CXX") && !unsetenv("CMAKE_TOOLCHAIN_FILE"));
    assert(!unsetenv("CFLAGS"));
    assert(!setenv("CMAKE_GENERATOR", "Unix Makefiles", 1));
    assert(!setenv("CHIBI_DEPENDENCY_PACKAGES", "0", 1));
    assert(!setenv("CHIBI_FETCHCONTENT_CACHE_DIR", "/dev/null/chibi-no-cache", 1));
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
        /* A tighter inherited supervisor deadline changes execution metadata,
         * without granting any process additional aggregate time. */
        const char *inherited = getenv("CHIBI_TEST_DEADLINE");
        char *saved_deadline = inherited ? strdup(inherited) : NULL;
        double deadline = chibi_build_now() + 5;
        if (inherited && strtod(inherited, NULL) < deadline) {
            deadline = strtod(inherited, NULL);
        }
        char deadline_text[64];
        snprintf(deadline_text, sizeof deadline_text, "%.9f", deadline);
        assert(!setenv("CHIBI_TEST_DEADLINE", deadline_text, 1));
        bool warm = invoke(changed, 4, b, 0);
        if (saved_deadline) {
            assert(!setenv("CHIBI_TEST_DEADLINE", saved_deadline, 1));
        } else {
            assert(!unsetenv("CHIBI_TEST_DEADLINE"));
        }
        free(saved_deadline);
        REQUIRE(warm);
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

TEST_CASE(private_source_overrides_disable_immutable_dependency_packages)
{
    for (int b = 0; b < 2; b++) {
        reset_fixture("private dependencies", b);
        assert(!setenv("CHIBI_DEPENDENCY_PACKAGES", "1", 1));
        char definition[1200];
        snprintf(definition, sizeof definition, "FETCHCONTENT_SOURCE_DIR_TEST:PATH=%s/external checkout", root);
        const char *split[] = { "native", "split build", "--configure-only",
                                "--",     "-D",          definition };
        REQUIRE(invoke(split, 6, b, 0));
        REQUIRE(
            file_contains("split build/.chibi-dependency-package.cmake", "unset(CHIBI_DEPENDENCY_PACKAGE CACHE)")
        );
        char build[1024], cache[4096];
        path(build, root, "cached override");
        assert(!mkdir(build, 0700));
        snprintf(
            cache,
            sizeof cache,
            "CMAKE_GENERATOR:INTERNAL=Unix Makefiles\n"
            "FETCHCONTENT_SOURCE_DIR_TEST:PATH=%s/external checkout\n",
            root
        );
        write_file("cached override/CMakeCache.txt", cache);
        const char *cached[] = { "native", "cached override", "--configure-only", "--" };
        REQUIRE(invoke(cached, 4, b, 0));
        REQUIRE(
            file_contains("cached override/.chibi-dependency-package.cmake", "unset(CHIBI_DEPENDENCY_PACKAGE CACHE)")
        );
        REQUIRE(trace_count("CONFIGURE\n") == 2 && !trace_count("BUILD\n"));
    }
    return true;
}

static void make_build(const char *name, const char *cache)
{
    char directory[1024], file[1100];
    path(directory, root, name);
    assert(!mkdir(directory, 0700));
    snprintf(file, sizeof file, "%s/CMakeCache.txt", name);
    write_file(file, cache);
}
/* Dependency preparation runs only when configuration chooses packages. Here
 * it fails at once: a multi-configuration generator or a private override is
 * not packageable. */
static bool packages_chosen(const char *const *arguments, int count, int backend)
{
    char file[1100];
    snprintf(file, sizeof file, "%s/%s/.chibi-dependency-package.cmake", root, arguments[1]);
    return invoke(arguments, count, backend, 1) &&
           strstr(last_stderr, "dependency preparation failed") &&
           access(file, F_OK);
}
static bool packages_declined(const char *const *arguments, int count, int backend)
{
    char file[1100];
    snprintf(file, sizeof file, "%s/.chibi-dependency-package.cmake", arguments[1]);
    return invoke(arguments, count, backend, 0) &&
           file_contains(file, "unset(CHIBI_DEPENDENCY_PACKAGE CACHE)");
}

TEST_CASE(dependency_overrides_follow_definitions_then_cache)
{
    for (int b = 0; b < 2; b++) {
        reset_fixture("dependency overrides", b);
        assert(!setenv("CHIBI_DEPENDENCY_PACKAGES", "1", 1));
        make_build(
            "inside",
            "CMAKE_GENERATOR:INTERNAL=Unix Makefiles\n"
            "FETCHCONTENT_SOURCE_DIR_TEST:PATH=inside/_deps/test-src\n"
        );
        char directory[1024];
        path(directory, root, "inside/_deps");
        assert(!mkdir(directory, 0700));
        path(directory, root, "inside/_deps/test-src");
        assert(!mkdir(directory, 0700));
        const char *inside[] = { "native", "inside", "--configure-only", "--", "-DCMAKE_GENERATOR=Xcode" };
        REQUIRE(packages_chosen(inside, 5, b));

        const char *sljit_cache = "CMAKE_GENERATOR:INTERNAL=Unix Makefiles\n"
                                  "CHIBI_SJLIT_SOURCE_DIR:PATH=/opt/sljit\n";
        make_build("cached", sljit_cache);
        const char *cached[] = { "native", "cached", "--configure-only", "--", "-DCMAKE_GENERATOR=Xcode" };
        REQUIRE(packages_declined(cached, 5, b));
        make_build("cleared", sljit_cache);
        const char *cleared[] = {
            "native",
            "cleared",
            "--configure-only",
            "--",
            "-DCHIBI_SJLIT_SOURCE_DIR:PATH=",
            "-DCMAKE_GENERATOR=Xcode",
        };
        REQUIRE(packages_chosen(cleared, 6, b));

        const char *valueless[] = {
            "native",
            "valueless",
            "--configure-only",
            "--",
            "-DFETCHCONTENT_FULLY_DISCONNECTED",
            "-DCMAKE_GENERATOR=Xcode",
        };
        REQUIRE(packages_chosen(valueless, 6, b));
        const char *quiet[] = { "native", "quiet", "--configure-only", "--", "-DFETCHCONTENT_QUIET=OFF" };
        REQUIRE(packages_declined(quiet, 5, b));

        const char *dangling[] = { "native",
                                   "dangling",
                                   "--configure-only",
                                   "--",
                                   "-DCMAKE_C_COMPILER=clang",
                                   "-D" };
        REQUIRE(invoke(dangling, 6, b, 1));
#ifdef __APPLE__
        /* The SDK definition configuration appends completes the dangling -D. */
        REQUIRE(strstr(last_stderr, "dependency preparation failed"));
#else
        REQUIRE(strstr(last_stderr, "-D requires a definition"));
#endif
        REQUIRE(trace_count("CONFIGURE\n") == 2 && !trace_count("BUILD\n"));
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
    const char *arguments[] = {
        host_path,
        "--root",
        root,
        "--source",
        program_path,
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
    path(previous, root, "protocol-4 host");
    path(current, root, "current host");
    path(missing, root, "missing host");
    assert(!symlink(worker_path, old));
    assert(!symlink(worker_path, previous));
    assert(!symlink(host_path, current));
    char recursive_make[4096];
    snprintf(recursive_make, sizeof recursive_make, "MAKE=make -f '%s' -f '%s'", makefile, overrides);
    /* A protocol-4 host answers the query but runs only library-form programs. */
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
    RUN_TEST_CASE(native_compiler_and_explicit_cache_precedence);
    RUN_TEST_CASE(warm_configuration_tracks_inputs_options_environment_and_compiler);
    RUN_TEST_CASE(configuration_and_build_failures_propagate_and_tool_host_profile_is_narrow);
    RUN_TEST_CASE(private_source_overrides_disable_immutable_dependency_packages);
    RUN_TEST_CASE(dependency_overrides_follow_definitions_then_cache);

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
    free(program);
    tool_remove_tree(suite);
}
