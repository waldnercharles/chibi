#ifndef CHIBI_GAME_SCENE_H
#define CHIBI_GAME_SCENE_H

#include "runtime/behaviors/behavior.h"

#include <stdint.h>

typedef struct game_scene game_scene;

game_scene *game_scene_new(int entity_count, int behavior_count, uint64_t seed);
void game_scene_free(game_scene *scene);
int game_scene_frame(game_scene *scene, double monotonic_time);
void game_scene_set_parallel_for(game_scene *scene, flow_parallel_for_fn parallel_for, void *parallel_user);
uint64_t game_scene_hash(const game_scene *scene);
double game_scene_behavior_ms(const game_scene *scene);
int game_scene_entity_count(const game_scene *scene);
int game_scene_behavior_count(const game_scene *scene);

#endif
