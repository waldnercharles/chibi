#ifndef CHIBI_GAME_VISIBILITY_H
#define CHIBI_GAME_VISIBILITY_H

#include "runtime/behaviors/entities.h"
#include "runtime/components.h"

typedef struct game_visibility game_visibility;
typedef struct
{
    bool visible, explored, obstacle;
} game_visibility_sample;

game_visibility *game_visibility_new(ecs_world *world, game_entities *entities);
void game_visibility_free(game_visibility *visibility);
void game_visibility_reset(game_visibility *visibility);
const char *game_visibility_update(game_visibility *visibility, float seconds);
game_visibility_sample game_visibility_at(const game_visibility *visibility, CF_V2 point);
/* Borrowed row-major cells: R=current sight, G=explored, B=remembered terrain,
 * A=255. No actor positions are stored in map memory. NULL means fog is off. */
const unsigned char *game_visibility_cells(const game_visibility *visibility, GameFog *settings);
extern const registry_pack GAME_VISIBILITY_PACK;

#endif
