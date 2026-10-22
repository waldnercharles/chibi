#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char directory[1024], repo[1024], state[1024], log_path[1024], script[1024];
static const char *tag = "third-party-2026-10-22";
static const char *names[] = { "linux-x64-address-undefined",
                               "linux-x64-release",
                               "linux-x64-thread",
                               "macos-arm64-address-undefined",
                               "macos-arm64-release",
                               "macos-arm64-thread",
                               "web-player-release",
                               "web-release",
                               "web-threads-release" };
static const char *environment[] = { "PATH",
                                     "RELEASE",
                                     "GITHUB_SHA",
                                     "GITHUB_REPOSITORY",
                                     "MOCK_GH_STATE" };
static char *saved[5];

static void path(char *out, const char *base, const char *name)
{
    int n = snprintf(out, 1024, "%s/%s", base, name);
    assert(n > 0 && n < 1024);
}

static int command(const char *text)
{
    const char *args[] = { "sh", "-eu", "-c", text, NULL };
    return tool_run(args, repo, log_path);
}

static bool setup(const char *name)
{
    char root[1024], bin[1024], file[1024], source[1024], search[8192];
    path(root, directory, name);
    REQUIRE(mkdir(root, 0700) == 0);
    path(repo, root, "repo");
    path(state, root, "state");
    path(bin, root, "bin");
    path(log_path, root, "command.log");
    REQUIRE(mkdir(repo, 0700) == 0 && mkdir(state, 0700) == 0 && mkdir(bin, 0700) == 0);
    path(file, bin, "gh");
    path(source, tool_root(), "test/fixtures/third_party_publish/gh");
    char *fixture = tool_read(source);
    REQUIRE(fixture);
    tool_write(file, fixture);
    free(fixture);
    REQUIRE(chmod(file, 0700) == 0);
    int n = snprintf(search, sizeof search, "%s:%s", bin, saved[0] ? saved[0] : "/usr/bin:/bin");
    REQUIRE(n > 0 && (size_t)n < sizeof search);
    REQUIRE(setenv("PATH", search, 1) == 0 && setenv("RELEASE", tag, 1) == 0);
    REQUIRE(setenv("GITHUB_REPOSITORY", "owner/repo", 1) == 0);
    REQUIRE(setenv("MOCK_GH_STATE", state, 1) == 0);
    REQUIRE(
        command(
            "git init --bare ../origin.git; git init -b main; "
            "git config user.name Fixture; git config user.email "
            "fixture@example.invalid; "
            "git config core.hooksPath /dev/null; mkdir third_party archives; "
            "printf 'old pins\\n' > third_party/archives.tsv; git add "
            "third_party/archives.tsv; "
            "git commit -m fixture; git remote add origin ../origin.git; git "
            "push origin main"
        ) == 0
    );
    REQUIRE(command("git rev-parse HEAD") == 0);
    char *sha = tool_read(log_path);
    REQUIRE(sha);
    sha[strcspn(sha, "\r\n")] = 0;
    REQUIRE(setenv("GITHUB_SHA", sha, 1) == 0);
    free(sha);
    REQUIRE(
        command(
            "mkdir archive-input; printf 'set(CHIBI_THIRD_PARTY 1)\\n' > "
            "archive-input/third-party.cmake; "
            "tar -czf fixture.tar.gz -C archive-input third-party.cmake"
        ) == 0
    );
    for (int i = 0; i < 9; i++) {
        char name[128];
        snprintf(name, sizeof name, "archives/third-party-%s.tar.gz", names[i]);
        path(file, repo, name);
        const char *copy[] = { "cp", "fixture.tar.gz", file, NULL };
        REQUIRE(tool_run(copy, repo, log_path) == 0);
    }
    return true;
}

static int publish(bool validate_only)
{
    const char *args[] = { "python3", script, validate_only ? "--validate-only" : NULL, NULL };
    return tool_run(args, repo, log_path);
}

static bool text_contains(const char *name, const char *needle)
{
    char file[1024];
    path(file, state, name);
    char *text = tool_read(file);
    bool found = text && strstr(text, needle);
    free(text);
    return found;
}

static bool rejected(const char *message)
{
    REQUIRE(publish(false) != 0);
    char *text = tool_read(log_path);
    bool found = text && strstr(text, message);
    free(text);
    REQUIRE(found);
    REQUIRE(!text_contains("calls", "release upload") && !text_contains("calls", "release create"));
    REQUIRE(!text_contains("calls", "pr create"));
    return true;
}

static bool existing_release(int count, bool bad_digest)
{
    REQUIRE(
        command("git tag \"$RELEASE\" \"$GITHUB_SHA\"; git push origin \"refs/tags/$RELEASE\"") == 0
    );
    REQUIRE(
        command(
            "if command -v sha256sum >/dev/null 2>&1; then sha256sum "
            "fixture.tar.gz; "
            "else shasum -a 256 fixture.tar.gz; fi"
        ) == 0
    );
    char *output = tool_read(log_path);
    REQUIRE(output);
    char digest[65];
    REQUIRE(sscanf(output, "%64s", digest) == 1);
    free(output);
    char file[1024], rows[4096] = "";
    struct stat info;
    path(file, repo, "fixture.tar.gz");
    REQUIRE(stat(file, &info) == 0);
    path(file, state, "release");
    tool_write(file, "");
    for (int i = 0; i < count; i++) {
        char row[384];
        snprintf(
            row,
            sizeof row,
            "{\"name\":\"third-party-%s.tar.gz\",\"state\":\"uploaded\","
            "\"size\":%lld,"
            "\"digest\":\"sha256:%s\"}\n",
            names[i],
            (long long)info.st_size,
            bad_digest ? "wrong" : digest
        );
        strcat(rows, row);
    }
    path(file, state, "assets");
    tool_write(file, rows);
    return true;
}

TEST_CASE(third_party_invalid_inputs_have_no_side_effects)
{
    REQUIRE(setup("invalid"));
    REQUIRE(setenv("RELEASE", "--unexpected-option", 1) == 0);
    REQUIRE(publish(true) != 0);
    REQUIRE(setenv("RELEASE", tag, 1) == 0);
    REQUIRE(command("rm archives/third-party-web-release.tar.gz") == 0);
    REQUIRE(rejected("exactly the nine"));
    REQUIRE(command("printf invalid > archives/third-party-web-release.tar.gz") == 0);
    REQUIRE(rejected("invalid gzip tar archive"));
    REQUIRE(
        command(
            "printf other > archive-input/not-marker; "
            "tar -czf archives/third-party-web-release.tar.gz -C archive-input "
            "not-marker"
        ) == 0
    );
    REQUIRE(rejected("exactly one root third-party.cmake"));
    REQUIRE(
        command(
            ": > archive-input/third-party.cmake; "
            "tar -czf archives/third-party-web-release.tar.gz -C archive-input "
            "third-party.cmake"
        ) == 0
    );
    REQUIRE(rejected("must be a nonempty regular file"));
    return true;
}

TEST_CASE(third_party_new_release_and_original_artifact_retry)
{
    REQUIRE(setup("new"));
    REQUIRE(publish(false) == 0);
    REQUIRE(text_contains("calls", "release create") && text_contains("calls", "pr create"));
    REQUIRE(!text_contains("calls", "--clobber"));
    REQUIRE(command("git diff --name-only \"$GITHUB_SHA\" HEAD") == 0);
    char *changed = tool_read(log_path);
    REQUIRE(changed && !strcmp(changed, "third_party/archives.tsv\n"));
    free(changed);
    REQUIRE(command("git rev-parse HEAD") == 0);
    char *head = tool_read(log_path);
    REQUIRE(head);
    head[strcspn(head, "\r\n")] = 0;
    char file[1024], pr[512];
    snprintf(
        pr,
        sizeof pr,
        "[{\"state\":\"open\",\"base\":{\"ref\":\"main\"},"
        "\"head\":{\"sha\":\"%s\",\"repo\":{\"full_name\":\"owner/repo\"}},"
        "\"html_url\":\"https://github.com/owner/repo/pull/1\"}]",
        head
    );
    free(head);
    path(file, state, "pr.json");
    tool_write(file, pr);
    path(file, state, "calls");
    tool_write(file, "");
    REQUIRE(publish(false) == 0);
    REQUIRE(!text_contains("calls", "release upload") && !text_contains("calls", "release create"));
    REQUIRE(!text_contains("calls", "pr create"));
    return true;
}

TEST_CASE(third_party_partial_release_only_adds_missing_archives)
{
    REQUIRE(setup("partial") && existing_release(4, false));
    REQUIRE(publish(false) == 0);
    REQUIRE(!text_contains("calls", "release create"));
    REQUIRE(
        !text_contains("calls", "release upload third-party-2026-10-22 archives/third-party-linux-x64-release.tar.gz")
    );
    REQUIRE(
        text_contains("calls", "release upload third-party-2026-10-22 archives/third-party-web-release.tar.gz")
    );
    return true;
}

TEST_CASE(third_party_mismatches_and_closed_prs_are_preserved)
{
    REQUIRE(setup("mismatch") && existing_release(1, true));
    REQUIRE(rejected("existing asset differs"));
    REQUIRE(setup("closed") && existing_release(4, false));
    char file[1024];
    path(file, state, "pr.json");
    tool_write(file, "[{\"state\":\"closed\"}]");
    REQUIRE(rejected("no automatic reopening"));
    REQUIRE(setup("unrelated") && existing_release(9, false));
    REQUIRE(
        command(
            "git switch -c \"pin-$RELEASE\"; printf other > unrelated; git add "
            "unrelated; "
            "git commit -m unrelated; git push origin HEAD"
        ) == 0
    );
    REQUIRE(rejected("unrelated changes"));
    return true;
}

TEST_CASE(third_party_source_and_read_failures_do_not_pin)
{
    REQUIRE(setup("wrong-source"));
    REQUIRE(
        command(
            "git switch -c other; printf other > unrelated; git add unrelated; "
            "git commit -m other; git tag \"$RELEASE\" HEAD; "
            "git push origin \"refs/tags/$RELEASE\""
        ) == 0
    );
    REQUIRE(command("git checkout main") == 0);
    REQUIRE(rejected("does not resolve to the workflow source commit"));
    REQUIRE(setup("denied") && existing_release(4, false));
    char file[1024];
    path(file, state, "api-denied");
    tool_write(file, "");
    REQUIRE(rejected("GitHub API read failed"));
    REQUIRE(setup("moved"));
    REQUIRE(command("git commit --allow-empty -m other >/dev/null; git rev-parse HEAD") == 0);
    char *other = tool_read(log_path);
    REQUIRE(other);
    path(file, state, "move-tag");
    tool_write(file, other);
    free(other);
    REQUIRE(command("git checkout --detach \"$GITHUB_SHA\"") == 0);
    REQUIRE(publish(false) != 0);
    char *output = tool_read(log_path);
    bool moved = output && strstr(output, "tag changed after upload");
    free(output);
    REQUIRE(moved);
    REQUIRE(!text_contains("writes", "pr create"));
    REQUIRE(command("git ls-remote --heads origin refs/heads/pin-$RELEASE") == 0);
    output = tool_read(log_path);
    bool absent = output && !*output;
    free(output);
    REQUIRE(absent);
    return true;
}

void test_tool_third_party_publish()
{
    strcpy(directory, "/tmp/chibi-third-party-publish-XXXXXX");
    assert(mkdtemp(directory));
    path(script, tool_root(), "tools/publish_third_party.py");
    for (int i = 0; i < 5; i++) {
        const char *value = getenv(environment[i]);
        saved[i] = value ? strdup(value) : NULL;
    }
    RUN_TEST_CASE(third_party_invalid_inputs_have_no_side_effects);
    RUN_TEST_CASE(third_party_new_release_and_original_artifact_retry);
    RUN_TEST_CASE(third_party_partial_release_only_adds_missing_archives);
    RUN_TEST_CASE(third_party_mismatches_and_closed_prs_are_preserved);
    RUN_TEST_CASE(third_party_source_and_read_failures_do_not_pin);
    for (int i = 0; i < 5; i++) {
        if (saved[i]) {
            assert(setenv(environment[i], saved[i], 1) == 0);
            free(saved[i]);
        } else {
            assert(unsetenv(environment[i]) == 0);
        }
    }
    tool_remove_tree(directory);
}
