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

TEST_CASE(s2n_fetch_omits_unused_verification_submodules)
{
    /* This imports only the production parser/key functions. It never prepares
     * a cache, retrieves a source, or invokes a dependency build. */
    const char *
        script = "import sys\n"
                 "from pathlib import Path\n"
                 "sys.path.insert(0, str(Path(sys.argv[1]) / 'tools'))\n"
                 "import fetchcontent_cache as cache\n"
                 "pin = cache.source_pins(Path(sys.argv[1]))['s2n']\n"
                 "old = dict(pin); old.pop('GIT_SUBMODULES', None)\n"
                 "print(int(pin.get('GIT_SUBMODULES') == ''), "
                 "int(cache.dependency_key(pin) != cache.dependency_key(old)), "
                 "int(pin['GIT_REPOSITORY'] == "
                 "'https://github.com/aws/s2n-tls.git' "
                 "and pin['GIT_TAG'] == 'v1.6.3'))\n";
    const char *args[] = { "python3", "-B", "-c", script, tool_root(), NULL };
    int status = tool_run(args, directory, log_path);
    char *text = tool_read(log_path);
    bool exact = text && !strcmp(text, "1 1 1\n");
    free(text);
    REQUIRE(status == 0);
    REQUIRE(exact);
    return true;
}

TEST_CASE(discovered_opengl_roles_relocate_but_unowned_paths_fail)
{
    REQUIRE(relocated("opengl"));
    REQUIRE(rejected("unowned", "unowned absolute link"));
    return true;
}

TEST_CASE(cached_crypto_imports_preserve_provider_on_reconfigure)
{
    char producer[1024], build[1024], consumer[1024], consumer_build[1024];
    path(producer, fixture, "cache-producer");
    path(build, directory, "cache");
    const char *generate[] = { "cmake", "-S",    producer, "-B", build,
                               "-G",    "Ninja", root,     NULL };
    REQUIRE(tool_run(generate, directory, log_path) == 0);
    char package[1200], provider[1200];
    int n = snprintf(package, sizeof package, "-DPACKAGE=%s/package", build);
    assert(n > 0 && (size_t)n < sizeof package);
    n = snprintf(provider, sizeof provider, "-DPROVIDER=%s/provider", build);
    assert(n > 0 && (size_t)n < sizeof provider);
    path(consumer, fixture, "cache-consumer");
    path(consumer_build, build, "consumer");
    const char *args[] = { "cmake", "-S",    consumer, "-B",     consumer_build,
                           "-G",    "Ninja", package,  provider, NULL };
    const char *names[] = { "package/chibi-dependencies.cmake",
                            "package/chibi-crypto.cmake",
                            "package/chibi-dependency-targets.cmake" };
    char *before[3] = { 0 };
    bool present = true, stable = true;
    for (int i = 0; i < 3; i++) {
        char filename[1024];
        path(filename, build, names[i]);
        before[i] = tool_read(filename);
        present &= before[i] != NULL;
    }
    int first = tool_run(args, directory, log_path);
    int second = first == 0 ? tool_run(args, directory, log_path) : -1;
    for (int i = 0; i < 3; i++) {
        char filename[1024];
        path(filename, build, names[i]);
        char *after = tool_read(filename);
        stable &= before[i] && after && !strcmp(before[i], after);
        free(before[i]);
        free(after);
    }
    REQUIRE(present);
    REQUIRE(first == 0);
    REQUIRE(second == 0);
    REQUIRE(stable);
    return true;
}

TEST_CASE(sdk_scheduler_avoids_duplicate_enkits_acquisition)
{
    char producer[1024], build[1024];
    path(producer, fixture, "enkits");
    path(build, directory, "enkits");
    const char *args[] = { "cmake", "-S",    producer, "-B", build,
                           "-G",    "Ninja", root,     NULL };
    REQUIRE(tool_run(args, directory, log_path) == 0);
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
    RUN_TEST_CASE(s2n_fetch_omits_unused_verification_submodules);
    RUN_TEST_CASE(discovered_opengl_roles_relocate_but_unowned_paths_fail);
    RUN_TEST_CASE(cached_crypto_imports_preserve_provider_on_reconfigure);
    RUN_TEST_CASE(sdk_scheduler_avoids_duplicate_enkits_acquisition);
    RUN_TEST_CASE(tooling_languages_follow_native_packages);
    tool_remove_tree(directory);
}
