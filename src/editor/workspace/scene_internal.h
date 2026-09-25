#ifndef CHIBI_SCENE_INTERNAL_H
#define CHIBI_SCENE_INTERNAL_H
#include "editor/documents/scene_history.h"
#include "editor/documents/scene_retention.h"
#include "editor/workspace/asset_index.h"
#include "editor/workspace/editor.h"
#include "editor/workspace/scene_authoring.h"
#include "editor/workspace/scene_debug.h"
#include "render/scene_renderer.h"
#include "runtime/scenes/prototype_preview.h"

typedef struct
{
    ecs_entity entity;
    char *name;
    char *prototype_name;
    game_prototype *base;
    game_prototype *runtime_snapshot;
    game_prototype *restored_state;
    scene_retained_component *retained;
    bool marked;
} scene_actor;

typedef struct
{
    engine_viewport_view view;
    bool saved;
} scene_camera;

typedef struct
{
    char *path;
    game_prototype *prototype;
    int dirty;
    scene_camera camera;
    char **undo;
    char **redo;
} scene_prototype_asset;

typedef struct
{
    char *path;
    bool directory;
    bool image;
} scene_content_entry;

typedef struct
{
    app_editor_document_kind kind;
    char *key;
    char *label;
    char *path;
    char *scene_json;
    char *scene_history;
    uint64_t *scene_entities;
    int scene_entity_count;
    uint64_t selected_entity;
    engine_viewport_view camera;
    bool camera_saved;
    bool scene_dirty;
    int selected;
    int selected_prototype;
    char *selected_prototype_name;
    char *asset_saved;
    char *system_json;
    char *system_saved;
    unsigned system_editor_revision;
    unsigned system_query_revision;
} scene_document_tab;

/* Private authoring state. Module replacement transfers versioned document
 * checkpoints, never this layout or its runtime pointers. */
struct app_scene_authoring
{
    app_sdf_shapes shapes;
    app_scene_renderer *renderer;
    engine_viewport *render_viewport;
    bool perspective;
    ecs_world *world;
    game_live_world *live;
    game_live_definition *definition;
    const behavior_asset *editor_working;
    editor *graph_editor;
    unsigned editor_revision_seen;
    const behavior_asset *editor_behavior_asset_seen;
    game_live_definition *editor_definition_seen;
    bool editor_state_seen;
    bool behavior_apply_pending;
    bool preview_retry;
    char preview_message[256];
    app_scene_prototype_resolve_fn resolve_prototype;
    void *resolve_user;
    game_prototype_registry registry;
    editor_choices component_choices;
    editor_choices type_choices;
    editor_choices image_choices; /* Labels and names borrow content paths. */
    editor_choices prototype_choices; /* Names are owned across prototype Undo/reload. */
    editor_choices global_choices; /* Names and labels are owned by this scene. */
    game_system_document *system_document;
    char *system_path;
    engine *runtime;
    game_prototype_preview *prototype_preview;
    char *preview_prototype_id;
    int runtime_preview_prototype;
    scene_actor *actors;
    scene_prototype_asset *prototypes;
    scene_content_entry *content;
    asset_index *asset_index;
    unsigned asset_index_generation;
    unsigned asset_index_built_generation;
    char *content_root;
    int selected;
    ecs_entity reveal_actor;
    ecs_entity runtime_selected;
    int selected_prototype;
    scene_debug debug;
    app_runtime_target debug_target;
    uint64_t debug_sessions[2];
    app_debug_status debug_status;
    bool runtime_snapshot_valid;
    bool runtime_playing;
    bool runtime_step;
    bool preview_playing;
    bool preview_step;
    bool preview_loop;
    bool preview_follow;
    GameTransform preview_origin;
    app_runtime_target active_runtime_target;
    scene_camera camera;
    int camera_target;
    scene_history *history;
    ecs_entity edit_actor;
    ecs_entity property_actor;
    scene_field_edit property_edit;
    bool property_dirty;
    GameTransform edit_before;
    GameTransform drag_origin;
    bool edit_dirty;
    bool dragging_actor;
    bool marquee;
    bool marquee_visible;
    CF_V2 marquee_start; /* Normalized viewport coordinates. */
    CF_V2 marquee_end;
    float selection_scale;
    unsigned asset_index_editor_revision;
    unsigned asset_index_system_revision;
    unsigned system_runtime_editor_revision;
    app_editor_document_context document;
    char *document_id;
    char *graph_document_id;
    app_editor_document_kind graph_document_kind;
    char *scene_path;
    bool scene_dirty;
    scene_document_tab *tabs;
    int active_tab;
    char *current_scene_tab_key;
    int next_scene_key;
    bool tabs_transitioning;
    bool scene_tab_restoring;
    char message[256];
    char prototype_message[256];
};

/* Document I/O is shared by disk commands and module checkpoints. */
char *scene_authoring_scene_json(app_scene_authoring *scene);
int scene_authoring_load_json(app_scene_authoring *scene, const char *json, const char *path);
int scene_authoring_sync_document(app_scene_authoring *scene);
void scene_authoring_select_document(
    app_scene_authoring *scene,
    app_editor_document_kind kind,
    const char *id
);
void scene_tabs_initialize(app_scene_authoring *scene);
void scene_tabs_free(app_scene_authoring *scene);
void scene_tabs_sync_current(app_scene_authoring *scene);
void scene_tabs_mark_document_saved(
    app_scene_authoring *scene,
    app_editor_document_kind kind,
    const char *id
);
void scene_tabs_document_selected(
    app_scene_authoring *scene,
    app_editor_document_kind kind,
    const char *id
);
int scene_tabs_open_scene(app_scene_authoring *scene, const char *path);
bool scene_tabs_path_in_use(
    const app_scene_authoring *scene,
    app_editor_document_kind kind,
    const char *path
);
int scene_tabs_activate(app_scene_authoring *scene, int index);
int scene_tabs_discard(app_scene_authoring *scene, int index);
bool scene_tabs_dirty(app_scene_authoring *scene, const scene_document_tab *tab);
const char *scene_tabs_current_scene_key(const app_scene_authoring *scene);
int load_system_document_json(app_scene_authoring *scene, const char *json, const char *path, bool reload);
/* Scene model operations shared by the authoring modules and Studio. */

void begin_transform_edit(app_scene_authoring *scene, ecs_entity entity);

const char *content_basename(const char *path);
bool content_path_in_folder(const app_scene_authoring *scene, const char *path, const char *folder);
void content_refresh(app_scene_authoring *scene);
void asset_index_invalidate(app_scene_authoring *scene);
bool editable_scalar(const pin_value *value);

void finish_property_edit(app_scene_authoring *scene);
void finish_transform_edit(app_scene_authoring *scene, bool cancel);
void fit_scene_view(engine_viewport *viewport, ecs_world *world, ecs_entity only);
void free_behavior_names(char **names);
char **instance_behavior_names(app_scene_authoring *scene, ecs_entity entity);
scene_prototype_asset *previewed_prototype(app_scene_authoring *scene);

bool prototype_has_component_id(const game_prototype *prototype, ecs_component_id id);
void prototype_history_before(app_scene_authoring *scene, game_prototype *prototype);
const char *runtime_entity_label(const app_scene_authoring *scene, ecs_entity entity);
bool runtime_session(const app_scene_authoring *scene);
bool scene_can_move(const app_scene_authoring *scene);
void select_instance(app_scene_authoring *scene, int actor_index);
scene_actor *selected_actor(app_scene_authoring *scene);
scene_prototype_asset *selected_prototype(app_scene_authoring *scene);
ecs_entity selected_scene_entity(const app_scene_authoring *scene);
int write_prototype_field(
    app_scene_authoring *scene,
    scene_prototype_asset *asset,
    const char *component,
    const char *field,
    pin_value value
);
#define APP_SCENE_DOCUMENT_ID "Scene"

int actor_has_behavior(game_behavior_world *behaviors, const scene_actor *actor, const char *asset_name);
int add_actor(app_scene_authoring *scene, const char *name, const char *prototype_name);
int add_instance_at(app_scene_authoring *scene, const char *prototype_name, const GameTransform *position);
game_prototype_registry app_component_registry(void);
int apply_components_json(app_scene_authoring *scene, ecs_entity entity, const char *components_json);
int attach_prototype_behavior(ecs_world *world, ecs_entity entity, const char *asset_name, void *user);
void clear_prototype_history(scene_prototype_asset *asset);
char *content_path_join(const char *base, const char *name);
scene_prototype_asset *find_prototype(app_scene_authoring *scene, const char *name);
void free_behavior_snapshots(game_prototype **snapshots);
void invalidate_runtime_snapshot(app_scene_authoring *scene);
int open_behavior_asset(app_scene_authoring *scene, const char *asset_name);
int open_system_asset(app_scene_authoring *scene, const char *path);
void preview_behavior_edits(app_scene_authoring *scene);
scene_prototype_asset *prototype_at(app_scene_authoring *scene, int index);
int prototype_index(app_scene_authoring *scene, const char *name);
int prototype_redo(app_scene_authoring *scene);
int prototype_undo(app_scene_authoring *scene);
char *read_scene_file(const char *path);
int refresh_prototype_instances(app_scene_authoring *scene, scene_prototype_asset *asset);
void remove_selected(app_scene_authoring *scene);
int replay_scene_edit(app_scene_authoring *scene, bool redo);
const ecs_component_desc *resolve_system_component(const char *name, void *);
int restore_retained_components(app_scene_authoring *scene, scene_actor *actor, const game_prototype *prototype);
int save_active_document(app_scene_authoring *scene);
int save_behavior_document(app_scene_authoring *scene);
void select_prototype(app_scene_authoring *scene, int index);
void set_behavior_component_context(app_scene_authoring *scene, const char *asset_name);
int sync_behavior_working(app_scene_authoring *scene);
void sync_editor_inspection(app_scene_authoring *scene);
const char *system_component_name(ecs_component_id id, void *);
void write_position(app_scene_authoring *scene, ecs_entity entity, GameTransform position);
int write_text_file_atomic(const char *path, const char *text);
#endif
