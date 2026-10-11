#include "render/screenshot.h"

#include "platform/cute.h"
#include "support/xalloc.h"

#include <stdio.h>
#include <stdlib.h>

#define READBACK_SECONDS 10

int app_save_screenshot(const char *path)
{
    CF_Canvas canvas = cf_app_get_canvas();
    int width = cf_app_get_canvas_width();
    int height = cf_app_get_canvas_height();
    CF_Readback readback = cf_canvas_readback(canvas);
    if (!readback.id) {
        fprintf(stderr, "screenshot readback could not be started\n");
        return 1;
    }
    int size = cf_readback_size(readback);
    size_t expected = (size_t)width * (size_t)height * sizeof(CF_Pixel);
    if (size <= 0 || width <= 0 || height <= 0 || (size_t)size != expected) {
        fprintf(stderr, "screenshot readback has invalid dimensions\n");
        cf_destroy_readback(readback);
        return 1;
    }
    CF_Pixel *pixels = xmalloc((size_t)size);
    uint64_t start = cf_get_ticks();
    while (!cf_readback_ready(readback)) {
        if (cf_get_ticks() - start > cf_get_tick_frequency() * READBACK_SECONDS) {
            fprintf(stderr, "screenshot readback timed out\n");
            free(pixels);
            cf_destroy_readback(readback);
            return 1;
        }
        cf_sleep(1);
    }
    int copied = cf_readback_data(readback, pixels, size);
    int result = 1;
    if (copied == size) {
        CF_Image image = { width, height, pixels };
        void *png_data = NULL;
        int png_size = 0;
        if (!cf_is_error(cf_image_save_png_to_memory(&image, &png_data, &png_size)) &&
            png_data && png_size > 0) {
            FILE *file = fopen(path, "wb");
            if (file && fwrite(png_data, 1, (size_t)png_size, file) == (size_t)png_size) {
                result = 0;
            }
            if (file && fclose(file)) {
                result = 1;
            }
        }
        cf_free(png_data);
    }
    free(pixels);
    cf_destroy_readback(readback);
    if (result != 0) {
        fprintf(stderr, "could not save screenshot: %s\n", path);
        return 1;
    }
    printf("screenshot saved: %s (%dx%d)\n", path, width, height);
    return 0;
}
