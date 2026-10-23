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
                                     "MOCK_GH_STATE",
                                     "SOURCE_PR",
                                     "SOURCE_MAIN_SHA",
                                     "GITHUB_EVENT_NAME",
                                     "GITHUB_REF_TYPE",
                                     "GITHUB_REF_NAME" };
enum
{
    ENVIRONMENT_COUNT = sizeof environment / sizeof environment[0]
};
static char *saved[ENVIRONMENT_COUNT];

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
    for (int i = 5; i < ENVIRONMENT_COUNT; i++) {
        REQUIRE(unsetenv(environment[i]) == 0);
    }
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

static bool prepared_source(const char *name)
{
    REQUIRE(setup(name));
    REQUIRE(setenv("SOURCE_PR", "2", 1) == 0);
    REQUIRE(setenv("SOURCE_MAIN_SHA", getenv("GITHUB_SHA"), 1) == 0);
    REQUIRE(setenv("GITHUB_EVENT_NAME", "workflow_dispatch", 1) == 0);
    REQUIRE(setenv("GITHUB_REF_TYPE", "branch", 1) == 0);
    REQUIRE(setenv("GITHUB_REF_NAME", "prepared-source", 1) == 0);
    REQUIRE(
        command(
            "git switch -c prepared-source; printf source > prepared.txt; git "
            "add prepared.txt; "
            "git commit -m prepared-source; git push origin HEAD"
        ) == 0
    );
    REQUIRE(command("git rev-parse HEAD") == 0);
    char *sha = tool_read(log_path);
    REQUIRE(sha);
    sha[strcspn(sha, "\r\n")] = 0;
    REQUIRE(setenv("GITHUB_SHA", sha, 1) == 0);
    free(sha);
    char file[1024], pr[1024];
    int n = snprintf(
        pr,
        sizeof pr,
        "[{\"number\":2,\"state\":\"open\",\"draft\":false,"
        "\"head\":{\"repo\":{\"full_name\":\"owner/repo\"},"
        "\"ref\":\"prepared-source\",\"sha\":\"%s\"},"
        "\"base\":{\"repo\":{\"full_name\":\"owner/repo\"},"
        "\"ref\":\"main\",\"sha\":\"%s\"}}]",
        getenv("GITHUB_SHA"),
        getenv("SOURCE_MAIN_SHA")
    );
    REQUIRE(n > 0 && (size_t)n < sizeof pr);
    path(file, state, "source-pr.json");
    tool_write(file, pr);
    return true;
}

static bool record_variant(const char *name, const char *original, const char *from, const char *to)
{
    const char *match = strstr(original, from);
    REQUIRE(match);
    char record[2048], file[1024];
    int n = snprintf(
        record,
        sizeof record,
        "%.*s%s%s",
        (int)(match - original),
        original,
        to,
        match + strlen(from)
    );
    REQUIRE(n > 0 && (size_t)n < sizeof record);
    path(file, state, name);
    tool_write(file, record);
    return true;
}

static int publish(bool validate_only)
{
    /* One publication checks source/ref/API state around up to nine uploads,
     * through real Git and Python children. Still cancel a stalled publisher. */
    enum
    {
        PUBLICATION_SECONDS = 120
    };
    const char *args[] = { "python3", script, validate_only ? "--validate-only" : NULL, NULL };
    return tool_run_within(args, repo, log_path, PUBLICATION_SECONDS);
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
    REQUIRE(command("git ls-remote origin") == 0);
    char *before = tool_read(log_path);
    REQUIRE(before);
    REQUIRE(publish(false) != 0);
    char *text = tool_read(log_path);
    bool found = text && strstr(text, message);
    free(text);
    REQUIRE(found);
    REQUIRE(!text_contains("calls", "release upload") && !text_contains("calls", "release create"));
    REQUIRE(!text_contains("calls", "pr create"));
    REQUIRE(command("git ls-remote origin") == 0);
    char *after = tool_read(log_path);
    bool unchanged = after && !strcmp(before, after);
    free(before);
    free(after);
    REQUIRE(unchanged);
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

static bool prepared_pin_pr(bool draft, bool binding)
{
    REQUIRE(command("git rev-parse HEAD") == 0);
    char *head = tool_read(log_path);
    REQUIRE(head);
    head[strcspn(head, "\r\n")] = 0;
    char file[1024], marker[256], pr[1024];
    int n = snprintf(
        marker,
        sizeof marker,
        "<!-- chibi-prepared-source: pr=2 source=%s main=%s -->",
        getenv("GITHUB_SHA"),
        getenv("SOURCE_MAIN_SHA")
    );
    REQUIRE(n > 0 && (size_t)n < sizeof marker);
    n = snprintf(
        pr,
        sizeof pr,
        "[{\"state\":\"open\",\"draft\":%s,\"body\":\"%s\","
        "\"base\":{\"ref\":\"main\"},\"head\":{\"sha\":\"%s\","
        "\"repo\":{\"full_name\":\"owner/repo\"}},"
        "\"html_url\":\"https://github.com/owner/repo/pull/1\"}]",
        draft ? "true" : "false",
        binding ? marker : "Owner changed the binding",
        head
    );
    free(head);
    REQUIRE(n > 0 && (size_t)n < sizeof pr);
    path(file, state, "pr.json");
    tool_write(file, pr);
    return true;
}

static bool rejected_after_upload(const char *message)
{
    REQUIRE(publish(false) != 0);
    char *output = tool_read(log_path);
    bool found = output && strstr(output, message);
    free(output);
    REQUIRE(found);
    REQUIRE(text_contains("calls", "release upload"));
    REQUIRE(!text_contains("calls", "pr create"));
    REQUIRE(command("git ls-remote --heads origin refs/heads/pin-$RELEASE") == 0);
    output = tool_read(log_path);
    bool absent = output && !*output;
    free(output);
    REQUIRE(absent);
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

TEST_CASE(third_party_complete_release_and_main_pins_are_unchanged)
{
    REQUIRE(setup("complete") && publish(false) == 0);
    REQUIRE(
        command("git switch main; git merge --ff-only \"pin-$RELEASE\"; git push origin main") == 0
    );
    char file[1024];
    path(file, state, "calls");
    tool_write(file, "");
    path(file, state, "pr.json");
    tool_write(file, "[{\"state\":\"closed\"}]");
    REQUIRE(command("git ls-remote origin") == 0);
    char *before = tool_read(log_path);
    REQUIRE(before);
    REQUIRE(publish(false) == 0);
    char *output = tool_read(log_path);
    bool complete = output &&
                    strstr(output, "Pins already match main; no branch or PR changes.");
    free(output);
    REQUIRE(complete);
    REQUIRE(!text_contains("calls", "release upload") && !text_contains("calls", "release create"));
    REQUIRE(!text_contains("calls", "pr create"));
    REQUIRE(command("git ls-remote origin") == 0);
    char *after = tool_read(log_path);
    bool unchanged = after && !strcmp(before, after);
    free(before);
    free(after);
    REQUIRE(unchanged);
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

TEST_CASE(third_party_retargeted_and_fork_owned_prs_are_preserved)
{
    REQUIRE(setup("pr-ownership") && existing_release(4, false));
    char file[1024];
    path(file, state, "pr.json");
    const char *prs[] = {
        "[{\"state\":\"open\",\"base\":{\"ref\":\"release\"},"
        "\"head\":{\"repo\":{\"full_name\":\"owner/repo\"}}}]",
        "[{\"state\":\"open\",\"base\":{\"ref\":\"main\"},"
        "\"head\":{\"repo\":{\"full_name\":\"other/repo\"}}}]",
    };
    for (int i = 0; i < 2; i++) {
        tool_write(file, prs[i]);
        REQUIRE(rejected("no automatic reopening"));
    }
    return true;
}

TEST_CASE(third_party_different_pin_contents_and_source_parents_are_preserved)
{
    REQUIRE(setup("different-pins") && existing_release(9, false));
    REQUIRE(
        command(
            "git switch -c \"pin-$RELEASE\"; printf different > "
            "third_party/archives.tsv; "
            "git add third_party/archives.tsv; git commit -m different-pins; "
            "git push origin HEAD"
        ) == 0
    );
    REQUIRE(rejected("different archive pins"));
    REQUIRE(setup("different-parent") && existing_release(9, false));
    REQUIRE(
        command(
            "git commit --allow-empty -m different-parent; git switch -c "
            "\"pin-$RELEASE\"; "
            "printf different > third_party/archives.tsv; git add "
            "third_party/archives.tsv; "
            "git commit -m different-pins; git push origin HEAD"
        ) == 0
    );
    REQUIRE(rejected("not a single commit on this workflow source"));
    return true;
}

TEST_CASE(third_party_unverified_and_ambiguous_assets_are_preserved)
{
    REQUIRE(setup("asset-metadata") && existing_release(1, false));
    char file[1024], rows[1024];
    path(file, state, "assets");
    char *original = tool_read(file);
    REQUIRE(original);
    int n = snprintf(rows, sizeof rows, "%s", original);
    REQUIRE(n > 0 && (size_t)n < sizeof rows);
    char *digest = strstr(rows, ",\"digest\":");
    REQUIRE(digest);
    strcpy(digest, "}\n");
    tool_write(file, rows);
    REQUIRE(rejected("existing asset differs or has no verified digest"));
    n = snprintf(rows, sizeof rows, "%s%s", original, original);
    REQUIRE(n > 0 && (size_t)n < sizeof rows);
    tool_write(file, rows);
    REQUIRE(rejected("duplicate or unexpected assets"));
    tool_write(file, "{\"name\":\"unexpected.tar.gz\"}\n");
    REQUIRE(rejected("duplicate or unexpected assets"));
    free(original);
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
    bool moved = output && strstr(output, "tag changed");
    free(output);
    REQUIRE(moved);
    REQUIRE(!text_contains("calls", "pr create"));
    REQUIRE(command("git ls-remote --heads origin refs/heads/pin-$RELEASE") == 0);
    output = tool_read(log_path);
    bool absent = output && !*output;
    free(output);
    REQUIRE(absent);
    return true;
}

TEST_CASE(third_party_prepared_source_requires_paired_manual_branch_inputs)
{
    REQUIRE(prepared_source("prepared-inputs"));
    char main[41];
    strcpy(main, getenv("SOURCE_MAIN_SHA"));
    const char *variables[] = { "SOURCE_PR",
                                "SOURCE_MAIN_SHA",
                                "GITHUB_EVENT_NAME",
                                "GITHUB_REF_TYPE",
                                "GITHUB_REF_NAME" };
    const char *invalid[] = { "", "", "pull_request", "tag", "main" };
    const char *valid[] = { "2", main, "workflow_dispatch", "branch", "prepared-source" };
    for (int i = 0; i < 5; i++) {
        REQUIRE(setenv(variables[i], invalid[i], 1) == 0);
        REQUIRE(rejected("prepared source"));
        REQUIRE(setenv(variables[i], valid[i], 1) == 0);
    }
    REQUIRE(setenv("GITHUB_REF_NAME", "pin-third-party-2026-10-22", 1) == 0);
    REQUIRE(rejected("prepared source"));
    REQUIRE(setenv("GITHUB_REF_NAME", "prepared-source", 1) == 0);
    REQUIRE(setenv("SOURCE_MAIN_SHA", getenv("GITHUB_SHA"), 1) == 0);
    REQUIRE(rejected("prepared source"));
    return true;
}

TEST_CASE(third_party_prepared_source_creates_and_reuses_only_bound_draft_prs)
{
    REQUIRE(prepared_source("prepared-success"));
    char file[1024], marker[256];
    path(file, state, "source-pr.json");
    char *original = tool_read(file);
    REQUIRE(original);
    REQUIRE(publish(false) == 0);
    REQUIRE(text_contains("calls", "pr create") && text_contains("calls", "--draft"));
    int n = snprintf(
        marker,
        sizeof marker,
        "<!-- chibi-prepared-source: pr=2 source=%s main=%s -->",
        getenv("GITHUB_SHA"),
        getenv("SOURCE_MAIN_SHA")
    );
    REQUIRE(n > 0 && (size_t)n < sizeof marker);
    REQUIRE(text_contains("calls", marker));
    REQUIRE(text_contains("calls", "https://github.com/owner/repo/pull/2"));
    REQUIRE(!text_contains("calls", "--clobber"));
    REQUIRE(command("git diff --name-only \"$GITHUB_SHA\" HEAD") == 0);
    char *changed = tool_read(log_path);
    bool pin_only = changed && !strcmp(changed, "third_party/archives.tsv\n");
    free(changed);
    REQUIRE(pin_only);
    REQUIRE(command("git show -s --format=%P HEAD") == 0);
    char *parent = tool_read(log_path);
    REQUIRE(parent);
    parent[strcspn(parent, "\r\n")] = 0;
    bool source_parent = !strcmp(parent, getenv("GITHUB_SHA"));
    free(parent);
    REQUIRE(source_parent);
    REQUIRE(prepared_pin_pr(true, true));
    path(file, state, "calls");
    tool_write(file, "");
    REQUIRE(publish(false) == 0);
    REQUIRE(!text_contains("calls", "release create") && !text_contains("calls", "release upload"));
    REQUIRE(!text_contains("calls", "pr create"));
    path(file, state, "source-pr.json");
    char *after = tool_read(file);
    bool unchanged = after && !strcmp(original, after);
    free(original);
    free(after);
    REQUIRE(unchanged);
    REQUIRE(prepared_pin_pr(false, true));
    REQUIRE(rejected("prepared integration PR"));
    REQUIRE(prepared_pin_pr(true, false));
    REQUIRE(rejected("prepared integration PR"));
    return true;
}

TEST_CASE(third_party_prepared_source_encodes_permitted_branch_names)
{
    REQUIRE(prepared_source("prepared-encoded-ref"));
    REQUIRE(
        command(
            "git switch -c 'prepared/source#review&1'; "
            "git push origin 'HEAD:refs/heads/prepared/source#review&1'"
        ) == 0
    );
    REQUIRE(setenv("GITHUB_REF_NAME", "prepared/source#review&1", 1) == 0);
    char file[1024];
    path(file, state, "source-pr.json");
    char *original = tool_read(file);
    REQUIRE(original);
    REQUIRE(
        record_variant("source-pr.json", original, "\"ref\":\"prepared-source\"", "\"ref\":\"prepared/source#review&1\"")
    );
    free(original);
    REQUIRE(publish(false) == 0);
    REQUIRE(text_contains("calls", "pulls?head=owner:prepared%2Fsource%23review%261&state=all"));
    REQUIRE(text_contains("calls", "pr create") && text_contains("calls", "--draft"));
    return true;
}

TEST_CASE(third_party_prepared_source_rejects_changed_or_ambiguous_source_prs)
{
    REQUIRE(prepared_source("prepared-pr"));
    char file[1024];
    path(file, state, "source-pr.json");
    char *original = tool_read(file);
    REQUIRE(original);
    const char *from[] = {
        "\"number\":2",
        "\"state\":\"open\"",
        "\"draft\":false",
        "\"head\":{\"repo\":{\"full_name\":\"owner/repo\"}",
        "\"base\":{\"repo\":{\"full_name\":\"owner/repo\"}",
        "\"ref\":\"main\"",
        "\"ref\":\"prepared-source\"",
    };
    const char *to[] = {
        "\"number\":3",
        "\"state\":\"closed\"",
        "\"draft\":true",
        "\"head\":{\"repo\":{\"full_name\":\"fork/repo\"}",
        "\"base\":{\"repo\":{\"full_name\":\"fork/repo\"}",
        "\"ref\":\"release\"",
        "\"ref\":\"other-source\"",
    };
    for (int i = 0; i < 7; i++) {
        REQUIRE(record_variant("source-pr.json", original, from[i], to[i]));
        REQUIRE(rejected("prepared source"));
    }
    const char *different_sha = "0000000000000000000000000000000000000000";
    REQUIRE(record_variant("source-pr.json", original, getenv("GITHUB_SHA"), different_sha));
    REQUIRE(rejected("prepared source"));
    REQUIRE(record_variant("source-pr.json", original, getenv("SOURCE_MAIN_SHA"), different_sha));
    REQUIRE(rejected("prepared source"));
    char ambiguous[2048];
    int length = (int)strlen(original);
    REQUIRE(length > 2 && original[0] == '[' && original[length - 1] == ']');
    int n = snprintf(
        ambiguous,
        sizeof ambiguous,
        "[%.*s,%.*s]",
        length - 2,
        original + 1,
        length - 2,
        original + 1
    );
    REQUIRE(n > 0 && (size_t)n < sizeof ambiguous);
    tool_write(file, ambiguous);
    REQUIRE(rejected("prepared source"));
    tool_write(file, "[]");
    REQUIRE(rejected("prepared source"));
    free(original);
    return true;
}

TEST_CASE(third_party_prepared_source_rejects_remote_main_and_source_drift)
{
    REQUIRE(prepared_source("prepared-main-drift"));
    REQUIRE(command("git push origin HEAD:refs/heads/main") == 0);
    REQUIRE(rejected("prepared source main changed"));
    REQUIRE(prepared_source("prepared-source-drift"));
    REQUIRE(
        command(
            "git commit --allow-empty -m source-drift; git push origin "
            "HEAD:refs/heads/prepared-source; "
            "git checkout --detach \"$GITHUB_SHA\""
        ) == 0
    );
    REQUIRE(rejected("prepared source branch changed"));
    return true;
}

TEST_CASE(third_party_prepared_source_drift_during_upload_never_pins)
{
    REQUIRE(prepared_source("prepared-pr-after-upload"));
    char file[1024];
    path(file, state, "source-pr.json");
    char *original = tool_read(file);
    REQUIRE(original);
    REQUIRE(
        record_variant("source-pr-after-upload.json", original, "\"state\":\"open\"", "\"state\":\"closed\"")
    );
    free(original);
    REQUIRE(rejected_after_upload("prepared source"));
    REQUIRE(prepared_source("prepared-main-after-upload"));
    path(file, state, "move-main");
    tool_write(file, getenv("GITHUB_SHA"));
    REQUIRE(rejected_after_upload("prepared source main changed"));
    REQUIRE(prepared_source("prepared-source-after-upload"));
    REQUIRE(command("git commit --allow-empty -m source-drift") == 0);
    REQUIRE(command("git rev-parse HEAD") == 0);
    char *next = tool_read(log_path);
    REQUIRE(next);
    path(file, state, "move-source");
    tool_write(file, next);
    free(next);
    REQUIRE(command("git checkout --detach \"$GITHUB_SHA\"") == 0);
    REQUIRE(rejected_after_upload("prepared source branch changed"));
    return true;
}

TEST_CASE(third_party_prepared_source_preserves_pin_races_before_push)
{
    const char *cases[] = { "prepared-pin-branch-race", "prepared-pin-pr-race" };
    const char *markers[] = { "pin-branch-after-local-commit",
                              "pin-pr-after-local-commit.json" };
    const char *closed = "[{\"state\":\"closed\"}]";
    for (int i = 0; i < 2; i++) {
        REQUIRE(prepared_source(cases[i]));
        char file[1024];
        path(file, state, markers[i]);
        tool_write(file, i == 0 ? getenv("GITHUB_SHA") : closed);
        REQUIRE(publish(false) != 0);
        char *output = tool_read(log_path);
        bool refused = output &&
                       strstr(output, "pin branch or PR appeared before creation; refusing to push");
        free(output);
        REQUIRE(refused);
        REQUIRE(access(file, F_OK) != 0);
        REQUIRE(text_contains("calls", "release upload"));
        REQUIRE(!text_contains("calls", "pr create"));
        REQUIRE(command("git show -s --format=%P HEAD") == 0);
        char *parent = tool_read(log_path);
        REQUIRE(parent);
        parent[strcspn(parent, "\r\n")] = 0;
        bool source_parent = !strcmp(parent, getenv("GITHUB_SHA"));
        free(parent);
        REQUIRE(source_parent);
        REQUIRE(command("git ls-remote --heads origin refs/heads/pin-$RELEASE") == 0);
        char *remote = tool_read(log_path);
        REQUIRE(remote);
        char expected[256] = "";
        if (i == 0) {
            int n = snprintf(expected, sizeof expected, "%s\trefs/heads/pin-%s\n", getenv("GITHUB_SHA"), tag);
            REQUIRE(n > 0 && (size_t)n < sizeof expected);
        }
        bool unchanged = !strcmp(remote, expected);
        free(remote);
        REQUIRE(unchanged);
        if (i == 1) {
            path(file, state, "pr.json");
            char *pr = tool_read(file);
            bool preserved = pr && !strcmp(pr, closed);
            free(pr);
            REQUIRE(preserved);
        }
    }
    return true;
}

TEST_CASE(third_party_prepared_merged_pins_allow_only_a_complete_release_noop)
{
    REQUIRE(prepared_source("prepared-merged") && publish(false) == 0);
    REQUIRE(
        command("git switch main; git merge --ff-only \"pin-$RELEASE\"; git push origin main") == 0
    );
    char file[1024];
    path(file, state, "source-pr.json");
    char *original = tool_read(file);
    REQUIRE(original);
    REQUIRE(record_variant("source-pr.json", original, "\"state\":\"open\"", "\"state\":\"closed\""));
    free(original);
    path(file, state, "calls");
    tool_write(file, "");
    REQUIRE(command("git ls-remote origin") == 0);
    char *before = tool_read(log_path);
    REQUIRE(before);
    REQUIRE(publish(false) == 0);
    REQUIRE(!text_contains("calls", "release create") && !text_contains("calls", "release upload"));
    REQUIRE(!text_contains("calls", "pr create"));
    REQUIRE(command("git ls-remote origin") == 0);
    char *after = tool_read(log_path);
    bool unchanged = after && !strcmp(before, after);
    free(before);
    free(after);
    REQUIRE(unchanged);
    path(file, state, "assets");
    char *assets = tool_read(file);
    REQUIRE(assets);
    char *next = strchr(assets, '\n');
    REQUIRE(next && next[1]);
    tool_write(file, next + 1);
    free(assets);
    REQUIRE(rejected("prepared source binding required for incomplete release recovery"));
    return true;
}

void test_tool_third_party_publish()
{
    strcpy(directory, "/tmp/chibi-third-party-publish-XXXXXX");
    assert(mkdtemp(directory));
    path(script, tool_root(), "tools/publish_third_party.py");
    for (int i = 0; i < ENVIRONMENT_COUNT; i++) {
        const char *value = getenv(environment[i]);
        saved[i] = value ? strdup(value) : NULL;
    }
    RUN_TEST_CASE(third_party_invalid_inputs_have_no_side_effects);
    RUN_TEST_CASE(third_party_new_release_and_original_artifact_retry);
    RUN_TEST_CASE(third_party_partial_release_only_adds_missing_archives);
    RUN_TEST_CASE(third_party_complete_release_and_main_pins_are_unchanged);
    RUN_TEST_CASE(third_party_mismatches_and_closed_prs_are_preserved);
    RUN_TEST_CASE(third_party_retargeted_and_fork_owned_prs_are_preserved);
    RUN_TEST_CASE(third_party_different_pin_contents_and_source_parents_are_preserved);
    RUN_TEST_CASE(third_party_unverified_and_ambiguous_assets_are_preserved);
    RUN_TEST_CASE(third_party_source_and_read_failures_do_not_pin);
    RUN_TEST_CASE(third_party_prepared_source_requires_paired_manual_branch_inputs);
    RUN_TEST_CASE(third_party_prepared_source_creates_and_reuses_only_bound_draft_prs);
    RUN_TEST_CASE(third_party_prepared_source_encodes_permitted_branch_names);
    RUN_TEST_CASE(third_party_prepared_source_rejects_changed_or_ambiguous_source_prs);
    RUN_TEST_CASE(third_party_prepared_source_rejects_remote_main_and_source_drift);
    RUN_TEST_CASE(third_party_prepared_source_drift_during_upload_never_pins);
    RUN_TEST_CASE(third_party_prepared_source_preserves_pin_races_before_push);
    RUN_TEST_CASE(third_party_prepared_merged_pins_allow_only_a_complete_release_noop);
    for (int i = 0; i < ENVIRONMENT_COUNT; i++) {
        if (saved[i]) {
            assert(setenv(environment[i], saved[i], 1) == 0);
            free(saved[i]);
        } else {
            assert(unsetenv(environment[i]) == 0);
        }
    }
    tool_remove_tree(directory);
}
