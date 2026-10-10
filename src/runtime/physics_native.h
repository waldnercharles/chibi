#ifndef CHIBI_PHYSICS_NATIVE_H
#define CHIBI_PHYSICS_NATIVE_H
#include "ecs/ecs.h"
#include "execution/host.h"
struct game_entities;
/* Graph presentation and canonical source execution share these actual query
 * kernels. Dynamic results belong to the supplied invocation arena. */
const char *game_physics_native_raycast(
    ecs_world *,
    struct game_entities *,
    const pin_value *,
    arena *,
    pin_value *
);
const char *game_physics_native_contacts(
    ecs_world *,
    struct game_entities *,
    const pin_value *,
    arena *,
    pin_value *
);
extern const node_host_call GAME_PHYSICS_RAYCAST_CALL, GAME_PHYSICS_CONTACTS_CALL,
    GAME_PHYSICS_IMPULSE_CALL, GAME_PHYSICS_FORCE_CALL;
extern const node_access GAME_PHYSICS_READ_ACCESS, GAME_PHYSICS_PUSH_ACCESS;
#endif
