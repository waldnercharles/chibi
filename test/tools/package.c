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
    tool_remove_tree(directory);
}
