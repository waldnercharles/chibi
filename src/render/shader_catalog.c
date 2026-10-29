#include "render/shader_catalog.h"

#include "render/catalog_publication.h"
#include "render/scene3d_parts.h"
#include "render/shader_catalog_internal.h"
#include "render/shader_values.h"
#include "runtime/component_catalog.h"
#include "runtime/geometry/mesh_asset.h"
#include "runtime/images/catalog.h"
#include "runtime/images/sky.h"
#include "runtime/models/model_program.h"
#include "runtime/shaders/volume.h"
#include "support/arena.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "types/text.h"

#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
shader_asset *shader_catalog_assets;
shader_override *shader_catalog_overrides;
atomic_flag shader_catalog_override_lock = ATOMIC_FLAG_INIT;
unsigned shader_catalog_revision = 1, shader_catalog_ramp_revision = 1,
         shader_catalog_parameter_revision = 1;
render_model_stats shader_catalog_model_stats;
static atomic_int model_setter_validations;
static const void *prepared_publication;
void shader_catalog_publication_exchange(const void *expected, const void *next)
{
    assert(prepared_publication == expected);
    (void)prepared_publication;
    (void)expected;
    prepared_publication = next;
}

bool shader_catalog_model_material_contract_validate(
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
    for (int i = 0; i < acount(shader_catalog_assets); i++) {
        if (!strcmp(name, shader_catalog_assets[i].name)) {
            return i + 1;
        }
    }
    return 0;
}

const char *render_shader_name(int id)
{
    assert(id > 0 && id <= acount(shader_catalog_assets));
    return shader_catalog_assets[id - 1].name;
}

const shader_graph *render_shader_material_graph(int index)
{
    if (index <= 0 || index > acount(shader_catalog_assets) ||
        shader_catalog_assets[index - 1].shape ||
        shader_catalog_assets[index - 1].is_model) {
        return NULL;
    }
    return &shader_catalog_assets[index - 1].graph;
}

const char *render_shader_parameter_name(int index, int slot)
{
    return index > 0 && index <= acount(shader_catalog_assets) && slot >= 0 && slot < 32
             ? shader_catalog_assets[index - 1].names[slot]
             : NULL;
}
int render_shader_parameter_width(int index, int slot)
{
    return render_shader_parameter_name(index, slot)
             ? shader_catalog_assets[index - 1].widths[slot]
             : 0;
}
bool render_shader_model_anchor(int index, int slot, model_anchor *out)
{
    if (!render_shader_is_model(index) || slot < 0 ||
        slot >= acount(shader_catalog_assets[index - 1].model.anchors)) {
        return false;
    }
    *out = shader_catalog_assets[index - 1].model.anchors[slot];
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
           model_program_anchor(&shader_catalog_assets[index - 1].model, slot, prepared, position, angles, error);
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
           model_program_part_bounds(&shader_catalog_assets[index - 1].model, prepared, part, out, error);
}
bool render_shader_material_inspection(int index, shader_inspection *out)
{
    if (index <= 0 || index > acount(shader_catalog_assets) ||
        shader_catalog_assets[index - 1].shape) {
        return false;
    }
    *out = shader_catalog_assets[index - 1].material_inspection;
    return true;
}
const model_inspection *render_shader_model_inspection(int index)
{
    return render_shader_is_model(index) ? &shader_catalog_assets[index - 1].inspection
                                         : NULL;
}
bool render_shader_model_parameter(int index, int slot, model_parameter *out)
{
    if (!render_shader_is_model(index)) {
        return false;
    }
    const model_program *program = &shader_catalog_assets[index - 1].model;
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
    const model_program *program = &shader_catalog_assets[index - 1].model;
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
    if (!model_program_frame(&shader_catalog_assets[index - 1].model, part, prepared, rows, error)) {
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
    return model_program_part_bounds(&shader_catalog_assets[index - 1].model, prepared, part, bounds, error);
}
char *render_shader_model_preview_glsl(int index, int part, shader_error *error)
{
    if (!render_shader_is_model(index)) {
        return NULL;
    }
    shader_graph distance, hit;
    shader_graph_init(&distance);
    shader_graph_init(&hit);
    if (!model_program_part_shaders(&shader_catalog_assets[index - 1].model, part, &distance, &hit, error)) {
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
    for (int i = 0; i < acount(shader_catalog_assets[index - 1].images); i++) {
        append(&result, "vec4 sg_preview_distance_image_%d(vec2 uv){return sg_asset_%d_image_%d(uv); }\n", i, index, i);
        append(&result, "vec4 sg_preview_hit_image_%d(vec2 uv){return sg_asset_%d_image_%d(uv); }\n", i, index, i);
    }
    append(&result, "%s%s", d, h);
    free(d);
    free(h);
    return result;
}
static bool model_snapshot_prepare(
    int index,
    const float incoming[32][4],
    float prepared[32][4],
    model_bounds *bounds,
    shader_error *error
);
int shader_catalog_parameter_slot(const shader_asset *asset, const char *name)
{
    for (int i = 0; i < SHADER_PARAMETER_MAX; i++) {
        if (asset->names[i] && !strcmp(asset->names[i], name)) {
            return i;
        }
    }
    return -1;
}

float render_shader_distance_lipschitz(int index)
{
    return index > 0 && index <= acount(shader_catalog_assets)
             ? shader_graph_lipschitz(&shader_catalog_assets[index - 1].graph)
             : 1;
}
bool render_shader_is_shape(int index)
{
    return index > 0 && index <= acount(shader_catalog_assets) &&
           shader_catalog_assets[index - 1].shape;
}
bool render_shader_is_model(int index)
{
    return index > 0 && index <= acount(shader_catalog_assets) &&
           shader_catalog_assets[index - 1].is_model;
}
bool render_shader_is_particle(int index)
{
    if (index <= 0 || index > acount(shader_catalog_assets)) {
        return false;
    }
    const shader_asset *asset = &shader_catalog_assets[index - 1];
    return !asset->shape && !asset->is_model &&
           shader_particle_parameters_validate(asset->names, asset->widths, NULL) &&
           shader_graph_particle_validate(&asset->graph, NULL);
}
char *render_shader_particle_glsl(int index, shader_error *error)
{
    if (!render_shader_is_particle(index)) {
        if (error) {
            error->node = 0;
            snprintf(error->message, sizeof error->message, "Shader is not a particle function");
        }
        return NULL;
    }
    return shader_graph_glsl_named(&shader_catalog_assets[index - 1].graph, "sg_particle", error);
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
    if (index <= 0 || index > acount(shader_catalog_assets) ||
        !shader_catalog_assets[index - 1].name[0] || render_shader_is_shape(index)) {
        return false;
    }
    const shader_asset *asset = &shader_catalog_assets[index - 1];
    if (asset->graph.output_count == 3) {
        return true;
    }
    if (asset->alpha_mode != SHADER_ALPHA_BLEND) {
        return false;
    }
    const shader_node *opacity = &asset->graph.nodes[asset->graph.output[SHADER_SURFACE_OPACITY]];
    return opacity->op != SHADER_CONSTANT || opacity->value[0] < 1;
}
shader_alpha_mode render_shader_alpha_mode(int index)
{
    assert(index > 0 && index <= acount(shader_catalog_assets));
    return shader_catalog_assets[index - 1].alpha_mode;
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
    return index > 0 && index <= acount(shader_catalog_assets) &&
           shader_graph_eval_outputs(&shader_catalog_assets[index - 1].graph, &context, parameters, result, error);
}
unsigned render_shader_revision()
{
    return shader_catalog_revision;
}
unsigned render_shader_ramp_revision()
{
    return shader_catalog_ramp_revision;
}
void shader_catalog_free_asset(shader_asset *asset)
{
    free(asset->name);
    free(asset->glsl);
    afree(asset->images);
    shader_graph_free(&asset->graph);
    model_program_free(&asset->model);
    model_inspection_free(&asset->inspection);
    if (asset->runtime) {
        afree(asset->runtime->snapshots);
        afree(asset->runtime->callers);
        afree(asset->runtime->caller_slots);
        free(asset->runtime);
    }
    for (int i = 0; i < SHADER_PARAMETER_MAX; i++) {
        free(asset->names[i]);
    }
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
    const shader_asset *asset = &shader_catalog_assets[id - 1];
    if (!asset->name[0] || asset->graph.output_count == 3) {
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
            offset += shader_graph_ramp_vec4_count(&shader_catalog_assets[i].graph);
        }
    }
    return offset;
}

bool render_shader_set_add_image(render_shader_set *set, int image, shader_error *error)
{
    if (set_slot(set, image) >= 0) {
        return true;
    }
    if (set->count == RENDER_SHADER_IMAGE_MAX) {
        if (error) {
            snprintf(
                error->message,
                sizeof error->message,
                "Adding image %d exceeds %d image variants in one "
                "material/shape "
                "or composite group.",
                image,
                RENDER_SHADER_IMAGE_MAX
            );
        }
        return false;
    }
    int at = set->count++;
    while (at > 0 && set->images[at - 1] > image) {
        set->images[at] = set->images[at - 1];
        at--;
    }
    set->images[at] = image;
    return true;
}

bool render_shader_set_add(render_shader_set *set, int id, shader_error *error)
{
    if (id <= 0) {
        return true;
    }
    const shader_asset *asset = &shader_catalog_assets[id - 1];
    if (asset->graph.output_count == 3) {
        if (error) {
            snprintf(error->message, sizeof error->message, "Particle function requires a ParticleField3D renderer");
        }
        return false;
    }
    render_shader_set fresh = *set;
    for (int i = 0; i < acount(asset->images); i++) {
        if (!render_shader_set_add_image(&fresh, asset->images[i], error)) {
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
    }
    *set = fresh;
    return true;
}

int render_shader_ramp_vec4_count(const render_shader_set *set)
{
    int count = 0;
    for (int i = 0; i < acount(shader_catalog_assets); i++) {
        if (asset_in_set(set, i)) {
            int asset_count = shader_graph_ramp_vec4_count(
                &shader_catalog_assets[i].graph
            );
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
    for (int i = 0; i < acount(shader_catalog_assets); i++) {
        if (!asset_in_set(set, i)) {
            continue;
        }
        shader_graph_ramp_payload(&shader_catalog_assets[i].graph, payload + offset);
        offset += shader_graph_ramp_vec4_count(&shader_catalog_assets[i].graph);
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
    if (!index || !shader_catalog_assets[index - 1].shape ||
        !sphere_graph(&shader_catalog_assets[index - 1].graph)) {
        return false;
    }
    const shader_graph *g = &shader_catalog_assets[index - 1].graph;
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
    /* Sprites name their image and map variants; graphs reach theirs through
     * assets. */
    append(&text, "vec4 sg_image_sample(int id,vec2 uv) {\n");
    for (int i = 0; i < set->count; i++) {
        append(&text, " if(id==%d) return textureLod(sg_image_%d,uv,0.0);\n", set->images[i], i);
    }
    append(&text, " return vec4(0);\n}\n");
    append(&text, "#ifndef SG_SAMPLE_DEFINED\n#define SG_SAMPLE_DEFINED\nstruct SGSample { vec2 position; vec2 uv; vec3 object_position; vec3 part_position; vec3 object_from_world[3]; vec3 part_from_world[3]; vec3 uv_from_world[2]; vec3 world_position; vec3 normal; vec3 view_direction; float time; };\n#endif\n");
    for (int i = 0; i < acount(shader_catalog_assets); i++) {
        if (!render_shader_set_contains(set, i + 1)) {
            continue;
        }
        for (int j = 0; j < acount(shader_catalog_assets[i].images); j++) {
            const char *normalize = shader_catalog_assets[i].graph.images[j].wrap ==
                                            SHADER_IMAGE_REPEAT
                                      ? "uv-floor(uv)"
                                      : "clamp(uv,vec2(0),vec2(1))";
            /* Gradients come from the unwrapped UV, continuous across repeat
             * seams; programs without them sample the base level. */
            append(
                &text,
                "vec4 sg_asset_%d_image_%d(vec2 uv) { "
                "if(any(isnan(uv))||any(isinf(uv))) return vec4(0);\n"
                "#ifdef SG_IMAGE_GRADIENTS\n"
                " vec2 sg_dx=dFdx(uv),sg_dy=dFdy(uv); uv=%s; "
                "return textureGrad(sg_image_%d,uv,sg_dx,sg_dy);\n"
                "#else\n"
                " uv=%s; return textureLod(sg_image_%d,uv,0.0);\n"
                "#endif\n}\n",
                i + 1,
                j,
                normalize,
                set_slot(set, shader_catalog_assets[i].images[j]),
                normalize,
                set_slot(set, shader_catalog_assets[i].images[j])
            );
        }
        if (shader_catalog_assets[i].is_model) {
            for (int j = 0; j < acount(shader_catalog_assets[i].images); j++) {
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
        char *glsl = shader_graph_glsl_named_ramps(
            &shader_catalog_assets[i].graph,
            name,
            ramp_offset(set, i),
            &error
        );
        if (glsl) {
            append(&text, "%s\n", glsl);
        }
        free(glsl);
        if (shader_catalog_assets[i].is_model) {
            snprintf(name, sizeof name, "sg_asset_%d_hit", i + 1);
            char *hit = shader_graph_glsl_named(&shader_catalog_assets[i].model.hit_shader, name, &error);
            if (hit) {
                append(&text, "%s\n", hit);
            }
            free(hit);
            snprintf(name, sizeof name, "sg_asset_%d_frame", i + 1);
            char *frame = model_program_frame_glsl(&shader_catalog_assets[i].model, name, &error);
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
            shader_graph_lipschitz(&shader_catalog_assets[set->shape - 1].graph)
        );
        char name[64];
        snprintf(name, sizeof name, "sg_gradient_%d", set->shape);
        shader_error error = { 0 };
        char *gradient = shader_graph_glsl_gradient_named(
            &shader_catalog_assets[set->shape - 1].graph,
            name,
            &error
        );
        append(&text, "#define SG_ANALYTIC_GRADIENT %d\n", gradient != NULL);
        if (gradient) {
            append(
                &text,
                "%s\n#define SG_GRADIENT_CALL(context,values,tolerance) "
                "%s(context,values,tolerance)\n",
                gradient,
                name
            );
        }
        free(gradient);
        append(
            &text,
            "#define SG_ANALYTIC_SPHERE %d\n",
            sphere_graph(&shader_catalog_assets[set->shape - 1].graph)
        );
        append(&text, "#define SG_SHAPE_CALL(id,context,values,result) sg_asset_%d(context,values,result)\n", set->shape);
        if (set->material) {
            append(&text, "#define SG_MATERIAL_CALL(id,context,values,result) sg_asset_%d(context,values,result)\n", set->material);
        }
    }
    append(&text, "int sg_model_material_slot(int id,int part) { switch(id) {\n");
    for (int i = 0; i < acount(shader_catalog_assets); i++) {
        if (!shader_catalog_assets[i].is_model ||
            !render_shader_set_contains(set, i + 1)) {
            continue;
        }
        append(&text, "case %d: switch(part) {\n", i + 1);
        for (int j = 0; j < acount(shader_catalog_assets[i].model.parts); j++) {
            if (shader_catalog_assets[i].model.parts[j].material >= 0) {
                append(
                    &text,
                    "case %d: return %d;\n",
                    j,
                    shader_catalog_assets[i].model.parts[j].material
                );
            }
        }
        append(&text, "default: return -1; }\n");
    }
    append(&text, "default: return -1; } }\n");
    append(&text, "void sg_model_hit_dispatch(int id,SGSample context,vec4 values[32],out vec4 result[SG_OUTPUTS]) {\n for(int i=0;i<SG_OUTPUTS;i++) result[i]=vec4(0); result[0].xy=vec2(-1); result[1].xyz=context.object_position;\n switch(id) {\n");
    for (int i = 0; i < acount(shader_catalog_assets); i++) {
        if (shader_catalog_assets[i].is_model &&
            render_shader_set_contains(set, i + 1)) {
            append(&text, "case %d: sg_asset_%d_hit(context,values,result); break;\n", i + 1, i + 1);
        }
    }
    append(&text, "} }\n");
    append(&text, "void sg_model_frame_dispatch(int id,int part,SGSample context,vec4 values[32],out vec3 rows[3]) { rows[0]=vec3(1,0,0); rows[1]=vec3(0,1,0); rows[2]=vec3(0,0,1); switch(id) {\n");
    for (int i = 0; i < acount(shader_catalog_assets); i++) {
        if (shader_catalog_assets[i].is_model &&
            render_shader_set_contains(set, i + 1)) {
            append(&text, "case %d: sg_asset_%d_frame(part,context,values,rows); break;\n", i + 1, i + 1);
        }
    }
    append(&text, "default: break; } }\n");
    append(&text, "void sg_dispatch(int id,SGSample sg_context,vec4 values[32],out vec4 result[SG_OUTPUTS]) {\n for(int i=0;i<SG_OUTPUTS;i++) result[i]=vec4(0);\n switch(id) {\n");
    for (int i = 0; i < acount(shader_catalog_assets); i++) {
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
        "time,vec4 values[32],out vec4 result[SG_OUTPUTS]) {\n"
        " SGSample sg_context; sg_context.position=p; "
        "sg_context.uv=vec2(p.x,-p.y)/max(2.0*half_size,vec2(0.0001))+0.5;\n"
        " sg_context.object_position=vec3(p.x,0.0,p.y); "
        "sg_context.part_position=sg_context.object_position; "
        "sg_context.world_position=vec3(wp.x,0.0,wp.y);\n"
        " sg_context.object_from_world[0]=vec3(object_frame.x,0,object_frame.y)"
        "; sg_context.object_from_world[1]=vec3(0,1,0); "
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
    for (int i = 0; i < acount(shader_catalog_assets); i++) {
        if (shader_catalog_assets[i].shape) {
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
    unsigned shader_catalog_revision;
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
const shader_asset *shader_catalog_material_asset_find(
    const char *name,
    const shader_asset *const *catalog,
    int count
)
{
    if (catalog) {
        for (int i = 0; i < count; i++) {
            if (!strcmp(catalog[i]->name, name)) {
                return !catalog[i]->shape && catalog[i]->graph.output_count == SHADER_OUTPUT_MAX
                         ? catalog[i]
                         : NULL;
            }
        }
        return NULL;
    }
    int id = render_shader_find(name);
    return id && !shader_catalog_assets[id - 1].shape &&
                   shader_catalog_assets[id - 1].graph.output_count == SHADER_OUTPUT_MAX
             ? &shader_catalog_assets[id - 1]
             : NULL;
}
bool shader_catalog_model_material_entries(
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
    if (shape && fallback && fallback->graph.output_count != SHADER_OUTPUT_MAX) {
        if (error) {
            snprintf(error->message, sizeof error->message, "Shape material must return surface outputs");
        }
        return false;
    }
    int slots = shape && shape->is_model ? acount(shape->model.material_slots) : 0;
    int used = 0;
    *length = slots + 1;
    for (int i = 0; i <= slots; i++) {
        const model_material_slot *slot = i ? &shape->model.material_slots[i - 1]
                                            : NULL;
        const shader_asset *asset = slot
                                      ? shader_catalog_material_asset_find(slot->material, catalog, count)
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
bool shader_catalog_material_source_resources_validate(
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
bool shader_catalog_model_material_contract_validate(
    const shader_asset *model,
    int replaced,
    const shader_asset *candidate,
    shader_error *error
)
{
    const shader_asset **catalog = NULL;
    for (int i = 0; i < acount(shader_catalog_assets); i++) {
        const shader_asset *asset = i + 1 == replaced ? candidate
                                                      : &shader_catalog_assets[i];
        if (asset && asset->name[0] && !asset->shape &&
            asset->graph.output_count == SHADER_OUTPUT_MAX) {
            apush(catalog, asset);
        }
    }
    if (candidate && replaced > acount(shader_catalog_assets) &&
        !candidate->shape && candidate->graph.output_count == SHADER_OUTPUT_MAX) {
        apush(catalog, candidate);
    }
    const shader_asset *empty = NULL;
    const shader_asset *const *validated_catalog = catalog ? catalog : &empty;
    for (int i = 0; i < acount(model->model.material_dependencies); i++) {
        const char *dependency = model->model.material_dependencies[i];
        if (!shader_catalog_material_asset_find(dependency, validated_catalog, acount(catalog))) {
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
    bool ok = shader_catalog_model_material_entries(
        model,
        NULL,
        validated_catalog,
        acount(catalog),
        entries,
        &length,
        values,
        error
    );
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
    ok = ok &&
         shader_catalog_material_source_resources_validate(used, acount(used), error);
    afree(used);
    afree(catalog);
    return ok;
}

bool shader_catalog_binding_parameter_values_from(
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
    assert(!catalog && count == 0);
    (void)catalog;
    (void)count;
    if (text && *text) {
        return shader_values_binding(
            shape ? render_shader_find(shape->name) : 0,
            material ? render_shader_find(material->name) : 0,
            text,
            shape_values,
            material_values,
            bindings,
            error
        );
    }
    memset(shape_values, 0, 32 * 4 * sizeof(float));
    if (shape) {
        memcpy(shape_values, shape->defaults, sizeof shape->defaults);
    }
    model_material_entry entries[MODEL_MATERIAL_SLOT_MAX + 1];
    int length;
    if (!shader_catalog_model_material_entries(shape, material, NULL, 0, entries, &length, material_values, error)) {
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
    return true;
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
    return shader_catalog_binding_parameter_values_from(
        shape,
        material,
        NULL,
        0,
        text,
        shape_values,
        material_values,
        NULL,
        error
    );
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
    if (!render_shader_is_model(index) || fallback < 0 ||
        fallback > acount(shader_catalog_assets) ||
        (fallback && (!shader_catalog_assets[fallback - 1].name[0] ||
                      shader_catalog_assets[fallback - 1].shape ||
                      shader_catalog_assets[fallback - 1].graph.output_count !=
                          SHADER_OUTPUT_MAX))) {
        if (error) {
            snprintf(error->message, sizeof error->message, "Model material snapshot requires valid model and Material IDs.");
        }
        return false;
    }
    const shader_asset *shape = &shader_catalog_assets[index - 1];
    const shader_asset *material = fallback ? &shader_catalog_assets[fallback - 1]
                                            : NULL;
    float geometry[32][4], values[32][4], table[MODEL_MATERIAL_SLOT_MAX + 1][4];
    if (!shader_catalog_binding_parameter_values_from(shape, material, NULL, 0, text, geometry, values, table, error)) {
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
    for (int i = 0; material && i < acount(shader_catalog_overrides); i++) {
        const shader_override *override = &shader_catalog_overrides[i];
        if (override->world != world || override->entity != ecs_entity_to_u64(entity) ||
            override->shader != fallback) {
            continue;
        }
        int slot = shader_catalog_parameter_slot(material, override->name);
        if (slot >= 0 && override->width == material->widths[slot]) {
            memcpy(values[slot], override->value, sizeof override->value);
        }
    }
    memcpy(bindings, table, sizeof table);
    memcpy(parameters, values, sizeof values);
    *resources = set;
    return true;
}
bool shader_catalog_validate_model_values(
    const shader_asset *shape,
    const float incoming[32][4],
    shader_error *error
)
{
    if (!shape || !shape->is_model) {
        return true;
    }
    float prepared[32][4];
    model_bounds bounds;
    return model_program_prepare(&shape->model, incoming, prepared, &bounds, error);
}
bool render_shader_binding_parameters_validate(const Shader *binding, const char *text, shader_error *error)
{
    int shape_id = binding ? render_shader_find(binding->shape) : 0;
    int material_id = binding ? render_shader_find(binding->material) : 0;
    const shader_asset *shape = shape_id ? &shader_catalog_assets[shape_id - 1] : NULL;
    const shader_asset *material = material_id
                                     ? &shader_catalog_assets[material_id - 1]
                                     : NULL;
    float shape_values[32][4], material_values[32][4];
    return binding_parameter_values(shape, material, text, shape_values, material_values, error) &&
           shader_catalog_validate_model_values(shape, shape_values, error);
}

static bool model_snapshot_prepare(
    int index,
    const float incoming[32][4],
    float prepared[32][4],
    model_bounds *bounds,
    shader_error *error
)
{
    shader_asset *asset = &shader_catalog_assets[index - 1];
    if (!asset->runtime) {
        asset->runtime = xcalloc(1, sizeof *asset->runtime);
    }
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
    for (int i = 0; i < acount(asset->runtime->snapshots); i++) {
        const model_snapshot *snapshot = &asset->runtime->snapshots[i];
        if (memcmp(normalized, snapshot->incoming, sizeof normalized)) {
            continue;
        }
        memcpy(prepared, snapshot->prepared, sizeof snapshot->prepared);
        *bounds = snapshot->bounds;
        shader_catalog_model_stats.cache_hits++;
        return true;
    }
    model_snapshot fresh;
    memcpy(fresh.incoming, normalized, sizeof normalized);
    if (!model_program_prepare(&asset->model, normalized, fresh.prepared, &fresh.bounds, error)) {
        return false;
    }
    if (acount(asset->runtime->snapshots) < MODEL_SNAPSHOT_CACHE_MAX) {
        apush(asset->runtime->snapshots, fresh);
    } else {
        asset->runtime->snapshots[asset->runtime->next_snapshot] = fresh;
        asset->runtime->next_snapshot = (asset->runtime->next_snapshot + 1) %
                                        MODEL_SNAPSHOT_CACHE_MAX;
    }
    memcpy(prepared, fresh.prepared, sizeof fresh.prepared);
    *bounds = fresh.bounds;
    shader_catalog_model_stats.parameter_updates++;
    shader_catalog_model_stats.bounds_updates++;
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
    asetlen(asset->runtime->caller_slots, capacity);
    memset(
        asset->runtime->caller_slots,
        0,
        (size_t)capacity * sizeof *asset->runtime->caller_slots
    );
    for (int i = 0; i < acount(asset->runtime->callers); i++) {
        const model_caller *caller = &asset->runtime->callers[i];
        int at = (int)(model_caller_hash(caller->world, caller->entity) &
                       (unsigned)(capacity - 1));
        while (asset->runtime->caller_slots[at]) {
            at = (at + 1) & (capacity - 1);
        }
        asset->runtime->caller_slots[at] = i + 1;
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
    shader_asset *asset = &shader_catalog_assets[index - 1];
    if (!asset->runtime) {
        asset->runtime = xcalloc(1, sizeof *asset->runtime);
    }
    int capacity = acount(asset->runtime->caller_slots);
    if (!capacity) {
        model_caller_reindex(asset, 16);
        capacity = 16;
    }
    int at = (int)(model_caller_hash(world, entity) & (unsigned)(capacity - 1));
    int found = -1;
    while (asset->runtime->caller_slots[at]) {
        int i = asset->runtime->caller_slots[at] - 1;
        if (asset->runtime->callers[i].world == world &&
            asset->runtime->callers[i].entity.slot == entity.slot) {
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
        const model_caller *caller = &asset->runtime->callers[found];
        if (ecs_entity_to_u64(caller->entity) == ecs_entity_to_u64(entity) &&
            !memcmp(normalized, caller->snapshot.incoming, sizeof normalized)) {
            memcpy(prepared, caller->snapshot.prepared, sizeof caller->snapshot.prepared);
            *bounds = caller->snapshot.bounds;
            shader_catalog_model_stats.cache_hits++;
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
        asset->runtime->callers[found] = fresh;
    } else {
        apush(asset->runtime->callers, fresh);
        if (acount(asset->runtime->callers) * 2 >= capacity) {
            model_caller_reindex(asset, capacity * 2);
        } else {
            asset->runtime->caller_slots[at] = acount(asset->runtime->callers);
        }
    }
    memcpy(prepared, fresh.snapshot.prepared, sizeof fresh.snapshot.prepared);
    *bounds = fresh.snapshot.bounds;
    return true;
}
render_model_stats render_shader_model_stats()
{
    render_model_stats result = shader_catalog_model_stats;
    result.setter_validations = atomic_load_explicit(&model_setter_validations, memory_order_relaxed);
    result.program_revision = shader_catalog_revision;
    result.parameter_revision = shader_catalog_parameter_revision;
    return result;
}
void render_shader_model_stats_reset()
{
    shader_catalog_model_stats = (render_model_stats){ 0 };
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
    const Shader *binding = world ? Shader_get_const(world, entity) : NULL;
    int material_id = binding ? render_shader_find(binding->material) : 0;
    shader_asset *asset = &shader_catalog_assets[index - 1];
    const shader_asset *material = material_id
                                     ? &shader_catalog_assets[material_id - 1]
                                     : NULL;
    float incoming[32][4], material_values[32][4];
    if (!binding_parameter_values(asset, material, text, incoming, material_values, error)) {
        return false;
    }
    for (int j = 0; j < acount(shader_catalog_overrides); j++) {
        const shader_override *v = &shader_catalog_overrides[j];
        if (v->world != world || v->entity != ecs_entity_to_u64(entity) ||
            v->shader != index) {
            continue;
        }
        int slot = shader_catalog_parameter_slot(asset, v->name);
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

static void apply_parameter_overrides(
    const shader_asset *asset,
    int index,
    const ecs_world *world,
    ecs_entity entity,
    float values[32][4]
)
{
    for (int j = 0; j < acount(shader_catalog_overrides); j++) {
        const shader_override *v = &shader_catalog_overrides[j];
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
    if (index < 0 || index > acount(shader_catalog_assets) ||
        shader_catalog_assets[index - 1].shape) {
        if (error) {
            snprintf(error->message, sizeof error->message, "Scoped parameters require a material asset.");
        }
        return false;
    }
    const shader_asset *asset = &shader_catalog_assets[index - 1];
    float fresh[32][4];
    if (text && *text) {
        scope = scope ? scope : "";
        int count = acount(*cache);
        if (count < index) {
            asetlen(*cache, index);
            memset(*cache + count, 0, (size_t)(index - count) * sizeof **cache);
        }
        render_shader_parameter_cache *c = &(*cache)[index - 1];
        if (c->shader_catalog_revision != shader_catalog_parameter_revision || !c->text ||
            strcmp(c->text, text) || !c->scope || strcmp(c->scope, scope)) {
            if (!shader_values_scoped(index, text, scope, fresh, error)) {
                return false;
            }
            free(c->text);
            free(c->scope);
            c->text = xstrdup(text);
            c->scope = xstrdup(scope);
            c->shader_catalog_revision = shader_catalog_parameter_revision;
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
    if (index <= 0 || index > acount(shader_catalog_assets)) {
        memset(values, 0, 32 * 4 * sizeof(float));
        return;
    }
    shader_asset *asset = &shader_catalog_assets[index - 1];
    if (text && *text) {
        int count = acount(*cache);
        if (count < index) {
            asetlen(*cache, index);
            memset(*cache + count, 0, (size_t)(index - count) * sizeof **cache);
        }
        render_shader_parameter_cache *c = &(*cache)[index - 1];
        if (c->shader_catalog_revision != shader_catalog_parameter_revision ||
            !c->text || strcmp(c->text, text) || c->scope) {
            free(c->scope);
            c->scope = NULL;
            free(c->text);
            c->text = xstrdup(text);
            c->shader_catalog_revision = shader_catalog_parameter_revision;
            bool ready = shader_values_plain(index, text, c->values, NULL);
            assert(ready);
            (void)ready;
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
    memcpy(bounds, shader_catalog_assets[index - 1].bounds, 2 * sizeof(float));
    return bounds[0] > 0 && bounds[1] > 0 && isfinite(bounds[0]) &&
           isfinite(bounds[1]);
}
bool render_shader_contains(
    const Shader *binding,
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
    float parameters[32][4], result[SHADER_OUTPUT_MAX][4];
    shader_error error;
    render_shader_parameters(id, world, entity, binding->parameters, parameters);
    shader_sample_context sample = {
        .position = { local[0], local[1] },
        .uv = { local[0] / (2 * shader_catalog_assets[id - 1].bounds[0]) + 0.5f,
                0.5f - local[1] / (2 * shader_catalog_assets[id - 1].bounds[1]) },
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
    for (int i = 0; i < acount(shader_catalog_assets); i++) {
        shader_asset *asset = &shader_catalog_assets[i];
        if (!asset->runtime) {
            continue;
        }
        bool changed = false;
        for (int j = acount(asset->runtime->callers) - 1; j >= 0; j--) {
            if (asset->runtime->callers[j].world == world) {
                adel(asset->runtime->callers, j);
                changed = true;
            }
        }
        if (changed && acount(asset->runtime->caller_slots)) {
            model_caller_reindex(asset, acount(asset->runtime->caller_slots));
        }
    }
    for (int i = acount(shader_catalog_overrides) - 1; i >= 0; i--) {
        if (shader_catalog_overrides[i].world == world) {
            free(shader_catalog_overrides[i].name);
            adel(shader_catalog_overrides, i);
        }
    }
}
uint64_t render_shader_overrides_hash(const ecs_world *world)
{
    uint64_t hash = 14695981039346656037ull;
    while (atomic_flag_test_and_set_explicit(&shader_catalog_override_lock, memory_order_acquire)) {
    }
    for (int i = 0; i < acount(shader_catalog_overrides); i++) {
        const shader_override *o = &shader_catalog_overrides[i];
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
    atomic_flag_clear_explicit(&shader_catalog_override_lock, memory_order_release);
    return hash;
}

void shader_catalog_resources_changed()
{
    render_shader_parameter_cache_free(serial_parameters);
    serial_parameters = NULL;
    for (int i = 0; i < acount(shader_catalog_overrides); i++) {
        free(shader_catalog_overrides[i].name);
    }
    afree(shader_catalog_overrides);
    shader_catalog_revision++;
    shader_catalog_ramp_revision++;
    shader_catalog_parameter_revision++;
    shader_catalog_model_stats = (render_model_stats){ 0 };
    atomic_store_explicit(&model_setter_validations, 0, memory_order_relaxed);
}
void render_shader_catalog_clear()
{
    assert(!prepared_publication);
    shader_values_clear();
    render_scene3d_parts_clear();
    for (int i = 0; i < acount(shader_catalog_assets); i++) {
        shader_catalog_free_asset(&shader_catalog_assets[i]);
    }
    afree(shader_catalog_assets);
    render_image_catalog_clear();
    render_mesh_assets_clear();
    render_skies_clear();
    shader_catalog_resources_changed();
}
static const char *write_parameter_value(void *service, const pin_value *inputs, bool shape, const char *local_name)
{
    ecs_world *world = service;
    pin_value target = inputs[0], value = inputs[2];
    ecs_entity entity = ecs_entity_from_u64(target.u64);
    const Shader *binding = world ? Shader_get_const(world, entity) : NULL;
    int id = binding ? render_shader_find(shape ? binding->shape : binding->material)
                     : 0;
    if (!id) {
        return "Choose an entity with a shader binding and a named parameter.";
    }
    int slot = -1;
    shader_asset *asset = &shader_catalog_assets[id - 1];
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
         (!strcmp(value.type_name, "vec2") && fresh.width == 2))
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
        const shader_asset *material = material_id
                                         ? &shader_catalog_assets[material_id - 1]
                                         : NULL;
        float incoming[32][4], material_values[32][4], prepared[32][4];
        shader_error error = { 0 };
        model_bounds bounds;
        if (!binding_parameter_values(asset, material, binding->parameters, incoming, material_values, &error)) {
            snprintf(parameter_error, sizeof parameter_error, "%s", error.message);
            return parameter_error;
        }
        while (atomic_flag_test_and_set_explicit(&shader_catalog_override_lock, memory_order_acquire)) {
        }
        for (int i = 0; i < acount(shader_catalog_overrides); i++) {
            const shader_override *o = &shader_catalog_overrides[i];
            if (o->world != world || o->entity != target.u64 || o->shader != id) {
                continue;
            }
            int previous = shader_catalog_parameter_slot(asset, o->name);
            if (previous >= 0 && o->width == asset->widths[previous]) {
                memcpy(incoming[previous], o->value, sizeof o->value);
            }
        }
        atomic_flag_clear_explicit(&shader_catalog_override_lock, memory_order_release);
        memcpy(incoming[slot], fresh.value, sizeof fresh.value);
        atomic_fetch_add_explicit(&model_setter_validations, 1, memory_order_relaxed);
        if (!model_program_prepare(&asset->model, incoming, prepared, &bounds, &error)) {
            snprintf(parameter_error, sizeof parameter_error, "%s", error.message);
            return parameter_error;
        }
    }
    while (atomic_flag_test_and_set_explicit(&shader_catalog_override_lock, memory_order_acquire)) {
    }
    for (int i = 0; i < acount(shader_catalog_overrides); i++) {
        if (shader_catalog_overrides[i].world == world &&
            shader_catalog_overrides[i].entity == target.u64 &&
            shader_catalog_overrides[i].shader == id &&
            !strcmp(shader_catalog_overrides[i].name, local_name)) {
            memcpy(shader_catalog_overrides[i].value, fresh.value, sizeof fresh.value);
            shader_catalog_overrides[i].width = fresh.width;
            atomic_flag_clear_explicit(&shader_catalog_override_lock, memory_order_release);
            return NULL;
        }
    }
    fresh.name = xstrdup(local_name);
    apush(shader_catalog_overrides, fresh);
    atomic_flag_clear_explicit(&shader_catalog_override_lock, memory_order_release);
    return NULL;
}
const char *shader_catalog_write_parameter_value(void *service, const pin_value *inputs, bool shape)
{
    arena storage = { 0 };
    const char *name;
    const char *error = pin_text_cstring(&storage, inputs[1], &name, NULL);
    if (!error) {
        error = write_parameter_value(service, inputs, shape, name);
    }
    arena_free(&storage);
    return error;
}
const char *render_shader_shape_parameter_set(
    ecs_world *world,
    ecs_entity entity,
    const char *name,
    pin_value value
)
{
    size_t length = name ? strlen(name) : 0;
    if (length > UINT32_MAX) {
        return "Shader parameter name exceeds string length range.";
    }
    pin_string text = { .bytes = name ? name : "", .length = (uint32_t)length };
    pin_value inputs[3] = { { .type = PIN_HANDLE,
                              .type_name = "ecs.entity",
                              .u64 = ecs_entity_to_u64(entity) },
                            { .type = PIN_STR, .text = &text },
                            value };
    return shader_catalog_write_parameter_value(world, inputs, true);
}

static void model_overrides_migrate(
    int id,
    const shader_asset *old,
    const shader_asset *fresh,
    const char *renamed_from,
    const char *renamed_to
)
{
    for (int i = acount(shader_catalog_overrides) - 1; i >= 0; i--) {
        shader_override *override = &shader_catalog_overrides[i];
        if (override->shader != id) {
            continue;
        }
        int slot = shader_catalog_parameter_slot(old, override->name);
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
            adel(shader_catalog_overrides, i);
            continue;
        }
        free(override->name);
        override->name = xstrdup(after->name);
    }
}

void shader_catalog_model_overrides_migrate(
    const shader_model_migration *values,
    int count,
    const char *from,
    const char *to
)
{
    while (atomic_flag_test_and_set_explicit(&shader_catalog_override_lock, memory_order_acquire)) {
    }
    for (int i = 0; i < count; i++) {
        assert(values[i].id > 0 && values[i].id <= acount(shader_catalog_assets));
        model_overrides_migrate(
            values[i].id,
            &shader_catalog_assets[values[i].id - 1],
            values[i].replacement,
            from,
            to
        );
    }
    atomic_flag_clear_explicit(&shader_catalog_override_lock, memory_order_release);
}
