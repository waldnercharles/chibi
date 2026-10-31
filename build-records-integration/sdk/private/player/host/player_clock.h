#ifndef CHIBI_HOST_PLAYER_CLOCK_H
#define CHIBI_HOST_PLAYER_CLOCK_H

#include <cute_time.h>

static inline void player_clock_start()
{
    /* requestAnimationFrame already paces the browser's main loop. */
#ifndef __EMSCRIPTEN__
    cf_set_target_framerate(60);
#endif
}

#endif
