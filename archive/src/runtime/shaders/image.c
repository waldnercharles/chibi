#include "runtime/shaders/image.h"

#include "support/array.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

int shader_graph_add_image(shader_graph *g, const shader_image_source *source)
{
    for (int i = 0; i < acount(g->images); i++) {
        const shader_image_source *other = &g->images[i];
        if (other->color_space == source->color_space && other->filter == source->filter &&
            other->wrap == source->wrap && !strcmp(other->key, source->key)) {
            return i;
        }
    }
    shader_image_source copy = *source;
    copy.key = xstrdup(source->key);
    apush(g->images, copy);
    return acount(g->images) - 1;
}
bool shader_image_source_valid(const shader_image_source *source)
{
    return source->key && *source->key && source->color_space >= SHADER_IMAGE_SRGB &&
           source->color_space <= SHADER_IMAGE_LINEAR &&
           source->filter >= SHADER_IMAGE_NEAREST &&
           source->filter <= SHADER_IMAGE_BILINEAR &&
           source->wrap >= SHADER_IMAGE_REPEAT && source->wrap <= SHADER_IMAGE_CLAMP;
}
static int pixel_index(int64_t index, int size, shader_image_wrap wrap)
{
    if (wrap == SHADER_IMAGE_CLAMP) {
        return index < 0 ? 0 : index >= size ? size - 1 : (int)index;
    }
    int result = (int)(index % size);
    return result < 0 ? result + size : result;
}
static void texel(
    const shader_image_source *source,
    const shader_image_view *image,
    int64_t x,
    int64_t y,
    float value[4]
)
{
    int column = pixel_index(x, image->width, source->wrap);
    int row = pixel_index(y, image->height, source->wrap);
    const unsigned char *pixel = image->pixels +
                                 ((size_t)row * (size_t)image->width + (size_t)column) * 4;
    for (int c = 0; c < 4; c++) {
        float channel = (float)pixel[c] / 255.0f;
        if (c < 3 && source->color_space == SHADER_IMAGE_SRGB) {
            channel = channel <= .04045f ? channel / 12.92f
                                         : powf((channel + .055f) / 1.055f, 2.4f);
        }
        value[c] = channel;
    }
}
bool shader_image_sample(
    const shader_image_source *source,
    const shader_image_view *image,
    const float uv[2],
    float result[4]
)
{
    if (!image->pixels || image->width < 1 || image->height < 1 ||
        (size_t)image->width > SIZE_MAX / 4 / (size_t)image->height) {
        return false;
    }
    memset(result, 0, sizeof(float) * 4);
    if (!isfinite(uv[0]) || !isfinite(uv[1])) {
        return true;
    }
    float position[2];
    for (int i = 0; i < 2; i++) {
        float coordinate = source->wrap == SHADER_IMAGE_REPEAT
                             ? uv[i] - floorf(uv[i])
                             : fminf(1, fmaxf(0, uv[i]));
        position[i] = coordinate * (float)(i ? image->height : image->width);
    }
    if (source->filter == SHADER_IMAGE_NEAREST) {
        texel(source, image, (int64_t)floorf(position[0]), (int64_t)floorf(position[1]), result);
        return true;
    }
    float x = position[0] - .5f, y = position[1] - .5f;
    int64_t left = (int64_t)floorf(x), top = (int64_t)floorf(y);
    float tx = x - floorf(x), ty = y - floorf(y);
    float pixels[4][4];
    texel(source, image, left, top, pixels[0]);
    texel(source, image, left + 1, top, pixels[1]);
    texel(source, image, left, top + 1, pixels[2]);
    texel(source, image, left + 1, top + 1, pixels[3]);
    for (int c = 0; c < 4; c++) {
        float upper = pixels[0][c] + (pixels[1][c] - pixels[0][c]) * tx;
        float lower = pixels[2][c] + (pixels[3][c] - pixels[2][c]) * tx;
        result[c] = upper + (lower - upper) * ty;
    }
    return true;
}
