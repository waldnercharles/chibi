#include "editor/commands/internal.h"
#include "editor/commands/model.h"
#include "editor/ui/command_transport.h"
#include "editor/ui/ui_shell.h"
#include "editor/workspace/editor_internal.h"
#include "support/xalloc.h"

#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef __EMSCRIPTEN__
#include <sys/stat.h>
#else
#include <emscripten.h>
EM_JS(void, web_trace, (const char *json), {
    Module.chibiEditorTrace ?.(JSON.parse(UTF8ToString(json)));
})
#endif

typedef struct
{
    char *id, *input, *catalog, *result;
    yyjson_mut_doc *packet;
    editor_command_request *request;
    Uint64 started, elapsed;
    int steps, reobservations;
    bool dry, measuring, laya, local;
} command_task;

struct editor_command_runtime
{
    command_task **tasks;
    command_task *active, *latest;
    char *context;
    unsigned serial;
};

static struct editor_command_runtime *runtime(app_scene_authoring *scene)
{
    if (!scene->command_runtime) {
        scene->command_runtime = xcalloc(1, sizeof *scene->command_runtime);
    }
    return scene->command_runtime;
}

static char *response(const char *status, const char *message)
{
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_bool(doc, root, "ok", strcmp(status, "failed") != 0);
    yyjson_mut_obj_add_str(doc, root, "status", status);
    yyjson_mut_obj_add_str(doc, root, "message", message);
    char *json = yyjson_mut_write(doc, 0, NULL);
    yyjson_mut_doc_free(doc);
    return json;
}

static void describe(command_task *task, yyjson_mut_doc *doc, yyjson_val *proposal)
{
    yyjson_mut_val *root = yyjson_mut_doc_get_root(doc);
    if (yyjson_mut_obj_get(root, "summary")) {
        return;
    }
    yyjson_mut_val *summary = yyjson_mut_obj_add_arr(doc, root, "summary");
    yyjson_doc *catalog = yyjson_read(task->catalog, strlen(task->catalog), 0);
    yyjson_val *objects = yyjson_obj_get(yyjson_doc_get_root(catalog), "objects");
    yyjson_val *ops = yyjson_obj_get(proposal, "operations");
    for (int i = 0; i < (int)yyjson_arr_size(ops); i++) {
        yyjson_val *op = yyjson_arr_get(ops, i);
        yyjson_val *object = command_find(objects, command_string(op, "target"));
        yyjson_val *field = command_find(yyjson_obj_get(object, "fields"), command_string(op, "field"));
        yyjson_val *value = yyjson_obj_get(op, "value");
        char *literal = value ? yyjson_val_write(value, 0, NULL) : NULL;
        char color[16];
        if (!strcmp(command_string(field, "type"), "color")) {
            snprintf(color, sizeof color, "#%08x", (unsigned)yyjson_get_uint(value));
            free(literal);
            literal = xstrdup(color);
        }
        char line[1024];
        snprintf(
            line,
            sizeof line,
            "%s %s%s%s%s%s",
            command_string(op, "op"),
            command_string(object, "name"),
            field ? " · " : "",
            command_string(field, "label"),
            literal ? " → " : "",
            literal ? literal : ""
        );
        yyjson_mut_arr_add_strcpy(doc, summary, line);
        free(literal);
    }
    yyjson_doc_free(catalog);
}

static void set_result(command_task *task, char *json)
{
    yyjson_doc *read = yyjson_read(json, strlen(json), 0);
    yyjson_mut_doc *doc = read ? yyjson_doc_mut_copy(read, NULL)
                               : yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = yyjson_mut_doc_get_root(doc);
    if (!root) {
        root = yyjson_mut_obj(doc);
        yyjson_mut_doc_set_root(doc, root);
    }
    if (!yyjson_mut_obj_get(root, "status")) {
        yyjson_mut_obj_add_str(doc, root, "status", "failed");
        const char *error = command_string(yyjson_doc_get_root(read), "error");
        yyjson_mut_obj_add_strcpy(doc, root, "message", *error ? error : "Invalid model response");
    }
    describe(task, doc, yyjson_doc_get_root(read));
    yyjson_mut_obj_remove_key(root, "task_id");
    yyjson_mut_obj_add_str(doc, root, "task_id", task->id);
    yyjson_mut_obj_remove_key(root, "elapsed_ms");
    Uint64 now = SDL_GetTicks();
    if (task->measuring) {
        task->elapsed += now - task->started;
    }
    task->started = now;
    task->measuring = !strcmp(command_string(yyjson_doc_get_root(read), "status"), "pending");
    yyjson_mut_obj_add_uint(doc, root, "elapsed_ms", task->elapsed);
    free(task->result);
    task->result = yyjson_mut_write(doc, 0, NULL);
    yyjson_mut_doc_free(doc);
    yyjson_doc_free(read);
    free(json);
}

static void trace(command_task *task)
{
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_str(doc, root, "request", task->input);
    yyjson_mut_obj_add_str(doc, root, "catalog", task->catalog);
    yyjson_mut_obj_add_str(doc, root, "result", task->result);
    char *json = yyjson_mut_write(doc, 0, NULL);
#ifdef __EMSCRIPTEN__
    web_trace(json);
#else
    const char *path = getenv("CHIBI_COMMAND_TRACE");
    if (!path || !*path) {
        mkdir(".chibi", 0700);
        path = ".chibi/command-traces.jsonl";
    }
    FILE *file = fopen(path, "a");
    if (file) {
        chmod(path, 0600);
        fprintf(file, "%s\n", json);
        fclose(file);
    }
#endif
    free(json);
    yyjson_mut_doc_free(doc);
}

static bool same_value(yyjson_val *actual, yyjson_val *expected, yyjson_val *field)
{
    if (!yyjson_is_num(actual) || !yyjson_is_num(expected)) {
        return yyjson_equals(actual, expected);
    }
    double a = yyjson_get_num(actual), b = yyjson_get_num(expected);
    if (yyjson_get_bool(yyjson_obj_get(field, "integer")) ||
        (!yyjson_is_real(actual) && !yyjson_is_real(expected))) {
        return a == b;
    }
    return fabs(a - b) <= 1e-5 * fmax(1, fabs(b));
}

static bool verify_sets(yyjson_val *operations, yyjson_val *catalog)
{
    yyjson_val *objects = yyjson_obj_get(catalog, "objects");
    if (!yyjson_arr_size(operations)) {
        return false;
    }
    for (size_t i = 0; i < yyjson_arr_size(operations); i++) {
        yyjson_val *operation = yyjson_arr_get(operations, i);
        if (strcmp(command_string(operation, "op"), "set")) {
            return false;
        }
        yyjson_val *object = command_find(objects, command_string(operation, "target"));
        yyjson_val *field = command_find(
            yyjson_obj_get(object, "fields"),
            command_string(operation, "field")
        );
        if (!field ||
            !same_value(yyjson_obj_get(field, "value"), yyjson_obj_get(operation, "value"), field)) {
            return false;
        }
    }
    return true;
}

static void execute(app_scene_authoring *scene, command_task *task)
{
    task->started = SDL_GetTicks();
    task->measuring = true;
    char error[256] = { 0 };
    yyjson_doc *read = yyjson_read(task->result, strlen(task->result), 0);
    bool laya_proposal = task->laya &&
                         !strcmp(command_string(yyjson_doc_get_root(read), "provider"), "laya");
    yyjson_mut_doc *doc = yyjson_doc_mut_copy(read, NULL);
    yyjson_mut_val *root = yyjson_mut_doc_get_root(doc);
    yyjson_mut_obj_remove_key(root, "dry_run");
    yyjson_mut_obj_add_bool(doc, root, "dry_run", task->dry);
    char *proposal = yyjson_mut_write(doc, 0, NULL);
    char *history_before = scene_history_checkpoint(scene->history);
    int graph_undo = acount(scene->graph_editor->undo), prototype_undo = 0;
    for (int i = 0; i < acount(scene->prototypes); i++) {
        prototype_undo += acount(scene->prototypes[i].undo);
    }
    bool applied = !editor_commands_apply(scene, task->catalog, proposal, error, sizeof error);
    char *history_after = scene_history_checkpoint(scene->history);
    int prototype_after = 0;
    for (int i = 0; i < acount(scene->prototypes); i++) {
        prototype_after += acount(scene->prototypes[i].undo);
    }
    bool changed = strcmp(history_before, history_after) ||
                   graph_undo != acount(scene->graph_editor->undo) ||
                   prototype_undo != prototype_after;
    free(history_before);
    free(history_after);
    yyjson_val *ops = yyjson_obj_get(yyjson_doc_get_root(read), "operations");
    const char *first = command_string(yyjson_arr_get(ops, 0), "op");
    bool content = !strcmp(first, "set") || !strcmp(first, "assert") ||
                   !strcmp(first, "duplicate") || !strcmp(first, "connect");
    yyjson_mut_obj_add_bool(doc, root, "undoable", applied && !task->dry && content && changed);
    char *after = editor_commands_catalog(scene);
    yyjson_doc *after_doc = yyjson_read(after, strlen(after), 0);
    bool verified = applied && !task->dry && laya_proposal &&
                    verify_sets(ops, yyjson_doc_get_root(after_doc));
    if (applied && !task->dry && laya_proposal) {
        yyjson_mut_obj_add_bool(doc, root, "verified", verified);
    }
    yyjson_mut_obj_add_strcpy(
        doc,
        root,
        "after_revision",
        command_string(yyjson_doc_get_root(after_doc), "revision")
    );
    yyjson_mut_val *context = yyjson_mut_obj_get(root, "context");
    if (context) {
        yyjson_mut_obj_remove_key(context, "document");
        yyjson_mut_obj_add_strcpy(
            doc,
            context,
            "document",
            command_string(yyjson_doc_get_root(after_doc), "document")
        );
        yyjson_val *last = yyjson_arr_get(ops, yyjson_arr_size(ops) - 1);
        if (*command_string(last, "result_target")) {
            yyjson_mut_obj_remove_key(context, "targets");
            yyjson_mut_val *targets = yyjson_mut_obj_add_arr(doc, context, "targets");
            yyjson_val *objects = yyjson_obj_get(yyjson_doc_get_root(after_doc), "objects");
            for (int i = 0; i < (int)yyjson_arr_size(objects); i++) {
                yyjson_val *object = yyjson_arr_get(objects, i);
                if (!strcmp(command_string(object, "kind"), "entity") &&
                    yyjson_get_bool(yyjson_obj_get(object, "selected"))) {
                    yyjson_mut_arr_add_strcpy(doc, targets, command_string(object, "id"));
                }
            }
        }
    }
    yyjson_doc_free(after_doc);
    free(after);
    free(proposal);
    yyjson_mut_obj_remove_key(root, "status");
    yyjson_mut_obj_remove_key(root, "message");
    yyjson_mut_obj_remove_key(root, "ok");
    yyjson_mut_obj_add_bool(doc, root, "ok", applied && (!laya_proposal || task->dry || verified));
    yyjson_mut_obj_add_str(
        doc,
        root,
        "status",
        !applied                     ? "failed"
        : task->dry                  ? "ready"
        : laya_proposal && !verified ? "unverified"
                                     : "applied"
    );
    yyjson_mut_obj_add_str(
        doc,
        root,
        "message",
        !applied    ? error
        : task->dry ? "Validated"
        : laya_proposal && !verified
            ? "Applied, but readback did not match. Review or undo the change."
            : ""
    );
    set_result(task, yyjson_mut_write(doc, 0, NULL));
    if (applied && !task->dry && (!laya_proposal || verified)) {
        free(runtime(scene)->context);
        runtime(scene)->context = context ? yyjson_mut_val_write(context, 0, NULL)
                                          : NULL;
    }
    yyjson_mut_doc_free(doc);
    yyjson_doc_free(read);
    runtime(scene)->active = NULL;
    trace(task);
}

static bool start(command_task *task)
{
    if (++task->steps > 12) {
        set_result(task, response("failed", "Command decision limit reached"));
        return false;
    }
    task->started = SDL_GetTicks();
    task->measuring = true;
    char error[256] = { 0 };
    char *packet = yyjson_mut_write(task->packet, 0, NULL);
    char *model_request = editor_command_model_prepare(packet, error, sizeof error);
    free(packet);
    yyjson_doc *request_doc = model_request
                                ? yyjson_read(model_request, strlen(model_request), 0)
                                : NULL;
    task->local = yyjson_obj_get(yyjson_doc_get_root(request_doc), "local_response") !=
                  NULL;
    yyjson_doc_free(request_doc);
    task->request = model_request ? editor_command_request_start(model_request) : NULL;
    free(model_request);
    set_result(
        task,
        response(
            task->request ? "pending" : "failed",
            task->request ? "Contacting model…"
            : error[0]    ? error
                          : "Could not start model request"
        )
    );
    return task->request != NULL;
}

static void set_catalog(command_task *task, char *catalog)
{
    yyjson_doc *read = yyjson_read(catalog, strlen(catalog), 0);
    yyjson_mut_val *packet = yyjson_mut_doc_get_root(task->packet);
    yyjson_mut_obj_remove_key(packet, "catalog");
    yyjson_mut_obj_add_val(
        task->packet,
        packet,
        "catalog",
        yyjson_val_mut_copy(task->packet, yyjson_doc_get_root(read))
    );
    free(task->catalog);
    task->catalog = catalog;
    yyjson_doc_free(read);
}

static bool reobserve(app_scene_authoring *scene, command_task *task)
{
    if (!task->laya) {
        return false;
    }
    char *catalog = editor_commands_catalog(scene);
    yyjson_doc *before = yyjson_read(task->catalog, strlen(task->catalog), 0);
    yyjson_doc *after = yyjson_read(catalog, strlen(catalog), 0);
    bool stale = strcmp(
                     command_string(yyjson_doc_get_root(before), "revision"),
                     command_string(yyjson_doc_get_root(after), "revision")
                 ) != 0;
    yyjson_doc_free(before);
    yyjson_doc_free(after);
    if (!stale) {
        free(catalog);
        return false;
    }
    set_catalog(task, catalog);
    yyjson_mut_obj_remove_key(yyjson_mut_doc_get_root(task->packet), "bindings");
    if (++task->reobservations > 2) {
        set_result(task, response("failed", "Editor kept changing during this command"));
        runtime(scene)->active = NULL;
        trace(task);
    } else if (!start(task)) {
        runtime(scene)->active = NULL;
        trace(task);
    }
    return true;
}

static void bind(command_task *task, const char *key, const char *value)
{
    yyjson_mut_val *packet = yyjson_mut_doc_get_root(task->packet);
    yyjson_mut_val *bindings = yyjson_mut_obj_get(packet, "bindings");
    if (!bindings) {
        bindings = yyjson_mut_obj_add_obj(task->packet, packet, "bindings");
    }
    yyjson_mut_obj_remove_key(bindings, key);
    yyjson_mut_obj_add(
        bindings,
        yyjson_mut_strcpy(task->packet, key),
        yyjson_mut_strcpy(task->packet, value)
    );
}

const char *editor_commands_result(app_scene_authoring *scene)
{
    command_task *task = runtime(scene)->latest;
    if (task && task->request) {
        set_result(task, xstrdup(task->result));
    }
    return task ? task->result : "{\"status\":\"idle\"}";
}

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
EM_JS(char *, editor_web_next, (), {
    const message = Module.chibiEditorNext ?.();
    return message ? stringToNewUTF8(JSON.stringify(message)) : 0;
})
EM_JS(void, editor_web_reply, (const char *id, const char *result), {
    Module.chibiEditorReply ?.(UTF8ToString(id), JSON.parse(UTF8ToString(result)));
})
#endif

void editor_commands_frame(app_scene_authoring *scene)
{
#ifdef __EMSCRIPTEN__
    char *message = editor_web_next();
    if (message) {
        yyjson_doc *doc = yyjson_read(message, strlen(message), 0);
        yyjson_val *root = yyjson_doc_get_root(doc);
        char *packet = yyjson_val_write(yyjson_obj_get(root, "packet"), 0, NULL);
        char *result = editor_commands_api(scene, packet);
        editor_web_reply(command_string(root, "id"), result);
        free(result);
        free(packet);
        free(message);
        yyjson_doc_free(doc);
    }
#endif
    command_task *task = runtime(scene)->active;
    if (!task || !task->request) {
        return;
    }
    char *raw = editor_command_request_poll(task->request);
    if (!raw) {
        return;
    }
    editor_command_request_free(task->request);
    task->request = NULL;
    bool local = task->local;
    if (!local && reobserve(scene, task)) {
        free(raw);
        return;
    }
    char error[256] = { 0 };
    char *packet = yyjson_mut_write(task->packet, 0, NULL);
    char *result = editor_command_model_result(packet, raw, error, sizeof error);
    free(packet);
    free(raw);
    if (!result) {
        result = response("failed", error[0] ? error : "Could not parse the model command");
    }
    if (local) {
        yyjson_doc *result_doc = yyjson_read(result, strlen(result), 0);
        bool complete = strcmp(command_string(yyjson_doc_get_root(result_doc), "status"), "continue") !=
                        0;
        yyjson_doc_free(result_doc);
        if (complete && reobserve(scene, task)) {
            free(result);
            return;
        }
    }
    set_result(task, result);
    yyjson_doc *doc = yyjson_read(task->result, strlen(task->result), 0);
    yyjson_val *reply = yyjson_doc_get_root(doc);
    const char *status = command_string(reply, "status");
    if (!strcmp(status, "continue")) {
        const char *key = command_string(reply, "binding");
        const char *value = command_string(reply, "value");
        if (!*key || !*value) {
            set_result(task, response("failed", "Model returned an incomplete decision"));
            runtime(scene)->active = NULL;
            trace(task);
        } else {
            bind(task, key, value);
            if (!start(task)) {
                runtime(scene)->active = NULL;
                trace(task);
            }
        }
    } else if (!strcmp(status, "ready")) {
        execute(scene, task);
    } else if (strcmp(status, "needs_review") && strcmp(status, "needs_clarification")) {
        runtime(scene)->active = NULL;
        trace(task);
    } else {
        trace(task);
    }
    yyjson_doc_free(doc);
}

static command_task *find_task(struct editor_command_runtime *state, const char *id)
{
    if (!*id) {
        return state->latest;
    }
    for (int i = 0; i < acount(state->tasks); i++) {
        if (!strcmp(state->tasks[i]->id, id)) {
            return state->tasks[i];
        }
    }
    return NULL;
}

char *editor_commands_api(app_scene_authoring *scene, const char *request)
{
    yyjson_doc *doc = yyjson_read(request, strlen(request), 0);
    yyjson_val *root = yyjson_doc_get_root(doc);
    const char *operation = command_string(root, "operation");
    struct editor_command_runtime *state = runtime(scene);
    char *result = NULL;
    command_task *task = find_task(state, command_string(root, "task_id"));
    if (!strcmp(operation, "inspect")) {
        result = editor_commands_catalog(scene);
    } else if (!strcmp(operation, "status")) {
        result = task ? xstrdup(task->result) : response("failed", "Unknown task");
    } else if (!strcmp(operation, "cancel")) {
        if (task && state->active == task) {
            editor_command_request_free(task->request);
            task->request = NULL;
            set_result(task, response("cancelled", "Cancelled"));
            state->active = NULL;
            trace(task);
        }
        result = task ? xstrdup(task->result) : response("failed", "Unknown task");
    } else if (!strcmp(operation, "respond")) {
        if (task && yyjson_get_bool(yyjson_obj_get(root, "undo"))) {
            yyjson_doc *prior = yyjson_read(task->result, strlen(task->result), 0);
            yyjson_val *previous = yyjson_doc_get_root(prior);
            char *catalog = editor_commands_catalog(scene);
            yyjson_doc *current = yyjson_read(catalog, strlen(catalog), 0);
            bool same = !strcmp(
                command_string(previous, "after_revision"),
                command_string(yyjson_doc_get_root(current), "revision")
            );
            const char *status = command_string(previous, "status");
            if (state->active ||
                (strcmp(status, "applied") && strcmp(status, "unverified")) ||
                !yyjson_get_bool(yyjson_obj_get(previous, "undoable")) || !same) {
                result = response("failed", "The editor changed. Use document Undo.");
            } else if (editor_ui_run_command(EDITOR_UI_COMMAND_UNDO)) {
                result = response("failed", "Undo failed");
            } else {
                set_result(task, response("undone", "Undone"));
                free(state->context);
                state->context = NULL;
                trace(task);
                result = xstrdup(task->result);
            }
            yyjson_doc_free(prior);
            yyjson_doc_free(current);
            free(catalog);
        } else if (task && !strcmp(command_string(root, "feedback"), "incorrect")) {
            yyjson_doc *prior = yyjson_read(task->result, strlen(task->result), 0);
            yyjson_mut_doc *record = yyjson_doc_mut_copy(prior, NULL);
            yyjson_mut_val *entry = yyjson_mut_doc_get_root(record);
            yyjson_mut_obj_remove_key(entry, "feedback");
            yyjson_mut_obj_add_str(record, entry, "feedback", "incorrect");
            set_result(task, yyjson_mut_write(record, 0, NULL));
            trace(task);
            yyjson_mut_doc_free(record);
            yyjson_doc_free(prior);
            result = xstrdup(task->result);
        } else if (!task || state->active != task || task->request) {
            result = response("failed", "Task is not waiting for a reply");
        } else {
            yyjson_doc *prior = yyjson_read(task->result, strlen(task->result), 0);
            yyjson_val *reply = yyjson_doc_get_root(prior);
            const char *status = command_string(reply, "status");
            if (!strcmp(status, "needs_review") &&
                yyjson_get_bool(yyjson_obj_get(root, "approve")) &&
                yyjson_arr_size(yyjson_obj_get(reply, "operations"))) {
                execute(scene, task);
            } else if (!strcmp(status, "needs_clarification")) {
                const char *value = command_string(root, "value"),
                           *binding = command_string(reply, "binding");
                yyjson_val *choices = yyjson_obj_get(reply, "choices");
                if (!*binding ||
                    (yyjson_arr_size(choices) && !command_find(choices, value))) {
                    result = response("failed", "Choose a listed answer");
                } else {
                    bind(task, binding, value);
                    if (!start(task)) {
                        state->active = NULL;
                        trace(task);
                    }
                }
            } else {
                result = response("failed", "Review the proposed operations");
            }
            yyjson_doc_free(prior);
            if (!result) {
                result = xstrdup(task->result);
            }
        }
    } else if (!strcmp(operation, "submit")) {
        const char *id = command_string(root, "request_id");
        command_task *existing = *id ? find_task(state, id) : NULL;
        if (existing) {
            result = !strcmp(existing->input, request)
                       ? xstrdup(existing->result)
                       : response("failed", "Request ID already used with different input");
        } else if (state->active) {
            result = response("busy", "Another command is active");
        } else if (acount(state->tasks) >= 1024) {
            result = response("failed", "Session task limit reached");
        } else {
            task = xcalloc(1, sizeof *task);
            char generated[48];
            snprintf(generated, sizeof generated, "command-%u", ++state->serial);
            task->id = xstrdup(*id ? id : generated);
            task->input = xstrdup(request);
            task->started = SDL_GetTicks();
            task->dry = yyjson_get_bool(yyjson_obj_get(root, "dry_run"));
            task->packet = yyjson_doc_mut_copy(doc, NULL);
            char model[128];
            editor_command_model_get_settings(NULL, 0, model, sizeof model);
            task->laya = !strcmp(model, "laya");
            set_catalog(task, editor_commands_catalog(scene));
            yyjson_mut_val *packet = yyjson_mut_doc_get_root(task->packet);
            if (state->context && !yyjson_obj_get(root, "context")) {
                yyjson_doc *context = yyjson_read(state->context, strlen(state->context), 0);
                yyjson_mut_obj_add_val(
                    task->packet,
                    packet,
                    "context",
                    yyjson_val_mut_copy(task->packet, yyjson_doc_get_root(context))
                );
                yyjson_doc_free(context);
            }
            apush(state->tasks, task);
            state->active = state->latest = task;
            if (yyjson_obj_get(root, "operations")) {
                set_result(task, xstrdup(request));
                execute(scene, task);
            } else {
                if (!start(task)) {
                    state->active = NULL;
                    trace(task);
                }
            }
            result = xstrdup(task->result);
        }
    } else {
        result = response("failed", "Unknown command API operation");
    }
    yyjson_doc_free(doc);
    return result;
}

void editor_commands_free(app_scene_authoring *scene)
{
    struct editor_command_runtime *state = scene->command_runtime;
    if (!state) {
        return;
    }
    for (int i = 0; i < acount(state->tasks); i++) {
        command_task *task = state->tasks[i];
        editor_command_request_free(task->request);
        yyjson_mut_doc_free(task->packet);
        free(task->id);
        free(task->input);
        free(task->catalog);
        free(task->result);
        free(task);
    }
    afree(state->tasks);
    free(state->context);
    free(state);
    scene->command_runtime = NULL;
}
