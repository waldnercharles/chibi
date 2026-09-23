#include "editor/commands/internal.h"
#include "editor/commands/model.h"
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
    ecs_world *world;
    game_live_world *live;
    engine *runtime;
    editor *graph;
    app_scene_authoring *scene;
} laya_fixture;

static laya_fixture fixture_new()
{
    laya_fixture f = { 0 };
    f.previous = ImGui_GetCurrentContext();
    f.context = ImGui_CreateContext(NULL);
    f.world = ecs_world_new();
    for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
        assert(ecs_world_register_component(f.world, GAME_REGISTRY_COMPONENTS[i], NULL) == ECS_OK);
    }
    behavior_asset *asset = behavior_asset_new("Laya live evaluation");
    f.graph = editor_new(asset);
    f.live = game_live_world_new(f.world);
    game_live_definition *definition = game_live_definition_new(
        "Laya live evaluation",
        behavior_asset_new("Laya live evaluation")
    );
    assert(game_live_add_definition(f.live, definition) == 0);
    f.runtime = engine_new(f.world, f.live);
    f.scene = app_scene_authoring_new_headless(f.world, f.live, definition, asset, f.graph, f.runtime);
    assert(f.scene);
    assert(
        app_scene_authoring_register_prototype(
            f.scene,
            "{\"version\":1,\"name\":\"Player\",\"components\":{"
            "\"GameAppearance\":{\"width\":32,\"height\":24,\"color\":"
            "4294967295},"
            "\"GameCollider\":{\"shape\":1,\"width\":32,\"height\":24}},"
            "\"behaviors\":[]}",
            NULL
        ) == 0
    );
    assert(
        app_scene_authoring_register_prototype(
            f.scene,
            "{\"version\":1,\"name\":\"Enemy\",\"components\":{"
            "\"GameAppearance\":{\"width\":32,\"height\":24,\"color\":"
            "4294967295},"
            "\"GameCollider\":{\"shape\":1,\"width\":32,\"height\":24}},"
            "\"behaviors\":[]}",
            NULL
        ) == 0
    );
    assert(
        scene_authoring_load_json(
            f.scene,
            "{\"version\":1,\"name\":\"Laya live evaluation\",\"entities\":["
            "{\"name\":\"Ship\",\"prototype\":\"Player\",\"components\":{},"
            "\"behaviors\":[]},"
            "{\"name\":\"Foe\",\"prototype\":\"Enemy\",\"components\":{},"
            "\"behaviors\":[]}]}",
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

static void fixture_free(laya_fixture *f)
{
    app_scene_authoring_free(f->scene);
    engine_free(f->runtime);
    game_live_world_free(f->live);
    editor_free(f->graph);
    ecs_world_free(f->world);
    ImGui_DestroyContext(f->context);
    ImGui_SetCurrentContext(f->previous);
}

static char *submit(app_scene_authoring *scene, const char *goal)
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

static char *settled(app_scene_authoring *scene, char *reply)
{
    Uint64 started = SDL_GetTicks();
    for (;;) {
        yyjson_doc *read = yyjson_read(reply, strlen(reply), 0);
        const char *status = command_string(yyjson_doc_get_root(read), "status");
        bool waiting = !strcmp(status, "pending") || !strcmp(status, "continue");
        yyjson_doc_free(read);
        if (!waiting || SDL_GetTicks() - started > 15000) {
            return reply;
        }
        editor_commands_frame(scene);
        free(reply);
        reply = xstrdup(editor_commands_result(scene));
        SDL_Delay(1);
    }
}

static bool operation_matches(app_scene_authoring *scene, yyjson_val *operation, yyjson_val *expected)
{
    const char *op = command_string(expected, "op");
    if (!*op) {
        op = "set";
    }
    if (strcmp(command_string(operation, "op"), op)) {
        return false;
    }
    if (!strcmp(op, "panel")) {
        yyjson_val *actual = yyjson_obj_get(operation, "value");
        yyjson_val *wanted = yyjson_obj_get(expected, "value");
        return !strcmp(command_string(operation, "target"), command_string(expected, "target")) &&
               yyjson_is_bool(actual) && yyjson_is_bool(wanted) &&
               yyjson_get_bool(actual) == yyjson_get_bool(wanted);
    }
    if (strcmp(op, "set")) {
        return false;
    }
    const char *scope = command_string(expected, "scope");
    const char *target_name = command_string(expected, "target");
    const char *component = command_string(expected, "component");
    const char *field_name = command_string(expected, "field");
    yyjson_val *value = yyjson_obj_get(expected, "value");
    yyjson_val *actual_value = yyjson_obj_get(operation, "value");
    if (!*scope) {
        scope = "prototype";
    }
    if (!*target_name) {
        target_name = command_string(expected, "expected_target");
    }
    if (!*component) {
        component = command_string(expected, "expected_component");
    }
    if (!*field_name) {
        field_name = command_string(expected, "expected_field");
    }
    if (!value) {
        value = yyjson_obj_get(expected, "expected_value");
    }
    if (!*scope || !*target_name || !*component || !*field_name ||
        !yyjson_is_num(value) || !yyjson_is_num(actual_value)) {
        return false;
    }
    if (fabs(yyjson_get_num(actual_value) - yyjson_get_num(value)) >= 0.00001) {
        return false;
    }
    char id[256];
    snprintf(id, sizeof id, "GAME.%s.%s", component, field_name);
    if (strcmp(command_string(operation, "field"), id)) {
        return false;
    }
    const char *kind = !strcmp(scope, "instance") ? "entity" : scope;
    char *catalog = editor_commands_catalog(scene);
    yyjson_doc *doc = yyjson_read(catalog, strlen(catalog), 0);
    yyjson_val *objects = yyjson_obj_get(yyjson_doc_get_root(doc), "objects");
    yyjson_val *target = command_find(objects, command_string(operation, "target"));
    bool correct = target && !strcmp(command_string(target, "name"), target_name) &&
                   !strcmp(command_string(target, "kind"), kind);
    yyjson_doc_free(doc);
    free(catalog);
    return correct;
}

static bool operation_readback(app_scene_authoring *scene, yyjson_val *expected)
{
    const char *op = command_string(expected, "op");
    if (!*op) {
        op = "set";
    }
    char *catalog = editor_commands_catalog(scene);
    yyjson_doc *doc = yyjson_read(catalog, strlen(catalog), 0);
    yyjson_val *root = yyjson_doc_get_root(doc);
    bool correct = false;
    if (!strcmp(op, "panel")) {
        yyjson_val *panels = yyjson_obj_get(root, "panels");
        const char *target = command_string(expected, "target");
        size_t index, count;
        yyjson_val *panel;
        yyjson_arr_foreach(panels, index, count, panel)
        {
            if (!strcmp(command_string(panel, "id"), target)) {
                yyjson_val *visible = yyjson_obj_get(panel, "visible");
                yyjson_val *wanted = yyjson_obj_get(expected, "value");
                correct = yyjson_is_bool(visible) && yyjson_is_bool(wanted) &&
                          yyjson_get_bool(visible) == yyjson_get_bool(wanted);
            }
        }
    } else if (!strcmp(op, "set")) {
        const char *scope = command_string(expected, "scope");
        const char *target = command_string(expected, "target");
        const char *component = command_string(expected, "component");
        const char *field = command_string(expected, "field");
        yyjson_val *wanted = yyjson_obj_get(expected, "value");
        if (!*scope) {
            scope = "prototype";
        }
        if (!*target) {
            target = command_string(expected, "expected_target");
        }
        if (!*component) {
            component = command_string(expected, "expected_component");
        }
        if (!*field) {
            field = command_string(expected, "expected_field");
        }
        if (!wanted) {
            wanted = yyjson_obj_get(expected, "expected_value");
        }
        char id[256];
        snprintf(id, sizeof id, "GAME.%s.%s", component, field);
        const char *kind = !strcmp(scope, "instance") ? "entity" : scope;
        yyjson_val *objects = yyjson_obj_get(root, "objects");
        size_t index, count;
        yyjson_val *object;
        yyjson_arr_foreach(objects, index, count, object)
        {
            if (strcmp(command_string(object, "kind"), kind) ||
                strcmp(command_string(object, "name"), target)) {
                continue;
            }
            yyjson_val *fields = yyjson_obj_get(object, "fields");
            size_t field_index, field_count;
            yyjson_val *field;
            yyjson_arr_foreach(fields, field_index, field_count, field)
            {
                if (!strcmp(command_string(field, "id"), id)) {
                    yyjson_val *actual = yyjson_obj_get(field, "value");
                    correct = yyjson_is_num(actual) && yyjson_is_num(wanted) &&
                              fabs(yyjson_get_num(actual) - yyjson_get_num(wanted)) <
                                  0.00001;
                }
            }
        }
    }
    yyjson_doc_free(doc);
    free(catalog);
    return correct;
}

static bool run_case(yyjson_val *item)
{
    laya_fixture f = fixture_new();
    const char *goal = command_string(item, "goal");
    const char *id = command_string(item, "id");
    yyjson_val *native = yyjson_obj_get(item, "native_expected");
    yyjson_val *expected_operations = yyjson_obj_get(item, "native_expected_operations");
    yyjson_val *expected_single = native;
    const char *expected_status = command_string(item, "native_expected_status");
    Uint64 started = SDL_GetTicks();
    char *reply = settled(f.scene, submit(f.scene, goal));
    yyjson_doc *read = yyjson_read(reply, strlen(reply), 0);
    yyjson_val *result = yyjson_doc_get_root(read);
    const char *status = command_string(result, "status");
    int calls = (int)yyjson_get_int(yyjson_obj_get(result, "model_calls"));
    bool correct = false;
    yyjson_val *operations = yyjson_obj_get(result, "operations");
    int expected_count = expected_operations
                           ? (int)yyjson_arr_size(expected_operations)
                       : native || *command_string(item, "expected_target") ? 1
                                                                            : 0;
    if (!*expected_status) {
        expected_status = expected_count ? "needs_review" : "";
    }
    if (!expected_operations && !expected_single && expected_count) {
        expected_single = item;
    }
    bool supported = !strcmp(expected_status, "needs_review");
    bool proposal_matches = supported && !strcmp(status, expected_status) &&
                            yyjson_arr_size(operations) == (size_t)expected_count &&
                            expected_count > 0;
    bool matched[64] = { false };
    if (expected_count > (int)(sizeof matched / sizeof matched[0])) {
        proposal_matches = false;
    }
    for (int i = 0; proposal_matches && i < expected_count; i++) {
        yyjson_val *proposed = yyjson_arr_get(operations, i);
        bool found = false;
        for (int j = 0; j < expected_count; j++) {
            if (matched[j]) {
                continue;
            }
            yyjson_val *expected = expected_operations
                                     ? yyjson_arr_get(expected_operations, j)
                                     : expected_single;
            if (operation_matches(f.scene, proposed, expected)) {
                matched[j] = true;
                found = true;
                break;
            }
        }
        if (!found) {
            proposal_matches = false;
        }
    }
    if (proposal_matches) {
        yyjson_doc_free(read);
        free(reply);
        reply = editor_commands_api(f.scene, "{\"operation\":\"respond\",\"approve\":true}");
        read = yyjson_read(reply, strlen(reply), 0);
        result = yyjson_doc_get_root(read);
        status = command_string(result, "status");
        yyjson_val *verified = yyjson_obj_get(result, "verified");
        correct = !strcmp(status, "applied") &&
                  (!verified || yyjson_get_bool(verified));
        for (int i = 0; correct && i < expected_count; i++) {
            yyjson_val *expected = expected_operations
                                     ? yyjson_arr_get(expected_operations, i)
                                     : expected_single;
            correct = operation_readback(f.scene, expected);
        }
        yyjson_val *unchanged = yyjson_obj_get(item, "native_unchanged");
        for (size_t i = 0; correct && i < yyjson_arr_size(unchanged); i++) {
            correct = operation_readback(f.scene, yyjson_arr_get(unchanged, i));
        }
    } else if (!supported) {
        correct = !strcmp(status, expected_status) && yyjson_arr_size(operations) == 0;
    }
    printf(
        "%s %s status=%s model_calls=%d elapsed_ms=%llu\n",
        correct ? "PASS" : "FAIL",
        id,
        status,
        calls,
        (unsigned long long)(SDL_GetTicks() - started)
    );
    if (!correct && getenv("LAYA_LIVE_VERBOSE")) {
        fprintf(stderr, "%s\n", reply);
    }
    yyjson_doc_free(read);
    free(reply);
    fixture_free(&f);
    return correct;
}

int test_editor_command_laya_live(const char *provider, const char *path)
{
    char host[256], model[128], error[256] = { 0 };
    editor_command_model_get_settings(host, sizeof host, model, sizeof model);
    bool previous_schema = editor_command_model_json_schema();
    const char *endpoint = !strcmp(provider, "laya")
                             ? "http://127.0.0.1:8768/v1/decide"
                             : "http://127.0.0.1:11435/api";
    const char *override = getenv("CHIBI_LIVE_HOST");
    if (override && *override) {
        endpoint = override;
    }
    if (!editor_command_model_set_settings(endpoint, provider, error, sizeof error)) {
        fprintf(stderr, "%s\n", error);
        return 1;
    }
    editor_command_model_set_json_schema(getenv("CHIBI_LIVE_JSON_SCHEMA") != NULL);
    yyjson_read_err read_error;
    yyjson_doc *doc = yyjson_read_file(path, 0, NULL, &read_error);
    if (!doc) {
        fprintf(stderr, "Could not load Laya cases: %s\n", read_error.msg);
        editor_command_model_set_settings(host, model, error, sizeof error);
        editor_command_model_set_json_schema(previous_schema);
        return 1;
    }
    yyjson_val *cases = yyjson_obj_get(yyjson_doc_get_root(doc), "cases");
    int passed = 0, total = (int)yyjson_arr_size(cases), executed = 0;
    const char *only = getenv("LAYA_LIVE_CASE");
    const char *limit = getenv("LAYA_LIVE_LIMIT");
    if (limit && atoi(limit) > 0 && atoi(limit) < total) {
        total = atoi(limit);
    }
    for (int i = 0; i < total; i++) {
        yyjson_val *item = yyjson_arr_get(cases, i);
        if (only && strcmp(only, command_string(item, "id"))) {
            continue;
        }
        executed++;
        passed += run_case(item);
    }
    printf("%s live editor: %d/%d complete outcomes\n", provider, passed, executed);
    yyjson_doc_free(doc);
    editor_command_model_set_settings(host, model, error, sizeof error);
    editor_command_model_set_json_schema(previous_schema);
    return passed == executed && executed ? 0 : 1;
}
