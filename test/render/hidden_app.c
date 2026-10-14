#include "hidden_app.h"

#include "render/scene_renderer.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>

void test_open_hidden_app(const char *title, int width, int height)
{
#ifdef __linux__
    if (!getenv("SDL_VIDEODRIVER")) {
        setenv("SDL_VIDEO_DRIVER", "offscreen", 0);
    }
#endif
    setenv("CHIBI_RENDER_SHADER_DIR", CHIBI_SOURCE_ROOT "/src/render/shaders", 0);
    CF_AppOptionFlags options = CF_APP_OPTIONS_NO_AUDIO_BIT | CF_APP_OPTIONS_HIDDEN_BIT;
    CF_Result made = cf_make_app(title, 0, 0, 0, width, height, options, NULL);
    assert(!cf_is_error(made));
    app_scene_renderer_initialize();
}

bool test_read_canvas(CF_Canvas target, void *data, int size)
{
    CF_Readback readback = cf_canvas_readback(target);
    assert(readback.id);
    while (!cf_readback_ready(readback)) {
        cf_sleep(1);
    }
    bool read = cf_readback_size(readback) == size &&
                cf_readback_data(readback, data, size) == size;
    cf_destroy_readback(readback);
    return read;
}

bool test_read_texture(CF_Texture texture, uint16_t *halves, int count)
{
    CF_CanvasParams params = cf_canvas_defaults(0, 0);
    params.attach_target = texture;
    CF_Canvas target = cf_make_canvas(params);
    assert(target.id);
    bool read = test_read_canvas(target, halves, count * 4 * (int)sizeof *halves);
    cf_destroy_canvas(target);
    return read;
}

float test_half(uint16_t value)
{
    int exponent = (value >> 10) & 31;
    int mantissa = value & 1023;
    float result = exponent == 31 ? (mantissa ? NAN : INFINITY)
                 : exponent ? ldexpf(1 + mantissa / 1024.0f, exponent - 15)
                            : ldexpf((float)mantissa, -24);
    return value & 0x8000 ? -result : result;
}
