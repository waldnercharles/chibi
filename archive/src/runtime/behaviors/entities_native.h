#ifndef CHIBI_ENTITIES_NATIVE_H
#define CHIBI_ENTITIES_NATIVE_H

#include "execution/host.h"
#include "execution/request.h"

typedef struct entities entities;
typedef struct scene scene;

/* Static native contracts. The borrowed game.entities service owns world state;
 * callbacks snapshot or queue inputs according to the execution host contract. */
extern const node_command ENTITY_SPAWN_COMMAND;
extern const node_command ENTITY_DESTROY_COMMAND;
extern const node_command ENTITY_SEND_COMMAND;
extern const node_command ENTITY_ADD_PAIR_COMMAND;
extern const node_command ENTITY_REMOVE_PAIR_COMMAND;
extern const node_host_call ENTITY_HAS_PAIR_CALL;
extern const node_host_call ENTITY_NEAREST_CALL;
extern const node_host_call ENTITY_GET_GLOBAL_CALL;
extern const node_host_call ENTITY_SET_GLOBAL_CALL;
extern const node_host_call ENTITY_HAS_COMPONENT_CALL;
extern const node_access ENTITY_QUERY_ACCESS;
extern const node_access ENTITY_NEAREST_ACCESS;
extern const node_access ENTITY_STRUCTURAL_ACCESS;
extern const node_access ENTITY_GLOBAL_READ_ACCESS;
extern const node_access ENTITY_GLOBAL_WRITE_ACCESS;
const char *entities_find_snapshot(void *service, const char *key, struct pin_array *out);

/* Preparation retains the selected immutable prototype generation. Submit
 * transfers that reference into the queued request; the caller owns it until
 * submission and must release it if no continuation can be created. */
scene *entities_prepare_spawn(entities *, const char *identity, const char **message);
void entities_submit_spawn(entities *, scene *, flow_host_request *);

#endif
