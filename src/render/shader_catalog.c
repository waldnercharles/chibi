#include "render/shader_catalog.h"

#include "graph/document/serialize.h"
#include "graph/modeling/model_compile.h"
#include "graph/runtime/flow.h"
#include "graph/shader/shader_volume.h"
#include "render/mesh_asset.h"
#include "render/shader_images.h"
#include "runtime/generated.h"
#include "support/array.h"
#include "support/asset_text.h"
#include "support/xalloc.h"

#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
    MODEL_SNAPSHOT_CACHE_MAX = 32
};
typedef struct
{
    float incoming[32][4], prepared[32][4];
    model_bounds bounds;
} model_snapshot;
typedef struct
{
    ecs_world *world;
    ecs_entity entity;
    model_snapshot snapshot;
} model_caller;
typedef struct
{
    char *name;
    av_value *source_value;
    shader_graph graph;
    char *glsl; /* Structural GLSL used to detect topology changes. */
    int *images;
    char *names[SHADER_PARAMETER_MAX];
    int widths[SHADER_PARAMETER_MAX];
    float defaults[SHADER_PARAMETER_MAX][4], bounds[2];
    bool shape;
    bool is_model;
    model_program model;
    model_inspection inspection;
    shader_inspection material_inspection;
    behavior_asset *model_source;
    model_snapshot *snapshots;
    int next_snapshot;
    model_caller *callers;
    int *caller_slots;
} shader_asset;
typedef struct
{
    ecs_world *world;
    uint64_t entity;
    char *name;
    float value[4];
    int width, shader;
} shader_override;
static shader_asset *assets;
static shader_override *overrides;
static atomic_flag override_lock = ATOMIC_FLAG_INIT;
static unsigned revision = 1;
static unsigned ramp_revision = 1;
static unsigned parameter_revision = 1;
static render_model_stats model_stats;
static atomic_int model_setter_validations;

static bool model_material_contract_validate(
    const shader_asset *model,
    int replaced,
    const shader_asset *candidate,
    shader_error *error
);

static void append(char **text, const char *format, ...)
{
    va_list args, copy;
    va_start(args, format);
    va_copy(copy, args);
    int count = vsnprintf(NULL, 0, format, copy);
    va_end(copy);
    size_t old = *text ? strlen(*text) : 0;
    *text = xrealloc(*text, old + (size_t)count + 1);
    vsnprintf(*text + old, (size_t)count + 1, format, args);
    va_end(args);
}
int render_shader_find(const char *name)
{
    if (!name || !*name) {
        return 0;
    }
    for (int i = 0; i < acount(assets); i++) {
        if (!strcmp(name, assets[i].name)) {
            return i + 1;
        }
    }
    return 0;
}
const char *render_shader_parameter_name(int index, int slot)
{
    return index > 0 && index <= acount(assets) && slot >= 0 && slot < 32
             ? assets[index - 1].names[slot]
             : NULL;
}
int render_shader_parameter_width(int index, int slot)
{
    return render_shader_parameter_name(index, slot) ? assets[index - 1].widths[slot]
                                                     : 0;
}
bool render_shader_model_anchor(int index, int slot, model_anchor *out)
{
    if (!render_shader_is_model(index) || slot < 0 ||
        slot >= acount(assets[index - 1].model.anchors)) {
        return false;
    }
    *out = assets[index - 1].model.anchors[slot];
    return true;
}
bool render_shader_model_anchor_values(
    int index,
    int slot,
    const float prepared[SHADER_PARAMETER_MAX][4],
    float position[3],
    float angles[3],
    shader_error *error
)
{
    return render_shader_is_model(index) &&
           model_program_anchor(&assets[index - 1].model, slot, prepared, position, angles, error);
}
bool render_shader_model_part_bounds(
    int index,
    const float prepared[SHADER_PARAMETER_MAX][4],
    int part,
    model_bounds *out,
    shader_error *error
)
{
    return render_shader_is_model(index) &&
           model_program_part_bounds(&assets[index - 1].model, prepared, part, out, error);
}
bool render_shader_material_inspection(int index, shader_inspection *out)
{
    if (index <= 0 || index > acount(assets) || assets[index - 1].shape) {
        return false;
    }
    *out = assets[index - 1].material_inspection;
    return true;
}
const model_inspection *render_shader_model_inspection(int index)
{
    return render_shader_is_model(index) ? &assets[index - 1].inspection : NULL;
}
bool render_shader_model_parameter(int index, int slot, model_parameter *out)
{
    if (!render_shader_is_model(index)) {
        return false;
    }
    const model_program *program = &assets[index - 1].model;
    if (slot < 0 || slot >= acount(program->parameters) ||
        program->parameters[slot].hidden) {
        return false;
    }
    *out = program->parameters[slot];
    return true;
}
bool render_shader_model_material_slot(int index, int slot, model_material_slot *out)
{
    if (!render_shader_is_model(index)) {
        return false;
    }
    const model_program *program = &assets[index - 1].model;
    if (slot < 0 || slot >= acount(program->material_slots)) {
        return false;
    }
    *out = program->material_slots[slot];
    return true;
}
bool render_shader_model_preview_snapshot(
    int index,
    ecs_world *world,
    ecs_entity entity,
    const char *parameters,
    int part,
    model_bounds *bounds,
    shader_error *error
)
{
    float prepared[32][4];
    if (!render_shader_model_snapshot(
            index,
            world,
            entity,
            parameters,
            prepared,
            bounds->min,
            bounds->max,
            &bounds->empty,
            error
        )) {
        return false;
    }
    return part < 0 ||
           render_shader_model_preview_bounds(index, prepared, part, bounds, error);
}
bool render_shader_model_preview_factor(
    int index,
    const float prepared[32][4],
    int part,
    float *factor,
    shader_error *error
)
{
    if (!render_shader_is_model(index)) {
        return false;
    }
    float rows[3][3];
    if (!model_program_frame(&assets[index - 1].model, part, prepared, rows, error)) {
        return false;
    }
    double norm = 0;
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            norm += (double)rows[i][j] * rows[i][j];
        }
    }
    if (!isfinite(norm) || norm <= 0) {
        return false;
    }
    *factor = nextafterf((float)fmin(1 / sqrt(norm), FLT_MAX), 0);
    return true;
}
bool render_shader_model_preview_bounds(
    int index,
    const float prepared[32][4],
    int part,
    model_bounds *bounds,
    shader_error *error
)
{
    if (!render_shader_is_model(index)) {
        return false;
    }
    return model_program_part_bounds(&assets[index - 1].model, prepared, part, bounds, error);
}
char *render_shader_model_preview_glsl(int index, int part, shader_error *error)
{
    if (!render_shader_is_model(index)) {
        return NULL;
    }
    shader_graph distance, hit;
    shader_graph_init(&distance);
    shader_graph_init(&hit);
    if (!model_program_part_shaders(&assets[index - 1].model, part, &distance, &hit, error)) {
        return NULL;
    }
    char *d = shader_graph_glsl_named(&distance, "sg_preview_distance", error),
         *h = shader_graph_glsl_named(&hit, "sg_preview_hit", error);
    shader_graph_free(&distance);
    shader_graph_free(&hit);
    if (!d || !h) {
        free(d);
        free(h);
        return NULL;
    }
    char *result = NULL;
    for (int i = 0; i < acount(assets[index - 1].images); i++) {
        append(&result, "vec4 sg_preview_distance_image_%d(vec2 uv){return sg_asset_%d_image_%d(uv); }\n", i, index, i);
        append(&result, "vec4 sg_preview_hit_image_%d(vec2 uv){return sg_asset_%d_image_%d(uv); }\n", i, index, i);
    }
    append(&result, "%s%s", d, h);
    free(d);
    free(h);
    return result;
}
bool render_shader_model_hit(
    int index,
    const float prepared[32][4],
    const float object_position[3],
    model_hit *out,
    shader_error *error
)
{
    return render_shader_is_model(index) &&
           model_program_hit(&assets[index - 1].model, prepared, object_position, out, error);
}
static bool model_snapshot_prepare(
    int index,
    const float incoming[32][4],
    float prepared[32][4],
    model_bounds *bounds,
    shader_error *error
);
static int parameter_slot(const shader_asset *asset, const char *name)
{
    for (int i = 0; i < SHADER_PARAMETER_MAX; i++) {
        if (asset->names[i] && !strcmp(asset->names[i], name)) {
            return i;
        }
    }
    return -1;
}
bool render_shader_parameters_validate(int index, const char *text, shader_error *error)
{
    if (render_shader_is_model(index)) {
        GameShader binding = { .shape = assets[index - 1].name };
        return render_shader_binding_parameters_validate(&binding, text, error);
    }
    if (error) {
        *error = (shader_error){ 0 };
    }
    if (!text || !*text) {
        return true;
    }
    if (index <= 0 || index > acount(assets)) {
        if (error) {
            snprintf(error->message, sizeof error->message, "Shader asset is not registered.");
        }
        return false;
    }
    av_doc *doc = av_value_read(text, NULL, 0);
    av_value *root = doc ? av_doc_root(doc) : NULL;
    if (!root || !av_is_obj(root)) {
        if (error) {
            snprintf(error->message, sizeof error->message, "Preview parameters must be KDL named values.");
        }
        av_doc_free(doc);
        return false;
    }
    shader_asset *asset = &assets[index - 1];
    av_obj_iter iterator = av_obj_iter_with(root);
    av_value *key;
    int count = 0;
    while ((key = av_obj_iter_next(&iterator))) {
        count++;
        if (count > SHADER_PARAMETER_MAX) {
            if (error) {
                snprintf(
                    error->message,
                    sizeof error->message,
                    "Shader preview accepts at most %d parameter overrides.",
                    SHADER_PARAMETER_MAX
                );
            }
            av_doc_free(doc);
            return false;
        }
        const char *name = av_get_str(key);
        int slot = parameter_slot(asset, name);
        if (slot < 0) {
            if (error) {
                snprintf(
                    error->message,
                    sizeof error->message,
                    "Unknown shader preview parameter '%s'.",
                    name ? name : ""
                );
            }
            av_doc_free(doc);
            return false;
        }
        av_value *value = av_obj_iter_get_val(key);
        int width = asset->widths[slot];
        bool valid = width == 1 &&
                     ((av_is_num(value) && isfinite(av_get_num(value))) ||
                      av_is_bool(value));
        if (!valid && width > 1 && av_is_arr(value) && (int)av_arr_size(value) == width) {
            valid = true;
            for (int i = 0; i < width; i++) {
                av_value *component = av_arr_get(value, (size_t)i);
                valid &= av_is_num(component) && isfinite(av_get_num(component));
            }
        }
        if (!valid) {
            if (error) {
                snprintf(
                    error->message,
                    sizeof error->message,
                    width == 1 ? "Shader preview parameter '%s' must be a "
                                 "finite number or boolean."
                               : "Shader preview parameter '%s' must be an "
                                 "array of %d finite numbers.",
                    name ? name : "",
                    width
                );
            }
            av_doc_free(doc);
            return false;
        }
    }
    av_doc_free(doc);
    return true;
}
bool render_shader_is_shape(int index)
{
    return index > 0 && index <= acount(assets) && assets[index - 1].shape;
}
bool render_shader_is_model(int index)
{
    return index > 0 && index <= acount(assets) && assets[index - 1].is_model;
}
bool render_shader_model_bounds(
    int index,
    const float parameters[32][4],
    float min[3],
    float max[3],
    bool *empty,
    shader_error *error
)
{
    if (!render_shader_is_model(index)) {
        return false;
    }
    model_bounds bounds;
    float prepared[32][4];
    if (!model_snapshot_prepare(index, parameters, prepared, &bounds, error)) {
        return false;
    }
    memcpy(min, bounds.min, sizeof bounds.min);
    memcpy(max, bounds.max, sizeof bounds.max);
    *empty = bounds.empty;
    return true;
}
bool render_shader_may_be_transparent(int index)
{
    if (index <= 0 || index > acount(assets) ||
        !assets[index - 1].source_value || render_shader_is_shape(index)) {
        return false;
    }
    const shader_graph *graph = &assets[index - 1].graph;
    const shader_node *opacity = &graph->nodes[graph->output[5]];
    return opacity->op != SHADER_CONSTANT || opacity->value[0] < 1;
}
bool render_shader_sample(
    int index,
    const shader_sample_context *sample,
    const float parameters[SHADER_PARAMETER_MAX][4],
    float result[SHADER_OUTPUT_MAX][4],
    shader_error *error
)
{
    float prepared[32][4];
    model_bounds bounds;
    if (render_shader_is_model(index)) {
        if (!model_snapshot_prepare(index, parameters, prepared, &bounds, error)) {
            return false;
        }
        parameters = prepared;
    }
    shader_sample_context context = *sample;
    context.resolve_image = render_image_resolve;
    context.image_userdata = NULL;
    return index > 0 && index <= acount(assets) &&
           shader_graph_eval_outputs(&assets[index - 1].graph, &context, parameters, result, error);
}
unsigned render_shader_revision()
{
    return revision;
}
unsigned render_shader_ramp_revision()
{
    return ramp_revision;
}
static void free_asset(shader_asset *asset)
{
    free(asset->name);
    av_value_free(asset->source_value);
    free(asset->glsl);
    afree(asset->images);
    shader_graph_free(&asset->graph);
    model_program_free(&asset->model);
    model_inspection_free(&asset->inspection);
    behavior_asset_free(asset->model_source);
    afree(asset->snapshots);
    afree(asset->callers);
    afree(asset->caller_slots);
    for (int i = 0; i < SHADER_PARAMETER_MAX; i++) {
        free(asset->names[i]);
    }
}
bool render_shader_upsert(const behavior_asset *source, shader_error *error)
{
    if (source && source->domain == BEHAVIOR_DOMAIN_MODEL) {
        return render_shader_models_upsert(&source, 1, error);
    }
    if (!source || (source->domain != BEHAVIOR_DOMAIN_MATERIAL &&
                    source->domain != BEHAVIOR_DOMAIN_SHAPE)) {
        return false;
    }
    if (source->domain == BEHAVIOR_DOMAIN_SHAPE &&
        (!(source->bounds[0] > 0) || !(source->bounds[1] > 0) ||
         !isfinite(source->bounds[0]) || !isfinite(source->bounds[1]))) {
        if (error) {
            error->node = 0;
            snprintf(error->message, sizeof error->message, "Shape bounds must be finite positive half extents.");
        }
        return false;
    }
    av_doc *document = av_doc_new();
    av_value *value = behavior_asset_to_value(document, source);
    av_doc_set_root(document, value);
    int existing = render_shader_find(source->name);
    if (existing && assets[existing - 1].is_model !=
                        (source->domain == BEHAVIOR_DOMAIN_MODEL)) {
        if (error) {
            snprintf(
                error->message,
                sizeof error->message,
                "Model and shader asset names must be unique: %s",
                source->name
            );
        }
        av_value_free(value);
        return false;
    }
    if (existing && av_equals(value, assets[existing - 1].source_value)) {
        av_value_free(value);
        return true;
    }
    shader_asset fresh = { .shape = source->domain == BEHAVIOR_DOMAIN_SHAPE,
                           .source_value = value };
    model_program_init(&fresh.model);
    if (!shader_graph_from_graph(source->graph, &fresh.graph, error)) {
        free_asset(&fresh);
        return false;
    }
    fresh.material_inspection = shader_graph_inspect(&fresh.graph);
    fresh.name = xstrdup(source->name);
    for (int i = 0; i < acount(fresh.graph.images); i++) {
        int variant = render_image_variant(&fresh.graph.images[i], error);
        if (!variant) {
            if (error) {
                for (int n = 0; n < acount(fresh.graph.nodes); n++) {
                    const shader_node *node = &fresh.graph.nodes[n];
                    if ((node->op == SHADER_IMAGE_TEXTURE ||
                         node->op == SHADER_IMAGE_TEXTURE_ALPHA ||
                         node->op == SHADER_TRIPLANAR_TEXTURE ||
                         node->op == SHADER_TRIPLANAR_TEXTURE_ALPHA) &&
                        node->image == i) {
                        error->node = node->source_id;
                        break;
                    }
                }
            }
            free_asset(&fresh);
            return false;
        }
        apush(fresh.images, variant);
    }
    if (acount(fresh.images) > RENDER_SHADER_IMAGE_MAX) {
        if (error) {
            snprintf(
                error->message,
                sizeof error->message,
                "Material %s exceeds %d image variants per program.",
                source->name,
                RENDER_SHADER_IMAGE_MAX
            );
        }
        free_asset(&fresh);
        return false;
    }
    memcpy(fresh.bounds, source->bounds, sizeof fresh.bounds);
    int id = render_shader_find(source->name);
    if (!id) {
        id = acount(assets) + 1;
    }
    char name[64];
    snprintf(name, sizeof name, "sg_asset_%d", id);
    fresh.glsl = shader_graph_glsl_named_ramps(&fresh.graph, name, 0, error);
    if (!fresh.glsl) {
        free_asset(&fresh);
        return false;
    }
    for (int i = 0; i < acount(fresh.graph.nodes); i++) {
        shader_node *n = &fresh.graph.nodes[i];
        if (n->op != SHADER_PARAMETER) {
            continue;
        }
        node *authored = graph_node_by_id(source->graph, n->source_id);
        const char *label = authored && authored->reference_name ? authored->reference_name
                                                                 : "";
        free(fresh.names[n->parameter]);
        fresh.names[n->parameter] = xstrdup(label);
        fresh.widths[n->parameter] = n->width;
        memcpy(fresh.defaults[n->parameter], n->value, sizeof n->value);
    }
    for (int i = 0; i < acount(assets); i++) {
        if (assets[i].is_model &&
            !model_material_contract_validate(&assets[i], id, &fresh, error)) {
            free_asset(&fresh);
            return false;
        }
    }
    if (id <= acount(assets)) {
        /* Avoid recompiling an unchanged graph on every editor frame. */
        shader_asset *old = &assets[id - 1];
        bool names_equal = true;
        for (int slot = 0; slot < 32; slot++) {
            const char *a = old->names[slot], *b = fresh.names[slot];
            names_equal &= (!a && !b) || (a && b && !strcmp(a, b));
        }
        bool images_equal = acount(old->images) == acount(fresh.images) &&
                            (!acount(fresh.images) ||
                             !memcmp(
                                 old->images,
                                 fresh.images,
                                 (size_t)acount(fresh.images) * sizeof *fresh.images
                             ));
        if (old->shape == fresh.shape && names_equal && images_equal &&
            !strcmp(old->glsl, fresh.glsl) &&
            !memcmp(old->defaults, fresh.defaults, sizeof old->defaults) &&
            !memcmp(old->bounds, fresh.bounds, sizeof old->bounds)) {
            if (av_equals(old->source_value, value)) {
                av_value_free(old->source_value);
                old->source_value = fresh.source_value;
                fresh.source_value = NULL;
                free_asset(&fresh);
                return true;
            }
            /* Ramp stop values are uploaded as a live payload. Keep the
             * compiled program revision when only those values changed. */
            free_asset(old);
            *old = fresh;
            ramp_revision++;
            return true;
        }
        free_asset(old);
        *old = fresh;
    } else {
        apush(assets, fresh);
    }
    revision++;
    ramp_revision++;
    parameter_revision++;
    return true;
}
float render_shader_distance_lipschitz(int index)
{
    return index > 0 && index <= acount(assets)
             ? shader_graph_lipschitz(&assets[index - 1].graph)
             : 1;
}
static bool model_asset_prepare(
    const behavior_asset *source,
    int id,
    const behavior_asset *const *catalog,
    int count,
    shader_asset *fresh,
    shader_error *error
)
{
    *fresh = (shader_asset){ .shape = true, .is_model = true };
    model_program_init(&fresh->model);
    fresh->name = xstrdup(source->name);
    fresh->model_source = behavior_asset_clone(source);
    if (!fresh->model_source ||
        !model_compile_with_catalog(source, catalog, count, &fresh->model, error)) {
        free_asset(fresh);
        *fresh = (shader_asset){ 0 };
        return false;
    }
    av_doc *document = av_doc_new();
    fresh->source_value = behavior_asset_to_value(document, source);
    av_doc_set_root(document, fresh->source_value);
    model_program_inspect(&fresh->model, &fresh->inspection);
    fresh->inspection.image_limit = RENDER_SHADER_IMAGE_MAX;
    fresh->inspection.ramp_limit = RENDER_SHADER_RAMP_VEC4_MAX;
    if (!render_image_validate_volumes(&fresh->model.shader, error)) {
        free_asset(fresh);
        *fresh = (shader_asset){ 0 };
        return false;
    }
    fresh->model.resolve_image = render_image_resolve;
    for (int i = 0; i < acount(fresh->model.shader.images); i++) {
        int variant = render_image_variant(&fresh->model.shader.images[i], error);
        if (!variant || acount(fresh->images) >= RENDER_SHADER_IMAGE_MAX) {
            free_asset(fresh);
            *fresh = (shader_asset){ 0 };
            return false;
        }
        apush(fresh->images, variant);
    }
    fresh->graph = fresh->model.shader;
    shader_graph_init(&fresh->model.shader);
    model_bounds bounds;
    if (!model_program_bounds(&fresh->model, NULL, &bounds, error)) {
        free_asset(fresh);
        *fresh = (shader_asset){ 0 };
        return false;
    }
    fresh->bounds[0] = fmaxf(fabsf(bounds.min[0]), fabsf(bounds.max[0]));
    fresh->bounds[1] = fmaxf(fabsf(bounds.min[2]), fabsf(bounds.max[2]));
    char name[64];
    snprintf(name, sizeof name, "sg_asset_%d", id);
    fresh->glsl = shader_graph_glsl_named_ramps(&fresh->graph, name, 0, error);
    if (!fresh->glsl) {
        free_asset(fresh);
        *fresh = (shader_asset){ 0 };
        return false;
    }
    snprintf(name, sizeof name, "sg_asset_%d_hit", id);
    char *hit = shader_graph_glsl_named(&fresh->model.hit_shader, name, error);
    if (!hit) {
        free_asset(fresh);
        *fresh = (shader_asset){ 0 };
        return false;
    }
    append(&fresh->glsl, "%s", hit);
    free(hit);
    for (int i = 0; i < acount(fresh->model.parameters); i++) {
        const model_parameter *parameter = &fresh->model.parameters[i];
        int slot = parameter->slot;
        memcpy(fresh->defaults[slot], parameter->defaults, sizeof parameter->defaults);
        if (parameter->hidden) {
            continue;
        }
        fresh->names[slot] = xstrdup(parameter->name);
        fresh->widths[slot] = parameter->width;
    }
    return true;
}
static void model_overrides_migrate(
    int id,
    const shader_asset *old,
    const shader_asset *fresh,
    const char *renamed_from,
    const char *renamed_to
)
{
    for (int i = acount(overrides) - 1; i >= 0; i--) {
        shader_override *override = &overrides[i];
        if (override->shader != id) {
            continue;
        }
        int slot = parameter_slot(old, override->name);
        const model_parameter *before = slot >= 0 ? &old->model.parameters[slot] : NULL;
        const model_parameter *after = NULL;
        if (before) {
            const char *asset = before->asset;
            if (renamed_from && !strcmp(asset, renamed_from)) {
                asset = renamed_to;
            }
            for (int j = 0; j < acount(fresh->model.parameters); j++) {
                const model_parameter *candidate = &fresh->model.parameters[j];
                if (!candidate->hidden && candidate->node == before->node &&
                    candidate->width == before->width &&
                    candidate->type == before->type &&
                    !strcmp(candidate->asset, asset) &&
                    !strcmp(candidate->scope, before->scope)) {
                    after = candidate;
                    break;
                }
            }
        }
        if (!after) {
            free(override->name);
            adel(overrides, i);
            continue;
        }
        free(override->name);
        override->name = xstrdup(after->name);
    }
}
static void model_catalog_materials(const behavior_asset ***catalog, behavior_asset **stubs)
{
    for (int i = 0; i < acount(assets); i++) {
        const shader_asset *asset = &assets[i];
        if (asset->source_value && !asset->shape) {
            apush(*stubs, ((behavior_asset){ .name = asset->name, .domain = BEHAVIOR_DOMAIN_MATERIAL }));
        }
    }
    for (int i = 0; i < acount(*stubs); i++) {
        apush(*catalog, &(*stubs)[i]);
    }
}
typedef struct
{
    int id;
    bool bindings_only;
    shader_asset asset;
    model_material_bindings bindings;
} model_pending;
static void model_pending_free(model_pending *pending)
{
    if (pending->bindings_only) {
        free(pending->asset.name);
        av_value_free(pending->asset.source_value);
        behavior_asset_free(pending->asset.model_source);
        model_material_bindings_free(&pending->bindings);
        return;
    }
    free_asset(&pending->asset);
}
static bool model_generation_publish(
    const behavior_asset *const *catalog,
    int count,
    int removed,
    const char *renamed,
    int rename_id,
    shader_error *error
)
{
    model_pending *prepared = NULL;
    const behavior_asset **mixed = NULL;
    behavior_asset *material_stubs = NULL;
    for (int i = 0; i < count; i++) {
        apush(mixed, catalog[i]);
    }
    model_catalog_materials(&mixed, &material_stubs);
    int next = acount(assets) + 1, compilations = 0;
    bool ok = true, changed = removed > 0;
    for (int i = 0; i < count; i++) {
        int id = renamed && !strcmp(catalog[i]->name, renamed)
                   ? rename_id
                   : render_shader_find(catalog[i]->name);
        if (id && !render_shader_is_model(id)) {
            if (error) {
                snprintf(
                    error->message,
                    sizeof error->message,
                    "Model and shader asset names must be unique: %s",
                    catalog[i]->name
                );
            }
            ok = false;
            break;
        }
        model_pending pending = { .id = id ? id : next++ };
        model_material_bindings_init(&pending.bindings);
        model_bindings_result bindings = id && !renamed
                                           ? model_program_compile_bindings(
                                                 &assets[id - 1].model,
                                                 catalog[i],
                                                 mixed,
                                                 acount(mixed),
                                                 &pending.bindings,
                                                 error
                                             )
                                           : MODEL_BINDINGS_GEOMETRY_CHANGED;
        if (bindings == MODEL_BINDINGS_INVALID) {
            model_material_bindings_free(&pending.bindings);
            ok = false;
            break;
        }
        if (bindings == MODEL_BINDINGS_READY) {
            pending.bindings_only = true;
            pending.asset = assets[id - 1];
            pending.asset.name = xstrdup(catalog[i]->name);
            pending.asset.model_source = behavior_asset_clone(catalog[i]);
            av_doc *doc = av_doc_new();
            pending.asset.source_value = behavior_asset_to_value(doc, catalog[i]);
            av_doc_set_root(doc, pending.asset.source_value);
            pending.asset.model.material_slots = pending.bindings.slots;
            pending.asset.model.material_dependencies = pending.bindings.dependencies;
            for (int j = 0; j < acount(assets[id - 1].model.parts); j++) {
                changed |= assets[id - 1].model.parts[j].material !=
                           pending.bindings.part_slots[j];
            }
        } else {
            if (!model_asset_prepare(
                    catalog[i],
                    pending.id,
                    mixed,
                    acount(mixed),
                    &pending.asset,
                    error
                )) {
                ok = false;
                break;
            }
            compilations++;
            changed |= !id || strcmp(assets[id - 1].glsl, pending.asset.glsl) != 0;
        }
        if (!model_material_contract_validate(&pending.asset, 0, NULL, error)) {
            model_pending_free(&pending);
            ok = false;
            break;
        }
        apush(prepared, pending);
    }
    if (!ok) {
        for (int i = 0; i < acount(prepared); i++) {
            model_pending_free(&prepared[i]);
        }
        afree(prepared);
        afree(mixed);
        afree(material_stubs);
        return false;
    }
    while (atomic_flag_test_and_set_explicit(&override_lock, memory_order_acquire)) {
    }
    const char *renamed_from = rename_id ? assets[rename_id - 1].name : NULL;
    for (int i = 0; i < count; i++) {
        if (prepared[i].id <= acount(assets) && !prepared[i].bindings_only) {
            model_overrides_migrate(
                prepared[i].id,
                &assets[prepared[i].id - 1],
                &prepared[i].asset,
                renamed_from,
                renamed
            );
        }
    }
    atomic_flag_clear_explicit(&override_lock, memory_order_release);
    for (int i = 0; i < count; i++) {
        model_pending *pending = &prepared[i];
        if (pending->bindings_only) {
            shader_asset *old = &assets[pending->id - 1];
            model_material_bindings discarded = {
                .slots = old->model.material_slots,
                .dependencies = old->model.material_dependencies
            };
            model_material_bindings_free(&discarded);
            free(old->name);
            av_value_free(old->source_value);
            behavior_asset_free(old->model_source);
            old->name = pending->asset.name;
            old->source_value = pending->asset.source_value;
            old->model_source = pending->asset.model_source;
            old->model.material_slots = pending->bindings.slots;
            old->model.material_dependencies = pending->bindings.dependencies;
            old->inspection.material_slots = acount(old->model.material_slots);
            for (int j = 0; j < acount(old->model.parts); j++) {
                old->model.parts[j].material = pending->bindings.part_slots[j];
                old->inspection.parts[j].material = pending->bindings.part_slots[j];
            }
            pending->bindings.slots = NULL;
            pending->bindings.dependencies = NULL;
            model_material_bindings_free(&pending->bindings);
        } else if (pending->id <= acount(assets)) {
            free_asset(&assets[pending->id - 1]);
            assets[pending->id - 1] = pending->asset;
        } else {
            assert(pending->id == acount(assets) + 1);
            apush(assets, pending->asset);
        }
    }
    if (removed) {
        free_asset(&assets[removed - 1]);
        assets[removed - 1] = (shader_asset){ .name = xstrdup("") };
    }
    if (changed) {
        revision++;
        ramp_revision++;
    }
    parameter_revision++;
    model_stats.compilations += compilations;
    afree(prepared);
    afree(mixed);
    afree(material_stubs);
    return true;
}
bool render_shader_models_upsert(const behavior_asset *const *sources, int count, shader_error *error)
{
    const behavior_asset **catalog = NULL;
    bool unchanged = true;
    for (int i = 0; i < count; i++) {
        if (!sources[i] || sources[i]->domain != BEHAVIOR_DOMAIN_MODEL) {
            if (error) {
                snprintf(error->message, sizeof error->message, "Model catalog requires model assets.");
            }
            return false;
        }
        for (int j = 0; j < i; j++) {
            if (!strcmp(sources[i]->name, sources[j]->name)) {
                if (error) {
                    snprintf(
                        error->message,
                        sizeof error->message,
                        "Ambiguous model name: %s",
                        sources[i]->name
                    );
                }
                return false;
            }
        }
        int id = render_shader_find(sources[i]->name);
        unchanged &= id && render_shader_is_model(id) &&
                     behavior_asset_equal(sources[i], assets[id - 1].model_source);
    }
    if (unchanged) {
        return true;
    }
    for (int i = 0; i < acount(assets); i++) {
        if (!assets[i].is_model) {
            continue;
        }
        const behavior_asset *source = assets[i].model_source;
        for (int j = 0; j < count; j++) {
            if (!strcmp(source->name, sources[j]->name)) {
                source = sources[j];
                break;
            }
        }
        apush(catalog, source);
    }
    for (int i = 0; i < count; i++) {
        if (!render_shader_find(sources[i]->name)) {
            apush(catalog, sources[i]);
        } else if (!render_shader_is_model(render_shader_find(sources[i]->name))) {
            if (error) {
                snprintf(
                    error->message,
                    sizeof error->message,
                    "Model and shader asset names must be unique: %s",
                    sources[i]->name
                );
            }
            afree(catalog);
            return false;
        }
    }
    bool ok = model_generation_publish(catalog, acount(catalog), 0, NULL, 0, error);
    afree(catalog);
    return ok;
}
bool render_shader_model_remove(const char *name, shader_error *error)
{
    int id = render_shader_find(name);
    if (!render_shader_is_model(id)) {
        return false;
    }
    const behavior_asset **catalog = NULL;
    for (int i = 0; i < acount(assets); i++) {
        if (assets[i].is_model && i + 1 != id) {
            apush(catalog, assets[i].model_source);
        }
    }
    bool ok = model_generation_publish(catalog, acount(catalog), id, NULL, 0, error);
    afree(catalog);
    return ok;
}
bool render_shader_model_rename(const char *from, const char *to, shader_error *error)
{
    int id = render_shader_find(from);
    if (!render_shader_is_model(id) || !to || !*to || render_shader_find(to)) {
        if (error) {
            snprintf(error->message, sizeof error->message, "Model rename requires an available name.");
        }
        return false;
    }
    behavior_asset **copies = NULL;
    const behavior_asset **catalog = NULL;
    for (int i = 0; i < acount(assets); i++) {
        if (!assets[i].is_model) {
            continue;
        }
        behavior_asset *copy = behavior_asset_clone(assets[i].model_source);
        assert(copy);
        if (i + 1 == id) {
            assert(behavior_asset_set_name(copy, to) == 0);
        }
        for (int j = 0; j < acount(copy->graphs); j++) {
            graph *g = copy->graphs[j];
            for (int k = 0; k < acount(g->nodes); k++) {
                node *n = g->nodes[k];
                if (n->desc == &NODE_ModelInstance && n->reference_asset &&
                    !strcmp(n->reference_asset, from)) {
                    n->reference_asset = graph_own_str(g, to, strlen(to));
                }
            }
        }
        apush(copies, copy);
        apush(catalog, copy);
    }
    bool ok = model_generation_publish(catalog, acount(catalog), 0, to, id, error);
    for (int i = 0; i < acount(copies); i++) {
        behavior_asset_free(copies[i]);
    }
    afree(copies);
    afree(catalog);
    return ok;
}
bool render_shader_material_remove(const char *name, shader_error *error)
{
    int id = render_shader_find(name);
    if (!id || assets[id - 1].shape) {
        return false;
    }
    for (int i = 0; i < acount(assets); i++) {
        for (int j = 0;
             assets[i].is_model && j < acount(assets[i].model.material_slots);
             j++) {
            const model_material_slot *slot = &assets[i].model.material_slots[j];
            if (!strcmp(slot->material, name)) {
                if (error) {
                    error->node = slot->node;
                    snprintf(
                        error->message,
                        sizeof error->message,
                        "Material %s is referenced by %s slot %s.",
                        name,
                        assets[i].name,
                        slot->name
                    );
                }
                return false;
            }
        }
        for (int j = 0; assets[i].is_model &&
                        j < acount(assets[i].model.material_dependencies);
             j++) {
            if (!strcmp(assets[i].model.material_dependencies[j], name)) {
                if (error) {
                    error->node = 0;
                    snprintf(
                        error->message,
                        sizeof error->message,
                        "Material %s is referenced by model %s.",
                        name,
                        assets[i].name
                    );
                }
                return false;
            }
        }
    }
    free_asset(&assets[id - 1]);
    assets[id - 1] = (shader_asset){ .name = xstrdup("") };
    revision++;
    ramp_revision++;
    parameter_revision++;
    return true;
}
static void model_source_material_rename(behavior_asset *source, const char *from, const char *to)
{
    for (int i = 0; i < acount(source->graphs); i++) {
        graph *g = source->graphs[i];
        for (int j = 0; j < acount(g->nodes); j++) {
            node *n = g->nodes[j];
            if (n->desc == &NODE_ModelMaterialSlot && n->reference_asset &&
                !strcmp(n->reference_asset, from)) {
                n->reference_asset = graph_own_str(g, to, strlen(to));
            }
            for (int k = 0; n->desc == &NODE_ModelInstance && k < n->input_count; k++) {
                pin_spec *pin = &n->input_pins[k];
                if (pin->type == PIN_STR && pin->type_name &&
                    !strcmp(pin->type_name, "MaterialAsset") &&
                    pin->default_value.str && !strcmp(pin->default_value.str, from)) {
                    pin_value value = { .type = PIN_STR,
                                        .str = to,
                                        .type_name = "MaterialAsset" };
                    assert(!graph_set_pin_default(g, n, false, k, &value));
                }
            }
        }
    }
}
bool render_shader_material_rename(const char *from, const char *to, shader_error *error)
{
    int id = render_shader_find(from);
    if (!id || assets[id - 1].shape || !to || !*to || render_shader_find(to)) {
        if (error) {
            snprintf(error->message, sizeof error->message, "Material rename requires an available name.");
        }
        return false;
    }
    /* Identity and shader code stay fixed. Prepare all owned replacement source
       strings before publishing the renamed material and its model bindings. */
    behavior_asset *material = behavior_asset_from_value(assets[id - 1].source_value);
    assert(material && !behavior_asset_set_name(material, to));
    av_doc *doc = av_doc_new();
    av_value *material_value = behavior_asset_to_value(doc, material);
    av_doc_set_root(doc, material_value);
    behavior_asset_free(material);
    behavior_asset **sources = NULL;
    av_value **values = NULL;
    for (int i = 0; i < acount(assets); i++) {
        behavior_asset *source = assets[i].is_model
                                   ? behavior_asset_clone(assets[i].model_source)
                                   : NULL;
        av_value *value = NULL;
        if (source) {
            model_source_material_rename(source, from, to);
            av_doc *source_doc = av_doc_new();
            value = behavior_asset_to_value(source_doc, source);
            av_doc_set_root(source_doc, value);
        }
        apush(sources, source);
        apush(values, value);
    }
    for (int i = 0; i < acount(assets); i++) {
        shader_asset *asset = &assets[i];
        if (!asset->is_model) {
            continue;
        }
        behavior_asset_free(asset->model_source);
        av_value_free(asset->source_value);
        asset->model_source = sources[i];
        asset->source_value = values[i];
        for (int j = 0; j < acount(asset->model.material_slots); j++) {
            model_material_slot *slot = &asset->model.material_slots[j];
            if (!strcmp(slot->material, from)) {
                free(slot->material);
                slot->material = xstrdup(to);
            }
        }
        for (int j = 0; j < acount(asset->model.material_dependencies); j++) {
            char **dependency = &asset->model.material_dependencies[j];
            if (!strcmp(*dependency, from)) {
                free(*dependency);
                *dependency = xstrdup(to);
            }
        }
    }
    free(assets[id - 1].name);
    assets[id - 1].name = xstrdup(to);
    av_value_free(assets[id - 1].source_value);
    assets[id - 1].source_value = material_value;
    parameter_revision++;
    afree(sources);
    afree(values);
    return true;
}
static int set_slot(const render_shader_set *set, int image)
{
    for (int i = 0; i < set->count; i++) {
        if (set->images[i] == image) {
            return i;
        }
    }
    return -1;
}

static bool render_shader_set_contains(const render_shader_set *set, int id)
{
    if (id <= 0) {
        return true;
    }
    if (set->shape && id != set->shape && id != set->material) {
        return false;
    }
    const shader_asset *asset = &assets[id - 1];
    if (!asset->source_value) {
        return false;
    }
    for (int i = 0; i < acount(asset->images); i++) {
        if (set_slot(set, asset->images[i]) < 0) {
            return false;
        }
    }
    return true;
}

static bool asset_in_set(const render_shader_set *set, int index)
{
    return render_shader_set_contains(set, index + 1);
}

static int ramp_offset(const render_shader_set *set, int index)
{
    int offset = 0;
    for (int i = 0; i < index; i++) {
        if (asset_in_set(set, i)) {
            offset += shader_graph_ramp_vec4_count(&assets[i].graph);
        }
    }
    return offset;
}

static bool shader_resources_add(render_shader_set *set, const shader_asset *asset, shader_error *error)
{
    if (!asset) {
        return true;
    }
    render_shader_set fresh = *set;
    for (int i = 0; i < acount(asset->images); i++) {
        int image = asset->images[i];
        if (set_slot(&fresh, image) >= 0) {
            continue;
        }
        if (fresh.count == RENDER_SHADER_IMAGE_MAX) {
            if (error) {
                snprintf(
                    error->message,
                    sizeof error->message,
                    "Adding %s exceeds %d image variants in one material/shape "
                    "or composite group.",
                    asset->name,
                    RENDER_SHADER_IMAGE_MAX
                );
            }
            return false;
        }
        int at = fresh.count++;
        while (at > 0 && fresh.images[at - 1] > image) {
            fresh.images[at] = fresh.images[at - 1];
            at--;
        }
        fresh.images[at] = image;
    }
    *set = fresh;
    return true;
}

bool render_shader_set_add(render_shader_set *set, int id, shader_error *error)
{
    return id <= 0 || shader_resources_add(set, &assets[id - 1], error);
}

int render_shader_ramp_vec4_count(const render_shader_set *set)
{
    int count = 0;
    for (int i = 0; i < acount(assets); i++) {
        if (asset_in_set(set, i)) {
            int asset_count = shader_graph_ramp_vec4_count(&assets[i].graph);
            if (asset_count > RENDER_SHADER_RAMP_VEC4_MAX - count) {
                return RENDER_SHADER_RAMP_VEC4_MAX + 1;
            }
            count += asset_count;
        }
    }
    return count;
}

void render_shader_ramp_payload(const render_shader_set *set, float (*payload)[4])
{
    int offset = 0;
    for (int i = 0; i < acount(assets); i++) {
        if (!asset_in_set(set, i)) {
            continue;
        }
        shader_graph_ramp_payload(&assets[i].graph, payload + offset);
        offset += shader_graph_ramp_vec4_count(&assets[i].graph);
    }
}

/* Recognize only an exact centered sphere. Radius may animate or bind to an
 * instance parameter, but must not depend on the point being evaluated. */
static bool sphere_graph(const shader_graph *graph)
{
    const shader_node *distance = &graph->nodes[graph->output[0]];
    if (distance->op != SHADER_SUBTRACT) {
        return false;
    }
    const shader_node *length = &graph->nodes[distance->input[0]];
    if (length->op != SHADER_LENGTH ||
        graph->nodes[length->input[0]].op != SHADER_OBJECT_POSITION) {
        return false;
    }
    bool spatial[SHADER_NODE_MAX] = { 0 };
    for (int i = 0; i < acount(graph->nodes); i++) {
        const shader_node *node = &graph->nodes[i];
        switch (node->op) {
            case SHADER_POSITION:
            case SHADER_UV:
            case SHADER_OBJECT_POSITION:
            case SHADER_PART_POSITION:
            case SHADER_WORLD_POSITION:
            case SHADER_NORMAL:
            case SHADER_VIEW_DIRECTION:
            case SHADER_SURFACE_INFLUENCE: spatial[i] = true; break;
            default:
                for (int j = 0; j < shader_op_inputs(node->op); j++) {
                    spatial[i] |= spatial[node->input[j]];
                }
                break;
        }
    }
    return !spatial[distance->input[1]];
}

bool render_shader_sphere_radius(int index, const float parameters[32][4], float *radius)
{
    if (!index || !assets[index - 1].shape || !sphere_graph(&assets[index - 1].graph)) {
        return false;
    }
    const shader_graph *g = &assets[index - 1].graph;
    const shader_node *node = &g->nodes[g->nodes[g->output[0]].input[1]];
    if (node->op == SHADER_CONSTANT) {
        *radius = node->value[0];
    } else if (node->op == SHADER_PARAMETER) {
        *radius = parameters[node->parameter][0];
    } else {
        return false;
    }
    return isfinite(*radius) && *radius > 0;
}

char *render_shader_glsl(const render_shader_set *set, int sampler_set, int first_binding)
{
    char *text = NULL;
    for (int i = 0; i < set->count; i++) {
        append(
            &text,
            "layout(set=%d,binding=%d) uniform sampler2D sg_image_%d;\n",
            sampler_set,
            first_binding + i,
            i
        );
    }
    append(&text, "#ifndef SG_SAMPLE_DEFINED\n#define SG_SAMPLE_DEFINED\nstruct SGSample { vec2 position; vec2 uv; vec3 object_position; vec3 part_position; vec3 object_from_world[3]; vec3 part_from_world[3]; vec3 uv_from_world[2]; vec3 world_position; vec3 normal; vec3 view_direction; float time; };\n#endif\n");
    for (int i = 0; i < acount(assets); i++) {
        if (!render_shader_set_contains(set, i + 1)) {
            continue;
        }
        for (int j = 0; j < acount(assets[i].images); j++) {
            const char *normalize = assets[i].graph.images[j].wrap == SHADER_IMAGE_REPEAT
                                      ? "uv-floor(uv)"
                                      : "clamp(uv,vec2(0),vec2(1))";
            append(
                &text,
                "vec4 sg_asset_%d_image_%d(vec2 uv) { "
                "if(any(isnan(uv))||any(isinf(uv))) return vec4(0); uv=%s; "
                "return textureLod(sg_image_%d,uv,0.0); }\n",
                i + 1,
                j,
                normalize,
                set_slot(set, assets[i].images[j])
            );
        }
        if (assets[i].is_model) {
            for (int j = 0; j < acount(assets[i].images); j++) {
                append(
                    &text,
                    "vec4 sg_asset_%d_hit_image_%d(vec2 uv){return "
                    "sg_asset_%d_image_%d(uv); }\n",
                    i + 1,
                    j,
                    i + 1,
                    j
                );
            }
        }
        char name[64];
        snprintf(name, sizeof name, "sg_asset_%d", i + 1);
        shader_error error = { 0 };
        char *glsl = shader_graph_glsl_named_ramps(&assets[i].graph, name, ramp_offset(set, i), &error);
        if (glsl) {
            append(&text, "%s\n", glsl);
        }
        free(glsl);
        if (assets[i].is_model) {
            snprintf(name, sizeof name, "sg_asset_%d_hit", i + 1);
            char *hit = shader_graph_glsl_named(&assets[i].model.hit_shader, name, &error);
            if (hit) {
                append(&text, "%s\n", hit);
            }
            free(hit);
            snprintf(name, sizeof name, "sg_asset_%d_frame", i + 1);
            char *frame = model_program_frame_glsl(&assets[i].model, name, &error);
            if (frame) {
                append(&text, "%s\n", frame);
            }
            free(frame);
        }
    }
    if (set->shape) {
        append(
            &text,
            "#define SG_DISTANCE_LIPSCHITZ %.9g\n",
            shader_graph_lipschitz(&assets[set->shape - 1].graph)
        );
        char name[64];
        snprintf(name, sizeof name, "sg_gradient_%d", set->shape);
        shader_error error = { 0 };
        char *gradient = shader_graph_glsl_gradient_named(
            &assets[set->shape - 1].graph,
            name,
            &error
        );
        append(&text, "#define SG_ANALYTIC_GRADIENT %d\n", gradient != NULL);
        if (gradient) {
            append(&text, "%s\n#define SG_GRADIENT_CALL(context,values) %s(context,values)\n", gradient, name);
        }
        free(gradient);
        append(
            &text,
            "#define SG_ANALYTIC_SPHERE %d\n",
            sphere_graph(&assets[set->shape - 1].graph)
        );
        append(&text, "#define SG_SHAPE_CALL(id,context,values,result) sg_asset_%d(context,values,result)\n", set->shape);
        if (set->material) {
            append(&text, "#define SG_MATERIAL_CALL(id,context,values,result) sg_asset_%d(context,values,result)\n", set->material);
        }
    }
    append(&text, "int sg_model_material_slot(int id,int part) { switch(id) {\n");
    for (int i = 0; i < acount(assets); i++) {
        if (!assets[i].is_model || !render_shader_set_contains(set, i + 1)) {
            continue;
        }
        append(&text, "case %d: switch(part) {\n", i + 1);
        for (int j = 0; j < acount(assets[i].model.parts); j++) {
            if (assets[i].model.parts[j].material >= 0) {
                append(
                    &text,
                    "case %d: return %d;\n",
                    j,
                    assets[i].model.parts[j].material
                );
            }
        }
        append(&text, "default: return -1; }\n");
    }
    append(&text, "default: return -1; } }\n");
    append(&text, "void sg_model_hit_dispatch(int id,SGSample context,vec4 values[32],out vec4 result[8]) {\n for(int i=0;i<8;i++) result[i]=vec4(0); result[0].xy=vec2(-1); result[1].xyz=context.object_position;\n switch(id) {\n");
    for (int i = 0; i < acount(assets); i++) {
        if (assets[i].is_model && render_shader_set_contains(set, i + 1)) {
            append(&text, "case %d: sg_asset_%d_hit(context,values,result); break;\n", i + 1, i + 1);
        }
    }
    append(&text, "} }\n");
    append(&text, "void sg_model_frame_dispatch(int id,int part,SGSample context,vec4 values[32],out vec3 rows[3]) { rows[0]=vec3(1,0,0); rows[1]=vec3(0,1,0); rows[2]=vec3(0,0,1); switch(id) {\n");
    for (int i = 0; i < acount(assets); i++) {
        if (assets[i].is_model && render_shader_set_contains(set, i + 1)) {
            append(&text, "case %d: sg_asset_%d_frame(part,context,values,rows); break;\n", i + 1, i + 1);
        }
    }
    append(&text, "default: break; } }\n");
    append(&text, "void sg_dispatch(int id,SGSample sg_context,vec4 values[32],out vec4 result[8]) {\n for(int i=0;i<8;i++) result[i]=vec4(0);\n switch(id) {\n");
    for (int i = 0; i < acount(assets); i++) {
        if (!render_shader_set_contains(set, i + 1)) {
            continue;
        }
        append(&text, "case %d: sg_asset_%d(sg_context,values,result); break;\n", i + 1, i + 1);
    }
    append(&text, "default: break;\n }\n}\n");
    append(
        &text,
        "void sg_dispatch_2d(int id,vec2 p,vec2 wp,vec2 half_size,vec4 "
        "object_frame,float "
        "time,vec4 values[32],out vec4 result[8]) {\n"
        " SGSample sg_context; sg_context.position=p; "
        "sg_context.uv=vec2(p.x,-p.y)/max(2.0*half_size,vec2(0.0001))+0.5;\n"
        " sg_context.object_position=vec3(p.x,0.0,p.y); "
        "sg_context.part_position=sg_context.object_position; "
        "sg_context.world_position=vec3(wp.x,0.0,wp.y);\n"
        " sg_context.object_from_world[0]=vec3(object_frame.x,0,object_frame.y)"
        "; sg_context.object_from_world[1]=dot(object_frame,object_frame)>0.0 "
        "? vec3(0,1,0) : vec3(0); "
        "sg_context.object_from_world[2]=vec3(object_frame.z,0,object_frame.w);"
        " for(int i=0;i<3;i++) "
        "sg_context.part_from_world[i]=sg_context.object_from_world[i]; "
        "sg_context.uv_from_world[0]=sg_context.object_from_world[0]/"
        "max(2.0*half_size.x,0.0001); "
        "sg_context.uv_from_world[1]=-sg_context.object_from_world[2]/"
        "max(2.0*half_size.y,0.0001); "
        " sg_context.normal=vec3(0,1,0); "
        "sg_context.view_direction=vec3(0,1,0); "
        "sg_context.time=time;\n"
        " sg_dispatch(id,sg_context,values,result);\n switch(id) {\n"
    );
    for (int i = 0; i < acount(assets); i++) {
        if (assets[i].shape) {
            append(&text, "case %d: return;\n", i + 1);
        }
    }
    append(
        &text,
        "default: break; }\n"
        " vec3 base=max(result[0].rgb,vec3(0)); vec3 "
        "emission=max(result[1].rgb,vec3(0));\n"
        " float opacity=clamp(result[5].x,0.0,1.0); float "
        "metallic=clamp(result[3].x,0.0,1.0); bool unlit=result[6].x>0.5;\n"
        " float gloss=1.0-clamp(result[2].x,0.0,1.0);\n"
        /* The surface normal faces the camera along +Y; its XZ tilt becomes
         * world X and up. A normal facing away keeps a maximal tilt. */
        " vec3 n=result[4].xyz; float n2=dot(n,n); vec2 tilt=vec2(0);\n"
        " if(n2>1e-8) { tilt=n.xz*inversesqrt(n2); float t2=dot(tilt,tilt); "
        "if(n.y<=0.0&&t2>1e-8) tilt*=0.999*inversesqrt(t2); }\n"
        " result[0]=vec4(base,opacity); result[1]=vec4(emission,opacity);\n"
        " result[2]=vec4(unlit?vec3(0):base*(1.0-metallic),opacity); "
        "result[3]=vec4(vec3(opacity),opacity);\n"
        " result[4]=unlit?vec4(0):vec4(tilt,gloss,result[7].x);\n"
        " result[5]=vec4(0); result[6]=vec4(0); result[7]=vec4(0);\n}\n"
    );
    return text;
}
struct render_shader_parameter_cache
{
    unsigned revision;
    char *text, *scope;
    float values[SHADER_PARAMETER_MAX][4];
};
static render_shader_parameter_cache *serial_parameters;
void render_shader_parameter_cache_free(render_shader_parameter_cache *cache)
{
    for (int i = 0; i < acount(cache); i++) {
        free(cache[i].text);
        free(cache[i].scope);
    }
    afree(cache);
}
static void parse_parameters(const shader_asset *asset, const char *text, float values[32][4])
{
    memcpy(values, asset->defaults, sizeof asset->defaults);
    av_doc *doc = text && *text ? av_value_read(text, NULL, 0) : NULL;
    av_value *root = doc ? av_doc_root(doc) : NULL;
    for (int i = 0; i < 32; i++) {
        if (!asset->names[i]) {
            continue;
        }
        av_value *value = root ? av_obj_get(root, asset->names[i]) : NULL;
        if (root) {
            const char *scope = asset->shape ? "shape" : "material";
            size_t size = strlen(scope) + strlen(asset->names[i]) + 2;
            char *scoped = xmalloc(size);
            snprintf(scoped, size, "%s.%s", scope, asset->names[i]);
            av_value *explicit_value = av_obj_get(root, scoped);
            free(scoped);
            if (explicit_value) {
                value = explicit_value;
            }
        }
        float parsed[4] = { 0 };
        bool valid = false;
        if (av_is_num(value) && asset->widths[i] == 1) {
            parsed[0] = (float)av_get_num(value);
            valid = isfinite(parsed[0]);
        }
        if (av_is_bool(value) && asset->widths[i] == 1) {
            parsed[0] = av_get_bool(value);
            valid = true;
        }
        if (av_is_arr(value) && (int)av_arr_size(value) == asset->widths[i]) {
            valid = true;
            for (int j = 0; j < asset->widths[i]; j++) {
                av_value *v = av_arr_get(value, (size_t)j);
                parsed[j] = (float)av_get_num(v);
                valid &= av_is_num(v) && isfinite(parsed[j]);
            }
        }
        if (valid) {
            memcpy(values[i], parsed, sizeof parsed);
        }
    }
    av_doc_free(doc);
}

static bool read_parameter_value(const shader_asset *asset, int slot, av_value *value, float result[4])
{
    int width = asset->widths[slot];
    memset(result, 0, 4 * sizeof(float));
    if (width == 1 && av_is_num(value)) {
        result[0] = (float)av_get_num(value);
        return isfinite(result[0]);
    }
    if (width == 1 && !asset->is_model && av_is_bool(value)) {
        result[0] = av_get_bool(value);
        return true;
    }
    if (!av_is_arr(value) || (int)av_arr_size(value) != width) {
        return false;
    }
    for (int i = 0; i < width; i++) {
        av_value *component = av_arr_get(value, (size_t)i);
        if (!av_is_num(component)) {
            return false;
        }
        result[i] = (float)av_get_num(component);
        if (!isfinite(result[i])) {
            return false;
        }
    }
    return true;
}
typedef struct
{
    const shader_asset *asset;
    const char *scope;
    int offset, count;
    uint64_t node;
} model_material_entry;
static bool material_namespace_validate(const model_material_entry *entries, int length, shader_error *error)
{
    char *names[32];
    const char *aliases[32];
    uint64_t nodes[32];
    int count = 0;
    for (int i = 0; i < length; i++) {
        const model_material_entry *entry = &entries[i];
        for (int j = 0; entry->asset && j < entry->count; j++) {
            const char *name = entry->asset->names[j];
            if (!name) {
                continue;
            }
            size_t size = strlen(entry->scope) + strlen(name) + 2;
            names[count] = xmalloc(size);
            snprintf(names[count], size, "%s%s%s", entry->scope, *entry->scope ? "." : "", name);
            aliases[count] = name;
            nodes[count++] = entry->node;
        }
    }
    bool ok = true;
    for (int i = 0; ok && i < count; i++) {
        for (int j = 0; j < count; j++) {
            bool alias_collision = strcmp(names[i], aliases[i]) &&
                                   !strcmp(names[i], aliases[j]);
            if (i == j || (strcmp(names[i], names[j]) && !alias_collision)) {
                continue;
            }
            if (error) {
                error->node = nodes[i];
                snprintf(
                    error->message,
                    sizeof error->message,
                    "Material parameter namespace collision at '%s'.",
                    names[i]
                );
            }
            ok = false;
            break;
        }
    }
    for (int i = 0; i < count; i++) {
        free(names[i]);
    }
    return ok;
}
static const shader_asset *material_asset_find(
    const char *name,
    const shader_asset *const *catalog,
    int count
)
{
    if (catalog) {
        for (int i = 0; i < count; i++) {
            if (!strcmp(catalog[i]->name, name)) {
                return catalog[i]->shape ? NULL : catalog[i];
            }
        }
        return NULL;
    }
    int id = render_shader_find(name);
    return id && !assets[id - 1].shape ? &assets[id - 1] : NULL;
}
static bool model_material_entries(
    const shader_asset *shape,
    const shader_asset *fallback,
    const shader_asset *const *catalog,
    int count,
    model_material_entry entries[MODEL_MATERIAL_SLOT_MAX + 1],
    int *length,
    float values[32][4],
    shader_error *error
)
{
    memset(values, 0, 32 * 4 * sizeof(float));
    int slots = shape && shape->is_model ? acount(shape->model.material_slots) : 0;
    int used = 0;
    *length = slots + 1;
    for (int i = 0; i <= slots; i++) {
        const model_material_slot *slot = i ? &shape->model.material_slots[i - 1]
                                            : NULL;
        const shader_asset *asset = slot
                                      ? material_asset_find(slot->material, catalog, count)
                                      : fallback;
        if (slot && !asset) {
            if (error) {
                error->node = slot->node;
                snprintf(
                    error->message,
                    sizeof error->message,
                    "Material slot %s references missing Material %s.",
                    slot->name,
                    slot->material
                );
            }
            return false;
        }
        int size = 0;
        for (int j = 0; asset && j < 32; j++) {
            if (asset->names[j]) {
                size = j + 1;
            }
        }
        if (used + size > 32) {
            if (error) {
                error->node = slot ? slot->node : 0;
                snprintf(
                    error->message,
                    sizeof error->message,
                    "Material slot %s exceeds 32 combined material parameter "
                    "slots.",
                    slot ? slot->name : "entity"
                );
            }
            return false;
        }
        entries[i] = (model_material_entry){ asset,
                                             slot ? slot->name : "",
                                             used,
                                             size,
                                             slot ? slot->node : 0 };
        if (asset && size) {
            memcpy(values + used, asset->defaults, (size_t)size * sizeof *values);
        }
        used += size;
    }
    return material_namespace_validate(entries, *length, error);
}
static bool material_source_resources_validate(
    const shader_asset *const *catalog,
    int count,
    shader_error *error
)
{
    const shader_image_source *images[RENDER_SHADER_IMAGE_MAX];
    int image_count = 0, ramps = 0;
    for (int i = 0; i < count; i++) {
        ramps += shader_graph_ramp_vec4_count(&catalog[i]->graph);
        if (ramps > RENDER_SHADER_RAMP_VEC4_MAX) {
            if (error) {
                snprintf(
                    error->message,
                    sizeof error->message,
                    "Model materials exceed %d ramp vec4 resources.",
                    RENDER_SHADER_RAMP_VEC4_MAX
                );
            }
            return false;
        }
        for (int j = 0; j < acount(catalog[i]->graph.images); j++) {
            const shader_image_source *image = &catalog[i]->graph.images[j];
            bool found = false;
            for (int k = 0; k < image_count; k++) {
                found |= image->color_space == images[k]->color_space &&
                         image->filter == images[k]->filter &&
                         image->wrap == images[k]->wrap &&
                         !strcmp(image->key, images[k]->key);
            }
            if (found) {
                continue;
            }
            if (image_count == RENDER_SHADER_IMAGE_MAX) {
                if (error) {
                    snprintf(
                        error->message,
                        sizeof error->message,
                        "Model materials exceed %d unique image resources.",
                        RENDER_SHADER_IMAGE_MAX
                    );
                }
                return false;
            }
            images[image_count++] = image;
        }
    }
    return true;
}
static bool model_material_contract_validate(
    const shader_asset *model,
    int replaced,
    const shader_asset *candidate,
    shader_error *error
)
{
    const shader_asset **catalog = NULL;
    for (int i = 0; i < acount(assets); i++) {
        const shader_asset *asset = i + 1 == replaced ? candidate : &assets[i];
        if (asset && asset->source_value && !asset->shape) {
            apush(catalog, asset);
        }
    }
    if (candidate && replaced > acount(assets) && !candidate->shape) {
        apush(catalog, candidate);
    }
    const shader_asset *empty = NULL;
    const shader_asset *const *validated_catalog = catalog ? catalog : &empty;
    for (int i = 0; i < acount(model->model.material_dependencies); i++) {
        const char *dependency = model->model.material_dependencies[i];
        if (!material_asset_find(dependency, validated_catalog, acount(catalog))) {
            if (error) {
                snprintf(
                    error->message,
                    sizeof error->message,
                    "Model %s requires Material %s.",
                    model->name,
                    dependency
                );
            }
            afree(catalog);
            return false;
        }
    }
    model_material_entry entries[MODEL_MATERIAL_SLOT_MAX + 1];
    float values[32][4];
    int length;
    bool ok = model_material_entries(model, NULL, validated_catalog, acount(catalog), entries, &length, values, error);
    const shader_asset **used = NULL;
    for (int i = 0; ok && i < length; i++) {
        const shader_asset *asset = entries[i].asset;
        bool seen = !asset;
        for (int j = 0; j < acount(used); j++) {
            seen |= used[j] == asset;
        }
        if (!seen) {
            apush(used, asset);
        }
    }
    ok = ok && material_source_resources_validate(used, acount(used), error);
    afree(used);
    afree(catalog);
    return ok;
}
static int material_parameter_slot(const model_material_entry *entry, const char *name)
{
    if (!entry->asset) {
        return -1;
    }
    int direct = parameter_slot(entry->asset, name);
    if (direct >= 0 || !*entry->scope) {
        return direct;
    }
    size_t length = strlen(entry->scope);
    return !strncmp(name, entry->scope, length) && name[length] == '.'
             ? parameter_slot(entry->asset, name + length + 1)
             : -1;
}
static bool binding_parameter_values_from(
    const shader_asset *shape,
    const shader_asset *material,
    const shader_asset *const *catalog,
    int count,
    const char *text,
    float shape_values[32][4],
    float material_values[32][4],
    float bindings[MODEL_MATERIAL_SLOT_MAX + 1][4],
    shader_error *error
)
{
    memset(shape_values, 0, 32 * 4 * sizeof(float));
    if (shape) {
        memcpy(shape_values, shape->defaults, sizeof shape->defaults);
    }
    model_material_entry entries[MODEL_MATERIAL_SLOT_MAX + 1];
    int length;
    if (!model_material_entries(shape, material, catalog, count, entries, &length, material_values, error)) {
        return false;
    }
    if (bindings) {
        memset(bindings, 0, (MODEL_MATERIAL_SLOT_MAX + 1) * 4 * sizeof(float));
        for (int i = 0; i < length; i++) {
            bindings[i][0] = entries[i].asset
                               ? render_shader_find(entries[i].asset->name)
                               : 0;
            bindings[i][1] = entries[i].offset;
            bindings[i][2] = entries[i].count;
        }
    }
    if (!text || !*text) {
        return true;
    }
    av_doc *doc = av_value_read(text, NULL, 0);
    av_value *root = doc ? av_doc_root(doc) : NULL;
    if (!root || !av_is_obj(root)) {
        if (error) {
            snprintf(error->message, sizeof error->message, "Shader parameters require named KDL values.");
        }
        av_doc_free(doc);
        return false;
    }
    bool seen_shape[32] = { 0 }, seen_material[32] = { 0 }, ok = true;
    av_obj_iter iterator = av_obj_iter_with(root);
    av_value *key;
    while ((key = av_obj_iter_next(&iterator))) {
        const char *name = av_get_str(key), *local = name;
        bool shape_only = name && !strncmp(name, "shape.", 6);
        bool material_only = name && !strncmp(name, "material.", 9);
        local += shape_only ? 6 : material_only ? 9 : 0;
        int s = shape && !material_only ? parameter_slot(shape, local) : -1;
        int m = -1, entry = -1, matches = s >= 0;
        for (int i = 0; !shape_only && i < length; i++) {
            int found = material_parameter_slot(&entries[i], local);
            if (found >= 0) {
                m = found;
                entry = i;
                matches++;
            }
        }
        if (matches != 1) {
            if (error) {
                snprintf(
                    error->message,
                    sizeof error->message,
                    "%s shader parameter '%s'; use shape.Name or "
                    "material.Slot.Name to disambiguate.",
                    matches ? "Ambiguous" : "Unknown",
                    name
                );
            }
            ok = false;
            break;
        }
        const shader_asset *asset = s >= 0 ? shape : entries[entry].asset;
        int slot = s >= 0 ? s : m;
        int flat = s >= 0 ? slot : entries[entry].offset + slot;
        bool *seen = s >= 0 ? seen_shape : seen_material;
        float *destination = s >= 0 ? shape_values[flat] : material_values[flat];
        if (seen[flat] ||
            !read_parameter_value(asset, slot, av_obj_iter_get_val(key), destination)) {
            if (error) {
                error->node = asset->is_model ? asset->model.parameters[slot].node : 0;
                snprintf(
                    error->message,
                    sizeof error->message,
                    "Parameter '%s' requires one override with %d finite "
                    "numeric components.",
                    name,
                    asset->widths[slot]
                );
            }
            ok = false;
            break;
        }
        seen[flat] = true;
    }
    av_doc_free(doc);
    return ok;
}
static bool binding_parameter_values(
    const shader_asset *shape,
    const shader_asset *material,
    const char *text,
    float shape_values[32][4],
    float material_values[32][4],
    shader_error *error
)
{
    return binding_parameter_values_from(shape, material, NULL, 0, text, shape_values, material_values, NULL, error);
}
bool render_shader_model_material_snapshot(
    int index,
    ecs_world *world,
    ecs_entity entity,
    const char *text,
    int fallback,
    float bindings[MODEL_MATERIAL_SLOT_MAX + 1][4],
    float parameters[32][4],
    render_shader_set *resources,
    shader_error *error
)
{
    if (!render_shader_is_model(index) || fallback < 0 || fallback > acount(assets) ||
        (fallback &&
         (!assets[fallback - 1].source_value || assets[fallback - 1].shape))) {
        if (error) {
            snprintf(error->message, sizeof error->message, "Model material snapshot requires valid model and Material IDs.");
        }
        return false;
    }
    const shader_asset *shape = &assets[index - 1];
    const shader_asset *material = fallback ? &assets[fallback - 1] : NULL;
    float geometry[32][4], values[32][4], table[MODEL_MATERIAL_SLOT_MAX + 1][4];
    if (!binding_parameter_values_from(shape, material, NULL, 0, text, geometry, values, table, error)) {
        return false;
    }
    render_shader_set set = *resources;
    if (!render_shader_set_add(&set, index, error) ||
        !render_shader_set_add(&set, fallback, error)) {
        return false;
    }
    for (int i = 0; i < acount(shape->model.material_slots); i++) {
        if (!render_shader_set_add(&set, (int)table[i + 1][0], error)) {
            return false;
        }
    }
    if (render_shader_ramp_vec4_count(&set) > RENDER_SHADER_RAMP_VEC4_MAX) {
        if (error) {
            snprintf(
                error->message,
                sizeof error->message,
                "Model materials exceed %d ramp vec4 resources.",
                RENDER_SHADER_RAMP_VEC4_MAX
            );
        }
        return false;
    }
    for (int i = 0; material && i < acount(overrides); i++) {
        const shader_override *override = &overrides[i];
        if (override->world != world || override->entity != ecs_entity_to_u64(entity) ||
            override->shader != fallback) {
            continue;
        }
        int slot = parameter_slot(material, override->name);
        if (slot >= 0 && override->width == material->widths[slot]) {
            memcpy(values[slot], override->value, sizeof override->value);
        }
    }
    memcpy(bindings, table, sizeof table);
    memcpy(parameters, values, sizeof values);
    *resources = set;
    return true;
}
static bool validate_model_values(const shader_asset *shape, const float incoming[32][4], shader_error *error)
{
    if (!shape || !shape->is_model) {
        return true;
    }
    float prepared[32][4];
    model_bounds bounds;
    return model_program_prepare(&shape->model, incoming, prepared, &bounds, error);
}
bool render_shader_binding_parameters_validate(const GameShader *binding, const char *text, shader_error *error)
{
    int shape_id = binding ? render_shader_find(binding->shape) : 0;
    int material_id = binding ? render_shader_find(binding->material) : 0;
    const shader_asset *shape = shape_id ? &assets[shape_id - 1] : NULL;
    const shader_asset *material = material_id ? &assets[material_id - 1] : NULL;
    float shape_values[32][4], material_values[32][4];
    return binding_parameter_values(shape, material, text, shape_values, material_values, error) &&
           validate_model_values(shape, shape_values, error);
}
static bool source_parameter_asset(
    const behavior_asset *source,
    const behavior_asset *const *catalog,
    int count,
    shader_asset *result,
    shader_error *error
)
{
    *result = (shader_asset){ .shape = source->domain != BEHAVIOR_DOMAIN_MATERIAL,
                              .is_model = source->domain == BEHAVIOR_DOMAIN_MODEL };
    model_program_init(&result->model);
    result->name = xstrdup(source->name);
    if (result->is_model) {
        if (!model_compile_with_catalog(source, catalog, count, &result->model, error)) {
            return false;
        }
        if (!render_image_validate_volumes(&result->model.shader, error)) {
            return false;
        }
        result->model.resolve_image = render_image_resolve;
        result->graph = result->model.shader;
        shader_graph_init(&result->model.shader);
        for (int i = 0; i < acount(result->model.parameters); i++) {
            const model_parameter *p = &result->model.parameters[i];
            memcpy(result->defaults[p->slot], p->defaults, sizeof p->defaults);
            if (p->hidden) {
                continue;
            }
            result->names[p->slot] = xstrdup(p->name);
            result->widths[p->slot] = p->width;
        }
        return true;
    }
    if (!shader_graph_from_graph(source->graph, &result->graph, error)) {
        return false;
    }
    for (int i = 0; i < acount(result->graph.nodes); i++) {
        const shader_node *n = &result->graph.nodes[i];
        if (n->op != SHADER_PARAMETER) {
            continue;
        }
        const node *authored = graph_node_by_id(source->graph, n->source_id);
        free(result->names[n->parameter]);
        result->names[n->parameter] = xstrdup(
            authored && authored->reference_name ? authored->reference_name : ""
        );
        result->widths[n->parameter] = n->width;
        memcpy(result->defaults[n->parameter], n->value, sizeof n->value);
    }
    return true;
}
bool render_shader_binding_sources_validate(
    const GameShader *binding,
    const char *text,
    const behavior_asset *const *sources,
    int count,
    shader_error *error
)
{
    const behavior_asset *shape = NULL, *material = NULL;
    for (int i = 0; binding && i < count; i++) {
        if (binding->shape && !strcmp(binding->shape, sources[i]->name)) {
            shape = sources[i];
        }
        if (binding->material && !strcmp(binding->material, sources[i]->name)) {
            material = sources[i];
        }
    }
    if (binding && ((binding->shape && *binding->shape && !shape) ||
                    (binding->material && *binding->material && !material))) {
        if (error) {
            snprintf(error->message, sizeof error->message, "Shader parameter binding references a missing asset.");
        }
        return false;
    }
    if ((shape && shape->domain != BEHAVIOR_DOMAIN_SHAPE &&
         shape->domain != BEHAVIOR_DOMAIN_MODEL) ||
        (material && material->domain != BEHAVIOR_DOMAIN_MATERIAL)) {
        if (error) {
            snprintf(error->message, sizeof error->message, "Shader parameter binding uses the wrong asset family.");
        }
        return false;
    }
    shader_asset s = { 0 };
    shader_asset *material_assets = NULL;
    const shader_asset **material_catalog = NULL;
    bool ok = !shape || source_parameter_asset(shape, sources, count, &s, error);
    for (int i = 0; ok && i < count; i++) {
        if (sources[i]->domain != BEHAVIOR_DOMAIN_MATERIAL) {
            continue;
        }
        bool used = material && !strcmp(material->name, sources[i]->name);
        for (int j = 0; shape && j < acount(s.model.material_slots); j++) {
            used |= !strcmp(s.model.material_slots[j].material, sources[i]->name);
        }
        if (!used) {
            continue;
        }
        shader_asset temporary = { 0 };
        ok = source_parameter_asset(sources[i], sources, count, &temporary, error);
        apush(material_assets, temporary);
    }
    for (int i = 0; i < acount(material_assets); i++) {
        apush(material_catalog, &material_assets[i]);
    }
    ok = ok &&
         material_source_resources_validate(material_catalog, acount(material_catalog), error);
    float sv[32][4], mv[32][4];
    if (ok) {
        const shader_asset *fallback = material ? material_asset_find(
                                                      material->name,
                                                      material_catalog,
                                                      acount(material_catalog)
                                                  )
                                                : NULL;
        ok = binding_parameter_values_from(
                 shape ? &s : NULL,
                 fallback,
                 material_catalog,
                 acount(material_catalog),
                 text,
                 sv,
                 mv,
                 NULL,
                 error
             ) &&
             validate_model_values(shape ? &s : NULL, sv, error);
    }
    free_asset(&s);
    for (int i = 0; i < acount(material_assets); i++) {
        free_asset(&material_assets[i]);
    }
    afree(material_assets);
    afree(material_catalog);
    return ok;
}
static bool model_snapshot_prepare(
    int index,
    const float incoming[32][4],
    float prepared[32][4],
    model_bounds *bounds,
    shader_error *error
)
{
    shader_asset *asset = &assets[index - 1];
    float normalized[32][4] = { 0 };
    for (int i = 0; i < 32; i++) {
        if (!asset->names[i]) {
            continue;
        }
        memcpy(
            normalized[i],
            incoming ? incoming[i] : asset->defaults[i],
            (size_t)asset->widths[i] * sizeof(float)
        );
    }
    for (int i = 0; i < acount(asset->snapshots); i++) {
        const model_snapshot *snapshot = &asset->snapshots[i];
        if (memcmp(normalized, snapshot->incoming, sizeof normalized)) {
            continue;
        }
        memcpy(prepared, snapshot->prepared, sizeof snapshot->prepared);
        *bounds = snapshot->bounds;
        model_stats.cache_hits++;
        return true;
    }
    model_snapshot fresh;
    memcpy(fresh.incoming, normalized, sizeof normalized);
    if (!model_program_prepare(&asset->model, normalized, fresh.prepared, &fresh.bounds, error)) {
        return false;
    }
    if (acount(asset->snapshots) < MODEL_SNAPSHOT_CACHE_MAX) {
        apush(asset->snapshots, fresh);
    } else {
        asset->snapshots[asset->next_snapshot] = fresh;
        asset->next_snapshot = (asset->next_snapshot + 1) % MODEL_SNAPSHOT_CACHE_MAX;
    }
    memcpy(prepared, fresh.prepared, sizeof fresh.prepared);
    *bounds = fresh.bounds;
    model_stats.parameter_updates++;
    model_stats.bounds_updates++;
    return true;
}
static unsigned model_caller_hash(ecs_world *world, ecs_entity entity)
{
    uint64_t value = (uint64_t)(uintptr_t)world ^
                     ((uint64_t)entity.slot * 0x9e3779b97f4a7c15ull);
    value ^= value >> 33;
    value *= 0xff51afd7ed558ccdull;
    return (unsigned)(value ^ (value >> 32));
}
static void model_caller_reindex(shader_asset *asset, int capacity)
{
    asetlen(asset->caller_slots, capacity);
    memset(asset->caller_slots, 0, (size_t)capacity * sizeof *asset->caller_slots);
    for (int i = 0; i < acount(asset->callers); i++) {
        const model_caller *caller = &asset->callers[i];
        int at = (int)(model_caller_hash(caller->world, caller->entity) &
                       (unsigned)(capacity - 1));
        while (asset->caller_slots[at]) {
            at = (at + 1) & (capacity - 1);
        }
        asset->caller_slots[at] = i + 1;
    }
}
static bool model_caller_prepare(
    int index,
    ecs_world *world,
    ecs_entity entity,
    const float incoming[32][4],
    float prepared[32][4],
    model_bounds *bounds,
    shader_error *error
)
{
    if (!world) {
        return model_snapshot_prepare(index, incoming, prepared, bounds, error);
    }
    shader_asset *asset = &assets[index - 1];
    int capacity = acount(asset->caller_slots);
    if (!capacity) {
        model_caller_reindex(asset, 16);
        capacity = 16;
    }
    int at = (int)(model_caller_hash(world, entity) & (unsigned)(capacity - 1));
    int found = -1;
    while (asset->caller_slots[at]) {
        int i = asset->caller_slots[at] - 1;
        if (asset->callers[i].world == world &&
            asset->callers[i].entity.slot == entity.slot) {
            found = i;
            break;
        }
        at = (at + 1) & (capacity - 1);
    }
    float normalized[32][4] = { 0 };
    for (int i = 0; i < 32; i++) {
        if (asset->names[i]) {
            memcpy(normalized[i], incoming[i], (size_t)asset->widths[i] * sizeof(float));
        }
    }
    if (found >= 0) {
        const model_caller *caller = &asset->callers[found];
        if (ecs_entity_to_u64(caller->entity) == ecs_entity_to_u64(entity) &&
            !memcmp(normalized, caller->snapshot.incoming, sizeof normalized)) {
            memcpy(prepared, caller->snapshot.prepared, sizeof caller->snapshot.prepared);
            *bounds = caller->snapshot.bounds;
            model_stats.cache_hits++;
            return true;
        }
    }
    model_caller fresh = { .world = world, .entity = entity };
    memcpy(fresh.snapshot.incoming, normalized, sizeof normalized);
    if (!model_snapshot_prepare(
            index,
            normalized,
            fresh.snapshot.prepared,
            &fresh.snapshot.bounds,
            error
        )) {
        return false;
    }
    if (found >= 0) {
        asset->callers[found] = fresh;
    } else {
        apush(asset->callers, fresh);
        if (acount(asset->callers) * 2 >= capacity) {
            model_caller_reindex(asset, capacity * 2);
        } else {
            asset->caller_slots[at] = acount(asset->callers);
        }
    }
    memcpy(prepared, fresh.snapshot.prepared, sizeof fresh.snapshot.prepared);
    *bounds = fresh.snapshot.bounds;
    return true;
}
render_model_stats render_shader_model_stats()
{
    render_model_stats result = model_stats;
    result.setter_validations = atomic_load_explicit(&model_setter_validations, memory_order_relaxed);
    result.program_revision = revision;
    result.parameter_revision = parameter_revision;
    return result;
}
void render_shader_model_stats_reset()
{
    model_stats = (render_model_stats){ 0 };
    atomic_store_explicit(&model_setter_validations, 0, memory_order_relaxed);
}
bool render_shader_model_snapshot(
    int index,
    ecs_world *world,
    ecs_entity entity,
    const char *text,
    float prepared[32][4],
    float min[3],
    float max[3],
    bool *empty,
    shader_error *error
)
{
    if (!render_shader_is_model(index)) {
        return false;
    }
    const GameShader *binding = world ? GAME_GameShader_get_const(world, entity) : NULL;
    int material_id = binding ? render_shader_find(binding->material) : 0;
    shader_asset *asset = &assets[index - 1];
    const shader_asset *material = material_id ? &assets[material_id - 1] : NULL;
    float incoming[32][4], material_values[32][4];
    if (!binding_parameter_values(asset, material, text, incoming, material_values, error)) {
        return false;
    }
    for (int j = 0; j < acount(overrides); j++) {
        const shader_override *v = &overrides[j];
        if (v->world != world || v->entity != ecs_entity_to_u64(entity) ||
            v->shader != index) {
            continue;
        }
        int slot = parameter_slot(asset, v->name);
        if (slot >= 0 && v->width == asset->widths[slot]) {
            memcpy(incoming[slot], v->value, sizeof v->value);
        }
    }
    model_bounds bounds;
    float temporary[32][4];
    if (!model_caller_prepare(index, world, entity, incoming, temporary, &bounds, error)) {
        return false;
    }
    memcpy(prepared, temporary, sizeof temporary);
    memcpy(min, bounds.min, sizeof bounds.min);
    memcpy(max, bounds.max, sizeof bounds.max);
    *empty = bounds.empty;
    return true;
}

static bool parse_material_parameters_scoped(
    const shader_asset *asset,
    const char *text,
    const char *scope,
    float values[32][4],
    shader_error *error
)
{
    memcpy(values, asset->defaults, sizeof asset->defaults);
    if (!text || !*text) {
        return true;
    }
    av_doc *doc = av_value_read(text, NULL, 0);
    av_value *root = doc ? av_doc_root(doc) : NULL;
    if (!root || !av_is_obj(root)) {
        if (error) {
            snprintf(error->message, sizeof error->message, "Shader parameters require named KDL values.");
        }
        av_doc_free(doc);
        return false;
    }
    bool seen[32] = { 0 }, ok = true;
    av_obj_iter iterator = av_obj_iter_with(root);
    av_value *key;
    while ((key = av_obj_iter_next(&iterator))) {
        const char *name = av_get_str(key);
        if (!strncmp(name, "shape.", 6)) {
            continue;
        }
        const char *local = !strncmp(name, "material.", 9) ? name + 9 : name;
        int slot = parameter_slot(asset, local);
        bool selected = false;
        size_t length = strlen(scope);
        if (slot < 0 && length && !strncmp(local, scope, length) &&
            local[length] == '.') {
            selected = true;
            slot = parameter_slot(asset, local + length + 1);
        }
        if (slot < 0 && !selected) {
            continue; /* Geometry or another material slot. */
        }
        if (slot < 0 || seen[slot] ||
            !read_parameter_value(asset, slot, av_obj_iter_get_val(key), values[slot])) {
            if (error) {
                snprintf(error->message, sizeof error->message, "Selected material parameter '%s' requires a known parameter and one override with the declared finite numeric width.", name);
            }
            ok = false;
            break;
        }
        seen[slot] = true;
    }
    av_doc_free(doc);
    return ok;
}
static void apply_parameter_overrides(
    const shader_asset *asset,
    int index,
    const ecs_world *world,
    ecs_entity entity,
    float values[32][4]
)
{
    for (int j = 0; j < acount(overrides); j++) {
        const shader_override *v = &overrides[j];
        if (v->world != world || v->entity != ecs_entity_to_u64(entity) ||
            v->shader != index) {
            continue;
        }
        for (int i = 0; i < 32; i++) {
            if (asset->names[i] && v->width == asset->widths[i] &&
                !strcmp(v->name, asset->names[i])) {
                memcpy(values[i], v->value, sizeof v->value);
            }
        }
    }
}
bool render_shader_material_parameters_scoped_cached(
    int index,
    ecs_world *world,
    ecs_entity entity,
    const char *text,
    const char *scope,
    float values[32][4],
    render_shader_parameter_cache **cache,
    shader_error *error
)
{
    if (index == 0) {
        memset(values, 0, 32 * 4 * sizeof(float));
        return true;
    }
    if (index < 0 || index > acount(assets) || assets[index - 1].shape) {
        if (error) {
            snprintf(error->message, sizeof error->message, "Scoped parameters require a material asset.");
        }
        return false;
    }
    const shader_asset *asset = &assets[index - 1];
    float fresh[32][4];
    if (text && *text) {
        scope = scope ? scope : "";
        int count = acount(*cache);
        if (count < index) {
            asetlen(*cache, index);
            memset(*cache + count, 0, (size_t)(index - count) * sizeof **cache);
        }
        render_shader_parameter_cache *c = &(*cache)[index - 1];
        if (c->revision != parameter_revision || !c->text ||
            strcmp(c->text, text) || !c->scope || strcmp(c->scope, scope)) {
            if (!parse_material_parameters_scoped(asset, text, scope, fresh, error)) {
                return false;
            }
            free(c->text);
            free(c->scope);
            c->text = xstrdup(text);
            c->scope = xstrdup(scope);
            c->revision = parameter_revision;
            memcpy(c->values, fresh, sizeof fresh);
        }
        memcpy(fresh, c->values, sizeof fresh);
    } else {
        memcpy(fresh, asset->defaults, sizeof fresh);
    }
    apply_parameter_overrides(asset, index, world, entity, fresh);
    memcpy(values, fresh, sizeof fresh);
    return true;
}

void render_shader_parameters_cached(
    int index,
    ecs_world *world,
    ecs_entity entity,
    const char *text,
    float values[32][4],
    render_shader_parameter_cache **cache
)
{
    if (index <= 0 || index > acount(assets)) {
        memset(values, 0, 32 * 4 * sizeof(float));
        return;
    }
    shader_asset *asset = &assets[index - 1];
    if (text && *text) {
        int count = acount(*cache);
        if (count < index) {
            asetlen(*cache, index);
            memset(*cache + count, 0, (size_t)(index - count) * sizeof **cache);
        }
        render_shader_parameter_cache *c = &(*cache)[index - 1];
        if (c->revision != parameter_revision || !c->text ||
            strcmp(c->text, text) || c->scope) {
            free(c->scope);
            c->scope = NULL;
            free(c->text);
            c->text = xstrdup(text);
            c->revision = parameter_revision;
            parse_parameters(asset, text, c->values);
        }
        memcpy(values, c->values, sizeof c->values);
    } else {
        memcpy(values, asset->defaults, sizeof asset->defaults);
    }
    apply_parameter_overrides(asset, index, world, entity, values);
}
void render_shader_parameters(
    int index,
    ecs_world *world,
    ecs_entity entity,
    const char *text,
    float values[32][4]
)
{
    render_shader_parameters_cached(index, world, entity, text, values, &serial_parameters);
}
bool render_shader_bounds(int index, float bounds[2])
{
    if (!render_shader_is_shape(index)) {
        return false;
    }
    memcpy(bounds, assets[index - 1].bounds, 2 * sizeof(float));
    return bounds[0] > 0 && bounds[1] > 0 && isfinite(bounds[0]) &&
           isfinite(bounds[1]);
}
bool render_shader_contains(
    const GameShader *binding,
    ecs_world *world,
    ecs_entity entity,
    const float local[2],
    const float wp[2],
    float time
)
{
    int id = binding ? render_shader_find(binding->shape) : 0;
    if (!render_shader_is_shape(id)) {
        return false;
    }
    float parameters[32][4], result[8][4];
    shader_error error;
    render_shader_parameters(id, world, entity, binding->parameters, parameters);
    shader_sample_context sample = {
        .position = { local[0], local[1] },
        .uv = { local[0] / (2 * assets[id - 1].bounds[0]) + 0.5f,
                0.5f - local[1] / (2 * assets[id - 1].bounds[1]) },
        .object_position = { local[0], 0, local[1] },
        .part_position = { local[0], 0, local[1] },
        .world_position = { wp[0], 0, wp[1] },
        .normal = { 0, 1, 0 },
        .view_direction = { 0, 1, 0 },
        .time = time,
    };
    return render_shader_sample(id, &sample, parameters, result, &error) &&
           result[0][0] <= 0;
}
void render_shader_clear_world(ecs_world *world)
{
    for (int i = 0; i < acount(assets); i++) {
        shader_asset *asset = &assets[i];
        bool changed = false;
        for (int j = acount(asset->callers) - 1; j >= 0; j--) {
            if (asset->callers[j].world == world) {
                adel(asset->callers, j);
                changed = true;
            }
        }
        if (changed && acount(asset->caller_slots)) {
            model_caller_reindex(asset, acount(asset->caller_slots));
        }
    }
    for (int i = acount(overrides) - 1; i >= 0; i--) {
        if (overrides[i].world == world) {
            free(overrides[i].name);
            adel(overrides, i);
        }
    }
}
uint64_t render_shader_overrides_hash(const ecs_world *world)
{
    uint64_t hash = 14695981039346656037ull;
    while (atomic_flag_test_and_set_explicit(&override_lock, memory_order_acquire)) {
    }
    for (int i = 0; i < acount(overrides); i++) {
        const shader_override *o = &overrides[i];
        if (o->world != world) {
            continue;
        }
        const unsigned char *parts[] = { (const unsigned char *)&o->entity,
                                         (const unsigned char *)o->name,
                                         (const unsigned char *)o->value,
                                         (const unsigned char *)&o->shader };
        size_t sizes[] = { sizeof o->entity,
                           strlen(o->name),
                           (size_t)o->width * sizeof *o->value,
                           sizeof o->shader };
        for (int p = 0; p < 4; p++) {
            for (size_t b = 0; b < sizes[p]; b++) {
                hash = (hash ^ parts[p][b]) * 1099511628211ull;
            }
        }
    }
    atomic_flag_clear_explicit(&override_lock, memory_order_release);
    return hash;
}

void render_shader_catalog_clear()
{
    render_shader_parameter_cache_free(serial_parameters);
    serial_parameters = NULL;
    for (int i = 0; i < acount(assets); i++) {
        free_asset(&assets[i]);
    }
    afree(assets);
    render_images_clear();
    render_mesh_assets_clear();
    for (int i = 0; i < acount(overrides); i++) {
        free(overrides[i].name);
    }
    afree(overrides);
    revision++;
    ramp_revision++;
    parameter_revision++;
    model_stats = (render_model_stats){ 0 };
    atomic_store_explicit(&model_setter_validations, 0, memory_order_relaxed);
}
static const char *write_parameter_value(void *service, const pin_value *inputs, bool shape)
{
    ecs_world *world = service;
    pin_value target = inputs[0], name = inputs[1], value = inputs[2];
    ecs_entity entity = ecs_entity_from_u64(target.u64);
    const GameShader *binding = world ? GAME_GameShader_get_const(world, entity) : NULL;
    int id = binding ? render_shader_find(shape ? binding->shape : binding->material)
                     : 0;
    if (!id || !name.str) {
        return "Choose an entity with a shader binding and a named parameter.";
    }
    int slot = -1;
    shader_asset *asset = &assets[id - 1];
    const char *local_name = name.str;
    const char *prefix = shape ? "shape." : "material.";
    if (!strncmp(local_name, prefix, strlen(prefix))) {
        local_name += strlen(prefix);
    }
    for (int i = 0; i < 32; i++) {
        if (asset->names[i] && !strcmp(asset->names[i], local_name)) {
            slot = i;
        }
    }
    if (slot < 0) {
        return "Shader parameter does not exist.";
    }
    shader_override fresh = { .world = world,
                              .entity = target.u64,
                              .width = asset->widths[slot],
                              .shader = id };
    if (fresh.width == 1 && value.type == PIN_F32) {
        fresh.value[0] = value.f;
    } else if (fresh.width == 1 && value.type == PIN_F64) {
        fresh.value[0] = (float)value.d;
    } else if (fresh.width == 1 && !asset->is_model && value.type == PIN_BOOL) {
        fresh.value[0] = value.b;
    } else if (
        value.type == PIN_STRUCT && value.ptr && value.type_name &&
        ((!strcmp(value.type_name, "ShaderVec2") && fresh.width == 2) ||
         (!strcmp(value.type_name, "ShaderVec3") && fresh.width == 3) ||
         (!strcmp(value.type_name, "ShaderVec4") && fresh.width == 4) ||
         (!strcmp(value.type_name, "CF_V2") && fresh.width == 2))
    ) {
        memcpy(fresh.value, value.ptr, (size_t)fresh.width * sizeof(float));
    } else {
        return "Shader parameter type does not match.";
    }
    for (int i = 0; i < fresh.width; i++) {
        if (!isfinite(fresh.value[i])) {
            return "Shader parameters must be finite.";
        }
    }
    if (asset->is_model) {
        static _Thread_local char parameter_error[256];
        int material_id = render_shader_find(binding->material);
        const shader_asset *material = material_id ? &assets[material_id - 1] : NULL;
        float incoming[32][4], material_values[32][4], prepared[32][4];
        shader_error error = { 0 };
        model_bounds bounds;
        if (!binding_parameter_values(asset, material, binding->parameters, incoming, material_values, &error)) {
            snprintf(parameter_error, sizeof parameter_error, "%s", error.message);
            return parameter_error;
        }
        while (atomic_flag_test_and_set_explicit(&override_lock, memory_order_acquire)) {
        }
        for (int i = 0; i < acount(overrides); i++) {
            const shader_override *o = &overrides[i];
            if (o->world != world || o->entity != target.u64 || o->shader != id) {
                continue;
            }
            int previous = parameter_slot(asset, o->name);
            if (previous >= 0 && o->width == asset->widths[previous]) {
                memcpy(incoming[previous], o->value, sizeof o->value);
            }
        }
        atomic_flag_clear_explicit(&override_lock, memory_order_release);
        memcpy(incoming[slot], fresh.value, sizeof fresh.value);
        atomic_fetch_add_explicit(&model_setter_validations, 1, memory_order_relaxed);
        if (!model_program_prepare(&asset->model, incoming, prepared, &bounds, &error)) {
            snprintf(parameter_error, sizeof parameter_error, "%s", error.message);
            return parameter_error;
        }
    }
    while (atomic_flag_test_and_set_explicit(&override_lock, memory_order_acquire)) {
    }
    for (int i = 0; i < acount(overrides); i++) {
        if (overrides[i].world == world && overrides[i].entity == target.u64 &&
            overrides[i].shader == id && !strcmp(overrides[i].name, local_name)) {
            memcpy(overrides[i].value, fresh.value, sizeof fresh.value);
            overrides[i].width = fresh.width;
            atomic_flag_clear_explicit(&override_lock, memory_order_release);
            return NULL;
        }
    }
    fresh.name = xstrdup(local_name);
    apush(overrides, fresh);
    atomic_flag_clear_explicit(&override_lock, memory_order_release);
    return NULL;
}
const char *render_shader_shape_parameter_set(
    ecs_world *world,
    ecs_entity entity,
    const char *name,
    pin_value value
)
{
    pin_value inputs[3] = { { .type = PIN_HANDLE,
                              .type_name = "ecs.entity",
                              .u64 = ecs_entity_to_u64(entity) },
                            { .type = PIN_STR, .str = name },
                            value };
    return write_parameter_value(world, inputs, true);
}
static const char *write_parameter(void *service, const pin_value *inputs, pin_value *)
{
    return write_parameter_value(service, inputs, false);
}
static const char *write_shape_parameter(void *service, const pin_value *inputs, pin_value *)
{
    return write_parameter_value(service, inputs, true);
}
static int flow_set_parameter(flow_context *ctx, bool shape)
{
    pin_value inputs[3];
    for (int i = 0; i < 3; i++) {
        int result = flow_context_eval_data(ctx, i + 1, &inputs[i]);
        if (result < 0) {
            return result;
        }
    }
    const char *error = write_parameter_value(flow_context_service(ctx, "ecs.world"), inputs, shape);
    return error ? flow_context_fail(ctx, FLOW_ERR_NODE, error)
                 : flow_context_continue(ctx, 0);
}
static int set_parameter(node *, flow_context *ctx)
{
    return flow_set_parameter(ctx, false);
}
static int set_shape_parameter(node *, flow_context *ctx)
{
    return flow_set_parameter(ctx, true);
}
static const node_host_call parameter_call = { .service = "ecs.world",
                                               .invoke = write_parameter };
static const node_host_call shape_parameter_call = { .service = "ecs.world",
                                                     .invoke = write_shape_parameter };
static pin_spec parameter_inputs[] = {
    { .name = "Exec", .type = PIN_FLOW, .kind = PIN_EXEC },
    { .name = "Entity", .type = PIN_HANDLE, .type_name = "ecs.entity" },
    { .name = "Name", .type = PIN_STR },
    { .name = "Value", .type = PIN_F32, .flags = PIN_FLAG_WILDCARD, .group = 1 },
};
static pin_spec parameter_outputs[] = {
    { .name = "Then", .type = PIN_FLOW, .kind = PIN_EXEC }
};
static const node_access parameter_access = {
    .reads = &GAME_COMPONENT_GameShader.id,
    .read_count = 1,
    .flags = NODE_EFFECT_PARALLEL | NODE_EFFECT_ENTITY_SCOPED | NODE_EFFECT_HOST_SINK,
    .target_input = 1
};
static node_desc parameter_node = {
    .name = "Set Material Parameter",
    .category = "Rendering/Material",
    .description = "Overrides the named Shader Parameter of Entity's material "
                   "for that entity only; Value must match its type.",
    .inputs = parameter_inputs,
    .input_count = 4,
    .outputs = parameter_outputs,
    .output_count = 1,
    .flow_kind = NODE_FLOW_CUSTOM,
    .flow = set_parameter,
    .host_call = &parameter_call,
    .display_class = NODE_DISPLAY_FLOW,
    .access = &parameter_access
};
static node_desc shape_parameter_node = {
    .name = "Set Shape Parameter",
    .category = "Rendering/Shape",
    .description = "Overrides a named Shape or Model parameter for one entity. "
                   "Model ranges and derived bounds are validated before "
                   "accepting the value.",
    .inputs = parameter_inputs,
    .input_count = 4,
    .outputs = parameter_outputs,
    .output_count = 1,
    .flow_kind = NODE_FLOW_CUSTOM,
    .flow = set_shape_parameter,
    .host_call = &shape_parameter_call,
    .display_class = NODE_DISPLAY_FLOW,
    .access = &parameter_access
};
static node_desc *binding_nodes[] = { &parameter_node, &shape_parameter_node };
const registry_pack SHADER_BINDING_PACK = { .name = "shader binding",
                                            .nodes = binding_nodes,
                                            .node_count = sizeof binding_nodes /
                                                          sizeof *binding_nodes };
