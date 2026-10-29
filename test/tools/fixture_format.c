#include "assets/project.h"
#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static const char *program;
static char scratch[64];
static const char *const projects[] = { "test/fixtures/behavior-catalog",
                                        "test/fixtures/component-presentation",
                                        "test/fixtures/courtyard",
                                        "test/fixtures/declared-components",
                                        "test/fixtures/feature-coverage",
                                        "test/fixtures/graph-routing",
                                        "test/fixtures/language",
                                        "test/fixtures/material-contracts",
                                        "test/fixtures",
                                        "test/fixtures/scene3d",
                                        "test/fixtures/sprites",
                                        "test/fixtures/studio-workspace",
                                        "test/fixtures/studio" };

static void cleanup()
{
    if (scratch[0]) {
        tool_remove_tree(scratch);
        scratch[0] = 0;
    }
}

TEST_CASE(fixture_projects_are_canonically_formatted)
{
    /* An outdated manifest fails to open. Refuse all of them before starting
     * any controller child. */
    for (size_t i = 0; i < sizeof projects / sizeof *projects; i++) {
        char manifest[4096];
        int length = snprintf(manifest, sizeof manifest, "%s/%s/project.kdl", tool_root(), projects[i]);
        REQUIRE(length > 0 && (size_t)length < sizeof manifest);
        FILE *file = fopen(manifest, "rb");
        REQUIRE(file);
        int version = 0;
        bool current = fscanf(file, "version %d", &version) == 1 &&
                       version == CHIBI_PROJECT_VERSION;
        REQUIRE(!fclose(file));
        REQUIRE(current);
    }
    snprintf(scratch, sizeof scratch, "/tmp/chibi-fixture-format-XXXXXX");
    REQUIRE(mkdtemp(scratch));
    char log[128];
    snprintf(log, sizeof log, "%s/format.log", scratch);
    bool canonical = true;
    for (size_t i = 0; i < sizeof projects / sizeof *projects; i++) {
        char root[4096];
        int length = snprintf(root, sizeof root, "%s/%s", tool_root(), projects[i]);
        REQUIRE(length > 0 && (size_t)length < sizeof root);
        const char *args[] = { program,    "--project-root", root,
                               "--format", "--check",        NULL };
        int status = tool_run(args, tool_root(), log);
        printf("fixture-format %s status=%d\n", projects[i], status);
        canonical &= status == 0;
    }
    REQUIRE(canonical);
    return true;
}

void test_tool_fixture_format(const char *binary)
{
    program = binary;
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(fixture_projects_are_canonically_formatted);
    pu_clear_setup();
}
