#ifndef CHIBI_UI_RML_GPU_H
#define CHIBI_UI_RML_GPU_H

/* The RmlUi backend's GPU programs and post-processing passes, after RmlUi's
 * reference GL3 renderer: the filters a layer composites through, the mask
 * image a composite multiplies by, and the gradients RmlUi's decorators draw.
 * Every pass renders physical window pixels with a top-left origin. */

#include "platform/cute.h"
#include "ui/rml.h"

#include <limits.h>

/* Kept apart from application layers, so the RmlUi passes flush without
 * consuming queued scene geometry. */
#define RML_DRAW_LAYER (INT_MAX / 2)
#define RML_GRADIENT_STOPS 16

typedef enum rml_filter_kind
{
    RML_FILTER_OPACITY,
    RML_FILTER_BLUR,
    RML_FILTER_DROP_SHADOW,
    RML_FILTER_COLOR_MATRIX,
    RML_FILTER_MASK_IMAGE,
} rml_filter_kind;

/* A compiled filter. Its fields have no padding, so equal filters compare
 * equal with memcmp. */
typedef struct rml_filter
{
    rml_filter_kind kind;
    float value;    /* opacity */
    float sigma;    /* blur and drop-shadow */
    CF_V2 offset;   /* drop-shadow, in pixels */
    CF_Color color; /* drop-shadow, premultiplied */
    CF_V4 rows[3];  /* color matrix rows over premultiplied rgba */
} rml_filter;

typedef enum rml_gradient_function
{
    RML_GRADIENT_LINEAR,
    RML_GRADIENT_RADIAL,
    RML_GRADIENT_CONIC,
    RML_GRADIENT_REPEATING_LINEAR,
    RML_GRADIENT_REPEATING_RADIAL,
    RML_GRADIENT_REPEATING_CONIC,
} rml_gradient_function;

/* A compiled gradient, without padding like rml_filter. p is the linear
 * start, or the radial and conic center; v is the vector to the linear end,
 * the inverse radial radii, or the conic start direction. */
typedef struct rml_gradient
{
    rml_gradient_function function;
    CF_V2 p, v;
    int stop_count;
    float positions[RML_GRADIENT_STOPS];
    CF_Color colors[RML_GRADIENT_STOPS]; /* premultiplied */
} rml_gradient;

/* A canvas holding a window region: window pixel p is canvas pixel p - bounds.xy. */
typedef struct rml_surface
{
    CF_Canvas canvas;
    CF_Rect bounds;
} rml_surface;

typedef struct rml_gpu
{
    CF_Shader ui, resolve, gradient, copy, matrix, blur, shadow, mask;
    CF_Mesh quad; /* the unit square, which passes scale to their rectangle */
    CF_Texture white;
    /* Window-sized postprocess canvases, made on first use and kept until a
     * resize: primary, secondary and tertiary, as GL3 names them. */
    CF_Canvas post[3];
    CF_Canvas mask_canvas;
    int width, height;
    uint64_t passes;
} rml_gpu;

/* False for a filter or shader RmlUi names that the backend does not draw. */
bool rml_filter_compile(const char *name, const rml_effect_parameters *parameters, rml_filter *filter);
bool rml_gradient_compile(const char *name, const rml_effect_parameters *parameters, rml_gradient *gradient);

void rml_gpu_create(rml_gpu *gpu, int width, int height);
void rml_gpu_destroy(rml_gpu *gpu);
/* Drops the postprocess canvases, which a later pass remakes at the new size. */
void rml_gpu_resize(rml_gpu *gpu, int width, int height);
CF_Canvas rml_gpu_canvas(int width, int height, const char *name, bool stencil);
/* Premultiplied source-over, or a replacing write. */
CF_RenderState rml_gpu_blend_state(bool blend);
/* Pixel space for queued draws: window pixels over a canvas covering bounds. */
void rml_gpu_push_view(CF_Rect bounds);
void rml_gpu_pop_view();
/* Renders what the RmlUi draw layer queued into the canvas. */
void rml_gpu_flush(rml_gpu *gpu, CF_Canvas canvas, bool clear);
/* The texture coordinates of a window region in the surface. */
CF_V4 rml_surface_uv(rml_surface surface, CF_Rect region);
/* Queues a rectangle sampling uv, multiplied by tint, under the current view,
 * scissor and render state. */
void rml_gpu_quad(rml_gpu *gpu, CF_Rect rect, CF_Texture texture, CF_V4 uv, CF_Color tint);
/* Copies the region of source into the primary canvas and filters it there.
 * Returns the primary canvas, which then holds the region's result. */
CF_Canvas rml_gpu_filter(rml_gpu *gpu, rml_surface source, CF_Rect region, const rml_filter *filters, int count);
/* Keeps the region of source as the mask a mask-image filter multiplies by. */
void rml_gpu_save_mask(rml_gpu *gpu, rml_surface source, CF_Rect region);
/* Converts the premultiplied canvas into the straight-alpha one, both window-sized. */
void rml_gpu_resolve(rml_gpu *gpu, CF_Canvas premultiplied, CF_Canvas straight);
/* Pushes the gradient program and sets its parameters for the next draw. */
void rml_gpu_push_gradient(const rml_gpu *gpu, const rml_gradient *gradient);

#endif
