#include "test/editor/command_fixture.h"

#include "editor/commands/internal.h"
#include "runtime/generated.h"
#include "support/xalloc.h"

#include <SDL3/SDL.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <yyjson.h>

static bool drawer_visible(void *user, int panel)
{
    return ((command_fixture *)user)->drawers[panel];
}

static void show_drawer(void *user, int panel, bool visible)
{
    ((command_fixture *)user)->drawers[panel] = visible;
}

static void history(void *, bool) {}

static void frame(void *) {}

void command_fixture_drawers(command_fixture *f)
{
    static const char *const panels[] = { "Assets", "Output", "Performance" };
    editor_command_hooks hooks = { .user = f,
                                   .panels = panels,
                                   .panel_count = 3,
                                   .panel_visible = drawer_visible,
                                   .show_panel = show_drawer,
                                   .history = history,
                                   .frame = frame };
    editor_commands_set_hooks(f->scene, &hooks);
}

command_fixture command_fixture_new()
{
    command_fixture f = { 0 };
    f.world = ecs_world_new();
    for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
        assert(ecs_world_register_component(f.world, GAME_REGISTRY_COMPONENTS[i], NULL) == ECS_OK);
    }
    behavior_asset *asset = behavior_asset_new("Command evaluation");
    f.graph = editor_new(asset);
    f.live = game_live_world_new(f.world);
    game_live_definition *definition = game_live_definition_new(
        "Command evaluation",
        behavior_asset_new("Command evaluation")
    );
    assert(game_live_add_definition(f.live, definition) == 0);
    f.runtime = engine_new(f.world, f.live);
    f.scene = app_scene_authoring_new_headless(f.world, f.live, definition, f.graph, f.runtime, "build/command-live-empty");
    assert(f.scene);
    const char *prototypes[] = { "Player", "Enemy" };
    for (int i = 0; i < 2; i++) {
        char json[512];
        snprintf(
            json,
            sizeof json,
            "{\"name\":\"%s\",\"components\":{"
            "\"GameAppearance\":{\"width\":32,\"height\":24,\"color\":"
            "4294967295},"
            "\"GameCollider\":{\"shape\":\"Box\",\"width\":32,\"height\":24}}}",
            prototypes[i]
        );
        assert(app_scene_authoring_register_prototype(f.scene, json, NULL) == 0);
    }
    assert(
        scene_authoring_load_json(
            f.scene,
            "{\"version\":1,\"name\":\"Command evaluation\",\"entities\":["
            "{\"name\":\"Ship\",\"prototype\":\"Player\"},"
            "{\"name\":\"Foe\",\"prototype\":\"Enemy\"}]}",
            NULL
        ) == 0
    );
    scene_authoring_select_document(f.scene, APP_EDITOR_DOCUMENT_SCENE, APP_SCENE_DOCUMENT_ID);
    f.scene->selected = -1;
    for (int i = 0; i < acount(f.scene->actors); i++) {
        f.scene->actors[i].marked = false;
    }
    return f;
}

void command_fixture_free(command_fixture *f)
{
    app_scene_authoring_free(f->scene);
    engine_free(f->runtime);
    game_live_world_free(f->live);
    editor_free(f->graph);
    ecs_world_free(f->world);
}

char *command_fixture_submit(app_scene_authoring *scene, const char *goal)
{
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_str(doc, root, "operation", "submit");
    yyjson_mut_obj_add_strcpy(doc, root, "command", goal);
    yyjson_mut_obj_add_obj(doc, root, "context");
    char *request = yyjson_mut_write(doc, 0, NULL);
    yyjson_mut_doc_free(doc);
    char *reply = editor_commands_api(scene, request);
    free(request);
    return reply;
}

char *command_fixture_settled(app_scene_authoring *scene, char *reply)
{
    Uint64 started = SDL_GetTicks();
    for (;;) {
        yyjson_doc *read = yyjson_read(reply, strlen(reply), 0);
        bool waiting = !strcmp(command_string(yyjson_doc_get_root(read), "status"), "pending");
        yyjson_doc_free(read);
        if (!waiting || SDL_GetTicks() - started > 35000) {
            return reply;
        }
        editor_commands_frame(scene);
        free(reply);
        reply = xstrdup(editor_commands_result(scene));
        SDL_Delay(1);
    }
}
