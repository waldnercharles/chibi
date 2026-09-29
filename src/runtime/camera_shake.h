#ifndef CHIBI_GAME_CAMERA_SHAKE_H
#define CHIBI_GAME_CAMERA_SHAKE_H

#include "ecs/ecs.h"
#include "graph/model/registry.h"
#include "runtime/components.h"

/* A GameCameraShake's view offset at real time `seconds`. */
CF_V2 game_camera_shake_offset(const GameCameraShake *shake, double seconds);
/* Decays GameCameraShake trauma over real seconds. */
void game_camera_shake_tick(ecs_world *world, float real_seconds);
extern const registry_pack GAME_CAMERA_PACK;

#endif
