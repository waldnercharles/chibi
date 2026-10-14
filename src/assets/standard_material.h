#ifndef CHIBI_ASSETS_STANDARD_MATERIAL_H
#define CHIBI_ASSETS_STANDARD_MATERIAL_H

#include "graph/model/behavior_asset.h"
#include "runtime/shaders/program.h"

/* The Shader Parameter slots a Standard material names its factors with, so
 * entities can override each one. */
typedef enum
{
    STANDARD_BASE_COLOR,
    STANDARD_OPACITY,
    STANDARD_METALLIC,
    STANDARD_ROUGHNESS,
    STANDARD_EMISSION,
    STANDARD_NORMAL_SCALE,
    STANDARD_OCCLUSION_STRENGTH,
    STANDARD_PARAMETER_COUNT,
} standard_parameter;

typedef enum
{
    STANDARD_TEXTURE_BASE_COLOR,
    STANDARD_TEXTURE_METALLIC_ROUGHNESS,
    STANDARD_TEXTURE_NORMAL,
    STANDARD_TEXTURE_OCCLUSION,
    STANDARD_TEXTURE_EMISSION,
    STANDARD_TEXTURE_COUNT,
} standard_texture_slot;

/* The image a slot samples at the surface UV: a content/... key, or NULL
 * when the slot has none. */
typedef struct
{
    const char *image;
    shader_image_filter filter;
    shader_image_wrap wrap;
    /* Scale, then counterclockwise rotation in radians about (0, 0), then
     * offset. Identity requires scale = { 1, 1 }; zero scale is supported. */
    float offset[2], scale[2], rotation;
} standard_texture;

/* glTF 2.0's metallic-roughness material. Base color and emission are linear;
 * their images hold sRGB color, and the others linear data. Metallic is the
 * metallic-roughness image's blue channel and roughness its green, occlusion
 * is the red channel of its image, and normal images use the OpenGL
 * convention, whose green tilts up the image. */
typedef struct
{
    float base_color[4]; /* RGB and opacity */
    float metallic, roughness;
    float emission[3];
    float normal_scale, occlusion_strength;
    shader_alpha_mode alpha_mode;
    float alpha_cutoff;
    bool unlit;
    standard_texture textures[STANDARD_TEXTURE_COUNT];
} standard_material;

/* A material graph named `name` that shades as `material`. Every factor is a
 * Shader Parameter at its slot, except Opacity under Opaque and the normal
 * and occlusion strengths without their images. Each image is one Image
 * Texture node scaling its factor, and a slot without one adds no node. */
behavior_asset *standard_material_build(const char *name, const standard_material *material);

#endif
