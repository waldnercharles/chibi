#ifndef CHIBI_RENDER_FIELD_IMAGE_H
#define CHIBI_RENDER_FIELD_IMAGE_H

#include "platform/cute.h"

/* A baked field as previews show it: inside (values at or below zero) teal,
 * outside dark, both lightening toward the iso-contour within one cell
 * `spacing`. `values` are grid rows from the bottom up; `pixels` are image
 * rows from the top down. */
void render_field_image(const float *values, int width, int height, float spacing, CF_Pixel *pixels);

#endif
