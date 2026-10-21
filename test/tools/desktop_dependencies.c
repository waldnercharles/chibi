#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char directory[1024], fixture[1024], root[1200], log_path[1024];

static void path(char *out, const char *base, const char *name)
{
    int n = snprintf(out, 1024, "%s/%s", base, name);
    assert(n > 0 && n < 1024);
}

static int configure(const char *name)
{
    char build[1024], producer[1024], prefix[1200], mode[100];
    path(build, directory, name);
    path(producer, fixture, "producer");
    int n = snprintf(prefix, sizeof prefix, "-DCMAKE_INSTALL_PREFIX=%s/%s/install", directory, name);
    assert(n > 0 && (size_t)n < sizeof prefix);
    n = snprintf(mode, sizeof mode, "-DCASE=%s", name);
    assert(n > 0 && (size_t)n < sizeof mode);
    const char *args[] = { "cmake", "-S", producer, "-B",   build, "-G",
                           "Ninja", root, mode,     prefix, NULL };
    return tool_run(args, directory, log_path);
}

static bool relocated(const char *name)
{
    REQUIRE(configure(name) == 0);
    char build[1024], prefix[1024], moved[1024], consumer[1024], consumer_build[1024];
    path(build, directory, name);
    path(prefix, build, "install");
    path(moved, build, "relocated SDK");
    const char *install[] = { "cmake", "--install", build, NULL };
    REQUIRE(tool_run(install, directory, log_path) == 0);
    REQUIRE(rename(prefix, moved) == 0);
    path(consumer, fixture, "consumer");
    path(consumer_build, build, "consumer");
    char prefix_arg[1200], mode[100];
    int n = snprintf(prefix_arg, sizeof prefix_arg, "-DPREFIX=%s", moved);
    assert(n > 0 && (size_t)n < sizeof prefix_arg);
    n = snprintf(mode, sizeof mode, "-DCASE=%s", name);
    assert(n > 0 && (size_t)n < sizeof mode);
    const char *args[] = { "cmake", "-S",    consumer,   "-B", consumer_build,
                           "-G",    "Ninja", prefix_arg, mode, NULL };
    REQUIRE(tool_run(args, directory, log_path) == 0);
    return true;
}

static bool rejected(const char *name, const char *message)
{
    int status = configure(name);
    char *text = tool_read(log_path);
    bool found = text && strstr(text, message);
    free(text);
    REQUIRE(status == 1);
    REQUIRE(found);
    return true;
}

TEST_CASE(discovered_crypto_has_relocatable_owners)
{
    REQUIRE(relocated("automatic"));
    return true;
}

TEST_CASE(supplied_crypto_keeps_its_provider_contract)
{
    REQUIRE(relocated("supplied"));
    return true;
}

TEST_CASE(mismatched_crypto_metadata_is_rejected)
{
    REQUIRE(
        rejected("mismatch", "OpenSSL metadata does not match s2n's selected crypto library/headers")
    );
    return true;
}

TEST_CASE(discovered_opengl_roles_relocate_but_unowned_paths_fail)
{
#ifdef __linux__
    REQUIRE(relocated("opengl"));
#else
    /* Only Linux recreates FindOpenGL's absolute library roles; other hosts
     * link frameworks or import libraries, so the same paths stay unowned. */
    REQUIRE(rejected("opengl", "unowned absolute link"));
#endif
    REQUIRE(rejected("unowned", "unowned absolute link"));
    return true;
}

TEST_CASE(tooling_languages_follow_native_packages)
{
    char producer[1024];
    path(producer, fixture, "languages");
    const char *modes[] = { "runtime", "scheduler", "render", "installed" };
    for (int i = 0; i < 4; i++) {
        char build[1024], mode[100];
        path(build, directory, modes[i]);
        int n = snprintf(mode, sizeof mode, "-DCASE=%s", modes[i]);
        assert(n > 0 && (size_t)n < sizeof mode);
        const char *args[] = { "cmake", "-S",    producer, "-B", build,
                               "-G",    "Ninja", root,     mode, NULL };
        REQUIRE(tool_run(args, directory, log_path) == 0);
    }
    return true;
}

void test_tool_desktop_dependencies()
{
    strcpy(directory, "/tmp/chibi-desktop-dependencies-XXXXXX");
    assert(mkdtemp(directory));
    path(fixture, tool_root(), "cmake/tests/desktop_dependencies");
    path(log_path, directory, "command.log");
    int n = snprintf(root, sizeof root, "-DCHIBI_ROOT=%s", tool_root());
    assert(n > 0 && (size_t)n < sizeof root);
    RUN_TEST_CASE(discovered_crypto_has_relocatable_owners);
    RUN_TEST_CASE(supplied_crypto_keeps_its_provider_contract);
    RUN_TEST_CASE(mismatched_crypto_metadata_is_rejected);
    RUN_TEST_CASE(discovered_opengl_roles_relocate_but_unowned_paths_fail);
    RUN_TEST_CASE(tooling_languages_follow_native_packages);
    tool_remove_tree(directory);
}
