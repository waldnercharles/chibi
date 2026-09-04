#ifndef CBLUEPRINT_CUTE_HOST_H
#define CBLUEPRINT_CUTE_HOST_H

#include <stdbool.h>
#include <stdint.h>

typedef uint32_t CF_DisplayID;
typedef int CF_AppOptionFlags;
typedef struct CF_Result
{
    int code;
    const char *details;
} CF_Result;

enum
{
    CF_RESULT_ERROR = -1,
    CF_APP_OPTIONS_RESIZABLE_BIT = 1 << 2,
    CF_APP_OPTIONS_HIDDEN_BIT = 1 << 3,
    CF_APP_OPTIONS_WINDOW_POS_CENTERED_BIT = 1 << 4,
    CF_APP_OPTIONS_NO_AUDIO_BIT = 1 << 6,
};

extern float CF_DELTA_TIME;
typedef void CF_OnUpdateFn(void *user);
typedef struct CF_Canvas
{
    uint64_t id;
} CF_Canvas;
typedef struct CF_VideoEncoder CF_VideoEncoder;
typedef struct CF_Readback
{
    uint64_t id;
} CF_Readback;
typedef union CF_Pixel
{
    struct
    {
        uint8_t r;
        uint8_t g;
        uint8_t b;
        uint8_t a;
    } colors;
    uint32_t val;
} CF_Pixel;
typedef struct CF_Image
{
    int w;
    int h;
    CF_Pixel *pix;
} CF_Image;

typedef int CF_KeyButton;
typedef int CF_MouseButton;
enum
{
    CF_KEY_SPACE = ' ',
    CF_KEY_ESCAPE = '\033',
    CF_KEY_A = 'a',
    CF_KEY_D = 'd',
    CF_KEY_S = 's',
    CF_KEY_W = 'w',
    CF_KEY_RIGHT = 145,
    CF_KEY_LEFT = 146,
    CF_KEY_DOWN = 147,
    CF_KEY_UP = 148,
    CF_MOUSE_BUTTON_LEFT = 0,
};
bool cf_key_down(CF_KeyButton key);
bool cf_key_just_pressed(CF_KeyButton key);
bool cf_key_just_released(CF_KeyButton key);
float cf_mouse_x(void);
float cf_mouse_y(void);
bool cf_mouse_just_pressed(CF_MouseButton button);
bool cf_mouse_down(CF_MouseButton button);

CF_Result cf_make_app(
    const char *window_title,
    CF_DisplayID display_id,
    int x,
    int y,
    int w,
    int h,
    CF_AppOptionFlags options,
    const char *argv0
);
void cf_destroy_app(void);
static inline bool cf_is_error(CF_Result result)
{
    return result.code == CF_RESULT_ERROR;
}
void *cf_app_init_imgui(void);
bool cf_app_is_running(void);
void cf_app_update(CF_OnUpdateFn *on_update);
void cf_set_target_framerate(int frames_per_second);
CF_Canvas cf_app_get_canvas(void);
int cf_app_get_canvas_width(void);
int cf_app_get_canvas_height(void);
int cf_app_draw_onto_screen(bool clear);
CF_Readback cf_canvas_readback(CF_Canvas canvas);
bool cf_readback_ready(CF_Readback readback);
int cf_readback_size(CF_Readback readback);
int cf_readback_data(CF_Readback readback, void *data, int size);
void cf_destroy_readback(CF_Readback readback);
CF_Result cf_image_save_png_to_memory(CF_Image *image, void **data, int *size);
void cf_free(void *ptr);
const char *cf_fs_get_working_directory(void);
CF_Result cf_fs_set_write_directory(const char *platform_dependent_directory);
CF_VideoEncoder *cf_make_video_encoder(int w, int h, int fps);
void cf_destroy_video_encoder(CF_VideoEncoder *encoder);
int cf_video_encoder_update(CF_VideoEncoder *encoder, CF_Canvas canvas, float dt);
CF_Result cf_video_encoder_save(CF_VideoEncoder *encoder, const char *virtual_path);

typedef void cf_assert_fn(bool expr, const char *message, const char *file, int line);
void cf_set_assert_handler(cf_assert_fn *assert_fn);

#endif
