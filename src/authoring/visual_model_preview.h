#ifndef CHIBI_VISUAL_MODEL_PREVIEW_H
#define CHIBI_VISUAL_MODEL_PREVIEW_H
#include "authoring/visual_model.h"
#include "render/shader_catalog.h"
#include "render/view.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct visual_model_preview visual_model_preview;
/* GPU-thread, owned renderer/world/target. The current mutable resource scope
 * must remain active throughout construction, drawing and destruction. */
visual_model_preview *visual_model_preview_new(const char *name, int width, int height, char *error, size_t);
void visual_model_preview_free(visual_model_preview *);
bool visual_model_preview_pan(visual_model_preview *, float x, float y, char *error, size_t);
bool visual_model_preview_reference(
    visual_model_preview *,
    const visual_model_reference *,
    bool visible,
    float opacity,
    char *error,
    size_t
);
int visual_model_preview_reference_sampler(const visual_model_preview *);
/* Pure camera-plane corner projection, source alignment uses Y-up coordinates. */
bool visual_model_reference_corners(
    const visual_model_reference *,
    int image_width,
    int image_height,
    const engine_viewport_camera *,
    CF_V2 out[4]
);
/* Owns a validated parameter copy; draw and evidence snapshots use this exact binding. */
bool visual_model_preview_parameter_text(visual_model_preview *, const char *json, char *error, size_t);
bool visual_model_preview_inspection(
    visual_model_preview *,
    int part,
    int section_axis,
    float offset,
    char *error,
    size_t
);
bool visual_model_preview_images(visual_model_preview *, render_shader_set *, char *error, size_t);
uint64_t visual_model_preview_take_canvas(visual_model_preview *);
void visual_model_preview_parameters(const visual_model_preview *, float out[32][4]);
bool visual_model_preview_draw(
    visual_model_preview *,
    float time,
    float yaw,
    float pitch,
    float zoom,
    char *error,
    size_t
);
uint64_t visual_model_preview_texture(const visual_model_preview *);
#endif
