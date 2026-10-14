#include "authoring/workspace_runtime.h"
#include "authoring/workspace_runtime_definition.h"
#include "authoring/workspace_runtime_model.h"
#include "execution/function.h"
#include "types/array.h"

#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void outputs(const behavior_function_context *ctx, pin_value *out, workspace_runtime_result r)
{
    out[0].b = r.ok;
    out[1].b = r.preparing;
    out[2].b = r.ready;
    out[3].b = r.playing;
    out[4].i = r.session;
    out[5].i = r.document;
    out[6].i = r.revision;
    out[7].i = r.steps > INT_MAX ? INT_MAX : (int)r.steps;
    out[8].f = (float)r.time;
    char image[64] = "";
    if (r.texture) {
        snprintf(image, sizeof image, "kit:runtime/%d", r.session);
    }
    out[9].str = ctx->copy_string(ctx->value_storage, image);
    out[10].str = ctx->copy_string(ctx->value_storage, r.error);
    out[11].str = ctx->copy_string(ctx->value_storage, r.status);
    out[12].str = ctx->copy_string(ctx->value_storage, r.log);
}

static const char *model_snapshot_action(
    const behavior_function_context *c,
    void *s,
    const pin_value *in,
    pin_value *out
)
{
    workspace_model_bounds bounds;
    char error[256] = "";
    out[0].b = workspace_runtime_model_snapshot(s, in[0].i, in[1].i, in[2].i, &bounds, error, sizeof error);
    out[1].b = bounds.empty;
    for (int i = 0; i < 3; i++) {
        out[2 + i].f = bounds.min[i];
        out[5 + i].f = bounds.max[i];
    }
    out[8].str = c->copy_string(c->value_storage, error);
    return NULL;
}
static const char *model_inspection_action(
    const behavior_function_context *c,
    void *s,
    const pin_value *in,
    pin_value *out
)
{
    float normal[3] = { in[4].f, in[5].f, in[6].f };
    char error[256] = "";
    out[0].b = workspace_runtime_model_inspection(
        s,
        in[0].i,
        in[1].i,
        in[2].i,
        in[3].b,
        normal,
        in[7].f,
        error,
        sizeof error
    );
    out[1].str = c->copy_string(c->value_storage, error);
    return NULL;
}
static const char *model_parameter_action(
    const behavior_function_context *c,
    void *s,
    const pin_value *in,
    pin_value *out
)
{
    workspace_model_parameter_edit edit = {
        .slot = in[2].i,
        .write = in[3].b,
        .node = in[4].u64,
        .asset = in[5].str,
        .scope = in[6].str,
        .width = in[7].i,
        .values = { in[8].f, in[9].f, in[10].f, in[11].f }
    };
    workspace_model_parameter row;
    char error[256] = "";
    out[0].b = workspace_runtime_model_parameter(
        s,
        in[0].i,
        in[1].i,
        &edit,
        c->value_storage,
        &row,
        error,
        sizeof error
    );
    out[1].b = row.found;
    out[2].str = row.name;
    out[3].str = row.asset;
    out[4].str = row.scope;
    out[5].u64 = row.node;
    out[6].str = row.type_name;
    out[7].b = row.hidden;
    out[8].i = row.width;
    for (int i = 0; i < 4; i++) {
        out[9 + i].f = row.values[i];
        out[13 + i].f = row.minimum[i];
        out[17 + i].f = row.maximum[i];
    }
    out[21].str = c->copy_string(c->value_storage, error);
    return NULL;
}
static const char *runtime_trace(
    const behavior_function_context *c,
    void *s,
    const pin_value *in,
    pin_value *out
)
{
    char *json = NULL, error[256] = "";
    out[0].b = workspace_runtime_trace(s, in[0].i, &json, error, sizeof error);
    out[1].str = c->copy_string(c->value_storage, json ? json : "");
    out[2].str = c->copy_string(c->value_storage, error);
    free(json);
    return NULL;
}
static const char *model_replay_action(
    const behavior_function_context *c,
    void *s,
    const pin_value *in,
    pin_value *out
)
{
    char error[256] = "";
    out[0].b = workspace_runtime_model_replay(s, in[0].i, in[1].str, error, sizeof error);
    out[1].str = c->copy_string(c->value_storage, error);
    return NULL;
}
static const char *prepare(const behavior_function_context *c, void *s, const pin_value *in, pin_value *out)
{
    outputs(c, out, workspace_runtime_prepare(s, in[0].i, in[1].i));
    return NULL;
}
static const char *object_subject(
    const behavior_function_context *c,
    void *s,
    const pin_value *in,
    pin_value *out
)
{
    char subject[64], error[256] = "";
    out[0].b = workspace_runtime_object_subject(s, in[0].i, in[1].i, subject, error, sizeof error);
    out[1].str = c->copy_string(c->value_storage, subject);
    out[2].str = c->copy_string(c->value_storage, error);
    return NULL;
}
static const char *subject_origin(
    const behavior_function_context *c,
    void *s,
    const pin_value *in,
    pin_value *out
)
{
    workspace_runtime_subject_info info;
    char error[256] = "";
    out[0].b = workspace_runtime_subject_origin(s, in[0].i, in[1].str, &info, error, sizeof error);
    out[1].i = info.object;
    out[2].str = c->copy_string(c->value_storage, info.prototype ? info.prototype : "");
    out[3].u64 = info.creation;
    out[4].b = info.spawned;
    out[5].b = info.authored;
    out[6].b = info.known;
    out[7].str = c->copy_string(c->value_storage, info.path ? info.path : "");
    out[8].str = c->copy_string(c->value_storage, info.source_path ? info.source_path : "");
    out[9].str = c->copy_string(c->value_storage, error);
    free(info.source_path);
    free(info.prototype);
    free(info.path);
    return NULL;
}
static const char *prepare_document_sources(
    const behavior_function_context *c,
    void *s,
    const pin_value *in,
    pin_value *out
)
{
    const pin_array *path_rows = in[3].ptr, *source_rows = in[4].ptr;
    const char *paths[16], *sources[16];
    int count = path_rows ? path_rows->count : -1;
    if (!source_rows || count < 0 || count > 16 || source_rows->count != count) {
        workspace_runtime_result result = workspace_runtime_current(s);
        result.ok = false;
        result.error = "Private paths and sources require matching arrays of "
                       "at most 16 rows";
        outputs(c, out, result);
        return NULL;
    }
    for (int i = 0; i < count; i++) {
        paths[i] = path_rows->items[i].str;
        sources[i] = source_rows->items[i].str;
    }
    outputs(
        c,
        out,
        workspace_runtime_prepare_document_sources(s, in[0].i, in[1].i, in[2].str, paths, sources, count)
    );
    return NULL;
}
static const char *prepare_document_scene_source(
    const behavior_function_context *c,
    void *s,
    const pin_value *in,
    pin_value *out
)
{
    outputs(
        c,
        out,
        workspace_runtime_prepare_document_scene_source(
            s,
            in[0].i,
            in[1].i,
            in[2].str,
            in[3].str
        )
    );
    return NULL;
}
static const char *poll(const behavior_function_context *c, void *s, const pin_value *, pin_value *out)
{
    outputs(c, out, workspace_runtime_poll(s));
    return NULL;
}
static const char *cancel(const behavior_function_context *c, void *s, const pin_value *, pin_value *out)
{
    outputs(c, out, workspace_runtime_cancel(s));
    return NULL;
}
static const char *control(const behavior_function_context *c, void *s, const pin_value *in, pin_value *out)
{
    outputs(c, out, workspace_runtime_control(s, in[0].i, in[1].str));
    return NULL;
}
static const char *draw(const behavior_function_context *c, void *s, const pin_value *in, pin_value *out)
{
    outputs(c, out, workspace_runtime_draw(s, in[0].i, in[1].i, in[2].i, in[3].f));
    return NULL;
}
static const char *input(const behavior_function_context *c, void *s, const pin_value *in, pin_value *out)
{
    outputs(
        c,
        out,
        workspace_runtime_input(s, in[0].i, in[1].str, in[2].f, in[3].b)
    );
    return NULL;
}
static const char *view(const behavior_function_context *c, void *s, const pin_value *in, pin_value *out)
{
    engine_viewport_view v = { .center_x = in[1].f,
                               .center_y = in[2].f,
                               .width = in[3].f,
                               .height = in[4].f,
                               .yaw = in[5].f,
                               .pitch = in[6].f,
                               .target_elevation = in[7].f,
                               .focal_distance = in[10].f };
    out[0].b = isfinite(in[9].f) && in[9].f >= 0 && in[9].f <= 1e6f &&
               workspace_runtime_set_view(s, in[0].i, &v, in[8].b) &&
               workspace_runtime_grid(s, in[0].i, in[9].f);
    out[1].str = c->copy_string(c->value_storage, out[0].b ? "" : "Invalid runtime view or stale session");
    return NULL;
}
static const char *pick(const behavior_function_context *c, void *s, const pin_value *in, pin_value *out)
{
    char error[512] = "";
    int object = -1;
    char subject[64] = "";
    out[0].b = workspace_runtime_pick_subject(s, in[0].i, in[1].f, in[2].f, &object, subject, error, sizeof error);
    out[1].i = object;
    out[2].str = c->copy_string(c->value_storage, error);
    out[3].str = c->copy_string(c->value_storage, subject);
    return NULL;
}
static const char *skip(const behavior_function_context *c, void *s, const pin_value *in, pin_value *out)
{
    const pin_array *array = in[1].ptr;
    int count = array ? array->count : 0;
    int objects[4096];
    bool valid = count >= 0 && count <= 4096;
    if (valid) {
        for (int i = 0; i < count; i++) {
            objects[i] = array->items[i].i;
        }
    }
    out[0].b = valid && workspace_runtime_skip(s, in[0].i, objects, count, in[2].b);
    out[1].str = c->copy_string(c->value_storage, out[0].b ? "" : "Invalid runtime object exclusions or stale session");
    return NULL;
}
static pin_array *rows(const behavior_function_context *c, pin_type type, int count)
{
    pin_array *a = arena_alloc(c->value_storage, sizeof *a);
    *a = (pin_array){ .element_type = type,
                      .type_name = type == PIN_STR ? "const char *" : "int",
                      .count = count,
                      .capacity = count };
    a->items = count ? arena_alloc(c->value_storage, (size_t)count * sizeof *a->items)
                     : NULL;
    return a;
}
static const char *objects(const behavior_function_context *c, void *host, const pin_value *in, pin_value *out)
{
    workspace_runtime *s = host;
    workspace_runtime_result r = workspace_runtime_current(s);
    const scene_instance *instance = workspace_runtime_scene(s);
    const scene *scene = instance ? scene_instance_scene(instance) : NULL;
    int count = scene ? scene_object_count(scene) : 0;
    bool valid = r.ready && r.session == in[0].i && count <= 4096;
    pin_array *names = rows(c, PIN_STR, valid ? count : 0),
              *parents = rows(c, PIN_I32, valid ? count : 0);
    for (int i = 0; i < names->count; i++) {
        names->items[i] = (pin_value){
            .type = PIN_STR,
            .str = c->copy_string(c->value_storage, scene_object_name(scene, i))
        };
        parents->items[i] = (pin_value){ .type = PIN_I32,
                                         .i = scene_object_parent(scene, i) };
    }
    out[4].b = scene && scene_perspective(scene);
    out[0].b = valid;
    out[1].ptr = names;
    out[2].ptr = parents;
    out[3].str = c->copy_string(c->value_storage, valid ? "" : "Stale session or runtime object count exceeds 4096");
    return NULL;
}
static const char *bounds(const behavior_function_context *c, void *s, const pin_value *in, pin_value *out)
{
    const pin_array *a = in[1].ptr;
    int count = a ? a->count : 0;
    int indices[4096];
    bool valid = count >= 0 && count <= 4096;
    if (valid) {
        for (int i = 0; i < count; i++) {
            indices[i] = a->items[i].i;
        }
    }
    CF_V3 min = { 0 }, max = { 0 };
    char error[512] = "";
    out[0].b = valid &&
               workspace_runtime_bounds(s, in[0].i, indices, count, &min, &max, error, sizeof error);
    out[1].f = min.x;
    out[2].f = min.y;
    out[3].f = min.z;
    out[4].f = max.x;
    out[5].f = max.y;
    out[6].f = max.z;
    out[7].str = c->copy_string(c->value_storage, valid ? error : "Bounds query exceeds 4096 indices");
    return NULL;
}
static const char *value_snapshot(
    const behavior_function_context *c,
    void *s,
    const pin_value *in,
    pin_value *out
)
{
    char *json = NULL;
    char error[512] = "";
    out[0].b = workspace_runtime_value(s, in[0].i, in[1].i, in[2].str, in[3].str, &json, error, sizeof error);
    out[1].str = c->copy_string(c->value_storage, json ? json : "");
    out[2].str = c->copy_string(c->value_storage, error);
    free(json);
    return NULL;
}
static const char *number(const behavior_function_context *c, void *s, const pin_value *in, pin_value *out)
{
    double value = 0;
    char error[512] = "";
    out[0].b = workspace_runtime_number(
        s,
        in[0].i,
        in[1].i,
        in[2].str,
        in[3].str,
        in[4].b,
        in[5].d,
        &value,
        error,
        sizeof error
    );
    out[1].d = value;
    out[2].str = c->copy_string(c->value_storage, error);
    return NULL;
}
static void evidence_output(const behavior_function_context *c, pin_value *out, workspace_runtime_evidence_result r)
{
    out[0].b = r.ok;
    out[1].b = r.pending;
    out[2].i = r.job;
    out[3].str = c->copy_string(c->value_storage, r.state);
    out[4].str = c->copy_string(c->value_storage, r.directory);
    out[5].str = c->copy_string(c->value_storage, r.error);
}
static const char *evidence_begin(
    const behavior_function_context *c,
    void *s,
    const pin_value *in,
    pin_value *out
)
{
    evidence_output(c, out, workspace_runtime_evidence_begin(s, in[0].i, in[1].str));
    return NULL;
}
static const char *evidence_poll(
    const behavior_function_context *c,
    void *s,
    const pin_value *in,
    pin_value *out
)
{
    evidence_output(c, out, workspace_runtime_evidence_poll(s, in[0].i));
    return NULL;
}
static const char *evidence_cancel(
    const behavior_function_context *c,
    void *s,
    const pin_value *in,
    pin_value *out
)
{
    evidence_output(c, out, workspace_runtime_evidence_cancel(s, in[0].i));
    return NULL;
}
static const char *presentation(
    const behavior_function_context *c,
    void *s,
    const pin_value *in,
    pin_value *out
)
{
    out[0].b = workspace_runtime_presentation(
        s,
        in[0].i,
        in[1].b,
        in[2].b,
        in[3].b,
        in[4].b
    );
    render_debug_geometry_result r = workspace_runtime_debug_geometry_status(s);
    out[1].i = r.unsupported;
    out[2].b = r.truncated;
    out[3].b = r.invalid;
    out[4].str = c->copy_string(c->value_storage, out[0].b ? "" : "Runtime presentation session is stale");
    return NULL;
}
static const char *camera(const behavior_function_context *c, void *s, const pin_value *in, pin_value *out)
{
    engine_viewport_view v = { 0 };
    char error[512] = "";
    int code = workspace_runtime_camera(s, in[0].i, &v, error, sizeof error);
    workspace_runtime_result state = workspace_runtime_current(s);
    const scene_instance *instance = workspace_runtime_scene(s);
    bool perspective = instance && scene_perspective(scene_instance_scene(instance));
    bool ok = code >= 0;
    if (in[1].b) {
        ok = code == 1 &&
             workspace_runtime_apply_camera(s, in[0].i, error, sizeof error);
    }
    out[0].b = ok;
    out[1].b = code == 1;
    if (code < 0) {
        v = (engine_viewport_view){ 0 };
    }
    const float values[] = {
        v.center_x,       v.center_y,         v.width,
        v.height,         v.focal_distance,   v.yaw,
        v.pitch,          v.target_elevation, v.roll,
        v.lens.eye.x,     v.lens.eye.y,       v.lens.eye.z,
        v.lens.forward.x, v.lens.forward.y,   v.lens.forward.z,
        v.lens.up.x,      v.lens.up.y,        v.lens.up.z,
        v.lens.fov,       v.lens.near_clip,   v.lens.far_clip
    };
    for (int i = 0; i < 21; i++) {
        out[i + 2].f = values[i];
    }
    out[23].b = state.ready && perspective;
    out[24].str = c->copy_string(c->value_storage, error);
    return NULL;
}
static const char *ray(const behavior_function_context *c, void *s, const pin_value *in, pin_value *out)
{
    CF_Ray3 r = { 0 };
    char error[512] = "";
    out[0].b = workspace_runtime_ray(s, in[0].i, (CF_V2){ in[1].f, in[2].f }, &r, error, sizeof error);
    out[1].f = r.p.x;
    out[2].f = r.p.y;
    out[3].f = r.p.z;
    out[4].f = r.d.x;
    out[5].f = r.d.y;
    out[6].f = r.d.z;
    out[7].str = c->copy_string(c->value_storage, error);
    return NULL;
}
static const char *project(const behavior_function_context *c, void *s, const pin_value *in, pin_value *out)
{
    CF_V3 point = { 0 };
    bool visible = false;
    char error[512] = "";
    out[0].b = workspace_runtime_project(
        s,
        in[0].i,
        (CF_V3){ in[1].f, in[2].f, in[3].f },
        &point,
        &visible,
        error,
        sizeof error
    );
    out[1].f = point.x;
    out[2].f = point.y;
    out[3].f = point.z;
    out[4].b = visible;
    out[5].str = c->copy_string(c->value_storage, error);
    return NULL;
}
static const char *subject_resolve(
    const behavior_function_context *c,
    void *s,
    const pin_value *in,
    pin_value *out
)
{
    char error[512] = "", subject[64] = "";
    out[0].b = workspace_runtime_subject(s, in[0].i, in[1].u64, subject, error, sizeof error);
    out[1].str = c->copy_string(c->value_storage, subject);
    out[2].str = c->copy_string(c->value_storage, error);
    return NULL;
}
static const char *component_presence(
    const behavior_function_context *c,
    void *s,
    const pin_value *in,
    pin_value *out
)
{
    char error[512] = "";
    bool present = false;
    out[0].b = workspace_runtime_component(s, in[0].i, in[1].i, in[2].str, &present, error, sizeof error);
    out[1].b = present;
    out[2].str = c->copy_string(c->value_storage, error);
    return NULL;
}
static const char *subject_properties(
    const behavior_function_context *c,
    void *s,
    const pin_value *in,
    pin_value *out
)
{
    char error[512] = "", *name = NULL;
    pin_array *fields, *choices;
    int total = 0;
    out[0].b = workspace_runtime_subject_properties(
        s,
        in[0].i,
        in[1].str,
        in[2].i,
        in[3].i,
        c->value_storage,
        &fields,
        &choices,
        &total,
        &name,
        error,
        sizeof error
    );
    out[1].str = name ? name : c->copy_string(c->value_storage, "");
    out[2].ptr = fields;
    out[3].ptr = choices;
    out[4].i = total;
    out[5].str = c->copy_string(c->value_storage, error);
    return NULL;
}
static const char *subject_write(
    const behavior_function_context *c,
    void *s,
    const pin_value *in,
    pin_value *out
)
{
    char error[512] = "";
    out[0].b = workspace_runtime_subject_write(
        s,
        in[0].i,
        in[1].str,
        in[2].str,
        in[3].str,
        in[4].str,
        error,
        sizeof error
    );
    out[1].str = c->copy_string(c->value_storage, error);
    return NULL;
}
#define OPS(X)                                                                 \
    X("Workspace Runtime Presentation", presentation)                          \
    X("Workspace Runtime Camera", camera)                                      \
    X("Workspace Runtime Ray", ray)                                            \
    X("Workspace Runtime Project", project)                                    \
    X("Capture Workspace Runtime Evidence", evidence_begin)                    \
    X("Poll Workspace Runtime Evidence", evidence_poll)                        \
    X("Cancel Workspace Runtime Evidence", evidence_cancel)                    \
    X("Workspace Runtime Value", value_snapshot)                               \
    X("Workspace Runtime Number", number)                                      \
    X("Bounds Workspace Runtime", bounds)                                      \
    X("Inspect Runtime Trace", runtime_trace)                                  \
    X("Replay Runtime Model Action", model_replay_action)                      \
    X("Runtime Model Snapshot", model_snapshot_action)                         \
    X("Runtime Model Inspection", model_inspection_action)                     \
    X("Runtime Model Parameter", model_parameter_action)                       \
    X("Prepare Workspace Runtime", prepare)                                    \
    X("Prepare Workspace Runtime Scene Source", prepare_document_scene_source) \
    X("Prepare Workspace Runtime Sources", prepare_document_sources)           \
    X("Inspect Workspace Runtime Subject Origin", subject_origin)              \
    X("Resolve Workspace Runtime Object Subject", object_subject)              \
    X("Poll Workspace Runtime", poll)                                          \
    X("Cancel Workspace Runtime", cancel)                                      \
    X("Control Workspace Runtime", control)                                    \
    X("Draw Workspace Runtime", draw)                                          \
    X("View Workspace Runtime", view)                                          \
    X("Pick Workspace Runtime", pick)                                          \
    X("Skip Workspace Runtime", skip)                                          \
    X("Input Workspace Runtime", input)                                        \
    X("Inspect Workspace Runtime Objects", objects)                            \
    X("Inspect Workspace Runtime Subject", subject_properties)                 \
    X("Write Workspace Runtime Subject", subject_write)                        \
    X("Query Workspace Runtime Component", component_presence)                 \
    X("Resolve Workspace Runtime Subject", subject_resolve)
static void unavailable(const behavior_function_context *c, pin_value *out, const char *name)
{
    const native_capability_definition *definition = NULL;
    for (int i = 0; i < WORKSPACE_RUNTIME_OPERATION_COUNT; i++) {
        if (!strcmp(WORKSPACE_RUNTIME_DEFINITIONS[i].canonical_name, name)) {
            definition = &WORKSPACE_RUNTIME_DEFINITIONS[i];
            break;
        }
    }
    assert(definition);
    int slot = 0;
    for (int i = 0; i < definition->output_count; i++) {
        const native_parameter_definition *p = &definition->outputs[i];
        if (p->kind == NATIVE_PARAMETER_EXEC) {
            continue;
        }
        pin_value *value = &out[slot++];
        *value = (pin_value){ .type = p->storage, .container = p->container };
        if (p->container == PIN_CONTAINER_ARRAY) {
            value->ptr = rows(c, p->storage, 0);
            ((pin_array *)value->ptr)->type_name = p->type_name;
        } else if (p->storage == PIN_STR) {
            const char
                *text = !strcmp(p->name, "Error")
                          ? "Workspace runtime unavailable: no active project"
                      : (!strcmp(p->name, "Status") || !strcmp(p->name, "State"))
                          ? "unavailable"
                          : "";
            value->str = c->copy_string(c->value_storage, text);
        } else if (p->storage == PIN_I32 && !strcmp(p->name, "Object")) {
            value->i = -1;
        }
    }
}
#define CALL(N, F)                                                             \
    static const char *F##_dispatch(                                           \
        const behavior_function_context *c,                                    \
        void *host,                                                            \
        const pin_value *in,                                                   \
        pin_value *out                                                         \
    )                                                                          \
    {                                                                          \
        if (!host) {                                                           \
            unavailable(c, out, N);                                            \
            return NULL;                                                       \
        }                                                                      \
        return F(c, host, in, out);                                            \
    }                                                                          \
    static const node_host_call F##_call = {                                   \
        .service = "workspace_runtime",                                        \
        .missing_service = "Workspace runtime requires an authoring host",     \
        .optional_service = true,                                              \
        .context = F##_dispatch                                                \
    };
OPS(CALL)
static const node_access ACCESS = { .flags = NODE_EFFECT_HOST_SINK };
#define BIND(N, F)                                                             \
    { .canonical_name = N,                                                     \
      .implementation = { .call = &F##_call, .access = &ACCESS } },
static const native_definition_binding BINDINGS[] = { OPS(BIND) };
static const type_desc *const TYPES[] = { &RUNTIME_PROPERTY_LAYOUT,
                                          &RUNTIME_PROPERTY_CHOICE_LAYOUT };
const native_definition_pack WORKSPACE_RUNTIME_DEFINITION_PACK = {
    .definitions = WORKSPACE_RUNTIME_DEFINITIONS,
    .definition_count = WORKSPACE_RUNTIME_OPERATION_COUNT,
    .bindings = BINDINGS,
    .binding_count = NATIVE_COUNT(BINDINGS),
    .layouts = TYPES,
    .layout_count = NATIVE_COUNT(TYPES)
};
