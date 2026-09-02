#ifndef CBLUEPRINT_APP_SCENE_AUTHORING_H
#define CBLUEPRINT_APP_SCENE_AUTHORING_H

#include "../engine/engine.h"
#include "../game/live.h"
#include "../game/system_document.h"

typedef struct app_scene_authoring app_scene_authoring;
typedef struct editor editor;
typedef const char *(*app_scene_prototype_resolve_fn)(const char *name, void *user);

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
int app_scene_authoring_save(app_scene_authoring *scene, const char *path);
int app_scene_authoring_save_as(app_scene_authoring *scene);
int app_scene_authoring_load(app_scene_authoring *scene, const char *path);
int app_scene_authoring_reload(app_scene_authoring *scene);
const char *app_scene_authoring_message(const app_scene_authoring *scene);
const char *app_scene_authoring_selected_content(const app_scene_authoring *scene);
app_editor_document_context app_scene_authoring_document(const app_scene_authoring *scene);
void app_scene_authoring_draw(app_scene_authoring *scene);
void app_scene_authoring_render(void *user);
int app_scene_authoring_frame(app_scene_authoring *scene, double monotonic_time);
int app_scene_authoring_input(app_scene_authoring *scene, const char *action, double value, int pressed);

#endif
