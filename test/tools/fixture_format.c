#include "assets/project.h"
#include "support/asset_document.h"
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
    /* Opening an old project may migrate it even with --check. Refuse all
     * outdated manifests before starting any controller child. */
    for (size_t i = 0; i < sizeof projects / sizeof *projects; i++) {
        char manifest[4096];
        int length = snprintf(manifest, sizeof manifest, "%s/%s/project.chibi", tool_root(), projects[i]);
        REQUIRE(length > 0 && (size_t)length < sizeof manifest);
        char *text = tool_read(manifest);
        REQUIRE(text);
        char error[256];
        av_doc *doc = asset_document_read(text, error, sizeof error);
        free(text);
        REQUIRE(doc);
        bool current = av_get_int(av_obj_get(av_doc_root(doc), "version")) ==
                       CHIBI_PROJECT_VERSION;
        av_doc_free(doc);
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
