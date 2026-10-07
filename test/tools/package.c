#include "assets/project.h"
#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char directory[1024], build[1024], output[1024], log_path[1024];

static void path(char *destination, const char *base, const char *relative)
{
    int size = snprintf(destination, 1024, "%s/%s", base, relative);
    assert(size > 0 && size < 1024);
}

static bool file_equals(const char *filename, const char *expected)
{
    char *actual = tool_read(filename);
    bool equal = actual && !strcmp(actual, expected);
    free(actual);
    return equal;
}

static void write_profile(int invalid)
{
    char filename[1024], contents[256];
    path(filename, build, "CMakeCache.txt");
    snprintf(
        contents,
        sizeof contents,
        "CHIBI_DISTRIBUTION:BOOL=%s\n"
        "CHIBI_ENABLE_SANITIZERS:BOOL=%s\n",
        invalid == 0 ? "OFF" : "ON",
        invalid == 1 ? "ON" : "OFF"
    );
    tool_write(filename, contents);
}

static int package()
{
    char script[1024];
    path(script, tool_root(), "tools/package_native.py");
    const char *argv[] = { "python3",  script, "--build", build,
                           "--output", output, NULL };
    return tool_run(argv, directory, log_path);
}

static int install(const char *registration)
{
    char script[1024];
    path(script, tool_root(), "tools/install_game_runtime.py");
    const char *argv[] = { "python3",  script,       "--package", output,
                           "--config", registration, NULL };
    return tool_run(argv, directory, log_path);
}

static int configure_fixture(
    const char *source,
    const char *search,
    const char *option,
    const char *environment,
    const char *lock
)
{
    const char *argv[] = { "env",
                           search,
                           "CHIBI_DEPENDENCY_PACKAGES=0",
                           environment,
                           lock,
                           "sh",
                           "tools/configure.sh",
                           "native",
                           "build",
                           option,
                           NULL };
    return tool_run(argv, source, log_path);
}

TEST_CASE(warm_configuration_preserves_input_invalidation)
{
    char source[1024], filename[1024], bin[1024], search[8192], calls[1024];
    path(source, directory, "configure");
    assert(!mkdir(source, 0700));
    path(filename, source, "tools");
    assert(!mkdir(filename, 0700));
    char original[1024];
    path(original, tool_root(), "tools/configure.sh");
    path(filename, source, "tools/configure.sh");
    const char *copy[] = { "cp", original, filename, NULL };
    REQUIRE(tool_run(copy, source, log_path) == 0);
    path(original, tool_root(), "tools/fetchcontent_cache.py");
    path(filename, source, "tools/fetchcontent_cache.py");
    REQUIRE(tool_run(copy, source, log_path) == 0);
    const char *git[] = { "git", "init", "-q", NULL };
    REQUIRE(tool_run(git, source, log_path) == 0);
    path(filename, source, "CMakeLists.txt");
    tool_write(filename, "project(fixture)\n");
    path(filename, source, "external.cmake");
    tool_write(filename, "# external CMake input\n");
    path(bin, source, "bin");
    assert(!mkdir(bin, 0700));
    path(filename, bin, "cmake");
    const char
        *compiler = "#!/bin/sh\nset -eu\ngenerator='Unix Makefiles'\n"
                    "while [ $# -gt 0 ]; do\n"
                    " case $1 in -B) build=$2; shift ;; -S) source=$2; shift "
                    ";; -DGENERATOR=Ninja) generator=Ninja ;; esac\n"
                    " shift\ndone\n"
                    "mkdir -p \"$build/CMakeFiles\"\n"
                    "printf 'CMAKE_GENERATOR:INTERNAL=%s\\n' \"$generator\" > "
                    "\"$build/CMakeCache.txt\"\n"
                    "printf 'set(CMAKE_MAKEFILE_DEPENDS\\n "
                    "\"%s/CMakeLists.txt\"\\n "
                    "\"%s/external.cmake\"\\n  )\\n' \"$source\" \"$source\" > "
                    "\"$build/CMakeFiles/Makefile.cmake\"\n"
                    "if [ \"$generator\" = Ninja ]; then\n"
                    " printf 'build build.ninja: RERUN_CMAKE | "
                    "%s/CMakeLists.txt %s/external.cmake\\n' \"$source\" "
                    "\"$source\" > \"$build/build.ninja\"\n"
                    "fi\n"
                    "printf 'configured\\n' >> \"$source/calls\"\n";
    tool_write(filename, compiler);
    assert(!chmod(filename, 0700));
    snprintf(search, sizeof search, "PATH=%s:%s", bin, getenv("PATH"));
    path(calls, source, "calls");
    REQUIRE(
        configure_fixture(source, search, "-DPROBE=one", "CFLAGS=-DFIRST", "CHIBI_WORKSPACE_LOCK_FD=4") == 0
    );
    REQUIRE(file_equals(calls, "configured\n"));
    REQUIRE(
        configure_fixture(source, search, "-DPROBE=one", "CFLAGS=-DFIRST", "CHIBI_WORKSPACE_LOCK_FD=8") == 0
    );
    REQUIRE(file_equals(calls, "configured\n"));
    path(filename, source, "CMakeLists.txt");
    tool_write(filename, "project(changed)\n");
    REQUIRE(
        configure_fixture(source, search, "-DPROBE=one", "CFLAGS=-DFIRST", "CHIBI_WORKSPACE_LOCK_FD=8") == 0
    );
    REQUIRE(file_equals(calls, "configured\nconfigured\n"));
    path(filename, source, "external.cmake");
    tool_write(filename, "# changed toolchain input\n");
    REQUIRE(
        configure_fixture(source, search, "-DPROBE=one", "CFLAGS=-DFIRST", "CHIBI_WORKSPACE_LOCK_FD=8") == 0
    );
    REQUIRE(file_equals(calls, "configured\nconfigured\nconfigured\n"));
    REQUIRE(
        configure_fixture(source, search, "-DPROBE=one", "CFLAGS=-DSECOND", "CHIBI_WORKSPACE_LOCK_FD=8") == 0
    );
    REQUIRE(file_equals(calls, "configured\nconfigured\nconfigured\nconfigured\n"));
    REQUIRE(
        configure_fixture(source, search, "-DPROBE=two", "CFLAGS=-DSECOND", "CHIBI_WORKSPACE_LOCK_FD=8") == 0
    );
    REQUIRE(file_equals(calls, "configured\nconfigured\nconfigured\nconfigured\nconfigured\n"));
    path(filename, bin, "cmake");
    FILE *script = fopen(filename, "a");
    assert(script);
    assert(fputs("# tool replaced\n", script) >= 0);
    assert(!fclose(script));
    REQUIRE(
        configure_fixture(source, search, "-DPROBE=two", "CFLAGS=-DSECOND", "CHIBI_WORKSPACE_LOCK_FD=8") == 0
    );
    REQUIRE(
        file_equals(calls, "configured\nconfigured\nconfigured\nconfigured\nconfigured\nconfigured\n")
    );
    REQUIRE(
        configure_fixture(source, search, "-DGENERATOR=Ninja", "CFLAGS=-DSECOND", "CHIBI_WORKSPACE_LOCK_FD=8") ==
        0
    );
    path(filename, source, "external.cmake");
    tool_write(filename, "# changed Ninja input\n");
    REQUIRE(
        configure_fixture(source, search, "-DGENERATOR=Ninja", "CFLAGS=-DSECOND", "CHIBI_WORKSPACE_LOCK_FD=8") ==
        0
    );
    REQUIRE(
        file_equals(calls, "configured\nconfigured\nconfigured\nconfigured\nconfigured\nconfigured\nconfigured\nconfigured\n")
    );
#ifdef __APPLE__
    path(filename, bin, "xcrun");
    tool_write(filename, "#!/bin/sh\ncat \"$(dirname \"$0\")/../sdk-choice\"\n");
    assert(!chmod(filename, 0700));
    char sdk[1024];
    path(sdk, source, "sdk-one");
    assert(!mkdir(sdk, 0700));
    path(filename, source, "sdk-choice");
    tool_write(filename, sdk);
    REQUIRE(
        configure_fixture(source, search, "-DGENERATOR=Ninja", "CFLAGS=-DSECOND", "CHIBI_WORKSPACE_LOCK_FD=8") ==
        0
    );
    REQUIRE(
        configure_fixture(source, search, "-DGENERATOR=Ninja", "CFLAGS=-DSECOND", "CHIBI_WORKSPACE_LOCK_FD=8") ==
        0
    );
    REQUIRE(
        file_equals(calls, "configured\nconfigured\nconfigured\nconfigured\nconfigured\nconfigured\nconfigured\nconfigured\nconfigured\n")
    );
    path(sdk, source, "sdk-two");
    assert(!mkdir(sdk, 0700));
    tool_write(filename, sdk);
    REQUIRE(
        configure_fixture(source, search, "-DGENERATOR=Ninja", "CFLAGS=-DSECOND", "CHIBI_WORKSPACE_LOCK_FD=8") ==
        0
    );
    REQUIRE(
        file_equals(calls, "configured\nconfigured\nconfigured\nconfigured\nconfigured\nconfigured\nconfigured\nconfigured\nconfigured\nconfigured\n")
    );
#endif
    return true;
}

TEST_CASE(package_preserves_unrelated_output_and_rejects_development_builds)
{
    char marker[1024];
    assert(!mkdir(output, 0700));
    path(marker, output, "important.txt");
    tool_write(marker, "keep authored data\n");
    REQUIRE(package() != 0);
    REQUIRE(file_equals(marker, "keep authored data\n"));
    tool_remove_tree(output);
    for (int invalid = 0; invalid < 2; invalid++) {
        write_profile(invalid);
        REQUIRE(package() != 0);
        REQUIRE(access(output, F_OK) != 0);
    }
    write_profile(-1);
    return true;
}

TEST_CASE(package_installation_preserves_game_and_rejects_corruption)
{
    REQUIRE(package() == 0);
    char manifest_path[1024];
    path(manifest_path, output, "share/chibi/engine.json");
    char *manifest = tool_read(manifest_path);
    REQUIRE(manifest != NULL);
    bool complete = strstr(manifest, "\"build_id\"") &&
                    strstr(manifest, "bin/chibi_ctl") &&
                    strstr(manifest, "share/chibi/mcp/server.mjs") &&
                    strstr(manifest, "share/chibi/studio/project.kdl");
    free(manifest);
    REQUIRE(complete);

    char game[1024], filename[1024], config[1024], registration[1024], contents[2048];
    path(game, directory, "authored game");
    assert(!mkdir(game, 0700));
    path(filename, game, "project.kdl");
    snprintf(contents, sizeof contents, "version %d\nname \"Independent\"\ncontent \"content\"\n", CHIBI_PROJECT_VERSION);
    tool_write(filename, contents);
    path(filename, game, "content");
    assert(!mkdir(filename, 0700));
    path(filename, game, "content/authored.txt");
    tool_write(filename, "original game content\n");
    path(config, game, ".codex");
    assert(!mkdir(config, 0700));
    path(config, game, ".codex/config.toml");
    const char *original = "model = \"custom\"\n\n[mcp_servers.other]\ncommand "
                           "= \"other\"\n";
    tool_write(config, original);
    path(registration, directory, "registration.json");
    snprintf(contents, sizeof contents, "{\"games\":[\"%s\"]}\n", game);
    tool_write(registration, contents);
    REQUIRE(install(registration) == 0);
    REQUIRE(file_equals(filename, "original game content\n"));
    char *installed_config = tool_read(config);
    REQUIRE(installed_config != NULL);
    bool preserved = strstr(installed_config, original) &&
                     strstr(installed_config, ".chibi/runtime/bin/chibi-mcp");
    free(installed_config);
    REQUIRE(preserved);

    char runtime_binary[1024], package_binary[1024], pin_path[1024];
    path(runtime_binary, game, ".chibi/runtime/bin/chibi");
    path(package_binary, output, "bin/chibi");
    struct stat installed_stat, source_stat;
    REQUIRE(!lstat(runtime_binary, &installed_stat));
    REQUIRE(!stat(package_binary, &source_stat));
    REQUIRE(S_ISREG(installed_stat.st_mode));
    REQUIRE(installed_stat.st_nlink == 1);
    REQUIRE(
        installed_stat.st_ino != source_stat.st_ino ||
        installed_stat.st_dev != source_stat.st_dev
    );
    ino_t active_inode = installed_stat.st_ino;
    path(pin_path, game, ".chibi-engine.json");
    char *pin = tool_read(pin_path);
    REQUIRE(pin != NULL);
    tool_write(package_binary, "corrupted executable\n");
    int result = install(registration);
    bool pin_preserved = file_equals(pin_path, pin);
    free(pin);
    REQUIRE(result != 0);
    REQUIRE(pin_preserved);
    REQUIRE(!stat(runtime_binary, &installed_stat));
    REQUIRE(installed_stat.st_ino == active_inode);
    REQUIRE(installed_stat.st_size == source_stat.st_size);
    REQUIRE(file_equals(filename, "original game content\n"));
    return true;
}

TEST_CASE(serial_web_package_disables_stale_threaded_entrypoints)
{
    char serial[1024], threads[1024], script[1024], filename[1024], entry[1024],
        residue[1024];
    path(serial, directory, "web serial");
    path(threads, directory, "web threads");
    assert(!mkdir(serial, 0700));
    assert(!mkdir(threads, 0700));
    const char *files[] = { "chibi.html", "chibi.js", "chibi.wasm", "chibi.data", "chibi-isolation.js" };
    for (int i = 0; i < (int)(sizeof files / sizeof *files); i++) {
        path(filename, serial, files[i]);
        tool_write(filename, "serial app");
        path(filename, threads, files[i]);
        tool_write(filename, "threaded app");
    }
    path(script, tool_root(), "tools/package_web.py");
    const char *serial_args[] = { "python3",  script, "--serial", serial,
                                  "--output", serial, NULL };
    const char *paired_args[] = { "python3",  script,      "--serial",
                                  serial,     "--threads", threads,
                                  "--output", serial,      NULL };
    REQUIRE(tool_run(serial_args, directory, log_path) == 0);
    REQUIRE(tool_run(paired_args, directory, log_path) == 0);
    path(entry, serial, "threads/index.html");
    REQUIRE(file_equals(entry, "threaded app"));
    path(filename, serial, "threads/chibi.html");
    REQUIRE(file_equals(filename, "threaded app"));
    path(residue, serial, "threads/authored.txt");
    tool_write(residue, "keep unrelated residue");
    REQUIRE(tool_run(serial_args, directory, log_path) == 0);
    REQUIRE(access(entry, F_OK) != 0 && access(filename, F_OK) != 0);
    REQUIRE(file_equals(residue, "keep unrelated residue"));
    path(filename, serial, "threads/chibi.wasm");
    REQUIRE(file_equals(filename, "threaded app"));
    REQUIRE(tool_run(paired_args, directory, log_path) == 0);
    REQUIRE(file_equals(entry, "threaded app"));
    REQUIRE(file_equals(residue, "keep unrelated residue"));
    assert(!unlink(entry));
    assert(!symlink(residue, entry));
    REQUIRE(tool_run(serial_args, directory, log_path) != 0);
    REQUIRE(file_equals(residue, "keep unrelated residue"));
    struct stat info;
    REQUIRE(!lstat(entry, &info) && S_ISLNK(info.st_mode));
    assert(!unlink(entry));
    char worker_directory[1024], preserved[1024];
    path(worker_directory, serial, "threads");
    path(preserved, serial, "preserved threads");
    assert(!rename(worker_directory, preserved));
    assert(!symlink(preserved, worker_directory));
    REQUIRE(tool_run(serial_args, directory, log_path) != 0);
    path(residue, preserved, "authored.txt");
    REQUIRE(file_equals(residue, "keep unrelated residue"));
    return true;
}

void test_tool_package()
{
    strcpy(directory, "/tmp/chibi-package-XXXXXX");
    assert(mkdtemp(directory));
    path(build, directory, "distribution build");
    path(output, directory, "native release");
    path(log_path, directory, "command.log");
    assert(!mkdir(build, 0700));
    for (int i = 0; i < 2; i++) {
        char filename[1024];
        path(filename, build, i ? "chibi_ctl" : "chibi");
        const char *argv[] = { "cp", "/usr/bin/true", filename, NULL };
        assert(tool_run(argv, directory, log_path) == 0);
    }
    write_profile(-1);
    RUN_TEST_CASE(package_preserves_unrelated_output_and_rejects_development_builds);
    RUN_TEST_CASE(package_installation_preserves_game_and_rejects_corruption);
    RUN_TEST_CASE(warm_configuration_preserves_input_invalidation);
    RUN_TEST_CASE(serial_web_package_disables_stale_threaded_entrypoints);
    tool_remove_tree(directory);
}
