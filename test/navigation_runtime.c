#include "../app/scene_authoring.h"
#include "../editor/editor.h"
#include "../editor/ui_shell.h"
#include "../game/generated.h"

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
    app_scene_authoring_free(scene);
    engine_free(runtime);
    game_live_world_free(live);
    editor_free(e);
    ecs_world_free(world);
    ImGui_DestroyContext(context);
    ImGui_SetCurrentContext(previous);
    puts("scene navigation PASS: deleted entity generations are skipped; Back/Forward preserve a playing Scene target and component values");
}
