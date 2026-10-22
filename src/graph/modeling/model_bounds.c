#include "graph/model/value_copy.h"
#include "graph/modeling/model_internal.h"
#include "runtime/shaders/volume.h"
#include "support/xalloc.h"

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static node_desc *const descriptors[] = { &NODE_ModelSphere,
                                          &NODE_ModelBox,
                                          &NODE_ModelRoundedBox,
                                          &NODE_ModelCylinder,
                                          &NODE_ModelCapsule,
                                          &NODE_ModelTorus,
                                          &NODE_ModelTranslate,
                                          &NODE_ModelRotate,
                                          &NODE_ModelScale,
                                          &NODE_ModelReflect,
                                          &NODE_ModelUnion,
                                          &NODE_ModelIntersection,
                                          &NODE_ModelDifference,
                                          &NODE_ModelSmoothUnion,
                                          &NODE_ModelOutput,
                                          &NODE_ModelPart,
                                          &NODE_ModelProfileCircle,
                                          &NODE_ModelProfileRectangle,
                                          &NODE_ModelProfilePolygon,
                                          &NODE_ModelExtrude,
                                          &NODE_ModelRevolve,
                                          &NODE_ModelProfileOffset,
                                          &NODE_ModelProfileUnion,
                                          &NODE_ModelProfileIntersection,
                                          &NODE_ModelProfileDifference,
                                          &NODE_ModelRoundedExtrude,
                                          &NODE_ModelBeveledExtrude,
                                          &NODE_ModelOffset,
                                          &NODE_ModelShell,
                                          &NODE_ModelTubeSweep,
                                          &NODE_ModelSampledSdf };
int model_operation(const node *n)
{
    for (int i = 0; i < (int)(sizeof descriptors / sizeof *descriptors); i++) {
        if (n->desc == descriptors[i]) {
            return i;
        }
    }
    return -1;
}
static bool geometry_pin(const pin_spec *pin)
{
    return pin->kind == PIN_DATA && pin->type == PIN_HANDLE &&
           pin->container == PIN_CONTAINER_SINGLE && pin->type_name &&
           (!strcmp(pin->type_name, "Geometry3D") ||
            !strcmp(pin->type_name, "Profile2D")) &&
           !pin->binding && pin->default_value.u64 == 0;
}
float model_number(const node *n, int pin)
{
    return n->input_pins[pin].default_value.f;
}
void model_vector(const node *n, int pin, float out[3])
{
    memset(out, 0, 3 * sizeof *out);
    if (n->input_pins[pin].default_value.ptr) {
        memcpy(
            out,
            n->input_pins[pin].default_value.ptr,
            (!strcmp(n->input_pins[pin].type_name, "ShaderVec2") ? 2 : 3) * sizeof *out
        );
    }
}
bool model_validate_node(const graph *g, const node *n, shader_error *error)
{
    int op = model_operation(n);
    if (op < 0) {
        return model_fail(error, n->id, "Unsupported model construction node");
    }
    if (n->disabled || n->input_count != n->desc->input_count ||
        n->output_count != n->desc->output_count) {
        return model_fail(error, n->id, "Model construction has an invalid or disabled node contract");
    }
    for (int i = 0; i < n->input_count; i++) {
        const pin_spec *p = &n->input_pins[i], *declared = &n->desc->inputs[i];
        node *from;
        int from_pin;
        bool connected = graph_input_source(g, n, i, &from, &from_pin);
        if (n->desc == &NODE_ModelPart && i == 1) {
            if (p->type != PIN_HANDLE || !p->type_name ||
                strcmp(p->type_name, "ModelMaterialSlot") ||
                p->container != PIN_CONTAINER_SINGLE || p->binding ||
                p->default_value.u64 || connected) {
                return model_fail(error, n->id, "Compiled Part material binding must be resolved metadata");
            }
            continue;
        }
        if (declared->type == PIN_HANDLE) {
            if (!geometry_pin(p) || !connected || from_pin != 0 ||
                from->output_count != 1 || !geometry_pin(&from->output_pins[0]) ||
                strcmp(p->type_name, declared->type_name) ||
                strcmp(from->output_pins[0].type_name, declared->type_name) ||
                graph_input_conversion(g, n, i)) {
                return model_fail(error, n->id, "Model Geometry is required as an unevaluated Geometry3D wire");
            }
            continue;
        }
        if (declared->container == PIN_CONTAINER_ARRAY) {
            if (p->binding || connected || p->type != PIN_STRUCT ||
                !p->type_name || strcmp(p->type_name, declared->type_name) ||
                p->container != PIN_CONTAINER_ARRAY) {
                return model_fail(error, n->id, "Polygon Vertices require a literal structural ShaderVec2 array");
            }
            continue;
        }
        if (p->binding || connected || p->type != declared->type ||
            p->container != PIN_CONTAINER_SINGLE) {
            return model_fail(error, n->id, "Model dimensions and transforms require typed literal values");
        }
        if (p->type == PIN_STRUCT) {
            if (!p->type_name || strcmp(p->type_name, declared->type_name)) {
                return model_fail(error, n->id, "Model vector requires ShaderVec3");
            }
            float v[3];
            model_vector(n, i, v);
            for (int axis = 0; axis < (!strcmp(p->type_name, "ShaderVec2") ? 2 : 3);
                 axis++) {
                if (!isfinite(v[axis])) {
                    return model_fail(error, n->id, "Model vector values must be finite");
                }
            }
        }
        if (p->type == PIN_F32 && !isfinite(model_number(n, i))) {
            return model_fail(error, n->id, "Model dimensions must be finite and positive");
        }
    }
    if (n->output_count &&
        (!geometry_pin(&n->output_pins[0]) ||
         strcmp(n->output_pins[0].type_name, n->desc->outputs[0].type_name))) {
        return model_fail(error, n->id, "Model construction requires a Geometry3D output");
    }
    if (op == MODEL_SAMPLED_SDF) {
        shader_node spec = { .parameter = n->input_pins[2].default_value.i,
                             .volume_range = model_number(n, 3),
                             .volume_lipschitz = model_number(n, 4) };
        model_vector(n, 1, spec.value);
        const char *key = graph_text_cstring(g, n->input_pins[0].default_value);
        const char *base = key ? strrchr(key, '/') : NULL;
        bool identity = base && strlen(base + 1) == 24 &&
                        !strcmp(base + 17, ".sdf.png");
        for (int i = 1; identity && i <= 16; i++) {
            identity = (base[i] >= '0' && base[i] <= '9') ||
                       (base[i] >= 'a' && base[i] <= 'f');
        }
        if (!shader_volume_contract(&spec) || !key ||
            strncmp(key, "content/", 8) || strstr(key, "..") || !identity) {
            return model_fail(error, n->id, "Sampled SDF needs finite metadata and a content-relative .sdf.png atlas");
        }
    }
    if (op == MODEL_SPHERE) {
        if (model_number(n, 0) <= 0) {
            return model_fail(error, n->id, "Model Sphere Radius must be finite and positive");
        }
        if (model_number(n, 0) >= sqrtf(FLT_MAX / 3.0f)) {
            return model_fail(error, n->id, "Model Sphere Radius exceeds the finite local distance range");
        }
    }
    if (op == MODEL_BOX || op == MODEL_ROUNDED_BOX || op == MODEL_SCALE) {
        float v[3];
        model_vector(n, op == MODEL_SCALE ? 1 : 0, v);
        float minimum = fminf(v[0], fminf(v[1], v[2]));
        if (minimum <= 0) {
            return model_fail(error, n->id, "Model half sizes and scale Factors must be positive");
        }
        if (op == MODEL_ROUNDED_BOX &&
            (model_number(n, 1) < 0 || model_number(n, 1) > minimum)) {
            return model_fail(error, n->id, "Rounded Box Radius must lie between zero and the smallest outer half size");
        }
    }
    if ((op == MODEL_CYLINDER || op == MODEL_CAPSULE || op == MODEL_TORUS) &&
        (model_number(n, 0) <= 0 || model_number(n, 1) < 0 ||
         (op != MODEL_CAPSULE && model_number(n, 1) == 0))) {
        return model_fail(error, n->id, "Model radii and cylinder half height must be positive; capsule Half Segment may be zero");
    }
    if (op == MODEL_TORUS && model_number(n, 0) <= model_number(n, 1)) {
        return model_fail(error, n->id, "Torus Major Radius must exceed Minor Radius");
    }
    if (op == MODEL_REFLECT && (n->input_pins[1].default_value.e < 0 ||
                                n->input_pins[1].default_value.e > 2)) {
        return model_fail(error, n->id, "Model Reflect Axis must be X, Y or Z");
    }
    if (op == MODEL_SMOOTH_UNION && model_number(n, 2) <= 0) {
        return model_fail(error, n->id, "Smooth Union Smoothness must be positive");
    }
    return model_profile_validate(n, error) && model_constructive_validate(n, error);
}
const node *model_child(const graph *g, const node *n, int pin)
{
    node *from;
    int from_pin;
    graph_input_source(g, n, pin, &from, &from_pin);
    return from;
}
/* Rz * Ry * Rx: forward construction rotation; inverse evaluation transposes it. */
bool model_enter(compiler *c, const node *n)
{
    for (int i = 0; i < c->depth; i++) {
        if (c->stack[i] == n) {
            return model_fail(c->error, n->id, "Model construction contains a geometry cycle");
        }
    }
    if (c->depth == MODEL_DEPTH_MAX) {
        return model_fail(c->error, n->id, "Model construction exceeds the supported depth of 128");
    }
    c->stack[c->depth++] = n;
    return true;
}
