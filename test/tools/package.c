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

/* The fixture is produced in C; the production packager is the only Python
 * workload. shasum supplies independent content digests for installed records. */
static bool checksum(const char *filename, char result[65])
{
    const char *argv[] = { "shasum", "-a", "256", filename, NULL };
    if (tool_run(argv, directory, log_path)) {
        return false;
    }
    char *text = tool_read(log_path);
    bool valid = text && strlen(text) >= 65;
    if (valid) {
        memcpy(result, text, 64);
        result[64] = 0;
    }
    free(text);
    return valid;
}
static void write_profile(int invalid)
{
    static const char *names[] = { "bin/chibi-player",
                                   "include/chibi/types/example.h",
                                   "share/chibi/engine.json",
                                   "share/chibi/shaders/example.glsl" };
    char filename[1024], files[4096] = "{", checksum_text[65];
    for (int i = 0; i < 4; i++) {
        path(filename, build, names[i]);
        assert(checksum(filename, checksum_text));
        struct stat info;
        assert(!stat(filename, &info));
        size_t offset = strlen(files);
        int count = snprintf(
            files + offset,
            sizeof files - offset,
            "%s\"%s\":{\"bytes\":%lld,\"sha256\":\"%s\"}",
            i ? "," : "",
            names[i],
            (long long)info.st_size,
            checksum_text
        );
        assert(count > 0 && (size_t)count < sizeof files - offset);
    }
    strcat(files, "}");
    char canonical[8192], manifest[8192], identity[65];
    const char *kind = invalid == 0 ? "sdk" : "runtime";
    snprintf(
        canonical,
        sizeof canonical,
        "{\"engine\":{\"build_id\":\"fixture-engine\",\"manifest\":\"share/"
        "chibi/engine.json\"},"
        "\"files\":%s,\"kind\":\"%s\",\"manifest_format\":1,"
        "\"package_version\":\"0.1.0\",\"products\":%s,"
        "\"requirements\":{\"project_format\":%d},\"targets\":[\"Chibi::"
        "player\"]}",
        files,
        kind,
        invalid == 1 ? "{}" : "{\"chibi-player\":\"bin/chibi-player\"}",
        CHIBI_PROJECT_VERSION
    );
    path(filename, directory, "identity.json");
    tool_write(filename, canonical);
    assert(checksum(filename, identity));
    snprintf(
        manifest,
        sizeof manifest,
        "{\"manifest_format\":1,\"kind\":\"%s\",\"build_id\":\"%s\","
        "\"package_version\":\"0.1.0\",\"files\":%s,"
        "\"products\":%s,\"targets\":[\"Chibi::player\"],"
        "\"requirements\":{\"project_format\":%d},"
        "\"engine\":{\"manifest\":\"share/chibi/"
        "engine.json\",\"build_id\":\"fixture-engine\"}}\n",
        kind,
        identity,
        files,
        invalid == 1 ? "{}" : "{\"chibi-player\":\"bin/chibi-player\"}",
        CHIBI_PROJECT_VERSION
    );
    path(filename, build, "share/chibi/package.json");
    tool_write(filename, manifest);
}

static int package()
{
    char script[1024];
    path(script, tool_root(), "tools/package_player.py");
    const char *argv[] = { "python3",  script, "--sdk", build,
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

static void previous_runtime(const char *game)
{
    char filename[1024], runtime[1024], canonical[1024], identity[65],
        binary_hash[65];
    static const char *directories[] = { ".chibi",
                                         ".chibi/runtime",
                                         ".chibi/runtime/bin",
                                         ".chibi/runtime/share",
                                         ".chibi/runtime/share/chibi" };
    for (int i = 0; i < 5; i++) {
        path(filename, game, directories[i]);
        assert(!mkdir(filename, 0700));
    }
    path(runtime, game, ".chibi/runtime");
    for (int i = 0; i < 2; i++) {
        path(filename, runtime, i ? "bin/chibi_ctl" : "bin/chibi");
        const char *copy[] = { "cp", "/usr/bin/true", filename, NULL };
        assert(!tool_run(copy, directory, log_path));
    }
    assert(checksum(filename, binary_hash));
    snprintf(canonical, sizeof canonical, "{\"bin/chibi\":\"%s\",\"bin/chibi_ctl\":\"%s\"}", binary_hash, binary_hash);
    path(filename, directory, "previous-identity.json");
    tool_write(filename, canonical);
    assert(checksum(filename, identity));
    char manifest[2048];
    snprintf(
        manifest,
        sizeof manifest,
        "{\"format\":1,\"build_id\":\"%s\",\"project_version\":%d,\"files\":%s}"
        "\n",
        identity,
        CHIBI_PROJECT_VERSION,
        canonical
    );
    path(filename, runtime, "share/chibi/engine.json");
    tool_write(filename, manifest);
}

static int configure_fixture(const char *source, const char *search, const char *option, const char *environment)
{
    const char *argv[] = { "env",       search,  "CHIBI_DEPENDENCY_PACKAGES=0",
                           environment, "sh",    "tools/configure.sh",
                           "native",    "build", option,
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
    REQUIRE(configure_fixture(source, search, "-DPROBE=one", "CFLAGS=-DFIRST") == 0);
    REQUIRE(file_equals(calls, "configured\n"));
    REQUIRE(configure_fixture(source, search, "-DPROBE=one", "CFLAGS=-DFIRST") == 0);
    REQUIRE(file_equals(calls, "configured\n"));
    path(filename, source, "CMakeLists.txt");
    tool_write(filename, "project(changed)\n");
    REQUIRE(configure_fixture(source, search, "-DPROBE=one", "CFLAGS=-DFIRST") == 0);
    REQUIRE(file_equals(calls, "configured\nconfigured\n"));
    path(filename, source, "external.cmake");
    tool_write(filename, "# changed toolchain input\n");
    REQUIRE(configure_fixture(source, search, "-DPROBE=one", "CFLAGS=-DFIRST") == 0);
    REQUIRE(file_equals(calls, "configured\nconfigured\nconfigured\n"));
    REQUIRE(configure_fixture(source, search, "-DPROBE=one", "CFLAGS=-DSECOND") == 0);
    REQUIRE(file_equals(calls, "configured\nconfigured\nconfigured\nconfigured\n"));
    REQUIRE(configure_fixture(source, search, "-DPROBE=two", "CFLAGS=-DSECOND") == 0);
    REQUIRE(file_equals(calls, "configured\nconfigured\nconfigured\nconfigured\nconfigured\n"));
    path(filename, bin, "cmake");
    FILE *script = fopen(filename, "a");
    assert(script);
    assert(fputs("# tool replaced\n", script) >= 0);
    assert(!fclose(script));
    REQUIRE(configure_fixture(source, search, "-DPROBE=two", "CFLAGS=-DSECOND") == 0);
    REQUIRE(
        file_equals(calls, "configured\nconfigured\nconfigured\nconfigured\nconfigured\nconfigured\n")
    );
    REQUIRE(configure_fixture(source, search, "-DGENERATOR=Ninja", "CFLAGS=-DSECOND") == 0);
    path(filename, source, "external.cmake");
    tool_write(filename, "# changed Ninja input\n");
    REQUIRE(configure_fixture(source, search, "-DGENERATOR=Ninja", "CFLAGS=-DSECOND") == 0);
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
    REQUIRE(configure_fixture(source, search, "-DGENERATOR=Ninja", "CFLAGS=-DSECOND") == 0);
    REQUIRE(configure_fixture(source, search, "-DGENERATOR=Ninja", "CFLAGS=-DSECOND") == 0);
    REQUIRE(
        file_equals(calls, "configured\nconfigured\nconfigured\nconfigured\nconfigured\nconfigured\nconfigured\nconfigured\nconfigured\n")
    );
    path(sdk, source, "sdk-two");
    assert(!mkdir(sdk, 0700));
    tool_write(filename, sdk);
    REQUIRE(configure_fixture(source, search, "-DGENERATOR=Ninja", "CFLAGS=-DSECOND") == 0);
    REQUIRE(
        file_equals(calls, "configured\nconfigured\nconfigured\nconfigured\nconfigured\nconfigured\nconfigured\nconfigured\nconfigured\nconfigured\n")
    );
#endif
    return true;
}

TEST_CASE(package_preserves_unrelated_output_and_rejects_nonruntime_packages)
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
    path(manifest_path, output, "share/chibi/package.json");
    char *manifest = tool_read(manifest_path);
    REQUIRE(manifest != NULL);
    bool complete = strstr(manifest, "\"build_id\"") &&
                    strstr(manifest, "bin/chibi-player") &&
                    strstr(manifest, "\"kind\": \"player\"") &&
                    strstr(manifest, "\"sdk\"") &&
                    !strstr(manifest, "bin/chibi_ctl") &&
                    !strstr(manifest, "include/chibi/") &&
                    !strstr(manifest, "share/chibi/studio/");
    free(manifest);
    REQUIRE(complete);

    char game[1024], filename[1024], config[1024], registration[1024], contents[2048];
    path(game, directory, "authored game");
    assert(!mkdir(game, 0700));
    path(filename, game, "project.kdl");
    snprintf(
        contents,
        sizeof contents,
        "version %d\n"
        "name \"Independent\"\n"
        "content \"content\"\n",
        CHIBI_PROJECT_VERSION
    );
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
    previous_runtime(game);
    char previous_binary[1024];
    path(previous_binary, game, ".chibi/runtime/bin/chibi_ctl");
    tool_write(previous_binary, "modified prior runtime\n");
    REQUIRE(install(registration) != 0);
    REQUIRE(file_equals(previous_binary, "modified prior runtime\n"));
    REQUIRE(file_equals(filename, "original game content\n"));
    const char *restore[] = { "cp", "/usr/bin/true", previous_binary, NULL };
    REQUIRE(tool_run(restore, directory, log_path) == 0);
    REQUIRE(install(registration) == 0);
    REQUIRE(access(previous_binary, F_OK) != 0);
    REQUIRE(file_equals(filename, "original game content\n"));
    REQUIRE(file_equals(config, original));

    char runtime_binary[1024], package_binary[1024], pin_path[1024];
    path(runtime_binary, game, ".chibi/runtime/bin/chibi-player");
    path(package_binary, output, "bin/chibi-player");
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

TEST_CASE(web_sdk_recipe_passes_only_configuration_definitions)
{
    char source[1024], bin[1024], filename[1024], original[1024], search[8192];
    path(source, directory, "web-recipe");
    assert(!mkdir(source, 0700));
    path(filename, source, "tools");
    assert(!mkdir(filename, 0700));
    path(bin, source, "bin");
    assert(!mkdir(bin, 0700));
    path(original, tool_root(), "Makefile");
    path(filename, source, "Makefile");
    const char *copy[] = { "cp", original, filename, NULL };
    REQUIRE(tool_run(copy, source, log_path) == 0);
    path(original, tool_root(), "tools/web_env.sh");
    path(filename, source, "tools/web_env.sh");
    REQUIRE(tool_run(copy, source, log_path) == 0);
    path(filename, source, "tools/quiet.sh");
    tool_write(filename, "#!/bin/sh\nshift 2\nexec \"$@\"\n");
    path(filename, source, "tools/configure.sh");
    tool_write(
        filename,
        "#!/bin/sh\nset -eu\n"
        "test \"$1\" = sdk-web\ntest \"$2\" = web-sdk\nshift 2\n"
        "for argument do\n case $argument in -D*=*) ;; *) exit 1 ;; "
        "esac\ndone\n"
        "printf '%s\\n' \"$@\" > configure-arguments\n"
    );
    path(filename, bin, "cmake");
    tool_write(filename, "#!/bin/sh\nprintf '%s\\n' \"$@\" > build-arguments\n");
    assert(!chmod(filename, 0700));
    snprintf(search, sizeof search, "PATH=%s:%s", bin, getenv("PATH"));
    const char *args[] = { "env",
                           search,
                           "make",
                           "-o",
                           "sdk-reflect-build",
                           "web-sdk-build",
                           "QUIET=sh tools/quiet.sh",
                           "WEB_SDK_BUILD_DIR=web-sdk",
                           "WEB_SDK_CMAKE_FLAGS=-DPROBE=present",
                           "WEB_THREADS=0",
                           "JOBS=2",
                           NULL };
    REQUIRE(tool_run(args, source, log_path) == 0);
    path(filename, source, "configure-arguments");
    char *arguments = tool_read(filename);
    REQUIRE(arguments);
    bool definitions = strstr(arguments, "-DPROBE=present\n") &&
                       strstr(arguments, "-DCMAKE_BUILD_TYPE=Release\n") &&
                       strstr(arguments, "-DCHIBI_WEB_THREADS=OFF\n") &&
                       strstr(arguments, "-DCHIBI_REFLECT_EXECUTABLE=");
    free(arguments);
    REQUIRE(definitions);
    path(filename, source, "build-arguments");
    REQUIRE(file_equals(filename, "--build\nweb-sdk\n--parallel\n2\n"));
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
    char filename[1024];
    const char *directories[] = {
        "bin",   "include",     "include/chibi",      "include/chibi/types",
        "share", "share/chibi", "share/chibi/shaders"
    };
    for (int i = 0; i < 7; i++) {
        path(filename, build, directories[i]);
        assert(!mkdir(filename, 0700));
    }
    path(filename, build, "bin/chibi-player");
    const char *copy[] = { "cp", "/usr/bin/true", filename, NULL };
    assert(tool_run(copy, directory, log_path) == 0);
    path(filename, build, "include/chibi/types/example.h");
    tool_write(filename, "/* SDK-only declaration */\n");
    path(filename, build, "share/chibi/shaders/example.glsl");
    tool_write(filename, "void main() {}\n");
    path(filename, build, "share/chibi/engine.json");
    char engine[256];
    snprintf(engine, sizeof engine, "{\"manifest_format\":1,\"build_id\":\"fixture-engine\",\"project_version\":%d}\n", CHIBI_PROJECT_VERSION);
    tool_write(filename, engine);
    write_profile(-1);
    RUN_TEST_CASE(package_preserves_unrelated_output_and_rejects_nonruntime_packages);
    RUN_TEST_CASE(package_installation_preserves_game_and_rejects_corruption);
    RUN_TEST_CASE(warm_configuration_preserves_input_invalidation);
    RUN_TEST_CASE(web_sdk_recipe_passes_only_configuration_definitions);
    RUN_TEST_CASE(serial_web_package_disables_stale_threaded_entrypoints);
    tool_remove_tree(directory);
}
