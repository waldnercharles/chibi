#include "render/shader_catalog.h"

#include "graph/document/serialize.h"
#include "graph/runtime/flow.h"
#include "render/shader_images.h"
#include "runtime/generated.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "third_party/yyjson/yyjson.h"

#include <math.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    char *name, *source_json;
    shader_graph graph;
    char *glsl; /* Structural GLSL used to detect topology changes. */
    int *images;
    char *names[SHADER_PARAMETER_MAX];
    int widths[SHADER_PARAMETER_MAX];
    float defaults[SHADER_PARAMETER_MAX][4], bounds[2];
    bool shape;
} shader_asset;
typedef struct
{
    ecs_world *world;
    uint64_t entity;
    char *name;
    float value[4];
    int width;
} shader_override;
static shader_asset *assets;
static shader_override *overrides;
static atomic_flag override_lock = ATOMIC_FLAG_INIT;
static unsigned revision = 1;
static unsigned ramp_revision = 1;

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
static int parameter_slot(const shader_asset *asset, const char *name)
{
    for (int i = 0; i < SHADER_PARAMETER_MAX; i++) {
        if (asset->names[i] && !strcmp(asset->names[i], name)) {
            return i;
        }
    }
    return -1;
}
bool render_shader_parameters_validate(int index, const char *json, shader_error *error)
{
    if (error) {
        *error = (shader_error){ 0 };
    }
    if (!json || !*json) {
        return true;
    }
    if (index <= 0 || index > acount(assets)) {
        if (error) {
            snprintf(error->message, sizeof error->message, "Shader asset is not registered.");
        }
        return false;
    }
    yyjson_doc *doc = yyjson_read(json, strlen(json), 0);
    yyjson_val *root = doc ? yyjson_doc_get_root(doc) : NULL;
    if (!root || !yyjson_is_obj(root)) {
        if (error) {
            snprintf(error->message, sizeof error->message, "Preview parameters must be a JSON object.");
        }
        yyjson_doc_free(doc);
        return false;
    }
    shader_asset *asset = &assets[index - 1];
    yyjson_obj_iter iterator = yyjson_obj_iter_with(root);
    yyjson_val *key;
    int count = 0;
    while ((key = yyjson_obj_iter_next(&iterator))) {
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
            yyjson_doc_free(doc);
            return false;
        }
        const char *name = yyjson_get_str(key);
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
            yyjson_doc_free(doc);
            return false;
        }
        yyjson_val *value = yyjson_obj_iter_get_val(key);
        int width = asset->widths[slot];
        bool valid = width == 1 &&
                     ((yyjson_is_num(value) && isfinite(yyjson_get_num(value))) ||
                      yyjson_is_bool(value));
        if (!valid && width > 1 && yyjson_is_arr(value) &&
            (int)yyjson_arr_size(value) == width) {
            valid = true;
            for (int i = 0; i < width; i++) {
                yyjson_val *component = yyjson_arr_get(value, (size_t)i);
                valid &= yyjson_is_num(component) &&
                         isfinite(yyjson_get_num(component));
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
            yyjson_doc_free(doc);
            return false;
        }
    }
    yyjson_doc_free(doc);
    return true;
}
bool render_shader_is_shape(int index)
{
    return index > 0 && index <= acount(assets) && assets[index - 1].shape;
}
bool render_shader_may_be_transparent(int index)
{
    if (index <= 0 || index > acount(assets) || render_shader_is_shape(index)) {
        return false;
    }
    const shader_graph *graph = &assets[index - 1].graph;
    const shader_node *opacity = &graph->nodes[graph->output[5]];
    return opacity->op != SHADER_CONSTANT || opacity->value[0] < 1;
}
/* Whether a material's Surface Output input may be nonzero. */
static bool surface_input(int index, int input)
{
    if (index <= 0 || index > acount(assets) || render_shader_is_shape(index)) {
        return false;
    }
    const shader_graph *graph = &assets[index - 1].graph;
    if (graph->output_count <= input) {
        return false;
    }
    const shader_node *node = &graph->nodes[graph->output[input]];
    return node->op != SHADER_CONSTANT || node->value[0] != 0 ||
           node->value[1] != 0 || node->value[2] != 0;
}
bool render_shader_refracts(int index)
{
    return surface_input(index, SHADER_SURFACE_REFRACTION);
}
bool render_shader_transmits(int index)
{
    return surface_input(index, SHADER_SURFACE_TRANSMISSION);
}
bool render_shader_animated(int index)
{
    if (index <= 0 || index > acount(assets)) {
        return false;
    }
    const shader_graph *graph = &assets[index - 1].graph;
    for (int i = 0; i < acount(graph->nodes); i++) {
        switch (graph->nodes[i].op) {
            case SHADER_TIME:
            case SHADER_BEAT_PHASE:
            case SHADER_BEAT_PULSE:
            case SHADER_SURFACE_INFLUENCE: return true;
            default: break;
        }
    }
    return false;
}
bool render_shader_sample(
    int index,
    const shader_sample_context *sample,
    const float parameters[SHADER_PARAMETER_MAX][4],
    float result[SHADER_OUTPUT_MAX][4],
    shader_error *error
)
{
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
    free(asset->source_json);
    free(asset->glsl);
    afree(asset->images);
    shader_graph_free(&asset->graph);
    for (int i = 0; i < SHADER_PARAMETER_MAX; i++) {
        free(asset->names[i]);
    }
}
bool render_shader_upsert(const behavior_asset *source, shader_error *error)
{
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
    char *json = behavior_asset_save_json(source);
    int existing = render_shader_find(source->name);
    if (existing && json && assets[existing - 1].source_json &&
        !strcmp(json, assets[existing - 1].source_json)) {
        free(json);
        return true;
    }
    shader_asset fresh = { .shape = source->domain == BEHAVIOR_DOMAIN_SHAPE,
                           .source_json = json };
    if (!shader_graph_from_graph(source->graph, &fresh.graph, error)) {
        free(json);
        return false;
    }
    fresh.name = xstrdup(source->name);
    for (int i = 0; i < acount(fresh.graph.images); i++) {
        int variant = render_image_variant(&fresh.graph.images[i], error);
        if (!variant) {
            if (error) {
                for (int n = 0; n < acount(fresh.graph.nodes); n++) {
                    const shader_node *node = &fresh.graph.nodes[n];
                    if ((node->op == SHADER_IMAGE_TEXTURE ||
                         node->op == SHADER_IMAGE_TEXTURE_ALPHA) &&
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
            if (old->source_json && json && !strcmp(old->source_json, json)) {
                free(old->source_json);
                old->source_json = fresh.source_json;
                fresh.source_json = NULL;
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
    const shader_asset *asset = &assets[id - 1];
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

bool render_shader_set_add(render_shader_set *set, int id, shader_error *error)
{
    if (id <= 0) {
        return true;
    }
    const shader_asset *asset = &assets[id - 1];
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
    append(&text, "#ifndef SG_SAMPLE_DEFINED\n#define SG_SAMPLE_DEFINED\nstruct SGSample { vec2 position; vec2 uv; vec3 object_position; vec3 world_position; vec3 normal; vec3 view_direction; float time; };\n#endif\n");
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
        char name[64];
        snprintf(name, sizeof name, "sg_asset_%d", i + 1);
        shader_error error = { 0 };
        char *glsl = shader_graph_glsl_named_ramps(&assets[i].graph, name, ramp_offset(set, i), &error);
        if (glsl) {
            append(&text, "%s\n", glsl);
        }
        free(glsl);
    }
    /* 2D reads six mapped channels; keeping them apart from the full
     * Surface Output lets heavy graphs keep fewer values live. */
    append(&text, "#define SG_OUTPUTS %d\n#define SG_RESULTS_2D 6\n", SHADER_OUTPUT_MAX);
    append(&text, "void sg_dispatch(int id,SGSample sg_context,vec4 values[32],out vec4 result[SG_OUTPUTS]) {\n for(int i=0;i<SG_OUTPUTS;i++) result[i]=vec4(0);\n switch(id) {\n");
    for (int i = 0; i < acount(assets); i++) {
        if (!render_shader_set_contains(set, i + 1)) {
            continue;
        }
        append(&text, "case %d: sg_asset_%d(sg_context,values,result); break;\n", i + 1, i + 1);
    }
    append(&text, "default: break;\n }\n}\n");
    append(
        &text,
        "void sg_dispatch_2d(int id,vec2 p,vec2 wp,vec2 half_size,float "
        "time,vec4 values[32],out vec4 result[SG_RESULTS_2D]) {\n"
        " SGSample sg_context; sg_context.position=p; "
        "sg_context.uv=vec2(p.x,-p.y)/max(2.0*half_size,vec2(0.0001))+0.5;\n"
        " sg_context.object_position=vec3(p.x,0.0,p.y); "
        "sg_context.world_position=vec3(wp.x,0.0,wp.y);\n"
        " sg_context.normal=vec3(0,1,0); "
        "sg_context.view_direction=vec3(0,1,0); "
        "sg_context.time=time;\n"
        " vec4 surface[SG_OUTPUTS]; "
        "sg_dispatch(id,sg_context,values,surface);\n"
        " for(int i=0;i<SG_RESULTS_2D;i++) result[i]=surface[i];\n switch(id) "
        "{\n"
    );
    for (int i = 0; i < acount(assets); i++) {
        if (assets[i].shape) {
            append(&text, "case %d: return;\n", i + 1);
        }
    }
    append(
        &text,
        "default: break; }\n"
        " vec3 base=max(surface[0].rgb,vec3(0)); vec3 "
        "emission=max(surface[1].rgb,vec3(0));\n"
        " float opacity=clamp(surface[5].x,0.0,1.0); float "
        "metallic=clamp(surface[3].x,0.0,1.0); bool unlit=surface[6].x>0.5;\n"
        " float gloss=1.0-clamp(surface[2].x,0.0,1.0);\n"
        /* The surface normal faces the camera along +Y; its XZ tilt becomes
         * world X and up. A normal facing away keeps a maximal tilt. */
        " vec3 n=surface[4].xyz; float n2=dot(n,n); vec2 tilt=vec2(0);\n"
        " if(n2>1e-8) { tilt=n.xz*inversesqrt(n2); float t2=dot(tilt,tilt); "
        "if(n.y<=0.0&&t2>1e-8) tilt*=0.999*inversesqrt(t2); }\n"
        " result[0]=vec4(base,opacity); result[1]=vec4(emission,opacity);\n"
        " result[2]=vec4(unlit?vec3(0):base*(1.0-metallic),opacity); "
        "result[3]=vec4(vec3(opacity),opacity);\n"
        " result[4]=unlit?vec4(0):vec4(tilt,gloss,surface[7].x);\n"
        " result[5]=vec4(max(surface[8].x,0.0),clamp(surface[9].rgb,vec3(0),"
        "vec3(1)));\n}\n"
    );
    return text;
}
struct render_shader_parameter_cache
{
    unsigned revision;
    char *json;
    float values[SHADER_PARAMETER_MAX][4];
};
static render_shader_parameter_cache *serial_parameters;
void render_shader_parameter_cache_free(render_shader_parameter_cache *cache)
{
    for (int i = 0; i < acount(cache); i++) {
        free(cache[i].json);
    }
    afree(cache);
}
static void parse_parameters(const shader_asset *asset, const char *json, float values[32][4])
{
    memcpy(values, asset->defaults, sizeof asset->defaults);
    yyjson_doc *doc = json && *json ? yyjson_read(json, strlen(json), 0) : NULL;
    yyjson_val *root = doc ? yyjson_doc_get_root(doc) : NULL;
    for (int i = 0; i < 32; i++) {
        if (!asset->names[i]) {
            continue;
        }
        yyjson_val *value = root ? yyjson_obj_get(root, asset->names[i]) : NULL;
        float parsed[4] = { 0 };
        bool valid = false;
        if (yyjson_is_num(value) && asset->widths[i] == 1) {
            parsed[0] = (float)yyjson_get_num(value);
            valid = isfinite(parsed[0]);
        }
        if (yyjson_is_bool(value) && asset->widths[i] == 1) {
            parsed[0] = yyjson_get_bool(value);
            valid = true;
        }
        if (yyjson_is_arr(value) && (int)yyjson_arr_size(value) == asset->widths[i]) {
            valid = true;
            for (int j = 0; j < asset->widths[i]; j++) {
                yyjson_val *v = yyjson_arr_get(value, (size_t)j);
                parsed[j] = (float)yyjson_get_num(v);
                valid &= yyjson_is_num(v) && isfinite(parsed[j]);
            }
        }
        if (valid) {
            memcpy(values[i], parsed, sizeof parsed);
        }
    }
    yyjson_doc_free(doc);
}

void render_shader_parameters_cached(
    int index,
    ecs_world *world,
    ecs_entity entity,
    const char *json,
    float values[32][4],
    render_shader_parameter_cache **cache
)
{
    if (index <= 0 || index > acount(assets)) {
        memset(values, 0, 32 * 4 * sizeof(float));
        return;
    }
    shader_asset *asset = &assets[index - 1];
    if (json && *json) {
        int count = acount(*cache);
        if (count < index) {
            asetlen(*cache, index);
            memset(*cache + count, 0, (size_t)(index - count) * sizeof **cache);
        }
        render_shader_parameter_cache *c = &(*cache)[index - 1];
        if (c->revision != revision || !c->json || strcmp(c->json, json)) {
            free(c->json);
            c->json = xstrdup(json);
            c->revision = revision;
            parse_parameters(asset, json, c->values);
        }
        memcpy(values, c->values, sizeof c->values);
    } else {
        memcpy(values, asset->defaults, sizeof asset->defaults);
    }
    for (int j = 0; j < acount(overrides); j++) {
        const shader_override *v = &overrides[j];
        if (v->world != world || v->entity != ecs_entity_to_u64(entity)) {
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
void render_shader_parameters(
    int index,
    ecs_world *world,
    ecs_entity entity,
    const char *json,
    float values[32][4]
)
{
    render_shader_parameters_cached(index, world, entity, json, values, &serial_parameters);
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
    float parameters[32][4], result[SHADER_OUTPUT_MAX][4];
    shader_error error;
    render_shader_parameters(id, world, entity, binding->parameters, parameters);
    shader_sample_context sample = {
        .position = { local[0], local[1] },
        .uv = { local[0] / (2 * assets[id - 1].bounds[0]) + 0.5f,
                0.5f - local[1] / (2 * assets[id - 1].bounds[1]) },
        .object_position = { local[0], 0, local[1] },
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
                                         (const unsigned char *)o->value };
        size_t sizes[] = { sizeof o->entity,
                           strlen(o->name),
                           (size_t)o->width * sizeof *o->value };
        for (int p = 0; p < 3; p++) {
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
    for (int i = 0; i < acount(overrides); i++) {
        free(overrides[i].name);
    }
    afree(overrides);
    revision++;
    ramp_revision++;
}
static const char *write_parameter(void *service, const pin_value *inputs, pin_value *)
{
    ecs_world *world = service;
    pin_value target = inputs[0], name = inputs[1], value = inputs[2];
    ecs_entity entity = ecs_entity_from_u64(target.u64);
    const GameShader *binding = world ? GAME_GameShader_get_const(world, entity) : NULL;
    int id = binding ? render_shader_find(binding->material) : 0;
    if (!id || !name.str) {
        return "Choose an entity with a material and a named parameter.";
    }
    int slot = -1;
    shader_asset *asset = &assets[id - 1];
    for (int i = 0; i < 32; i++) {
        if (asset->names[i] && !strcmp(asset->names[i], name.str)) {
            slot = i;
        }
    }
    if (slot < 0) {
        return "Material parameter does not exist.";
    }
    shader_override fresh = { .world = world,
                              .entity = target.u64,
                              .width = asset->widths[slot] };
    if (fresh.width == 1 && value.type == PIN_F32) {
        fresh.value[0] = value.f;
    } else if (fresh.width == 1 && value.type == PIN_F64) {
        fresh.value[0] = (float)value.d;
    } else if (fresh.width == 1 && value.type == PIN_BOOL) {
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
        return "Material parameter type does not match.";
    }
    for (int i = 0; i < fresh.width; i++) {
        if (!isfinite(fresh.value[i])) {
            return "Material parameters must be finite.";
        }
    }
    while (atomic_flag_test_and_set_explicit(&override_lock, memory_order_acquire)) {
    }
    for (int i = 0; i < acount(overrides); i++) {
        if (overrides[i].world == world && overrides[i].entity == target.u64 &&
            !strcmp(overrides[i].name, name.str)) {
            memcpy(overrides[i].value, fresh.value, sizeof fresh.value);
            overrides[i].width = fresh.width;
            atomic_flag_clear_explicit(&override_lock, memory_order_release);
            return NULL;
        }
    }
    fresh.name = xstrdup(name.str);
    apush(overrides, fresh);
    atomic_flag_clear_explicit(&override_lock, memory_order_release);
    return NULL;
}
static int set_parameter(node *, flow_context *ctx)
{
    pin_value inputs[3];
    for (int i = 0; i < 3; i++) {
        int result = flow_context_eval_data(ctx, i + 1, &inputs[i]);
        if (result < 0) {
            return result;
        }
    }
    const char *error = write_parameter(flow_context_service(ctx, "ecs.world"), inputs, NULL);
    return error ? flow_context_fail(ctx, FLOW_ERR_NODE, error)
                 : flow_context_continue(ctx, 0);
}
static const node_host_call parameter_call = { .service = "ecs.world",
                                               .invoke = write_parameter };
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
static node_desc *binding_nodes[] = { &parameter_node };
const registry_pack SHADER_BINDING_PACK = { .name = "shader binding",
                                            .nodes = binding_nodes,
                                            .node_count = 1 };
