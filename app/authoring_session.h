#ifndef CBLUEPRINT_APP_AUTHORING_SESSION_H
#define CBLUEPRINT_APP_AUTHORING_SESSION_H

#include "../core/compile.h"
#include "../core/graph.h"

#include <stdint.h>

typedef struct app_authoring_session app_authoring_session;

/* A headless authoring session is the deterministic command/query seam shared
 * by integration tests and future computer-use adapters. It owns its ECS/live
 * world and loads the same content/behaviors fixtures as the app. */
app_authoring_session *app_authoring_session_new(const char *content_root);
void app_authoring_session_free(app_authoring_session *session);
int app_authoring_session_load_fixtures(app_authoring_session *session);
int app_authoring_session_asset_count(const app_authoring_session *session);
const char *app_authoring_session_asset_name(const app_authoring_session *session, int index);
int app_authoring_session_open_behavior(app_authoring_session *session, const char *name);
int app_authoring_session_create_behavior(app_authoring_session *session, const char *name);
const behavior_asset *app_authoring_session_behavior(const app_authoring_session *session);
const graph *app_authoring_session_active_graph(const app_authoring_session *session);
int app_authoring_session_select_graph(app_authoring_session *session, uint64_t graph_id);
graph *app_authoring_session_add_graph(app_authoring_session *session, graph_kind kind, const char *name);
uint64_t app_authoring_session_add_node(app_authoring_session *session, const char *name);
int app_authoring_session_set_pin_default(
    app_authoring_session *session,
    uint64_t node_id,
    int output,
    int index,
    const pin_value *value
);
int app_authoring_session_connect_data(
    app_authoring_session *session,
    uint64_t from_node,
    int from_pin,
    uint64_t to_node,
    int to_pin
);
int app_authoring_session_connect_exec(
    app_authoring_session *session,
    uint64_t from_node,
    int from_pin,
    uint64_t to_node,
    int to_pin,
    int exec_order
);
int app_authoring_session_compile(app_authoring_session *session);
int app_authoring_session_rename_graph(
    app_authoring_session *session,
    uint64_t graph_id,
    const char *name
);
int app_authoring_session_save(app_authoring_session *session, const char *path);
int app_authoring_session_reload(app_authoring_session *session, const char *path);
int app_authoring_session_dirty(const app_authoring_session *session);
const compile_report *app_authoring_session_diagnostics(const app_authoring_session *session);

#endif
