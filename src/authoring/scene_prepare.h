#ifndef CHIBI_AUTHORING_SCENE_PREPARE_H
#define CHIBI_AUTHORING_SCENE_PREPARE_H

#include "assets/prototype.h"
#include "assets/scene_document.h"
#include "runtime/scenes/scene.h"

/* Objects are in document order. Each prototype already contains the resolved
 * inherited components, scene overrides and complete behavior attachment list.
 * The returned runtime scene owns snapshots and keeps no document/prototype. */
game_scene *authoring_scene_prepare(
    const game_scene_document *,
    const game_prototype *const *objects,
    int count,
    char *error,
    size_t size
);

#endif
