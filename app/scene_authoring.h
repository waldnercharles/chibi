#ifndef CBLUEPRINT_APP_SCENE_AUTHORING_H
#define CBLUEPRINT_APP_SCENE_AUTHORING_H

#include "../engine/engine.h"
#include "../game/live.h"
#include "../game/system_document.h"
#include "sdf_shapes.h"

typedef struct app_scene_authoring app_scene_authoring;
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
int app_scene_authoring_save_prototype(
    app_scene_authoring *scene,
    const char *prototype_name,
    const char *path
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
int app_scene_authoring_preview_prototype(app_scene_authoring *scene, const char *prototype_name);
void app_scene_authoring_stop_prototype_preview(app_scene_authoring *scene);
int app_scene_authoring_runtime_command(
    app_scene_authoring *scene,
    app_runtime_target target,
    app_runtime_command command
);
app_runtime_state app_scene_authoring_runtime_state(const app_scene_authoring *scene);
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

#endif
