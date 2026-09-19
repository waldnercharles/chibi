#ifndef CHIBI_APP_SCENE_AUTHORING_H
#define CHIBI_APP_SCENE_AUTHORING_H
#include "assets/system_document.h"
#include "render/sdf_shapes.h"
#include "runtime/behaviors/live.h"
#include "runtime/engine.h"
#include "runtime/scenes/physics_settings.h"

typedef struct app_scene_authoring app_scene_authoring;
/* Authored world settings; editable while Scene and prototype preview are stopped.
 * Records one undo step. Invalid settings leave the world unchanged. */
int app_scene_authoring_set_physics(app_scene_authoring *scene, game_physics_settings settings);
typedef struct editor editor;
typedef const char *(*app_scene_prototype_resolve_fn)(const char *name, void *user);

typedef enum
{
    APP_RUNTIME_PREVIEW,
    APP_RUNTIME_SCENE,
} app_runtime_target;

typedef enum
{
    APP_RUNTIME_PLAY,
    APP_RUNTIME_PAUSE,
    APP_RUNTIME_STEP,
    APP_RUNTIME_RESTART,
    APP_RUNTIME_STOP,
} app_runtime_command;

typedef struct
{
    app_runtime_target target;
    bool playing;
    ecs_entity selected_entity;
    int component_count;
    int behavior_count;
    const char *message;
} app_runtime_state;

typedef enum
{
    APP_DEBUG_NONE,
    APP_DEBUG_AVAILABLE,
    APP_DEBUG_UNAVAILABLE,
    APP_DEBUG_OTHER_GRAPH,
    APP_DEBUG_STALE,
} app_debug_status;

typedef struct
{
    app_runtime_target target;
    ecs_entity entity;
    const char *asset_name;
    bool pinned;
    app_debug_status status;
} app_debug_state;

int app_scene_authoring_set_debug_target(
    app_scene_authoring *scene,
    app_runtime_target target,
    ecs_entity entity,
    const char *asset_name
);
void app_scene_authoring_set_debug_pinned(app_scene_authoring *scene, bool pinned);
app_debug_state app_scene_authoring_debug_state(app_scene_authoring *scene);

typedef enum
{
    APP_EDITOR_DOCUMENT_SCENE,
    APP_EDITOR_DOCUMENT_PROTOTYPE,
    APP_EDITOR_DOCUMENT_BEHAVIOR,
    APP_EDITOR_DOCUMENT_SYSTEM,
} app_editor_document_kind;

typedef struct
{
    app_editor_document_kind kind;
    const char *id; /* borrowed stable asset or scene identity */
} app_editor_document_context;

app_scene_authoring *app_scene_authoring_new(
    ecs_world *world,
    game_live_world *live,
    game_live_definition *definition,
    const behavior_asset *editor_working,
    editor *graph_editor,
    app_scene_prototype_resolve_fn resolve_prototype,
    void *resolve_user,
    engine *runtime
);
app_scene_authoring *app_scene_authoring_new_with_content_root(
    ecs_world *world,
    game_live_world *live,
    game_live_definition *definition,
    const behavior_asset *editor_working,
    editor *graph_editor,
    app_scene_prototype_resolve_fn resolve_prototype,
    void *resolve_user,
    engine *runtime,
    const char *content_root
);
void app_scene_authoring_set_editor_working(app_scene_authoring *scene, const behavior_asset *editor_working);
int app_scene_authoring_apply_pending(app_scene_authoring *scene);
int app_scene_authoring_set_content_root(app_scene_authoring *scene, const char *path);
void app_scene_authoring_refresh_content(app_scene_authoring *scene);
int app_scene_authoring_load_content(app_scene_authoring *scene);
int app_scene_authoring_register_prototype(
    app_scene_authoring *scene,
    const char *json,
    const char *path
);
int app_scene_authoring_load_prototype(app_scene_authoring *scene, const char *path);
int app_scene_authoring_open_prototype(app_scene_authoring *scene, const char *name);
int app_scene_authoring_save_prototype(
    app_scene_authoring *scene,
    const char *prototype_name,
    const char *path
);
/* Field storage is borrowed from the authored prototype until its next edit. */
int app_scene_authoring_read_prototype_field(
    app_scene_authoring *scene,
    const char *prototype_name,
    const char *component,
    const char *field,
    pin_value *value
);
int app_scene_authoring_set_prototype_field(
    app_scene_authoring *scene,
    const char *prototype_name,
    const char *component_name,
    const char *field_name,
    pin_value value
);
int app_scene_authoring_add_prototype_component(
    app_scene_authoring *scene,
    const char *prototype_name,
    const char *component_name
);
int app_scene_authoring_remove_prototype_component(
    app_scene_authoring *scene,
    const char *prototype_name,
    const char *component_name
);
void app_scene_authoring_free(app_scene_authoring *scene);
int app_scene_authoring_seed_default(app_scene_authoring *scene);
int app_scene_authoring_open_behavior(app_scene_authoring *scene, const char *asset_name);
int app_scene_authoring_open_system(app_scene_authoring *scene, const char *path);
int app_scene_authoring_attach_prototype_behavior(
    app_scene_authoring *scene,
    const char *prototype_name,
    const char *asset_name
);
int app_scene_authoring_detach_prototype_behavior(
    app_scene_authoring *scene,
    const char *prototype_name,
    const char *asset_name
);
int app_scene_authoring_set_runtime_target(app_scene_authoring *scene, app_runtime_target target);
/* Read-only authored pose shown in the Inspector; never runs behaviors. */
const ecs_world *app_scene_authoring_still_preview(const app_scene_authoring *scene, ecs_entity *entity);
const ecs_world *app_scene_authoring_preview_world(const app_scene_authoring *scene);
int app_scene_authoring_preview_prototype(app_scene_authoring *scene, const char *prototype_name);
void app_scene_authoring_stop_prototype_preview(app_scene_authoring *scene);
int app_scene_authoring_runtime_command(
    app_scene_authoring *scene,
    app_runtime_target target,
    app_runtime_command command
);
app_runtime_state app_scene_authoring_runtime_state(const app_scene_authoring *scene);
const engine_frame_timing *app_scene_authoring_runtime_timing(const app_scene_authoring *scene);
void app_scene_authoring_set_behavior_profiling(app_scene_authoring *scene, bool enabled);
/* Draw live renderer controls inside the Performance pane. */
void app_scene_authoring_draw_render_settings(app_scene_authoring *scene);
const game_behavior_profile_frame *app_scene_authoring_behavior_profile(
    const app_scene_authoring *scene
);
int app_scene_authoring_save(app_scene_authoring *scene, const char *path);
int app_scene_authoring_save_as(app_scene_authoring *scene);
int app_scene_authoring_load(app_scene_authoring *scene, const char *path);
int app_scene_authoring_reload(app_scene_authoring *scene);
const char *app_scene_authoring_message(const app_scene_authoring *scene);
/* Select a live host-spawned entity without adding it to the authored scene. */
int app_scene_authoring_select_runtime_entity(app_scene_authoring *scene, ecs_entity entity);
const char *app_scene_authoring_selected_content(const app_scene_authoring *scene);
app_editor_document_context app_scene_authoring_document(const app_scene_authoring *scene);
void app_scene_authoring_draw(app_scene_authoring *scene);
int app_scene_authoring_draw_viewport(app_scene_authoring *scene, engine_viewport *viewport);
void app_scene_authoring_render(void *user);
app_sdf_shapes app_scene_authoring_shapes(const app_scene_authoring *scene);
int app_scene_authoring_frame(app_scene_authoring *scene, double monotonic_time);
int app_scene_authoring_move(app_scene_authoring *scene, float x, float y);
/* Sample aiming independently of movement; points use game coordinates. */
int app_scene_authoring_aim(app_scene_authoring *scene, CF_V2 direction, bool firing);
int app_scene_authoring_aim_at(app_scene_authoring *scene, CF_V2 point, bool firing);
int app_scene_authoring_input(app_scene_authoring *scene, const char *action, double value, int pressed);

/* Authored lifecycle actions, recorded in Scene Undo. */
int app_scene_authoring_add_instance(app_scene_authoring *scene, const char *prototype_name);
int app_scene_authoring_set_instance_component(app_scene_authoring *scene, const char *name, bool present);
int app_scene_authoring_selection_count(const app_scene_authoring *scene);
int app_scene_authoring_delete_instance(app_scene_authoring *scene);
int app_scene_authoring_set_instance_behavior(
    app_scene_authoring *scene,
    const char *asset_name,
    bool attached
);

/* Capture explicit programmatic visits; native frames capture automatically. */
void app_scene_authoring_navigation_sync(app_scene_authoring *scene);
bool app_scene_authoring_can_navigate(const app_scene_authoring *scene, bool forward);
int app_scene_authoring_navigate(app_scene_authoring *scene, bool forward);

/* Owned, versioned workspace JSON; restore only into a disposable candidate. */
char *app_scene_authoring_checkpoint(app_scene_authoring *scene);
int app_scene_authoring_restore_checkpoint(app_scene_authoring *scene, const char *json);

#endif
