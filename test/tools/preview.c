#include "assets/project.h"
#include "authoring/prototype_preview.h"
#include "automation/controller.h"
#include "runtime/declared_components.h"
#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/catalog.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char root[128];
static chibi_preview_run *run;

static void cleanup()
{
    chibi_preview_free(run);
    run = NULL;
    if (root[0]) {
        tool_remove_tree(root);
        root[0] = 0;
    }
}

static void write_file(const char *name, const char *text)
{
    char path[256];
    snprintf(path, sizeof path, "%s/%s", root, name);
    tool_write(path, text);
}

TEST_CASE(preview_loads_declared_components_before_sources_and_keeps_them_alive)
{
    snprintf(root, sizeof root, "/tmp/chibi-preview-components-XXXXXX");
    REQUIRE(mkdtemp(root));
    const char *folders[] = { "content",
                              "content/components",
                              "content/behaviors",
                              "content/prototypes",
                              "content/scenes" };
    for (int i = 0; i < 5; i++) {
        char path[256];
        snprintf(path, sizeof path, "%s/%s", root, folders[i]);
        REQUIRE(!mkdir(path, 0700));
    }
    char manifest[256];
    snprintf(manifest, sizeof manifest, "version %d\nname \"Preview\"\ncontent \"content\"\nstart_scene \"Test\"\n", CHIBI_PROJECT_VERSION);
    write_file("project.kdl", manifest);
    write_file("content/components/PreviewCounter.chibi", "component PreviewCounter { value:int }\n");
    write_file(
        "content/behaviors/Count.chibi",
        "behavior Count { on Begin { self.PreviewCounter.value += 2; } "
        "on Tick { self.PreviewCounter.value += 1; } }\n"
    );
    write_file(
        "content/prototypes/Counter.kdl",
        "name \"Counter\"\ncomponents { PreviewCounter value=7; }\n"
        "(array)behaviors \"Count\"\n"
    );
    write_file("content/scenes/Test.kdl", "version 1\nname \"Test\"\n(array)entities { - name=\"Counter\" prototype=\"Counter\"; }\n");
    REQUIRE(!type_catalog_find("PreviewCounter"));
    char error[512] = { 0 };
    const char *requests[] = { "{\"kind\":\"scene\",\"name\":\"Test\","
                               "\"frames\":0,\"render_frames\":0}",
                               "{\"kind\":\"prototype\",\"name\":\"Counter\","
                               "\"frames\":0,\"render_frames\":0}" };
    for (int i = 0; i < 2; i++) {
        int status = chibi_automation_preview(root, requests[i], &run, error, sizeof error);
        if (status != CHIBI_AUTOMATION_OK) {
            fprintf(stderr, "declared preview preparation: %s\n", error);
        }
        REQUIRE(status == CHIBI_AUTOMATION_OK && run);
        REQUIRE(type_catalog_find("PreviewCounter"));
        prototype_preview *preview = chibi_preview_runtime(run);
        ecs_world *world = prototype_preview_world(preview);
        ecs_entity entity = prototype_preview_entity(preview);
        ecs_component_catalog components = project_components();
        const ecs_component_desc *counter = NULL;
        for (int j = 0; j < components.component_count; j++) {
            if (!strcmp(components.components[j]->name, "PreviewCounter")) {
                counter = components.components[j];
            }
        }
        REQUIRE(counter);
        const int *value = ecs_component_data_const(world, entity, counter->id);
        REQUIRE(value && *value == 9);
        REQUIRE(chibi_preview_step(run, error, sizeof error) == CHIBI_AUTOMATION_OK);
        value = ecs_component_data_const(world, entity, counter->id);
        REQUIRE(value && *value == 10);
        chibi_preview_free(run);
        run = NULL;
        REQUIRE(!type_catalog_find("PreviewCounter"));
    }
    return true;
}

void test_preview_preparation()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(preview_loads_declared_components_before_sources_and_keeps_them_alive);
    pu_clear_setup();
}
