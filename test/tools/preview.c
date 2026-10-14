#include "assets/project.h"
#include "authoring/prototype_preview.h"
#include "authoring/shader_prepare.h"
#include "automation/controller.h"
#include "graph/authoring/asset_edit.h"
#include "graph/authoring/graph_edit.h"
#include "graph/document/library.h"
#include "graph/model/registry.h"
#include "graph/nodes/core_nodes.h"
#include "graph/shader/shader_graph.h"
#include "render/shader_catalog_records.h"
#include "runtime/declared_components.h"
#include "support/array.h"
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
static preview_world *live;
static preview_definition *pending;
static behavior_asset *library;
static const registry_pack **previous_packs;
static bool registry_changed;

static void cleanup()
{
    chibi_preview_free(run);
    run = NULL;
    preview_definition_free(pending);
    pending = NULL;
    preview_world_free(live);
    live = NULL;
    behavior_asset_free(library);
    library = NULL;
    if (registry_changed) {
        authoring_shader_catalog_clear();
        registry_set_packs(previous_packs, acount(previous_packs));
        registry_changed = false;
    }
    afree(previous_packs);
    if (root[0]) {
        tool_remove_tree(root);
        root[0] = 0;
    }
}

static bool particle_preview_fixture()
{
    for (int i = 0; i < registry_pack_count(); i++) {
        apush(previous_packs, registry_pack_at(i));
    }
    const registry_pack *packs[] = { &CORE_FLOW_PACK, &SHADER_PACK };
    registry_set_packs(packs, 2);
    registry_changed = true;
    live = preview_world_new(NULL);
    REQUIRE(live);
    library = behavior_asset_new("ParticlePosition");
    library->domain = BEHAVIOR_DOMAIN_SHADER_LIBRARY;
    graph *helper = behavior_asset_add_function(library, "Origin", true);
    REQUIRE(helper);
    node *result = graph_node_by_id(helper, helper->return_id);
    pin_spec position = { .name = "Value",
                          .kind = PIN_DATA,
                          .type = PIN_STRUCT,
                          .type_name = "ShaderVec3" };
    REQUIRE(graph_add_pin(helper, result, false, &position) == 0);
    float origin[3] = { 4, 5, 6 };
    REQUIRE(!graph_set_pin_default(
        helper,
        result,
        false,
        0,
        &(pin_value){ .type = PIN_STRUCT, .type_name = "ShaderVec3", .ptr = origin }
    ));
    pending = preview_definition_new(library->name, library);
    REQUIRE(pending);
    library = NULL;
    REQUIRE(!preview_add_definition(live, pending));
    pending = NULL;

    library = behavior_asset_new("PreviewParticles");
    library->domain = BEHAVIOR_DOMAIN_SHADER_LIBRARY;
    graph *g = behavior_asset_add_function(library, "Cloud", true);
    REQUIRE(g);
    node *entry = graph_node_by_id(g, g->entry_id);
    node *ret = graph_node_by_id(g, g->return_id);
    const char *names[] = { "Index", "Count", "Seed", "Radius" };
    for (int i = 0; i < 4; i++) {
        REQUIRE(
            graph_add_pin(
                g,
                entry,
                true,
                &(pin_spec){ .name = names[i], .kind = PIN_DATA, .type = PIN_F32, .type_name = "float" }
            ) == i
        );
    }
    REQUIRE(
        !graph_set_pin_default(g, entry, true, 3, &(pin_value){ .type = PIN_F32, .f = 0.75f })
    );
    const pin_spec outputs[] = {
        { .name = "Position", .kind = PIN_DATA, .type = PIN_STRUCT, .type_name = "ShaderVec3" },
        { .name = "Size", .kind = PIN_DATA, .type = PIN_F32, .type_name = "float" },
        { .name = "Color", .kind = PIN_DATA, .type = PIN_STRUCT, .type_name = "ShaderVec4" },
    };
    for (int i = 0; i < 3; i++) {
        REQUIRE(graph_add_pin(g, ret, false, &outputs[i]) == i);
    }
    float tint[4] = { 1, 0.5f, 0.25f, 1 };
    REQUIRE(!graph_set_pin_default(
        g,
        ret,
        false,
        2,
        &(pin_value){ .type = PIN_STRUCT, .type_name = "ShaderVec4", .ptr = tint }
    ));
    node *call = graph_add_node(g, &NODE_CallPureFunction);
    REQUIRE(!graph_library_refresh_call(g, call, helper));
    REQUIRE(!graph_set_node_reference_asset(g, call, "ParticlePosition"));
    REQUIRE(!graph_connect_data(g, call, 0, ret, 0));
    REQUIRE(!graph_connect_data(g, entry, 3, ret, 1));
    pending = preview_definition_new(library->name, library);
    REQUIRE(pending);
    library = NULL;
    REQUIRE(!preview_add_definition(live, pending));
    pending = NULL;
    return true;
}

static bool particle_preview_result(float x, float size)
{
    int id = render_shader_find("PreviewParticles.Cloud");
    REQUIRE(id && render_shader_is_particle(id));
    float parameters[32][4], output[SHADER_OUTPUT_MAX][4];
    render_shader_parameters(id, NULL, (ecs_entity){ 0 }, NULL, parameters);
    shader_error error = { 0 };
    REQUIRE(shader_graph_eval_outputs(
        &shader_catalog_assets[id - 1].graph,
        &(shader_sample_context){ 0 },
        parameters,
        output,
        &error
    ));
    REQUIRE(output[0][0] == x && output[1][0] == size);
    return true;
}

TEST_CASE(preview_publishes_and_removes_particle_functions)
{
    REQUIRE(particle_preview_fixture());
    REQUIRE(particle_preview_result(4, 0.75f));
    preview_definition *definition = preview_find_definition(live, "PreviewParticles");
    graph *g = definition->working->graphs[1];
    REQUIRE(!graph_set_pin_default(
        g,
        graph_node_by_id(g, g->entry_id),
        true,
        3,
        &(pin_value){ .type = PIN_F32, .f = 2 }
    ));
    REQUIRE(!preview_mark_dirty(definition));
    unsigned revision = render_shader_revision();
    char message[256];
    REQUIRE(!preview_can_update_library(live, definition, message, sizeof message));
    REQUIRE(render_shader_revision() == revision);
    REQUIRE(particle_preview_result(4, 0.75f));
    REQUIRE(!preview_apply(live, definition, message, sizeof message));
    REQUIRE(render_shader_revision() != revision);
    REQUIRE(particle_preview_result(4, 2));
    REQUIRE(!preview_rename_definition(live, "PreviewParticles", "RenamedParticles"));
    REQUIRE(!render_shader_find("PreviewParticles.Cloud"));
    int renamed = render_shader_find("RenamedParticles.Cloud");
    REQUIRE(renamed && render_shader_is_particle(renamed));
    REQUIRE(!preview_remove_definition(live, "RenamedParticles"));
    REQUIRE(!render_shader_find("RenamedParticles.Cloud"));
    return true;
}

TEST_CASE(preview_updates_particle_helpers_and_rejects_invalid_programs)
{
    REQUIRE(particle_preview_fixture());
    REQUIRE(particle_preview_result(4, 0.75f));
    preview_definition *helper = preview_find_definition(live, "ParticlePosition");
    graph *g = helper->working->graphs[1];
    node *ret = graph_node_by_id(g, g->return_id);
    float origin[3] = { 9, 8, 7 };
    REQUIRE(!graph_set_pin_default(
        g,
        ret,
        false,
        0,
        &(pin_value){ .type = PIN_STRUCT, .type_name = "ShaderVec3", .ptr = origin }
    ));
    REQUIRE(!preview_mark_dirty(helper));
    char message[256];
    REQUIRE(!preview_apply(live, helper, message, sizeof message));
    REQUIRE(particle_preview_result(9, 0.75f));
    unsigned revision = render_shader_revision();
    node *world = graph_add_node(g, registry_node_by_name("Shader WorldPosition"));
    REQUIRE(world);
    REQUIRE(!graph_connect_data(g, world, 0, ret, 0));
    REQUIRE(!preview_mark_dirty(helper));
    REQUIRE(preview_can_update_library(live, helper, message, sizeof message) < 0);
    REQUIRE(strstr(message, "WorldPosition"));
    REQUIRE(preview_apply(live, helper, message, sizeof message) < 0);
    REQUIRE(render_shader_revision() == revision);
    REQUIRE(particle_preview_result(9, 0.75f));
    return true;
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

void test_preview_shader_libraries()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(preview_publishes_and_removes_particle_functions);
    RUN_TEST_CASE(preview_updates_particle_helpers_and_rejects_invalid_programs);
    pu_clear_setup();
}
