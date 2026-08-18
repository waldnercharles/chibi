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
    CF_APP_OPTIONS_WINDOW_POS_CENTERED_BIT = 1 << 4,
    CF_APP_OPTIONS_NO_AUDIO_BIT = 1 << 6,
};

extern double CF_SECONDS;
typedef void CF_OnUpdateFn(void *user);

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
{ return result.code == CF_RESULT_ERROR; }
void *cf_app_init_imgui(void);
bool cf_app_is_running(void);
void cf_app_update(CF_OnUpdateFn *on_update);
int cf_app_draw_onto_screen(bool clear);

#endif
