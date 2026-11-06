#ifndef CHIBI_PHYSICS_NATIVE_H
#define CHIBI_PHYSICS_NATIVE_H
#include "ecs/ecs.h"
#include "execution/host.h"
struct entities;
/* Graph presentation and canonical source execution share these actual query
 * kernels. Dynamic results belong to the supplied invocation arena. */
const char *physics_native_raycast(ecs_world *, struct entities *, const pin_value *, arena *, pin_value *);
const char *physics_native_contacts(ecs_world *, struct entities *, const pin_value *, arena *, pin_value *);
extern const node_host_call PHYSICS_RAYCAST_CALL, PHYSICS_CONTACTS_CALL,
    PHYSICS_IMPULSE_CALL, PHYSICS_FORCE_CALL;
extern const node_access PHYSICS_READ_ACCESS, PHYSICS_PUSH_ACCESS;
#endif
