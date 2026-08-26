#include "../app/cute_host.h"
#include "../app/scene_authoring.h"
#include "../core/blueprint.h"
#include "../core/registry.h"
#include "../editor/editor.h"
#include "../editor/ui_shell.h"
#include "../engine/engine.h"
#include "../game/generated.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char PLAYER_PROTOTYPE_JSON
    [] = "{\"name\":\"PlayerPrototype\",\"components\":{"
         "\"GameTransform\":{\"x\":0,\"y\":0},"
         "\"GameHealth\":{\"current\":100,\"maximum\":100}},"
         "\"behaviors\":[\"SceneBehavior\"]}";
static const char
    PROTOTYPE_JSON[] = "{\"name\":\"EnemyPrototype\",\"components\":{"
                       "\"GameTransform\":{\"x\":80,\"y\":0},"
                       "\"GameHealth\":{\"current\":100,\"maximum\":100}},"
                       "\"behaviors\":[\"SceneBehavior\"]}";

static char *read_file(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (!file || fseek(file, 0, SEEK_END) != 0) {
        if (file) {
            fclose(file);
        }
        return NULL;
    }
    long size = ftell(file);
    if (size < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    char *text = (char *)malloc((size_t)size + 1);
    if (!text) {
        fclose(file);
        return NULL;
    }
    size_t length = fread(text, 1, (size_t)size, file);
    fclose(file);
    text[length] = '\0';
    return text;
}

int main(int argc, char **argv)
{
    CF_Result host = cf_make_app(
        "prototype-authoring-check",
        0,
        0,
        0,
        320,
        240,
        CF_APP_OPTIONS_WINDOW_POS_CENTERED_BIT | CF_APP_OPTIONS_NO_AUDIO_BIT,
        argc ? argv[0] : "prototype-authoring-check"
    );
    if (cf_is_error(host)) {
        return 1;
    }
    cf_app_init_imgui();
    const char *scene_path = "prototype_authoring_check_scene.json";
    const char *prototype_path = "prototype_authoring_check_prototype.json";
    ecs_world *world = ecs_world_new();
    ecs_world_register_component(world, &GAME_COMPONENT_GameTransform, NULL);
    ecs_world_register_component(world, &GAME_COMPONENT_GameHealth, NULL);
    game_live_world *live = game_live_world_new(world);
    game_live_definition *definition = game_live_definition_new("SceneBehavior", blueprint_new("SceneBehavior"));
    editor *graph_editor = editor_new(blueprint_new("Editor"));
    engine *runtime = engine_new(world, live);
    app_scene_authoring *scene = app_scene_authoring_new(
        world,
        live,
        definition,
        editor_blueprint(graph_editor),
        graph_editor,
        NULL,
        NULL,
        runtime
    );
    int ok = world && live && definition && graph_editor && runtime && scene;
    int stage = 0;
    int attachments_before = -1;
    if (ok && !editor_ui_take_focus(EDITOR_UI_FOCUS_SCENE)) {
        ok = 0;
        stage = 8;
    }
    if (ok && editor_ui_take_focus(EDITOR_UI_FOCUS_SCENE)) {
        ok = 0;
        stage = 9;
    }
    editor_ui_request_focus(EDITOR_UI_FOCUS_NODE_CANVAS);
    if (ok && !editor_ui_take_focus(EDITOR_UI_FOCUS_NODE_CANVAS)) {
        ok = 0;
        stage = 10;
    }
    editor_ui_request_focus(EDITOR_UI_FOCUS_SCENE);
    editor_ui_request_focus(EDITOR_UI_FOCUS_NONE);
    if (ok && editor_ui_take_focus(EDITOR_UI_FOCUS_SCENE)) {
        ok = 0;
        stage = 11;
    }
    if (ok && game_live_add_definition(live, definition) != 0) {
        ok = 0;
        stage = 1;
    }
    if (ok &&
        game_live_duplicate_definition(live, "InputBehavior", editor_blueprint(graph_editor)) !=
            0) {
        ok = 0;
        stage = 12;
    }
    if (ok &&
        (app_scene_authoring_register_prototype(scene, PLAYER_PROTOTYPE_JSON, NULL) != 0 ||
         app_scene_authoring_register_prototype(scene, PROTOTYPE_JSON, prototype_path) !=
             0)) {
        ok = 0;
        stage = 2;
    }
    app_editor_document_context document = app_scene_authoring_document(scene);
    if (ok && (!document.id || document.kind != APP_EDITOR_DOCUMENT_SCENE ||
               strcmp(document.id, "Scene") != 0)) {
        ok = 0;
        stage = 7;
    }
    if (ok && app_scene_authoring_seed_default(scene) != 0) {
        ok = 0;
        stage = 3;
    }
    if (ok) {
        attachments_before = game_behavior_attachment_count(live->behaviors);
    }
    if (ok && app_scene_authoring_attach_prototype_behavior(scene, "EnemyPrototype", "InputBehavior") !=
                  0) {
        ok = 0;
        stage = 13;
    }
    if (ok && game_behavior_attachment_count(live->behaviors) !=
                  attachments_before + 1) {
        ok = 0;
        stage = 14;
    }
    game_live_definition *input_definition = game_live_find_definition(live, "InputBehavior");
    if (ok && (app_scene_authoring_open_behavior(scene, "InputBehavior") != 0 ||
               !input_definition)) {
        ok = 0;
        stage = 15;
    }
    if (ok) {
        editor_tick(graph_editor, 0.0);
        app_scene_authoring_set_editor_working(scene, editor_blueprint(graph_editor));
        const blueprint *applied_before = input_definition->applied;
        node *added = editor_spawn(graph_editor, registry_node_by_name("Event"), 0.0f, 0.0f);
        editor_tick(graph_editor, 1.0);
        app_scene_authoring_set_editor_working(scene, editor_blueprint(graph_editor));
        int applied = app_scene_authoring_apply_pending(scene);
        const blueprint *applied_after = input_definition->applied;
        if (!added || applied != 0 || applied_after == applied_before ||
            input_definition->dirty ||
            !strstr(app_scene_authoring_message(scene), "destroyed and recreated")) {
            ok = 0;
            stage = 16;
        }
    }
    if (ok && app_scene_authoring_save(scene, scene_path) != 0) {
        ok = 0;
        stage = 4;
    }
    if (ok && app_scene_authoring_save_prototype(scene, "EnemyPrototype", prototype_path) !=
                  0) {
        ok = 0;
        stage = 5;
    }
    if (ok && app_scene_authoring_load(scene, scene_path) != 0) {
        ok = 0;
        stage = 6;
    }
    char *saved_scene = read_file(scene_path);
    char *saved_prototype = read_file(prototype_path);
    ok = ok && saved_scene && saved_prototype &&
         strstr(saved_scene, "EnemyPrototype") &&
         strstr(saved_scene, "InputBehavior") &&
         strstr(saved_scene, "behavior_additions") &&
         strstr(saved_prototype, "GameHealth") &&
         strstr(saved_prototype, "SceneBehavior") &&
         strstr(saved_prototype, "InputBehavior") && strstr(saved_prototype, "80");
    if (!ok) {
        fprintf(
            stderr,
            "prototype authoring stage %d failed: %s\n%s\n%s\n",
            stage,
            app_scene_authoring_message(scene),
            saved_scene ? saved_scene : "(no scene)",
            saved_prototype ? saved_prototype : "(no prototype)"
        );
    }
    printf("%s: prototype-first authoring and persistence\n", ok ? "PASS" : "FAIL");
    free(saved_scene);
    free(saved_prototype);
    remove(scene_path);
    remove(prototype_path);
    app_scene_authoring_free(scene);
    engine_free(runtime);
    editor_free(graph_editor);
    game_live_world_free(live);
    ecs_world_free(world);
    cf_destroy_app();
    return ok ? 0 : 1;
}
