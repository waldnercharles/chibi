#ifndef CHIBI_RUNTIME_BEHAVIORS_SCENE_FLOW_H
#define CHIBI_RUNTIME_BEHAVIORS_SCENE_FLOW_H
#include "execution/host.h"

/* Which scene runs and which one scripts asked for next. Scripts only request
 * a scene; the host loads it between frames, so a frame never sees a world
 * half torn down. The host lists the scenes it can load; with none listed,
 * Load Scene and Reload Scene fail. */
typedef struct scene_flow scene_flow;
scene_flow *scene_flow_new();
void scene_flow_free(scene_flow *flow);
/* Copies the names of the scenes the host can load. */
void scene_flow_set(scene_flow *flow, const char *const *names, int count);
/* Names the scene the host just started. */
void scene_flow_enter(scene_flow *flow, const char *name);
/* The running scene's name, or "" before a host names one. */
const char *scene_flow_current(const scene_flow *flow);
/* The owned name of the scene to load next, clearing the request; NULL when
 * scripts asked for none. A reload asks for the running scene. */
char *scene_flow_take(scene_flow *flow);
extern const node_host_call SCENE_LOAD_CALL;
extern const node_host_call SCENE_RELOAD_CALL;
extern const node_host_call SCENE_CURRENT_CALL;
extern const node_access SCENE_REQUEST_ACCESS;
extern const node_access SCENE_READ_ACCESS;
#endif
