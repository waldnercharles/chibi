#include "render/field_image.h"

#include <math.h>

void render_field_image(const float *values, int width, int height, float spacing, CF_Pixel *pixels)
{
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            float value = values[y * width + x];
            float edge = expf(-fabsf(value) / spacing);
            CF_Pixel color = value <= 0
                               ? (CF_Pixel){ .r = 55, .g = 156, .b = 181, .a = 255 }
                               : (CF_Pixel){ .r = 17, .g = 24, .b = 35, .a = 255 };
            color.r = (uint8_t)(color.r + (240 - color.r) * edge);
            color.g = (uint8_t)(color.g + (240 - color.g) * edge);
            color.b = (uint8_t)(color.b + (240 - color.b) * edge);
            pixels[(height - 1 - y) * width + x] = color;
        }
    }
}
