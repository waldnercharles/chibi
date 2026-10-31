#ifndef CHIBI_RENDER_DRAW_TEXTURES_H
#define CHIBI_RENDER_DRAW_TEXTURES_H

#include "platform/cute.h"

/* Cute's shared draw material keeps each cf_draw_set_texture binding until
 * the name is rebound, and its WebGL backend reads every bound texture on
 * every draw. Queue this after the draw or flush that samples textures their
 * owner may destroy. Render passes draw on layer 0, so the trailing unbind
 * runs after the draws queued before it. */
static inline void render_draw_unbind_textures(const char *const *names, int count)
{
    for (int i = 0; i < count; i++) {
        cf_draw_set_texture(names[i], (CF_Texture){ 0 });
    }
}

#define RENDER_DRAW_UNBIND_TEXTURES(...)                                       \
    render_draw_unbind_textures(                                               \
        (const char *const[]){ __VA_ARGS__ },                                  \
        (int)(sizeof((const char *const[]){ __VA_ARGS__ }) / sizeof(char *))   \
    )

#endif
