#ifndef CHIBI_RENDER_WIDGETS_H
#define CHIBI_RENDER_WIDGETS_H

#include "runtime/visibility.h"
#include "runtime/widgets.h"

/* Called inside the host's viewport window, after drawing its image. */
bool render_widgets(
    ecs_world *world,
    game_entities *entities,
    const game_visibility *visibility,
    CF_V2 origin,
    CF_V2 size,
    bool enabled
);

#endif
