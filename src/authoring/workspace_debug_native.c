#include "authoring/workspace_debug_definition.h"
#include "execution/status.h"
#include "runtime/behaviors/entities.h"
#include "runtime/scenes/scene.h"
#include "runtime/value_json.h"
#include "support/array.h"
#include "types/array.h"

#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
enum
{
    TARGETS_MAX = 512,
    POINTS_MAX = 2048,
    FRAMES_MAX = 64,
    MODULES_MAX = 128
};
static const char *copy(const behavior_function_context *ctx, const char *text)
{
    return ctx->copy_string(ctx->value_storage, text ? text : "");
}
static pin_array *array(const behavior_function_context *ctx, workspace_debug_row *rows, int count)
{
    pin_array *a = arena_alloc(ctx->value_storage, sizeof *a);
    *a = (pin_array){ .count = count,
                      .capacity = count,
                      .element_type = PIN_STRUCT,
                      .type_name = WORKSPACE_DEBUG_ROW_LAYOUT.name };
    a->items = count ? arena_alloc(ctx->value_storage, (size_t)count * sizeof *a->items)
                     : NULL;
    for (int i = 0; i < count; i++) {
        workspace_debug_row *row = arena_alloc(ctx->value_storage, sizeof *row);
        *row = rows[i];
        row->source = copy(ctx, row->source);
        row->prototype = copy(ctx, row->prototype);
        row->kind = copy(ctx, row->kind);
        row->name = copy(ctx, row->name);
        row->text = copy(ctx, row->text);
        row->type = copy(ctx, row->type);
        a->items[i] = (pin_value){ .type = PIN_STRUCT,
                                   .type_name = WORKSPACE_DEBUG_ROW_LAYOUT.name,
                                   .ptr = row };
    }
    return a;
}
static const char *query(const behavior_function_context *ctx, void *host, const pin_value *in, pin_value *out)
{
    source_registry *r = workspace_debug_sources(host, in[0].i);
    workspace_debug_row *rows = NULL;
    bool truncated = false;
    for (int i = 0; r && i < source_program_count(r); i++) {
        const char *name = source_program_name_at(r, i);
        execution_program *p = source_program(r, name);
        for (int j = 0; j < execution_program_owner_count(p); j++) {
            if (acount(rows) == TARGETS_MAX) {
                truncated = true;
                break;
            }
            workspace_debug_owner provenance = { 0 };
            workspace_debug_owner_read(host, in[0].i, execution_program_owner_at(p, j), &provenance);
            apush(
                rows,
                ((workspace_debug_row){
                    .kind = "target",
                    .prototype = provenance.prototype,
                    .creation = (int64_t)provenance.creation,
                    .object = provenance.object,
                    .name = name,
                    .owner = execution_program_owner_at(p, j),
                    .module = execution_module_id(execution_program_module(p)),
                    .enabled = execution_program_paused(p),
                    .available = true })
            );
        }
        if (truncated) {
            break;
        }
    }
    out[0].ptr = array(ctx, rows, acount(rows));
    out[1].str = copy(
        ctx,
        !r          ? "No committed runtime for this session"
        : truncated ? "Debug target limit reached; at most512 owners are shown"
                    : ""
    );
    afree(rows);
    return NULL;
}
static const char *open_target(
    const behavior_function_context *ctx,
    void *host,
    const pin_value *in,
    pin_value *out
)
{
    char error[256] = "";
    int handle = 0;
    workspace_debug_open(host, in[0].i, in[1].str, in[2].u64, &handle, error, sizeof error);
    out[0].i = handle;
    out[1].str = copy(ctx, error);
    return NULL;
}
static const execution_module *find_module(const execution_module *m, uint64_t id, int depth)
{
    if (depth > 16) {
        return NULL;
    }
    if (execution_module_id(m) == id) {
        return m;
    }
    for (int i = 0; i < execution_module_import_count(m); i++) {
        const execution_module
            *found = find_module(execution_module_import_at(m, i), id, depth + 1);
        if (found) {
            return found;
        }
    }
    return NULL;
}
static const char *function_name(const execution_module *root, uint64_t module, uint64_t function)
{
    const execution_module *m = find_module(root, module, 0);
    for (int i = 0; m && i < execution_module_function_count(m); i++) {
        execution_module_function_info f = execution_module_function_at(m, i);
        if (f.id == function) {
            return f.entry ? f.entry : f.event_name;
        }
    }
    return "";
}
static const char *function_source(const execution_module *root, uint64_t module, uint64_t function)
{
    const execution_module *m = find_module(root, module, 0);
    for (int i = 0; m && i < execution_module_function_count(m); i++) {
        execution_module_function_info f = execution_module_function_at(m, i);
        if (f.id != function) {
            continue;
        }
        for (int k = 0; k < behavior_function_source_count(f.body); k++) {
            const char *asset = behavior_function_source_at(f.body, k)->asset_name;
            if (asset && *asset) {
                return asset;
            }
        }
    }
    return "";
}
static workspace_debug_row point_row(const char *kind, uint64_t module, uint64_t function, behavior_source_point p)
{
    return (workspace_debug_row){ .kind = kind,
                                  .module = module,
                                  .function = function,
                                  .occurrence = p.occurrence,
                                  .start = p.start,
                                  .end = p.end,
                                  .line = p.line,
                                  .column = p.column,
                                  .available = true };
}
static void points(
    const execution_module *m,
    const execution_inspector *inspector,
    workspace_debug_row **rows,
    uint64_t visited[MODULES_MAX],
    int *count,
    int depth,
    bool *truncated
)
{
    if (depth > 16 || *count == MODULES_MAX) {
        *truncated = true;
        return;
    }
    uint64_t id = execution_module_id(m);
    for (int i = 0; i < *count; i++) {
        if (visited[i] == id) {
            return;
        }
    }
    visited[(*count)++] = id;
    for (int i = 0; i < execution_module_function_count(m); i++) {
        execution_module_function_info f = execution_module_function_at(m, i);
        for (int j = 0; j < behavior_function_source_point_count(f.body); j++) {
            if (acount(*rows) == POINTS_MAX) {
                *truncated = true;
                return;
            }
            workspace_debug_row row = point_row(
                "point",
                id,
                f.id,
                behavior_function_source_point_at(f.body, j)
            );
            row.name = f.entry ? f.entry : f.event_name;
            row.source = function_source(m, id, f.id);
            row.enabled = execution_inspector_has_breakpoint(inspector, id, f.id, row.occurrence);
            apush(*rows, row);
        }
    }
    for (int i = 0; i < execution_module_import_count(m); i++) {
        points(execution_module_import_at(m, i), inspector, rows, visited, count, depth + 1, truncated);
    }
}
static const char *inspect(
    const behavior_function_context *ctx,
    void *host,
    const pin_value *in,
    pin_value *out
)
{
    workspace_debug_view view = { 0 };
    bool ok = workspace_debug_read(host, in[0].i, &view);
    char error[256] = "";
    workspace_debug_row cursor = { 0 }, *frames = NULL, *values = NULL,
                        *source_points = NULL;
    bool truncated = false;
    declared_components_scope scope = { 0 };
    if (ok && view.types) {
        ok = declared_components_scope_enter(view.types, &scope);
        if (!ok) {
            snprintf(error, sizeof error, "Debug type generation could not be entered");
        }
    }
    if (ok) {
        cursor = point_row(
            "cursor",
            view.cursor.module,
            view.cursor.function,
            view.cursor.point
        );
        memcpy(
            cursor.query_variables,
            view.cursor.query_variables,
            sizeof cursor.query_variables
        );
        cursor.name = view.name;
        cursor.source = function_source(
            view.module,
            view.cursor.module,
            view.cursor.function
        );
        cursor.owner = view.owner;
        cursor.generation = view.cursor.generation;
        cursor.run = view.cursor.run;
        cursor.step = view.cursor.step;
        cursor.depth = view.cursor.depth;
        cursor.status = view.cursor.status;
        cursor.retired = view.cursor.retired;
        cursor.available = view.live;
        cursor.enabled = view.cursor.status == FLOW_PAUSED_RESULT;
        if (view.failure && view.failure->owner == view.owner) {
            snprintf(error, sizeof error, "%s", view.failure->message);
            {
                /* Retained failure and source location share one exact activation. */
                cursor.run = 0;
                cursor.step = 0;
                cursor.module = view.failure->module_id;
                cursor.function = view.failure->function_id;
                cursor.occurrence = view.failure->node_id;
                cursor.start = view.failure->start;
                cursor.end = view.failure->end;
                cursor.line = view.failure->line;
                cursor.column = view.failure->column;
                cursor.status = view.failure->status;
                cursor.source = view.failure->asset_name;
                memcpy(
                    cursor.query_variables,
                    view.failure->query_variables,
                    sizeof cursor.query_variables
                );
            }
        }
        int count = execution_inspector_owner_frame_count(view.inspector, view.owner);
        truncated = count > FRAMES_MAX;
        if (count > FRAMES_MAX) {
            count = FRAMES_MAX;
        }
        for (int i = 0; i < count; i++) {
            execution_debug_frame
                f = execution_inspector_owner_frame_at(view.inspector, view.owner, i);
            workspace_debug_row row = point_row("frame", f.module, f.function, f.point);
            row.index = i;
            row.generation = view.cursor.generation;
            row.owner = view.owner;
            row.run = view.cursor.run;
            row.depth = f.depth;
            row.name = function_name(view.module, f.module, f.function);
            row.source = function_source(view.module, f.module, f.function);
            apush(frames, row);
        }
        size_t bytes = 0;
        for (int i = 0; i < execution_inspector_watch_count(view.inspector); i++) {
            const execution_debug_watch *w = execution_inspector_watch_at(view.inspector, i);
            if (w->owner != view.owner) {
                continue;
            }
            workspace_debug_row
                row = point_row("value", w->module, w->function, w->source.origin);
            memcpy(row.query_variables, w->query_variables, sizeof row.query_variables);
            row.source = function_source(view.module, w->module, w->function);
            row.generation = w->generation;
            row.owner = w->owner;
            row.run = w->run;
            row.index = w->source.ordinal;
            row.type = w->value.type_name;
            row.status = w->availability;
            row.output = w->source.output;
            row.available = w->availability == EXECUTION_WATCH_AVAILABLE;
            char *text = NULL, problem[128] = "";
            if (row.available &&
                !value_to_json(&w->value, &text, problem, sizeof problem)) {
                row.available = false;
                row.text = problem;
            }
            if (text) {
                bytes += strlen(text);
                if (bytes > VALUE_JSON_MAX_BYTES) {
                    free(text);
                    truncated = true;
                    break;
                }
                row.text = text;
            }
            /* Rows copy strings before releasing temporary JSON/error storage. */
            row.kind = copy(ctx, row.kind);
            row.name = copy(ctx, row.name);
            row.type = copy(ctx, row.type);
            row.text = copy(ctx, row.text);
            apush(values, row);
            free(text);
        }
        uint64_t visited[MODULES_MAX];
        int modules = 0;
        points(view.module, view.inspector, &source_points, visited, &modules, 0, &truncated);
    }
    if (scope.active) {
        bool left = declared_components_scope_leave(&scope);
        assert(left);
        (void)left;
    }
    out[0].b = ok;
    pin_array *c = array(ctx, &cursor, 1);
    out[1] = c->items[0];
    out[2].ptr = array(ctx, frames, acount(frames));
    out[3].ptr = array(ctx, values, acount(values));
    out[4].ptr = array(ctx, source_points, acount(source_points));
    out[5].str = copy(
        ctx,
        error[0]    ? error
        : !ok       ? "Invalid debug observation"
        : truncated ? "Debug snapshot reached its explicit row or byte limit"
                    : ""
    );
    afree(frames);
    afree(values);
    afree(source_points);
    return NULL;
}
static const char *activity(
    const behavior_function_context *ctx,
    void *host,
    const pin_value *in,
    pin_value *out
)
{
    workspace_debug_view view = { 0 };
    bool valid = workspace_debug_read(host, in[0].i, &view);
    const char *error = !valid ? "Invalid debug observation" : "";
    if (valid && in[1].u64 != view.cursor.generation) {
        valid = false;
        error = "Debug activity generation does not match this retained "
                "observation";
    }
    if (valid && in[2].u64 != view.owner) {
        valid = false;
        error = "Debug activity owner does not match this retained observation";
    }
    int count = valid ? execution_inspector_activity_count(view.inspector) : 0;
    if (count > 512) {
        valid = false;
        error = "Debug activity exceeds the explicit 512-row bound";
    }
    int selected = 0;
    for (int i = 0; valid && i < count; i++) {
        execution_debug_activity a = execution_inspector_activity_at(view.inspector, i);
        if (a.generation == in[1].u64 && a.owner == view.owner) {
            selected++;
        }
    }
    pin_array *rows = pin_array_snapshot_allocate(
        ctx->value_storage,
        PIN_STRUCT,
        WORKSPACE_DEBUG_ACTIVITY_LAYOUT.name,
        valid ? selected : 0
    );
    int at = 0;
    for (int i = 0; valid && i < count; i++) {
        execution_debug_activity a = execution_inspector_activity_at(view.inspector, i);
        if (a.generation != in[1].u64 || a.owner != view.owner) {
            continue;
        }
        workspace_debug_activity *row = arena_alloc(ctx->value_storage, sizeof *row);
        *row = (workspace_debug_activity){
            .source = copy(ctx, function_source(view.module, a.module, a.function)),
            .generation = a.generation,
            .owner = a.owner,
            .module = a.module,
            .function = a.function,
            .occurrence = a.occurrence,
            .from_module = a.from_module,
            .from_function = a.from_function,
            .from_occurrence = a.from_occurrence,
            .count = a.count
        };
        memcpy(row->query_variables, a.query_variables, sizeof row->query_variables);
        rows->items[at++] = (pin_value){
            .type = PIN_STRUCT,
            .type_name = WORKSPACE_DEBUG_ACTIVITY_LAYOUT.name,
            .ptr = row
        };
    }
    out[0].b = valid;
    out[1].ptr = rows;
    out[2].u64 = valid ? execution_inspector_activity_dropped(view.inspector) : 0;
    out[3].b = valid && !view.live;
    out[4].str = copy(ctx, error);
    return NULL;
}
static const char *control(
    const behavior_function_context *ctx,
    void *host,
    const pin_value *in,
    pin_value *out
)
{
    char error[256] = "";
    out[0].b = workspace_debug_control(
        host,
        in[0].i,
        in[1].str,
        in[2].u64,
        in[3].u64,
        in[4].u64,
        in[5].b,
        error,
        sizeof error
    );
    out[1].str = copy(ctx, error);
    return NULL;
}
static const char *close_target(
    const behavior_function_context *,
    void *host,
    const pin_value *in,
    pin_value *out
)
{
    out[0].b = workspace_debug_close(host, in[0].i);
    return NULL;
}
static const char *read_source(
    const behavior_function_context *ctx,
    void *host,
    const pin_value *in,
    pin_value *out
)
{
    const char *path, *text;
    char error[256];
    out[0].b = workspace_debug_source(host, in[0].i, in[1].str, &path, &text, error, sizeof error);
    out[1].str = copy(ctx, path);
    out[2].str = copy(ctx, text);
    out[3].str = copy(ctx, error);
    return NULL;
}

static bool contains(const char *text, const char *query)
{
    if (!query || !*query) {
        return true;
    }
    for (const char *p = text; p && *p; p++) {
        const char *a = p, *b = query;
        while (*a && *b && tolower((unsigned char)*a) == tolower((unsigned char)*b)) {
            a++;
            b++;
        }
        if (!*b) {
            return true;
        }
    }
    return false;
}
static const char *query_prototypes(
    const behavior_function_context *ctx,
    void *host,
    const pin_value *in,
    pin_value *out
)
{
    out[0].ptr = array(ctx, NULL, 0);
    out[1].i = 0;
    out[2].str = copy(ctx, "");
    if (!in || in[1].i < 0 || in[2].i < 0 || in[2].i > 256 || !in[3].str ||
        strlen(in[3].str) > 256) {
        out[2].str = copy(ctx, "Prototype query requires First>=0, Count0..256 and Query<=256bytes");
        return NULL;
    }
    entities *service = workspace_debug_entities(host, in[0].i);
    if (!service) {
        out[2].str = copy(ctx, "No committed entity registry for this session");
        return NULL;
    }
    if (entities_prototype_count(service) > 4096) {
        out[2].str = copy(ctx, "Registered prototype inventory exceeds4096generations");
        return NULL;
    }
    workspace_debug_row *rows = NULL;
    int total = 0, work = 0;
    bool valid = true;
    for (int i = 0; valid && i < entities_prototype_count(service); i++) {
        const char *key = entities_prototype_name_at(service, i);
        const scene *prototype = entities_prototype_scene_at(service, i);
        for (int object = 0; valid && object < scene_object_count(prototype); object++) {
            int count = scene_object_behavior_count(prototype, object);
            for (int j = 0; valid && j < (count ? count : 1); j++) {
                if (++work > 65536) {
                    valid = false;
                    break;
                }
                const char *behavior = count
                                         ? scene_object_behavior(prototype, object, j)
                                         : "";
                if (!contains(key, in[3].str) && !contains(behavior, in[3].str)) {
                    continue;
                }
                int slot = total++;
                if (slot >= in[1].i && slot - in[1].i < in[2].i) {
                    bool resolved = *behavior &&
                                    source_program(
                                        workspace_debug_sources(host, in[0].i),
                                        behavior
                                    );
                    apush(
                        rows,
                        ((workspace_debug_row){ .kind = "prototype",
                                                .prototype = key,
                                                .name = resolved ? behavior : "",
                                                .text = *behavior && !resolved ? "Declared source module is unavailable"
                                                                               : "",
                                                .object = object,
                                                .available = true })
                    );
                }
            }
        }
    }
    if (valid) {
        out[0].ptr = array(ctx, rows, acount(rows));
        out[1].i = total;
    } else {
        out[2].str = copy(ctx, "Prototype association scan exceeds65536records");
    }
    afree(rows);
    return NULL;
}
static int created_order(const void *a, const void *b)
{
    const workspace_debug_row *x = a, *y = b;
    return x->creation > y->creation ? -1
         : x->creation < y->creation ? 1
         : x->owner < y->owner       ? -1
         : x->owner > y->owner       ? 1
                                     : 0;
}
static const char *query_spawns(
    const behavior_function_context *ctx,
    void *host,
    const pin_value *in,
    pin_value *out
)
{
    out[0].ptr = array(ctx, NULL, 0);
    out[1].str = copy(ctx, "");
    if (!in || !in[1].str || !*in[1].str || strlen(in[1].str) > 4096 ||
        in[2].i < -1 || in[3].i64 < 0) {
        out[1].str = copy(ctx, "Spawn query requires exact prototype, Object>=-1 and After>=0");
        return NULL;
    }
    entities *service = workspace_debug_entities(host, in[0].i);
    if (!service) {
        out[1].str = copy(ctx, "No committed entity registry for this session");
        return NULL;
    }
    if (entities_origin_count(service) > 65536) {
        out[1].str = copy(ctx, "Spawn origin scan exceeds65536records");
        return NULL;
    }
    workspace_debug_row *rows = NULL;
    bool valid = true;
    for (int i = 0; i < entities_origin_count(service); i++) {
        ecs_entity entity;
        entity_origin origin;
        if (!entities_origin_at(service, i, &entity, &origin) ||
            strcmp(origin.prototype, in[1].str) ||
            (in[2].i >= 0 && origin.object != in[2].i)) {
            continue;
        }
        if (origin.creation > (uint64_t)INT64_MAX) {
            valid = false;
            out[1].str = copy(ctx, "Spawn chronology is outside the representable signed domain");
            break;
        }
        if ((int64_t)origin.creation <= in[3].i64) {
            continue;
        }
        if (acount(rows) == 512) {
            valid = false;
            out[1].str = copy(ctx, "Spawn observation exceeds512matches; narrow its object or chronology boundary");
            break;
        }
        apush(
            rows,
            ((workspace_debug_row){ .kind = "spawn",
                                    .prototype = origin.prototype,
                                    .object = origin.object,
                                    .creation = (int64_t)origin.creation,
                                    .owner = ecs_entity_to_u64(entity),
                                    .available = true })
        );
    }
    if (valid) {
        if (acount(rows) > 1) {
            qsort(rows, acount(rows), sizeof *rows, created_order);
        }
        out[0].ptr = array(ctx, rows, acount(rows));
    }
    afree(rows);
    return NULL;
}
static const node_access ACCESS = { .flags = NODE_EFFECT_HOST_SINK };
#define OPS(X)                                                                 \
    X("Read Debug Source", read_source)                                        \
    X("Query Debug Targets", query)                                            \
    X("Open Debug Target", open_target)                                        \
    X("Inspect Debug Target", inspect)                                         \
    X("Control Debug Target", control)                                         \
    X("Close Debug Target", close_target)                                      \
    X("Inspect Debug Activity", activity)                                      \
    X("Query Debug Prototypes", query_prototypes)                              \
    X("Query Debug Spawns", query_spawns)
#define CALL(N, F)                                                             \
    static const char *F##_dispatch(                                           \
        const behavior_function_context *ctx,                                  \
        void *host,                                                            \
        const pin_value *in,                                                   \
        pin_value *out                                                         \
    )                                                                          \
    {                                                                          \
        if (!host) {                                                           \
            return "Debugging requires an authoring host";                     \
        }                                                                      \
        return F(ctx, host, in, out);                                          \
    }                                                                          \
    static const node_host_call F##_CALL = {                                   \
        .context = F##_dispatch,                                               \
        .service = "workspace.debug",                                          \
        .optional_service = true,                                              \
        .missing_service = "Debugging requires an authoring host"              \
    };
OPS(CALL)
#define BIND(N, F)                                                             \
    { .canonical_name = N,                                                     \
      .implementation = { .call = &F##_CALL, .access = &ACCESS } },
static const native_definition_binding BINDINGS[] = { OPS(BIND) };
static const type_desc *const LAYOUTS[] = { &WORKSPACE_DEBUG_ROW_LAYOUT,
                                            &WORKSPACE_DEBUG_ACTIVITY_LAYOUT };
const native_definition_pack WORKSPACE_DEBUG_DEFINITION_PACK = {
    .definitions = WORKSPACE_DEBUG_DEFINITIONS,
    .definition_count = 9,
    .bindings = BINDINGS,
    .binding_count = 9,
    .layouts = LAYOUTS,
    .layout_count = 2
};
