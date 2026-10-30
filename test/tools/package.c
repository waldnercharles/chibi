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

/* The Makefile's graph host recipe runs tools/bootstrap/seed.sh before it looks
 * for a host; fixtures that copy the Makefile stand in a seed step that does
 * nothing. */
static void seed_step(const char *source)
{
    char filename[1024];
    path(filename, source, "tools/bootstrap");
    assert(!mkdir(filename, 0700));
    path(filename, source, "tools/bootstrap/seed.sh");
    tool_write(filename, "#!/bin/sh\nexit 0\n");
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

TEST_CASE(reflection_build_outputs_preserve_sdk_source_identity)
{
    char source[1024], original[1024], filename[1024], packager[1024];
    path(source, directory, "source identity");
    assert(!mkdir(source, 0700));
    path(original, tool_root(), ".gitignore");
    path(filename, source, ".gitignore");
    const char *copy[] = { "cp", original, filename, NULL };
    REQUIRE(tool_run(copy, source, log_path) == 0);
    path(original, tool_root(), "Makefile");
    path(filename, source, "Makefile");
    REQUIRE(tool_run(copy, source, log_path) == 0);
    path(filename, source, "tools");
    assert(!mkdir(filename, 0700));
    path(filename, source, "tools/quiet.sh");
    tool_write(filename, "#!/bin/sh\nshift 2\nexec \"$@\"\n");
    seed_step(source);
    path(filename, source, "tools/configure.sh");
    tool_write(
        filename,
        "#!/bin/sh\nset -eu\n"
        "test \"$1\" = sdk-native\n"
        "mkdir -p \"$2\"\n"
        "printf 'generated configuration\\n' > \"$2/CMakeCache.txt\"\n"
    );
    path(filename, source, "bin");
    assert(!mkdir(filename, 0700));
    path(filename, source, "bin/cmake");
    tool_write(
        filename,
        "#!/bin/sh\nset -eu\n"
        "test \"$1\" = --build\n"
        "mkdir -p \"$2/bin\"\n"
        "cp host-template \"$2/bin/chibi-build-host\"\n"
        "chmod 700 \"$2/bin/chibi-build-host\"\n"
    );
    assert(!chmod(filename, 0700));
    path(filename, source, "host-template");
    tool_write(
        filename,
        "#!/bin/sh\nset -eu\n"
        "if [ \"$1\" = --protocol-version ]; then echo 14; exit 0; fi\n"
        "while [ $# -gt 0 ]; do\n"
        " if [ \"$1\" = --build ]; then\n"
        "  mkdir -p \"$2\"\n"
        "  printf 'generated reflection tool\\n' > \"$2/reflect\"\n"
        "  exit 0\n fi\n shift\ndone\nexit 1\n"
    );
    path(filename, source, "Authored.chibi");
    tool_write(filename, "package fixture\n");
    path(packager, tool_root(), "tools/package_sdk.py");
    const char *script =
        "import importlib.util, os, pathlib, subprocess, sys\n"
        "root = pathlib.Path(sys.argv[2])\n"
        "def git(*args):\n"
        "    subprocess.run(['git', *args], cwd=root, check=True, "
        "stdout=subprocess.DEVNULL)\n"
        "git('-c', 'init.defaultBranch=main', 'init', '-q')\n"
        "git('config', 'core.excludesFile', '/dev/null')\n"
        "git('config', 'core.hooksPath', '/dev/null')\n"
        "git('add', '--all')\n"
        "git('-c', 'user.name=Fixture', '-c', "
        "'user.email=fixture@example.invalid',\n"
        "    '-c', 'commit.gpgsign=false', 'commit', '-qm', 'fixture')\n"
        "spec = importlib.util.spec_from_file_location('chibi_package_sdk', "
        "sys.argv[1])\n"
        "packager = importlib.util.module_from_spec(spec)\n"
        "spec.loader.exec_module(packager)\n"
        "packager.ROOT = root\n"
        "before = packager.source_identity()\n"
        "assert not before['dirty'], 'source fixture is not clean'\n"
        "environment = dict(os.environ, PATH=str(root / 'bin') + os.pathsep + "
        "os.environ['PATH'])\n"
        "for options in ([], ['SDK_REFLECT_BUILD_DIR=.chibi/reflection "
        "build']):\n"
        "    for attempt in range(2):\n"
        "        subprocess.run(['make', 'sdk-reflect-build', 'QUIET=sh "
        "tools/quiet.sh',\n"
        "                        'BUILD_HOST=missing-host', "
        "'NATIVE_TOOLCHAIN_FLAGS=', 'JOBS=2', *options],\n"
        "                       cwd=root, env=environment, check=True)\n"
        "        generated = packager.source_identity()\n"
        "        assert not generated['dirty'], 'reflection build dirtied SDK "
        "source identity'\n"
        "        assert generated['snapshot_sha256'] == "
        "before['snapshot_sha256'], 'reflection build changed SDK source "
        "snapshot'\n"
        "(root / 'Authored.chibi').write_text('package edited\\n')\n"
        "edited = packager.source_identity()\n"
        "assert edited['dirty'], 'authored source changes must remain "
        "visible'\n"
        "assert edited['snapshot_sha256'] != before['snapshot_sha256'], "
        "'authored source changes must change the snapshot'\n";
    const char *argv[] = { "python3", "-B", "-c", script, packager, source, NULL };
    int status = tool_run(argv, source, log_path);
    if (status) {
        char *log = tool_read(log_path);
        fprintf(stderr, "SDK source identity: %s\n", log ? log : "");
        free(log);
    }
    REQUIRE(!status);
    return true;
}

TEST_CASE(sdk_reflection_recipe_uses_graph_and_recovers_its_host)
{
    char source[1024], canonical[1024], filename[1024], original[1024],
        bin[1024], search[8192], calls[1024], expected[8192];
    path(source, directory, "reflect recipe");
    assert(!mkdir(source, 0700));
    REQUIRE(realpath(source, canonical));
    path(filename, source, "tools");
    assert(!mkdir(filename, 0700));
    path(bin, source, "bin");
    assert(!mkdir(bin, 0700));
    path(original, tool_root(), "Makefile");
    path(filename, source, "Makefile");
    const char *copy[] = { "cp", original, filename, NULL };
    REQUIRE(tool_run(copy, source, log_path) == 0);
    path(filename, source, "tools/quiet.sh");
    tool_write(filename, "#!/bin/sh\nshift 2\nexec \"$@\"\n");
    seed_step(source);
    const char
        *host = "#!/bin/sh\nset -eu\n"
                "if [ \"$1\" = --protocol-version ]; then\n"
                " if [ -f \"$0.old\" ]; then echo 13; else echo 14; fi\n "
                "exit 0\nfi\n"
                "test \"$CCACHE_BASEDIR\" = \"$PWD\"\n"
                "printf 'graph\\n' >> calls\n"
                "printf '%s\\n' \"$0\" > graph-program\n"
                "printf '%s\\n' \"$@\" > graph-arguments\n"
                "if [ -f graph-fails ]; then exit 33; fi\n";
    path(filename, source, "host-template");
    tool_write(filename, host);
    path(filename, source, "fixture-host");
    tool_write(filename, host);
    assert(!chmod(filename, 0700));
    path(filename, source, "tools/configure.sh");
    tool_write(
        filename,
        "#!/bin/sh\nset -eu\n"
        "printf 'configure\\n' >> calls\n"
        "printf '%s\\n' \"$@\" > configure-arguments\n"
        "if [ -f configure-fails ]; then exit 34; fi\n"
    );
    path(filename, bin, "cmake");
    tool_write(
        filename,
        "#!/bin/sh\nset -eu\n"
        "printf 'build\\n' >> calls\n"
        "printf '%s\\n' \"$@\" > build-arguments\n"
        "if [ -f build-fails ]; then exit 35; fi\n"
        "test \"$1\" = --build\n"
        "mkdir -p \"$2/bin\"\n"
        "cp host-template \"$2/bin/chibi-build-host\"\n"
        "chmod 700 \"$2/bin/chibi-build-host\"\n"
        "rm -f \"$2/bin/chibi-build-host.old\"\n"
    );
    assert(!chmod(filename, 0700));
    snprintf(search, sizeof search, "PATH=%s:%s", bin, getenv("PATH"));
    const char *args[] = { "env",
                           search,
                           "make",
                           "sdk-reflect-build",
                           "QUIET=sh tools/quiet.sh",
                           "BUILD_HOST=./fixture-host",
                           "SDK_REFLECT_BUILD_DIR=reflection build",
                           "LIBCLANG_ROOT=llvm root",
                           "JOBS=3",
                           "NATIVE_TOOLCHAIN_FLAGS=-DCMAKE_C_COMPILER=/fixture/"
                           "cc -DCMAKE_CXX_COMPILER=/fixture/c++",
                           NULL };
    path(calls, source, "calls");
    REQUIRE(tool_run(args, source, log_path) == 0);
    REQUIRE(tool_run(args, source, log_path) == 0);
    REQUIRE(file_equals(calls, "graph\ngraph\n"));
    path(filename, source, "configure-arguments");
    REQUIRE(access(filename, F_OK) != 0);
    path(filename, source, "build-arguments");
    REQUIRE(access(filename, F_OK) != 0);
    path(filename, source, "graph-program");
    REQUIRE(file_equals(filename, "./fixture-host\n"));
    snprintf(
        expected,
        sizeof expected,
        "--compiler\ntools/compiler\n--compiler-core\npackages/core\n"
        "--root\n%s\n--source\ntools/toolchain/Graph.chibi\n--core\npackages/"
        "core\n"
        "--collection\ngraph=tools/"
        "graph\n--entry\nSDKReflect\n--timeout-ms\n3600000\n"
        "--\n--build\n%s/reflection build\n--jobs\n3\n--libclang-root\n%s/llvm "
        "root\n"
        "--\n-DCMAKE_C_COMPILER=/fixture/cc\n-DCMAKE_CXX_COMPILER=/fixture/"
        "c++\n",
        canonical,
        canonical,
        canonical
    );
    path(filename, source, "graph-arguments");
    REQUIRE(file_equals(filename, expected));

    tool_write(calls, "");
    path(filename, source, "graph-fails");
    tool_write(filename, "");
    REQUIRE(tool_run(args, source, log_path) != 0);
    REQUIRE(file_equals(calls, "graph\n"));
    assert(!unlink(filename));
    REQUIRE(tool_run(args, source, log_path) == 0);
    REQUIRE(file_equals(calls, "graph\ngraph\n"));

    path(filename, source, "fixture-host");
    assert(!unlink(filename));
    tool_write(calls, "");
    path(filename, source, "configure-fails");
    tool_write(filename, "");
    REQUIRE(tool_run(args, source, log_path) != 0);
    REQUIRE(file_equals(calls, "configure\n"));
    assert(!unlink(filename));
    tool_write(calls, "");
    path(filename, source, "build-fails");
    tool_write(filename, "");
    REQUIRE(tool_run(args, source, log_path) != 0);
    REQUIRE(file_equals(calls, "configure\nbuild\n"));
    assert(!unlink(filename));
    tool_write(calls, "");
    REQUIRE(tool_run(args, source, log_path) == 0);
    REQUIRE(file_equals(calls, "configure\nbuild\ngraph\n"));
    path(filename, source, "configure-arguments");
    REQUIRE(file_equals(
        filename,
        "sdk-native\nreflection build/host\n-DCMAKE_C_COMPILER=/fixture/cc\n"
        "-DCMAKE_CXX_COMPILER=/fixture/c++\n-DCMAKE_BUILD_TYPE=Release\n"
        "-DCHIBI_SDK_BUILD_COMPILER=ON\n-DCHIBI_SDK_BUILD_NATIVE=OFF\n"
        "-DCHIBI_SDK_BUILD_DESKTOP=OFF\n"
    ));
    path(filename, source, "build-arguments");
    REQUIRE(
        file_equals(filename, "--build\nreflection build/host\n--target\nchibi-build-host\n--parallel\n3\n")
    );
    path(filename, source, "graph-arguments");
    REQUIRE(file_equals(filename, expected));
    path(filename, source, "graph-program");
    REQUIRE(file_equals(filename, "reflection build/host/bin/chibi-build-host\n"));
    tool_write(calls, "");
    REQUIRE(tool_run(args, source, log_path) == 0);
    REQUIRE(file_equals(calls, "graph\n"));
    REQUIRE(file_equals(filename, "reflection build/host/bin/chibi-build-host\n"));

    path(filename, source, "fixture-host");
    tool_write(filename, host);
    assert(!chmod(filename, 0700));
    path(filename, source, "fixture-host.old");
    tool_write(filename, "");
    tool_write(calls, "");
    REQUIRE(tool_run(args, source, log_path) == 0);
    REQUIRE(file_equals(calls, "graph\n"));
    path(filename, source, "graph-program");
    REQUIRE(file_equals(filename, "reflection build/host/bin/chibi-build-host\n"));
    path(filename, source, "reflection build/host/bin/chibi-build-host.old");
    tool_write(filename, "");
    tool_write(calls, "");
    REQUIRE(tool_run(args, source, log_path) == 0);
    REQUIRE(file_equals(calls, "configure\nbuild\ngraph\n"));
    return true;
}

#ifdef __APPLE__
TEST_CASE(runtime_sdk_recipes_use_graph_programs_on_macos)
{
    char source[1024], canonical[1024], filename[1024], original[1024],
        invocations[1024], expected[8192];
    path(source, directory, "runtime recipe");
    assert(!mkdir(source, 0700));
    REQUIRE(realpath(source, canonical));
    path(filename, source, "tools");
    assert(!mkdir(filename, 0700));
    path(original, tool_root(), "Makefile");
    path(filename, source, "Makefile");
    const char *copy[] = { "cp", original, filename, NULL };
    REQUIRE(tool_run(copy, source, log_path) == 0);
    path(filename, source, "tools/quiet.sh");
    tool_write(filename, "#!/bin/sh\nshift 2\nexec \"$@\"\n");
    seed_step(source);
    path(filename, source, "fixture-host");
    tool_write(
        filename,
        "#!/bin/sh\nset -eu\n"
        "if [ \"$1\" = --protocol-version ]; then echo 14; exit 0; fi\n"
        "printf '%s\\n' \"$@\" >> invocations\n"
        "printf -- '--end\\n' >> invocations\n"
    );
    assert(!chmod(filename, 0700));
    path(invocations, source, "invocations");
    const char *args[] = {
        "make",
        "-o",
        "sdk-reflect-build",
        "package-runtime-sdk",
        "QUIET=sh tools/quiet.sh",
        "BUILD_HOST=./fixture-host",
        "SDK_REFLECT_BUILD_DIR=reflection build",
        "RUNTIME_SDK_BUILD_DIR=runtime-build",
        "RUNTIME_SDK_PREFIX=runtime-package",
        "JOBS=3",
        "NATIVE_TOOLCHAIN_FLAGS=-DCMAKE_C_COMPILER=/fixture/cc",
        "SDK_CMAKE_FLAGS=-DCMAKE_BUILD_TYPE=Release",
        NULL
    };
    REQUIRE(tool_run(args, source, log_path) == 0);
    snprintf(
        expected,
        sizeof expected,
        "--compiler\ntools/compiler\n--compiler-core\npackages/"
        "core\n--root\n%s\n"
        "--source\ntools/toolchain/Build.chibi\n--core\npackages/"
        "core\n--entry\nBuild\n"
        "--timeout-ms\n1800000\n--\nthird-party\nruntime-build\n--end\n"
        "--compiler\ntools/compiler\n--compiler-core\npackages/"
        "core\n--root\n%s\n"
        "--source\ntools/toolchain/Graph.chibi\n--core\npackages/core\n"
        "--collection\ngraph=tools/"
        "graph\n--entry\nRuntimeSDK\n--timeout-ms\n3600000\n"
        "--\n--build\n%s/runtime-build\n--reflect\n%s/reflection "
        "build/reflect\n"
        "--jobs\n3\n--\n-DCMAKE_C_COMPILER=/fixture/"
        "cc\n-DCMAKE_BUILD_TYPE=Release\n--end\n"
        "--compiler\ntools/compiler\n--compiler-core\npackages/"
        "core\n--root\n%s\n"
        "--source\ntools/toolchain/RuntimeSDK.chibi\n--core\npackages/core\n"
        "--collection\ngraph=tools/"
        "graph\n--entry\nRuntimePackage\n--timeout-ms\n3600000\n"
        "--\n--build\n%s/runtime-build\n--output\n%s/runtime-package\n--end\n",
        canonical,
        canonical,
        canonical,
        canonical,
        canonical,
        canonical,
        canonical
    );
    REQUIRE(file_equals(invocations, expected));
    return true;
}
#endif

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
    RUN_TEST_CASE(reflection_build_outputs_preserve_sdk_source_identity);
    RUN_TEST_CASE(sdk_reflection_recipe_uses_graph_and_recovers_its_host);
#ifdef __APPLE__
    RUN_TEST_CASE(runtime_sdk_recipes_use_graph_programs_on_macos);
#endif
    tool_remove_tree(directory);
}
