#include "assets/project.h"
#include "editor/commands/command.h"
#include "editor/commands/internal.h"
#include "editor/commands/model.h"
#include "editor/workspace/scene_authoring.h"
#include "editor/workspace/scene_internal.h"
#include "runtime/generated.h"
#include "support/xalloc.h"
#include "test/suite_internal.h"

#include <SDL3/SDL.h>
#include <assert.h>
#include <imgui/dcimgui.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yyjson.h>

typedef struct
{
    ImGuiContext *previous, *context;
    chibi_project project;
    ecs_world *world;
    game_live_world *live;
    engine *runtime;
    editor *graph;
    app_scene_authoring *scene;
} project_fixture;

static project_fixture fixture_new(const char *project_path)
{
    project_fixture f = { 0 };
    char error[256] = { 0 };
    f.previous = ImGui_GetCurrentContext();
    f.context = ImGui_CreateContext(NULL);
    assert(chibi_project_open(&f.project, project_path, error, sizeof error) == 0);
    f.world = ecs_world_new();
    for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
        assert(ecs_world_register_component(f.world, GAME_REGISTRY_COMPONENTS[i], NULL) == ECS_OK);
    }
    behavior_asset *working = behavior_asset_new("AppBehavior");
    f.graph = editor_new(working);
    f.live = game_live_world_new(f.world);
    char *json = behavior_asset_save_json(working);
    behavior_asset *definition_asset = behavior_asset_load_json(json);
    free(json);
    game_live_definition *definition = game_live_definition_new("AppBehavior", definition_asset);
    f.runtime = engine_new(f.world, f.live);
    assert(f.graph && f.live && definition && f.runtime);
    assert(game_live_add_definition(f.live, definition) == 0);
    f.scene = app_scene_authoring_new_headless(
        f.world,
        f.live,
        definition,
        editor_behavior_asset(f.graph),
        f.graph,
        f.runtime,
        f.project.content
    );
    assert(f.scene);
    assert(app_scene_authoring_load_content(f.scene) == 0);
    char *scene_path = chibi_project_scene_path(&f.project, NULL);
    assert(scene_path && app_scene_authoring_load(f.scene, scene_path) == 0);
    free(scene_path);
    assert(app_scene_authoring_apply_pending(f.scene) == 0);
    return f;
}

static void fixture_free(project_fixture *f)
{
    app_scene_authoring_free(f->scene);
    engine_free(f->runtime);
    game_live_world_free(f->live);
    editor_free(f->graph);
    ecs_world_free(f->world);
    chibi_project_free(&f->project);
    ImGui_DestroyContext(f->context);
    ImGui_SetCurrentContext(f->previous);
}

static char *submit_dry_run(app_scene_authoring *scene, const char *goal)
{
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_str(doc, root, "operation", "submit");
    yyjson_mut_obj_add_str(doc, root, "command", goal);
    yyjson_mut_obj_add_bool(doc, root, "dry_run", true);
    yyjson_mut_obj_add_obj(doc, root, "context");
    char *request = yyjson_mut_write(doc, 0, NULL);
    yyjson_mut_doc_free(doc);
    char *reply = editor_commands_api(scene, request);
    free(request);
    return reply;
}

static char *settled(app_scene_authoring *scene, char *reply)
{
    Uint64 started = SDL_GetTicks();
    for (;;) {
        yyjson_doc *doc = yyjson_read(reply, strlen(reply), 0);
        const char *status = command_string(yyjson_doc_get_root(doc), "status");
        bool waiting = !strcmp(status, "pending") || !strcmp(status, "continue");
        yyjson_doc_free(doc);
        if (!waiting || SDL_GetTicks() - started > 35000) {
            return reply;
        }
        editor_commands_frame(scene);
        free(reply);
        reply = xstrdup(editor_commands_result(scene));
        SDL_Delay(1);
    }
}

static yyjson_val *find_named(yyjson_val *items, const char *key, const char *value)
{
    size_t i, count;
    yyjson_val *item;
    yyjson_arr_foreach(items, i, count, item)
    {
        const char *found = yyjson_get_str(yyjson_obj_get(item, key));
        if (found && !strcmp(found, value)) {
            return item;
        }
    }
    return NULL;
}

static bool field_number(app_scene_authoring *scene, const char *target, const char *field_id, double *value)
{
    char *catalog = editor_commands_catalog(scene);
    yyjson_doc *doc = yyjson_read(catalog, strlen(catalog), 0);
    yyjson_val *root = yyjson_doc_get_root(doc);
    yyjson_val *object = find_named(yyjson_obj_get(root, "objects"), "id", target);
    yyjson_val *field = object
                          ? find_named(yyjson_obj_get(object, "fields"), "id", field_id)
                          : NULL;
    yyjson_val *actual = field ? yyjson_obj_get(field, "value") : NULL;
    bool valid = yyjson_is_num(actual);
    if (valid) {
        *value = yyjson_get_num(actual);
    }
    yyjson_doc_free(doc);
    free(catalog);
    return valid;
}

static bool exact_proposal(yyjson_val *result, const char *target, const char *field, double value)
{
    yyjson_val *operations = yyjson_obj_get(result, "operations");
    yyjson_val *operation = yyjson_arr_get(operations, 0);
    return !strcmp(command_string(result, "status"), "needs_review") &&
           yyjson_arr_size(operations) == 1 &&
           !strcmp(command_string(operation, "op"), "set") &&
           !strcmp(command_string(operation, "target"), target) &&
           !strcmp(command_string(operation, "field"), field) &&
           fabs(yyjson_get_num(yyjson_obj_get(operation, "value")) - value) < 0.00001;
}

int test_editor_command_project_live(const char *provider, const char *project_path)
{
    char host[256], model[128], error[256] = { 0 };
    editor_command_model_get_settings(host, sizeof host, model, sizeof model);
    bool previous_schema = editor_command_model_json_schema();
    const char *endpoint = getenv("CHIBI_LIVE_HOST");
    if (!endpoint || !*endpoint) {
        endpoint = "http://127.0.0.1:11435/api";
    }
    if (!editor_command_model_set_settings(endpoint, provider, error, sizeof error)) {
        fprintf(stderr, "%s\n", error);
        return 1;
    }
    editor_command_model_set_json_schema(getenv("CHIBI_LIVE_JSON_SCHEMA") != NULL);

    project_fixture f = fixture_new(project_path);
    char *catalog = editor_commands_api(f.scene, "{\"operation\":\"inspect\"}");
    yyjson_doc *catalog_doc = yyjson_read(catalog, strlen(catalog), 0);
    yyjson_val *catalog_root = yyjson_doc_get_root(catalog_doc);
    yyjson_val *objects = yyjson_obj_get(catalog_root, "objects");
    yyjson_val *player = NULL;
    size_t i, count;
    yyjson_val *object;
    yyjson_arr_foreach(objects, i, count, object)
    {
        if (!strcmp(command_string(object, "kind"), "prototype") &&
            !strcmp(command_string(object, "name"), "Player")) {
            player = object;
            break;
        }
    }
    const char *target = player ? command_string(player, "id") : "";
    const char *field_id = "GAME.GameAppearance.width";
    yyjson_val *field = player
                          ? find_named(yyjson_obj_get(player, "fields"), "id", field_id)
                          : NULL;
    yyjson_val *initial = field ? yyjson_obj_get(field, "value") : NULL;
    double before = yyjson_get_num(initial);
    bool passed = target[0] && field && yyjson_is_num(initial);
    test_check(passed, "real project catalog exposes Player appearance width");

    char path[2048];
    snprintf(path, sizeof path, "%s/prototypes/Player.json", f.project.content);
    FILE *file = fopen(path, "rb");
    char *source_before = NULL;
    size_t source_size = 0;
    if (file) {
        fseek(file, 0, SEEK_END);
        long length = ftell(file);
        rewind(file);
        if (length >= 0) {
            source_size = (size_t)length;
            source_before = xmalloc(source_size + 1);
            source_size = fread(source_before, 1, source_size, file);
            source_before[source_size] = 0;
        }
        fclose(file);
    }

    const char *goal = "Set the Player prototype's GameAppearance.width to 21.";
    char *reply = settled(f.scene, submit_dry_run(f.scene, goal));
    yyjson_doc *result_doc = yyjson_read(reply, strlen(reply), 0);
    yyjson_val *result = yyjson_doc_get_root(result_doc);
    bool proposal = exact_proposal(result, target, field_id, 21.0) &&
                    yyjson_get_int(yyjson_obj_get(result, "model_calls")) == 1;
    passed &= proposal;
    test_check(proposal, "GPT-OSS proposes the exact prototype edit on the authored scene");

    if (proposal) {
        free(reply);
        reply = settled(f.scene, editor_commands_api(f.scene, "{\"operation\":\"respond\",\"approve\":true}"));
        yyjson_doc_free(result_doc);
        result_doc = yyjson_read(reply, strlen(reply), 0);
        result = yyjson_doc_get_root(result_doc);
    }
    double after = 0;
    bool dry_readback = !strcmp(command_string(result, "status"), "ready") &&
                        field_number(f.scene, target, field_id, &after) &&
                        fabs(after - before) < 0.00001;
    passed &= dry_readback;
    test_check(dry_readback, "approved dry run reads back the unchanged authored catalog value");

    bool unchanged_file = false;
    file = fopen(path, "rb");
    if (file) {
        if (source_before) {
            char *source_after = xmalloc(source_size + 1);
            size_t after_size = fread(source_after, 1, source_size + 1, file);
            unchanged_file = after_size == source_size &&
                             !memcmp(source_before, source_after, source_size);
            free(source_after);
        }
        fclose(file);
    }
    passed &= unchanged_file;
    test_check(unchanged_file, "dry-run leaves the authored Player asset unchanged");

    char result_status[32];
    snprintf(result_status, sizeof result_status, "%s", command_string(result, "status"));
    int model_calls = (int)yyjson_get_int(yyjson_obj_get(result, "model_calls"));
    Uint64 elapsed = yyjson_get_uint(yyjson_obj_get(result, "elapsed_ms"));

    free(source_before);
    yyjson_doc_free(result_doc);
    free(reply);
    yyjson_doc_free(catalog_doc);
    free(catalog);
    fixture_free(&f);
    editor_command_model_set_settings(host, model, error, sizeof error);
    editor_command_model_set_json_schema(previous_schema);
    printf(
        "%s real-project command: status=%s model_calls=%d elapsed_ms=%llu\n",
        provider,
        result_status,
        model_calls,
        (unsigned long long)elapsed
    );
    return passed ? 0 : 1;
}
