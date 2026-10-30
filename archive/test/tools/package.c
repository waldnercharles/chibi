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

static int configure_fixture(const char *source, const char *search, const char *option, const char *environment)
{
    const char *argv[] = {
        "env",    search,  environment, "sh", "tools/configure.sh",
        "native", "build", option,      NULL
    };
    return tool_run(argv, source, log_path);
}

TEST_CASE(native_exports_have_package_owners)
{
    char filename[1024];
    path(filename, tool_root(), "sdk/native.cmake");
    char *cmake = tool_read(filename);
    path(filename, tool_root(), "tools/package_sdk.py");
    char *packager = tool_read(filename);
    REQUIRE(cmake && packager);
    char *inventory = strstr(packager, "\nNATIVE_TARGETS = {");
    REQUIRE(inventory);
    char *end = strchr(inventory, '}');
    REQUIRE(end);
    *end = 0;
    int exports = 0;
    const char *cursor = cmake;
    while ((cursor = strstr(cursor, "EXPORT_NAME "))) {
        cursor += strlen("EXPORT_NAME ");
        char name[128];
        REQUIRE(sscanf(cursor, "%127[a-z_0-9]", name) == 1);
        char quoted[132];
        snprintf(quoted, sizeof quoted, "'%s'", name);
        if (!strstr(inventory, quoted)) {
            fprintf(stderr, "Native export has no package owner: %s\n", name);
        }
        REQUIRE(strstr(inventory, quoted));
        exports++;
    }
    REQUIRE(exports > 0);
    free(packager);
    free(cmake);
    return true;
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
    RUN_TEST_CASE(native_exports_have_package_owners);
    RUN_TEST_CASE(warm_configuration_preserves_input_invalidation);
    tool_remove_tree(directory);
}
