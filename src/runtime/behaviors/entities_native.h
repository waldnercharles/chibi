#ifndef CHIBI_GAME_ENTITIES_NATIVE_H
#define CHIBI_GAME_ENTITIES_NATIVE_H

#include "execution/host.h"
#include "execution/request.h"

typedef struct game_entities game_entities;
typedef struct game_scene game_scene;

/* Static native contracts. The borrowed game.entities service owns world state;
 * callbacks snapshot or queue inputs according to the execution host contract. */
extern const node_command GAME_ENTITY_SPAWN_COMMAND;
extern const node_command GAME_ENTITY_DESTROY_COMMAND;
extern const node_command GAME_ENTITY_SEND_COMMAND;
extern const node_host_call GAME_ENTITY_NEAREST_CALL;
extern const node_host_call GAME_ENTITY_GET_GLOBAL_CALL;
extern const node_host_call GAME_ENTITY_SET_GLOBAL_CALL;
extern const node_host_call GAME_ENTITY_HAS_COMPONENT_CALL;
extern const node_access GAME_ENTITY_QUERY_ACCESS;
extern const node_access GAME_ENTITY_NEAREST_ACCESS;
extern const node_access GAME_ENTITY_STRUCTURAL_ACCESS;
extern const node_access GAME_ENTITY_GLOBAL_READ_ACCESS;
extern const node_access GAME_ENTITY_GLOBAL_WRITE_ACCESS;
const char *game_entities_find_snapshot(void *service, const char *key, struct pin_array *out);

/* Preparation retains the selected immutable prototype generation. Submit
 * transfers that reference into the queued request; the caller owns it until
 * submission and must release it if no continuation can be created. */
game_scene *game_entities_prepare_spawn(game_entities *, const char *identity, const char **message);
void game_entities_submit_spawn(game_entities *, game_scene *, flow_host_request *);

#endif
