#include "render/material_height.h"

#include "runtime/images/catalog.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    int index;
    shader_image_view view;
} height_image;

struct render_material_height
{
    shader_graph graph;
    int root;
    uint64_t key;
    float parameters[32][4];
    float (*scratch)[4];
    height_image *images;
};

static uint64_t hash_bytes(uint64_t hash, const void *data, size_t size)
{
    const unsigned char *bytes = data;
    for (size_t i = 0; i < size; i++) {
        hash = (hash ^ bytes[i]) * UINT64_C(1099511628211);
    }
    return hash;
}

static bool static_op(shader_op op)
{
    switch (op) {
        case SHADER_TIME:
        case SHADER_WORLD_POSITION:
        case SHADER_VIEW_DIRECTION:
        case SHADER_NORMAL:
        case SHADER_SURFACE_INFLUENCE:
        case SHADER_FRESNEL:
        case SHADER_NORMAL_MAP:
        case SHADER_BUMP:
        case SHADER_TRIPLANAR_TEXTURE:
        case SHADER_TRIPLANAR_TEXTURE_ALPHA:
        case SHADER_DECAL_MASK:
        case SHADER_SAMPLED_SDF:
        case SHADER_POSITION:
        case SHADER_OBJECT_POSITION:
        case SHADER_UV: return false;
        default: return true;
    }
}

void render_material_height_free(render_material_height *height)
{
    if (!height) {
        return;
    }
    shader_graph_free(&height->graph);
    afree(height->images);
    free(height->scratch);
    free(height);
}

static bool image_resolve(void *context, const shader_image_source *source, shader_image_view *view)
{
    render_material_height *height = context;
    for (int i = 0; i < acount(height->images); i++) {
        const shader_image_source *candidate = &height->graph
                                                    .images[height->images[i].index];
        if (!strcmp(source->key, candidate->key) &&
            source->color_space == candidate->color_space &&
            source->filter == candidate->filter && source->wrap == candidate->wrap) {
            *view = height->images[i].view;
            return true;
        }
    }
    return false;
}

static bool sample(void *context, CF_V2 point, float *value)
{
    render_material_height *height = context;
    shader_sample_context at = {
        .part_position = { point.x, 0, point.y },
        .part_from_world = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } },
        .resolve_image = image_resolve,
        .image_userdata = height
    };
    float result[4];
    shader_error error = { 0 };
    if (!shader_graph_eval_node_scratch(
            &height->graph,
            &at,
            height->parameters,
            height->root,
            height->scratch,
            result,
            &error
        ) ||
        !isfinite(result[0])) {
        return false;
    }
    *value = result[0];
    return true;
}

material_surface_height render_material_height_sampler(render_material_height *height)
{
    return (material_surface_height){ .sample = sample, .context = height };
}

bool render_material_height_prepare(
    ecs_world *world,
    ecs_entity entity,
    const Shader *binding,
    render_shader_parameter_cache **parameter_cache,
    render_material_height *old,
    render_material_height **next,
    char *error,
    size_t size
)
{
    int id = binding ? render_shader_find(binding->material) : 0;
    const shader_graph *graph = render_shader_material_graph(id);
    if (!graph || graph->output_count <= SHADER_SURFACE_HEIGHT) {
        snprintf(error, size, "MaterialSurface3D material_height needs a bound Surface Height material");
        return false;
    }
    int root = graph->output[SHADER_SURFACE_HEIGHT];
    bool selected[SHADER_NODE_MAX] = { false };
    bool image_used[SHADER_NODE_MAX] = { false };
    selected[root] = true;
    for (int i = root; i >= 0; i--) {
        if (!selected[i]) {
            continue;
        }
        const shader_node *node = &graph->nodes[i];
        if (!static_op(node->op)) {
            snprintf(
                error,
                size,
                "MaterialSurface3D Height cannot depend on %s; use static "
                "PartPosition coordinates",
                shader_op_name(node->op)
            );
            return false;
        }
        for (int j = 0; j < shader_op_inputs(node->op); j++) {
            selected[node->input[j]] = true;
        }
        if (node->op == SHADER_IMAGE_TEXTURE || node->op == SHADER_IMAGE_TEXTURE_ALPHA) {
            image_used[node->image] = true;
        }
    }
    float parameters[32][4];
    shader_error diagnostic = { 0 };
    if (!render_shader_material_parameters_scoped_cached(
            id,
            world,
            entity,
            binding->parameters,
            "",
            parameters,
            parameter_cache,
            &diagnostic
        )) {
        snprintf(
            error,
            size,
            "MaterialSurface3D Height parameters: %s",
            diagnostic.message
        );
        return false;
    }
    uint64_t key = UINT64_C(1469598103934665603);
    unsigned revision = render_shader_revision(),
             ramps = render_shader_ramp_revision();
    key = hash_bytes(key, &id, sizeof id);
    key = hash_bytes(key, &revision, sizeof revision);
    key = hash_bytes(key, &ramps, sizeof ramps);
    key = hash_bytes(key, parameters, sizeof parameters);
    height_image *images = NULL;
    for (int i = 0; i < acount(graph->images); i++) {
        if (!image_used[i]) {
            continue;
        }
        const shader_image_source *source = &graph->images[i];
        if (source->color_space != SHADER_IMAGE_LINEAR) {
            snprintf(
                error,
                size,
                "MaterialSurface3D Height image must use Linear color space: "
                "%.80s",
                source->key
            );
            afree(images);
            return false;
        }
        int image = render_image_variant_resident(source, &diagnostic);
        shader_image_source sampler;
        shader_image_view view;
        uint64_t stamp;
        if (!image || !render_image_sampler(image, &sampler, &view, &stamp)) {
            snprintf(
                error,
                size,
                "MaterialSurface3D Height image is not resident: %.80s",
                source->key
            );
            afree(images);
            return false;
        }
        key = hash_bytes(key, &image, sizeof image);
        key = hash_bytes(key, &stamp, sizeof stamp);
        key = hash_bytes(key, &source->color_space, sizeof source->color_space);
        key = hash_bytes(key, &source->filter, sizeof source->filter);
        key = hash_bytes(key, &source->wrap, sizeof source->wrap);
        apush(images, ((height_image){ i, view }));
    }
    if (old && old->key == key) {
        afree(images);
        *next = old;
        return true;
    }
    render_material_height *height = xcalloc(1, sizeof *height);
    if (!shader_graph_clone(graph, &height->graph, &diagnostic)) {
        snprintf(error, size, "MaterialSurface3D Height graph: %s", diagnostic.message);
        afree(images);
        free(height);
        return false;
    }
    height->root = root;
    height->key = key;
    height->images = images;
    memcpy(height->parameters, parameters, sizeof parameters);
    height->scratch = xcalloc((size_t)acount(graph->nodes), sizeof *height->scratch);
    *next = height;
    return true;
}
