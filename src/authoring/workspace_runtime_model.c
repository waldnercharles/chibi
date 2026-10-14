#include "authoring/workspace_runtime_model.h"

#include "authoring/workspace_runtime_model_internal.h"
#include "render/shader_catalog.h"
#include "render/shader_publish.h"
#include "runtime/component_catalog.h"
#include "support/array.h"
#include "support/asset_json.h"

#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct
{
    render_resource_scope render;
    declared_components_scope types;
    declared_components *components;
} model_scope;
static bool model_enter(render_resource_context *context, model_scope *scope, char *error, size_t size)
{
    if (!render_resource_scope_enter(context, &scope->render, error, size)) {
        return false;
    }
    if (declared_components_scope_enter(scope->components, &scope->types)) {
        return true;
    }
    if (!render_resource_scope_leave(&scope->render, error, size)) {
        abort();
    }
    if (size) {
        snprintf(error, size, "Cannot enter runtime type reflection");
    }
    return false;
}
static bool model_leave(model_scope *scope, char *error, size_t size)
{
    if (!declared_components_scope_leave(&scope->types)) {
        if (size) {
            snprintf(error, size, "Cannot restore runtime type reflection");
        }
        return false;
    }
    return render_resource_scope_leave(&scope->render, error, size);
}
typedef struct
{
    model_scope scope;
    ecs_world *world;
    ecs_entity entity;
    const Shader *binding;
    int shader;
} model_target;
static bool model_refuse(char *error, size_t size, const char *text)
{
    if (size) {
        snprintf(error, size, "%s", text);
    }
    return false;
}
static bool model_open(
    workspace_runtime *s,
    int session,
    int object,
    model_target *target,
    char *error,
    size_t size
)
{
    *target = (model_target){ 0 };
    if (!s || !workspace_runtime_world(s) ||
        session != workspace_runtime_current(s).session) {
        return model_refuse(error, size, "Runtime model session is unavailable or stale");
    }
    const scene_instance *instance = workspace_runtime_scene(s);
    if (object < 0 || object >= scene_object_count(scene_instance_scene(instance))) {
        return model_refuse(error, size, "Runtime model object is unavailable");
    }
    target->scope.components = workspace_runtime_types(s);
    if (!model_enter(
            render_resource_context_for_world(workspace_runtime_world(s)),
            &target->scope,
            error,
            size
        )) {
        return false;
    }
    target->world = world_ecs(workspace_runtime_world(s));
    target->entity = scene_instance_entity(instance, object);
    target->binding = Shader_get_const(target->world, target->entity);
    target->shader = target->binding ? render_shader_find(target->binding->shape) : 0;
    if (!target->shader || !render_shader_is_model(target->shader)) {
        if (!model_leave(&target->scope, error, size)) {
            return false;
        }
        return model_refuse(error, size, "Runtime object has no applied model");
    }
    return true;
}
static av_doc *model_action(workspace_runtime *s, int object)
{
    av_doc *doc = av_doc_new();
    av_value *details = av_obj(doc);
    av_doc_set_root(doc, details);
    av_obj_add_int(doc, details, "object", object);
    av_obj_add_str(doc, details, "artifact_sha256", workspace_runtime_artifact_sha256(s));
    return doc;
}
static void model_record(workspace_runtime *s, int session, const char *operation, av_doc *doc)
{
    if (!workspace_runtime_record_call(s, session, operation, av_doc_root(doc))) {
        workspace_runtime_mark_external_change(s);
    }
    av_doc_free(doc);
}
bool workspace_runtime_model_snapshot(
    workspace_runtime *s,
    int session,
    int object,
    int part,
    workspace_model_bounds *out,
    char *error,
    size_t size
)
{
    *out = (workspace_model_bounds){ .empty = true };
    if (part < -1) {
        return model_refuse(error, size, "Model snapshot part must be -1 or an existing part index");
    }
    model_target target;
    if (!model_open(s, session, object, &target, error, size)) {
        return false;
    }
    model_bounds bounds = { 0 };
    shader_error problem = { 0 };
    float values[SHADER_PARAMETER_MAX][4];
    bool ok = part >= -1 && render_shader_model_snapshot(
                                target.shader,
                                target.world,
                                target.entity,
                                target.binding->parameters,
                                values,
                                bounds.min,
                                bounds.max,
                                &bounds.empty,
                                &problem
                            );
    if (ok && part >= 0) {
        ok = render_shader_model_part_bounds(target.shader, values, part, &bounds, &problem);
    }
    if (ok) {
        memcpy(out->min, bounds.min, sizeof out->min);
        memcpy(out->max, bounds.max, sizeof out->max);
        out->empty = bounds.empty;
    } else {
        model_refuse(error, size, problem.message);
    }
    return model_leave(&target.scope, error, size) && ok;
}
bool workspace_runtime_model_inspection(
    workspace_runtime *s,
    int session,
    int object,
    int part,
    bool section,
    const float normal[3],
    float offset,
    char *error,
    size_t size
)
{
    float length = 0;
    for (int i = 0; i < 3; i++) {
        if (!isfinite(normal[i])) {
            return model_refuse(error, size, "Model section normal must be finite");
        }
        length += normal[i] * normal[i];
    }
    if (!isfinite(offset) || fabsf(offset) > 1e6f || !isfinite(length) ||
        (section && fabsf(length - 1) > 1e-4f)) {
        return model_refuse(error, size, "Model section needs a unit normal and finite bounded offset");
    }
    model_target target;
    if (!model_open(s, session, object, &target, error, size)) {
        return false;
    }
    const model_inspection *info = render_shader_model_inspection(target.shader);
    bool ok = info && part >= -1 && part < acount(info->parts);
    if (ok) {
        workspace_runtime_model_present(s, object, part, section, normal, offset);
        av_doc *action = model_action(s, object);
        av_value *details = av_doc_root(action), *vector = av_arr(action);
        av_obj_add_int(action, details, "part", part);
        av_obj_add_bool(action, details, "section", section);
        for (int i = 0; i < 3; i++) {
            av_arr_append(vector, av_real(action, normal[i]));
        }
        av_obj_set(action, details, "normal", vector);
        av_obj_set(action, details, "offset", av_real(action, offset));
        model_record(s, session, "model-inspection", action);
    } else {
        model_refuse(error, size, "Model inspection part no longer exists");
    }
    return model_leave(&target.scope, error, size) && ok;
}
static const char *model_copy(arena *storage, const char *text)
{
    if (!text) {
        text = "";
    }
    char *copy = arena_alloc(storage, strlen(text) + 1);
    strcpy(copy, text);
    return copy;
}
bool workspace_runtime_model_parameter(
    workspace_runtime *s,
    int session,
    int object,
    const workspace_model_parameter_edit *edit,
    arena *storage,
    workspace_model_parameter *out,
    char *error,
    size_t size
)
{
    *out = (workspace_model_parameter){ .name = "", .asset = "", .scope = "", .type_name = "" };
    if (edit->slot < 0 || edit->slot >= SHADER_PARAMETER_MAX) {
        return model_refuse(error, size, "Model parameter slot must be in 0..31");
    }
    model_target target;
    if (!model_open(s, session, object, &target, error, size)) {
        return false;
    }
    model_parameter parameter;
    if (!render_shader_model_parameter(target.shader, edit->slot, &parameter)) {
        if (edit->write) {
            model_refuse(error, size, "Model parameter no longer exists");
        }
        return model_leave(&target.scope, error, size) && !edit->write;
    }
    assert(parameter.width >= 1 && parameter.width <= 4);
    *out = (workspace_model_parameter){
        .found = true,
        .name = model_copy(storage, parameter.name),
        .asset = model_copy(storage, parameter.asset),
        .scope = model_copy(storage, parameter.scope),
        .type_name = model_copy(storage, parameter.type_name),
        .node = parameter.node,
        .width = parameter.width,
        .hidden = parameter.hidden
    };
    memcpy(out->minimum, parameter.minimum, sizeof out->minimum);
    memcpy(out->maximum, parameter.maximum, sizeof out->maximum);
    float prepared[SHADER_PARAMETER_MAX][4], lo[3], hi[3];
    bool empty;
    shader_error problem = { 0 };
    bool ok = render_shader_model_snapshot(
        target.shader,
        target.world,
        target.entity,
        target.binding->parameters,
        prepared,
        lo,
        hi,
        &empty,
        &problem
    );
    if (!ok) {
        model_refuse(error, size, problem.message);
        return model_leave(&target.scope, error, size) && false;
    }
    memcpy(out->values, prepared[edit->slot], sizeof out->values);
    if (edit->write) {
        ok = !workspace_runtime_read_only(s) && edit->node == parameter.node &&
             edit->asset && !strcmp(edit->asset, parameter.asset) && edit->scope &&
             !strcmp(edit->scope, parameter.scope) && edit->width == parameter.width;
        if (!ok) {
            model_refuse(error, size, "Model parameter identity changed or session is read-only");
        } else {
            static const char *const vectors[] = { "", "", "ShaderVec2", "ShaderVec3", "ShaderVec4" };
            pin_value value = parameter.width == 1
                                ? (pin_value){ .type = PIN_F32, .f = edit->values[0] }
                                : (pin_value){ .type = PIN_STRUCT,
                                               .type_name = vectors[parameter.width],
                                               .ptr = (void *)edit->values };
            const char *binding_name = render_shader_parameter_name(target.shader, edit->slot);
            const char
                *problem = binding_name
                             ? render_shader_shape_parameter_set(
                                   target.world,
                                   target.entity,
                                   binding_name,
                                   value
                               )
                             : "Applied model parameter binding is unavailable";
            ok = !problem;
            if (problem) {
                model_refuse(error, size, problem);
            } else {
                memcpy(out->values, edit->values, sizeof out->values);
                av_doc *action = model_action(s, object);
                av_value *details = av_doc_root(action), *values = av_arr(action);
                av_obj_add_int(action, details, "slot", edit->slot);
                av_obj_add_uint(action, details, "node", parameter.node);
                av_obj_add_str(action, details, "asset", parameter.asset);
                av_obj_add_str(action, details, "scope", parameter.scope);
                av_obj_add_int(action, details, "width", parameter.width);
                for (int i = 0; i < 4; i++) {
                    av_arr_append(values, av_real(action, edit->values[i]));
                }
                av_obj_set(action, details, "values", values);
                model_record(s, session, "model-parameter", action);
            }
        }
    }
    return model_leave(&target.scope, error, size) && ok;
}

static bool replay_int(const av_value *object, const char *key, int min, int max, int *out)
{
    const av_value *value = av_obj_get(object, key);
    if (!av_is_int(value)) {
        return false;
    }
    double number = av_get_num(value);
    if (number < min || number > max) {
        return false;
    }
    *out = (int)number;
    return true;
}
static bool replay_vector(const av_value *object, const char *key, float *out, int count)
{
    const av_value *vector = av_obj_get(object, key);
    if (!av_is_arr(vector) || av_arr_size(vector) != (size_t)count) {
        return false;
    }
    for (int i = 0; i < count; i++) {
        const av_value *value = av_arr_get(vector, i);
        if (!av_is_num(value) || !isfinite(av_get_num(value)) ||
            fabs(av_get_num(value)) > FLT_MAX) {
            return false;
        }
        out[i] = (float)av_get_num(value);
    }
    return true;
}
bool workspace_runtime_model_replay(workspace_runtime *s, int session, const char *json, char *error, size_t size)
{
    if (!json || strlen(json) > 16384 ||
        !workspace_runtime_verify_artifact(s, session, error, size)) {
        return model_refuse(error, size, "Model replay requires a bounded action and the unchanged runtime artifact");
    }
    int depth = 0;
    bool quoted = false, escaped = false;
    for (const char *at = json; *at; at++) {
        if (quoted) {
            if (escaped) {
                escaped = false;
            } else if (*at == '\\') {
                escaped = true;
            } else if (*at == '"') {
                quoted = false;
            }
            continue;
        }
        if (*at == '"') {
            quoted = true;
        } else if (*at == '[' || *at == '{') {
            if (++depth > 8) {
                return model_refuse(error, size, "Model replay nesting exceeds eight levels");
            }
        } else if (*at == ']' || *at == '}') {
            depth--;
        }
    }
    av_doc *doc = av_json_read(json);
    const av_value *root = doc ? av_doc_root(doc) : NULL,
                   *details = av_obj_get(root, "details"),
                   *steps = av_obj_get(root, "fixed_steps");
    const char *operation = av_get_str(av_obj_get(root, "operation")),
               *artifact = av_get_str(av_obj_get(details, "artifact_sha256"));
    int object;
    bool valid = av_is_obj(root) && av_is_obj(details) && operation && artifact &&
                 !strcmp(artifact, workspace_runtime_artifact_sha256(s)) &&
                 av_is_int(steps) && !(av_is_sint(steps) && av_get_sint(steps) < 0) &&
                 av_get_uint(steps) == workspace_runtime_current(s).steps &&
                 replay_int(details, "object", 0, 4095, &object);
    bool ok = false;
    if (valid && !strcmp(operation, "model-inspection")) {
        int part;
        float normal[3];
        const av_value *section = av_obj_get(details, "section"),
                       *offset = av_obj_get(details, "offset");
        valid = replay_int(details, "part", -1, 4095, &part) && av_is_bool(section) &&
                av_is_num(offset) && replay_vector(details, "normal", normal, 3);
        if (valid) {
            ok = workspace_runtime_model_inspection(
                s,
                session,
                object,
                part,
                av_get_bool(section),
                normal,
                (float)av_get_num(offset),
                error,
                size
            );
        }
    } else if (valid && !strcmp(operation, "model-parameter")) {
        workspace_model_parameter_edit edit = { .write = true };
        const av_value *node = av_obj_get(details, "node");
        edit.asset = av_get_str(av_obj_get(details, "asset"));
        edit.scope = av_get_str(av_obj_get(details, "scope"));
        valid = replay_int(details, "slot", 0, 31, &edit.slot) &&
                replay_int(details, "width", 1, 4, &edit.width) && av_is_int(node) &&
                !(av_is_sint(node) && av_get_sint(node) < 0) && edit.asset &&
                edit.scope && replay_vector(details, "values", edit.values, 4);
        if (valid) {
            edit.node = av_get_uint(node);
            arena storage = { 0 };
            workspace_model_parameter out;
            ok = workspace_runtime_model_parameter(s, session, object, &edit, &storage, &out, error, size);
            arena_free(&storage);
        }
    } else {
        valid = false;
    }
    av_doc_free(doc);
    return valid ? ok : model_refuse(error, size, "Model replay action identity, timing or fields are invalid");
}
