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

static const char *release_pins[] = {
    "linux-x64\trelease\tthird-party-fixture\t"
    "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\n",
    "web\trelease\tthird-party-fixture\t"
    "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\n",
    "web-player\trelease\tthird-party-fixture\t"
    "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\n",
};

static int publish_pins(int changed, const char *replacement)
{
    char script[1024], manifest[1024], text[1024] = "";
    path(script, tool_root(), "tools/check_publish_pins.sh");
    path(manifest, directory, "archives.tsv");
    for (int i = 0; i < 3; i++) {
        const char *row = i == changed ? replacement : release_pins[i];
        assert(strlen(text) + strlen(row) < sizeof text);
        strcat(text, row);
    }
    tool_write(manifest, text);
    const char *args[] = { "sh", script, manifest, NULL };
    return tool_run(args, directory, log_path);
}

TEST_CASE(publish_requires_valid_unique_release_pins)
{
    REQUIRE(publish_pins(-1, "") == 0);
    for (int i = 0; i < 3; i++) {
        REQUIRE(publish_pins(i, "") == 1);
        char *text = tool_read(log_path);
        REQUIRE(text);
        bool actionable = strstr(text, "Run the Third-party workflow") &&
                          strstr(text, "merge its archive-pin PR");
        free(text);
        REQUIRE(actionable);
    }
    REQUIRE(publish_pins(0, "linux-x64\trelease\tthird-party-fixture\tbad-sha\n") == 1);
    REQUIRE(
        publish_pins(
            0,
            "linux-x64\tthread\tthird-party-fixture\t"
            "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\n"
        ) == 1
    );
    REQUIRE(
        publish_pins(
            0,
            "linux-x64\trelease\t\t"
            "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\n"
        ) == 1
    );
    REQUIRE(
        publish_pins(
            0,
            "linux-x64\trelease\tbad tag\t"
            "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\n"
        ) == 1
    );
    REQUIRE(
        publish_pins(
            0,
            "linux-x64\trelease\tthird-party-fixture\t"
            "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\t"
            "extra\n"
        ) == 1
    );
    REQUIRE(
        publish_pins(
            0,
            "linux-x64\trelease\tthird-party-fixture\t"
            "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF\n"
        ) == 1
    );
    char duplicate[512];
    int n = snprintf(duplicate, sizeof duplicate, "%s%s", release_pins[0], release_pins[0]);
    assert(n > 0 && (size_t)n < sizeof duplicate);
    REQUIRE(publish_pins(0, duplicate) == 1);
    char manifest[1024], script[1024];
    path(manifest, directory, "archives.tsv");
    REQUIRE(unlink(manifest) == 0);
    path(script, tool_root(), "tools/check_publish_pins.sh");
    const char *args[] = { "sh", script, manifest, NULL };
    REQUIRE(tool_run(args, directory, log_path) == 1);
    return true;
}

TEST_CASE(publish_checks_pins_before_installing_toolchains)
{
    char filename[1024];
    path(filename, tool_root(), ".github/workflows/publish.yml");
    char *text = tool_read(filename);
    REQUIRE(text);
    const char *release = strstr(text, "      - name: Check immutable release\n");
    const char *preflight = strstr(
        text,
        "      - name: Validate SDK release pins\n"
        "        if: steps.release.outputs.complete != 'true'\n"
        "        run: sh tools/check_publish_pins.sh\n"
    );
    const char *install = strstr(text, "      - name: Install build dependencies\n");
    bool ordered = release && preflight && install && release < preflight &&
                   preflight < install;
    free(text);
    REQUIRE(ordered);
    return true;
}

TEST_CASE(publish_tracks_packaged_sources_and_licenses)
{
    char filename[1024];
    path(filename, tool_root(), ".github/workflows/publish.yml");
    char *text = tool_read(filename);
    REQUIRE(text);
    const char *inputs[] = {
        "      - 'packages/core/**'\n",
        "      - 'COPYING'\n",
        "      - 'COPYING.LESSER'\n",
        "      - 'COPYING.EXAMPLES'\n",
        "      - 'LICENSE.md'\n",
        "      - 'docs/distribution-licensing.md'\n",
    };
    bool covered = true;
    for (size_t i = 0; i < sizeof inputs / sizeof inputs[0]; i++) {
        covered = covered && strstr(text, inputs[i]);
    }
    free(text);
    REQUIRE(covered);
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
    RUN_TEST_CASE(publish_tracks_packaged_sources_and_licenses);
    RUN_TEST_CASE(publish_checks_pins_before_installing_toolchains);
    RUN_TEST_CASE(publish_requires_valid_unique_release_pins);
    RUN_TEST_CASE(discovered_crypto_has_relocatable_owners);
    RUN_TEST_CASE(supplied_crypto_keeps_its_provider_contract);
    RUN_TEST_CASE(mismatched_crypto_metadata_is_rejected);
    RUN_TEST_CASE(discovered_opengl_roles_relocate_but_unowned_paths_fail);
    RUN_TEST_CASE(tooling_languages_follow_native_packages);
    tool_remove_tree(directory);
}
