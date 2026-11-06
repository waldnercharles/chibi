#include "authoring/image_import.h"

#include "authoring/image_prepare.h"
#include "support/xalloc.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static uint64_t fingerprint(const void *data, size_t length)
{
    const unsigned char *bytes = data;
    uint64_t hash = UINT64_C(14695981039346656037);
    for (size_t i = 0; i < length; i++) {
        hash = (hash ^ bytes[i]) * UINT64_C(1099511628211);
    }
    return hash;
}
static bool fail(char *error, size_t size, const char *message)
{
    if (size) {
        snprintf(error, size, "%s", message);
    }
    return false;
}
static unsigned char quantize(double value)
{
    return (unsigned char)floor(fmax(0, fmin(1, value)) * 255 + .5);
}
static double srgb(double value)
{
    return value <= .0031308 ? value * 12.92 : 1.055 * pow(value, 1 / 2.4) - .055;
}
static void resample(shader_image_view source, shader_image_view output, image_import_semantic semantic)
{
    double linear[256];
    for (int i = 0; i < 256; i++) {
        double v = i / 255.0;
        linear[i] = v <= .04045 ? v / 12.92 : pow((v + .055) / 1.055, 2.4);
    }
    unsigned char *pixels = (unsigned char *)output.pixels;
    for (int y = 0; y < output.height; y++) {
        double top = (double)y * source.height / output.height;
        double bottom = (double)(y + 1) * source.height / output.height;
        for (int x = 0; x < output.width; x++) {
            double left = (double)x * source.width / output.width;
            double right = (double)(x + 1) * source.width / output.width;
            double sums[4] = { 0 }, area = (right - left) * (bottom - top);
            for (int sy = (int)floor(top); sy < (int)ceil(bottom); sy++) {
                double wy = fmin(bottom, sy + 1) - fmax(top, sy);
                for (int sx = (int)floor(left); sx < (int)ceil(right); sx++) {
                    double weight = wy * (fmin(right, sx + 1) - fmax(left, sx));
                    const unsigned char *p = source.pixels +
                                             ((size_t)sy * source.width + sx) * 4;
                    double alpha = p[3] / 255.0;
                    sums[3] += alpha * weight;
                    for (int c = 0; c < 3; c++) {
                        double v = semantic == IMAGE_IMPORT_COLOR ? linear[p[c]]
                                 : semantic == IMAGE_IMPORT_NORMAL
                                     ? 2 * (p[c] / 255.0) - 1
                                     : p[c] / 255.0;
                        sums[c] += v * weight *
                                   (semantic == IMAGE_IMPORT_SCALAR ? 1 : alpha);
                    }
                }
            }
            unsigned char *p = pixels + ((size_t)y * output.width + x) * 4;
            p[3] = quantize(sums[3] / area);
            if (semantic == IMAGE_IMPORT_NORMAL) {
                double length = sqrt(
                    sums[0] * sums[0] + sums[1] * sums[1] + sums[2] * sums[2]
                );
                for (int c = 0; c < 3; c++) {
                    double v = length > 1e-12 ? sums[c] / length : (c == 2 ? 1 : 0);
                    p[c] = quantize(v * .5 + .5);
                }
                continue;
            }
            for (int c = 0; c < 3; c++) {
                double value = semantic == IMAGE_IMPORT_COLOR
                                 ? srgb(sums[3] > 0 ? sums[c] / sums[3] : 0)
                                 : sums[c] / area;
                p[c] = quantize(value);
            }
        }
    }
}
static bool prepare(
    const void *bytes,
    size_t length,
    image_import_options options,
    int fit,
    image_import_plan *result,
    char *error,
    size_t size
)
{
    if (!result || options.semantic < IMAGE_IMPORT_COLOR ||
        options.semantic > IMAGE_IMPORT_NORMAL ||
        (fit < 0 && (options.width < 1 || options.height < 1 ||
                     options.width > RENDER_IMAGE_SIDE_MAX ||
                     options.height > RENDER_IMAGE_SIDE_MAX))) {
        return fail(error, size, "Choose color, scalar or normal semantics and explicit output dimensions within 1..2048");
    }
    shader_image_view source = { 0 };
    const char *format = image_prepare_import_decode(bytes, length, &source, error, size);
    if (!format) {
        return false;
    }
    if (fit >= 0) {
        int longest = source.width > source.height ? source.width : source.height;
        double scale = fit && longest > fit ? (double)fit / longest : 1;
        options.width = (int)fmax(1, floor(source.width * scale + .5));
        options.height = (int)fmax(1, floor(source.height * scale + .5));
        if (options.width > RENDER_IMAGE_SIDE_MAX ||
            options.height > RENDER_IMAGE_SIDE_MAX) {
            free((void *)source.pixels);
            return fail(error, size, "Source resolution exceeds 2048; explicitly choose the 1K or 2K derivative");
        }
    }
    image_import_plan plan = { .source_bytes = length,
                               .source_hash = fingerprint(bytes, length),
                               .source_width = source.width,
                               .source_height = source.height,
                               .width = options.width,
                               .height = options.height,
                               .semantic = options.semantic,
                               .source_format = format,
                               .pixel_bytes = (size_t)options.width *
                                              options.height * 4 };
    shader_image_view output = { xmalloc(plan.pixel_bytes),
                                 options.width,
                                 options.height };
    resample(source, output, options.semantic);
    free((void *)source.pixels);
    bool ok = image_prepare_png_encode(&output, &plan.png, &plan.png_bytes, error, size);
    unsigned char *prepared = NULL;
    shader_error diagnostic = { 0 };
    if (ok) {
        ok = render_image_pixels_prepared_write(&output, &prepared, &plan.prepared_bytes, &diagnostic);
        if (!ok) {
            fail(error, size, diagnostic.message);
        }
    }
    free((void *)output.pixels);
    if (!ok) {
        free(prepared);
        image_import_free(&plan);
        return false;
    }
    plan.output_hash = fingerprint(plan.png, plan.png_bytes);
    plan.prepared_hash = fingerprint(prepared, plan.prepared_bytes);
    free(prepared);
    *result = plan;
    return true;
}
bool image_import_prepare(
    const void *bytes,
    size_t length,
    image_import_options options,
    image_import_plan *result,
    char *error,
    size_t size
)
{
    return prepare(bytes, length, options, -1, result, error, size);
}
bool image_import_prepare_fit(
    const void *bytes,
    size_t length,
    int max_side,
    image_import_semantic semantic,
    image_import_plan *result,
    char *error,
    size_t size
)
{
    if (max_side != 0 && max_side != 1024 && max_side != 2048) {
        return fail(error, size, "Resolution choice must be source (0), 1024 or 2048");
    }
    return prepare(bytes, length, (image_import_options){ .semantic = semantic }, max_side, result, error, size);
}
void image_import_free(image_import_plan *plan)
{
    free(plan->png);
    *plan = (image_import_plan){ 0 };
}
