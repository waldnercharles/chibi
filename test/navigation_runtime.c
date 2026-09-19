#include "editor/ui/ui_shell.h"
#include "editor/workspace/editor.h"
#include "editor/workspace/scene_authoring.h"
#include "graph/model/registry.h"
#include "runtime/generated.h"
#include "support/array.h"

#include <assert.h>
#include <cute_defines.h>
#define CF_MATH_H
#include <imgui/dcimgui.h>
#undef CF_MATH_H
#include <stdio.h>
#include <string.h>

static void first_attachment_redo_after_prototype_gain(app_scene_authoring *scene, ecs_world *world)
{
    assert(
        app_scene_authoring_register_prototype(
            scene,
            "{\"version\":1,\"name\":\"FirstAttachment\",\"components\":{"
            "\"GameTransform\":{\"x\":0,\"y\":0}},\"behaviors\":[]}",
            NULL
        ) == 0
    );
    assert(app_scene_authoring_add_instance(scene, "FirstAttachment") == 0);
    ecs_entity entity = app_scene_authoring_runtime_state(scene).selected_entity;
    assert(!GAME_GameHealth_has(world, entity));
    assert(app_scene_authoring_set_instance_component(scene, "GameHealth", true) == 0);
    const GameHealth *health = GAME_GameHealth_get_const(world, entity);
    assert(health);
    GameHealth original = *health;
    assert(editor_ui_run_command(EDITOR_UI_COMMAND_UNDO) == 0);
    assert(!GAME_GameHealth_has(world, entity));
    assert(app_scene_authoring_add_prototype_component(scene, "FirstAttachment", "GameHealth") == 0);
    assert(
        app_scene_authoring_set_prototype_field(
            scene,
            "FirstAttachment",
            "GameHealth",
            "current",
            (pin_value){ .type = PIN_I32, .i = 100 }
        ) == 0
    );
    assert(
        app_scene_authoring_set_prototype_field(
            scene,
            "FirstAttachment",
            "GameHealth",
            "maximum",
            (pin_value){ .type = PIN_I32, .i = 200 }
        ) == 0
    );
    health = GAME_GameHealth_get_const(world, entity);
    assert(health && health->current == 100 && health->maximum == 200);
    assert(editor_ui_run_command(EDITOR_UI_COMMAND_REDO) == 0);
    health = GAME_GameHealth_get_const(world, entity);
    assert(
        health && health->current == original.current &&
        health->maximum == original.maximum
    );
    assert(editor_ui_run_command(EDITOR_UI_COMMAND_UNDO) == 0);
    health = GAME_GameHealth_get_const(world, entity);
    assert(health && health->current == 100 && health->maximum == 200);
    assert(editor_ui_run_command(EDITOR_UI_COMMAND_REDO) == 0);
    health = GAME_GameHealth_get_const(world, entity);
    assert(
        health && health->current == original.current &&
        health->maximum == original.maximum
    );
    assert(app_scene_authoring_set_instance_component(scene, "GameHealth", false) == 0);
    assert(
        app_scene_authoring_set_prototype_field(
            scene,
            "FirstAttachment",
            "GameHealth",
            "maximum",
            (pin_value){ .type = PIN_I32, .i = 300 }
        ) == 0
    );
    assert(!GAME_GameHealth_has(world, entity));
    assert(app_scene_authoring_set_instance_component(scene, "GameHealth", true) == 0);
    health = GAME_GameHealth_get_const(world, entity);
    assert(
        health && health->current == original.current &&
        health->maximum == original.maximum
    );
    assert(editor_ui_run_command(EDITOR_UI_COMMAND_UNDO) == 0);
    assert(!GAME_GameHealth_has(world, entity));
    assert(editor_ui_run_command(EDITOR_UI_COMMAND_REDO) == 0);
    health = GAME_GameHealth_get_const(world, entity);
    assert(
        health && health->current == original.current &&
        health->maximum == original.maximum
    );
    puts("instance retention PASS: Undo first attachment inherits later prototype additions; Redo restores authored values and later manual detachment remains distinct");
}

static ecs_entity spawn_registered_child(game_live_world *live, ecs_entity owner)
{
    assert(
        game_behavior_dispatch(live->behaviors, owner, "SpawnRegistered", (flow_inputs){ 0 }, NULL) == 1
    );
    assert(game_entities_flush_initializers(live->entities, 0) == 0);
    ecs_entity *spawned = NULL;
    game_entities_copy_live(live->entities, &spawned);
    assert(acount(spawned) > 0);
    ecs_entity entity = spawned[acount(spawned) - 1];
    afree(spawned);
    return entity;
}

static void prototype_spawn_refresh(app_scene_authoring *scene, game_live_world *live, ecs_world *world)
{
    behavior_asset *asset = behavior_asset_new("PrototypeRefreshSpawner");
    assert(behavior_asset_set_owner_type(asset, "ecs.entity") == 0);
    graph *g = asset->graph;
    node *spawn = graph_add_node(g, registry_node_by_name("Spawn Prototype"));
    assert(
        spawn &&
        graph_set_pin_default(g, spawn, 0, 1, &(pin_value){ .type = PIN_NAME, .str = "RuntimeRefreshChild" }) ==
            0
    );
    const char *events[] = { "Begin", "SpawnRegistered" };
    for (int i = 0; i < 2; i++) {
        node *event = graph_add_node(g, registry_node_by_name("Event"));
        assert(event && graph_set_event_name(g, event, events[i]) == 0);
        assert(graph_connect_exec(g, event, 0, spawn, 0, 0) == 0);
    }
    assert(
        game_live_add_definition(live, game_live_definition_new("PrototypeRefreshSpawner", asset)) == 0
    );
    behavior_asset *child_behavior = behavior_asset_new("RefreshedChild");
    assert(behavior_asset_set_owner_type(child_behavior, "ecs.entity") == 0);
    assert(
        game_live_add_definition(live, game_live_definition_new("RefreshedChild", child_behavior)) == 0
    );
    assert(
        app_scene_authoring_register_prototype(scene, "{\"name\":\"RuntimeRefreshChild\",\"components\":{\"GameHealth\":{\"current\":73,\"maximum\":100}}}", NULL) ==
        0
    );
    assert(
        app_scene_authoring_register_prototype(scene, "{\"name\":\"RuntimeRefreshSpawner\",\"components\":{},\"behaviors\":[\"PrototypeRefreshSpawner\"]}", NULL) ==
        0
    );
    ecs_entity owner = ecs_world_create_entity(world);
    assert(game_behavior_attach(live->behaviors, owner, "PrototypeRefreshSpawner") == 0);
    ecs_entity original = spawn_registered_child(live, owner);
    assert(GAME_GameHealth_get_const(world, original)->current == 73);
    for (int pass = 0; pass < 6; pass++) {
        int expected = pass == 0 || pass == 2 ? 73 : 19;
        if (pass == 1) {
            assert(
                app_scene_authoring_set_prototype_field(
                    scene,
                    "RuntimeRefreshChild",
                    "GameHealth",
                    "current",
                    (pin_value){ .type = PIN_I32, .i = expected }
                ) == 0
            );
        }
        if (pass == 2) {
            assert(app_scene_authoring_open_prototype(scene, "RuntimeRefreshChild") == 0);
            assert(editor_ui_run_command(EDITOR_UI_COMMAND_UNDO) == 0);
        }
        if (pass == 3) {
            assert(app_scene_authoring_open_prototype(scene, "RuntimeRefreshChild") == 0);
            assert(editor_ui_run_command(EDITOR_UI_COMMAND_REDO) == 0);
        }
        if (pass == 4) {
            assert(
                app_scene_authoring_attach_prototype_behavior(scene, "RuntimeRefreshChild", "RefreshedChild") ==
                0
            );
        }
        if (pass == 5) {
            assert(
                app_scene_authoring_detach_prototype_behavior(scene, "RuntimeRefreshChild", "RefreshedChild") ==
                0
            );
        }
        /* Spawn before any editor frame: mutations update the runtime snapshot immediately. */
        ecs_entity child = spawn_registered_child(live, owner);
        assert(GAME_GameHealth_get_const(world, child)->current == expected);
        assert(
            (game_behavior_instance(live->behaviors, child, "RefreshedChild") !=
             NULL) == (pass == 4)
        );
        assert(GAME_GameHealth_get_const(world, original)->current == 73);
        assert(app_scene_authoring_preview_prototype(scene, "RuntimeRefreshSpawner") == 0);
        assert(app_scene_authoring_frame(scene, (double)pass) == FLOW_OK);
        const ecs_world *preview = app_scene_authoring_preview_world(scene);
        ecs_component_id health = GAME_COMPONENT_GameHealth.id;
        ecs_query query;
        assert(ecs_query_begin((ecs_world *)preview, &query, &health, 1) == ECS_OK);
        assert(ecs_query_next(&query, &child));
        assert(GAME_GameHealth_get_const(preview, child)->current == expected);
        assert(!ecs_query_next(&query, &child));
        ecs_query_end(&query);
        app_scene_authoring_stop_prototype_preview(scene);
    }
    assert(game_behavior_destroy_entity(live->behaviors, owner) == 0);
    game_entities_reset(live->entities);
    puts("prototype spawn refresh PASS: initial registration, edits, Undo/Redo and behavior attachment changes reach the next scene/preview spawn without changing earlier instances");
}

static void duplicate_instance_preserves_edits(app_scene_authoring *scene, ecs_world *world)
{
    assert(app_scene_authoring_add_instance(scene, "First") == 0);
    ecs_entity original = app_scene_authoring_runtime_state(scene).selected_entity;
    GAME_GameTransform_get(world, original)->x = 123;
    GAME_GameHealth_get(world, original)->current = 42;
    assert(app_scene_authoring_duplicate_instance(scene) == 0);
    ecs_entity copy = app_scene_authoring_runtime_state(scene).selected_entity;
    assert(ecs_entity_to_u64(copy) != ecs_entity_to_u64(original));
    assert(GAME_GameTransform_get_const(world, copy)->x == 123);
    assert(GAME_GameHealth_get_const(world, copy)->current == 42);
    assert(editor_ui_run_command(EDITOR_UI_COMMAND_UNDO) == 0);
    assert(!ecs_entity_is_alive(world, copy));
    assert(ecs_entity_is_alive(world, original));
    assert(editor_ui_run_command(EDITOR_UI_COMMAND_REDO) == 0);
    copy = app_scene_authoring_runtime_state(scene).selected_entity;
    assert(GAME_GameHealth_get_const(world, copy)->current == 42);
    GAME_GameHealth_get(world, copy)->current = 10;
    assert(GAME_GameHealth_get_const(world, original)->current == 42);
}

static void prototype_vectors_and_text(app_scene_authoring *scene, ecs_world *world)
{
    const char
        *json = "{\"name\":\"GuideTest\",\"components\":{\"GameTransform\":{},"
                "\"GameWidget\":{}},\"behaviors\":[]}";
    assert(app_scene_authoring_register_prototype(scene, json, NULL) == 0);
    assert(app_scene_authoring_add_instance(scene, "GuideTest") == 0);
    ecs_entity entity = app_scene_authoring_runtime_state(scene).selected_entity;
    CF_V2 anchor = { 0.5f, 1.0f };
    assert(
        app_scene_authoring_set_prototype_field(
            scene,
            "GuideTest",
            "GameWidget",
            "anchor",
            (pin_value){ .type = PIN_STRUCT, .type_name = "CF_V2", .ptr = &anchor }
        ) == 0
    );
    assert(GAME_GameWidget_get_const(world, entity)->anchor.x == 0.5f);
    assert(app_scene_authoring_open_prototype(scene, "GuideTest") == 0);
    assert(editor_ui_run_command(EDITOR_UI_COMMAND_UNDO) == 0);
    assert(GAME_GameWidget_get_const(world, entity)->anchor.x == 0);
    assert(editor_ui_run_command(EDITOR_UI_COMMAND_REDO) == 0);
    assert(GAME_GameWidget_get_const(world, entity)->anchor.y == 1);
    char text[] = "Collect the blooms";
    assert(
        app_scene_authoring_set_prototype_field(
            scene,
            "GuideTest",
            "GameWidget",
            "text",
            (pin_value){ .type = PIN_STR, .str = text }
        ) == 0
    );
    text[0] = 'X';
    assert(!strcmp(GAME_GameWidget_get_const(world, entity)->text, "Collect the blooms"));
    puts("prototype fields PASS: vector Undo/Redo and owned text reach instances");
}

void test_navigation_runtime()
{
    ImGuiContext *previous = ImGui_GetCurrentContext();
    ImGuiContext *context = ImGui_CreateContext(NULL);
    ecs_world *world = ecs_world_new();
    for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
        assert(ecs_world_register_component(world, GAME_REGISTRY_COMPONENTS[i], NULL) == ECS_OK);
    }
    behavior_asset *asset = behavior_asset_new("NavigationRuntime");
    editor *e = editor_new(asset);
    game_live_world *live = game_live_world_new(world);
    game_live_definition *definition = game_live_definition_new(
        "NavigationRuntime",
        behavior_asset_new("NavigationRuntime")
    );
    assert(game_live_add_definition(live, definition) == 0);
    engine *runtime = engine_new(world, live);
    app_scene_authoring *
        scene = app_scene_authoring_new(world, live, definition, asset, e, NULL, NULL, runtime);
    assert(scene);
    const char *first = "{\"version\":1,\"name\":\"First\",\"components\":{"
                        "\"GameTransform\":{\"x\":0,\"y\":0},\"GameHealth\":{"
                        "\"current\":73,\"maximum\":100}},\"behaviors\":[]}";
    const char *second = "{\"version\":1,\"name\":\"Second\",\"components\":{"
                         "\"GameTransform\":{\"x\":10,\"y\":0},\"GameHealth\":{"
                         "\"current\":50,\"maximum\":100}},\"behaviors\":[]}";
    assert(app_scene_authoring_register_prototype(scene, first, NULL) == 0);
    assert(app_scene_authoring_register_prototype(scene, second, NULL) == 0);
    assert(app_scene_authoring_add_instance(scene, "First") == 0);
    ecs_entity first_entity = app_scene_authoring_runtime_state(scene).selected_entity;
    app_scene_authoring_navigation_sync(scene);
    assert(app_scene_authoring_add_instance(scene, "Second") == 0);
    ecs_entity deleted = app_scene_authoring_runtime_state(scene).selected_entity;
    app_scene_authoring_navigation_sync(scene);
    assert(app_scene_authoring_delete_instance(scene) == 0);
    assert(!ecs_entity_is_alive(world, deleted));
    assert(app_scene_authoring_add_instance(scene, "Second") == 0);
    ecs_entity replacement = app_scene_authoring_runtime_state(scene).selected_entity;
    assert(ecs_entity_to_u64(replacement) != ecs_entity_to_u64(deleted));
    app_scene_authoring_navigation_sync(scene);
    assert(app_scene_authoring_navigate(scene, false) == 0);
    assert(
        ecs_entity_to_u64(app_scene_authoring_runtime_state(scene).selected_entity) ==
        ecs_entity_to_u64(first_entity)
    );
    assert(strstr(app_scene_authoring_message(scene), "Skipped 1"));
    assert(ecs_entity_is_alive(world, replacement));
    assert(app_scene_authoring_navigate(scene, true) == 0);
    assert(
        ecs_entity_to_u64(app_scene_authoring_runtime_state(scene).selected_entity) ==
        ecs_entity_to_u64(replacement)
    );

    assert(app_scene_authoring_runtime_command(scene, APP_RUNTIME_SCENE, APP_RUNTIME_PLAY) == 0);
    app_scene_authoring_navigation_sync(scene);
    const GameHealth *before = GAME_GameHealth_get_const(world, replacement);
    assert(before);
    int health = before->current;
    assert(app_scene_authoring_open_prototype(scene, "First") == 0);
    app_scene_authoring_navigation_sync(scene);
    assert(app_scene_authoring_navigate(scene, false) == 0);
    app_runtime_state state = app_scene_authoring_runtime_state(scene);
    assert(state.playing && state.target == APP_RUNTIME_SCENE);
    assert(ecs_entity_to_u64(state.selected_entity) == ecs_entity_to_u64(replacement));
    assert(GAME_GameHealth_get_const(world, replacement)->current == health);
    assert(app_scene_authoring_navigate(scene, true) == 0);
    state = app_scene_authoring_runtime_state(scene);
    assert(state.playing && state.target == APP_RUNTIME_SCENE);
    assert(app_scene_authoring_document(scene).kind == APP_EDITOR_DOCUMENT_PROTOTYPE);
    assert(GAME_GameHealth_get_const(world, replacement)->current == health);
    assert(app_scene_authoring_runtime_command(scene, APP_RUNTIME_SCENE, APP_RUNTIME_STOP) == 0);
    first_attachment_redo_after_prototype_gain(scene, world);
    prototype_spawn_refresh(scene, live, world);
    duplicate_instance_preserves_edits(scene, world);
    prototype_vectors_and_text(scene, world);
    app_scene_authoring_free(scene);
    engine_free(runtime);
    game_live_world_free(live);
    editor_free(e);
    ecs_world_free(world);
    ImGui_DestroyContext(context);
    ImGui_SetCurrentContext(previous);
    puts("scene navigation PASS: deleted entity generations are skipped; Back/Forward preserve a playing Scene target and component values");
}
