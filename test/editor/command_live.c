#include "editor/commands/internal.h"
#include "editor/commands/model.h"
#include "support/xalloc.h"
#include "test/editor/command_fixture.h"
#include "test/suite_internal.h"

#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yyjson.h>

/* Numbers within tolerance; choices by their label. */
static bool same_value(yyjson_val *actual, yyjson_val *wanted)
{
    if (yyjson_is_num(actual) && yyjson_is_num(wanted)) {
        return fabs(yyjson_get_num(actual) - yyjson_get_num(wanted)) < 0.00001;
    }
    return yyjson_is_str(wanted) && yyjson_equals(actual, wanted);
}

static char *answer(app_scene_authoring *scene, const char *value)
{
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_str(doc, root, "operation", "respond");
    yyjson_mut_obj_add_strcpy(doc, root, "value", value);
    char *request = yyjson_mut_write(doc, 0, NULL);
    yyjson_mut_doc_free(doc);
    char *reply = editor_commands_api(scene, request);
    free(request);
    return reply;
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
    if (!*scope || !*target_name || !*component || !*field_name || !value ||
        !same_value(actual_value, value)) {
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
                    correct = same_value(yyjson_obj_get(field, "value"), wanted);
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
    command_fixture f = command_fixture_new();
    command_fixture_drawers(&f);
    const char *goal = command_string(item, "goal");
    const char *id = command_string(item, "id");
    yyjson_val *native = yyjson_obj_get(item, "native_expected");
    yyjson_val *expected_operations = yyjson_obj_get(item, "native_expected_operations");
    yyjson_val *expected_single = native;
    const char *expected_status = command_string(item, "native_expected_status");
    Uint64 started = SDL_GetTicks();
    char *reply = command_fixture_settled(f.scene, command_fixture_submit(f.scene, goal));
    yyjson_doc *read = yyjson_read(reply, strlen(reply), 0);
    yyjson_val *result = yyjson_doc_get_root(read);
    const char *status = command_string(result, "status");
    const char *followup = command_string(item, "native_reply");
    bool asked = true;
    if (*followup) {
        asked = !strcmp(status, "needs_clarification") &&
                !yyjson_arr_size(yyjson_obj_get(result, "operations"));
        if (asked) {
            yyjson_doc_free(read);
            free(reply);
            reply = command_fixture_settled(f.scene, answer(f.scene, followup));
            read = yyjson_read(reply, strlen(reply), 0);
            result = yyjson_doc_get_root(read);
            status = command_string(result, "status");
        }
    }
    int calls = (int)yyjson_get_int(yyjson_obj_get(result, "model_calls"));
    bool correct = false;
    yyjson_val *operations = yyjson_obj_get(result, "operations");
    int expected_count = expected_operations
                           ? (int)yyjson_arr_size(expected_operations)
                       : native || *command_string(item, "expected_target") ? 1
                                                                            : 0;
    if (!*expected_status) {
        expected_status = expected_count ? "applied" : "";
    }
    if (!expected_operations && !expected_single && expected_count) {
        expected_single = item;
    }
    bool supported = !strcmp(expected_status, "applied");
    bool proposal_matches = asked && supported && !strcmp(status, expected_status) &&
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
    /* Undoable proposals apply at once; the applied operations must be exactly
       the expected ones and read back. */
    if (proposal_matches) {
        yyjson_val *verified = yyjson_obj_get(result, "verified");
        correct = !verified || yyjson_get_bool(verified);
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
        correct = asked && !strcmp(status, expected_status) &&
                  yyjson_arr_size(operations) == 0;
    }
    printf(
        "%s %s status=%s model_calls=%d elapsed_ms=%llu\n",
        correct ? "PASS" : "FAIL",
        id,
        status,
        calls,
        (unsigned long long)(SDL_GetTicks() - started)
    );
    if (!correct && getenv("COMMAND_LIVE_VERBOSE")) {
        fprintf(stderr, "%s\n", reply);
    }
    yyjson_doc_free(read);
    free(reply);
    command_fixture_free(&f);
    return correct;
}

int test_editor_command_live(const char *provider, const char *path)
{
    char host[256], model[128], error[256] = { 0 };
    editor_command_model_get_settings(host, sizeof host, model, sizeof model);
    bool previous_schema = editor_command_model_json_schema();
    const char *endpoint = "http://127.0.0.1:11435/api";
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
        fprintf(stderr, "Could not load command cases: %s\n", read_error.msg);
        editor_command_model_set_settings(host, model, error, sizeof error);
        editor_command_model_set_json_schema(previous_schema);
        return 1;
    }
    yyjson_val *cases = yyjson_obj_get(yyjson_doc_get_root(doc), "cases");
    int passed = 0, total = (int)yyjson_arr_size(cases), executed = 0;
    const char *only = getenv("COMMAND_LIVE_CASE");
    const char *limit = getenv("COMMAND_LIVE_LIMIT");
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
