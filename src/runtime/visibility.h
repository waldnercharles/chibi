#ifndef CHIBI_GAME_VISIBILITY_H
#define CHIBI_GAME_VISIBILITY_H

#include "execution/host.h"
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

const char *game_visibility_sample_values(void *, const pin_value *, pin_value *);
extern const node_host_call GAME_VISIBILITY_SAMPLE_CALL;
extern const node_access GAME_VISIBILITY_READ_ACCESS;

#endif
