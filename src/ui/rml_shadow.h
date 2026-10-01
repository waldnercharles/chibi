/* CPU offscreen passes requested by RmlUi's box-shadow texture generator. */
#pragma once
#include "platform/cute.h"
#include "ui/rml.h"

typedef struct rml_shadow_layer
{
    uintptr_t handle;
    uint8_t *pixels;
    uint8_t *mask;
    bool mask_set;
} rml_shadow_layer;

typedef struct rml_shadow_target
{
    int width, height;
    CF_M4x4 transform;
    bool scissor_enabled;
    CF_Rect scissor;
    bool clip_mask_enabled;
} rml_shadow_target;

void rml_shadow_geometry(rml_shadow_target target, rml_shadow_layer *layer, rml_mesh mesh, rml_vec2 translation);
void rml_shadow_mask(
    rml_shadow_target target,
    rml_shadow_layer *layer,
    rml_mesh mesh,
    rml_vec2 translation,
    rml_clip_mask_operation operation
);
uint8_t *rml_shadow_blur(rml_shadow_target target, const uint8_t *source, float sigma);
