/* The command API against a real scene without a model host: typed
 * operations, readback, command Undo, the quick edit path and UI hooks. */
#include "editor/commands/internal.h"
#include "editor/commands/model.h"
#include "support/xalloc.h"
#include "test/editor/command_fixture.h"
#include "test/suite_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yyjson.h>

typedef struct
{
    char *json;
    yyjson_doc *doc;
    yyjson_val *root;
} reply;

static reply parse(char *json)
{
    reply r = { .json = json };
    r.doc = yyjson_read(json, strlen(json), 0);
    r.root = yyjson_doc_get_root(r.doc);
    return r;
}

static void reply_free(reply *r)
{
    yyjson_doc_free(r->doc);
    free(r->json);
}

static reply api(app_scene_authoring *scene, const char *request)
{
    return parse(editor_commands_api(scene, request));
}

static yyjson_val *named(yyjson_val *catalog, const char *kind, const char *name)
{
    size_t i, count;
    yyjson_val *object;
    yyjson_arr_foreach(yyjson_obj_get(catalog, "objects"), i, count, object)
    {
        if (!strcmp(command_string(object, "kind"), kind) &&
            !strcmp(command_string(object, "name"), name)) {
            return object;
        }
    }
    return NULL;
}

/* The current value of the Player prototype field, as written. */
static char *player_value(app_scene_authoring *scene, const char *field)
{
    reply catalog = api(scene, "{\"operation\":\"inspect\"}");
    yyjson_val *player = named(catalog.root, "prototype", "Player");
    yyjson_val *value = yyjson_obj_get(command_find(yyjson_obj_get(player, "fields"), field), "value");
    char *text = value ? yyjson_val_write(value, 0, NULL) : xstrdup("");
    reply_free(&catalog);
    return text;
}

static bool player_is(app_scene_authoring *scene, const char *field, const char *expected)
{
    char *actual = player_value(scene, field);
    bool same = !strcmp(actual, expected);
    free(actual);
    return same;
}

static const char *status(reply *r)
{
    return command_string(r->root, "status");
}

static void typed_edit_and_undo(app_scene_authoring *scene)
{
    reply catalog = api(scene, "{\"operation\":\"inspect\"}");
    yyjson_val *player = named(catalog.root, "prototype", "Player");
    yyjson_val *shape = command_find(yyjson_obj_get(player, "fields"), "GAME.GameCollider.shape");
    yyjson_val *first = yyjson_arr_get(yyjson_obj_get(shape, "choices"), 0);
    test_check(
        !strcmp(command_string(shape, "type"), "choice") &&
            !strcmp(command_string(shape, "value"), "Box") &&
            !strcmp(command_string(first, "value"), command_string(first, "label")),
        "command catalog spells enum values and choices by label"
    );
    char request[1024];
    snprintf(
        request,
        sizeof request,
        "{\"operation\":\"submit\",\"revision\":\"%s\",\"operations\":[{\"op\":"
        "\"set\","
        "\"target\":\"%s\",\"field\":\"GAME.GameCollider.shape\",\"value\":"
        "\"Circle\"}]}",
        command_string(catalog.root, "revision"),
        command_string(player, "id")
    );
    reply_free(&catalog);
    reply applied = api(scene, request);
    test_check(
        !strcmp(status(&applied), "applied") &&
            yyjson_get_bool(yyjson_obj_get(applied.root, "verified")) &&
            yyjson_get_bool(yyjson_obj_get(applied.root, "undoable")) &&
            player_is(scene, "GAME.GameCollider.shape", "\"Circle\""),
        "typed command sets an enum by label and reads it back"
    );
    snprintf(
        request,
        sizeof request,
        "{\"operation\":\"respond\",\"task_id\":\"%s\",\"undo\":true}",
        command_string(applied.root, "task_id")
    );
    reply_free(&applied);
    reply undone = api(scene, request);
    test_check(
        !strcmp(status(&undone), "undone") &&
            player_is(scene, "GAME.GameCollider.shape", "\"Box\""),
        "command Undo restores the edited prototype"
    );
    reply_free(&undone);
    reply stale = api(scene, "{\"operation\":\"submit\",\"revision\":\"stale\",\"operations\":[{\"op\":\"save\"}]}");
    test_check(!strcmp(status(&stale), "failed"), "typed commands require the observed revision");
    reply_free(&stale);
}

/* Submits typed operations against the current revision; `%s` in the
 * operations becomes the Player prototype ID. */
static reply submit_typed(app_scene_authoring *scene, const char *operations, bool dry)
{
    reply catalog = api(scene, "{\"operation\":\"inspect\"}");
    yyjson_val *player = named(catalog.root, "prototype", "Player");
    char ops[1024], request[2048];
    snprintf(ops, sizeof ops, operations, command_string(player, "id"));
    snprintf(
        request,
        sizeof request,
        "{\"operation\":\"submit\",\"dry_run\":%s,\"revision\":\"%s\","
        "\"operations\":%s}",
        dry ? "true" : "false",
        command_string(catalog.root, "revision"),
        ops
    );
    reply_free(&catalog);
    return api(scene, request);
}

static reply task_request(
    app_scene_authoring *scene,
    const char *operation,
    const char *task,
    const char *extra
)
{
    char request[512];
    snprintf(request, sizeof request, "{\"operation\":\"%s\",\"task_id\":\"%s\"%s}", operation, task, extra);
    return api(scene, request);
}

static void quick_edit(app_scene_authoring *scene)
{
    char host[256], model[128], error[256];
    editor_command_model_get_settings(host, sizeof host, model, sizeof model);
    /* A quick edit decides locally, so the unreachable host is never contacted. */
    editor_command_model_set_settings("http://127.0.0.1:9/api", "gpt-oss:20b", error, sizeof error);
    reply applied = parse(
        command_fixture_settled(scene, command_fixture_submit(scene, "Set Ship's visual width to 80"))
    );
    char first[64];
    snprintf(first, sizeof first, "%s", command_string(applied.root, "task_id"));
    yyjson_val *operation = yyjson_arr_get(yyjson_obj_get(applied.root, "operations"), 0);
    test_check(
        !strcmp(status(&applied), "applied") &&
            !strcmp(command_string(operation, "field"), "GAME.GameAppearance.width") &&
            yyjson_get_int(yyjson_obj_get(applied.root, "model_calls")) == 0 &&
            editor_commands_undoable(scene, first) &&
            player_is(scene, "GAME.GameAppearance.width", "80.0"),
        "an undoable proposal applies at once without the model"
    );
    reply_free(&applied);
    editor_command_model_set_settings(host, model, error, sizeof error);

    reply later = submit_typed(scene, "[{\"op\":\"set\",\"target\":\"%s\",\"field\":\"GAME.GameCollider.width\",\"value\":40}]", false);
    char second[64];
    snprintf(second, sizeof second, "%s", command_string(later.root, "task_id"));
    reply_free(&later);
    reply stale = task_request(scene, "respond", first, ",\"undo\":true");
    test_check(
        !editor_commands_undoable(scene, first) && !strcmp(status(&stale), "failed") &&
            player_is(scene, "GAME.GameAppearance.width", "80.0"),
        "a later edit of the document retires a command's Undo"
    );
    reply_free(&stale);
    reply undone = task_request(scene, "respond", second, ",\"undo\":true");
    test_check(
        !strcmp(status(&undone), "undone") && editor_commands_undoable(scene, first) &&
            player_is(scene, "GAME.GameCollider.width", "32.0"),
        "undoing the later edit makes the earlier command the latest again"
    );
    reply_free(&undone);
}

static bool drawer_open;

static bool panel_visible(void *, int)
{
    return drawer_open;
}

static void show_panel(void *, int, bool visible)
{
    drawer_open = visible;
}

static void history(void *, bool) {}

static void frame(void *) {}

static void panel_hooks(app_scene_authoring *scene)
{
    reply unhooked = api(scene, "{\"operation\":\"submit\",\"operations\":[{\"op\":\"panel\",\"target\":\"panel:0\",\"value\":true}]}");
    test_check(!strcmp(status(&unhooked), "failed"), "panel commands need a host UI");
    reply_free(&unhooked);
    static const char *const panels[] = { "Output" };
    editor_command_hooks hooks = { .panels = panels,
                                   .panel_count = 1,
                                   .panel_visible = panel_visible,
                                   .show_panel = show_panel,
                                   .history = history,
                                   .frame = frame };
    editor_commands_set_hooks(scene, &hooks);
    reply catalog = api(scene, "{\"operation\":\"inspect\"}");
    char request[512];
    snprintf(
        request,
        sizeof request,
        "{\"operation\":\"submit\",\"revision\":\"%s\",\"operations\":[{\"op\":"
        "\"panel\","
        "\"target\":\"panel:0\",\"value\":true}]}",
        command_string(catalog.root, "revision")
    );
    reply_free(&catalog);
    reply shown = api(scene, request);
    test_check(!strcmp(status(&shown), "applied") && drawer_open, "panel commands show host panels");
    reply_free(&shown);
    editor_commands_set_hooks(scene, NULL);
}

static void inspection(app_scene_authoring *scene)
{
    reply types = api(scene, "{\"operation\":\"inspect\",\"kinds\":[\"components\"],\"query\":\"Collider\"}");
    yyjson_val *component = yyjson_arr_get(yyjson_obj_get(types.root, "component_types"), 0);
    test_check(
        component && strstr(command_string(component, "name"), "Collider") &&
            !yyjson_obj_get(types.root, "node_types"),
        "command inspection lists requested reflected types"
    );
    reply_free(&types);
    reply found = api(scene, "{\"operation\":\"resolve_objects\",\"query\":\"make the ship blue\",\"kinds\":[\"entity\"]}");
    yyjson_val *objects = yyjson_obj_get(found.root, "objects");
    test_check(
        !strcmp(status(&found), "resolved") && yyjson_arr_size(objects) == 1 &&
            !strcmp(command_string(yyjson_arr_get(objects, 0), "name"), "Ship"),
        "resolve_objects finds observed objects named in a request"
    );
    reply_free(&found);
}

static void creation(app_scene_authoring *scene)
{
    static const char
        valid_op[] = "[{\"op\":\"create_asset\",\"kind\":\"prototype\","
                     "\"name\":\"Marker\","
                     "\"definition\":{\"components\":{\"GameCollider\":{"
                     "\"shape\":\"Circle\"}}}}]";
    reply valid = submit_typed(scene, valid_op, true);
    test_check(!strcmp(status(&valid), "ready"), "create_asset validates a new prototype in a dry run");
    reply_free(&valid);
    reply invalid = submit_typed(
        scene,
        "[{\"op\":\"create_asset\",\"kind\":\"prototype\",\"name\":\"Marker\","
        "\"definition\":{\"components\":{\"GameCollider\":{\"shape\":"
        "\"Hexagon\"}}}}]",
        true
    );
    test_check(!strcmp(status(&invalid), "failed"), "create_asset rejects an invalid prototype");
    reply_free(&invalid);
    reply confirm = submit_typed(scene, valid_op, false);
    char task[64];
    snprintf(task, sizeof task, "%s", command_string(confirm.root, "task_id"));
    test_check(
        !strcmp(status(&confirm), "needs_confirmation") &&
            yyjson_arr_size(yyjson_obj_get(confirm.root, "summary")) == 1,
        "an irreversible operation waits for confirmation"
    );
    reply_free(&confirm);
    reply cancelled = task_request(scene, "cancel", task, "");
    test_check(!strcmp(status(&cancelled), "cancelled"), "an unconfirmed operation can be cancelled");
    reply_free(&cancelled);
    static const char *const escapes[] = { "../shot.png",
                                           "/tmp/shot.png",
                                           "shots/shot.png",
                                           "shot.txt" };
    for (int i = 0; i < 4; i++) {
        char op[256];
        snprintf(op, sizeof op, "[{\"op\":\"capture\",\"path\":\"%s\"}]", escapes[i]);
        reply rejected = submit_typed(scene, op, false);
        test_check(!strcmp(status(&rejected), "failed"), "captures stay in the project .chibi directory");
        reply_free(&rejected);
    }
}

void test_editor_command_api()
{
    command_fixture f = command_fixture_new();
    typed_edit_and_undo(f.scene);
    quick_edit(f.scene);
    panel_hooks(f.scene);
    inspection(f.scene);
    creation(f.scene);
    command_fixture_free(&f);
}
