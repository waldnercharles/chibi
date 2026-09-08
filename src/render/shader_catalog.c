#include "render/shader_catalog.h"

#include "graph/document/library.h"
#include "graph/document/serialize.h"
#include "graph/runtime/flow.h"
#include "runtime/generated.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "third_party/yyjson/yyjson.h"

#include <dirent.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    char *name, *source_json;
    shader_graph graph;
    char *glsl;
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
static unsigned revision = 1;

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
const char *render_shader_name(int index)
{
    return index > 0 && index <= acount(assets) ? assets[index - 1].name : NULL;
}
int render_shader_count()
{
    return acount(assets);
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
void render_shader_parameter_default(int index, int slot, float value[4])
{
    memset(value, 0, 4 * sizeof(float));
    if (render_shader_parameter_name(index, slot)) {
        memcpy(value, assets[index - 1].defaults[slot], 4 * sizeof(float));
    }
}
bool render_shader_is_shape(int index)
{
    return index > 0 && index <= acount(assets) && assets[index - 1].shape;
}
unsigned render_shader_revision()
{
    return revision;
}
static void free_asset(shader_asset *asset)
{
    free(asset->name);
    free(asset->source_json);
    free(asset->glsl);
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
    memcpy(fresh.bounds, source->bounds, sizeof fresh.bounds);
    int id = render_shader_find(source->name);
    if (!id) {
        id = acount(assets) + 1;
    }
    char name[64];
    snprintf(name, sizeof name, "sg_asset_%d", id);
    fresh.glsl = shader_graph_glsl_named(&fresh.graph, name, error);
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
        if (names_equal && !strcmp(old->glsl, fresh.glsl) &&
            !memcmp(old->defaults, fresh.defaults, sizeof old->defaults) &&
            !memcmp(old->bounds, fresh.bounds, sizeof old->bounds)) {
            free(old->source_json);
            old->source_json = fresh.source_json;
            fresh.source_json = NULL;
            free_asset(&fresh);
            return true;
        }
        free_asset(old);
        *old = fresh;
    } else {
        apush(assets, fresh);
    }
    revision++;
    return true;
}
int render_shader_load(const char *root)
{
    const char *folders[] = { "shader-libraries", "materials", "shapes" };
    behavior_asset **sources = NULL;
    int loaded = 0;
    for (int f = 0; f < 3; f++) {
        char path[2048];
        snprintf(path, sizeof path, "%s/%s", root, folders[f]);
        DIR *dir = opendir(path);
        if (!dir) {
            continue;
        }
        struct dirent *entry;
        while ((entry = readdir(dir))) {
            if (entry->d_name[0] == '.') {
                continue;
            }
            snprintf(path, sizeof path, "%s/%s/%s", root, folders[f], entry->d_name);
            FILE *file = fopen(path, "rb");
            if (!file) {
                continue;
            }
            if (fseek(file, 0, SEEK_END)) {
                fclose(file);
                continue;
            }
            long length = ftell(file);
            rewind(file);
            if (length < 0) {
                fclose(file);
                continue;
            }
            char *json = xmalloc((size_t)length + 1);
            size_t size = fread(json, 1, (size_t)length, file);
            fclose(file);
            json[size] = 0;
            behavior_asset *asset = behavior_asset_load_json(json);
            free(json);
            if (asset) {
                apush(sources, asset);
            }
        }
        closedir(dir);
    }
    for (int i = 0; i < acount(sources); i++) {
        if (sources[i]->domain != BEHAVIOR_DOMAIN_MATERIAL &&
            sources[i]->domain != BEHAVIOR_DOMAIN_SHAPE) {
            continue;
        }
        char message[256];
        shader_error error;
        behavior_asset *linked = behavior_asset_link_libraries(
            sources[i],
            (const behavior_asset *const *)sources,
            acount(sources),
            message,
            sizeof message
        );
        if (linked && render_shader_upsert(linked, &error)) {
            loaded++;
        }
        behavior_asset_free(linked);
    }
    for (int i = 0; i < acount(sources); i++) {
        behavior_asset_free(sources[i]);
    }
    afree(sources);
    return loaded;
}
char *render_shader_glsl()
{
    char *text = NULL;
    for (int i = 0; i < acount(assets); i++) {
        append(&text, "%s\n", assets[i].glsl);
    }
    append(&text, "void sg_dispatch(int id,vec2 p,vec2 wp,float time,vec4 values[32],out vec4 result[8]) {\n for(int i=0;i<8;i++) result[i]=vec4(0);\n switch(id) {\n");
    for (int i = 0; i < acount(assets); i++) {
        append(&text, "case %d: sg_asset_%d(p,wp,time,values,result); break;\n", i + 1, i + 1);
    }
    append(&text, "default: break;\n }\n}\n");
    return text;
}
void render_shader_parameters(
    int index,
    ecs_world *world,
    ecs_entity entity,
    const char *json,
    float values[32][4]
)
{
    memset(values, 0, 32 * 4 * sizeof(float));
    if (index <= 0 || index > acount(assets)) {
        return;
    }
    shader_asset *asset = &assets[index - 1];
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
        for (int j = 0; j < acount(overrides); j++) {
            shader_override *v = &overrides[j];
            if (v->world == world && v->entity == ecs_entity_to_u64(entity) &&
                v->width == asset->widths[i] && !strcmp(v->name, asset->names[i])) {
                memcpy(values[i], v->value, sizeof v->value);
            }
        }
    }
    yyjson_doc_free(doc);
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
    return shader_graph_eval_outputs(&assets[id - 1].graph, local, wp, time, parameters, result, &error) &&
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
void render_shader_catalog_clear()
{
    for (int i = 0; i < acount(assets); i++) {
        free_asset(&assets[i]);
    }
    afree(assets);
    for (int i = 0; i < acount(overrides); i++) {
        free(overrides[i].name);
    }
    afree(overrides);
    revision++;
}
static int set_parameter(node *, flow_context *ctx)
{
    pin_value target, name, value;
    int result = flow_context_eval_data(ctx, 1, &target);
    if (result < 0) {
        return result;
    }
    result = flow_context_eval_data(ctx, 2, &name);
    if (result < 0) {
        return result;
    }
    result = flow_context_eval_data(ctx, 3, &value);
    if (result < 0) {
        return result;
    }
    ecs_world *world = flow_context_service(ctx, "ecs.world");
    ecs_entity entity = ecs_entity_from_u64(target.u64);
    const GameShader *binding = world ? GAME_GameShader_get_const(world, entity) : NULL;
    int id = binding ? render_shader_find(binding->material) : 0;
    if (!id || !name.str) {
        return flow_context_fail(ctx, FLOW_ERR_NODE, "Choose an entity with a material and a named parameter.");
    }
    int slot = -1;
    shader_asset *asset = &assets[id - 1];
    for (int i = 0; i < 32; i++) {
        if (asset->names[i] && !strcmp(asset->names[i], name.str)) {
            slot = i;
        }
    }
    if (slot < 0) {
        return flow_context_fail(ctx, FLOW_ERR_NODE, "Material parameter does not exist.");
    }
    shader_override fresh = { .world = world,
                              .entity = target.u64,
                              .width = asset->widths[slot] };
    if (fresh.width == 1 && value.type == PIN_F32) {
        fresh.value[0] = value.f;
    } else if (fresh.width == 1 && value.type == PIN_F64) {
        fresh.value[0] = (float)value.d;
    } else if (
        value.type == PIN_STRUCT && value.ptr && value.type_name &&
        ((!strcmp(value.type_name, "ShaderVec2") && fresh.width == 2) ||
         (!strcmp(value.type_name, "ShaderVec3") && fresh.width == 3) ||
         (!strcmp(value.type_name, "ShaderVec4") && fresh.width == 4) ||
         (!strcmp(value.type_name, "CF_V2") && fresh.width == 2))
    ) {
        memcpy(fresh.value, value.ptr, (size_t)fresh.width * sizeof(float));
    } else {
        return flow_context_fail(ctx, FLOW_ERR_NODE, "Material parameter type does not match.");
    }
    for (int i = 0; i < fresh.width; i++) {
        if (!isfinite(fresh.value[i])) {
            return flow_context_fail(ctx, FLOW_ERR_NODE, "Material parameters must be finite.");
        }
    }
    for (int i = 0; i < acount(overrides); i++) {
        if (overrides[i].world == world && overrides[i].entity == target.u64 &&
            !strcmp(overrides[i].name, name.str)) {
            memcpy(overrides[i].value, fresh.value, sizeof fresh.value);
            overrides[i].width = fresh.width;
            return flow_context_continue(ctx, 0);
        }
    }
    fresh.name = xstrdup(name.str);
    apush(overrides, fresh);
    return flow_context_continue(ctx, 0);
}
static pin_spec parameter_inputs[] = {
    { .name = "Exec", .type = PIN_FLOW, .kind = PIN_EXEC },
    { .name = "Entity", .type = PIN_HANDLE, .type_name = "ecs.entity" },
    { .name = "Name", .type = PIN_STR },
    { .name = "Value", .type = PIN_F32, .flags = PIN_FLAG_WILDCARD, .group = 1 },
};
static pin_spec parameter_outputs[] = {
    { .name = "Then", .type = PIN_FLOW, .kind = PIN_EXEC }
};
static const node_access parameter_access = { .flags = NODE_EFFECT_HOST_SINK };
static node_desc parameter_node = { .name = "Set Material Parameter",
                                    .category = "Rendering/Material",
                                    .inputs = parameter_inputs,
                                    .input_count = 4,
                                    .outputs = parameter_outputs,
                                    .output_count = 1,
                                    .flow_kind = NODE_FLOW_CUSTOM,
                                    .flow = set_parameter,
                                    .display_class = NODE_DISPLAY_FLOW,
                                    .access = &parameter_access };
static node_desc *binding_nodes[] = { &parameter_node };
const registry_pack SHADER_BINDING_PACK = { .name = "shader binding",
                                            .nodes = binding_nodes,
                                            .node_count = 1 };
