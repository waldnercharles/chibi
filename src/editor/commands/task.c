#include "editor/commands/internal.h"
#include "editor/commands/model.h"
#include "editor/ui/command_transport.h"
#include "editor/workspace/editor_internal.h"
#include "runtime/generated.h"
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
    int model_calls;
    bool dry, measuring;
} command_task;

struct editor_command_runtime
{
    command_task **tasks;
    command_task *active, *latest;
    char *context;
    unsigned serial;
    uint64_t incarnation; /* distinguishes editor sessions in revisions */
    editor_command_hooks hooks;
    bool hooked;
    int control_frames[GAME_CONTROL_COUNT]; /* input samples each control stays held */
};

static struct editor_command_runtime *runtime(app_scene_authoring *scene)
{
    if (!scene->command_runtime) {
        scene->command_runtime = xcalloc(1, sizeof *scene->command_runtime);
        scene->command_runtime->incarnation = SDL_GetPerformanceCounter();
    }
    return scene->command_runtime;
}

const editor_command_hooks *command_hooks(app_scene_authoring *scene)
{
    return runtime(scene)->hooked ? &runtime(scene)->hooks : NULL;
}

uint64_t command_incarnation(app_scene_authoring *scene)
{
    return runtime(scene)->incarnation;
}

void editor_commands_set_hooks(app_scene_authoring *scene, const editor_command_hooks *hooks)
{
    struct editor_command_runtime *state = runtime(scene);
    state->hooked = hooks != NULL;
    state->hooks = hooks ? *hooks : (editor_command_hooks){ 0 };
}

uint64_t editor_commands_controls(app_scene_authoring *scene)
{
    struct editor_command_runtime *state = runtime(scene);
    if (!scene->runtime_playing) {
        memset(state->control_frames, 0, sizeof state->control_frames);
        return 0;
    }
    uint64_t controls = 0;
    for (int i = 1; i < GAME_CONTROL_COUNT; i++) {
        if (state->control_frames[i] > 0) {
            controls |= UINT64_C(1) << i;
            state->control_frames[i]--;
        }
    }
    return controls;
}

/* The controls enabled scene bindings map to the action. */
static uint64_t action_controls(app_scene_authoring *scene, const char *action)
{
    uint64_t controls = 0;
    for (int i = 0; i < acount(scene->actors); i++) {
        const GameActionBinding *binding = GAME_GameActionBinding_get_const(
            scene->world,
            scene->actors[i].entity
        );
        if (binding && !binding->disabled && binding->action &&
            !strcmp(binding->action, action) && binding->source > GAME_CONTROL_NONE &&
            binding->source < GAME_CONTROL_COUNT) {
            controls |= UINT64_C(1) << binding->source;
        }
    }
    return controls;
}

int command_input(app_scene_authoring *scene, yyjson_val *operation, bool dry, char *error, int size)
{
    const char *action = command_string(operation, "action");
    yyjson_val *pressed = yyjson_obj_get(operation, "pressed");
    yyjson_val *duration = yyjson_obj_get(operation, "frames");
    int frames = duration ? (int)yyjson_get_int(duration) : 1;
    if (!scene->runtime_playing || scene->active_runtime_target != APP_RUNTIME_SCENE ||
        !*action || !yyjson_is_bool(pressed) || frames < 1 || frames > 600 ||
        (duration && !yyjson_is_int(duration))) {
        snprintf(error, size, "Input requires a playing scene, action, pressed boolean and optional frames (1-600)");
        return -1;
    }
    uint64_t controls = action_controls(scene, action);
    if (!controls) {
        snprintf(error, size, "No enabled scene action binding for %s", action);
        return -1;
    }
    for (int i = 1; !dry && i < GAME_CONTROL_COUNT; i++) {
        if (controls & (UINT64_C(1) << i)) {
            runtime(scene)->control_frames[i] = yyjson_get_bool(pressed) ? frames : 0;
        }
    }
    return 0;
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
            object ? command_string(object, "name") : command_string(op, "name"),
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
    yyjson_mut_obj_remove_key(root, "model_calls");
    Uint64 now = SDL_GetTicks();
    if (task->measuring) {
        task->elapsed += now - task->started;
    }
    task->started = now;
    task->measuring = !strcmp(command_string(yyjson_doc_get_root(read), "status"), "pending");
    yyjson_mut_obj_add_uint(doc, root, "elapsed_ms", task->elapsed);
    yyjson_mut_obj_add_int(doc, root, "model_calls", task->model_calls);
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
    bool check_readback = yyjson_arr_size(ops) > 0;
    for (size_t i = 0; i < yyjson_arr_size(ops); i++) {
        check_readback &= !strcmp(command_string(yyjson_arr_get(ops, i), "op"), "set");
    }
    bool content = !strcmp(first, "set") || !strcmp(first, "assert") ||
                   !strcmp(first, "duplicate") || !strcmp(first, "connect") ||
                   !strcmp(first, "edit_graph");
    yyjson_mut_obj_add_bool(doc, root, "undoable", applied && !task->dry && content && changed);
    char *after = editor_commands_catalog(scene);
    yyjson_doc *after_doc = yyjson_read(after, strlen(after), 0);
    bool verified = applied && !task->dry && check_readback &&
                    verify_sets(ops, yyjson_doc_get_root(after_doc));
    if (applied && !task->dry && check_readback) {
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
    yyjson_mut_obj_add_bool(doc, root, "ok", applied && (!check_readback || task->dry || verified));
    yyjson_mut_obj_add_str(
        doc,
        root,
        "status",
        !applied                      ? "failed"
        : task->dry                   ? "ready"
        : check_readback && !verified ? "unverified"
                                      : "applied"
    );
    yyjson_mut_obj_add_str(
        doc,
        root,
        "message",
        !applied    ? error
        : task->dry ? "Validated"
        : check_readback && !verified
            ? "Applied, but readback did not match. Review or undo the change."
            : ""
    );
    set_result(task, yyjson_mut_write(doc, 0, NULL));
    if (applied && !task->dry && (!check_readback || verified)) {
        free(runtime(scene)->context);
        runtime(scene)->context = context ? yyjson_mut_val_write(context, 0, NULL)
                                          : NULL;
    }
    yyjson_mut_doc_free(doc);
    yyjson_doc_free(read);
    runtime(scene)->active = NULL;
    trace(task);
}

static void start(command_task *task)
{
    task->started = SDL_GetTicks();
    task->measuring = true;
    char error[256];
    char *packet = yyjson_mut_write(task->packet, 0, NULL);
    char *model_request = editor_command_model_prepare(packet, error, sizeof error);
    free(packet);
    yyjson_doc *request_doc = model_request
                                ? yyjson_read(model_request, strlen(model_request), 0)
                                : NULL;
    bool local = yyjson_obj_get(yyjson_doc_get_root(request_doc), "local_response") !=
                 NULL;
    yyjson_doc_free(request_doc);
    task->request = model_request ? editor_command_request_start(model_request) : NULL;
    if (task->request && !local) {
        task->model_calls++;
    }
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
}

static bool validate_review(app_scene_authoring *scene, command_task *task)
{
    yyjson_doc *read = yyjson_read(task->result, strlen(task->result), 0);
    const char *status = command_string(yyjson_doc_get_root(read), "status");
    if (strcmp(status, "needs_review") && strcmp(status, "ready")) {
        yyjson_doc_free(read);
        return true;
    }
    yyjson_mut_doc *doc = yyjson_doc_mut_copy(read, NULL);
    yyjson_mut_val *root = yyjson_mut_doc_get_root(doc);
    yyjson_mut_obj_remove_key(root, "dry_run");
    yyjson_mut_obj_add_bool(doc, root, "dry_run", true);
    char *proposal = yyjson_mut_write(doc, 0, NULL);
    char error[256] = { 0 };
    bool valid = !editor_commands_apply(scene, task->catalog, proposal, error, sizeof error);
    free(proposal);
    yyjson_mut_doc_free(doc);
    yyjson_doc_free(read);
    if (!valid) {
        if (getenv("CHIBI_COMMAND_DEBUG_PROPOSAL")) {
            fprintf(stderr, "Rejected command proposal: %s\n", task->result);
        }
        set_result(
            task,
            response(
                !strcmp(error, "Changes span multiple documents") ||
                        !strcmp(error, "Choose a listed property value")
                    ? "unsupported"
                    : "failed",
                error
            )
        );
    }
    return valid;
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
    char error[256] = { 0 };
    char *packet = yyjson_mut_write(task->packet, 0, NULL);
    char *result = editor_command_model_result(packet, raw, error, sizeof error);
    free(packet);
    free(raw);
    if (!result) {
        result = response("failed", error[0] ? error : "Could not parse the model command");
    }
    set_result(task, result);
    if (!validate_review(scene, task)) {
        runtime(scene)->active = NULL;
        trace(task);
        return;
    }
    yyjson_doc *doc = yyjson_read(task->result, strlen(task->result), 0);
    const char *status = command_string(yyjson_doc_get_root(doc), "status");
    if (!strcmp(status, "ready")) {
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

/* The document a content command edited: node (the open graph), prototype
 * or entity (the scene). */
static const char *edited_kind(command_task *task, yyjson_val *result)
{
    yyjson_doc *catalog = yyjson_read(task->catalog, strlen(task->catalog), 0);
    yyjson_val *objects = yyjson_obj_get(yyjson_doc_get_root(catalog), "objects");
    yyjson_val *first = yyjson_arr_get(yyjson_obj_get(result, "operations"), 0);
    const char *kind = command_string(command_find(objects, command_string(first, "target")), "kind");
    bool graph = !strcmp(kind, "node") ||
                 !strcmp(command_string(first, "op"), "edit_graph");
    const char *known = graph                      ? "node"
                      : !strcmp(kind, "prototype") ? "prototype"
                                                   : "entity";
    yyjson_doc_free(catalog);
    return known;
}

/* Undoes the document the command changed, as that document's Undo would. */
static int undo_document(app_scene_authoring *scene, const char *kind)
{
    if (!strcmp(kind, "node")) {
        if (!editor_can_undo(scene->graph_editor)) {
            return -1;
        }
        editor_undo(scene->graph_editor);
        return 0;
    }
    if (!strcmp(kind, "prototype")) {
        return prototype_undo(scene);
    }
    finish_transform_edit(scene, false);
    return replay_scene_edit(scene, false);
}

/* Undo is offered only while the command's change is still the latest. */
static char *undo_task(app_scene_authoring *scene, command_task *task)
{
    struct editor_command_runtime *state = runtime(scene);
    yyjson_doc *prior = yyjson_read(task->result, strlen(task->result), 0);
    yyjson_val *previous = yyjson_doc_get_root(prior);
    char *catalog = editor_commands_catalog(scene);
    yyjson_doc *current = yyjson_read(catalog, strlen(catalog), 0);
    bool latest = !state->active &&
                  !strcmp(command_string(previous, "status"), "applied") &&
                  yyjson_get_bool(yyjson_obj_get(previous, "undoable")) &&
                  !strcmp(
                      command_string(previous, "after_revision"),
                      command_string(yyjson_doc_get_root(current), "revision")
                  );
    char *result = NULL;
    if (!latest) {
        result = response("failed", "The editor changed. Use document Undo.");
    } else if (undo_document(scene, edited_kind(task, previous))) {
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
    return result;
}

static char *mark_incorrect(command_task *task)
{
    yyjson_doc *prior = yyjson_read(task->result, strlen(task->result), 0);
    yyjson_mut_doc *record = yyjson_doc_mut_copy(prior, NULL);
    yyjson_mut_val *entry = yyjson_mut_doc_get_root(record);
    yyjson_mut_obj_remove_key(entry, "feedback");
    yyjson_mut_obj_add_str(record, entry, "feedback", "incorrect");
    set_result(task, yyjson_mut_write(record, 0, NULL));
    trace(task);
    yyjson_mut_doc_free(record);
    yyjson_doc_free(prior);
    return xstrdup(task->result);
}

/* Binds the answer to a clarification and asks the model again. NULL on success. */
static char *clarify(command_task *task, yyjson_val *reply, yyjson_val *root)
{
    const char *value = command_string(root, "value"),
               *binding = command_string(reply, "binding");
    yyjson_val *choices = yyjson_obj_get(reply, "choices");
    if (!*binding || (yyjson_arr_size(choices) && !command_find(choices, value))) {
        return response("failed", "Choose a listed answer");
    }
    yyjson_mut_val *packet = yyjson_mut_doc_get_root(task->packet);
    yyjson_mut_val *bindings = yyjson_mut_obj_get(packet, "bindings");
    if (!bindings) {
        bindings = yyjson_mut_obj_add_obj(task->packet, packet, "bindings");
    }
    yyjson_mut_obj_remove_key(bindings, binding);
    yyjson_mut_obj_add(
        bindings,
        yyjson_mut_strcpy(task->packet, binding),
        yyjson_mut_strcpy(task->packet, value)
    );
    start(task);
    return NULL;
}

static char *reply(app_scene_authoring *scene, command_task *task, yyjson_val *root)
{
    if (!task || runtime(scene)->active != task || task->request) {
        return response("failed", "Task is not waiting for a reply");
    }
    yyjson_doc *prior = yyjson_read(task->result, strlen(task->result), 0);
    yyjson_val *previous = yyjson_doc_get_root(prior);
    const char *status = command_string(previous, "status");
    char *result = NULL;
    if (!strcmp(status, "needs_review") &&
        yyjson_get_bool(yyjson_obj_get(root, "approve")) &&
        yyjson_arr_size(yyjson_obj_get(previous, "operations"))) {
        execute(scene, task);
    } else if (!strcmp(status, "needs_clarification")) {
        result = clarify(task, previous, root);
    } else {
        result = response("failed", "Review the proposed operations");
    }
    yyjson_doc_free(prior);
    return result ? result : xstrdup(task->result);
}

static char *respond(app_scene_authoring *scene, command_task *task, yyjson_val *root)
{
    if (task && yyjson_get_bool(yyjson_obj_get(root, "undo"))) {
        return undo_task(scene, task);
    }
    if (task && !strcmp(command_string(root, "feedback"), "incorrect")) {
        return mark_incorrect(task);
    }
    return reply(scene, task, root);
}

static char *cancel(app_scene_authoring *scene, command_task *task)
{
    struct editor_command_runtime *state = runtime(scene);
    if (!task) {
        return response("failed", "Unknown task");
    }
    if (state->active == task) {
        editor_command_request_free(task->request);
        task->request = NULL;
        set_result(task, response("cancelled", "Cancelled"));
        state->active = NULL;
        trace(task);
    }
    return xstrdup(task->result);
}

static command_task *new_task(app_scene_authoring *scene, yyjson_doc *doc, const char *request)
{
    struct editor_command_runtime *state = runtime(scene);
    yyjson_val *root = yyjson_doc_get_root(doc);
    const char *id = command_string(root, "request_id");
    command_task *task = xcalloc(1, sizeof *task);
    char generated[48];
    snprintf(generated, sizeof generated, "command-%u", ++state->serial);
    task->id = xstrdup(*id ? id : generated);
    task->input = xstrdup(request);
    task->started = SDL_GetTicks();
    task->catalog = editor_commands_catalog(scene);
    task->dry = yyjson_get_bool(yyjson_obj_get(root, "dry_run"));
    task->packet = yyjson_doc_mut_copy(doc, NULL);
    yyjson_doc *catalog = yyjson_read(task->catalog, strlen(task->catalog), 0);
    yyjson_mut_val *packet = yyjson_mut_doc_get_root(task->packet);
    yyjson_mut_obj_remove_key(packet, "catalog");
    yyjson_mut_obj_add_val(
        task->packet,
        packet,
        "catalog",
        yyjson_val_mut_copy(task->packet, yyjson_doc_get_root(catalog))
    );
    yyjson_doc_free(catalog);
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
    return task;
}

/* Typed operations execute at once; a natural language command asks the model. */
static char *submit(app_scene_authoring *scene, yyjson_doc *doc, const char *request)
{
    struct editor_command_runtime *state = runtime(scene);
    yyjson_val *root = yyjson_doc_get_root(doc);
    const char *id = command_string(root, "request_id");
    command_task *existing = *id ? find_task(state, id) : NULL;
    if (existing) {
        return !strcmp(existing->input, request)
                 ? xstrdup(existing->result)
                 : response("failed", "Request ID already used with different input");
    }
    if (state->active) {
        return response("busy", "Another command is active");
    }
    if (acount(state->tasks) >= 1024) {
        return response("failed", "Session task limit reached");
    }
    command_task *task = new_task(scene, doc, request);
    apush(state->tasks, task);
    state->active = state->latest = task;
    if (yyjson_obj_get(root, "operations")) {
        set_result(task, xstrdup(request));
        execute(scene, task);
    } else {
        start(task);
    }
    return xstrdup(task->result);
}

char *editor_commands_api(app_scene_authoring *scene, const char *request)
{
    yyjson_doc *doc = yyjson_read(request, strlen(request), 0);
    yyjson_val *root = yyjson_doc_get_root(doc);
    const char *operation = command_string(root, "operation");
    command_task *task = find_task(runtime(scene), command_string(root, "task_id"));
    char *result = NULL;
    const char *query = command_string(root, "query");
    if (!strcmp(operation, "inspect")) {
        result = command_inspect(scene, query, yyjson_obj_get(root, "kinds"));
    } else if (!strcmp(operation, "resolve_objects")) {
        result = command_resolve_objects(scene, query, yyjson_obj_get(root, "kinds"));
    } else if (!strcmp(operation, "status")) {
        result = task ? xstrdup(task->result) : response("failed", "Unknown task");
    } else if (!strcmp(operation, "cancel")) {
        result = cancel(scene, task);
    } else if (!strcmp(operation, "respond")) {
        result = respond(scene, task, root);
    } else if (!strcmp(operation, "submit")) {
        result = submit(scene, doc, request);
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
