#ifndef CHIBI_SHADER_IMAGE_H
#define CHIBI_SHADER_IMAGE_H
#include "runtime/shaders/program.h"

bool shader_image_source_valid(const shader_image_source *source);
/* UV(0,0) is the top-left edge. Nonfinite coordinates return transparent black. */
bool shader_image_sample(
    const shader_image_source *source,
    const shader_image_view *image,
    const float uv[2],
    float result[4]
);
#endif
