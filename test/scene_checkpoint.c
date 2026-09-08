#include "editor/ui/ui_shell.h"
#include "editor/workspace/gameplay_input.h"
#include "editor/workspace/scene_internal.h"
#include "graph/document/serialize.h"
#include "graph/model/registry.h"
#include "runtime/generated.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

/* Runs after Cute and ImGui initialization; no rendering or input injection is
 * required to prove that a native generation can reconstruct authoring state. */
typedef struct
{
    ecs_world *world;
    game_live_world *live;
    engine *runtime;
    editor *graph;
    app_scene_authoring *scene;
} checkpoint_fixture;

static checkpoint_fixture fixture_new()
{
    checkpoint_fixture fixture = { .world = ecs_world_new() };
    for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
        assert(
            ecs_world_register_component(fixture.world, GAME_REGISTRY_COMPONENTS[i], NULL) == ECS_OK
        );
    }
    fixture.live = game_live_world_new(fixture.world);
    game_live_definition *definition = game_live_definition_new(
        "ReloadBehavior",
        behavior_asset_new("ReloadBehavior")
    );
    assert(definition && game_live_add_definition(fixture.live, definition) == 0);
    fixture.graph = editor_new(behavior_asset_new("ReloadBehavior"));
    fixture.runtime = engine_new(fixture.world, fixture.live);
    fixture.scene = app_scene_authoring_new(
        fixture.world,
        fixture.live,
        definition,
        editor_behavior_asset(fixture.graph),
        fixture.graph,
        NULL,
        NULL,
        fixture.runtime
    );
    assert(fixture.scene);
    return fixture;
}

static void fixture_free(checkpoint_fixture *fixture)
{
    app_scene_authoring_free(fixture->scene);
    engine_free(fixture->runtime);
    game_live_world_free(fixture->live);
    editor_free(fixture->graph);
    ecs_world_free(fixture->world);
}

static checkpoint_fixture roundtrip(checkpoint_fixture *source)
{
    char *scene = app_scene_authoring_checkpoint(source->scene);
    char *graph = editor_checkpoint(source->graph);
    assert(scene && graph);
    bool full_catalog = game_live_definition_count(source->live) > 1;
    fixture_free(source);
    checkpoint_fixture candidate = fixture_new();
    if (full_catalog) {
        assert(app_scene_authoring_set_content_root(candidate.scene, "content") == 0);
        assert(app_scene_authoring_load_content(candidate.scene) == 0);
    }
    assert(app_scene_authoring_restore_checkpoint(candidate.scene, scene) == 0);
    assert(editor_restore_checkpoint(candidate.graph, graph));
    app_scene_authoring_set_editor_working(
        candidate.scene,
        editor_behavior_asset(candidate.graph)
    );
    free(scene);
    free(graph);
    return candidate;
}

static void shader_scene_load()
{
    checkpoint_fixture f = fixture_new();
    assert(app_scene_authoring_set_content_root(f.scene, "content") == 0);
    assert(app_scene_authoring_load_content(f.scene) == 0);
    int loaded = app_scene_authoring_load(f.scene, "content/scenes/ShaderDemo.json");
    if (loaded) {
        fprintf(stderr, "shader scene load: %s\n", app_scene_authoring_message(f.scene));
    }
    assert(loaded == 0 && acount(f.scene->actors) == 3);
    game_live_definition *input = game_live_find_definition(f.live, "InputBehavior");
    int applied_nodes = acount(input->applied->graph->nodes);
    int working_nodes = acount(input->working->graph->nodes);
    graph_add_node(input->working->graph, registry_node_by_name("Integer"));
    game_live_mark_dirty(input);
    f = roundtrip(&f);
    input = game_live_find_definition(f.live, "InputBehavior");
    assert(input->dirty && acount(input->working->graph->nodes) == working_nodes + 1);
    assert(acount(input->applied->graph->nodes) == applied_nodes);
    assert(graph_remove_node(input->working->graph, alast(input->working->graph->nodes)) == 0);
    assert(acount(f.scene->actors) == 3);
    assert(app_scene_authoring_runtime_command(f.scene, APP_RUNTIME_SCENE, APP_RUNTIME_PLAY) == 0);
    assert(app_scene_authoring_runtime_command(f.scene, APP_RUNTIME_SCENE, APP_RUNTIME_STOP) == 0);
    assert(app_scene_authoring_open_behavior(f.scene, "InputBehavior") == 0);
    assert(app_scene_authoring_preview_prototype(f.scene, "Player") == 0);
    behavior_instance *movement = game_prototype_preview_instance(f.scene->prototype_preview, "InputBehavior");
    behavior_instance_set_trace_enabled(movement, 1);
    engine_set_focus(f.runtime, ENGINE_FOCUS_VIEWPORT);
    int idle_trace = acount(movement->trace);
    assert(app_scene_authoring_move(f.scene, 0, 0) >= FLOW_OK);
    assert(acount(movement->trace) == idle_trace);
    assert(app_scene_authoring_move(f.scene, 1, 0) >= FLOW_OK);
    int first_trace = acount(movement->trace);
    assert(first_trace > idle_trace);
    assert(app_scene_authoring_move(f.scene, 1, 0) >= FLOW_OK);
    int held_trace = acount(movement->trace);
    assert(held_trace > first_trace);
    assert(app_scene_authoring_move(f.scene, 0, 0) >= FLOW_OK);
    int release_trace = acount(movement->trace);
    assert(release_trace > held_trace);
    assert(app_scene_authoring_move(f.scene, 0, 0) >= FLOW_OK);
    assert(acount(movement->trace) == release_trace);
    assert(app_scene_authoring_move(f.scene, 1, 0) >= FLOW_OK);
    int refocused_trace = acount(movement->trace);
    engine_viewport *unfocused = engine_viewport_new(32, 32);
    app_gameplay_input(f.scene, f.runtime, unfocused);
    int unfocused_trace = acount(movement->trace);
    assert(unfocused_trace > refocused_trace);
    app_gameplay_input(f.scene, f.runtime, unfocused);
    assert(acount(movement->trace) == unfocused_trace);
    engine_viewport_free(unfocused);
    engine_set_focus(f.runtime, ENGINE_FOCUS_VIEWPORT);
    assert(app_scene_authoring_move(f.scene, 0, 0) >= FLOW_OK);
    assert(acount(movement->trace) == unfocused_trace);
    assert(app_scene_authoring_move(f.scene, 1, 0) >= FLOW_OK);
    assert(acount(movement->trace) > unfocused_trace);

    assert(app_scene_authoring_runtime_command(f.scene, APP_RUNTIME_PREVIEW, APP_RUNTIME_PAUSE) == 0);
    behavior_instance *before = game_prototype_preview_instance(f.scene->prototype_preview, "InputBehavior");
    assert(before);
    assert(scene_debug_matches(editor_behavior_asset(f.graph), before->bp));
    char *linked_json = behavior_asset_save_json(before->bp);
    behavior_asset *linked_copy = behavior_asset_load_json(linked_json);
    free(linked_json);
    assert(linked_copy && scene_debug_matches(editor_behavior_asset(f.graph), linked_copy));
    behavior_asset_free(linked_copy);
    int count = acount(before->bp->graph->nodes);
    assert(editor_spawn(f.graph, registry_node_by_name("Has_GameHealth"), 20, 20));
    app_scene_authoring_set_editor_working(f.scene, editor_behavior_asset(f.graph));
    preview_behavior_edits(f.scene);
    behavior_instance *after = game_prototype_preview_instance(f.scene->prototype_preview, "InputBehavior");
    assert(after == before && acount(after->bp->graph->nodes) == count + 1);
    assert(scene_debug_matches(editor_behavior_asset(f.graph), after->bp));
    fixture_free(&f);
}

int test_scene_checkpoint()
{
    shader_scene_load();
    checkpoint_fixture fixture = fixture_new();
    assert(
        app_scene_authoring_register_prototype(fixture.scene, "{\"name\":\"ReloadPrototype\",\"components\":{\"GameTransform\":{\"x\":1,\"y\":2}}}", NULL) ==
        0
    );
    assert(
        app_scene_authoring_set_prototype_field(
            fixture.scene,
            "ReloadPrototype",
            "GameTransform",
            "x",
            (pin_value){ .type = PIN_F32, .f = 7 }
        ) == 0
    );
    assert(app_scene_authoring_add_instance(fixture.scene, "ReloadPrototype") == 0);
    assert(app_scene_authoring_open_behavior(fixture.scene, "ReloadBehavior") == 0);
    assert(editor_spawn(fixture.graph, registry_node_by_name("Integer"), 12, 34));
    fixture.scene->camera = (scene_camera){ .view = { 10, 20, 300, 200 }, .saved = true };
    app_scene_authoring_navigation_sync(fixture.scene);
    fixture = roundtrip(&fixture);
    assert(fixture.scene->scene_dirty && acount(fixture.scene->actors) == 1);
    assert(
        acount(fixture.scene->prototypes) == 1 && fixture.scene->prototypes[0].dirty
    );
    assert(fixture.scene->camera.saved && fixture.scene->camera.view.center_x == 10);
    assert(acount(editor_active_graph(fixture.graph)->nodes) == 1);
    assert(
        GAME_GameTransform_get_const(fixture.world, fixture.scene->actors[0].entity)
            ->x == 7
    );
    editor_undo(fixture.graph);
    assert(acount(editor_active_graph(fixture.graph)->nodes) == 0);
    assert(app_scene_authoring_open_prototype(fixture.scene, "ReloadPrototype") == 0);
    assert(editor_ui_run_command(EDITOR_UI_COMMAND_UNDO) == 0);
    pin_value value;
    assert(
        app_scene_authoring_read_prototype_field(fixture.scene, "ReloadPrototype", "GameTransform", "x", &value) ==
            0 &&
        value.f == 1
    );
    scene_authoring_select_document(fixture.scene, APP_EDITOR_DOCUMENT_SCENE, APP_SCENE_DOCUMENT_ID);
    assert(editor_ui_run_command(EDITOR_UI_COMMAND_UNDO) == 0);
    assert(acount(fixture.scene->actors) == 0);
    assert(editor_ui_run_command(EDITOR_UI_COMMAND_REDO) == 0);
    assert(acount(fixture.scene->actors) == 1);
    float authored_x = GAME_GameTransform_get_const(
                           fixture.world,
                           fixture.scene->actors[0].entity
    )
                           ->x;
    assert(
        app_scene_authoring_runtime_command(fixture.scene, APP_RUNTIME_SCENE, APP_RUNTIME_PLAY) == 0
    );
    GAME_GameTransform_get(fixture.world, fixture.scene->actors[0].entity)->x = 999;
    assert(
        app_scene_authoring_runtime_command(fixture.scene, APP_RUNTIME_SCENE, APP_RUNTIME_PAUSE) == 0
    );
    fixture = roundtrip(&fixture);
    app_runtime_state state = app_scene_authoring_runtime_state(fixture.scene);
    assert(state.target == APP_RUNTIME_SCENE && !state.playing && fixture.scene->runtime_snapshot_valid);
    assert(
        GAME_GameTransform_get_const(fixture.world, fixture.scene->actors[0].entity)
            ->x == authored_x
    );
    assert(
        app_scene_authoring_runtime_command(fixture.scene, APP_RUNTIME_SCENE, APP_RUNTIME_STOP) == 0
    );
    assert(
        GAME_GameTransform_get_const(fixture.world, fixture.scene->actors[0].entity)
            ->x == authored_x
    );
    fixture_free(&fixture);
    puts("scene checkpoint PASS: unsaved graph/prototype/scene, Undo/Redo, camera, paused restart and authored pre-Play pose");
    return 0;
}
