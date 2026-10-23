#include "assets/standard_material.h"

#include "graph/authoring/graph_edit.h"
#include "graph/shader/shader_graph.h"
#include "types/text.h"

#include <assert.h>
#include <math.h>
#include <string.h>

enum
{
    IMAGE_COLOR,
    IMAGE_ALPHA,
};
enum
{
    NORMAL_MAP_COLOR,
    NORMAL_MAP_UV,
    NORMAL_MAP_STRENGTH,
    NORMAL_MAP_DISTANCE,
    NORMAL_MAP_CONVENTION,
};
enum
{
    MIX_A,
    MIX_B,
    MIX_ALPHA,
};

static const char *const PARAMETER_NAMES[STANDARD_PARAMETER_COUNT] = {
    [STANDARD_BASE_COLOR] = "Base Color",
    [STANDARD_OPACITY] = "Opacity",
    [STANDARD_METALLIC] = "Metallic",
    [STANDARD_ROUGHNESS] = "Roughness",
    [STANDARD_EMISSION] = "Emission",
    [STANDARD_NORMAL_SCALE] = "Normal Scale",
    [STANDARD_OCCLUSION_STRENGTH] = "Occlusion Strength",
};

/* Graph edits on the nodes and pins this builder made cannot fail. */
static void check(int status)
{
    assert(status == 0);
    (void)status;
}

static node *add(graph *g, const char *type)
{
    const node_desc *desc = NULL;
    for (int i = 0; i < SHADER_PACK.node_count && !desc; i++) {
        desc = strcmp(SHADER_PACK.nodes[i]->name, type) ? NULL
                                                        : SHADER_PACK.nodes[i];
    }
    assert(desc);
    node *n = graph_add_node(g, desc);
    assert(n);
    bool initialized = shader_graph_init_node(g, n);
    assert(initialized);
    (void)initialized;
    /* The editor arranges it, and the file holds no position. */
    n->placed = false;
    return n;
}

static void wire(graph *g, node *from, int pin, node *to, int input)
{
    check(graph_connect_data(g, from, pin, to, input));
}

static void set(graph *g, node *n, int input, pin_value value)
{
    check(graph_set_pin_default(g, n, 0, input, &value));
}

static node *parameter(graph *g, standard_parameter slot, pin_value value)
{
    node *n = add(g, "Shader Parameter");
    check(graph_set_node_reference(g, n, (uint64_t)slot, PARAMETER_NAMES[slot]));
    check(graph_set_pin_type(g, n, 1, 0, value.type, value.type_name));
    check(graph_set_pin_default(g, n, 1, 0, &value));
    return n;
}

static node *scalar(graph *g, standard_parameter slot, float value)
{
    return parameter(g, slot, (pin_value){ .type = PIN_F32, .f = value });
}

static node *color(graph *g, standard_parameter slot, const float value[3])
{
    float rgb[3] = { value[0], value[1], value[2] };
    return parameter(g, slot, (pin_value){ .type = PIN_STRUCT, .type_name = "ShaderVec3", .ptr = rgb });
}

/* The slot's image, or NULL when it has none. */
static node *texture(graph *g, const standard_texture *source, shader_image_color_space space)
{
    if (!source->image) {
        return NULL;
    }
    node *n = add(g, "Shader Image Texture");
    set(g,
        n,
        SHADER_IMAGE_INPUT_IMAGE,
        (pin_value){ .type = PIN_STR,
                     .text = &(pin_string){ .bytes = source->image,
                                            .length = (uint32_t)strlen(source->image) } });
    set(g, n, SHADER_IMAGE_INPUT_COLOR_SPACE, (pin_value){ .type = PIN_I32, .i = space });
    set(g,
        n,
        SHADER_IMAGE_INPUT_FILTER,
        (pin_value){ .type = PIN_I32, .i = source->filter });
    set(g, n, SHADER_IMAGE_INPUT_WRAP, (pin_value){ .type = PIN_I32, .i = source->wrap });
    if (source->offset[0] || source->offset[1] || source->scale[0] != 1 ||
        source->scale[1] != 1 || source->rotation) {
        node *mapping = add(g, "Shader Mapping");
        float offset[2] = { source->offset[0], source->offset[1] };
        float scale[2] = { source->scale[0], source->scale[1] };
        /* Mapping uses degrees. Reduce via sine/cosine before conversion so
         * even a finite angle near FLT_MAX stays finite and keeps its phase. */
        float degrees = atan2f(sinf(source->rotation), cosf(source->rotation)) *
                        57.29577951308232f;
        set(g,
            mapping,
            1,
            (pin_value){ .type = PIN_STRUCT, .type_name = "ShaderVec2", .ptr = offset });
        set(g, mapping, 2, (pin_value){ .type = PIN_F32, .f = degrees });
        set(g,
            mapping,
            3,
            (pin_value){ .type = PIN_STRUCT, .type_name = "ShaderVec2", .ptr = scale });
        wire(g, mapping, 0, n, SHADER_IMAGE_INPUT_UV);
    }
    return n;
}

/* One component of `image`'s color, through a `type` node such as Shader Y,
 * or NULL without an image. */
static node *channel(graph *g, node *image, const char *type)
{
    if (!image) {
        return NULL;
    }
    node *n = add(g, type);
    wire(g, image, IMAGE_COLOR, n, 0);
    return n;
}

/* `factor` times output `pin` of `source`, or `factor` alone without a source. */
static node *scaled(graph *g, node *factor, node *source, int pin)
{
    if (!source) {
        return factor;
    }
    node *product = add(g, "Shader Multiply");
    wire(g, source, pin, product, 0);
    wire(g, factor, 0, product, 1);
    return product;
}

/* The normal image decoded along the mesh's tangent frame, scaled by Normal
 * Scale. */
static void normal(graph *g, node *output, const standard_material *material)
{
    node *image = texture(g, &material->textures[STANDARD_TEXTURE_NORMAL], SHADER_IMAGE_LINEAR);
    if (!image) {
        return;
    }
    node *map = add(g, "Shader Normal Map");
    set(g,
        map,
        NORMAL_MAP_CONVENTION,
        (pin_value){ .type = PIN_I32, .i = SHADER_NORMAL_OPENGL_TANGENT });
    wire(g, image, IMAGE_COLOR, map, NORMAL_MAP_COLOR);
    wire(g, scalar(g, STANDARD_NORMAL_SCALE, material->normal_scale), 0, map, NORMAL_MAP_STRENGTH);
    wire(g, map, 0, output, SHADER_SURFACE_NORMAL);
}

/* glTF's occlusion: 1 + strength * (red - 1), so strength 0 leaves the light. */
static void occlusion(graph *g, node *output, const standard_material *material)
{
    node *red = channel(
        g,
        texture(g, &material->textures[STANDARD_TEXTURE_OCCLUSION], SHADER_IMAGE_LINEAR),
        "Shader X"
    );
    if (!red) {
        return;
    }
    node *mix = add(g, "Shader Mix");
    wire(g, red, 0, mix, MIX_B);
    wire(g, scalar(g, STANDARD_OCCLUSION_STRENGTH, material->occlusion_strength), 0, mix, MIX_ALPHA);
    set(g, mix, MIX_A, (pin_value){ .type = PIN_F32, .f = 1 });
    wire(g, mix, 0, output, SHADER_SURFACE_OCCLUSION);
}

behavior_asset *standard_material_build(const char *name, const standard_material *material)
{
    behavior_asset *asset = behavior_asset_new(name);
    asset->domain = BEHAVIOR_DOMAIN_MATERIAL;
    graph *g = asset->graph;
    node *output = shader_graph_add_output(g, false);
    output->placed = false;
    const standard_texture *textures = material->textures;
    node *base = texture(g, &textures[STANDARD_TEXTURE_BASE_COLOR], SHADER_IMAGE_SRGB);
    wire(
        g,
        scaled(g, color(g, STANDARD_BASE_COLOR, material->base_color), base, IMAGE_COLOR),
        0,
        output,
        SHADER_SURFACE_BASE_COLOR
    );
    if (material->alpha_mode != SHADER_ALPHA_OPAQUE) {
        node *opacity = scalar(g, STANDARD_OPACITY, material->base_color[3]);
        wire(g, scaled(g, opacity, base, IMAGE_ALPHA), 0, output, SHADER_SURFACE_OPACITY);
    }
    node *packed = texture(g, &textures[STANDARD_TEXTURE_METALLIC_ROUGHNESS], SHADER_IMAGE_LINEAR);
    node *metallic = scalar(g, STANDARD_METALLIC, material->metallic);
    node *roughness = scalar(g, STANDARD_ROUGHNESS, material->roughness);
    wire(g, scaled(g, metallic, channel(g, packed, "Shader Z"), 0), 0, output, SHADER_SURFACE_METALLIC);
    wire(g, scaled(g, roughness, channel(g, packed, "Shader Y"), 0), 0, output, SHADER_SURFACE_ROUGHNESS);
    node *emission = color(g, STANDARD_EMISSION, material->emission);
    node *glow = texture(g, &textures[STANDARD_TEXTURE_EMISSION], SHADER_IMAGE_SRGB);
    wire(g, scaled(g, emission, glow, IMAGE_COLOR), 0, output, SHADER_SURFACE_EMISSION);
    normal(g, output, material);
    occlusion(g, output, material);
    set(g,
        output,
        SHADER_SURFACE_ALPHA_MODE,
        (pin_value){ .type = PIN_I32, .i = material->alpha_mode });
    set(g,
        output,
        SHADER_SURFACE_ALPHA_CUTOFF,
        (pin_value){ .type = PIN_F32, .f = material->alpha_cutoff });
    set(g,
        output,
        SHADER_SURFACE_UNLIT,
        (pin_value){ .type = PIN_BOOL, .b = material->unlit });
    return asset;
}
