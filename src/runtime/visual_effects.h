#ifndef CHIBI_GAME_VISUAL_EFFECTS_H
#define CHIBI_GAME_VISUAL_EFFECTS_H

#include "ecs/ecs.h"
#include "graph/model/registry.h"

#include <cute_math.h>
#include <stdbool.h>

/* Transient screen juice that behaviors trigger: a full-screen flash, pulses
 * of chromatic aberration and expanding shockwave rings. The renderer reads
 * the current state; the engine ages it on the fixed clock. */
typedef struct game_visual_effects game_visual_effects;

enum
{
    GAME_SHOCKWAVE_MAX = 16
};

/* A ring that expands from Position to Radius world units over Duration
 * seconds, displacing the frame by up to Strength output pixels across a band
 * Thickness world units wide. */
typedef struct
{
    CF_V2 position;
    float radius, thickness, strength, duration, age;
} game_shockwave;

typedef struct
{
    /* Display RGB and the flash's current weight. */
    float flash[4];
    /* Current extra chromatic aberration, output pixels. */
    float aberration;
    const game_shockwave *shockwaves;
    int shockwave_count;
} game_visual_effects_state;

game_visual_effects *game_visual_effects_new();
void game_visual_effects_free(game_visual_effects *effects);
void game_visual_effects_reset(game_visual_effects *effects);
void game_visual_effects_tick(game_visual_effects *effects, float delta);
game_visual_effects_state game_visual_effects_current(const game_visual_effects *effects);
/* False for nonfinite or negative settings. */
bool game_visual_effects_flash(game_visual_effects *effects, unsigned color, float amount, float duration);
bool game_visual_effects_pulse_aberration(game_visual_effects *effects, float amount, float duration);
/* Replaces the oldest ring when all are in use. */
bool game_visual_effects_add_shockwave(game_visual_effects *effects, game_shockwave shockwave);
/* Lowers every GameFlash amount by its decay over delta seconds. */
void game_visual_effects_fade_flashes(ecs_world *world, float delta);

extern const registry_pack GAME_VISUAL_EFFECTS_PACK;

#endif
