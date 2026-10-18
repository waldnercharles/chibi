#include "assets/project.h"
#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
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

TEST_CASE(web_sdk_recipe_selects_ninja_and_configuration_definitions)
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
        "test \"$1\" = -G\ntest \"$2\" = Ninja\nshift 2\n"
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

/* Exercise the real Studio consumer CMake with a fixed, tiny SDK facade. The
 * facade has the 53 Studio packages and three acceptance-only capabilities;
 * only support owns code. No real SDK or Studio source is copied or built. */
TEST_CASE(studio_sdk_identity_invalidates_older_installed_inputs)
{
    const char *studio = getenv("CHIBI_STUDIO_SOURCE");
    REQUIRE(studio && studio[0] == '/' && strlen(studio) < 900);
    char original[1024], filename[1024], source[1024], sdk[1024];
    path(original, studio, "CMakeLists.txt");
    struct stat info;
    REQUIRE(!stat(original, &info) && S_ISREG(info.st_mode));
    path(source, directory, "source");
    path(sdk, directory, "sdk");
    path(build, directory, "build");
    static const char *const folders[] = { "source",
                                           "source/cmake",
                                           "source/src",
                                           "source/src/host",
                                           "source/src/editor",
                                           "source/src/editor/rmlui",
                                           "source/src/editor/rmlui/resources",
                                           "source/tests",
                                           "source/tests/acceptance",
                                           "source/projects",
                                           "source/projects/studio",
                                           "sdk",
                                           "sdk/bin",
                                           "sdk/include",
                                           "sdk/lib",
                                           "sdk/lib/cmake",
                                           "sdk/lib/cmake/Chibi",
                                           "sdk/share",
                                           "sdk/share/chibi" };
    for (int i = 0; i < (int)(sizeof folders / sizeof *folders); i++) {
        path(filename, directory, folders[i]);
        REQUIRE(!mkdir(filename, 0700));
    }
    path(filename, source, "CMakeLists.txt");
    REQUIRE(!symlink(original, filename));
    path(filename, source, "cmake/apply_patch.cmake");
    tool_write(filename, "# No dependency patches in this fixture.\n");
    path(filename, source, "cmake/zep_dependencies.cmake");
    tool_write(filename, "add_library(zep INTERFACE)\n");
    path(filename, source, "cmake/studio_sources.cmake");
    tool_write(filename, "set(STUDIO_SOURCES src/probe.c)\n");
    path(filename, sdk, "lib/cmake/Chibi/ChibiConfig.cmake");
    tool_write(
        filename,
        "get_filename_component(prefix \"${CMAKE_CURRENT_LIST_DIR}/../../..\" "
        "ABSOLUTE)\n"
        "set(Chibi_VERSION 0.0.0)\n"
        "set(Chibi_RESOURCE_DIR \"${prefix}/share/chibi\")\n"
        "foreach(name asset_authoring asset_json asset_text asset_values "
        "authoring\n"
        " behavior_runtime capabilities component_presentation "
        "declared_components ecs\n"
        " engine entity_runtime execution execution_contract field_catalog "
        "graph_authoring\n"
        " graph_compilation graph_model graph_catalog image_preparation "
        "input_map json\n"
        " material_runtime model_authoring model_runtime native_types "
        "physics_runtime\n"
        " platform project_compilation project_document project_preparation "
        "prototype_document\n"
        " reference_index render save_runtime scenario_document "
        "scene_authoring scene_document\n"
        " scene_runtime shader_preparation source_catalog source_presentation "
        "spatial_runtime\n"
        " studio_capabilities studio_graph text_source tilemap_runtime types "
        "ui ui_authoring\n"
        " ui_document value_document array_capabilities text_capabilities "
        "host_native_catalog)\n"
        " add_library(Chibi::${name} INTERFACE IMPORTED)\n"
        "endforeach()\n"
        "add_library(Chibi::support STATIC IMPORTED)\n"
        "set_target_properties(Chibi::support PROPERTIES\n"
        " IMPORTED_LOCATION \"${prefix}/lib/libprobe.a\"\n"
        " INTERFACE_INCLUDE_DIRECTORIES \"${prefix}/include\")\n"
    );
    static const char *const placeholders[] = {
        "source/src/host/state_file.c",
        "source/src/host/stacktrace.c",
        "source/src/studio_registry.c",
        "source/tests/acceptance/host.c",
        "source/tests/acceptance/application.c",
        "source/tests/acceptance/debugger.c",
        "source/projects/studio/fixture.txt",
        "source/src/editor/rmlui/resources/fixture.txt",
        "sdk/bin/chibi",
        "sdk/bin/chibi-compile",
        "sdk/bin/chibi-player"
    };
    for (int i = 0; i < (int)(sizeof placeholders / sizeof *placeholders); i++) {
        path(filename, directory, placeholders[i]);
        tool_write(filename, "/* Fixed fixture placeholder; never executed. */\n");
    }
    /* Untagged consumers can pass generation A. Their stale header values then
     * fail generation B, so this case detects invalidation rather than merely
     * requiring the new definition to exist. */
    path(filename, source, "src/probe.c");
    tool_write(
        filename,
        "#include <probe.h>\n#include <string.h>\n"
        "#ifndef CHIBI_STUDIO_SDK_BUILD_ID\n"
        "#define CHIBI_STUDIO_SDK_BUILD_ID SDK_BUILD_ID\n#endif\n"
        "int studio_generation(const char *id, int value) {\n"
        " return !strcmp(CHIBI_STUDIO_SDK_BUILD_ID, id) && SDK_GENERATION == "
        "value;\n}\n"
    );
    const char *main_source =
        "#include <probe.h>\n#include <stdio.h>\n#include <string.h>\n"
        "#ifndef CHIBI_STUDIO_SDK_BUILD_ID\n"
        "#define CHIBI_STUDIO_SDK_BUILD_ID SDK_BUILD_ID\n#endif\n"
        "int studio_generation(const char *, int);\nint sdk_generation(void);\n"
        "int main(int argc, char **argv) {\n"
        " if (argc != 2 || (strcmp(argv[1], \"1\") && strcmp(argv[1], \"2\"))) "
        "return 2;\n"
        " int value = argv[1][0] - '0';\n"
        " const char *id = value == 1\n"
        "  ? "
        "\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"\n"
        "  : "
        "\"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\";"
        "\n"
        " int valid = !strcmp(CHIBI_STUDIO_SDK_BUILD_ID, id) && SDK_GENERATION "
        "== value\n"
        "  && studio_generation(id, value) && sdk_generation() == value;\n"
        " if (!valid) fprintf(stderr, \"stale consumer: own=%s header=%d "
        "library=%d archive=%d expected=%d\\n\",\n"
        "  CHIBI_STUDIO_SDK_BUILD_ID, SDK_GENERATION, studio_generation(id, "
        "value), sdk_generation(), value);\n"
        " return !valid;\n}\n";
    path(filename, source, "src/host/main.c");
    tool_write(filename, main_source);
    path(filename, source, "tests/acceptance/main.c");
    tool_write(filename, main_source);
    path(filename, directory, "sdk-probe.c");
    tool_write(filename, "#include <probe.h>\nint sdk_generation(void) { return SDK_GENERATION; }\n");

    char header[1024], archive[1024], manifest[1024], host[1024], acceptance[1024];
    path(header, sdk, "include/probe.h");
    path(archive, sdk, "lib/libprobe.a");
    path(manifest, sdk, "share/chibi/package.json");
    path(host, build, "bin/chibi-dev");
    path(acceptance, build, "bin/studio-acceptance");
    char prefix[1200];
    snprintf(prefix, sizeof prefix, "-DChibi_DIR=%s/lib/cmake/Chibi", sdk);
#ifdef __APPLE__
    const char *link_flags = "-DCMAKE_EXE_LINKER_FLAGS=-Wl,-dead_strip";
#else
    const char *link_flags = "-DCMAKE_EXE_LINKER_FLAGS=-Wl,--gc-sections";
#endif
    const char *configure[] = {
        "cmake",
        "-S",
        source,
        "-B",
        build,
        "-G",
        "Ninja",
        prefix,
        "-DCMAKE_BUILD_TYPE=Release",
        "-DCMAKE_C_COMPILER=cc",
        "-DCMAKE_CXX_COMPILER=c++",
        "-DCMAKE_TOOLCHAIN_FILE=",
        "-DCMAKE_C_FLAGS=-ffunction-sections -fdata-sections",
        "-DCMAKE_C_FLAGS_RELEASE=-O0",
        "-DCMAKE_CXX_FLAGS=",
        "-DCMAKE_CXX_FLAGS_RELEASE=-O0",
        link_flags,
        "-DCHIBI_STUDIO_BUILD_ACCEPTANCE=ON",
        "-DFETCHCONTENT_FULLY_DISCONNECTED=ON",
        "-DFETCHCONTENT_UPDATES_DISCONNECTED=ON",
        NULL
    };
    const char *compile[] = { "cc",
                              "-ffunction-sections",
                              "-fdata-sections",
                              "-Isdk/include",
                              "-c",
                              "sdk-probe.c",
                              "-o",
                              "sdk-probe.o",
                              NULL };
    const char *pack[] = { "ar", "rcs", archive, "sdk-probe.o", NULL };
    const char *rebuild[] = {
        "cmake", "--build",  build,          "--parallel",
        "1",     "--target", "chibi-studio", "studio-acceptance",
        NULL
    };
    const char *identities[] = {
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
        "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
    };
    const struct timespec older[] = { { .tv_sec = 946684800 }, { .tv_sec = 946684800 } };
    for (int generation = 0; generation < 2; generation++) {
        char text[256];
        snprintf(
            text,
            sizeof text,
            "#define SDK_GENERATION %d\n#define SDK_BUILD_ID \"%s\"\n",
            generation + 1,
            identities[generation]
        );
        tool_write(header, text);
        snprintf(text, sizeof text, "{\"build_id\":\"%s\"}\n", identities[generation]);
        tool_write(manifest, text);
        REQUIRE(tool_run(compile, directory, log_path) == 0);
        REQUIRE(tool_run(pack, directory, log_path) == 0);
        const char *installed[] = { header, archive, manifest };
        for (int i = 0; i < 3; i++) {
            REQUIRE(!utimensat(AT_FDCWD, installed[i], older, 0));
            REQUIRE(!stat(installed[i], &info) && info.st_mtime == older[1].tv_sec);
        }
        if (generation) {
            REQUIRE(!stat(host, &info) && info.st_mtime > older[1].tv_sec);
            REQUIRE(!stat(acceptance, &info) && info.st_mtime > older[1].tv_sec);
        }
        REQUIRE(tool_run(configure, directory, log_path) == 0);
        REQUIRE(tool_run(rebuild, directory, log_path) == 0);
        const char *number = generation ? "2" : "1";
        const char *run_host[] = { host, number, NULL };
        const char *run_acceptance[] = { acceptance, number, NULL };
        REQUIRE(tool_run(run_host, directory, log_path) == 0);
        REQUIRE(tool_run(run_acceptance, directory, log_path) == 0);
    }
    return true;
}

void test_tool_studio_sdk_identity()
{
    strcpy(directory, "/tmp/chibi-studio-sdk-XXXXXX");
    assert(mkdtemp(directory));
    path(log_path, directory, "command.log");
    RUN_TEST_CASE(studio_sdk_identity_invalidates_older_installed_inputs);
    tool_remove_tree(directory);
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
    RUN_TEST_CASE(package_preserves_unrelated_output_and_rejects_nonruntime_packages);
    RUN_TEST_CASE(package_installation_preserves_game_and_rejects_corruption);
    RUN_TEST_CASE(warm_configuration_preserves_input_invalidation);
    RUN_TEST_CASE(web_sdk_recipe_selects_ninja_and_configuration_definitions);
    RUN_TEST_CASE(serial_web_package_disables_stale_threaded_entrypoints);
    tool_remove_tree(directory);
}
