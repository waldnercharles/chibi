/* The graph's debug target while the game runs (Unreal's debug object): it
 * follows the selection to that object's instance of the open behavior, a
 * spawned object's included. Only that instance records activity, and none
 * does while no graph shows it or No target is chosen, which lasts until the
 * selection changes. */
#include "editor/workspace/scene_internal.h"
#include "platform/cute.h"
#include "render/shader_catalog.h"
#include "runtime/generated.h"
#include "test/suite_internal.h"

#include <assert.h>
#include <stdio.h>

typedef struct
{
    ecs_world *world;
    game_live_world *live;
    engine *runtime;
    editor *graph;
    app_scene_authoring *scene;
    double time;
    bool owns_file_system; /* content's image materials read through it */
} debug_fixture;

static debug_fixture fixture_new()
{
    debug_fixture f = { .world = ecs_world_new(),
                        .owns_file_system = !cf_fs_get_base_directory() };
    if (f.owns_file_system) {
        assert(!cf_is_error(cf_fs_init(NULL)));
        render_shader_catalog_clear();
    }
    for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
        assert(ecs_world_register_component(f.world, GAME_REGISTRY_COMPONENTS[i], NULL) == ECS_OK);
    }
    f.live = game_live_world_new(f.world);
    game_live_definition *definition = game_live_definition_new(
        "DebugTarget",
        behavior_asset_new("DebugTarget")
    );
    assert(game_live_add_definition(f.live, definition) == 0);
    f.graph = editor_new(behavior_asset_new("DebugTarget"));
    f.runtime = engine_new(f.world, f.live);
    f.scene = app_scene_authoring_new(
        f.world,
        f.live,
        definition,
        editor_behavior_asset(f.graph),
        f.graph,
        NULL,
        NULL,
        f.runtime
    );
    assert(app_scene_authoring_set_content_root(f.scene, "content") == 0);
    assert(app_scene_authoring_load_content(f.scene) == 0);
    assert(app_scene_authoring_load(f.scene, "content/scenes/Arena3D.kdl") == 0);
    assert(app_scene_authoring_runtime_command(f.scene, APP_RUNTIME_SCENE, APP_RUNTIME_PLAY) == 0);
    engine_set_focus(f.runtime, ENGINE_FOCUS_VIEWPORT);
    return f;
}

static void fixture_free(debug_fixture *f)
{
    app_scene_authoring_free(f->scene);
    engine_free(f->runtime);
    game_live_world_free(f->live);
    editor_free(f->graph);
    ecs_world_free(f->world);
    if (f->owns_file_system) {
        render_shader_catalog_clear();
        cf_fs_destroy();
    }
}

static void steps(debug_fixture *f, int count)
{
    for (int i = 0; i < count; i++) {
        f->time += ENGINE_FIXED_SECONDS;
        assert(app_scene_authoring_frame(f->scene, f->time) >= 0);
    }
}

static bool same_entity(ecs_entity a, ecs_entity b)
{
    return ecs_entity_to_u64(a) == ecs_entity_to_u64(b);
}

static ecs_entity first_with(ecs_world *world, ecs_component_id component)
{
    ecs_query query;
    assert(ecs_query_begin(world, &query, &component, 1) == ECS_OK);
    ecs_entity entity = { 0 };
    ecs_query_next(&query, &entity);
    ecs_query_end(&query);
    return entity;
}

static int traced(game_behavior_world *behaviors)
{
    int count = 0;
    for (int i = 0; i < game_behavior_attachment_count(behaviors); i++) {
        count += game_behavior_attachment_info_index(behaviors, i).instance->trace_enabled !=
                 0;
    }
    return count;
}

static bool recorded(const behavior_instance *instance)
{
    int capacity = 0;
    uint64_t generation;
    const behavior_instance_activity
        *table = behavior_instance_activity_table(instance, &capacity, &generation);
    for (int i = 0; i < capacity; i++) {
        if (table[i].count) {
            return true;
        }
    }
    return false;
}

void test_debug_target()
{
    debug_fixture f = fixture_new();
    game_behavior_world *behaviors = f.live->behaviors;
    ecs_entity player = first_with(f.world, GAME_COMPONENT_GameInput.id);
    for (int i = 0; i < acount(f.scene->actors); i++) {
        if (same_entity(f.scene->actors[i].entity, player)) {
            f.scene->selected = i;
        }
    }
    assert(app_scene_authoring_open_behavior(f.scene, "TwinStickInputBehavior") == 0);
    sync_editor_inspection(f.scene);
    behavior_instance *input = game_behavior_instance(behaviors, player, "TwinStickInputBehavior");
    assert(f.scene->debug_status == APP_DEBUG_AVAILABLE);
    assert(same_entity(f.scene->debug.entity, player));
    assert(input->trace_enabled && traced(behaviors) == 1);
    steps(&f, 2);
    assert(recorded(input));
    for (int i = 0; i < game_behavior_attachment_count(behaviors); i++) {
        behavior_instance *other = game_behavior_attachment_info_index(behaviors, i)
                                       .instance;
        assert(other == input || !recorded(other));
    }

    /* A shot the game spawned is a target too. */
    assert(app_scene_authoring_aim(f.scene, (CF_V2){ 1, 0 }, true) == FLOW_OK);
    steps(&f, 2);
    ecs_entity shot = first_with(f.world, GAME_COMPONENT_GameProjectile.id);
    game_behavior_attachment_info info;
    assert(shot.slot && game_behavior_attachment_info_at(behaviors, shot, 0, &info) == 0);
    char name[128];
    snprintf(name, sizeof name, "%s", info.asset_name);
    assert(app_scene_authoring_open_behavior(f.scene, name) == 0);
    assert(app_scene_authoring_select_runtime_entity(f.scene, shot) == 0);
    sync_editor_inspection(f.scene);
    assert(f.scene->debug_status == APP_DEBUG_AVAILABLE);
    assert(same_entity(f.scene->debug.entity, shot));
    assert(!input->trace_enabled && traced(behaviors) == 1);

    /* Nothing traces while no graph shows the target. */
    app_scene_authoring_set_debug_hidden(f.scene, true);
    sync_editor_inspection(f.scene);
    assert(traced(behaviors) == 0);
    app_scene_authoring_set_debug_hidden(f.scene, false);
    sync_editor_inspection(f.scene);
    assert(traced(behaviors) == 1);

    /* No target stays until the selection changes. */
    assert(app_scene_authoring_choose_debug_target(f.scene, (ecs_entity){ 0 }) == 0);
    steps(&f, 1);
    sync_editor_inspection(f.scene);
    assert(f.scene->debug_status == APP_DEBUG_NONE && traced(behaviors) == 0);
    assert(app_scene_authoring_open_behavior(f.scene, "TwinStickInputBehavior") == 0);
    f.scene->runtime_selected = (ecs_entity){ 0 };
    sync_editor_inspection(f.scene);
    assert(same_entity(f.scene->debug.entity, player) && input->trace_enabled);
    fixture_free(&f);
    puts("debug target PASS: follows the selection, spawned objects included; only the target traces; hidden graphs and No target trace nothing");
}
