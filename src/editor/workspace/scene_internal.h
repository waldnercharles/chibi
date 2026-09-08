#ifndef CBLUEPRINT_SCENE_INTERNAL_H
#define CBLUEPRINT_SCENE_INTERNAL_H
#include "editor/documents/scene_history.h"
#include "editor/documents/scene_retention.h"
#include "editor/ui/color.h"
#include "editor/workspace/editor.h"
#include "editor/workspace/prototype_still.h"
#include "editor/workspace/scene_authoring.h"
#include "editor/workspace/scene_debug.h"
#include "editor/workspace/scene_navigation.h"
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
} scene_content_entry;

/* Private authoring state. Module replacement transfers versioned document
 * checkpoints, never this layout or its runtime pointers. */
struct app_scene_authoring
{
    app_sdf_shapes shapes;
    app_scene_renderer *renderer;
    engine_viewport *render_viewport;
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
    game_system_document *system_document;
    char *system_path;
    engine *runtime;
    game_prototype_preview *prototype_preview;
    prototype_still *still_preview;
    char *preview_prototype_id;
    int runtime_preview_prototype;
    scene_actor *actors;
    scene_prototype_asset *prototypes;
    scene_content_entry *content;
    char *content_root;
    char *content_selected_path;
    int selected;
    ecs_entity reveal_actor;
    ecs_entity runtime_selected;
    ecs_entity *runtime_entities;
    int selected_prototype;
    int selected_attachment;
    scene_debug debug;
    app_runtime_target debug_target;
    uint64_t debug_sessions[2];
    bool debug_pinned;
    app_debug_status debug_status;
    editor_color_state property_color;
    char component_search[128];
    char behavior_search[128];
    bool runtime_snapshot_valid;
    bool runtime_playing;
    bool runtime_step;
    bool movement_active[2];
    bool preview_playing;
    bool preview_step;
    bool preview_loop;
    bool preview_follow;
    bool preview_has_time;
    double preview_last_time;
    double preview_elapsed;
    double runtime_elapsed;
    GameTransform preview_origin;
    app_runtime_target active_runtime_target;
    scene_camera camera;
    int camera_target;
    scene_history *history;
    scene_navigation *navigation;
    bool navigation_replaying;
    char navigation_error[256];
    ecs_entity edit_actor;
    ecs_entity property_actor;
    scene_field_edit property_edit;
    bool property_dirty;
    bool property_seen;
    GameTransform edit_before;
    GameTransform drag_origin;
    bool edit_dirty;
    bool dragging_actor;
    bool marquee;
    bool marquee_visible;
    GameTransform marquee_start;
    GameTransform marquee_end;
    int edit_field;
    float selection_scale;
    bool navigator_scene;
    char asset_search[128];
    double content_scan_time;
    char scene_search[128];
    app_editor_document_context document;
    char *document_id;
    char *graph_document_id;
    app_editor_document_kind graph_document_kind;
    char *scene_path;
    bool scene_dirty;
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
/* Scene model operations used by its native panels. */

int asset_search_score(const app_scene_authoring *scene, const char *path);

void begin_transform_edit(app_scene_authoring *scene, ecs_entity entity);

const char *content_basename(const char *path);
bool content_path_in_folder(const app_scene_authoring *scene, const char *path, const char *folder);
bool content_path_is_child(const char *parent, const char *path);
int content_prototype_index(const app_scene_authoring *scene, const char *path);
void content_refresh(app_scene_authoring *scene);
int create_behavior_asset(app_scene_authoring *scene);
int create_graph_asset(app_scene_authoring *scene, behavior_domain domain);
int create_new_prototype(app_scene_authoring *scene);
void draw_navigator(app_scene_authoring *scene);
void draw_prototype_inspector(app_scene_authoring *scene);
void draw_scene_inspector(app_scene_authoring *scene);
bool editable_scalar(const pin_value *value);

void finish_property_edit(app_scene_authoring *scene);
void finish_transform_edit(app_scene_authoring *scene, bool cancel);
void fit_scene_view(engine_viewport *viewport, ecs_world *world, ecs_entity only);
int focus_prototype_behavior(app_scene_authoring *scene, const char *asset_name);
void free_behavior_names(char **names);
char **instance_behavior_names(app_scene_authoring *scene, ecs_entity entity);
void open_content_entry(app_scene_authoring *scene, const scene_content_entry *entry);
scene_prototype_asset *previewed_prototype(app_scene_authoring *scene);

const game_prototype_component *prototype_component_by_id(
    const game_prototype *prototype,
    ecs_component_id id
);
bool prototype_has_component_id(const game_prototype *prototype, ecs_component_id id);
void prototype_history_before(app_scene_authoring *scene, game_prototype *prototype);
const char *runtime_entity_label(const app_scene_authoring *scene, ecs_entity entity);
bool runtime_session(const app_scene_authoring *scene);
bool scene_can_move(const app_scene_authoring *scene);
bool scene_search_matches(const char *query, const char *text);
void select_content_entry(app_scene_authoring *scene, const scene_content_entry *entry);
void select_instance(app_scene_authoring *scene, int actor_index);
scene_actor *selected_actor(app_scene_authoring *scene);
int selected_attachment(app_scene_authoring *scene, game_behavior_attachment_info *out);
int selected_behavior_attachment(app_scene_authoring *scene, game_behavior_attachment_info *out);
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
int append_prototype_asset(app_scene_authoring *scene, game_prototype *prototype, const char *path);
int apply_components_json(app_scene_authoring *scene, ecs_entity entity, const char *components_json);
int attach_prototype_behavior(ecs_world *world, ecs_entity entity, const char *asset_name, void *user);
void clear_prototype_history(scene_prototype_asset *asset);
char *content_path_join(const char *base, const char *name);
void content_select_path(app_scene_authoring *scene, const char *path);
scene_prototype_asset *find_prototype(app_scene_authoring *scene, const char *name);
void free_behavior_snapshots(game_prototype **snapshots);
void invalidate_runtime_snapshot(app_scene_authoring *scene);
int load_system_asset(app_scene_authoring *scene, const char *path, bool reload);
int open_behavior_asset(app_scene_authoring *scene, const char *asset_name);
void open_prototype(app_scene_authoring *scene, int index);
int open_system_asset(app_scene_authoring *scene, const char *path);
void preview_behavior_edits(app_scene_authoring *scene);
scene_prototype_asset *prototype_at(app_scene_authoring *scene, int index);
int prototype_index(app_scene_authoring *scene, const char *name);
int prototype_redo(app_scene_authoring *scene);
int prototype_undo(app_scene_authoring *scene);
char *read_scene_file(const char *path);
int refresh_prototype_instances(app_scene_authoring *scene, scene_prototype_asset *asset);
int reload_active_document(app_scene_authoring *scene);
void remove_selected(app_scene_authoring *scene);
int replay_scene_edit(app_scene_authoring *scene, bool redo);
const ecs_component_desc *resolve_system_component(const char *name, void *);
int restore_retained_components(app_scene_authoring *scene, scene_actor *actor, const game_prototype *prototype);
int save_active_document(app_scene_authoring *scene);
int save_all_documents(app_scene_authoring *scene);
int save_as_active_document(app_scene_authoring *scene);
void select_prototype(app_scene_authoring *scene, int index);
void set_behavior_component_context(app_scene_authoring *scene, const char *asset_name);
int sync_behavior_working(app_scene_authoring *scene);
void sync_editor_inspection(app_scene_authoring *scene);
const char *system_component_name(ecs_component_id id, void *);
void write_position(app_scene_authoring *scene, ecs_entity entity, GameTransform position);
int write_text_file_atomic(const char *path, const char *text);
#endif
