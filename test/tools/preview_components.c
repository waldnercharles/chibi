#include "assets/project.h"
#include "authoring/prototype_preview.h"
#include "automation/controller.h"
#include "runtime/declared_components.h"
#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/catalog.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char directory[] = "/tmp/chibi-preview-components-XXXXXX";
static bool owns_directory;
static declared_components *previous;
static chibi_preview_run *run;
static char error[512];

static void cleanup()
{
    if (pu_test_failed() && error[0]) {
        fprintf(stderr, "%s\n", error);
    }
    chibi_preview_free(run);
    run = NULL;
    declared_components_free(previous);
    previous = NULL;
    if (owns_directory) {
        tool_remove_tree(directory);
    }
}

static void write_asset(const char *name, const char *text)
{
    char path[256];
    int size = snprintf(path, sizeof path, "%s/%s", directory, name);
    assert(size > 0 && size < (int)sizeof path);
    tool_write(path, text);
}

TEST_CASE(preview_owns_project_components_until_its_world_is_freed)
{
    REQUIRE(mkdtemp(directory));
    owns_directory = true;
    const char *folders[] = { "content",
                              "content/components",
                              "content/prototypes",
                              "content/scenes" };
    for (int i = 0; i < 4; i++) {
        char path[256];
        snprintf(path, sizeof path, "%s/%s", directory, folders[i]);
        REQUIRE(mkdir(path, 0700) == 0);
    }
    char manifest[128];
    snprintf(manifest, sizeof manifest, "version %d\nname \"Preview components\"\ncontent \"content\"\n", CHIBI_PROJECT_VERSION);
    write_asset("project.kdl", manifest);
    write_asset(
        "content/components/PreviewHeat.chibi",
        "PreviewHeat :: component {\n"
        "    level: number\n"
        "}\n"
    );
    write_asset("content/prototypes/Probe.chibi", "Probe :: prototype { PreviewHeat { level: 7 } }\n");
    write_asset("content/scenes/Probe.kdl", "version 1\nname \"Probe\"\n(array)entities {\n    - name=\"Probe\" prototype=\"Probe\"\n}\n");
    /* Explicit preview targets do not need a default launch target. Full
     * project/player opening still requires one. */
    chibi_project launch = { 0 };
    REQUIRE(chibi_project_open(&launch, directory, error, sizeof error) != 0);
    REQUIRE(strstr(error, "no start scene"));
    chibi_project_free(&launch);

    const declared_field field = { .name = "value", .type = "number" };
    const declared_component component = { .name = "PreviousPreview",
                                           .fields = &field,
                                           .field_count = 1 };
    previous = declared_components_new(&component, 1, error, sizeof error);
    REQUIRE(previous);
    const type_desc *previous_type = type_catalog_find("PreviousPreview");
    REQUIRE(previous_type);

    const char *kinds[] = { "scene", "prototype" };
    for (int i = 0; i < 2; i++) {
        char request[128];
        snprintf(request, sizeof request, "{\"kind\":\"%s\",\"name\":\"Probe\",\"frames\":1}", kinds[i]);
        REQUIRE(
            chibi_automation_preview(directory, request, &run, error, sizeof error) == CHIBI_AUTOMATION_OK
        );
        REQUIRE(run);
        const type_desc *heat_type = type_catalog_find("PreviewHeat");
        REQUIRE(heat_type);
        REQUIRE(!type_catalog_find("PreviousPreview"));
        const ecs_component_desc *heat = ecs_component_catalog_find(project_components(), "PreviewHeat");
        REQUIRE(heat && heat->type == heat_type);
        prototype_preview *preview = chibi_preview_runtime(run);
        REQUIRE(prototype_preview_entity_count(preview) == 1);
        ecs_world *world = prototype_preview_world(preview);
        ecs_entity entity = prototype_preview_entity_at(preview, 0);
        float *level = ecs_component_data(world, entity, heat->id);
        REQUIRE(level && *level == 7);
        *level = 9;
        REQUIRE(chibi_preview_step(run, error, sizeof error) == CHIBI_AUTOMATION_OK);
        level = ecs_component_data(world, entity, heat->id);
        REQUIRE(level && *level == 9);
        REQUIRE(type_catalog_find("PreviewHeat") == heat_type);
        chibi_preview_free(run);
        run = NULL;
        REQUIRE(!type_catalog_find("PreviewHeat"));
        REQUIRE(type_catalog_find("PreviousPreview") == previous_type);
    }

    REQUIRE(
        chibi_automation_preview(
            directory,
            "{\"kind\":\"scene\",\"name\":\"Missing\",\"frames\":0}",
            &run,
            error,
            sizeof error
        ) == CHIBI_AUTOMATION_NOT_FOUND
    );
    REQUIRE(!run && !type_catalog_find("PreviewHeat"));
    REQUIRE(type_catalog_find("PreviousPreview") == previous_type);

    write_asset(
        "content/components/PreviewHeat.chibi",
        "PreviewHeat :: component {\n"
        "    level: MissingPreviewType\n"
        "}\n"
    );
    REQUIRE(
        chibi_automation_preview(
            directory,
            "{\"kind\":\"scene\",\"name\":\"Probe\",\"frames\":0}",
            &run,
            error,
            sizeof error
        ) == CHIBI_AUTOMATION_VALIDATION_ERROR
    );
    REQUIRE(!run && !type_catalog_find("PreviewHeat"));
    REQUIRE(type_catalog_find("PreviousPreview") == previous_type);
    return true;
}

void test_preview_components()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(preview_owns_project_components_until_its_world_is_freed);
    pu_clear_setup();
}
