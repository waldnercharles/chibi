#include "authoring/workspace_debug.h"

#include "authoring/workspace_assets.h"
#include "authoring/workspace_resources.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
enum
{
    OBSERVATIONS_MAX = 16
};
typedef struct
{
    char *name, *path, *text;
} debug_source;
typedef struct
{
    int handle, session;
    char *name;
    uint64_t owner;
    execution_inspector *inspector;
    execution_module *module;
    declared_components *types;
    bool run_preserve;
    debug_source *sources;
    char source_error[256];
    uint64_t run_module, run_function, run_occurrence;
} observation;
struct workspace_debug
{
    workspace_debug_registry registry;
    entities *(*entities_resolver)(void *, int);
    bool (*owner_info)(void *, int, uint64_t, workspace_debug_owner *);
    const char *(*source_root)(void *, int);
    declared_components *(*types)(void *, int);
    void *(*enter)(void *, int, const char **);
    void (*leave)(void *, void *);
    void *user;
    int sequence;
    observation observations[OBSERVATIONS_MAX];
};
static bool refuse(char *error, size_t size, const char *message)
{
    snprintf(error, size, "%s", message);
    return false;
}
static observation *find(workspace_debug *s, int handle)
{
    for (int i = 0; i < OBSERVATIONS_MAX; i++) {
        if (handle > 0 && s->observations[i].handle == handle) {
            return &s->observations[i];
        }
    }
    return NULL;
}
static bool owner_exists(execution_program *p, uint64_t owner)
{
    for (int i = 0; i < execution_program_owner_count(p); i++) {
        if (execution_program_owner_at(p, i) == owner) {
            return true;
        }
    }
    return false;
}
static execution_program *live(workspace_debug *s, observation *o)
{
    source_registry *r = s->registry(s->user, o->session);
    execution_program *p = r ? source_program(r, o->name) : NULL;
    return p && execution_program_inspection(p) == o->inspector &&
                   execution_program_module(p) == o->module &&
                   owner_exists(p, o->owner)
             ? p
             : NULL;
}
workspace_debug *workspace_debug_new(workspace_debug_registry registry, void *user)
{
    assert(registry);
    workspace_debug *s = xcalloc(1, sizeof *s);
    s->registry = registry;
    s->user = user;
    return s;
}
source_registry *workspace_debug_sources(workspace_debug *s, int session)
{
    return session > 0 ? s->registry(s->user, session) : NULL;
}
void workspace_debug_set_entities(workspace_debug *s, entities *(*resolve)(void *, int))
{
    if (s) {
        s->entities_resolver = resolve;
    }
}
entities *workspace_debug_entities(workspace_debug *s, int session)
{
    return s && session > 0 && s->registry(s->user, session) && s->entities_resolver
             ? s->entities_resolver(s->user, session)
             : NULL;
}
void workspace_debug_set_owner_resolver(
    workspace_debug *s,
    bool (*resolve)(void *, int, uint64_t, workspace_debug_owner *)
)
{
    if (s) {
        s->owner_info = resolve;
    }
}
bool workspace_debug_owner_read(workspace_debug *s, int session, uint64_t owner, workspace_debug_owner *out)
{
    *out = (workspace_debug_owner){ 0 };
    workspace_debug_owner candidate = { 0 };
    if (!s || session <= 0 || !s->owner_info || !s->registry(s->user, session) ||
        !s->owner_info(s->user, session, owner, &candidate) ||
        !candidate.prototype || !*candidate.prototype || candidate.object < 0 ||
        !candidate.creation || candidate.creation > (uint64_t)INT64_MAX) {
        return false;
    }
    *out = candidate;
    return true;
}
bool workspace_debug_close(workspace_debug *s, int handle)
{
    observation *o = find(s, handle);
    if (!o) {
        return false;
    }
    if (o->run_occurrence && !o->run_preserve) {
        execution_inspector_breakpoint(o->inspector, o->run_module, o->run_function, o->run_occurrence, false);
    }
    execution_inspector_recording(o->inspector, false, 0);
    execution_inspector_select_owner(o->inspector, 0);
    execution_inspector_free(o->inspector);
    execution_module_free(o->module);
    declared_components_free(o->types);
    for (int i = 0; i < acount(o->sources); i++) {
        free(o->sources[i].name);
        free(o->sources[i].path);
        free(o->sources[i].text);
    }
    afree(o->sources);
    free(o->name);
    *o = (observation){ 0 };
    return true;
}
void workspace_debug_free(workspace_debug *s)
{
    if (!s) {
        return;
    }
    for (int i = 0; i < OBSERVATIONS_MAX; i++) {
        workspace_debug_close(s, s->observations[i].handle);
    }
    free(s);
}
static void source_names(const execution_module *m, char ***names, uint64_t *visited, int *modules, int depth)
{
    if (depth > 16 || *modules == 128) {
        return;
    }
    uint64_t id = execution_module_id(m);
    for (int i = 0; i < *modules; i++) {
        if (visited[i] == id) {
            return;
        }
    }
    visited[(*modules)++] = id;
    for (int i = 0; i < execution_module_function_count(m); i++) {
        execution_module_function_info f = execution_module_function_at(m, i);
        for (int j = 0; j < behavior_function_source_count(f.body); j++) {
            const char *name = behavior_function_source_at(f.body, j)->asset_name;
            if (!name || !*name) {
                continue;
            }
            bool known = false;
            for (int n = 0; n < acount(*names); n++) {
                known |= !strcmp((*names)[n], name);
            }
            if (!known && acount(*names) < 128) {
                apush(*names, xstrdup(name));
            }
        }
    }
    for (int i = 0; i < execution_module_import_count(m); i++) {
        source_names(execution_module_import_at(m, i), names, visited, modules, depth + 1);
    }
}
static void capture_sources(workspace_debug *s, observation *o)
{
    const char *root = s->source_root ? s->source_root(s->user, o->session) : NULL;
    if (!root) {
        snprintf(o->source_error, sizeof o->source_error, "This host has no frozen executable source snapshot");
        return;
    }
    uint64_t visited[128];
    int modules = 0;
    char **names = NULL;
    source_names(o->module, &names, visited, &modules, 0);
    workspace_resources
        *inventory = workspace_resources_new(root, o->source_error, sizeof o->source_error);
    workspace_documents *documents = workspace_documents_new(root, NULL);
    size_t bytes = 0;
    bool ok = inventory && documents;
    if (ok) {
        workspace_resource_result status = workspace_resources_refresh(inventory);
        ok = status.ok && status.complete;
        if (!ok) {
            snprintf(o->source_error, sizeof o->source_error, "Frozen source inventory is incomplete");
        }
    }
    const char *kinds[] = { "behavior", "library", "system" };
    for (int k = 0; ok && k < 3; k++) {
        arena arena = { 0 };
        pin_array *rows = NULL;
        workspace_resource_result
            status = workspace_resources_query(inventory, 0, 256, kinds[k], "", &arena, &rows);
        if (!status.ok || status.total > 256) {
            ok = false;
            snprintf(o->source_error, sizeof o->source_error, "Frozen source category exceeds its256filebudget");
        }
        for (int i = 0; ok && rows && i < rows->count; i++) {
            workspace_resource_row *row = rows->items[i].ptr;
            int handle = 0;
            if (workspace_document_open(documents, row->path, &handle) !=
                WORKSPACE_DOCUMENT_OK) {
                continue;
            }
            workspace_document_view view;
            workspace_document_read(documents, handle, &view);
            char *kind = NULL, *name = NULL, error[128];
            bool identified = workspace_asset_identity(
                documents,
                handle,
                view.revision,
                &kind,
                &name,
                error,
                sizeof error
            );
            bool wanted = false;
            for (int n = 0; identified && n < acount(names); n++) {
                wanted |= !strcmp(names[n], name);
            }
            if (wanted) {
                bool duplicate = false;
                for (int n = 0; n < acount(o->sources); n++) {
                    duplicate |= !strcmp(o->sources[n].name, name);
                }
                if (duplicate || bytes + view.bytes > 4 * 1024 * 1024) {
                    ok = false;
                    snprintf(o->source_error, sizeof o->source_error, "Frozen source identities are ambiguous or exceed4MiB");
                } else {
                    apush(
                        o->sources,
                        ((debug_source){ xstrdup(name), xstrdup(view.path), xstrdup(view.text) })
                    );
                    bytes += view.bytes;
                }
            }
            free(kind);
            free(name);
            workspace_document_close(documents, handle);
        }
        arena_free(&arena);
    }
    if (inventory) {
        workspace_resources_free(inventory);
    }
    workspace_documents_free(documents);
    for (int i = 0; i < acount(names); i++) {
        free(names[i]);
    }
    afree(names);
    if (!ok) {
        for (int i = 0; i < acount(o->sources); i++) {
            free(o->sources[i].name);
            free(o->sources[i].path);
            free(o->sources[i].text);
        }
        afree(o->sources);
    }
}
bool workspace_debug_open(
    workspace_debug *s,
    int session,
    const char *name,
    uint64_t owner,
    int *handle,
    char *error,
    size_t size
)
{
    *handle = 0;
    source_registry *r = workspace_debug_sources(s, session);
    execution_program *p = r ? source_program(r, name) : NULL;
    if (!p || !owner_exists(p, owner)) {
        return refuse(error, size, "Debug target is unavailable in this session");
    }
    for (int i = 0; i < OBSERVATIONS_MAX; i++) {
        if (s->observations[i].handle && s->observations[i].session == session &&
            s->observations[i].module == execution_program_module(p)) {
            return refuse(error, size, "This target already has an owned observation");
        }
    }
    observation *o = NULL;
    for (int i = 0; i < OBSERVATIONS_MAX; i++) {
        if (!s->observations[i].handle) {
            o = &s->observations[i];
            break;
        }
    }
    if (!o || s->sequence == INT_MAX) {
        return refuse(error, size, "Close a debug observation before opening another");
    }
    execution_inspector *inspector = execution_program_inspector(p);
    if (!execution_inspector_select_owner(inspector, owner)) {
        execution_inspector_free(inspector);
        return refuse(error, size, "Debug owner could not be selected");
    }
    execution_module *module = (execution_module *)execution_program_module(p);
    execution_module_retain(module);
    *o = (observation){
        .handle = ++s->sequence,
        .session = session,
        .name = xstrdup(name),
        .owner = owner,
        .inspector = inspector,
        .module = module,
        .types = s->types ? declared_components_retain(s->types(s->user, session)) : NULL
    };
    capture_sources(s, o);
    execution_inspector_recording(inspector, true, owner);
    *handle = o->handle;
    error[0] = 0;
    return true;
}
bool workspace_debug_read(workspace_debug *s, int handle, workspace_debug_view *view)
{
    observation *o = find(s, handle);
    if (!o) {
        return false;
    }
    bool current = live(s, o) != NULL;
    execution_debug_cursor cursor = execution_inspector_cursor(o->inspector);
    cursor.retired |= !current;
    if (o->run_occurrence && cursor.module == o->run_module &&
        cursor.function == o->run_function &&
        cursor.point.occurrence == o->run_occurrence) {
        if (!o->run_preserve) {
            execution_inspector_breakpoint(o->inspector, o->run_module, o->run_function, o->run_occurrence, false);
        }
        o->run_occurrence = 0;
    }
    *view = (workspace_debug_view){ .session = o->session,
                                    .name = o->name,
                                    .owner = o->owner,
                                    .live = current,
                                    .cursor = cursor,
                                    .inspector = o->inspector,
                                    .module = o->module,
                                    .types = o->types,
                                    .failure = execution_inspector_failure(o->inspector) };
    return true;
}
static bool has_point(const execution_module *m, uint64_t module, uint64_t function, uint64_t occurrence, int depth)
{
    if (depth > 16) {
        return false;
    }
    if (execution_module_id(m) == module) {
        for (int i = 0; i < execution_module_function_count(m); i++) {
            execution_module_function_info f = execution_module_function_at(m, i);
            if (f.id != function) {
                continue;
            }
            for (int j = 0; j < behavior_function_source_point_count(f.body); j++) {
                if (behavior_function_source_point_at(f.body, j).occurrence == occurrence) {
                    return true;
                }
            }
        }
    }
    for (int i = 0; i < execution_module_import_count(m); i++) {
        if (has_point(execution_module_import_at(m, i), module, function, occurrence, depth + 1)) {
            return true;
        }
    }
    return false;
}
static bool control_inner(
    workspace_debug *s,
    int handle,
    const char *operation,
    uint64_t module,
    uint64_t function,
    uint64_t occurrence,
    bool enabled,
    char *error,
    size_t size
)
{
    observation *o = find(s, handle);
    if (o && !strcmp(operation, "clear-error")) {
        execution_program *program = live(s, o);
        if (program) {
            execution_program_clear_error(program);
        }
        execution_inspector_clear_failure(o->inspector);
        error[0] = 0;
        return true;
    }
    if (!o || !live(s, o)) {
        return refuse(error, size, "Debug target belongs to a retired session or generation");
    }
    bool ok = false;
    if (!strcmp(operation, "pause")) {
        ok = execution_inspector_pause(o->inspector, o->owner);
    } else if (!strcmp(operation, "breakpoint") || !strcmp(operation, "run-to")) {
        if (!has_point(o->module, module, function, occurrence, 0)) {
            return refuse(error, size, "Breakpoint does not identify an executable source point");
        }
        bool run = !strcmp(operation, "run-to");
        if (run && o->run_occurrence) {
            return refuse(error, size, "A run-to point is already armed");
        }
        bool existed = execution_inspector_has_breakpoint(o->inspector, module, function, occurrence);
        ok = execution_inspector_breakpoint(o->inspector, module, function, occurrence, run || enabled);
        if (ok && run) {
            ok = execution_inspector_control(o->inspector, EXECUTION_DEBUG_RESUME) >= 0;
            if (ok) {
                o->run_preserve = existed;
                o->run_module = module;
                o->run_function = function;
                o->run_occurrence = occurrence;
            } else if (!existed) {
                execution_inspector_breakpoint(o->inspector, module, function, occurrence, false);
            }
        }
    } else {
        execution_debug_control control;
        if (!strcmp(operation, "resume")) {
            control = EXECUTION_DEBUG_RESUME;
        } else if (!strcmp(operation, "into")) {
            control = EXECUTION_DEBUG_INTO;
        } else if (!strcmp(operation, "over")) {
            control = EXECUTION_DEBUG_OVER;
        } else if (!strcmp(operation, "out")) {
            control = EXECUTION_DEBUG_OUT;
        } else {
            return refuse(error, size, "Unknown debug operation");
        }
        ok = execution_inspector_control(o->inspector, control) >= 0;
    }
    if (!ok) {
        return refuse(error, size, "Debug operation is unavailable for the current activation");
    }
    error[0] = 0;
    return true;
}

void workspace_debug_set_types(workspace_debug *s, declared_components *(*types)(void *, int))
{
    s->types = types;
}
void workspace_debug_set_scope(
    workspace_debug *s,
    void *(*enter)(void *, int, const char **),
    void (*leave)(void *, void *)
)
{
    assert((enter != NULL) == (leave != NULL));
    s->enter = enter;
    s->leave = leave;
}
bool workspace_debug_control(
    workspace_debug *s,
    int handle,
    const char *operation,
    uint64_t module,
    uint64_t function,
    uint64_t occurrence,
    bool enabled,
    char *error,
    size_t size
)
{
    observation *o = find(s, handle);
    if (o && !strcmp(operation, "clear-error") && !live(s, o)) {
        execution_inspector_clear_failure(o->inspector);
        error[0] = 0;
        return true;
    }
    if (!o || !live(s, o)) {
        return refuse(error, size, "Debug target belongs to a retired session or generation");
    }
    const char *failure = NULL;
    void *token = s->enter ? s->enter(s->user, o->session, &failure) : NULL;
    if (failure) {
        return refuse(error, size, failure);
    }
    bool ok = control_inner(s, handle, operation, module, function, occurrence, enabled, error, size);
    if (s->leave) {
        s->leave(s->user, token);
    }
    return ok;
}

void workspace_debug_set_source_root(workspace_debug *s, const char *(*root)(void *, int))
{
    s->source_root = root;
}
bool workspace_debug_source(
    workspace_debug *s,
    int handle,
    const char *asset,
    const char **path,
    const char **text,
    char *error,
    size_t size
)
{
    *path = "";
    *text = "";
    observation *o = find(s, handle);
    if (!o) {
        return refuse(error, size, "Invalid debug observation");
    }
    for (int i = 0; i < acount(o->sources); i++) {
        if (!strcmp(o->sources[i].name, asset)) {
            *path = o->sources[i].path;
            *text = o->sources[i].text;
            error[0] = 0;
            return true;
        }
    }
    return refuse(error, size, o->source_error[0] ? o->source_error : "This executable has no captured source for the requested asset");
}
