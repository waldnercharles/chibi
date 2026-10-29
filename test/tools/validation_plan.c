#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"
#include "third_party/yyjson/yyjson.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char directory[128], repo[160], driver[160], log_path[160], saved_plan[160];
static char contract[180], tools[1024];
static char *output;
static yyjson_doc *document;

static void path(char *destination, size_t size, const char *base, const char *name)
{
    int length = snprintf(destination, size, "%s/%s", base, name);
    assert(length > 0 && length < (int)size);
}

static int run(const char *const argv[], const char *cwd)
{
    free(output);
    output = NULL;
    yyjson_doc_free(document);
    document = NULL;
    int result = tool_run(argv, cwd, log_path);
    output = tool_read(log_path);
    assert(output);
    return result;
}

static void git(const char *first, const char *second, const char *third, const char *fourth)
{
    const char *argv[] = { "git", first, second, third, fourth, NULL };
    assert(!run(argv, repo));
}

static int plan(const char *first, const char *second, const char *third, const char *fourth)
{
    const char *argv[] = {
        "python3",
        driver,
        tools,
        repo,
        "validation-plan",
        "--base",
        "base",
        "--source",
        "source",
        "--confirm-eligible-members",
        "--scope",
        "focused",
        "--review-note",
        "Review of native tooling commands",
        first,
        second,
        third,
        fourth,
        NULL,
    };
    return run(argv, repo);
}

static yyjson_val *json()
{
    document = yyjson_read(output, strlen(output), 0);
    assert(document);
    return yyjson_doc_get_root(document);
}

static bool string_equals(yyjson_val *value, const char *expected)
{
    const char *actual = yyjson_get_str(value);
    return actual && !strcmp(actual, expected);
}

static bool only_command(yyjson_val *root, const char *field, const char *command)
{
    yyjson_val *commands = yyjson_obj_get(root, field);
    return yyjson_arr_size(commands) == 1 &&
           string_equals(yyjson_arr_get(commands, 0), command);
}

TEST_CASE(validation_identity_is_frozen_and_requires_clean_checkout)
{
    git("rev-parse", "HEAD", NULL, NULL);
    char head[64];
    snprintf(head, sizeof head, "%.*s", (int)strcspn(output, "\n"), output);
    git("rev-parse", "HEAD^{tree}", NULL, NULL);
    char tree[64];
    snprintf(tree, sizeof tree, "%.*s", (int)strcspn(output, "\n"), output);
    REQUIRE(plan("--selector=--source-systems", NULL, NULL, NULL) == 0);
    yyjson_val *root = json();
    yyjson_val *identity = yyjson_obj_get(root, "identity");
    REQUIRE(string_equals(yyjson_obj_get(identity, "head"), head));
    REQUIRE(string_equals(yyjson_obj_get(identity, "tree"), tree));
    REQUIRE(string_equals(yyjson_obj_get(yyjson_obj_get(identity, "sources"), "source"), head));
    REQUIRE(only_command(root, "iteration_commands", "make agent-test AGENT_SELECTOR=--source-systems"));
    REQUIRE(yyjson_is_false(yyjson_obj_get(root, "full_verification")));
    tool_write(saved_plan, output);
    REQUIRE(plan("--selector=--source-systems", "--verify", saved_plan, NULL) == 0);
    REQUIRE(string_equals(yyjson_obj_get(json(), "status"), "identity-current"));
    git("branch", "-f", "source", "base");
    REQUIRE(plan("--selector=--source-systems", "--verify", saved_plan, NULL) == 2);
    REQUIRE(strstr(output, "stale or changed plan"));
    git("branch", "-f", "source", "HEAD");
    tool_write(contract, "uncommitted change\n");
    int dirty_status = plan("--selector=--source-systems", "--verify", saved_plan, NULL);
    bool dirty_message = strstr(output, "clean worktree") != NULL;
    git("restore", "contract.txt", NULL, NULL);
    REQUIRE(dirty_status == 2);
    REQUIRE(dirty_message);
    return true;
}

TEST_CASE(validation_plans_native_tools_without_certifying_full_gate)
{
    REQUIRE(plan("--tools", NULL, NULL, NULL) == 0);
    yyjson_val *root = json();
    REQUIRE(only_command(root, "iteration_commands", "make test-tools"));
    REQUIRE(only_command(root, "integration_commands", "make check-format"));
    REQUIRE(yyjson_is_false(yyjson_obj_get(root, "full_verification")));
    REQUIRE(strstr(output, "Evidence covers only the checks actually run"));
    REQUIRE(plan("--tool=native-tools", NULL, NULL, NULL) == 0);
    REQUIRE(only_command(json(), "iteration_commands", "make test-tools"));
    REQUIRE(plan("--tool=language-record-values", NULL, NULL, NULL) == 0);
    REQUIRE(
        only_command(json(), "iteration_commands", "python3 tools/test_policy.py tool language-record-values")
    );
    REQUIRE(plan("--tool=quiet", NULL, NULL, NULL) == 2);
    REQUIRE(strstr(output, "invalid choice"));
    REQUIRE(plan("--tools", "--scope", "mixed", NULL) == 0);
    root = json();
    REQUIRE(only_command(root, "iteration_commands", "make test-tools"));
    REQUIRE(string_equals(yyjson_obj_get(root, "validation_basis"), "reviewed affected contracts"));
    REQUIRE(strstr(output, "Local or CI results"));
    REQUIRE(!strstr(output, "CI build matrix"));
    return true;
}

TEST_CASE(validation_requires_review_and_checks_for_mixed_scope)
{
    REQUIRE(plan("--tools", "--scope=mixed", "--review-note=", NULL) == 2);
    REQUIRE(strstr(output, "reviewed scope requires"));
    REQUIRE(plan("--scope", "mixed", NULL, NULL) == 2);
    REQUIRE(strstr(output, "reviewed scope requires"));
    REQUIRE(plan("--selector=--source-systems", "--scope", "mixed", NULL) == 0);
    yyjson_val *root = json();
    REQUIRE(only_command(root, "iteration_commands", "make agent-test AGENT_SELECTOR=--source-systems"));
    REQUIRE(yyjson_is_false(yyjson_obj_get(root, "full_verification")));
    REQUIRE(plan("--scope", "unknown", NULL, NULL) == 0);
    root = json();
    REQUIRE(string_equals(yyjson_obj_get(root, "validation_basis"), "scope review required"));
    REQUIRE(strstr(output, "Resolve unknown scope"));
    return true;
}

TEST_CASE(validation_routes_gpu_checks_and_refuses_unknown_selectors)
{
    const char *selectors[] = {
        "--selector=--scene3d",
        "--selector=--studio-source-edit",
        "--selector=--not-a-test",
    };
    for (int i = 0; i < 2; i++) {
        REQUIRE(plan(selectors[i], NULL, NULL, NULL) == 0);
        char command[128];
        snprintf(
            command,
            sizeof command,
            "make agent-gpu-test AGENT_SELECTOR=%s",
            selectors[i] + strlen("--selector=")
        );
        REQUIRE(only_command(json(), "iteration_commands", command));
    }
    REQUIRE(plan(selectors[2], NULL, NULL, NULL) == 2);
    REQUIRE(strstr(output, "unclassified selector"));
    return true;
}

TEST_CASE(make_selector_data_never_reaches_the_shell)
{
    char marker[160], makefile[1024], scripts[160], source[1024],
        destination[180], binary[1024];
    path(marker, sizeof marker, directory, "injected");
    path(makefile, sizeof makefile, tool_root(), "Makefile");
    path(scripts, sizeof scripts, directory, "tools");
    assert(!mkdir(scripts, 0700));
    const char *files[] = { "test_policy.py", "quiet.py" };
    for (int i = 0; i < 2; i++) {
        path(source, sizeof source, tools, files[i]);
        path(destination, sizeof destination, scripts, files[i]);
        assert(!symlink(source, destination));
    }
    snprintf(binary, sizeof binary, "TEST_BINARY=%s", CHIBI_POLICY_WORKER_PATH);
    const char *targets[] = { "test-fast", "agent-test" };
    const char *variables[] = { "TEST_ARGS", "AGENT_SELECTOR" };
    const char *suffixes[] = { "; touch %s", " && touch %s", " `touch %s`", " $$(touch %s)" };
    for (int i = 0; i < 2; i++) {
        for (int j = 0; j < 4; j++) {
            char suffix[256], assignment[512];
            snprintf(suffix, sizeof suffix, suffixes[j], marker);
            snprintf(assignment, sizeof assignment, "%s=%s%s", variables[i], i ? "--source-systems" : "exit 0", suffix);
            /* Skip building the worker that already runs this test. The real
               recipes must transport selector data without shell evaluation. */
            const char *argv[] = { "make", "-f",          makefile,
                                   "-o",   "tools-build", targets[i],
                                   binary, "QUIET=false", assignment,
                                   NULL };
            REQUIRE(run(argv, directory) == (i ? 2 : 0));
            REQUIRE(!strstr(output, "DISABLED"));
            REQUIRE(access(marker, F_OK) != 0);
            REQUIRE(!strstr(output, "configure-native"));
            REQUIRE(!strstr(output, "cmake --build"));
        }
    }
    return true;
}

void test_tool_validation_plan()
{
    strcpy(directory, "/tmp/chibi-validation-plan-XXXXXX");
    assert(mkdtemp(directory));
    path(repo, sizeof repo, directory, "repo");
    path(driver, sizeof driver, directory, "plan.py");
    path(log_path, sizeof log_path, directory, "output");
    path(saved_plan, sizeof saved_plan, directory, "saved.json");
    path(contract, sizeof contract, repo, "contract.txt");
    path(tools, sizeof tools, tool_root(), "tools");
    assert(!mkdir(repo, 0700));
    tool_write(
        driver,
        "import argparse, sys\n"
        "from pathlib import Path\n"
        "sys.path.insert(0, sys.argv[1])\n"
        "from agent import CPU, GPU\n"
        "from validation_plan import add_parser, validation_plan\n"
        "parser = argparse.ArgumentParser()\n"
        "add_parser(parser.add_subparsers(dest='action', required=True))\n"
        "args = parser.parse_args(sys.argv[3:])\n"
        "sys.exit(validation_plan(args, Path(sys.argv[2]), CPU, GPU))\n"
    );
    git("init", "-q", NULL, NULL);
    git("config", "user.name", "Native tooling tests", NULL);
    git("config", "user.email", "test@example.invalid", NULL);
    git("config", "core.hooksPath", "/dev/null", NULL);
    tool_write(contract, "base\n");
    git("add", "contract.txt", NULL, NULL);
    git("commit", "-qm", "base", NULL);
    git("branch", "base", NULL, NULL);
    tool_write(contract, "candidate\n");
    git("commit", "-qam", "candidate", NULL);
    git("branch", "source", NULL, NULL);
    RUN_TEST_CASE(validation_identity_is_frozen_and_requires_clean_checkout);
    RUN_TEST_CASE(validation_plans_native_tools_without_certifying_full_gate);
    RUN_TEST_CASE(validation_requires_review_and_checks_for_mixed_scope);
    RUN_TEST_CASE(validation_routes_gpu_checks_and_refuses_unknown_selectors);
    RUN_TEST_CASE(make_selector_data_never_reaches_the_shell);
    yyjson_doc_free(document);
    document = NULL;
    free(output);
    output = NULL;
    tool_remove_tree(directory);
}
