#ifndef CHIBI_RUNTIME_BEHAVIORS_SCENE_FLOW_H
#define CHIBI_RUNTIME_BEHAVIORS_SCENE_FLOW_H
#include "execution/host.h"

/* Which scene runs and which one scripts asked for next. Scripts only request
 * a scene; the host loads it between frames, so a frame never sees a world
 * half torn down. The host lists the scenes it can load; with none listed,
 * Load Scene and Reload Scene fail. */
typedef struct game_scene_flow game_scene_flow;
game_scene_flow *game_scene_flow_new();
void game_scene_flow_free(game_scene_flow *flow);
/* Copies the names of the scenes the host can load. */
void game_scene_flow_set(game_scene_flow *flow, const char *const *names, int count);
/* Names the scene the host just started. */
void game_scene_flow_enter(game_scene_flow *flow, const char *name);
/* The running scene's name, or "" before a host names one. */
const char *game_scene_flow_current(const game_scene_flow *flow);
/* The owned name of the scene to load next, clearing the request; NULL when
 * scripts asked for none. A reload asks for the running scene. */
char *game_scene_flow_take(game_scene_flow *flow);
extern const node_host_call GAME_SCENE_LOAD_CALL;
extern const node_host_call GAME_SCENE_RELOAD_CALL;
extern const node_host_call GAME_SCENE_CURRENT_CALL;
extern const node_access GAME_SCENE_REQUEST_ACCESS;
extern const node_access GAME_SCENE_READ_ACCESS;
#endif
