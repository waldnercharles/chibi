#include "scene_authoring.h"

#include "../core/array.h"
#include "../core/pin_fmt.h"
#include "../core/registry.h"
#include "../core/serialize.h"
#include "../core/value_array.h"
#include "../core/xalloc.h"
#include "../editor/choices.h"
#include "../editor/color.h"
#include "../editor/editor.h"
#include "../editor/flags.h"
#include "../editor/labels.h"
#include "../editor/ui_shell.h"
#include "../game/appearance_geometry.h"
#include "../game/gameplay.h"
#include "../game/generated.h"
#include "../game/prototype.h"
#include "../game/prototype_preview.h"
#include "../game/scene_document.h"
#include "../game/sdf_motion.h"
#include "behavior_fixtures.h"
#include "scene_draw.h"
#include "scene_history.h"
#include "scene_imgui.h"
#include "scene_renderer.h"

#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static const char APP_SCENE_DOCUMENT_ID[] = "Scene";
static const mode_t DOCUMENT_DIRECTORY_MODE = 511;

enum
{
    CONTENT_PATH_CAP = 512,
};

static int save_active_document(app_scene_authoring *scene);
static int app_ui_command(editor_ui_command command, void *user);
static int app_editor_save(editor *, void *user);
static char *read_scene_file(const char *path);
static int open_system_asset(app_scene_authoring *scene, const char *path);

static int write_text_file_atomic(const char *path, const char *text)
{
    size_t size = strlen(path) + 5;
    char *temporary = (char *)xmalloc(size);
    snprintf(temporary, size, "%s.tmp", path);
    FILE *file = fopen(temporary, "wb");
    if (!file) {
        free(temporary);
        return -1;
    }
    int result = fputs(text, file) < 0;
    if (fclose(file) != 0) {
        result = -1;
    }
    if (!result && rename(temporary, path) != 0) {
        result = -1;
    }
    if (result) {
        remove(temporary);
    }
    free(temporary);
    return result;
}

static game_prototype_registry app_component_registry(void)
{
    return (game_prototype_registry){
        GAME_REGISTRY_COMPONENTS,
        GAME_REGISTRY_COMPONENT_COUNT,
        NULL,
        NULL,
    };
}

typedef struct
{
    ecs_entity entity;
    char *name;
    char *prototype_name;
    game_prototype *base;
    game_prototype *runtime_snapshot;
    game_prototype *restored_state;
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
} scene_prototype_asset;

typedef struct
{
    char *path;
    bool directory;
} scene_content_entry;

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
    char *preview_prototype_id;
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
    editor_color_state property_color;
    char **prototype_undo;
    char **prototype_redo;
    bool runtime_snapshot_valid;
    bool runtime_playing;
    bool runtime_step;
    bool preview_playing;
    bool preview_step;
    app_runtime_target active_runtime_target;
    scene_camera camera;
    int camera_target;
    scene_history *history;
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

static int refresh_prototype_instances(app_scene_authoring *scene, scene_prototype_asset *asset);
static void finish_transform_edit(app_scene_authoring *scene, bool cancel);

/* Collision bits authored by the Player, Enemy, and Projectile prototypes. */
static const editor_flag COLLISION_FLAGS[] = {
    { 1u, "Player" },
    { 2u, "Enemy" },
    { 4u, "Projectile" },
};
static const editor_flags COLLISION_CATEGORIES = { COLLISION_FLAGS, 3 };

static bool collision_category_field(const type_desc *type, const type_field *field)
{
    return type == GAME_COMPONENT_GameCollider.type && field &&
           field->type == PIN_U32 &&
           (field->offset == offsetof(GameCollider, category) ||
            field->offset == offsetof(GameCollider, mask));
}

static bool appearance_color_field(const type_desc *type, const type_field *field)
{
    if (!field || field->type != PIN_U32) {
        return false;
    }
    if (type == GAME_COMPONENT_GameMaterial.type) {
        return true;
    }
    return type == GAME_COMPONENT_GameAppearance.type &&
           (field->offset == offsetof(GameAppearance, color) ||
            field->offset == offsetof(GameAppearance, border_color) ||
            field->offset == offsetof(GameAppearance, glow_color));
}

static const editor_choice SHAPES[] = {
    { GAME_SHAPE_BOX, "Box" },
    { GAME_SHAPE_CIRCLE, "Circle" },
    { GAME_SHAPE_DIAMOND, "Diamond" },
    { GAME_SHAPE_CHEVRON, "Chevron" },
    { GAME_SHAPE_STROKED_CHEVRON, "Chevron Outline" },
    { GAME_SHAPE_CAPSULE, "Capsule" },
};
static const editor_choices APPEARANCE_SHAPES = { SHAPES, 6 };

static bool appearance_shape_field(const type_desc *type, const type_field *field)
{
    return type == GAME_COMPONENT_GameAppearance.type && field &&
           field->type == PIN_I32 &&
           field->offset == offsetof(GameAppearance, shape);
}

static const editor_choices *resolve_pin_choices(const node *n, int output, int pin, void *)
{
    const type_desc *type = n->desc->type;
    return !output && type && n->desc->pure_kind == NODE_PURE_MAKE &&
                   pin >= 0 && pin < type->field_count &&
                   appearance_shape_field(type, &type->fields[pin])
             ? &APPEARANCE_SHAPES
             : NULL;
}

static bool resolve_pin_color(const node *n, int output, int pin, void *)
{
    if (!output && pin == 2 && n->desc == registry_node_by_name("Emit Particles")) {
        return true;
    }
    const type_desc *type = n->desc->type;
    return !output && type && n->desc->pure_kind == NODE_PURE_MAKE &&
           pin >= 0 && pin < type->field_count &&
           appearance_color_field(type, &type->fields[pin]);
}

static const editor_flags *resolve_pin_flags(const node *n, int output, int pin, void *)
{
    if (!output && pin == 2 && n->desc == registry_node_by_name("Find Nearest Entity")) {
        return &COLLISION_CATEGORIES;
    }
    const type_desc *type = n->desc->type;
    const type_field *field = NULL;
    if (type && pin >= 0 && pin < type->field_count &&
        ((!output && n->desc->pure_kind == NODE_PURE_MAKE) ||
         (output && n->desc->pure_kind == NODE_PURE_BREAK))) {
        field = &type->fields[pin];
    }
    if (output && pin >= 0 && pin < n->output_count && n->output_pins[pin].field_source) {
        int source;
        field = graph_output_field(n, pin, &source);
        if (field) {
            type = registry_type_by_name(n->output_pins[source].type_name);
        }
    }
    return collision_category_field(type, field) ? &COLLISION_CATEGORIES : NULL;
}

static bool format_entity_value(
    const pin_value *value,
    const behavior_instance *instance,
    char *buf,
    size_t cap,
    void *user
)
{
    if (value->type != PIN_HANDLE || value->container != PIN_CONTAINER_SINGLE ||
        !value->type_name || strcmp(value->type_name, "ecs.entity") != 0) {
        return false;
    }
    if (!value->u64) {
        snprintf(buf, cap, "None");
        return true;
    }
    if (!instance) {
        return false;
    }
    app_scene_authoring *scene = user;
    ecs_world *world = behavior_instance_service(instance, "ecs.world");
    ecs_world *preview_world = game_prototype_preview_world(scene->prototype_preview);
    if (!world || (world != scene->world && world != preview_world)) {
        return false;
    }
    ecs_entity entity = ecs_entity_from_u64(value->u64);
    if (!ecs_entity_is_alive(world, entity)) {
        snprintf(buf, cap, "Deleted entity");
        return true;
    }
    if (world == scene->world) {
        for (int i = 0; i < acount(scene->actors); i++) {
            if (ecs_entity_to_u64(scene->actors[i].entity) == value->u64) {
                snprintf(buf, cap, "%s", scene->actors[i].name);
                return true;
            }
        }
    }
    if (world == preview_world && scene->preview_prototype_id &&
        ecs_entity_to_u64(game_prototype_preview_entity(scene->prototype_preview)) ==
            value->u64) {
        snprintf(buf, cap, "%s", scene->preview_prototype_id);
        return true;
    }
    snprintf(buf, cap, "Entity %u", (unsigned)entity.slot);
    return true;
}

static const ecs_component_desc *resolve_system_component(const char *name, void *)
{
    for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
        const ecs_component_desc *component = GAME_REGISTRY_COMPONENTS[i];
        if (strcmp(component->name, name) == 0 ||
            (component->key && strcmp(component->key, name) == 0)) {
            return component;
        }
    }
    return NULL;
}

static const char *system_component_name(ecs_component_id id, void *)
{
    for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
        const ecs_component_desc *component = GAME_REGISTRY_COMPONENTS[i];
        if (component->id == id) {
            return component->key ? component->key : component->name;
        }
    }
    return NULL;
}

static char *content_path_join(const char *base, const char *name)
{
    size_t base_size = strlen(base);
    size_t name_size = strlen(name);
    char *path = (char *)xmalloc(base_size + name_size + 2);
    memcpy(path, base, base_size);
    path[base_size] = '/';
    memcpy(path + base_size + 1, name, name_size + 1);
    return path;
}

static void content_scan_directory(app_scene_authoring *scene, const char *path)
{
    DIR *directory = opendir(path);
    if (!directory) {
        return;
    }
    struct dirent *entry;
    while ((entry = readdir(directory))) {
        if (entry->d_name[0] == '.') {
            continue;
        }
        char *child = content_path_join(path, entry->d_name);
        struct stat info;
        if (lstat(child, &info) != 0) {
            free(child);
            continue;
        }
        bool is_directory = S_ISDIR(info.st_mode);
        apush(scene->content, (scene_content_entry){ child, is_directory });
        if (is_directory) {
            content_scan_directory(scene, child);
        }
    }
    closedir(directory);
}

static int content_entry_compare(const void *left, const void *right)
{
    const scene_content_entry *a = (const scene_content_entry *)left;
    const scene_content_entry *b = (const scene_content_entry *)right;
    return strcmp(a->path, b->path);
}

static void content_refresh(app_scene_authoring *scene)
{
    for (int i = 0; i < acount(scene->content); i++) {
        free(scene->content[i].path);
    }
    asetlen(scene->content, 0);
    content_scan_directory(scene, scene->content_root);
    qsort(scene->content, (size_t)acount(scene->content), sizeof *scene->content, content_entry_compare);
}

static const char *content_basename(const char *path)
{
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static bool content_path_is_child(const char *parent, const char *path)
{
    size_t parent_size = strlen(parent);
    size_t path_size = strlen(path);
    if (path_size <= parent_size || strncmp(parent, path, parent_size) != 0 ||
        path[parent_size] != '/') {
        return false;
    }
    return strchr(path + parent_size + 1, '/') == NULL;
}

static bool content_path_is_descendant(const char *parent, const char *path)
{
    size_t parent_size = strlen(parent);
    return strlen(path) > parent_size &&
           strncmp(parent, path, parent_size) == 0 && path[parent_size] == '/';
}

static bool content_path_in_folder(const app_scene_authoring *scene, const char *path, const char *folder)
{
    char directory[CONTENT_PATH_CAP];
    int length = snprintf(directory, sizeof directory, "%s/%s", scene->content_root, folder);
    return length > 0 && length < (int)sizeof directory &&
           content_path_is_descendant(directory, path);
}

static void content_select_path(app_scene_authoring *scene, const char *path)
{
    free(scene->content_selected_path);
    scene->content_selected_path = xstrdup(path);
}

static scene_actor *selected_actor(app_scene_authoring *scene)
{
    if (!scene || scene->selected < 0 || scene->selected >= acount(scene->actors)) {
        return NULL;
    }
    return &scene->actors[scene->selected];
}

static const scene_actor *selected_actor_const(const app_scene_authoring *scene)
{
    if (!scene || scene->selected < 0 || scene->selected >= acount(scene->actors)) {
        return NULL;
    }
    return &scene->actors[scene->selected];
}

static ecs_entity selected_scene_entity(const app_scene_authoring *scene)
{
    if (scene->runtime_selected.slot) {
        return game_entities_owns(scene->live->entities, scene->runtime_selected)
                 ? scene->runtime_selected
                 : (ecs_entity){ 0 };
    }
    const scene_actor *actor = selected_actor_const(scene);
    return actor && ecs_entity_is_alive(scene->world, actor->entity)
             ? actor->entity
             : (ecs_entity){ 0 };
}

static const char *runtime_entity_label(const app_scene_authoring *scene, ecs_entity entity)
{
    for (int i = 0; i < acount(scene->prototypes); i++) {
        const game_prototype *prototype = scene->prototypes[i].prototype;
        if (ecs_component_has(
                scene->world,
                entity,
                game_prototype_tag_desc(prototype)->id
            )) {
            return prototype->name;
        }
    }
    return "Entity";
}

static scene_prototype_asset *prototype_at(app_scene_authoring *scene, int index)
{
    return scene && index >= 0 && index < acount(scene->prototypes)
             ? &scene->prototypes[index]
             : NULL;
}

static scene_prototype_asset *find_prototype(app_scene_authoring *scene, const char *name)
{
    if (!scene || !name) {
        return NULL;
    }
    for (int i = 0; i < acount(scene->prototypes); i++) {
        if (strcmp(scene->prototypes[i].prototype->name, name) == 0) {
            return &scene->prototypes[i];
        }
    }
    return NULL;
}

static scene_prototype_asset *selected_prototype(app_scene_authoring *scene)
{
    return prototype_at(scene, scene ? scene->selected_prototype : -1);
}

static scene_prototype_asset *previewed_prototype(app_scene_authoring *scene)
{
    if (!scene->preview_prototype_id) {
        return NULL;
    }
    for (int i = 0; i < acount(scene->prototypes); i++) {
        scene_prototype_asset *asset = &scene->prototypes[i];
        if (strcmp(game_prototype_identity(asset->prototype), scene->preview_prototype_id) ==
            0) {
            return asset;
        }
    }
    return NULL;
}

static void clear_prototype_history(app_scene_authoring *scene)
{
    for (int i = 0; i < acount(scene->prototype_undo); i++) {
        free(scene->prototype_undo[i]);
    }
    for (int i = 0; i < acount(scene->prototype_redo); i++) {
        free(scene->prototype_redo[i]);
    }
    afree(scene->prototype_undo);
    afree(scene->prototype_redo);
}

static void prototype_history_before(app_scene_authoring *scene, game_prototype *prototype)
{
    char *json = game_prototype_save(prototype);
    if (json) {
        apush(scene->prototype_undo, json);
    }
    for (int i = 0; i < acount(scene->prototype_redo); i++) {
        free(scene->prototype_redo[i]);
    }
    afree(scene->prototype_redo);
}

static void set_document(app_scene_authoring *scene, app_editor_document_kind kind, const char *id)
{
    if (scene->document.kind != kind || !scene->document_id ||
        strcmp(scene->document_id, id) != 0) {
        finish_transform_edit(scene, scene->dragging_actor);
        scene->navigator_scene = kind == APP_EDITOR_DOCUMENT_SCENE;
    }
    char *copy = xstrdup(id);
    free(scene->document_id);
    scene->document_id = copy;
    scene->document = (app_editor_document_context){ kind, copy };
    bool graph_active = kind == APP_EDITOR_DOCUMENT_BEHAVIOR ||
                        kind == APP_EDITOR_DOCUMENT_SYSTEM;
    editor_set_inspector_active(scene->graph_editor, graph_active);
    if (!graph_active) {
        editor_set_graph_visible(scene->graph_editor, scene->graph_document_id != NULL);
        return;
    }
    char *graph_id = xstrdup(id);
    free(scene->graph_document_id);
    scene->graph_document_id = graph_id;
    scene->graph_document_kind = kind;
    editor_set_ui_context(
        scene->graph_editor,
        kind == APP_EDITOR_DOCUMENT_SYSTEM ? EDITOR_UI_CONTEXT_SYSTEM : EDITOR_UI_CONTEXT_BEHAVIOR
    );
    editor_set_graph_visible(scene->graph_editor, 1);
}

static bool document_asset_active(
    const app_scene_authoring *scene,
    app_editor_document_kind kind,
    const char *id
)
{
    switch (scene->document.kind) {
        case APP_EDITOR_DOCUMENT_SCENE: return false;
        case APP_EDITOR_DOCUMENT_PROTOTYPE:
        case APP_EDITOR_DOCUMENT_BEHAVIOR:
            return kind == scene->document.kind && scene->document.id && id &&
                   strcmp(scene->document.id, id) == 0;
        case APP_EDITOR_DOCUMENT_SYSTEM: return false;
    }
    return false;
}

static int prototype_index(app_scene_authoring *scene, const char *name)
{
    scene_prototype_asset *asset = find_prototype(scene, name);
    return asset ? (int)(asset - scene->prototypes) : -1;
}

static int behavior_prototype_index(app_scene_authoring *scene, const char *asset_name)
{
    scene_actor *actor = selected_actor(scene);
    int index = actor ? prototype_index(scene, actor->prototype_name) : -1;
    scene_prototype_asset *asset = prototype_at(scene, index);
    if (asset && game_prototype_has_graph(asset->prototype, asset_name)) {
        return index;
    }
    for (int i = 0; i < acount(scene->prototypes); i++) {
        if (game_prototype_has_graph(scene->prototypes[i].prototype, asset_name)) {
            return i;
        }
    }
    return -1;
}

static void set_behavior_component_context(app_scene_authoring *scene, const char *asset_name)
{
    scene->selected_prototype = behavior_prototype_index(scene, asset_name);
}

static int selected_attachment(app_scene_authoring *scene, game_behavior_attachment_info *out)
{
    ecs_entity entity = selected_scene_entity(scene);
    if (!entity.slot || !out) {
        return 0;
    }
    game_behavior_world *behaviors = scene->live->behaviors;
    int count = game_behavior_attachment_count_for_entity(behaviors, entity);
    if (count == 0) {
        scene->selected_attachment = -1;
        return 0;
    }
    if (scene->selected_attachment < 0 || scene->selected_attachment >= count) {
        scene->selected_attachment = 0;
    }
    return game_behavior_attachment_info_at(behaviors, entity, scene->selected_attachment, out) ==
           0;
}

static int selected_behavior_attachment(app_scene_authoring *scene, game_behavior_attachment_info *out)
{
    if (!scene->definition || !scene->definition->name || !out) {
        return 0;
    }
    game_behavior_world *behaviors = scene->live->behaviors;
    ecs_entity entity;
    if (scene->active_runtime_target == APP_RUNTIME_PREVIEW) {
        behaviors = game_prototype_preview_behaviors(scene->prototype_preview);
        entity = game_prototype_preview_entity(scene->prototype_preview);
    } else {
        entity = selected_scene_entity(scene);
        if (!entity.slot) {
            return 0;
        }
    }
    int count = game_behavior_attachment_count_for_entity(behaviors, entity);
    for (int i = 0; i < count; i++) {
        game_behavior_attachment_info info;
        if (game_behavior_attachment_info_at(behaviors, entity, i, &info) != 0 ||
            strcmp(info.asset_name, scene->definition->name) != 0) {
            continue;
        }
        *out = info;
        return 1;
    }
    return 0;
}

static void select_prototype(app_scene_authoring *scene, int index)
{
    if (index < 0 || index >= acount(scene->prototypes)) {
        return;
    }
    if (scene->selected_prototype != index) {
        clear_prototype_history(scene);
    }
    scene->selected_prototype = index;
    snprintf(
        scene->prototype_message,
        sizeof scene->prototype_message,
        "Selected %s.",
        scene->prototypes[index].prototype->name
    );
}

static void open_prototype(app_scene_authoring *scene, int index)
{
    if (index < 0 || index >= acount(scene->prototypes)) {
        return;
    }
    select_prototype(scene, index);
    set_document(
        scene,
        APP_EDITOR_DOCUMENT_PROTOTYPE,
        scene->prototypes[index].prototype->name
    );
    editor_ui_set_pane_visible(EDITOR_UI_PANE_INSPECTOR, true);
    editor_ui_request_focus(EDITOR_UI_FOCUS_INSPECTOR);
}

static void select_instance(app_scene_authoring *scene, int actor_index)
{
    if (actor_index < 0 || actor_index >= acount(scene->actors)) {
        return;
    }
    if (scene->selected != actor_index) {
        finish_transform_edit(scene, scene->dragging_actor);
    }
    for (int i = 0; i < acount(scene->actors); i++) {
        scene->actors[i].marked = i == actor_index;
    }
    scene->runtime_selected = (ecs_entity){ 0 };
    scene->selected = actor_index;
    if (scene->document.kind != APP_EDITOR_DOCUMENT_BEHAVIOR) {
        set_document(scene, APP_EDITOR_DOCUMENT_SCENE, APP_SCENE_DOCUMENT_ID);
    }
    scene->selected_attachment = game_behavior_attachment_count_for_entity(
                                     scene->live->behaviors,
                                     scene->actors[actor_index].entity
                                 )
                                   ? 0
                                   : -1;
}

int app_scene_authoring_select_runtime_entity(app_scene_authoring *scene, ecs_entity entity)
{
    if (!scene || !game_entities_owns(scene->live->entities, entity)) {
        return -1;
    }
    finish_transform_edit(scene, scene->dragging_actor);
    scene->runtime_selected = entity;
    scene->selected = -1;
    scene->selected_attachment = 0;
    scene->active_runtime_target = APP_RUNTIME_SCENE;
    set_document(scene, APP_EDITOR_DOCUMENT_SCENE, APP_SCENE_DOCUMENT_ID);
    editor_ui_set_pane_visible(EDITOR_UI_PANE_INSPECTOR, true);
    return 0;
}

static bool scene_can_move(const app_scene_authoring *scene)
{
    return scene->active_runtime_target == APP_RUNTIME_SCENE &&
           !scene->runtime_playing && !scene->runtime_step &&
           !game_entities_initializing(scene->live->entities);
}

static void write_position(app_scene_authoring *scene, ecs_entity entity, GameTransform position)
{
    if (!isfinite(position.x) || !isfinite(position.y)) {
        return;
    }
    ecs_component_write_field(
        scene->world,
        entity,
        GAME_COMPONENT_GameTransform.id,
        0,
        (pin_value){ .type = PIN_F32, .f = position.x }
    );
    ecs_component_write_field(
        scene->world,
        entity,
        GAME_COMPONENT_GameTransform.id,
        1,
        (pin_value){ .type = PIN_F32, .f = position.y }
    );
}

static bool editable_scalar(const pin_value *value)
{
    switch (value->type) {
        case PIN_BOOL:
        case PIN_I8:
        case PIN_U8:
        case PIN_I16:
        case PIN_U16:
        case PIN_I32:
        case PIN_U32:
        case PIN_I64:
        case PIN_U64:
        case PIN_ENUM: return true;
        case PIN_F32: return isfinite(value->f);
        case PIN_F64: return isfinite(value->d);
        default: return false;
    }
}

static void finish_property_edit(app_scene_authoring *scene)
{
    if (!scene->property_actor.slot) {
        return;
    }
    if (!scene_history_push_fields(scene->history, scene->property_actor, &scene->property_edit, 1)) {
        scene->scene_dirty = scene->property_dirty;
    }
    scene->property_actor = (ecs_entity){ 0 };
}

static void begin_transform_edit(app_scene_authoring *scene, ecs_entity entity)
{
    finish_transform_edit(scene, false);
    scene->edit_actor = entity;
    scene->edit_before = *GAME_GameTransform_get_const(scene->world, entity);
    scene->edit_dirty = scene->scene_dirty;
    scene->edit_field = -1;
}

static void finish_transform_edit(app_scene_authoring *scene, bool cancel)
{
    finish_property_edit(scene);
    if (!scene->edit_actor.slot) {
        return;
    }
    const GameTransform
        *position = GAME_GameTransform_get_const(scene->world, scene->edit_actor);
    if (position && cancel) {
        write_position(scene, scene->edit_actor, scene->edit_before);
        scene->scene_dirty = scene->edit_dirty;
    }
    if (position && !cancel) {
        if (!scene_history_push(scene->history, scene->edit_actor, scene->edit_before, *position)) {
            scene->scene_dirty = scene->edit_dirty;
        }
    }
    scene->edit_actor = (ecs_entity){ 0 };
    scene->dragging_actor = false;
}

static void sync_editor_inspection(app_scene_authoring *scene)
{
    game_behavior_attachment_info info;
    int has_attachment = scene->graph_document_id && scene->graph_document_kind ==
                                                         APP_EDITOR_DOCUMENT_BEHAVIOR
                           ? selected_behavior_attachment(scene, &info)
                           : 0;
    const flow_run *run = NULL;
    const behavior_instance_trace_entry *trace = NULL;
    int trace_count = 0;
    if (has_attachment) {
        for (int i = acount(info.instance->activations) - 1; i >= 0; i--) {
            flow_state state = flow_run_status(info.instance->activations[i]);
            if (state != FLOW_COMPLETED && state != FLOW_FAILED) {
                run = info.instance->activations[i];
                break;
            }
        }
        trace = behavior_instance_trace(info.instance, &trace_count);
    }
    editor_set_inspection_instance(scene->graph_editor, has_attachment ? info.instance : NULL);
    editor_set_inspection_run(scene->graph_editor, run);
    editor_set_inspection_trace(
        scene->graph_editor,
        has_attachment ? trace : NULL,
        has_attachment ? trace_count : 0
    );
}

static int attach_prototype_behavior(ecs_world *world, ecs_entity entity, const char *asset_name, void *user)
{
    app_scene_authoring *scene = (app_scene_authoring *)user;
    if (!scene || !game_behavior_asset_registered(scene->live->behaviors, asset_name)) {
        return -1;
    }
    return game_behavior_attach(scene->live->behaviors, entity, asset_name);
}

static int actor_has_behavior(game_behavior_world *behaviors, const scene_actor *actor, const char *asset_name)
{
    int count = game_behavior_attachment_count_for_entity(behaviors, actor->entity);
    for (int i = 0; i < count; i++) {
        game_behavior_attachment_info info;
        if (game_behavior_attachment_info_at(behaviors, actor->entity, i, &info) == 0 &&
            strcmp(info.asset_name, asset_name) == 0) {
            return 1;
        }
    }
    return 0;
}

static void free_behavior_snapshots(game_prototype **snapshots)
{
    for (int i = 0; i < acount(snapshots); i++) {
        game_prototype_free(snapshots[i]);
    }
    afree(snapshots);
}

static int add_actor(app_scene_authoring *scene, const char *name, const char *prototype_name)
{
    scene_prototype_asset *asset = find_prototype(scene, prototype_name);
    if (!scene || !name || !name[0] || !asset) {
        return -1;
    }
    if (scene->runtime_snapshot_valid &&
        app_scene_authoring_runtime_command(scene, APP_RUNTIME_SCENE, APP_RUNTIME_STOP) !=
            0) {
        return -1;
    }
    for (int i = 0; i < acount(asset->prototype->graphs); i++) {
        const char *name = asset->prototype->graphs[i];
        if (!game_behavior_asset_registered(scene->live->behaviors, name)) {
            snprintf(
                scene->message,
                sizeof scene->message,
                "Prototype %s references missing behavior asset %s.",
                prototype_name,
                name
            );
            return -1;
        }
    }
    const game_prototype_spawn_hooks hooks = { attach_prototype_behavior, scene };
    char error[256];
    ecs_entity entity = { 0 };
    if (game_prototype_spawn(asset->prototype, scene->world, &hooks, &entity, error, sizeof error) !=
        ECS_OK) {
        snprintf(scene->message, sizeof scene->message, "Could not spawn %s: %s", name, error);
        return -1;
    }
    game_prototype *base = game_prototype_clone(asset->prototype, error, sizeof error);
    if (!base) {
        game_behavior_destroy_entity(scene->live->behaviors, entity);
        snprintf(scene->message, sizeof scene->message, "Could not snapshot %s: %s", name, error);
        return -1;
    }
    scene_actor actor = {
        .entity = entity,
        .name = xstrdup(name),
        .prototype_name = xstrdup(prototype_name),
        .base = base,
    };
    apush(scene->actors, actor);
    scene->scene_dirty = true;
    scene->selected = acount(scene->actors) - 1;
    select_prototype(scene, prototype_index(scene, prototype_name));
    select_instance(scene, scene->selected);
    return 0;
}

static void free_actor(scene_actor *actor)
{
    free(actor->name);
    free(actor->prototype_name);
    game_prototype_free(actor->base);
    game_prototype_free(actor->runtime_snapshot);
    game_prototype_free(actor->restored_state);
}

static int remove_actor_at(app_scene_authoring *scene, int index)
{
    scene_actor *actor = &scene->actors[index];
    if (game_behavior_destroy_entity(scene->live->behaviors, actor->entity) != ECS_OK) {
        return -1;
    }
    free_actor(actor);
    int count = acount(scene->actors);
    memmove(
        &scene->actors[index],
        &scene->actors[index + 1],
        (size_t)(count - index - 1) * sizeof *scene->actors
    );
    asetlen(scene->actors, count - 1);
    scene->scene_dirty = true;
    scene->selected_attachment = -1;
    scene->selected = index < count - 1 ? index : count - 2;
    if (scene->selected >= 0) {
        select_instance(scene, scene->selected);
    } else {
        sync_editor_inspection(scene);
    }
    return 0;
}

static void remove_selected(app_scene_authoring *scene)
{
    if (!selected_actor(scene)) {
        return;
    }
    finish_transform_edit(scene, true);
    scene_history_clear(scene->history);
    /* Unrecorded teardown/load also removes actors destroyed during simulation. */
    if (!ecs_entity_is_alive(scene->world, scene->actors[scene->selected].entity)) {
        free_actor(&scene->actors[scene->selected]);
        int count = acount(scene->actors);
        memmove(
            &scene->actors[scene->selected],
            &scene->actors[scene->selected + 1],
            (size_t)(count - scene->selected - 1) * sizeof *scene->actors
        );
        asetlen(scene->actors, count - 1);
        scene->selected = count - 2;
        return;
    }
    remove_actor_at(scene, scene->selected);
}

static char *capture_actor_state(app_scene_authoring *scene, const scene_actor *actor)
{
    game_prototype *state = game_prototype_capture_entity(
        actor->name,
        scene->world,
        actor->entity,
        &scene->registry
    );
    if (!state) {
        return NULL;
    }
    free(state->identity);
    state->identity = xstrdup(actor->base->identity);
    int count = game_behavior_attachment_count_for_entity(
        scene->live->behaviors,
        actor->entity
    );
    for (int i = 0; i < count; i++) {
        game_behavior_attachment_info info;
        if (game_behavior_attachment_info_at(scene->live->behaviors, actor->entity, i, &info) !=
                0 ||
            game_prototype_add_graph(state, info.asset_name) != 0) {
            game_prototype_free(state);
            return NULL;
        }
    }
    char *json = game_prototype_save(state);
    game_prototype_free(state);
    return json;
}

static bool actor_name_used(const app_scene_authoring *scene, const char *name)
{
    for (int i = 0; i < acount(scene->actors); i++) {
        if (strcmp(scene->actors[i].name, name) == 0) {
            return true;
        }
    }
    return false;
}

static int add_instance_at(app_scene_authoring *scene, const char *prototype_name, const GameTransform *position)
{
    if (!scene || !prototype_name || !find_prototype(scene, prototype_name)) {
        return -1;
    }
    finish_transform_edit(scene, false);
    if (scene->runtime_snapshot_valid &&
        app_scene_authoring_runtime_command(scene, APP_RUNTIME_SCENE, APP_RUNTIME_STOP) !=
            0) {
        return -1;
    }
    char name[256];
    int suffix = 1;
    do {
        if (snprintf(name, sizeof name, "%s_%d", prototype_name, suffix++) >=
            (int)sizeof name) {
            return -1;
        }
    } while (actor_name_used(scene, name));
    bool was_dirty = scene->scene_dirty;
    if (add_actor(scene, name, prototype_name) != 0) {
        return -1;
    }
    scene_actor *actor = &alast(scene->actors);
    if (position && GAME_GameTransform_has(scene->world, actor->entity)) {
        write_position(scene, actor->entity, *position);
    }
    char *state = capture_actor_state(scene, actor);
    char *base = game_prototype_save(actor->base);
    scene_actor_edit edit = { .index = acount(scene->actors) - 1,
                              .name = actor->name,
                              .prototype_name = actor->prototype_name,
                              .base_json = base,
                              .state_json = state,
                              .created = true };
    bool recorded = state && base &&
                    scene_history_push_actor(scene->history, actor->entity, &edit);
    free(state);
    free(base);
    if (!recorded) {
        remove_actor_at(scene, acount(scene->actors) - 1);
        scene->scene_dirty = was_dirty;
        return -1;
    }
    set_document(scene, APP_EDITOR_DOCUMENT_SCENE, APP_SCENE_DOCUMENT_ID);
    return 0;
}

int app_scene_authoring_add_instance(app_scene_authoring *scene, const char *prototype_name)
{
    return add_instance_at(scene, prototype_name, NULL);
}

int app_scene_authoring_selection_count(const app_scene_authoring *scene)
{
    int count = 0;
    for (int i = 0; i < acount(scene->actors); i++) {
        count += scene->actors[i].marked || i == scene->selected;
    }
    return count;
}

static char **instance_behavior_names(app_scene_authoring *scene, ecs_entity entity)
{
    char **names = NULL;
    game_behavior_world *world = scene->live->behaviors;
    int count = game_behavior_attachment_count_for_entity(world, entity);
    for (int i = 0; i < count; i++) {
        game_behavior_attachment_info info;
        int result = game_behavior_attachment_info_at(world, entity, i, &info);
        assert(result == 0);
        apush(names, xstrdup(info.asset_name));
    }
    return names;
}

static void free_behavior_names(char **names)
{
    for (int i = 0; i < acount(names); i++) {
        free(names[i]);
    }
    afree(names);
}

int app_scene_authoring_set_instance_behavior(app_scene_authoring *scene, const char *asset_name, bool attached)
{
    if (!scene || !asset_name || !selected_actor(scene)) {
        return -1;
    }
    finish_transform_edit(scene, false);
    if (scene->runtime_snapshot_valid &&
        app_scene_authoring_runtime_command(scene, APP_RUNTIME_SCENE, APP_RUNTIME_STOP) !=
            0) {
        return -1;
    }
    scene_actor *actor = selected_actor(scene);
    char **before = instance_behavior_names(scene, actor->entity);
    char **after = NULL;
    bool found = false;
    for (int i = 0; i < acount(before); i++) {
        bool match = strcmp(before[i], asset_name) == 0;
        found |= match;
        if (!match || attached) {
            apush(after, xstrdup(before[i]));
        }
    }
    if (attached && !found) {
        apush(after, xstrdup(asset_name));
    }
    int result = 0;
    if (found != attached) {
        result = game_behavior_set_attachments(
            scene->live->behaviors,
            actor->entity,
            (const char *const *)after,
            acount(after)
        );
        if (result == 0) {
            scene_behavior_edit edit = { .before = (const char *const *)before,
                                         .before_count = acount(before),
                                         .after = (const char *const *)after,
                                         .after_count = acount(after) };
            bool recorded = scene_history_push_behaviors(scene->history, actor->entity, &edit);
            assert(recorded);
            scene->selected_attachment = -1;
            scene->scene_dirty = true;
            set_document(scene, APP_EDITOR_DOCUMENT_SCENE, APP_SCENE_DOCUMENT_ID);
            sync_editor_inspection(scene);
        }
    }
    if (result != 0) {
        snprintf(scene->message, sizeof scene->message, "Could not update behavior %s.", asset_name);
    }
    free_behavior_names(before);
    free_behavior_names(after);
    return result;
}

int app_scene_authoring_delete_instance(app_scene_authoring *scene)
{
    if (!scene || !selected_actor(scene)) {
        return -1;
    }
    finish_transform_edit(scene, false);
    if (scene->runtime_snapshot_valid &&
        app_scene_authoring_runtime_command(scene, APP_RUNTIME_SCENE, APP_RUNTIME_STOP) !=
            0) {
        return -1;
    }
    scene_actor *actor = selected_actor(scene);
    char *state = capture_actor_state(scene, actor);
    char *base = game_prototype_save(actor->base);
    if (!state || !base) {
        free(state);
        free(base);
        return -1;
    }
    char *name = xstrdup(actor->name), *prototype = xstrdup(actor->prototype_name);
    ecs_entity entity = actor->entity;
    scene_actor_edit edit = { .index = scene->selected,
                              .name = name,
                              .prototype_name = prototype,
                              .base_json = base,
                              .state_json = state };
    int result = remove_actor_at(scene, scene->selected);
    if (!result) {
        bool recorded = scene_history_push_actor(scene->history, entity, &edit);
        assert(recorded);
        set_document(scene, APP_EDITOR_DOCUMENT_SCENE, APP_SCENE_DOCUMENT_ID);
    }
    free(name);
    free(prototype);
    free(state);
    free(base);
    return result;
}

static int restore_actor(app_scene_authoring *scene, const scene_actor_edit *edit, ecs_entity *entity)
{
    if (actor_name_used(scene, edit->name) || edit->index < 0 ||
        edit->index > acount(scene->actors)) {
        return -1;
    }
    char error[256];
    game_prototype *base = game_prototype_load(edit->base_json, &scene->registry, error, sizeof error);
    game_prototype *state = game_prototype_load(edit->state_json, &scene->registry, error, sizeof error);
    if (!base || !state) {
        game_prototype_free(base);
        game_prototype_free(state);
        return -1;
    }
    for (int i = 0; i < acount(state->graphs); i++) {
        if (!game_behavior_asset_registered(scene->live->behaviors, state->graphs[i])) {
            game_prototype_free(base);
            game_prototype_free(state);
            return -1;
        }
    }
    const game_prototype_spawn_hooks hooks = { attach_prototype_behavior, scene };
    if (game_prototype_spawn(state, scene->world, &hooks, entity, error, sizeof error) !=
        ECS_OK) {
        game_prototype_free(base);
        game_prototype_free(state);
        return -1;
    }
    scene_actor actor = { .entity = *entity,
                          .name = xstrdup(edit->name),
                          .prototype_name = xstrdup(edit->prototype_name),
                          .base = base,
                          .restored_state = state };
    int count = acount(scene->actors);
    apush(scene->actors, actor);
    memmove(
        &scene->actors[edit->index + 1],
        &scene->actors[edit->index],
        (size_t)(count - edit->index) * sizeof *scene->actors
    );
    scene->actors[edit->index] = actor;
    select_instance(scene, edit->index);
    return 0;
}

static void invalidate_runtime_snapshot(app_scene_authoring *scene)
{
    scene->runtime_selected = (ecs_entity){ 0 };
    for (int i = 0; i < acount(scene->actors); i++) {
        game_prototype_free(scene->actors[i].runtime_snapshot);
        scene->actors[i].runtime_snapshot = NULL;
    }
    scene->runtime_snapshot_valid = false;
}

static int capture_runtime_snapshot(app_scene_authoring *scene)
{
    if (scene->runtime_snapshot_valid) {
        return 0;
    }
    game_prototype **snapshots = NULL;
    for (int i = 0; i < acount(scene->actors); i++) {
        scene_actor *actor = &scene->actors[i];
        game_prototype *captured = game_prototype_capture_entity(
            actor->prototype_name,
            scene->world,
            actor->entity,
            &scene->registry
        );
        if (!captured) {
            goto fail;
        }
        /* Re-clone with the authored identity so prototype tags stay stable. */
        free(captured->identity);
        captured->identity = xstrdup(actor->base->identity);
        char error[256];
        game_prototype *snapshot = game_prototype_clone(captured, error, sizeof error);
        game_prototype_free(captured);
        if (!snapshot) {
            goto fail;
        }
        apush(snapshots, snapshot);
        int count = game_behavior_attachment_count_for_entity(
            scene->live->behaviors,
            actor->entity
        );
        for (int j = 0; j < count; j++) {
            game_behavior_attachment_info info;
            if (game_behavior_attachment_info_at(scene->live->behaviors, actor->entity, j, &info) !=
                    0 ||
                game_prototype_add_graph(snapshot, info.asset_name) != 0) {
                goto fail;
            }
        }
    }
    for (int i = 0; i < acount(snapshots); i++) {
        scene->actors[i].runtime_snapshot = snapshots[i];
    }
    afree(snapshots);
    scene->runtime_snapshot_valid = true;
    return 0;
fail:
    free_behavior_snapshots(snapshots);
    snprintf(scene->message, sizeof scene->message, "Could not capture authored Scene state.");
    return -1;
}

static int restart_runtime_actor(app_scene_authoring *scene, scene_actor *actor)
{
    assert(actor->runtime_snapshot);
    const game_prototype_spawn_hooks hooks = { attach_prototype_behavior, scene };
    char error[256];
    ecs_entity entity = { 0 };
    if (game_prototype_spawn(actor->runtime_snapshot, scene->world, &hooks, &entity, error, sizeof error) !=
        ECS_OK) {
        return -1;
    }
    if (ecs_entity_is_alive(scene->world, actor->entity)) {
        game_behavior_destroy_entity(scene->live->behaviors, actor->entity);
    }
    actor->entity = entity;
    return 0;
}

static int restart_runtime(app_scene_authoring *scene)
{
    finish_transform_edit(scene, false);
    if (capture_runtime_snapshot(scene) != 0) {
        return -1;
    }
    scene_history_clear(scene->history);
    game_behavior_set_deferred(scene->live->behaviors, true);
    scene->runtime_selected = (ecs_entity){ 0 };
    game_contacts_reset(scene->live->contacts);
    game_entities_reset(scene->live->entities);
    game_particles_clear(scene->live->particles);
    for (int i = 0; i < acount(scene->actors); i++) {
        if (restart_runtime_actor(scene, &scene->actors[i]) != 0) {
            snprintf(scene->message, sizeof scene->message, "Could not restart Scene Runtime.");
            return -1;
        }
    }
    if (scene->selected >= 0 && scene->selected < acount(scene->actors)) {
        select_instance(scene, scene->selected);
    }
    return 0;
}

app_scene_authoring *app_scene_authoring_new(
    ecs_world *world,
    game_live_world *live,
    game_live_definition *definition,
    const behavior_asset *editor_working,
    editor *graph_editor,
    app_scene_prototype_resolve_fn resolve_prototype,
    void *resolve_user,
    engine *runtime
)
{
    if (!world || !live || !definition || !editor_working || !runtime) {
        return NULL;
    }
    app_scene_authoring *scene = (app_scene_authoring *)xcalloc(1, sizeof *scene);
    scene->shapes = app_sdf_shapes_init();
    scene->renderer = app_scene_renderer_new();
    scene->history = scene_history_new();
    scene->world = world;
    scene->live = live;
    game_behavior_set_deferred(live->behaviors, true);
    scene->definition = definition;
    scene->editor_working = editor_working;
    scene->graph_editor = graph_editor;
    scene->editor_revision_seen = editor_edit_revision(graph_editor);
    scene->editor_behavior_asset_seen = editor_working;
    scene->editor_definition_seen = definition;
    scene->editor_state_seen = true;
    scene->resolve_prototype = resolve_prototype;
    scene->resolve_user = resolve_user;
    scene->registry = app_component_registry();
    scene->runtime = runtime;
    scene->prototype_preview = game_prototype_preview_new(&scene->registry);
    if (!scene->prototype_preview) {
        scene_history_free(scene->history);
        free(scene);
        return NULL;
    }
    scene->content_root = xstrdup("content");
    content_refresh(scene);
    scene->selected = -1;
    scene->selected_prototype = -1;
    scene->selected_attachment = -1;
    set_document(scene, APP_EDITOR_DOCUMENT_SCENE, APP_SCENE_DOCUMENT_ID);
    editor_ui_set_command_handler(app_ui_command, scene);
    editor_set_save_handler(graph_editor, app_editor_save, scene);
    editor_set_value_formatter(graph_editor, format_entity_value, scene);
    editor_set_flags_resolver(graph_editor, resolve_pin_flags, scene);
    editor_set_color_resolver(graph_editor, resolve_pin_color, scene);
    editor_set_choices_resolver(graph_editor, resolve_pin_choices, scene);
    engine_set_playing(runtime, false);
    scene->runtime_playing = false;
    scene->runtime_step = false;
    scene->preview_playing = false;
    scene->preview_step = false;
    scene->active_runtime_target = APP_RUNTIME_SCENE;
    scene->scene_dirty = false;
    snprintf(scene->message, sizeof scene->message, "No scene entities yet.");
    snprintf(scene->prototype_message, sizeof scene->prototype_message, "No Selection");
    return scene;
}

void app_scene_authoring_set_editor_working(app_scene_authoring *scene, const behavior_asset *editor_working)
{
    if (scene && editor_working) {
        scene->editor_working = editor_working;
    }
}

static void queue_behavior_apply(app_scene_authoring *scene)
{
    scene->editor_working = editor_behavior_asset(scene->graph_editor);
    unsigned revision = editor_edit_revision(scene->graph_editor);
    if (scene->editor_state_seen && scene->editor_definition_seen == scene->definition &&
        scene->editor_behavior_asset_seen == scene->editor_working &&
        scene->editor_revision_seen == revision) {
        return;
    }
    scene->editor_state_seen = true;
    scene->editor_definition_seen = scene->definition;
    scene->editor_behavior_asset_seen = scene->editor_working;
    scene->editor_revision_seen = revision;
    scene->behavior_apply_pending = true;
}

static int sync_behavior_working(app_scene_authoring *scene)
{
    queue_behavior_apply(scene);
    if (!scene->behavior_apply_pending) {
        return 0;
    }
    if (!scene->definition || !scene->editor_working ||
        game_live_sync_working(scene->definition, scene->editor_working) != 0) {
        snprintf(scene->message, sizeof scene->message, "Could not synchronize behavior graph.");
        return -1;
    }
    scene->behavior_apply_pending = false;
    return 0;
}

static int apply_behavior_definition(app_scene_authoring *scene, game_live_definition *definition)
{
    int dirty = definition->dirty;
    int result = game_live_apply(
        scene->live,
        definition,
        scene->message,
        sizeof scene->message
    );
    if (result == 0 && dirty && definition == scene->definition) {
        editor_preview_reset(scene->graph_editor);
        editor_preview_create(scene->graph_editor);
    }
    return result;
}

static void preview_behavior_edits(app_scene_authoring *scene)
{
    if (!scene->graph_document_id ||
        scene->graph_document_kind != APP_EDITOR_DOCUMENT_BEHAVIOR) {
        editor_ui_set_preview_status("", "", false);
        return;
    }
    queue_behavior_apply(scene);
    if (scene->behavior_apply_pending) {
        if (sync_behavior_working(scene) != 0) {
            return;
        }
        scene->preview_retry = true;
    }
    if (!scene->preview_retry) {
        return;
    }
    int result = game_live_preview(
        scene->live,
        scene->definition,
        scene->preview_message,
        sizeof scene->preview_message
    );
    char prototype_message[256] = { 0 };
    int prototype_result = game_prototype_preview_update_behavior(
        scene->prototype_preview,
        scene->definition->name,
        scene->definition->working,
        false,
        prototype_message,
        sizeof prototype_message
    );
    scene->preview_retry = result == INSTANCE_UPDATE_BUSY ||
                           prototype_result == INSTANCE_UPDATE_BUSY;
    if (result >= 0 && prototype_result != INSTANCE_UPDATE_READY) {
        result = prototype_result;
        snprintf(scene->preview_message, sizeof scene->preview_message, "Prototype: %s", prototype_message);
    }
    const char *label = "";
    if (result < 0) {
        label = "Graph error";
        editor_command_apply(scene->graph_editor, (editor_command){ .kind = EDITOR_COMMAND_COMPILE });
    }
    if (result == INSTANCE_UPDATE_BUSY) {
        label = "Waiting to apply";
    }
    if (result == INSTANCE_UPDATE_RESTART) {
        label = "Restart to apply";
    }
    editor_ui_set_preview_status(label, scene->preview_message, result == INSTANCE_UPDATE_RESTART);
    sync_editor_inspection(scene);
}

int app_scene_authoring_apply_pending(app_scene_authoring *scene)
{
    if (!scene || sync_behavior_working(scene) != 0 || !scene->definition) {
        return -1;
    }
    int result = scene->definition->dirty
                   ? apply_behavior_definition(scene, scene->definition)
                   : 0;
    if (result == 0) {
        result = game_prototype_preview_update_behavior(
            scene->prototype_preview,
            scene->definition->name,
            scene->definition->working,
            true,
            scene->preview_message,
            sizeof scene->preview_message
        );
    }
    sync_editor_inspection(scene);
    return result;
}

int app_scene_authoring_set_content_root(app_scene_authoring *scene, const char *path)
{
    if (!scene || !path || !path[0]) {
        return -1;
    }
    char *copy = xstrdup(path);
    free(scene->content_root);
    scene->content_root = copy;
    free(scene->content_selected_path);
    scene->content_selected_path = NULL;
    scene->asset_search[0] = '\0';
    content_refresh(scene);
    return 0;
}

void app_scene_authoring_refresh_content(app_scene_authoring *scene)
{
    if (scene) {
        content_refresh(scene);
    }
}

static int append_prototype_asset(app_scene_authoring *scene, game_prototype *prototype, const char *path)
{
    if (!scene || !prototype || !prototype->name ||
        find_prototype(scene, prototype->name)) {
        game_prototype_free(prototype);
        return -1;
    }
    scene_prototype_asset asset = {
        .path = path ? xstrdup(path) : NULL,
        .prototype = prototype,
        .dirty = path ? 0 : 1,
    };
    apush(scene->prototypes, asset);
    if (scene->selected_prototype < 0) {
        select_prototype(scene, 0);
    }
    return 0;
}

int app_scene_authoring_register_prototype(app_scene_authoring *scene, const char *json, const char *path)
{
    if (!scene || !json) {
        return -1;
    }
    char error[256];
    game_prototype *prototype = game_prototype_load(json, &scene->registry, error, sizeof error);
    return append_prototype_asset(scene, prototype, path);
}

int app_scene_authoring_save_prototype(
    app_scene_authoring *scene,
    const char *prototype_name,
    const char *path
)
{
    scene_prototype_asset *asset = find_prototype(scene, prototype_name);
    if (!asset || !asset->prototype || !path) {
        return -1;
    }
    char *json = game_prototype_save(asset->prototype);
    if (!json) {
        return -1;
    }
    int result = write_text_file_atomic(path, json);
    free(json);
    if (result == 0) {
        char *saved_path = xstrdup(path);
        free(asset->path);
        asset->path = saved_path;
        asset->dirty = 0;
        content_refresh(scene);
    }
    return result;
}

int app_scene_authoring_seed_default(app_scene_authoring *scene)
{
    if (!scene) {
        return -1;
    }
    char path[CONTENT_PATH_CAP];
    snprintf(path, sizeof path, "%s/scenes/NeonSwarm.json", scene->content_root);
    return app_scene_authoring_load(scene, path);
}

int app_scene_authoring_attach_prototype_behavior(
    app_scene_authoring *scene,
    const char *prototype_name,
    const char *asset_name
)
{
    if (!scene || !prototype_name || !asset_name) {
        return -1;
    }
    scene_prototype_asset *asset = find_prototype(scene, prototype_name);
    game_behavior_world *behaviors = scene->live->behaviors;
    if (!asset || !asset->prototype) {
        snprintf(scene->prototype_message, sizeof scene->prototype_message, "Prototype not found.");
        return -1;
    }
    if (!game_behavior_asset_registered(behaviors, asset_name)) {
        snprintf(
            scene->prototype_message,
            sizeof scene->prototype_message,
            "Behavior asset %s is not registered.",
            asset_name
        );
        return -1;
    }
    if (game_prototype_has_graph(asset->prototype, asset_name)) {
        snprintf(
            scene->prototype_message,
            sizeof scene->prototype_message,
            "%s is already attached to %s.",
            asset_name,
            prototype_name
        );
        return 0;
    }
    if (scene->runtime_snapshot_valid &&
        app_scene_authoring_runtime_command(scene, APP_RUNTIME_SCENE, APP_RUNTIME_STOP) !=
            0) {
        return -1;
    }
    prototype_history_before(scene, asset->prototype);
    if (game_prototype_add_graph(asset->prototype, asset_name) != 0) {
        snprintf(
            scene->prototype_message,
            sizeof scene->prototype_message,
            "Could not attach %s to %s.",
            asset_name,
            prototype_name
        );
        return -1;
    }

    scene_actor **actors = NULL;
    game_prototype **snapshots = NULL;
    scene_actor **attached = NULL;
    char error[256];
    for (int i = 0; i < acount(scene->actors); i++) {
        scene_actor *actor = &scene->actors[i];
        if (strcmp(actor->prototype_name, prototype_name) != 0) {
            continue;
        }
        game_prototype *snapshot = game_prototype_clone(asset->prototype, error, sizeof error);
        if (!snapshot) {
            snprintf(
                scene->prototype_message,
                sizeof scene->prototype_message,
                "Could not update %s: %s",
                prototype_name,
                error
            );
            free_behavior_snapshots(snapshots);
            game_prototype_remove_graph(asset->prototype, asset_name);
            afree(actors);
            afree(attached);
            return -1;
        }
        apush(actors, actor);
        apush(snapshots, snapshot);
    }
    for (int i = 0; i < acount(actors); i++) {
        if (actor_has_behavior(behaviors, actors[i], asset_name)) {
            continue;
        }
        if (game_behavior_attach(behaviors, actors[i]->entity, asset_name) != 0) {
            snprintf(
                scene->prototype_message,
                sizeof scene->prototype_message,
                "Could not attach %s to spawned actors.",
                asset_name
            );
            for (int j = 0; j < acount(attached); j++) {
                game_behavior_detach_asset(behaviors, attached[j]->entity, asset_name);
            }
            free_behavior_snapshots(snapshots);
            game_prototype_remove_graph(asset->prototype, asset_name);
            afree(actors);
            afree(attached);
            return -1;
        }
        apush(attached, actors[i]);
    }
    for (int i = 0; i < acount(actors); i++) {
        game_prototype_free(actors[i]->base);
        actors[i]->base = snapshots[i];
        snapshots[i] = NULL;
    }
    afree(actors);
    afree(attached);
    afree(snapshots);
    asset->dirty = 1;
    snprintf(scene->prototype_message, sizeof scene->prototype_message, "Attached %s to %s.", asset_name, prototype_name);
    return 0;
}

int app_scene_authoring_preview_prototype(app_scene_authoring *scene, const char *prototype_name)
{
    if (!scene) {
        return -1;
    }
    scene_prototype_asset *asset = find_prototype(scene, prototype_name);
    if (!asset || !asset->prototype) {
        return -1;
    }
    char error[256];
    finish_transform_edit(scene, true);
    app_scene_authoring_stop_prototype_preview(scene);
    for (int i = 0; i < game_live_definition_count(scene->live); i++) {
        game_live_definition *definition = game_live_definition_at(scene->live, i);
        if (game_prototype_preview_update_behavior(
                scene->prototype_preview,
                definition->name,
                definition->applied,
                true,
                error,
                sizeof error
            ) != 0) {
            snprintf(scene->prototype_message, sizeof scene->prototype_message, "%s", error);
            return -1;
        }
    }
    int result = game_prototype_preview_launch(scene->prototype_preview, asset->prototype, error, sizeof error);
    if (result == ECS_OK) {
        editor_ui_set_scene_play_expanded(false);
        scene->active_runtime_target = APP_RUNTIME_PREVIEW;
        scene->preview_prototype_id = xstrdup(game_prototype_identity(asset->prototype));
    }
    scene->preview_playing = result == ECS_OK;
    scene->preview_step = false;
    snprintf(
        scene->prototype_message,
        sizeof scene->prototype_message,
        result == ECS_OK ? "Previewing %s." : "Could not preview %s: %s.",
        prototype_name,
        result == ECS_OK ? "" : error
    );
    return result;
}

void app_scene_authoring_stop_prototype_preview(app_scene_authoring *scene)
{
    if (scene) {
        game_prototype_preview_stop(scene->prototype_preview);
        free(scene->preview_prototype_id);
        scene->preview_prototype_id = NULL;
        scene->preview_playing = false;
        scene->preview_step = false;
        scene->active_runtime_target = APP_RUNTIME_SCENE;
        snprintf(scene->prototype_message, sizeof scene->prototype_message, "Prototype preview stopped.");
    }
}

int app_scene_authoring_runtime_command(
    app_scene_authoring *scene,
    app_runtime_target target,
    app_runtime_command command
)
{
    if (!scene || target < APP_RUNTIME_PREVIEW || target > APP_RUNTIME_SCENE ||
        command < APP_RUNTIME_PLAY || command > APP_RUNTIME_STOP) {
        return -1;
    }
    if (command == APP_RUNTIME_RESTART || command == APP_RUNTIME_STOP) {
        app_scene_renderer_reset(scene->renderer);
    }
    bool *playing = target == APP_RUNTIME_PREVIEW ? &scene->preview_playing
                                                  : &scene->runtime_playing;
    finish_transform_edit(scene, false);
    bool *step = target == APP_RUNTIME_PREVIEW ? &scene->preview_step
                                               : &scene->runtime_step;
    if (target == APP_RUNTIME_SCENE) {
        scene->active_runtime_target = APP_RUNTIME_SCENE;
        if (command == APP_RUNTIME_PLAY || command == APP_RUNTIME_STEP) {
            if (capture_runtime_snapshot(scene) != 0) {
                return -1;
            }
            game_behavior_set_deferred(scene->live->behaviors, false);
            if (game_behavior_begin_pending(scene->live->behaviors) != 0) {
                *playing = false;
                *step = false;
                engine_set_playing(scene->runtime, false);
                snprintf(scene->message, sizeof scene->message, "Scene Begin failed. Stop to return to editing.");
                return -1;
            }
        }
        if (command == APP_RUNTIME_PLAY || command == APP_RUNTIME_PAUSE) {
            *playing = command == APP_RUNTIME_PLAY;
            *step = false;
            int result = engine_set_playing(scene->runtime, *playing);
            if (result == 0 && *playing) {
                editor_ui_set_scene_play_expanded(true);
            }
            return result;
        }
        if (command == APP_RUNTIME_STEP) {
            *playing = false;
            *step = true;
            return engine_set_playing(scene->runtime, false) == 0
                     ? engine_step(scene->runtime)
                     : -1;
        }
        *playing = false;
        *step = false;
        if (engine_set_playing(scene->runtime, false) != 0) {
            return -1;
        }
        int result = restart_runtime(scene);
        if (result != 0) {
            return result;
        }
        if (command == APP_RUNTIME_STOP) {
            invalidate_runtime_snapshot(scene);
            editor_ui_set_scene_play_expanded(false);
            return 0;
        }
        game_behavior_set_deferred(scene->live->behaviors, false);
        if (game_behavior_begin_pending(scene->live->behaviors) != 0) {
            snprintf(scene->message, sizeof scene->message, "Scene Begin failed. Stop to return to editing.");
            return -1;
        }
        *playing = true;
        result = engine_set_playing(scene->runtime, true);
        if (result == 0) {
            editor_ui_set_scene_play_expanded(true);
        }
        return result;
    }
    if (command == APP_RUNTIME_PLAY || command == APP_RUNTIME_PAUSE) {
        editor_ui_set_scene_play_expanded(false);
        scene->active_runtime_target = APP_RUNTIME_PREVIEW;
        *playing = command == APP_RUNTIME_PLAY;
        *step = false;
        return 0;
    }
    if (command == APP_RUNTIME_STEP) {
        editor_ui_set_scene_play_expanded(false);
        scene->active_runtime_target = APP_RUNTIME_PREVIEW;
        *playing = false;
        *step = true;
        return 0;
    }
    if (command == APP_RUNTIME_STOP) {
        app_scene_authoring_stop_prototype_preview(scene);
        return 0;
    }
    scene_prototype_asset *asset = previewed_prototype(scene);
    return asset ? app_scene_authoring_preview_prototype(scene, asset->prototype->name)
                 : -1;
}

static float actor_orientation(const ecs_world *world, ecs_entity entity)
{
    const GameOrientation *orientation = GAME_GameOrientation_get_const(world, entity);
    return orientation ? orientation->radians : 0;
}

static bool appearance_bounds(
    const GameTransform *transform,
    const GameAppearance *appearance,
    float radians,
    const GameSdfMotion *motion,
    bool effects,
    app_cf_aabb *out
)
{
    CF_V2 min, max;
    if (!isfinite(transform->x) || !isfinite(transform->y) ||
        !game_appearance_rotated_bounds(appearance, radians, &min, &max)) {
        return false;
    }
    float padding = 0;
    if (motion && !game_sdf_motion_padding(motion, &padding)) {
        return false;
    }
    if (effects) {
        if (isfinite(appearance->glow_radius) && appearance->glow_radius > 0) {
            padding += appearance->glow_radius;
        }
    }
    *out = (app_cf_aabb){
        { transform->x + min.x - padding, -transform->y + min.y - padding },
        { transform->x + max.x + padding, -transform->y + max.y + padding }
    };
    return isfinite(out->min.x) && isfinite(out->min.y) &&
           isfinite(out->max.x) && isfinite(out->max.y);
}

static void fit_scene_view(engine_viewport *viewport, ecs_world *world, ecs_entity only)
{
    ecs_component_id required[] = { GAME_COMPONENT_GameTransform.id,
                                    GAME_COMPONENT_GameAppearance.id };
    ecs_query query;
    if (ecs_query_begin(world, &query, required, 2) != ECS_OK) {
        return;
    }
    app_cf_aabb total = { 0 };
    bool found = false;
    ecs_entity entity;
    while (ecs_query_next(&query, &entity)) {
        if (only.slot &&
            (entity.slot != only.slot || entity.generation != only.generation)) {
            continue;
        }
        app_cf_aabb bounds;
        if (!appearance_bounds(
                GAME_GameTransform_get_const(world, entity),
                GAME_GameAppearance_get_const(world, entity),
                actor_orientation(world, entity),
                GAME_GameSdfMotion_get_const(world, entity),
                true,
                &bounds
            )) {
            continue;
        }
        if (!found) {
            total = bounds;
        }
        total.min.x = fminf(total.min.x, bounds.min.x);
        total.min.y = fminf(total.min.y, bounds.min.y);
        total.max.x = fmaxf(total.max.x, bounds.max.x);
        total.max.y = fmaxf(total.max.y, bounds.max.y);
        found = true;
    }
    ecs_query_end(&query);
    if (found) {
        engine_viewport_fit(
            viewport,
            total.min.x,
            total.min.y,
            total.max.x,
            total.max.y
        );
    }
}

static ecs_entity pick_scene_entity(app_scene_authoring *scene, float x, float y)
{
    ecs_component_id required[] = { GAME_COMPONENT_GameTransform.id,
                                    GAME_COMPONENT_GameAppearance.id };
    ecs_query query;
    if (ecs_query_begin(scene->world, &query, required, 2) != ECS_OK) {
        return (ecs_entity){ 0 };
    }
    ecs_entity picked = { 0 };
    ecs_entity entity;
    while (ecs_query_next(&query, &entity)) {
        const GameAppearance *appearance = GAME_GameAppearance_get_const(scene->world, entity);
        const GameTransform *transform = GAME_GameTransform_get_const(scene->world, entity);
        app_cf_aabb bounds;
        float radians = actor_orientation(scene->world, entity);
        const GameSdfMotion *motion = GAME_GameSdfMotion_get_const(scene->world, entity);
        CF_V2 local;
        GameAppearance selectable = *appearance;
        if (selectable.shape == GAME_SHAPE_STROKED_CHEVRON) {
            selectable.shape = GAME_SHAPE_CHEVRON;
        }
        if (!appearance_bounds(transform, appearance, radians, motion, false, &bounds) ||
            !game_appearance_rotate_point(
                (CF_V2){ x - transform->x, y + transform->y },
                -radians,
                &local
            ) ||
            (motion && !game_sdf_motion_warp(motion, local, &local)) ||
            !game_appearance_contains(&selectable, local)) {
            continue;
        }
        /* The renderer uses this same ECS query order; the last drawn hit
         * wins, regardless of whether it was authored or spawned at runtime. */
        if (game_entities_owns(scene->live->entities, entity)) {
            picked = entity;
            continue;
        }
        for (int i = 0; i < acount(scene->actors); i++) {
            if (ecs_entity_to_u64(scene->actors[i].entity) == ecs_entity_to_u64(entity)) {
                picked = entity;
                break;
            }
        }
    }
    ecs_query_end(&query);
    return picked;
}

static void edit_scene_pointer(app_scene_authoring *scene, engine_viewport *viewport)
{
    engine_viewport_pointer pointer;
    engine_viewport_get_pointer(viewport, &pointer);
    if (pointer.world_per_pixel > 0) {
        scene->selection_scale = pointer.world_per_pixel;
    }
    if (pointer.delete_pressed && scene_can_move(scene) &&
        !scene->runtime_snapshot_valid) {
        finish_transform_edit(scene, false);
        ecs_entity *remove = NULL;
        for (int i = 0; i < acount(scene->actors); i++) {
            if (scene->actors[i].marked || i == scene->selected) {
                apush(remove, scene->actors[i].entity);
            }
        }
        scene_history_begin_group(scene->history);
        for (int j = acount(remove) - 1; j >= 0; j--) {
            for (int i = 0; i < acount(scene->actors); i++) {
                if (ecs_entity_to_u64(scene->actors[i].entity) ==
                    ecs_entity_to_u64(remove[j])) {
                    scene->selected = i;
                    app_scene_authoring_delete_instance(scene);
                    break;
                }
            }
        }
        scene_history_end_group(scene->history);
        afree(remove);
        scene->selected = -1;
        scene->runtime_selected = (ecs_entity){ 0 };
        scene->selected_attachment = -1;
        for (int i = 0; i < acount(scene->actors); i++) {
            scene->actors[i].marked = false;
        }
        sync_editor_inspection(scene);
        return;
    }
    if (pointer.dropped_prototype[0] && scene_can_move(scene) &&
        !scene->runtime_snapshot_valid) {
        GameTransform position = { pointer.world_x, -pointer.world_y };
        if (add_instance_at(scene, pointer.dropped_prototype, &position) == 0) {
            scene->reveal_actor = (ecs_entity){ 0 };
        }
        return;
    }
    if (!scene_can_move(scene) || pointer.cancel) {
        scene->marquee = scene->marquee_visible = false;
        if (scene->dragging_actor) {
            finish_transform_edit(scene, true);
        }
        return;
    }
    if (pointer.pressed) {
        finish_transform_edit(scene, false);
        ecs_entity picked = pick_scene_entity(scene, pointer.world_x, pointer.world_y);
        set_document(scene, APP_EDITOR_DOCUMENT_SCENE, APP_SCENE_DOCUMENT_ID);
        if (!picked.slot) {
            scene->selected = -1;
            scene->runtime_selected = (ecs_entity){ 0 };
            scene->selected_attachment = -1;
            for (int i = 0; i < acount(scene->actors); i++) {
                scene->actors[i].marked = false;
            }
            scene->marquee = true;
            scene->marquee_start = (GameTransform){ pointer.world_x, pointer.world_y };
            scene->marquee_end = scene->marquee_start;
            return;
        }
        if (game_entities_owns(scene->live->entities, picked)) {
            app_scene_authoring_select_runtime_entity(scene, picked);
            return;
        }
        for (int i = 0; i < acount(scene->actors); i++) {
            if (ecs_entity_to_u64(scene->actors[i].entity) == ecs_entity_to_u64(picked)) {
                select_instance(scene, i);
                break;
            }
        }
        begin_transform_edit(scene, picked);
        scene->dragging_actor = true;
        scene->drag_origin = (GameTransform){ pointer.world_x, -pointer.world_y };
    }
    if (scene->marquee) {
        scene->marquee_end = (GameTransform){ pointer.world_x, pointer.world_y };
        scene->marquee_visible = pointer.dragging;
        if (pointer.dragging) {
            float min_x = fminf(scene->marquee_start.x, pointer.world_x);
            float max_x = fmaxf(scene->marquee_start.x, pointer.world_x);
            float min_y = fminf(scene->marquee_start.y, pointer.world_y);
            float max_y = fmaxf(scene->marquee_start.y, pointer.world_y);
            scene->selected = -1;
            for (int i = 0; i < acount(scene->actors); i++) {
                scene_actor *actor = &scene->actors[i];
                app_cf_aabb bounds;
                actor->marked =
                    GAME_GameTransform_has(scene->world, actor->entity) &&
                    GAME_GameAppearance_has(scene->world, actor->entity) &&
                    appearance_bounds(
                        GAME_GameTransform_get_const(scene->world, actor->entity),
                        GAME_GameAppearance_get_const(scene->world, actor->entity),
                        actor_orientation(scene->world, actor->entity),
                        GAME_GameSdfMotion_get_const(scene->world, actor->entity),
                        false,
                        &bounds
                    ) &&
                    bounds.max.x >= min_x && bounds.min.x <= max_x &&
                    bounds.max.y >= min_y && bounds.min.y <= max_y;
                if (actor->marked) {
                    scene->selected = i;
                }
            }
        }
        if (pointer.released) {
            scene->marquee = scene->marquee_visible = false;
        }
        return;
    }
    if (!scene->dragging_actor) {
        return;
    }
    if (pointer.dragging) {
        GameTransform position = {
            scene->edit_before.x + pointer.world_x - scene->drag_origin.x,
            scene->edit_before.y - pointer.world_y - scene->drag_origin.y,
        };
        write_position(scene, scene->edit_actor, position);
        scene->scene_dirty = true;
    }
    if (pointer.released) {
        finish_transform_edit(scene, false);
    }
}

int app_scene_authoring_draw_viewport(app_scene_authoring *scene, engine_viewport *viewport)
{
    scene->render_viewport = viewport;
    if (scene->reveal_actor.slot) {
        scene->active_runtime_target = APP_RUNTIME_SCENE;
    }
    app_runtime_target target = scene->active_runtime_target;
    scene_prototype_asset *prototype = previewed_prototype(scene);
    bool preview = target == APP_RUNTIME_PREVIEW && prototype;
    /* Prototype records are append-only; indices survive array reallocations. */
    int camera_target = preview ? (int)(prototype - scene->prototypes) + 1 : 0;
    scene_camera *camera = preview ? &prototype->camera : &scene->camera;
    if (camera_target != scene->camera_target) {
        if (camera->saved) {
            engine_viewport_set_view(viewport, &camera->view);
        } else {
            engine_viewport_reset_camera(viewport);
            if (preview) {
                fit_scene_view(
                    viewport,
                    game_prototype_preview_world(scene->prototype_preview),
                    (ecs_entity){ 0 }
                );
            }
        }
        app_scene_renderer_reset(scene->renderer);
        scene->camera_target = camera_target;
    }
    if (scene->reveal_actor.slot) {
        fit_scene_view(viewport, scene->world, scene->reveal_actor);
        scene->reveal_actor = (ecs_entity){ 0 };
    }
    char title[192];
    if (target == APP_RUNTIME_PREVIEW && prototype) {
        snprintf(title, sizeof title, "%s Preview", prototype->prototype->name);
    } else {
        snprintf(title, sizeof title, "Scene");
    }
    engine_viewport_controls controls = {
        .title = title,
        .session = target == APP_RUNTIME_PREVIEW ? prototype != NULL
                                                 : scene->runtime_snapshot_valid,
        .editing = scene_can_move(scene),
        .playing = target == APP_RUNTIME_PREVIEW ? scene->preview_playing
                                                 : scene->runtime_playing,
    };
    app_runtime_command command;
    engine_viewport_action action = engine_viewport_draw(viewport, &controls);
    edit_scene_pointer(scene, viewport);
    if (action == ENGINE_VIEWPORT_FIT) {
        fit_scene_view(
            viewport,
            preview ? game_prototype_preview_world(scene->prototype_preview)
                    : scene->world,
            (ecs_entity){ 0 }
        );
    }
    engine_viewport_get_view(viewport, &camera->view);
    camera->saved = true;
    switch (action) {
        case ENGINE_VIEWPORT_NONE: return 0;
        case ENGINE_VIEWPORT_FIT: return 0;
        case ENGINE_VIEWPORT_PLAY: command = APP_RUNTIME_PLAY; break;
        case ENGINE_VIEWPORT_PAUSE: command = APP_RUNTIME_PAUSE; break;
        case ENGINE_VIEWPORT_STEP: command = APP_RUNTIME_STEP; break;
        case ENGINE_VIEWPORT_RESTART: command = APP_RUNTIME_RESTART; break;
        case ENGINE_VIEWPORT_STOP: command = APP_RUNTIME_STOP; break;
        default: return -1;
    }
    return app_scene_authoring_runtime_command(scene, target, command);
}

app_runtime_state app_scene_authoring_runtime_state(const app_scene_authoring *scene)
{
    app_runtime_state state = { 0 };
    if (!scene) {
        return state;
    }
    state.target = scene->active_runtime_target;
    state.playing = state.target == APP_RUNTIME_PREVIEW ? scene->preview_playing
                                                        : scene->runtime_playing;
    ecs_world *world = NULL;
    game_behavior_world *behaviors = NULL;
    if (state.target == APP_RUNTIME_PREVIEW) {
        world = game_prototype_preview_world(scene->prototype_preview);
        behaviors = game_prototype_preview_behaviors(scene->prototype_preview);
        state.selected_entity = game_prototype_preview_entity(scene->prototype_preview);
        state.message = scene->prototype_message;
    } else {
        world = scene->world;
        behaviors = scene->live ? scene->live->behaviors : NULL;
        state.selected_entity = selected_scene_entity(scene);
        state.message = scene->message;
    }
    if (world && state.selected_entity.slot) {
        for (int i = 0; i < ecs_world_component_count(world); i++) {
            const ecs_component_desc *desc = ecs_world_component_at(world, i);
            if (desc && ecs_component_has(world, state.selected_entity, desc->id)) {
                state.component_count++;
            }
        }
    }
    if (behaviors && state.selected_entity.slot) {
        state.behavior_count = game_behavior_attachment_count_for_entity(
            behaviors,
            state.selected_entity
        );
    }
    return state;
}

int app_scene_authoring_detach_prototype_behavior(
    app_scene_authoring *scene,
    const char *prototype_name,
    const char *asset_name
)
{
    if (!scene || !prototype_name || !asset_name) {
        return -1;
    }
    scene_prototype_asset *asset = find_prototype(scene, prototype_name);
    if (!asset || !game_prototype_has_graph(asset->prototype, asset_name)) {
        return -1;
    }
    if (scene->runtime_snapshot_valid &&
        app_scene_authoring_runtime_command(scene, APP_RUNTIME_SCENE, APP_RUNTIME_STOP) !=
            0) {
        return -1;
    }
    prototype_history_before(scene, asset->prototype);
    if (game_prototype_remove_graph(asset->prototype, asset_name) != 0) {
        return -1;
    }
    game_behavior_world *behaviors = scene->live->behaviors;
    char error[256];
    for (int i = 0; i < acount(scene->actors); i++) {
        scene_actor *actor = &scene->actors[i];
        if (strcmp(actor->prototype_name, prototype_name) != 0) {
            continue;
        }
        game_behavior_detach_asset(behaviors, actor->entity, asset_name);
        game_prototype *base = game_prototype_clone(asset->prototype, error, sizeof error);
        if (!base) {
            snprintf(
                scene->prototype_message,
                sizeof scene->prototype_message,
                "Could not update %s: %s",
                actor->name,
                error
            );
            return -1;
        }
        game_prototype_free(actor->base);
        actor->base = base;
    }
    asset->dirty = 1;
    snprintf(scene->prototype_message, sizeof scene->prototype_message, "Detached %s from %s.", asset_name, prototype_name);
    return 0;
}

int app_scene_authoring_save(app_scene_authoring *scene, const char *path)
{
    if (!scene || !path) {
        return -1;
    }
    finish_transform_edit(scene, false);
    game_scene_document *document = game_scene_document_new("Scene");
    if (!document) {
        return -1;
    }
    game_behavior_world *behaviors = scene->live->behaviors;
    for (int i = 0; i < acount(scene->actors); i++) {
        const scene_actor *actor = &scene->actors[i];
        const game_prototype *snapshot = scene->runtime_snapshot_valid
                                           ? actor->runtime_snapshot
                                           : NULL;
        const char **names = NULL;
        if (snapshot) {
            for (int j = 0; j < acount(snapshot->graphs); j++) {
                apush(names, snapshot->graphs[j]);
            }
        }
        int count = snapshot ? 0
                             : game_behavior_attachment_count_for_entity(
                                   behaviors,
                                   actor->entity
                               );
        for (int j = 0; j < count; j++) {
            game_behavior_attachment_info info;
            if (game_behavior_attachment_info_at(behaviors, actor->entity, j, &info) != 0) {
                afree(names);
                game_scene_document_free(document);
                return -1;
            }
            apush(names, info.asset_name);
        }
        int result = game_scene_document_add_entity(
            document,
            actor->name,
            actor->prototype_name,
            names,
            acount(names)
        );
        if (result == 0) {
            char *components = snapshot
                                 ? game_prototype_overrides_json(actor->base, snapshot)
                                 : game_prototype_capture_overrides_json(
                                       actor->base,
                                       scene->world,
                                       actor->entity
                                   );
            result = game_scene_document_set_entity_components(document, i, components);
            free(components);
        }
        afree(names);
        if (result != 0) {
            game_scene_document_free(document);
            return -1;
        }
    }
    char *json = game_scene_document_save(document);
    game_scene_document_free(document);
    if (!json) {
        return -1;
    }
    int result = write_text_file_atomic(path, json);
    free(json);
    if (result == 0) {
        char *saved_path = xstrdup(path);
        free(scene->scene_path);
        scene->scene_path = saved_path;
        scene->scene_dirty = false;
    }
    return result;
}

static char *read_scene_file(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (!file || fseek(file, 0, SEEK_END) || ftell(file) < 0) {
        if (file) {
            fclose(file);
        }
        return NULL;
    }
    long size = ftell(file);
    if (fseek(file, 0, SEEK_SET)) {
        fclose(file);
        return NULL;
    }
    char *json = (char *)malloc((size_t)size + 1);
    if (!json) {
        fclose(file);
        return NULL;
    }
    size_t read = fread(json, 1, (size_t)size, file);
    fclose(file);
    json[read] = '\0';
    return json;
}

int app_scene_authoring_load_prototype(app_scene_authoring *scene, const char *path)
{
    if (!scene || !path) {
        return -1;
    }
    char *json = read_scene_file(path);
    if (!json) {
        return -1;
    }
    int result = app_scene_authoring_register_prototype(scene, json, path);
    free(json);
    return result;
}

static int load_content_behavior(app_scene_authoring *scene, const char *path)
{
    char *json = read_scene_file(path);
    if (!json) {
        return 0;
    }
    behavior_asset *working = behavior_asset_load_json(json);
    free(json);
    if (!working || !working->name ||
        game_live_find_definition(scene->live, working->name)) {
        behavior_asset_free(working);
        return 0;
    }
    game_live_definition *definition = game_live_definition_new(working->name, working);
    if (!definition) {
        behavior_asset_free(working);
        return 0;
    }
    if (game_live_definition_set_path(definition, path) != 0) {
        game_live_definition_free(definition);
        return 0;
    }
    if (game_live_add_definition(scene->live, definition) != 0) {
        game_live_definition_free(definition);
        return 0;
    }
    if (game_prototype_preview_register_behavior(
            scene->prototype_preview,
            working->name,
            working
        ) != 0) {
        game_live_remove_definition(scene->live, working->name);
        return 0;
    }
    return 1;
}

static int load_content_prototype(app_scene_authoring *scene, const char *path)
{
    char *json = read_scene_file(path);
    if (!json) {
        return 0;
    }
    int result = app_scene_authoring_register_prototype(scene, json, path) == 0;
    free(json);
    return result;
}

int app_scene_authoring_load_content(app_scene_authoring *scene)
{
    if (!scene) {
        return -1;
    }
    content_refresh(scene);
    int loaded = app_behavior_fixtures_load(
        scene->live,
        scene->prototype_preview,
        scene->content_root
    );
    if (loaded < 0) {
        loaded = 0;
    }
    for (int i = 0; i < acount(scene->content); i++) {
        if (!scene->content[i].directory &&
            content_path_in_folder(scene, scene->content[i].path, "prototypes")) {
            loaded += load_content_prototype(scene, scene->content[i].path);
        }
    }
    return loaded ? 0 : -1;
}

static scene_prototype_asset *ensure_prototype(app_scene_authoring *scene, const char *name)
{
    scene_prototype_asset *asset = find_prototype(scene, name);
    if (asset || !scene || !scene->resolve_prototype) {
        return asset;
    }
    const char *json = scene->resolve_prototype(name, scene->resolve_user);
    if (!json || app_scene_authoring_register_prototype(scene, json, NULL) != 0) {
        return NULL;
    }
    return find_prototype(scene, name);
}

static int apply_components_json(app_scene_authoring *scene, ecs_entity entity, const char *components_json)
{
    if (!components_json || !components_json[0]) {
        return 0;
    }
    char error[256];
    game_prototype_registry registry = app_component_registry();
    if (game_prototype_apply_components_json(scene->world, entity, components_json, &registry, error, sizeof error) !=
        0) {
        snprintf(scene->message, sizeof scene->message, "Could not apply overrides: %s", error);
        return -1;
    }
    return 0;
}

static int validate_scene_behaviors(app_scene_authoring *scene, char **names)
{
    game_behavior_world *world = scene->live->behaviors;
    for (int i = 0; i < acount(names); i++) {
        const behavior_asset *definition = NULL;
        for (int j = 0; j < game_behavior_asset_count(world); j++) {
            if (strcmp(names[i], game_behavior_asset_name_at(world, j)) == 0) {
                definition = game_behavior_asset_definition_at(world, j);
                break;
            }
        }
        if (!definition) {
            snprintf(scene->message, sizeof scene->message, "Unknown behavior asset %s.", names[i]);
            return -1;
        }
        if (definition->owner_type_name &&
            strcmp(definition->owner_type_name, "ecs.entity") != 0) {
            snprintf(
                scene->message,
                sizeof scene->message,
                "Behavior %s cannot attach to a scene entity.",
                names[i]
            );
            return -1;
        }
    }
    return 0;
}

static int validate_scene_entity(app_scene_authoring *scene, const game_scene_entity *entity)
{
    scene_prototype_asset *asset = ensure_prototype(scene, entity->prototype);
    if (!asset) {
        snprintf(
            scene->message,
            sizeof scene->message,
            "Unknown scene prototype %s.",
            entity->prototype
        );
        return -1;
    }
    if (validate_scene_behaviors(scene, asset->prototype->graphs) != 0 ||
        validate_scene_behaviors(scene, entity->behaviors) != 0) {
        return -1;
    }
    /* Exercise the same component parser without mutating the open Scene. */
    ecs_world *world = ecs_world_new();
    int result = 0;
    char error[256] = { 0 };
    for (int i = 0; i < scene->registry.component_count; i++) {
        if (ecs_world_register_component(world, scene->registry.components[i], NULL) !=
            ECS_OK) {
            snprintf(error, sizeof error, "could not register scene components");
            result = -1;
            break;
        }
    }
    ecs_entity actor;
    if (result == 0) {
        result = game_prototype_spawn_components(asset->prototype, world, &actor, error, sizeof error);
    }
    if (result == 0 && entity->components_json && entity->components_json[0]) {
        result = game_prototype_apply_components_json(
            world,
            actor,
            entity->components_json,
            &scene->registry,
            error,
            sizeof error
        );
    }
    ecs_world_free(world);
    if (result != 0) {
        snprintf(
            scene->message,
            sizeof scene->message,
            "Could not load scene entity %s: %s",
            entity->name,
            error
        );
    }
    return result;
}

int app_scene_authoring_load(app_scene_authoring *scene, const char *path)
{
    if (!scene || !path) {
        return -1;
    }
    char *json = read_scene_file(path);
    if (!json) {
        snprintf(scene->message, sizeof scene->message, "Could not read %s.", path);
        return -1;
    }
    char error[256];
    game_scene_document *document = game_scene_document_load(json, error, sizeof error);
    free(json);
    if (!document) {
        snprintf(scene->message, sizeof scene->message, "Could not load scene: %s", error);
        return -1;
    }
    game_behavior_world *behaviors = scene->live->behaviors;
    for (int i = 0; i < acount(document->entities); i++) {
        if (validate_scene_entity(scene, &document->entities[i]) != 0) {
            game_scene_document_free(document);
            return -1;
        }
    }
    engine_set_playing(scene->runtime, false);
    game_behavior_set_deferred(scene->live->behaviors, true);
    scene->runtime_playing = false;
    scene->runtime_step = false;
    editor_ui_set_scene_play_expanded(false);
    app_scene_authoring_stop_prototype_preview(scene);
    invalidate_runtime_snapshot(scene);
    game_entities_reset(scene->live->entities);
    game_contacts_reset(scene->live->contacts);
    game_particles_clear(scene->live->particles);
    app_scene_renderer_reset(scene->renderer);
    while (acount(scene->actors)) {
        scene->selected = acount(scene->actors) - 1;
        remove_selected(scene);
    }
    for (int i = 0; i < acount(document->entities); i++) {
        game_scene_entity *entity = &document->entities[i];
        if (add_actor(scene, entity->name, entity->prototype) != 0) {
            game_scene_document_free(document);
            return -1;
        }
        scene_actor *actor = selected_actor(scene);
        if (apply_components_json(scene, actor->entity, entity->components_json) != 0) {
            game_scene_document_free(document);
            return -1;
        }
        game_behavior_detach_entity(behaviors, actor->entity);
        for (int j = 0; j < acount(entity->behaviors); j++) {
            if (game_behavior_attach(behaviors, actor->entity, entity->behaviors[j]) != 0) {
                game_scene_document_free(document);
                snprintf(scene->message, sizeof scene->message, "Could not attach scene behavior.");
                return -1;
            }
        }
    }
    game_scene_document_free(document);
    char *saved_path = xstrdup(path);
    free(scene->scene_path);
    scene->scene_path = saved_path;
    scene->scene_dirty = false;
    snprintf(
        scene->message,
        sizeof scene->message,
        "Loaded scene from %s.",
        scene->scene_path
    );
    return 0;
}

const char *app_scene_authoring_message(const app_scene_authoring *scene)
{
    return scene ? scene->message : NULL;
}

const char *app_scene_authoring_selected_content(const app_scene_authoring *scene)
{
    return scene ? scene->content_selected_path : NULL;
}

app_editor_document_context app_scene_authoring_document(const app_scene_authoring *scene)
{
    return scene ? scene->document
                 : (app_editor_document_context){ APP_EDITOR_DOCUMENT_SCENE, NULL };
}

void app_scene_authoring_free(app_scene_authoring *scene)
{
    if (!scene) {
        return;
    }
    app_scene_renderer_free(scene->renderer);
    finish_transform_edit(scene, true);
    editor_set_inspection_instance(scene->graph_editor, NULL);
    editor_set_inspection_run(scene->graph_editor, NULL);
    editor_ui_set_command_handler(NULL, NULL);
    editor_set_save_handler(scene->graph_editor, NULL, NULL);
    editor_set_value_formatter(scene->graph_editor, NULL, NULL);
    editor_set_flags_resolver(scene->graph_editor, NULL, NULL);
    editor_set_color_resolver(scene->graph_editor, NULL, NULL);
    editor_set_choices_resolver(scene->graph_editor, NULL, NULL);
    while (acount(scene->actors)) {
        scene->selected = acount(scene->actors) - 1;
        remove_selected(scene);
    }
    afree(scene->actors);
    afree(scene->runtime_entities);
    scene_history_free(scene->history);
    for (int i = 0; i < acount(scene->prototypes); i++) {
        free(scene->prototypes[i].path);
        game_prototype_free(scene->prototypes[i].prototype);
    }
    afree(scene->prototypes);
    clear_prototype_history(scene);
    game_prototype_preview_free(scene->prototype_preview);
    free(scene->preview_prototype_id);
    for (int i = 0; i < acount(scene->content); i++) {
        free(scene->content[i].path);
    }
    afree(scene->content);
    free(scene->content_root);
    free(scene->scene_path);
    game_system_document_free(scene->system_document);
    free(scene->system_path);
    free(scene->content_selected_path);
    free(scene->document_id);
    free(scene->graph_document_id);
    free(scene);
}

static void duplicate_current_behavior(app_scene_authoring *scene)
{
    for (int i = 1; i < 1000; i++) {
        char name[64];
        snprintf(name, sizeof name, "SceneBehaviorCopy%d", i);
        if (game_behavior_asset_registered(scene->live->behaviors, name)) {
            continue;
        }
        if (game_live_duplicate_definition(scene->live, name, scene->editor_working) == 0) {
            game_live_mark_dirty(game_live_find_definition(scene->live, name));
            snprintf(scene->message, sizeof scene->message, "Created %s.", name);
        } else {
            snprintf(scene->message, sizeof scene->message, "Could not create %s.", name);
        }
        return;
    }
    snprintf(scene->message, sizeof scene->message, "Behavior asset name space is full.");
}

static int open_behavior_asset(app_scene_authoring *scene, const char *asset_name);

static int create_new_prototype(app_scene_authoring *scene)
{
    for (int i = 1; i < 1000; i++) {
        char name[64];
        char json[512];
        snprintf(name, sizeof name, "NewPrototype%d", i);
        if (find_prototype(scene, name)) {
            continue;
        }
        snprintf(
            json,
            sizeof json,
            "{\"name\":\"%s\",\"components\":{"
            "\"GameHealth\":{\"current\":100,\"maximum\":100},"
            "\"GameTransform\":{\"x\":0,\"y\":0}}}",
            name
        );
        if (app_scene_authoring_register_prototype(scene, json, NULL) != 0) {
            break;
        }
        open_prototype(scene, acount(scene->prototypes) - 1);
        snprintf(scene->message, sizeof scene->message, "Created %s.", name);
        return 0;
    }
    snprintf(scene->message, sizeof scene->message, "Prototype name space is full.");
    return -1;
}

static int duplicate_selected_prototype(app_scene_authoring *scene)
{
    scene_prototype_asset *source = selected_prototype(scene);
    if (!source) {
        snprintf(scene->message, sizeof scene->message, "Select a prototype to duplicate.");
        return -1;
    }
    char error[256];
    for (int i = 1; i < 1000; i++) {
        char name[64];
        snprintf(name, sizeof name, "%sCopy%d", source->prototype->name, i);
        if (find_prototype(scene, name)) {
            continue;
        }
        game_prototype *copy = game_prototype_clone(source->prototype, error, sizeof error);
        if (!copy) {
            snprintf(scene->message, sizeof scene->message, "Could not duplicate prototype: %s", error);
            return -1;
        }
        if (game_prototype_set_name(copy, name) != 0) {
            game_prototype_free(copy);
            snprintf(scene->message, sizeof scene->message, "Could not duplicate prototype.");
            return -1;
        }
        if (append_prototype_asset(scene, copy, NULL) != 0) {
            snprintf(scene->message, sizeof scene->message, "Could not duplicate prototype.");
            return -1;
        }
        select_prototype(scene, acount(scene->prototypes) - 1);
        snprintf(scene->message, sizeof scene->message, "Created %s.", name);
        return 0;
    }
    snprintf(scene->message, sizeof scene->message, "Prototype name space is full.");
    return -1;
}

static int create_behavior_asset(app_scene_authoring *scene)
{
    game_behavior_world *behaviors = scene->live->behaviors;
    for (int i = 1; i < 1000; i++) {
        char name[64];
        snprintf(name, sizeof name, "NewBehavior%d", i);
        if (game_behavior_asset_registered(behaviors, name)) {
            continue;
        }
        behavior_asset *working = behavior_asset_new(name);
        if (!working || behavior_asset_set_owner_type(working, "ecs.entity") != 0) {
            behavior_asset_free(working);
            break;
        }
        game_live_definition *definition = game_live_definition_new(name, working);
        if (!definition || game_live_add_definition(scene->live, definition) != 0) {
            if (definition) {
                game_live_definition_free(definition);
            } else {
                behavior_asset_free(working);
            }
            break;
        }
        game_live_mark_dirty(definition);
        if (open_behavior_asset(scene, name) != 0) {
            snprintf(scene->message, sizeof scene->message, "Created %s, but could not open it.", name);
            return -1;
        }
        snprintf(scene->message, sizeof scene->message, "Created %s.", name);
        return 0;
    }
    snprintf(scene->message, sizeof scene->message, "Behavior asset name space is full.");
    return -1;
}

static int sync_open_behavior(app_scene_authoring *scene)
{
    if (!scene->definition || !scene->editor_working || !scene->graph_editor) {
        return 0;
    }
    const behavior_asset *current = editor_behavior_asset(scene->graph_editor);
    scene->editor_working = current;
    if (!current || !current->name || !scene->definition->name ||
        strcmp(current->name, scene->definition->name) != 0 ||
        scene->editor_working == scene->definition->working) {
        return 0;
    }
    return game_live_sync_working(scene->definition, scene->editor_working);
}

static int open_behavior_graph(app_scene_authoring *scene, const char *asset_name)
{
    game_live_definition *target = game_live_find_definition(scene->live, asset_name);
    if (!target || !scene->graph_editor || sync_open_behavior(scene) != 0) {
        return -1;
    }
    behavior_asset *working = target->working;
    graph *default_graph = behavior_asset_find_graph(working, 0);
    if (!default_graph ||
        editor_open_behavior_asset(scene->graph_editor, working, default_graph->graph_id) !=
            0) {
        return -1;
    }
    scene->definition = target;
    scene->editor_working = editor_behavior_asset(scene->graph_editor);
    editor_ui_set_pane_visible(EDITOR_UI_PANE_NODE_CANVAS, true);
    return 0;
}

static int open_behavior_asset(app_scene_authoring *scene, const char *asset_name)
{
    if (open_behavior_graph(scene, asset_name) != 0) {
        return -1;
    }
    set_behavior_component_context(scene, asset_name);
    set_document(scene, APP_EDITOR_DOCUMENT_BEHAVIOR, asset_name);
    editor_ui_request_focus(EDITOR_UI_FOCUS_NODE_CANVAS);
    snprintf(scene->message, sizeof scene->message, "Editing behavior asset %s.", asset_name);
    return 0;
}

static int focus_prototype_behavior(app_scene_authoring *scene, const char *asset_name)
{
    if (open_behavior_graph(scene, asset_name) != 0) {
        return -1;
    }
    set_document(scene, APP_EDITOR_DOCUMENT_BEHAVIOR, asset_name);
    editor_set_graph_visible(scene->graph_editor, 1);
    editor_ui_request_focus(EDITOR_UI_FOCUS_NODE_CANVAS);
    snprintf(scene->message, sizeof scene->message, "Editing %s in the Prototype.", asset_name);
    return 0;
}

int app_scene_authoring_open_behavior(app_scene_authoring *scene, const char *asset_name)
{
    return open_behavior_asset(scene, asset_name);
}

static int spawn_selected_prototype(app_scene_authoring *scene)
{
    scene_prototype_asset *asset = selected_prototype(scene);
    return asset ? app_scene_authoring_add_instance(scene, asset->prototype->name)
                 : -1;
}

static int asset_search_score(const app_scene_authoring *scene, const char *path)
{
    const char *search = scene->asset_search;
    const char *name = content_basename(path);
    if (!search[0]) {
        return 0;
    }
    int score = 0;
    int query = 0;
    int run = 0;
    for (int i = 0; name[i] && search[query]; i++) {
        if (tolower((unsigned char)name[i]) != tolower((unsigned char)search[query])) {
            run = 0;
            continue;
        }
        bool boundary = i == 0 || name[i - 1] == '_' || name[i - 1] == '-' ||
                        name[i - 1] == ' ';
        score += 10 + (boundary ? 12 : 0) + run * 4;
        run++;
        query++;
    }
    return search[query] ? -1 : score;
}

static int content_prototype_index(const app_scene_authoring *scene, const char *path)
{
    for (int i = 0; i < acount(scene->prototypes); i++) {
        if (scene->prototypes[i].path && strcmp(scene->prototypes[i].path, path) == 0) {
            return i;
        }
    }
    return -1;
}

static int content_behavior_index(const app_scene_authoring *scene, const char *path)
{
    const char *name = content_basename(path);
    game_behavior_world *behaviors = scene->live->behaviors;
    for (int i = 0; i < game_behavior_asset_count(behaviors); i++) {
        const char *asset_name = game_behavior_asset_name_at(behaviors, i);
        char filename[256];
        snprintf(filename, sizeof filename, "%s.json", asset_name);
        if (strcmp(name, asset_name) == 0 || strcmp(name, filename) == 0) {
            return i;
        }
    }
    return -1;
}

static const char *behavior_path(const app_scene_authoring *scene, const char *asset_name)
{
    game_behavior_world *behaviors = scene->live->behaviors;
    for (int i = 0; i < acount(scene->content); i++) {
        const scene_content_entry *entry = &scene->content[i];
        if (entry->directory || content_behavior_index(scene, entry->path) < 0) {
            continue;
        }
        int index = content_behavior_index(scene, entry->path);
        if (strcmp(game_behavior_asset_name_at(behaviors, index), asset_name) == 0) {
            return entry->path;
        }
    }
    return NULL;
}

static int save_behavior_definition(
    app_scene_authoring *scene,
    game_live_definition *definition,
    const char *requested_path
)
{
    const char *path = requested_path ? requested_path
                                      : game_live_definition_path(definition);
    if (!path) {
        path = behavior_path(scene, definition->name);
    }
    if (!path) {
        snprintf(
            scene->message,
            sizeof scene->message,
            "Behavior %s has no source path.",
            definition->name
        );
        return -1;
    }
    char *json = behavior_asset_save_json(definition->working);
    if (!json) {
        return -1;
    }
    int result = write_text_file_atomic(path, json);
    free(json);
    if (result == 0) {
        game_live_definition_set_path(definition, path);
        definition->unsaved = false;
    }
    return result;
}

static int ensure_directory(const char *path)
{
    if (mkdir(path, DOCUMENT_DIRECTORY_MODE) == 0 || errno == EEXIST) {
        return 0;
    }
    return -1;
}

static char *document_default_path(app_scene_authoring *scene)
{
    const char *folder = NULL;
    const char *name = NULL;
    if (scene->document.kind == APP_EDITOR_DOCUMENT_PROTOTYPE) {
        scene_prototype_asset *asset = selected_prototype(scene);
        folder = "prototypes";
        name = asset && asset->prototype ? asset->prototype->name : NULL;
    } else if (scene->document.kind == APP_EDITOR_DOCUMENT_BEHAVIOR) {
        folder = "behaviors";
        name = scene->definition ? scene->definition->name : NULL;
    } else {
        name = "Scene";
    }
    if (!name || !name[0] || ensure_directory(scene->content_root) != 0) {
        return NULL;
    }
    char *directory = folder ? content_path_join(scene->content_root, folder)
                             : xstrdup(scene->content_root);
    if (folder && ensure_directory(directory) != 0) {
        free(directory);
        return NULL;
    }
    size_t filename_size = strlen(name) + strlen(".json") + 1;
    char *filename = (char *)xmalloc(filename_size);
    snprintf(filename, filename_size, "%s.json", name);
    char *path = content_path_join(directory, filename);
    free(filename);
    free(directory);
    return path;
}

static int reload_behavior_document(app_scene_authoring *scene)
{
    game_live_definition *definition = scene->definition;
    if (!definition) {
        snprintf(scene->message, sizeof scene->message, "No behavior document is active.");
        return -1;
    }
    const char *path = game_live_definition_path(definition);
    if (!path) {
        path = behavior_path(scene, definition->name);
    }
    if (!path) {
        snprintf(scene->message, sizeof scene->message, "Behavior requires Save As before Reload.");
        return -1;
    }
    char *json = read_scene_file(path);
    behavior_asset *loaded = json ? behavior_asset_load_json(json) : NULL;
    free(json);
    if (!loaded) {
        snprintf(scene->message, sizeof scene->message, "Could not reload %s.", path);
        return -1;
    }
    if (!loaded->name || strcmp(loaded->name, definition->name) != 0) {
        behavior_asset_free(loaded);
        snprintf(
            scene->message,
            sizeof scene->message,
            "Reloaded behavior name does not match %s.",
            definition->name
        );
        return -1;
    }
    const graph *active = editor_active_graph(scene->graph_editor);
    uint64_t graph_id = active ? active->graph_id : loaded->graph->graph_id;
    if (!behavior_asset_find_graph(loaded, graph_id)) {
        graph_id = loaded->graph->graph_id;
    }
    if (editor_open_behavior_asset(scene->graph_editor, loaded, graph_id) != 0) {
        behavior_asset_free(loaded);
        snprintf(scene->message, sizeof scene->message, "Could not open reloaded %s.", path);
        return -1;
    }
    behavior_asset *previous = definition->working;
    definition->working = loaded;
    definition->dirty = 1;
    char apply_message[256];
    int applied = game_live_preview(scene->live, definition, apply_message, sizeof apply_message);
    definition->unsaved = false;
    scene->preview_retry = true;
    behavior_asset_free(previous);
    scene->editor_working = editor_behavior_asset(scene->graph_editor);
    scene->editor_revision_seen = editor_edit_revision(scene->graph_editor);
    scene->editor_behavior_asset_seen = scene->editor_working;
    scene->editor_definition_seen = definition;
    scene->editor_state_seen = true;
    scene->behavior_apply_pending = false;
    if (applied != 0) {
        snprintf(
            scene->message,
            sizeof scene->message,
            "Reloaded %s; apply blocked, previous runtime remains active.",
            path
        );
    } else {
        snprintf(scene->message, sizeof scene->message, "Reloaded %s.", path);
    }
    return 0;
}

static int reload_prototype_document(app_scene_authoring *scene)
{
    scene_prototype_asset *asset = selected_prototype(scene);
    if (!asset || !asset->path) {
        snprintf(scene->message, sizeof scene->message, "Prototype requires Save As before Reload.");
        return -1;
    }
    char *json = read_scene_file(asset->path);
    char error[256];
    game_prototype *loaded = json
                               ? game_prototype_load(json, &scene->registry, error, sizeof error)
                               : NULL;
    free(json);
    if (!loaded) {
        snprintf(
            scene->message,
            sizeof scene->message,
            "Could not reload %s.",
            asset->path
        );
        return -1;
    }
    if (!loaded->name || strcmp(loaded->name, asset->prototype->name) != 0) {
        game_prototype_free(loaded);
        snprintf(
            scene->message,
            sizeof scene->message,
            "Reloaded prototype name does not match %s.",
            asset->prototype->name
        );
        return -1;
    }
    game_prototype *previous = asset->prototype;
    asset->prototype = loaded;
    if (refresh_prototype_instances(scene, asset) != 0) {
        asset->prototype = previous;
        game_prototype_free(loaded);
        snprintf(
            scene->message,
            sizeof scene->message,
            "Could not apply reloaded %s.",
            asset->path
        );
        return -1;
    }
    game_prototype_free(previous);
    asset->dirty = 0;
    clear_prototype_history(scene);
    snprintf(scene->message, sizeof scene->message, "Reloaded %s.", asset->path);
    return 0;
}

static int reload_active_document(app_scene_authoring *scene)
{
    if (scene->document.kind == APP_EDITOR_DOCUMENT_SYSTEM) {
        return scene->system_path ? open_system_asset(scene, scene->system_path) : -1;
    }
    if (scene->document.kind == APP_EDITOR_DOCUMENT_PROTOTYPE) {
        return reload_prototype_document(scene);
    }
    if (scene->document.kind == APP_EDITOR_DOCUMENT_BEHAVIOR) {
        return reload_behavior_document(scene);
    }
    if (!scene->scene_path) {
        snprintf(scene->message, sizeof scene->message, "Scene requires Save As before Reload.");
        return -1;
    }
    return app_scene_authoring_load(scene, scene->scene_path);
}

static void select_content_entry(app_scene_authoring *scene, const scene_content_entry *entry)
{
    content_select_path(scene, entry->path);
    if (entry->directory) {
        return;
    }
    int prototype_index = content_prototype_index(scene, entry->path);
    if (prototype_index >= 0) {
        select_prototype(scene, prototype_index);
    }
}

static int open_system_asset(app_scene_authoring *scene, const char *path)
{
    char *json = read_scene_file(path);
    if (!json) {
        return -1;
    }
    char error[256] = { 0 };
    game_system_document *document = game_system_document_load(
        json,
        resolve_system_component,
        NULL,
        error,
        sizeof error
    );
    free(json);
    if (!document) {
        snprintf(scene->message, sizeof scene->message, "Could not open System: %s", error);
        return -1;
    }
    const behavior_asset *source = game_system_document_behavior_asset(document);
    graph *graph = behavior_asset_find_graph((behavior_asset *)source, 0);
    if (!graph ||
        editor_open_behavior_asset(scene->graph_editor, source, graph->graph_id) != 0) {
        game_system_document_free(document);
        return -1;
    }
    char *saved_path = xstrdup(path);
    game_system_document_free(scene->system_document);
    free(scene->system_path);
    scene->system_document = document;
    scene->system_path = saved_path;
    scene->editor_working = editor_behavior_asset(scene->graph_editor);
    set_document(scene, APP_EDITOR_DOCUMENT_SYSTEM, source->name);
    editor_ui_set_pane_visible(EDITOR_UI_PANE_NODE_CANVAS, true);
    editor_ui_request_focus(EDITOR_UI_FOCUS_NODE_CANVAS);
    snprintf(
        scene->message,
        sizeof scene->message,
        "Editing System asset %s.",
        source->name
    );
    return 0;
}

int app_scene_authoring_open_system(app_scene_authoring *scene, const char *path)
{
    return scene && path ? open_system_asset(scene, path) : -1;
}

static void open_content_entry(app_scene_authoring *scene, const scene_content_entry *entry)
{
    if (entry->directory) {
        return;
    }
    int prototype_index = content_prototype_index(scene, entry->path);
    if (prototype_index >= 0) {
        open_prototype(scene, prototype_index);
        return;
    }
    int behavior_index = content_behavior_index(scene, entry->path);
    if (behavior_index >= 0) {
        game_behavior_world *behaviors = scene->live->behaviors;
        const char *name = game_behavior_asset_name_at(behaviors, behavior_index);
        if (open_behavior_asset(scene, name) != 0) {
            snprintf(scene->message, sizeof scene->message, "Could not open behavior asset.");
        }
        return;
    }
    if (content_path_in_folder(scene, entry->path, "scenes")) {
        if (app_scene_authoring_load(scene, entry->path) == 0) {
            set_document(scene, APP_EDITOR_DOCUMENT_SCENE, APP_SCENE_DOCUMENT_ID);
            editor_ui_request_focus(EDITOR_UI_FOCUS_SCENE);
        }
        return;
    }
    if (content_path_in_folder(scene, entry->path, "systems")) {
        if (open_system_asset(scene, entry->path) != 0) {
            snprintf(scene->message, sizeof scene->message, "Could not open System asset.");
        }
    }
}

static int prototype_has_component_id(const game_prototype *prototype, ecs_component_id id)
{
    for (int i = 0; i < acount(prototype->components); i++) {
        if (prototype->components[i].desc->id == id) {
            return 1;
        }
    }
    return 0;
}

static int refresh_prototype_actor(
    app_scene_authoring *scene,
    const game_prototype *prototype,
    scene_actor *actor
)
{
    char error[256];
    char *overrides = game_prototype_capture_overrides_json(
        actor->base,
        scene->world,
        actor->entity
    );
    if (!overrides) {
        return -1;
    }
    for (int i = 0; i < scene->registry.component_count; i++) {
        const ecs_component_desc *desc = scene->registry.components[i];
        if (desc && prototype_has_component_id(actor->base, desc->id) &&
            !prototype_has_component_id(prototype, desc->id) &&
            ecs_component_has(scene->world, actor->entity, desc->id) &&
            ecs_world_record_remove(scene->world, actor->entity, desc->id, (uint64_t)i + 1) !=
                ECS_OK) {
            free(overrides);
            return -1;
        }
    }
    for (int i = 0; i < acount(prototype->components); i++) {
        const game_prototype_component *component = &prototype->components[i];
        if (!ecs_component_has(scene->world, actor->entity, component->desc->id) &&
            ecs_world_record_add(scene->world, actor->entity, component->desc->id, NULL, (uint64_t)i + 1) !=
                ECS_OK) {
            free(overrides);
            return -1;
        }
    }
    if (ecs_world_apply_commands(scene->world) != ECS_OK) {
        free(overrides);
        return -1;
    }
    for (int i = 0; i < acount(prototype->components); i++) {
        const game_prototype_component *component = &prototype->components[i];
        for (int field = 0; field < component->desc->type->field_count; field++) {
            pin_value value;
            pin_read_field(component->data, &component->desc->type->fields[field], &value);
            if (ecs_component_write_field(
                    scene->world,
                    actor->entity,
                    component->desc->id,
                    field,
                    value
                ) != ECS_OK) {
                free(overrides);
                return -1;
            }
        }
    }
    if (game_prototype_apply_components_json(
            scene->world,
            actor->entity,
            overrides,
            &scene->registry,
            error,
            sizeof error
        ) != 0) {
        free(overrides);
        return -1;
    }
    for (int i = 0; i < scene->registry.component_count; i++) {
        const ecs_component_desc *desc = scene->registry.components[i];
        if (desc && prototype_has_component_id(actor->base, desc->id) &&
            !prototype_has_component_id(prototype, desc->id) &&
            ecs_component_has(scene->world, actor->entity, desc->id) &&
            ecs_world_record_remove(scene->world, actor->entity, desc->id, (uint64_t)i + 1) !=
                ECS_OK) {
            free(overrides);
            return -1;
        }
    }
    if (ecs_world_apply_commands(scene->world) != ECS_OK) {
        free(overrides);
        return -1;
    }
    game_prototype *base = game_prototype_clone(prototype, error, sizeof error);
    if (!base) {
        free(overrides);
        return -1;
    }
    game_prototype_free(actor->base);
    actor->base = base;
    free(overrides);
    return 0;
}

static int refresh_prototype_instances(app_scene_authoring *scene, scene_prototype_asset *asset)
{
    if (scene->runtime_snapshot_valid &&
        app_scene_authoring_runtime_command(scene, APP_RUNTIME_SCENE, APP_RUNTIME_STOP) !=
            0) {
        return -1;
    }
    for (int i = 0; i < acount(scene->actors); i++) {
        scene_actor *actor = &scene->actors[i];
        if (strcmp(actor->prototype_name, asset->prototype->name) != 0) {
            continue;
        }
        if (refresh_prototype_actor(scene, asset->prototype, actor) != 0) {
            snprintf(
                scene->prototype_message,
                sizeof scene->prototype_message,
                "Could not update instance %s.",
                actor->name
            );
            return -1;
        }
    }
    return 0;
}

static int write_prototype_field(
    app_scene_authoring *scene,
    scene_prototype_asset *asset,
    const char *component,
    const char *field,
    pin_value value
)
{
    if (game_prototype_write_field(asset->prototype, component, field, value) != 0) {
        return -1;
    }
    asset->dirty = 1;
    return refresh_prototype_instances(scene, asset);
}

int app_scene_authoring_set_prototype_field(
    app_scene_authoring *scene,
    const char *prototype_name,
    const char *component_name,
    const char *field_name,
    pin_value value
)
{
    if (!scene || !prototype_name || !component_name || !field_name) {
        return -1;
    }
    scene_prototype_asset *asset = find_prototype(scene, prototype_name);
    if (!asset) {
        return -1;
    }
    prototype_history_before(scene, asset->prototype);
    return write_prototype_field(scene, asset, component_name, field_name, value);
}

static const ecs_component_desc *component_descriptor(const app_scene_authoring *scene, const char *name)
{
    for (int i = 0; i < scene->registry.component_count; i++) {
        const ecs_component_desc *desc = scene->registry.components[i];
        if (desc && strcmp(desc->name, name) == 0) {
            return desc;
        }
    }
    return NULL;
}

int app_scene_authoring_set_instance_component(app_scene_authoring *scene, const char *name, bool present)
{
    if (!scene || !name) {
        return -1;
    }
    scene_actor *actor = selected_actor(scene);
    const ecs_component_desc *desc = component_descriptor(scene, name);
    if (!actor || !desc || scene->runtime_snapshot_valid) {
        return -1;
    }
    finish_transform_edit(scene, false);
    bool exists = ecs_component_has(scene->world, actor->entity, desc->id);
    if (exists == present) {
        return 0;
    }
    char *json = present ? NULL
                         : game_prototype_entity_component_json(
                               scene->world,
                               actor->entity,
                               desc,
                               &scene->registry
                           );
    if (!present && !json) {
        return -1;
    }
    int result = present
                   ? ecs_world_record_add(scene->world, actor->entity, desc->id, NULL, 0)
                   : ecs_world_record_remove(scene->world, actor->entity, desc->id, 0);
    if (result == ECS_OK) {
        result = ecs_world_apply_commands(scene->world);
    }
    if (result == ECS_OK && present) {
        json = game_prototype_entity_component_json(
            scene->world,
            actor->entity,
            desc,
            &scene->registry
        );
    }
    if (result == ECS_OK && !json) {
        if (present) {
            ecs_world_record_remove(scene->world, actor->entity, desc->id, 0);
            ecs_world_apply_commands(scene->world);
        }
        return -1;
    }
    if (result == ECS_OK && json) {
        scene_component_edit edit = { .component = desc->id,
                                      .state_json = json,
                                      .added = present };
        bool recorded = scene_history_push_component(scene->history, actor->entity, &edit);
        assert(recorded);
        scene->scene_dirty = true;
        set_document(scene, APP_EDITOR_DOCUMENT_SCENE, APP_SCENE_DOCUMENT_ID);
    }
    free(json);
    return result;
}

int app_scene_authoring_add_prototype_component(
    app_scene_authoring *scene,
    const char *prototype_name,
    const char *component_name
)
{
    if (!scene || !prototype_name || !component_name) {
        return -1;
    }
    scene_prototype_asset *asset = find_prototype(scene, prototype_name);
    const ecs_component_desc *desc = component_descriptor(scene, component_name);
    if (!asset || !desc) {
        return -1;
    }
    prototype_history_before(scene, asset->prototype);
    if (game_prototype_add_component(asset->prototype, desc) != 0) {
        return -1;
    }
    asset->dirty = 1;
    return refresh_prototype_instances(scene, asset);
}

int app_scene_authoring_remove_prototype_component(
    app_scene_authoring *scene,
    const char *prototype_name,
    const char *component_name
)
{
    if (!scene || !prototype_name || !component_name) {
        return -1;
    }
    scene_prototype_asset *asset = find_prototype(scene, prototype_name);
    if (!asset) {
        return -1;
    }
    prototype_history_before(scene, asset->prototype);
    if (game_prototype_remove_component(asset->prototype, component_name) != 0) {
        return -1;
    }
    asset->dirty = 1;
    return refresh_prototype_instances(scene, asset);
}

static int restore_prototype(
    app_scene_authoring *scene,
    scene_prototype_asset *asset,
    char ***from,
    char ***to
)
{
    int last = acount(*from) - 1;
    if (last < 0) {
        return 0;
    }
    char error[256];
    char *current = game_prototype_save(asset->prototype);
    char *snapshot = (*from)[last];
    game_prototype *prototype = game_prototype_load(snapshot, &scene->registry, error, sizeof error);
    if (!prototype) {
        return -1;
    }
    adel(*from, last);
    apush(*to, current);
    game_prototype_free(asset->prototype);
    asset->prototype = prototype;
    asset->dirty = 1;
    if (refresh_prototype_instances(scene, asset) != 0) {
        free(snapshot);
        return -1;
    }
    free(snapshot);
    return 0;
}

static int prototype_undo(app_scene_authoring *scene)
{
    scene_prototype_asset *asset = selected_prototype(scene);
    return asset
             ? restore_prototype(scene, asset, &scene->prototype_undo, &scene->prototype_redo)
             : -1;
}

static int prototype_redo(app_scene_authoring *scene)
{
    scene_prototype_asset *asset = selected_prototype(scene);
    return asset
             ? restore_prototype(scene, asset, &scene->prototype_redo, &scene->prototype_undo)
             : -1;
}

static bool fallback_appearance_field(const game_prototype_component *component, const type_field *field)
{
    return component->desc->id == GAME_COMPONENT_GameAppearance.id &&
           (appearance_color_field(component->desc->type, field) ||
            field->offset == offsetof(GameAppearance, glow_radius));
}

static void property_label(
    const game_prototype_component *component,
    const type_field *field,
    char *label,
    size_t size
)
{
    if (component->desc->id == GAME_COMPONENT_GameMaterial.id) {
        if (field->offset == offsetof(GameMaterial, scene)) {
            snprintf(label, size, "Base Color");
            return;
        }
        if (field->offset == offsetof(GameMaterial, border_scene)) {
            snprintf(label, size, "Border Color");
            return;
        }
    }
    editor_identifier_label(field->name, label, size);
}

static bool advanced_material_field(const ecs_component_desc *component, const type_field *field)
{
    if (component->id != GAME_COMPONENT_GameMaterial.id) {
        return false;
    }
    switch (field->offset) {
        case offsetof(GameMaterial, scene):
        case offsetof(GameMaterial, border_scene):
        case offsetof(GameMaterial, emission_color):
        case offsetof(GameMaterial, emission_intensity): return false;
        default: return true;
    }
}

static bool begin_property_group(const ecs_component_desc *component, int group, const char *id)
{
    if (!group) {
        return true;
    }
    if (component->id != GAME_COMPONENT_GameMaterial.id) {
        return false;
    }
    bool open = editor_ui_disclosure("Advanced Lighting", "advanced_lighting");
    editor_ui_track_item(id);
    return open;
}

static bool component_tree(const ecs_component_desc *component)
{
    char name[128];
    editor_component_label(component->name, name, sizeof name);
    return editor_ui_disclosure(name, component->name);
}

static void inspector_title(const char *name)
{
    ImGui_PushFontFloat(NULL, ImGui_GetFontSize() + 3);
    ImGui_TextUnformatted(name);
    ImGui_PopFont();
}

static bool property_number(const char *label, ImGuiDataType type, void *value, bool overridden)
{
    ImGui_TextDisabled("%s", label);
    float width = ImGui_GetContentRegionAvail().x;
    ImGui_SetNextItemWidth(fmaxf(1, width - (overridden ? 68 : 0)));
    return ImGui_InputScalar("###field_value", type, value);
}

static bool draw_prototype_field(
    app_scene_authoring *scene,
    scene_prototype_asset *asset,
    const game_prototype_component *component,
    const type_field *field
)
{
    pin_value value = { 0 };
    pin_read_field(component->data, field, &value);
    char label[160];
    property_label(component, field, label, sizeof label);
    bool changed = false;
    bool color = appearance_color_field(component->desc->type, field);
    bool first_color_change = false;
    switch (value.type) {
        case PIN_BOOL: changed = ImGui_Checkbox(label, &value.b); break;
        case PIN_I8:
            changed = property_number(label, ImGuiDataType_S8, &value.i8, false);
            break;
        case PIN_U8:
            changed = property_number(label, ImGuiDataType_U8, &value.u8, false);
            break;
        case PIN_I16:
            changed = property_number(label, ImGuiDataType_S16, &value.i16, false);
            break;
        case PIN_U16:
            changed = property_number(label, ImGuiDataType_U16, &value.u16, false);
            break;
        case PIN_I32:
        case PIN_ENUM:
            if (appearance_shape_field(component->desc->type, field)) {
                ImGui_TextDisabled("%s", label);
            }
            changed = appearance_shape_field(component->desc->type, field)
                        ? editor_choices_draw("prototype.shape", &value.i, &APPEARANCE_SHAPES)
                        : property_number(label, ImGuiDataType_S32, &value.i, false);
            break;
        case PIN_U32:
            if (color) {
                char color_id[192];
                snprintf(
                    color_id,
                    sizeof color_id,
                    "prototype.color.%s.%s",
                    component->desc->name,
                    field->name
                );
                ImGui_TextDisabled("%s", label);
                changed = editor_color_draw(
                    &scene->property_color,
                    color_id,
                    &value.u,
                    &first_color_change
                );
                break;
            }
            changed = collision_category_field(component->desc->type, field)
                        ? editor_flags_draw(field->name, &value.u, &COLLISION_CATEGORIES)
                        : property_number(label, ImGuiDataType_U32, &value.u, false);
            break;
        case PIN_I64:
            changed = property_number(label, ImGuiDataType_S64, &value.i64, false);
            break;
        case PIN_U64:
            changed = property_number(label, ImGuiDataType_U64, &value.u64, false);
            break;
        case PIN_F32:
            changed = property_number(label, ImGuiDataType_Float, &value.f, false);
            break;
        case PIN_F64:
            changed = property_number(label, ImGuiDataType_Double, &value.d, false);
            break;
        case PIN_NAME:
        case PIN_STR:
        case PIN_HANDLE:
        case PIN_STRUCT:
        case PIN_FLOW:
            ImGui_Text("%s", field->name);
            ImGui_SameLine();
            ImGui_TextDisabled("%s", pin_value_to_string(&value));
            break;
    }
    if (!changed) {
        return false;
    }
    if (color) {
        if (first_color_change) {
            prototype_history_before(scene, asset->prototype);
        }
        return write_prototype_field(scene, asset, component->desc->name, field->name, value) ==
               0;
    }
    return app_scene_authoring_set_prototype_field(
               scene,
               asset->prototype->name,
               component->desc->name,
               field->name,
               value
           ) == 0;
}

static void draw_prototype_preview_controls(app_scene_authoring *scene, scene_prototype_asset *asset)
{
    if (scene->active_runtime_target == APP_RUNTIME_PREVIEW &&
        previewed_prototype(scene) == asset) {
        return;
    }
    if (ImGui_Button("Preview prototype")) {
        app_scene_authoring_preview_prototype(scene, asset->prototype->name);
    }
}

static void draw_prototype_components(app_scene_authoring *scene, scene_prototype_asset *asset)
{
    ImGui_SeparatorText("Components");
    for (int i = 0; i < acount(asset->prototype->components); i++) {
        const game_prototype_component *component = &asset->prototype->components[i];
        ImGui_PushID(component->desc->name);
        ImGui_SetNextItemOpen(i == 0, ImGuiCond_Once);
        bool open = component_tree(component->desc);
        char item_id[192];
        snprintf(
            item_id,
            sizeof item_id,
            "prototype.component.%s",
            component->desc->name
        );
        editor_ui_track_item(item_id);
        bool remove = false;
        if (open) {
            bool material = prototype_has_component_id(
                asset->prototype,
                GAME_COMPONENT_GameMaterial.id
            );
            if (material && component->desc->id == GAME_COMPONENT_GameAppearance.id) {
                ImGui_TextDisabled("Colors are in Material.");
            }
            for (int group = 0; group < 2; group++) {
                if (!begin_property_group(component->desc, group, "prototype.material.advanced")) {
                    continue;
                }
                for (int field = 0; field < component->desc->type->field_count;
                     field++) {
                    if (advanced_material_field(
                            component->desc,
                            &component->desc->type->fields[field]
                        ) != (group == 1)) {
                        continue;
                    }
                    if (material && fallback_appearance_field(
                                        component,
                                        &component->desc->type->fields[field]
                                    )) {
                        continue;
                    }
                    ImGui_PushIDInt(field);
                    draw_prototype_field(
                        scene,
                        asset,
                        component,
                        &component->desc->type->fields[field]
                    );
                    snprintf(
                        item_id,
                        sizeof item_id,
                        "prototype.field.%s.%s",
                        component->desc->name,
                        component->desc->type->fields[field].name
                    );
                    editor_ui_track_item(item_id);
                    ImGui_PopID();
                }
                if (group) {
                    ImGui_TreePop();
                }
            }
            remove = ImGui_Button("Remove component");
            ImGui_TreePop();
        }
        ImGui_PopID();
        if (remove) {
            app_scene_authoring_remove_prototype_component(
                scene,
                asset->prototype->name,
                component->desc->name
            );
            break;
        }
    }
    if (ImGui_Button("Add Component")) {
        ImGui_OpenPopup("prototype_add_component", 0);
    }
    if (ImGui_BeginPopup("prototype_add_component", 0)) {
        int available = 0;
        for (int i = 0; i < scene->registry.component_count; i++) {
            const ecs_component_desc *desc = scene->registry.components[i];
            if (!desc || prototype_has_component_id(asset->prototype, desc->id)) {
                continue;
            }
            available++;
            ImGui_PushIDInt(i);
            char component_label[128];
            editor_component_label(desc->name, component_label, sizeof component_label);
            if (ImGui_MenuItem(component_label)) {
                app_scene_authoring_add_prototype_component(
                    scene,
                    asset->prototype->name,
                    desc->name
                );
                ImGui_CloseCurrentPopup();
            }
            ImGui_PopID();
        }
        if (!available) {
            ImGui_TextUnformatted("All components added");
        }
        ImGui_EndPopup();
    }
}

static void draw_prototype_behaviors(app_scene_authoring *scene, scene_prototype_asset *asset)
{
    ImGui_SeparatorText("Behaviors");
    if (!acount(asset->prototype->graphs)) {
        ImGui_TextUnformatted("No Behaviors");
    }
    for (int i = 0; i < acount(asset->prototype->graphs); i++) {
        const char *asset_name = asset->prototype->graphs[i];
        game_live_definition *definition = game_live_find_definition(scene->live, asset_name);
        bool active = scene->definition &&
                      strcmp(scene->definition->name, asset_name) == 0;
        char label[160];
        snprintf(label, sizeof label, "%s##behavior_%d", asset_name, i);
        ImGui_PushIDInt(i);
        bool open = ImGui_TreeNodeEx(label, ImGuiTreeNodeFlags_SpanAvailWidth);
        bool detached = false;
        if (open) {
            if (ImGui_Button("Open graph")) {
                active = true;
                (void)focus_prototype_behavior(scene, asset_name);
            }
            ImGui_SameLine();
            if (ImGui_Button("Detach")) {
                detached = app_scene_authoring_detach_prototype_behavior(
                               scene,
                               asset->prototype->name,
                               asset_name
                           ) == 0;
            }
        }
        if (open && !detached) {
            if (!definition || !definition->working) {
                ImGui_TextDisabled("Missing behavior asset");
            } else {
                for (int graph_index = 0;
                     graph_index < acount(definition->working->graphs);
                     graph_index++) {
                    graph *member = definition->working->graphs[graph_index];
                    char graph_label[160];
                    snprintf(
                        graph_label,
                        sizeof graph_label,
                        "%s: %s##member_%d",
                        graph_kind_name(member->kind),
                        member->name && member->name[0] ? member->name : "Default",
                        graph_index
                    );
                    if (ImGui_SelectableEx(
                            graph_label,
                            active && editor_active_graph(scene->graph_editor) &&
                                editor_active_graph(scene->graph_editor)->graph_id ==
                                    member->graph_id,
                            0,
                            (ImVec2){ 0.0f, 0.0f }
                        )) {
                        if (focus_prototype_behavior(scene, asset_name) == 0) {
                            editor_open_graph(scene->graph_editor, member->graph_id);
                        }
                    }
                }
                ImGui_SeparatorText("Variables");
                for (int variable = 0;
                     variable < acount(definition->working->variables);
                     variable++) {
                    ImGui_Text("  %s", definition->working->variables[variable].name);
                }
            }
        }
        if (open) {
            ImGui_TreePop();
        }
        ImGui_PopID();
        if (detached) {
            break;
        }
    }
    if (ImGui_Button("Add Behavior")) {
        ImGui_OpenPopup("prototype_add_behavior", 0);
    }
    if (ImGui_BeginPopup("prototype_add_behavior", 0)) {
        int available = 0;
        for (int i = 0; i < game_behavior_asset_count(scene->live->behaviors); i++) {
            const char *asset_name = game_behavior_asset_name_at(scene->live->behaviors, i);
            if (game_prototype_has_graph(asset->prototype, asset_name)) {
                continue;
            }
            available++;
            ImGui_PushIDInt(i);
            if (ImGui_MenuItem(asset_name)) {
                app_scene_authoring_attach_prototype_behavior(
                    scene,
                    asset->prototype->name,
                    asset_name
                );
                ImGui_CloseCurrentPopup();
            }
            ImGui_PopID();
        }
        if (!available) {
            ImGui_TextUnformatted("All behaviors attached");
        }
        ImGui_EndPopup();
    }
}

static void draw_prototype_inspector(app_scene_authoring *scene)
{
    if (!editor_ui_pane_visible(EDITOR_UI_PANE_INSPECTOR)) {
        return;
    }
    ImGui_SetNextWindowSize((ImVec2){ 340.0f, 520.0f }, ImGuiCond_FirstUseEver);
    if (editor_ui_take_focus(EDITOR_UI_FOCUS_INSPECTOR)) {
        ImGui_SetNextWindowFocus();
    }
    if (!editor_ui_begin_pane(EDITOR_UI_PANE_INSPECTOR, "Inspector", 0)) {
        ImGui_End();
        return;
    }
    scene_prototype_asset *asset = selected_prototype(scene);
    if (!asset || !asset->prototype) {
        ImGui_TextUnformatted("Prototype");
        ImGui_Separator();
        ImGui_TextUnformatted("No Selection");
        ImGui_End();
        return;
    }
    inspector_title(asset->prototype->name);
    ImGui_TextDisabled("Prototype");
    if (asset->dirty) {
        ImGui_SameLine();
        ImGui_TextUnformatted("(modified)");
    }
    ImGui_PushID(asset->prototype->name);
    draw_prototype_preview_controls(scene, asset);
    draw_prototype_components(scene, asset);
    draw_prototype_behaviors(scene, asset);
    ImGui_PopID();
    ImGui_End();
}

static bool scene_search_matches(const char *query, const char *text)
{
    if (!query[0]) {
        return true;
    }
    for (int start = 0; text[start]; start++) {
        int i = 0;
        while (query[i] && text[start + i] &&
               tolower((unsigned char)query[i]) ==
                   tolower((unsigned char)text[start + i])) {
            i++;
        }
        if (!query[i]) {
            return true;
        }
    }
    return false;
}

typedef struct
{
    ImDrawList *draw;
    ImVec2 min;
    float width;
    ImU32 selected;
    ImU32 hovered;
} sidebar_row;

static sidebar_row sidebar_row_begin()
{
    sidebar_row row = { ImGui_GetWindowDrawList(),
                        ImGui_GetCursorScreenPos(),
                        ImGui_GetContentRegionAvail().x,
                        ImGui_GetColorU32(ImGuiCol_Header),
                        ImGui_GetColorU32(ImGuiCol_HeaderHovered) };
    ImDrawList_ChannelsSplit(row.draw, 2);
    ImDrawList_ChannelsSetCurrent(row.draw, 1);
    ImGui_PushStyleColorImVec4(ImGuiCol_Header, (ImVec4){ 0 });
    ImGui_PushStyleColorImVec4(ImGuiCol_HeaderHovered, (ImVec4){ 0 });
    ImGui_PushStyleColorImVec4(ImGuiCol_HeaderActive, (ImVec4){ 0 });
    return row;
}

static void sidebar_row_end(sidebar_row row, bool selected, const char *icon, const char *label, float inset)
{
    ImGui_PopStyleColorEx(3);
    ImVec2 min = ImGui_GetItemRectMin(), max = ImGui_GetItemRectMax();
    bool hovered = ImGui_IsItemHovered(0);
    ImDrawList_ChannelsSetCurrent(row.draw, 0);
    if (selected || hovered) {
        ImDrawList_AddRectFilledEx(
            row.draw,
            (ImVec2){ row.min.x + 2, min.y + 1 },
            (ImVec2){ row.min.x + row.width - 2, max.y - 1 },
            selected ? row.selected : row.hovered,
            5.0f,
            ImDrawFlags_RoundCornersAll
        );
    }
    ImDrawList_ChannelsSetCurrent(row.draw, 1);
    float y = (min.y + max.y - ImGui_GetFontSize()) * 0.5f;
    ImDrawList_PushClipRect(
        row.draw,
        (ImVec2){ row.min.x, min.y },
        (ImVec2){ row.min.x + row.width - 4, max.y },
        true
    );
    ImDrawList_AddText(row.draw, (ImVec2){ row.min.x + inset, y }, ImGui_GetColorU32(ImGuiCol_TextDisabled), icon);
    ImDrawList_AddText(row.draw, (ImVec2){ row.min.x + inset + 24, y }, ImGui_GetColorU32(ImGuiCol_Text), label);
    ImDrawList_PopClipRect(row.draw);
    ImDrawList_ChannelsMerge(row.draw);
}

static bool scene_actor_row(const char *name, bool selected)
{
    sidebar_row row = sidebar_row_begin();
    bool pressed = ImGui_SelectableEx("##actor_row", selected, 0, (ImVec2){ 0.0f, 28.0f });
    sidebar_row_end(row, selected, "\xef\x86\xb2", name, 10.0f);
    return pressed;
}

static void draw_scene_outline(app_scene_authoring *scene)
{
    if (ImGui_Button("Add")) {
        ImGui_OpenPopup("scene_add", 0);
    }
    editor_ui_track_item("scene.add");
    if (ImGui_BeginPopup("scene_add", 0)) {
        for (int i = 0; i < acount(scene->prototypes); i++) {
            const char *name = scene->prototypes[i].prototype->name;
            if (ImGui_MenuItem(name)) {
                app_scene_authoring_add_instance(scene, name);
            }
            char id[192];
            snprintf(id, sizeof id, "scene.add.%s", name);
            editor_ui_track_item(id);
        }
        ImGui_EndPopup();
    }
    ImGui_SetNextItemWidth(fmaxf(1, ImGui_GetContentRegionAvail().x - 34));
    editor_ui_search_field("scene_search", scene->scene_search, sizeof scene->scene_search, "Search scene...");
    editor_ui_track_item("scene.search");
    ImGui_SameLine();
    if (editor_ui_icon_button("clear_scene_search", 0xf00d, scene->scene_search[0] != '\0')) {
        scene->scene_search[0] = '\0';
    }
    editor_ui_track_item("scene.clear_search");
    int visible = 0;
    int delete_index = -1;
    for (int i = 0; i < acount(scene->actors); i++) {
        if (!scene_search_matches(scene->scene_search, scene->actors[i].name) &&
            !scene_search_matches(scene->scene_search, scene->actors[i].prototype_name)) {
            continue;
        }
        visible++;
        ImGui_PushIDInt(i);
        if (scene_actor_row(
                scene->actors[i].name,
                scene->actors[i].marked || i == scene->selected
            )) {
            select_instance(scene, i);
            set_document(scene, APP_EDITOR_DOCUMENT_SCENE, APP_SCENE_DOCUMENT_ID);
        }
        char item_id[192];
        snprintf(item_id, sizeof item_id, "scene.actor.%s", scene->actors[i].name);
        editor_ui_track_item(item_id);
        if (ImGui_IsItemHovered(0) && ImGui_IsMouseDoubleClicked(0)) {
            scene->reveal_actor = scene->actors[i].entity;
        }
        if (ImGui_BeginPopupContextItemEx("actor_actions", 0)) {
            if (ImGui_MenuItem("Delete")) {
                delete_index = i;
            }
            editor_ui_track_item("scene.delete");
            ImGui_EndPopup();
        }
        ImGui_PopID();
    }
    if (delete_index >= 0) {
        select_instance(scene, delete_index);
        app_scene_authoring_delete_instance(scene);
    }
    game_entities_copy_live(scene->live->entities, &scene->runtime_entities);
    bool spawned_section = false;
    for (int i = 0; i < acount(scene->runtime_entities); i++) {
        ecs_entity entity = scene->runtime_entities[i];
        char id[96], label[192];
        snprintf(id, sizeof id, "scene.spawned.%u.%u", entity.slot, entity.generation);
        snprintf(
            label,
            sizeof label,
            "%s %u",
            runtime_entity_label(scene, entity),
            entity.slot
        );
        if (!scene_search_matches(scene->scene_search, label)) {
            continue;
        }
        visible++;
        if (!spawned_section) {
            ImGui_SeparatorText("Spawned during Play");
            editor_ui_track_item("scene.spawned.section");
            spawned_section = true;
        }
        ImGui_PushID(id);
        if (scene_actor_row(
                label,
                ecs_entity_to_u64(entity) == ecs_entity_to_u64(scene->runtime_selected)
            )) {
            app_scene_authoring_select_runtime_entity(scene, entity);
        }
        editor_ui_track_item(id);
        if (ImGui_IsItemHovered(0) && ImGui_IsMouseDoubleClicked(0)) {
            scene->reveal_actor = entity;
        }
        ImGui_PopID();
    }
    if (!visible) {
        ImGui_TextDisabled(scene->scene_search[0] ? "No Results" : "No Instances");
        if (scene->scene_search[0]) {
            editor_ui_track_item("scene.search.empty");
        }
    }
}

static const char *inspection_state_name(flow_state state)
{
    switch (state) {
        case FLOW_IDLE: return "Idle";
        case FLOW_RUNNING: return "Running";
        case FLOW_SUSPENDED: return "Suspended";
        case FLOW_PAUSED: return "Paused";
        case FLOW_COMPLETED: return "Completed";
        case FLOW_FAILED: return "Failed";
    }
    return "Unknown";
}

static int revert_instance_components(app_scene_authoring *scene, scene_actor *actor)
{
    scene_field_edit *edits = NULL;
    for (int i = 0; i < acount(actor->base->components); i++) {
        const game_prototype_component *component = &actor->base->components[i];
        if (!ecs_component_has(scene->world, actor->entity, component->desc->id)) {
            continue;
        }
        for (int j = 0; j < component->desc->type->field_count; j++) {
            const type_field *field = &component->desc->type->fields[j];
            scene_field_edit edit = { .component = component->desc->id, .field = j };
            if (ecs_component_read_field(scene->world, actor->entity, edit.component, j, &edit.before) !=
                ECS_OK) {
                afree(edits);
                return -1;
            }
            pin_read_field(component->data, field, &edit.after);
            if (editable_scalar(&edit.before) && editable_scalar(&edit.after) &&
                !pin_value_equal(&edit.before, &edit.after)) {
                apush(edits, edit);
            }
        }
    }
    for (int i = 0; i < acount(edits); i++) {
        scene_field_edit *edit = &edits[i];
        if (ecs_component_write_field(scene->world, actor->entity, edit->component, edit->field, edit->after) !=
            ECS_OK) {
            for (int j = 0; j < i; j++) {
                ecs_component_write_field(
                    scene->world,
                    actor->entity,
                    edits[j].component,
                    edits[j].field,
                    edits[j].before
                );
            }
            afree(edits);
            return -1;
        }
    }
    bool changed = scene_history_push_fields(scene->history, actor->entity, edits, acount(edits));
    afree(edits);
    scene->scene_dirty |= changed;
    return changed;
}

static bool draw_instance_field(
    app_scene_authoring *scene,
    scene_actor *actor,
    const game_prototype_component *component,
    const type_field *field
)
{
    pin_value value = { 0 };
    pin_value inherited = { 0 };
    if (ecs_component_read_field(
            scene->world,
            actor->entity,
            component->desc->id,
            (int)(field - component->desc->type->fields),
            &value
        ) != ECS_OK) {
        return false;
    }
    inherited = value;
    if (component->data) {
        pin_read_field(component->data, field, &inherited);
    }
    bool overridden = !pin_value_equal(&value, &inherited);
    char label[160];
    char field_label[128];
    property_label(component, field, field_label, sizeof field_label);
    snprintf(label, sizeof label, "%s%s", field_label, overridden ? " *" : "");
    pin_value before = value;
    int field_index = (int)(field - component->desc->type->fields);
    bool pending = scene->property_actor.slot == actor->entity.slot &&
                   ecs_entity_to_u64(scene->property_actor) ==
                       ecs_entity_to_u64(actor->entity) &&
                   scene->property_edit.component == component->desc->id &&
                   scene->property_edit.field == field_index;
    scene->property_seen |= pending;
    bool color_active = false;
    char control_label[192];
    snprintf(control_label, sizeof control_label, "%s###field_value", label);
    bool changed = false;
    bool reverted = false;
    bool position_field = component->desc->id == GAME_COMPONENT_GameTransform.id;
    ImGui_BeginDisabled(!scene_can_move(scene));
    switch (value.type) {
        case PIN_BOOL: changed = ImGui_Checkbox(control_label, &value.b); break;
        case PIN_I8:
            changed = property_number(label, ImGuiDataType_S8, &value.i8, overridden);
            break;
        case PIN_U8:
            changed = property_number(label, ImGuiDataType_U8, &value.u8, overridden);
            break;
        case PIN_I16:
            changed = property_number(label, ImGuiDataType_S16, &value.i16, overridden);
            break;
        case PIN_U16:
            changed = property_number(label, ImGuiDataType_U16, &value.u16, overridden);
            break;
        case PIN_I32:
        case PIN_ENUM:
            if (appearance_shape_field(component->desc->type, field)) {
                ImGui_TextDisabled("%s", label);
            }
            changed = appearance_shape_field(component->desc->type, field)
                        ? editor_choices_draw("instance.shape", &value.i, &APPEARANCE_SHAPES)
                        : property_number(label, ImGuiDataType_S32, &value.i, overridden);
            break;
        case PIN_U32:
            if (appearance_color_field(component->desc->type, field)) {
                bool first_change;
                char color_id[192];
                snprintf(
                    color_id,
                    sizeof color_id,
                    "instance.color.%s.%s",
                    component->desc->name,
                    field->name
                );
                ImGui_TextDisabled("%s", label);
                changed = editor_color_draw(
                    &scene->property_color,
                    color_id,
                    &value.u,
                    &first_change
                );
                color_active = editor_color_open(color_id);
                break;
            }
            changed = collision_category_field(component->desc->type, field)
                        ? editor_flags_draw(control_label, &value.u, &COLLISION_CATEGORIES)
                        : property_number(label, ImGuiDataType_U32, &value.u, overridden);
            break;
        case PIN_I64:
            changed = property_number(label, ImGuiDataType_S64, &value.i64, overridden);
            break;
        case PIN_U64:
            changed = property_number(label, ImGuiDataType_U64, &value.u64, overridden);
            break;
        case PIN_F32:
            changed = property_number(label, ImGuiDataType_Float, &value.f, overridden);
            break;
        case PIN_F64:
            changed = property_number(label, ImGuiDataType_Double, &value.d, overridden);
            break;
        case PIN_NAME:
        case PIN_STR:
        case PIN_HANDLE:
        case PIN_STRUCT:
        case PIN_FLOW:
            ImGui_Text("%s", label);
            ImGui_SameLine();
            ImGui_TextDisabled("%s", pin_value_to_string(&value));
            break;
    }
    bool field_active = color_active || ImGui_IsItemActive();
    char item_id[192];
    snprintf(
        item_id,
        sizeof item_id,
        "instance.field.%s.%s",
        component->desc->name,
        field->name
    );
    editor_ui_track_item(item_id);
    if (position_field) {
        editor_ui_track_item(
            field->offset == offsetof(GameTransform, x) ? "instance.transform.x" : "instance.transform.y"
        );
    }
    if (overridden && editable_scalar(&before)) {
        ImGui_SameLine();
        reverted = ImGui_Button("Revert");
        snprintf(
            item_id,
            sizeof item_id,
            "instance.revert.%s.%s",
            component->desc->name,
            field->name
        );
        editor_ui_track_item(item_id);
        if (position_field) {
            editor_ui_track_item(
                field->offset == offsetof(GameTransform, x)
                    ? "instance.revert.transform.x"
                    : "instance.revert.transform.y"
            );
        }
    }
    ImGui_EndDisabled();
    if (position_field) {
        if ((changed || reverted) &&
            (!scene->edit_actor.slot || scene->edit_field != field_index)) {
            begin_transform_edit(scene, actor->entity);
            scene->edit_field = field_index;
        }
        if (scene->edit_actor.slot && !changed && !reverted && !field_active &&
            !scene->dragging_actor && scene->edit_field == field_index) {
            finish_transform_edit(scene, false);
        }
    }
    if (!changed && !reverted) {
        if (pending && !field_active) {
            finish_property_edit(scene);
        }
        return false;
    }
    if (reverted) {
        value = inherited;
    }
    if (!editable_scalar(&value)) {
        return false;
    }
    if (!position_field && !pending) {
        finish_transform_edit(scene, false);
        scene->property_actor = actor->entity;
        scene->property_edit = (scene_field_edit){ .component = component->desc->id,
                                                   .field = field_index,
                                                   .before = before,
                                                   .after = before };
        scene->property_dirty = scene->scene_dirty;
        scene->property_seen = true;
    }
    int result = ecs_component_write_field(
        scene->world,
        actor->entity,
        component->desc->id,
        (int)(field - component->desc->type->fields),
        value
    );
    if (result == ECS_OK) {
        scene->scene_dirty = true;
        if (!position_field) {
            scene->property_edit.after = value;
        }
    }
    if (position_field && !field_active) {
        finish_transform_edit(scene, false);
    }
    if (!position_field && !field_active) {
        finish_property_edit(scene);
    }
    return result == ECS_OK;
}

static void draw_instance_components(app_scene_authoring *scene, scene_actor *actor)
{
    ImGui_SeparatorText("Components");
    ImGui_BeginDisabled(!scene_can_move(scene));
    if (ImGui_Button("Revert All")) {
        finish_transform_edit(scene, false);
        int result = revert_instance_components(scene, actor);
        scene->scene_dirty |= result > 0;
        if (result < 0) {
            snprintf(
                scene->message,
                sizeof scene->message,
                "Could not revert %s.",
                actor->name
            );
        } else if (result > 0) {
            snprintf(
                scene->message,
                sizeof scene->message,
                "Reverted %s to prototype defaults.",
                actor->name
            );
        }
    }
    editor_ui_track_item("instance.revert_all");
    ImGui_EndDisabled();
    for (int i = 0; i < scene->registry.component_count; i++) {
        const ecs_component_desc *desc = scene->registry.components[i];
        if (!ecs_component_has(scene->world, actor->entity, desc->id)) {
            continue;
        }
        game_prototype_component added = { .desc = desc };
        const game_prototype_component *component = &added;
        for (int j = 0; j < acount(actor->base->components); j++) {
            if (actor->base->components[j].desc->id == desc->id) {
                component = &actor->base->components[j];
                break;
            }
        }
        ImGui_PushIDInt(i);
        bool open = component_tree(component->desc);
        char component_id[192];
        snprintf(
            component_id,
            sizeof component_id,
            "instance.component.%s",
            component->desc->name
        );
        editor_ui_track_item(component_id);
        if (open) {
            bool material = GAME_GameMaterial_has(scene->world, actor->entity);
            if (material && component->desc->id == GAME_COMPONENT_GameAppearance.id) {
                ImGui_TextDisabled("Colors are in Material.");
            }
            for (int group = 0; group < 2; group++) {
                if (!begin_property_group(component->desc, group, "instance.material.advanced")) {
                    continue;
                }
                for (int field = 0; field < component->desc->type->field_count;
                     field++) {
                    if (advanced_material_field(
                            component->desc,
                            &component->desc->type->fields[field]
                        ) != (group == 1)) {
                        continue;
                    }
                    if (material && fallback_appearance_field(
                                        component,
                                        &component->desc->type->fields[field]
                                    )) {
                        continue;
                    }
                    ImGui_PushIDInt(field);
                    draw_instance_field(
                        scene,
                        actor,
                        component,
                        &component->desc->type->fields[field]
                    );
                    ImGui_PopID();
                }
                if (group) {
                    ImGui_TreePop();
                }
            }
            ImGui_BeginDisabled(scene->runtime_snapshot_valid);
            if (ImGui_Button("Remove Component")) {
                app_scene_authoring_set_instance_component(scene, desc->name, false);
            }
            char remove_id[192];
            snprintf(remove_id, sizeof remove_id, "instance.component.remove.%s", desc->name);
            editor_ui_track_item(remove_id);
            ImGui_EndDisabled();
            ImGui_TreePop();
        }
        ImGui_PopID();
    }
    ImGui_BeginDisabled(scene->runtime_snapshot_valid);
    if (ImGui_Button("Add Component")) {
        ImGui_OpenPopup("instance_add_component", 0);
    }
    editor_ui_track_item("instance.component.add");
    if (ImGui_BeginPopup("instance_add_component", 0)) {
        for (int i = 0; i < scene->registry.component_count; i++) {
            const ecs_component_desc *desc = scene->registry.components[i];
            if (ecs_component_has(scene->world, actor->entity, desc->id)) {
                continue;
            }
            char label[128], id[192];
            editor_component_label(desc->name, label, sizeof label);
            if (ImGui_MenuItem(label)) {
                app_scene_authoring_set_instance_component(scene, desc->name, true);
            }
            snprintf(id, sizeof id, "instance.component.attach.%s", desc->name);
            editor_ui_track_item(id);
        }
        ImGui_EndPopup();
    }
    ImGui_EndDisabled();
}

static void draw_runtime_components(app_scene_authoring *scene, ecs_entity entity)
{
    ImGui_SeparatorText("Components");
    for (int i = 0; i < scene->registry.component_count; i++) {
        const ecs_component_desc *desc = scene->registry.components[i];
        if (!ecs_component_has(scene->world, entity, desc->id) || !desc->type) {
            continue;
        }
        ImGui_PushID(desc->name);
        if (component_tree(desc)) {
            for (int group = 0; group < 2; group++) {
                if (!begin_property_group(desc, group, "runtime.material.advanced")) {
                    continue;
                }
                for (int j = 0; j < desc->type->field_count; j++) {
                    if (advanced_material_field(desc, &desc->type->fields[j]) !=
                        (group == 1)) {
                        continue;
                    }
                    pin_value value = { 0 };
                    if (ecs_component_read_field(scene->world, entity, desc->id, j, &value) !=
                        ECS_OK) {
                        continue;
                    }
                    char name[128];
                    editor_identifier_label(desc->type->fields[j].name, name, sizeof name);
                    ImGui_Text("%s: %s", name, pin_value_to_string(&value));
                    char id[192];
                    snprintf(
                        id,
                        sizeof id,
                        "runtime.field.%s.%s",
                        desc->name,
                        desc->type->fields[j].name
                    );
                    editor_ui_track_item(id);
                }
                if (group) {
                    ImGui_TreePop();
                }
            }
            ImGui_TreePop();
        }
        ImGui_PopID();
    }
}

static void draw_instance_behaviors(app_scene_authoring *scene, scene_actor *actor)
{
    ImGui_SeparatorText("Behaviors");
    char **names = instance_behavior_names(scene, actor->entity);
    for (int i = 0; i < acount(names); i++) {
        ImGui_PushID(names[i]);
        char label[192], id[256];
        editor_identifier_label(names[i], label, sizeof label);
        float width = fmaxf(1, ImGui_GetContentRegionAvail().x - 32);
        ImGui_AlignTextToFramePadding();
        if (ImGui_SelectableEx(label, false, 0, (ImVec2){ width, 0 })) {
            app_scene_authoring_open_behavior(scene, names[i]);
        }
        snprintf(id, sizeof id, "instance.behavior.open.%s", names[i]);
        editor_ui_track_item(id);
        if (ImGui_IsItemHovered(0)) {
            ImGui_SetTooltip("Open %s", names[i]);
        }
        ImGui_SameLineEx(0, 4);
        ImGui_BeginDisabled(scene->runtime_snapshot_valid);
        if (editor_ui_icon_button("remove_behavior", 0xf00d, true)) {
            app_scene_authoring_set_instance_behavior(scene, names[i], false);
        }
        snprintf(id, sizeof id, "instance.behavior.remove.%s", names[i]);
        editor_ui_track_item(id);
        if (ImGui_IsItemHovered(0)) {
            ImGui_SetTooltip("Remove behavior from this instance");
        }
        ImGui_EndDisabled();
        ImGui_PopID();
    }
    free_behavior_names(names);
    ImGui_BeginDisabled(scene->runtime_snapshot_valid);
    if (ImGui_Button("Add Behavior")) {
        ImGui_OpenPopup("instance_add_behavior", 0);
    }
    editor_ui_track_item("instance.behavior.add");
    if (ImGui_BeginPopup("instance_add_behavior", 0)) {
        game_behavior_world *world = scene->live->behaviors;
        int available = 0;
        for (int i = 0; i < game_behavior_asset_count(world); i++) {
            const char *name = game_behavior_asset_name_at(world, i);
            if (game_behavior_instance(world, actor->entity, name)) {
                continue;
            }
            available++;
            if (ImGui_MenuItem(name)) {
                app_scene_authoring_set_instance_behavior(scene, name, true);
            }
            char id[256];
            snprintf(id, sizeof id, "instance.behavior.attach.%s", name);
            editor_ui_track_item(id);
        }
        if (!available) {
            ImGui_TextDisabled("All behaviors are attached.");
        }
        ImGui_EndPopup();
    }
    ImGui_EndDisabled();
}

static void draw_scene_inspector(app_scene_authoring *scene)
{
    if (!editor_ui_pane_visible(EDITOR_UI_PANE_INSPECTOR)) {
        return;
    }
    if (editor_ui_take_focus(EDITOR_UI_FOCUS_INSPECTOR)) {
        ImGui_SetNextWindowFocus();
    }
    if (!editor_ui_begin_pane(EDITOR_UI_PANE_INSPECTOR, "Inspector", 0)) {
        ImGui_End();
        return;
    }

    scene_actor *actor = selected_actor(scene);
    ecs_entity runtime_entity = scene->runtime_selected.slot
                                  ? selected_scene_entity(scene)
                                  : (ecs_entity){ 0 };
    if (!actor && !runtime_entity.slot) {
        ImGui_TextUnformatted("No Selection");
        ImGui_End();
        return;
    }
    if (runtime_entity.slot) {
        inspector_title(runtime_entity_label(scene, runtime_entity));
        editor_ui_track_item("inspector.spawned");
        ImGui_TextDisabled("Runtime instance · Read-only");
        if (ImGui_IsItemHovered(0)) {
            ImGui_SetTooltip("Created during Play. This instance is not saved with the scene.");
        }
        draw_runtime_components(scene, runtime_entity);
    } else {
        inspector_title(actor->name);
        editor_ui_track_item("inspector.scene");
        int selection_count = app_scene_authoring_selection_count(scene);
        if (selection_count > 1) {
            ImGui_TextDisabled(
                "%d selected · Showing %s",
                selection_count,
                actor->name
            );
        }
        ImGui_TextDisabled("%s instance", actor->prototype_name);
        if (scene->runtime_snapshot_valid) {
            ImGui_TextDisabled("Simulation");
            if (ImGui_IsItemHovered(0)) {
                ImGui_SetTooltip("Save preserves the starting scene, including actors destroyed during Play.");
            }
        }
        draw_instance_components(scene, actor);
        draw_instance_behaviors(scene, actor);
    }
    game_behavior_attachment_info info;
    int has_attachment = scene->document.kind == APP_EDITOR_DOCUMENT_BEHAVIOR
                           ? selected_behavior_attachment(scene, &info)
                           : selected_attachment(scene, &info);
    if (!has_attachment) {
        ImGui_End();
        return;
    }
    ImGui_SeparatorText("Runtime");

    game_behavior_world *behaviors = scene->live->behaviors;
    int attachment_count = game_behavior_attachment_count_for_entity(behaviors, info.entity);
    if (ImGui_Button(info.asset_name)) {
        ImGui_OpenPopup("runtime_behavior_popup", 0);
    }
    if (ImGui_BeginPopup("runtime_behavior_popup", 0)) {
        for (int i = 0; i < attachment_count; i++) {
            game_behavior_attachment_info candidate;
            if (game_behavior_attachment_info_at(behaviors, info.entity, i, &candidate) !=
                0) {
                continue;
            }
            if (ImGui_SelectableEx(
                    candidate.asset_name,
                    i == scene->selected_attachment,
                    0,
                    (ImVec2){ 0.0f, 0.0f }
                )) {
                scene->selected_attachment = i;
                ImGui_CloseCurrentPopup();
            }
        }
        ImGui_EndPopup();
    }

    flow_run *run = NULL;
    for (int i = acount(info.instance->activations) - 1; i >= 0; i--) {
        flow_state state = flow_run_status(info.instance->activations[i]);
        if (state != FLOW_COMPLETED && state != FLOW_FAILED) {
            run = info.instance->activations[i];
            break;
        }
    }

    char line[256];
    if (run) {
        snprintf(line, sizeof line, "State: %s", inspection_state_name(flow_run_status(run)));
        ImGui_TextUnformatted(line);
        snprintf(line, sizeof line, "Steps: %lu", flow_run_steps(run));
        ImGui_TextUnformatted(line);
        const node *current = flow_run_current_node(run);
        snprintf(line, sizeof line, "Current node: %s", current ? current->desc->name : "(none)");
        ImGui_TextUnformatted(line);
        if (flow_run_status(run) == FLOW_RUNNING ||
            flow_run_status(run) == FLOW_SUSPENDED ||
            flow_run_status(run) == FLOW_PAUSED) {
            if (ImGui_Button("Pause")) {
                flow_run_pause(run);
            }
            ImGui_SameLine();
            if (ImGui_Button("Step")) {
                flow_run_step(run);
            }
            ImGui_SameLine();
            if (ImGui_Button("Resume")) {
                flow_run_resume(run);
            }
        }
        if (ImGui_Button("Stop")) {
            flow_run_stop(run);
        }
        const flow_error *error = flow_run_error(run);
        if (error && error->message) {
            snprintf(line, sizeof line, "Error: %s", error->message);
            ImGui_TextUnformatted(line);
        }
        int stack_count = 0;
        const flow_call_frame *stack = flow_run_call_stack(run, &stack_count);
        ImGui_Separator();
        snprintf(line, sizeof line, "Call stack: %d", stack_count);
        ImGui_TextUnformatted(line);
        for (int i = 0; i < stack_count && i < 8; i++) {
            snprintf(
                line,
                sizeof line,
                "  %d: node %llu",
                i,
                (unsigned long long)stack[i].node_id
            );
            ImGui_TextUnformatted(line);
        }
    } else {
        ImGui_TextDisabled("Idle");
    }

    int trace_count = 0;
    const behavior_instance_trace_entry *trace = behavior_instance_trace(info.instance, &trace_count);
    ImGui_Separator();
    if (!ImGui_TreeNodeEx("Execution trace", 0)) {
        ImGui_End();
        return;
    }
    snprintf(line, sizeof line, "Execution trace: %d", trace_count);
    ImGui_TextUnformatted(line);
    for (int i = trace_count - 1; i >= 0 && i >= trace_count - 12; i--) {
        const behavior_instance_trace_entry *entry = &trace[i];
        const flow_traversal *traversal = &entry->traversal;
        snprintf(
            line,
            sizeof line,
            "%s  %s %llu -> %llu",
            entry->event_name[0] ? entry->event_name : "(event)",
            traversal->kind == GRAPH_EDGE_EXEC ? "EXEC" : "DATA",
            (unsigned long long)traversal->from_id,
            (unsigned long long)traversal->to_id
        );
        ImGui_TextUnformatted(line);
    }
    ImGui_TreePop();
    ImGui_End();
}

static bool draw_content_tree(app_scene_authoring *scene, const char *parent)
{
    bool searching = scene->asset_search[0] != '\0';
    bool any = false;
    for (int i = 0; i < acount(scene->content); i++) {
        scene_content_entry *entry = &scene->content[i];
        if (searching) {
            if (entry->directory || asset_search_score(scene, entry->path) < 0) {
                continue;
            }
        } else if (!content_path_is_child(parent, entry->path)) {
            continue;
        }
        any = true;
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth |
                                   ImGuiTreeNodeFlags_FramePadding;
        if (!entry->directory) {
            flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        }
        if (scene->content_selected_path &&
            strcmp(scene->content_selected_path, entry->path) == 0) {
            flags |= ImGuiTreeNodeFlags_Selected;
        }
        ImGui_PushID(entry->path);
        int prototype_index = content_prototype_index(scene, entry->path);
        const char *icon = entry->directory     ? "\xef\x81\xbb"
                         : prototype_index >= 0 ? "\xef\x86\xb2"
                                                : "\xef\x85\x9b";
        sidebar_row row = sidebar_row_begin();
        float icon_inset = ImGui_GetTreeNodeToLabelSpacing();
        ImGui_PushStyleVarImVec2(ImGuiStyleVar_FramePadding, (ImVec2){ 6.0f, 6.0f });
        bool open = ImGui_TreeNodeEx("##file_row", flags);
        ImGui_PopStyleVar();
        sidebar_row_end(
            row,
            (flags & ImGuiTreeNodeFlags_Selected) != 0,
            icon,
            searching ? entry->path : content_basename(entry->path),
            icon_inset
        );
        editor_ui_track_item(entry->path);
        if (ImGui_IsItemClicked()) {
            select_content_entry(scene, entry);
        }
        if (!entry->directory && ImGui_IsItemHovered(0) &&
            ImGui_IsMouseDoubleClicked(0)) {
            open_content_entry(scene, entry);
        }
        if (prototype_index >= 0 && ImGui_BeginDragDropSource(0)) {
            const char *name = scene->prototypes[prototype_index].prototype->name;
            ImGui_SetDragDropPayload("CBLUEPRINT_PROTOTYPE", name, strlen(name) + 1, 0);
            ImGui_Text("Place %s in Scene", name);
            ImGui_EndDragDropSource();
        }
        if (!entry->directory && ImGui_BeginPopupContextItemEx("asset_actions", 1)) {
            if (ImGui_MenuItem("Open")) {
                open_content_entry(scene, entry);
            }
            if (prototype_index >= 0 && ImGui_MenuItem("Add to Scene")) {
                app_scene_authoring_add_instance(
                    scene,
                    scene->prototypes[prototype_index].prototype->name
                );
            }
            ImGui_EndPopup();
        }
        if (entry->directory && open) {
            draw_content_tree(scene, entry->path);
            ImGui_TreePop();
        }
        ImGui_PopID();
    }
    return any;
}

static void draw_files(app_scene_authoring *scene)
{
    double now = ImGui_GetTime();
    if (now - scene->content_scan_time > 1.0) {
        content_refresh(scene);
        scene->content_scan_time = now;
    }
    if (editor_ui_icon_button("create_asset", 0xf067, true)) {
        ImGui_OpenPopup("create_asset", 0);
    }
    editor_ui_track_item("content.create");
    if (ImGui_IsItemHovered(0)) {
        ImGui_SetTooltip("Create asset");
    }
    if (ImGui_BeginPopup("create_asset", 0)) {
        if (ImGui_MenuItem("Behavior")) {
            create_behavior_asset(scene);
        }
        editor_ui_track_item("content.new_behavior");
        if (ImGui_MenuItem("Prototype")) {
            create_new_prototype(scene);
        }
        editor_ui_track_item("content.new_prototype");
        ImGui_EndPopup();
    }
    ImGui_SameLine();
    ImGui_SetNextItemWidth(ImGui_GetContentRegionAvail().x - 34.0f);
    editor_ui_search_field("content_search", scene->asset_search, sizeof scene->asset_search, "Search files...");
    editor_ui_track_item("content.search");
    ImGui_SameLine();
    if (editor_ui_icon_button("clear_search", 0xf00d, scene->asset_search[0] != '\0')) {
        scene->asset_search[0] = '\0';
    }
    editor_ui_track_item("content.clear_search");
    ImGui_Separator();
    if (ImGui_BeginChild("content_tree", (ImVec2){ 0.0f, 0.0f }, 0, 0)) {
        if (!draw_content_tree(scene, scene->content_root)) {
            ImGui_TextUnformatted(scene->asset_search[0] ? "No Results" : "No Files");
            editor_ui_track_item("content.empty");
        }
    }
    ImGui_EndChild();
}

static void draw_prototype_outline(app_scene_authoring *scene)
{
    scene_prototype_asset *asset = selected_prototype(scene);
    if (!asset) {
        ImGui_TextDisabled("No Selection");
        return;
    }
    ImGui_TextUnformatted(asset->prototype->name);
    ImGui_SeparatorText("Components");
    for (int i = 0; i < acount(asset->prototype->components); i++) {
        const ecs_component_desc *desc = asset->prototype->components[i].desc;
        ImGui_TextUnformatted(desc->key ? desc->key : desc->name);
    }
    ImGui_SeparatorText("Behaviors");
    for (int i = 0; i < acount(asset->prototype->graphs); i++) {
        const char *name = asset->prototype->graphs[i];
        if (ImGui_SelectableEx(name, false, 0, (ImVec2){ 0.0f, 0.0f })) {
            focus_prototype_behavior(scene, name);
        }
    }
}

static void draw_navigator(app_scene_authoring *scene)
{
    if (editor_ui_take_focus(EDITOR_UI_FOCUS_WORLD_OUTLINER)) {
        scene->navigator_scene = true;
        editor_ui_request_focus(EDITOR_UI_FOCUS_WORLD_OUTLINER);
    }
    if (editor_ui_take_focus(EDITOR_UI_FOCUS_MY_BEHAVIOR_ASSET)) {
        scene->navigator_scene = false;
        editor_ui_request_focus(EDITOR_UI_FOCUS_MY_BEHAVIOR_ASSET);
    }
    bool has_document = scene->document.kind != APP_EDITOR_DOCUMENT_SCENE ||
                        scene->graph_document_id != NULL;
    editor_ui_pane outline = scene->navigator_scene || !has_document
                               ? EDITOR_UI_PANE_WORLD_OUTLINER
                               : EDITOR_UI_PANE_MY_BEHAVIOR_ASSET;
    editor_ui_pane selected;
    if (!editor_ui_begin_navigator(&outline, has_document, &selected)) {
        return;
    }
    scene->navigator_scene = outline == EDITOR_UI_PANE_WORLD_OUTLINER;
    if (selected == EDITOR_UI_PANE_CONTENT) {
        draw_files(scene);
        editor_ui_end_navigator();
        return;
    }
    if (scene->navigator_scene || !has_document) {
        draw_scene_outline(scene);
    } else if (scene->document.kind == APP_EDITOR_DOCUMENT_PROTOTYPE) {
        draw_prototype_outline(scene);
    } else {
        const behavior_asset *asset = editor_behavior_asset(scene->graph_editor);
        ImGui_TextUnformatted(asset->name);
        editor_ui_track_item("navigator.document.outline");
        editor_draw_outline(scene->graph_editor);
    }
    editor_ui_end_navigator();
}

static int save_system_document(app_scene_authoring *scene, const char *path)
{
    char *graph_json = behavior_asset_save_json(editor_behavior_asset(scene->graph_editor));
    behavior_asset *graph = graph_json ? behavior_asset_load_json(graph_json) : NULL;
    free(graph_json);
    if (!graph ||
        game_system_document_replace_behavior_asset(scene->system_document, graph) != 0) {
        behavior_asset_free(graph);
        return -1;
    }
    char *json = game_system_document_save(
        scene->system_document,
        resolve_system_component,
        system_component_name,
        NULL
    );
    if (!json) {
        return -1;
    }
    int result = write_text_file_atomic(path, json);
    free(json);
    if (result == 0) {
        char *saved_path = xstrdup(path);
        free(scene->system_path);
        scene->system_path = saved_path;
    }
    return result;
}

static int save_as_active_document(app_scene_authoring *scene)
{
    char *path = document_default_path(scene);
    if (!path) {
        snprintf(scene->message, sizeof scene->message, "Could not choose a Save As path.");
        return -1;
    }
    int result = -1;
    if (scene->document.kind == APP_EDITOR_DOCUMENT_SYSTEM) {
        result = scene->system_document ? save_system_document(scene, path) : -1;
    } else if (scene->document.kind == APP_EDITOR_DOCUMENT_PROTOTYPE) {
        scene_prototype_asset *asset = selected_prototype(scene);
        result = asset
                   ? app_scene_authoring_save_prototype(scene, asset->prototype->name, path)
                   : -1;
    } else if (scene->document.kind == APP_EDITOR_DOCUMENT_BEHAVIOR) {
        game_live_definition *definition = scene->definition;
        int synced = definition ? sync_behavior_working(scene) : -1;
        int saved = synced == 0 ? save_behavior_definition(scene, definition, path)
                                : -1;
        int applied = saved == 0 ? game_live_preview(
                                       scene->live,
                                       definition,
                                       scene->preview_message,
                                       sizeof scene->preview_message
                                   )
                                 : -1;
        result = saved;
        if (saved == 0 && applied != 0) {
            snprintf(
                scene->message,
                sizeof scene->message,
                "Saved %s; apply blocked, previous runtime remains active.",
                definition->name
            );
        } else if (saved == 0) {
            snprintf(
                scene->message,
                sizeof scene->message,
                "Saved %s.",
                definition->name
            );
        }
    } else {
        result = app_scene_authoring_save(scene, path);
    }
    free(path);
    return result;
}

static int save_active_document(app_scene_authoring *scene)
{
    if (scene->document.kind == APP_EDITOR_DOCUMENT_SYSTEM) {
        if (!scene->system_document || !scene->system_path) {
            snprintf(scene->message, sizeof scene->message, "System requires Save As.");
            return -1;
        }
        return save_system_document(scene, scene->system_path);
    }
    if (scene->document.kind == APP_EDITOR_DOCUMENT_PROTOTYPE) {
        scene_prototype_asset *asset = selected_prototype(scene);
        if (!asset || !asset->path) {
            snprintf(scene->message, sizeof scene->message, "Prototype requires Save As.");
            return -1;
        }
        return app_scene_authoring_save_prototype(
            scene,
            asset->prototype->name,
            asset->path
        );
    }
    if (scene->document.kind == APP_EDITOR_DOCUMENT_BEHAVIOR) {
        if (!scene->definition) {
            return -1;
        }
        int synced = sync_behavior_working(scene);
        int saved = synced == 0
                      ? save_behavior_definition(scene, scene->definition, NULL)
                      : -1;
        int applied = saved == 0 ? game_live_preview(
                                       scene->live,
                                       scene->definition,
                                       scene->preview_message,
                                       sizeof scene->preview_message
                                   )
                                 : -1;
        if (saved == 0 && applied != 0) {
            snprintf(
                scene->message,
                sizeof scene->message,
                "Saved %s; apply blocked, previous runtime remains active.",
                scene->definition->name
            );
        } else if (saved == 0) {
            snprintf(
                scene->message,
                sizeof scene->message,
                "Saved %s.",
                scene->definition->name
            );
        }
        return saved;
    }
    if (!scene->scene_path) {
        snprintf(scene->message, sizeof scene->message, "Scene requires Save As.");
        return -1;
    }
    return app_scene_authoring_save(scene, scene->scene_path);
}

static int save_all_documents(app_scene_authoring *scene)
{
    int result = 0;
    int apply_blocked = 0;
    if (scene->document.kind == APP_EDITOR_DOCUMENT_BEHAVIOR) {
        apply_blocked = sync_behavior_working(scene) != 0;
    }
    if (scene->document.kind == APP_EDITOR_DOCUMENT_SYSTEM &&
        (!scene->system_document || !scene->system_path ||
         save_system_document(scene, scene->system_path) != 0)) {
        result = -1;
    }
    for (int i = 0; i < acount(scene->prototypes); i++) {
        scene_prototype_asset *asset = &scene->prototypes[i];
        if (!asset->dirty) {
            continue;
        }
        if (!asset->path || app_scene_authoring_save_prototype(
                                scene,
                                asset->prototype->name,
                                asset->path
                            ) != 0) {
            result = -1;
        }
    }
    for (int i = 0; i < game_live_definition_count(scene->live); i++) {
        game_live_definition *definition = game_live_definition_at(scene->live, i);
        if (!definition || !definition->unsaved) {
            continue;
        }
        if (save_behavior_definition(scene, definition, NULL) != 0) {
            result = -1;
            continue;
        }
        if (game_live_preview(
                scene->live,
                definition,
                scene->preview_message,
                sizeof scene->preview_message
            ) != 0) {
            apply_blocked = 1;
        }
    }
    if (scene->scene_dirty) {
        if (!scene->scene_path ||
            app_scene_authoring_save(scene, scene->scene_path) != 0) {
            result = -1;
        }
    }
    if (apply_blocked) {
        snprintf(scene->message, sizeof scene->message, "Saved authored data; one or more behavior applies are blocked.");
    } else if (result == 0) {
        snprintf(scene->message, sizeof scene->message, "Saved all dirty documents.");
    }
    return result;
}

int app_scene_authoring_save_as(app_scene_authoring *scene)
{
    return scene ? save_as_active_document(scene) : -1;
}

int app_scene_authoring_reload(app_scene_authoring *scene)
{
    return scene ? reload_active_document(scene) : -1;
}

static int app_editor_save(editor *, void *user)
{
    return save_active_document((app_scene_authoring *)user);
}

static int replay_scene_edit_one(app_scene_authoring *scene, bool redo)
{
    if (!scene_can_move(scene)) {
        return -1;
    }
    finish_transform_edit(scene, false);
    ecs_entity entity;
    bool present;
    const scene_actor_edit
        *actor_edit = scene_history_peek_actor(scene->history, redo, &entity, &present);
    if (actor_edit) {
        ecs_entity replacement = { 0 };
        int result = -1;
        if (present) {
            if (!ecs_entity_is_alive(scene->world, entity)) {
                result = restore_actor(scene, actor_edit, &replacement);
            }
        } else {
            for (int i = 0; i < acount(scene->actors); i++) {
                if (ecs_entity_to_u64(scene->actors[i].entity) ==
                    ecs_entity_to_u64(entity)) {
                    result = remove_actor_at(scene, i);
                    break;
                }
            }
        }
        if (result != 0) {
            return -1;
        }
        scene_history_complete_actor(scene->history, redo, replacement);
        scene->scene_dirty = true;
        return 0;
    }
    const scene_component_edit *
        component_edit = scene_history_peek_component(scene->history, redo, &entity, &present);
    if (component_edit) {
        if (scene->runtime_snapshot_valid ||
            !ecs_entity_is_alive(scene->world, entity) ||
            ecs_component_has(scene->world, entity, component_edit->component) == present) {
            return -1;
        }
        int result = 0;
        if (present) {
            result = apply_components_json(scene, entity, component_edit->state_json);
        } else {
            result = ecs_world_record_remove(
                scene->world,
                entity,
                component_edit->component,
                0
            );
            if (result == ECS_OK) {
                result = ecs_world_apply_commands(scene->world);
            }
        }
        if (result != 0) {
            return -1;
        }
        scene_history_complete_component(scene->history, redo);
        scene->scene_dirty = true;
        for (int i = 0; i < acount(scene->actors); i++) {
            if (ecs_entity_to_u64(scene->actors[i].entity) == ecs_entity_to_u64(entity)) {
                select_instance(scene, i);
                break;
            }
        }
        return 0;
    }
    const scene_behavior_edit
        *behavior_edit = scene_history_peek_behaviors(scene->history, redo, &entity);
    if (behavior_edit) {
        if (scene->runtime_snapshot_valid) {
            snprintf(scene->message, sizeof scene->message, "Stop the scene before changing behavior attachments.");
            return -1;
        }
        const char *const *names = redo ? behavior_edit->after : behavior_edit->before;
        int count = redo ? behavior_edit->after_count : behavior_edit->before_count;
        if (game_behavior_set_attachments(scene->live->behaviors, entity, names, count) !=
            0) {
            return -1;
        }
        scene_history_complete_behaviors(scene->history, redo);
        scene->scene_dirty = true;
        for (int i = 0; i < acount(scene->actors); i++) {
            if (ecs_entity_to_u64(scene->actors[i].entity) == ecs_entity_to_u64(entity)) {
                select_instance(scene, i);
                break;
            }
        }
        scene->selected_attachment = -1;
        sync_editor_inspection(scene);
        return 0;
    }
    int result = redo ? scene_history_redo(scene->history, scene->world, &entity)
                      : scene_history_undo(scene->history, scene->world, &entity);
    if (result > 0) {
        scene->scene_dirty = true;
        for (int i = 0; i < acount(scene->actors); i++) {
            if (ecs_entity_to_u64(scene->actors[i].entity) == ecs_entity_to_u64(entity)) {
                select_instance(scene, i);
                break;
            }
        }
    }
    return result < 0 ? -1 : 0;
}

static int replay_scene_edit(app_scene_authoring *scene, bool redo)
{
    uint64_t group = scene_history_group_id(scene->history, redo);
    int applied = 0;
    do {
        if (replay_scene_edit_one(scene, redo) != 0) {
            while (applied-- > 0) {
                int restored = replay_scene_edit_one(scene, !redo);
                assert(restored == 0);
            }
            return -1;
        }
        applied++;
    } while (group && scene_history_group_id(scene->history, redo) == group);
    return 0;
}

static int app_ui_command(editor_ui_command command, void *user)
{
    app_scene_authoring *scene = (app_scene_authoring *)user;
    if (!scene) {
        return -1;
    }
    switch (command) {
        case EDITOR_UI_COMMAND_RUNTIME_PLAY_PAUSE: {
            app_runtime_state state = app_scene_authoring_runtime_state(scene);
            return app_scene_authoring_runtime_command(
                scene,
                state.target,
                state.playing ? APP_RUNTIME_PAUSE : APP_RUNTIME_PLAY
            );
        }
        case EDITOR_UI_COMMAND_RUNTIME_STOP:
            return app_scene_authoring_runtime_command(scene, scene->active_runtime_target, APP_RUNTIME_STOP);
        case EDITOR_UI_COMMAND_RUNTIME_STEP:
            return app_scene_authoring_runtime_command(scene, scene->active_runtime_target, APP_RUNTIME_STEP);
        case EDITOR_UI_COMMAND_RESTART_PREVIEW: {
            int result = app_scene_authoring_apply_pending(scene);
            editor_ui_set_preview_status(result == 0 ? "" : "Graph error", scene->message, false);
            return result;
        }
        case EDITOR_UI_COMMAND_SAVE: return save_active_document(scene);
        case EDITOR_UI_COMMAND_SAVE_AS: return save_as_active_document(scene);
        case EDITOR_UI_COMMAND_SAVE_ALL: return save_all_documents(scene);
        case EDITOR_UI_COMMAND_RELOAD: return reload_active_document(scene);
        case EDITOR_UI_COMMAND_UNDO:
            if (scene->document.kind == APP_EDITOR_DOCUMENT_SCENE) {
                return replay_scene_edit(scene, false);
            }
            if (scene->document.kind == APP_EDITOR_DOCUMENT_PROTOTYPE) {
                prototype_undo(scene);
            } else {
                editor_undo(scene->graph_editor);
            }
            return 0;
        case EDITOR_UI_COMMAND_REDO:
            if (scene->document.kind == APP_EDITOR_DOCUMENT_SCENE) {
                return replay_scene_edit(scene, true);
            }
            if (scene->document.kind == APP_EDITOR_DOCUMENT_PROTOTYPE) {
                prototype_redo(scene);
            } else {
                editor_redo(scene->graph_editor);
            }
            return 0;
        case EDITOR_UI_COMMAND_NEW_PROTOTYPE:
            return create_new_prototype(scene);
        case EDITOR_UI_COMMAND_NEW_BEHAVIOR:
            return create_behavior_asset(scene);
        case EDITOR_UI_COMMAND_FOCUS_SCENE:
            editor_ui_request_focus(EDITOR_UI_FOCUS_SCENE);
            return 0;
        case EDITOR_UI_COMMAND_FOCUS_MY_BEHAVIOR_ASSET:
            editor_ui_request_focus(EDITOR_UI_FOCUS_MY_BEHAVIOR_ASSET);
            return 0;
        case EDITOR_UI_COMMAND_FOCUS_INSPECTOR:
            editor_ui_request_focus(EDITOR_UI_FOCUS_INSPECTOR);
            return 0;
        case EDITOR_UI_COMMAND_FOCUS_COMPILER_RESULTS:
            editor_ui_request_focus(EDITOR_UI_FOCUS_COMPILER_RESULTS);
            return 0;
        case EDITOR_UI_COMMAND_FOCUS_OUTPUT_LOG:
            editor_ui_request_focus(EDITOR_UI_FOCUS_OUTPUT_LOG);
            return 0;
        case EDITOR_UI_COMMAND_REFRESH_CONTENT:
            app_scene_authoring_refresh_content(scene);
            return 0;
    }
    return -1;
}

void app_scene_authoring_draw(app_scene_authoring *scene)
{
    if (!scene) {
        return;
    }
    if (scene->runtime_selected.slot &&
        !game_entities_owns(scene->live->entities, scene->runtime_selected)) {
        scene->runtime_selected = (ecs_entity){ 0 };
        scene->selected_attachment = -1;
        sync_editor_inspection(scene);
    }
    editor_ui_pane interaction = editor_ui_take_document_interaction();
    if (interaction == EDITOR_UI_PANE_NODE_CANVAS && scene->graph_document_id) {
        set_document(scene, scene->graph_document_kind, scene->graph_document_id);
    }
    if (interaction == EDITOR_UI_PANE_SCENE) {
        set_document(scene, APP_EDITOR_DOCUMENT_SCENE, APP_SCENE_DOCUMENT_ID);
    }
    app_runtime_state transport = app_scene_authoring_runtime_state(scene);
    editor_ui_set_runtime_state(
        transport.playing,
        transport.target == APP_RUNTIME_PREVIEW ? previewed_prototype(scene) != NULL
                                                : scene->runtime_snapshot_valid
    );
    scene->property_seen = false;
    draw_navigator(scene);
    switch (scene->document.kind) {
        case APP_EDITOR_DOCUMENT_SCENE:
            editor_ui_set_history_available(
                scene_can_move(scene) &&
                    (scene_history_can_undo(scene->history) ||
                     (scene->property_actor.slot && !pin_value_equal(
                                                        &scene->property_edit.before,
                                                        &scene->property_edit.after
                                                    ))),
                scene_can_move(scene) && scene_history_can_redo(scene->history)
            );
            draw_scene_inspector(scene);
            if (!scene->property_seen) {
                finish_property_edit(scene);
            }
            sync_editor_inspection(scene);
            break;
        case APP_EDITOR_DOCUMENT_PROTOTYPE:
            editor_ui_set_history_available(
                acount(scene->prototype_undo) > 0,
                acount(scene->prototype_redo) > 0
            );
            draw_prototype_inspector(scene);
            editor_set_inspection_instance(scene->graph_editor, NULL);
            editor_set_inspection_run(scene->graph_editor, NULL);
            editor_set_inspection_trace(scene->graph_editor, NULL, 0);
            break;
        case APP_EDITOR_DOCUMENT_BEHAVIOR: {
            bool inspect_graph = editor_has_inspection_selection(scene->graph_editor) ||
                                 !selected_scene_entity(scene).slot;
            editor_set_inspector_active(scene->graph_editor, inspect_graph);
            if (!inspect_graph) {
                draw_scene_inspector(scene);
            }
            sync_editor_inspection(scene);
            break;
        }
        case APP_EDITOR_DOCUMENT_SYSTEM: sync_editor_inspection(scene); break;
    }
}

static void draw_particles(const game_particles *particles)
{
    const game_particle *items = game_particles_data(particles);
    for (int i = 0; i < game_particles_count(particles); i++) {
        const game_particle *p = &items[i];
        float remaining = 1.0f - p->age / p->lifetime;
        app_cf_color color = app_cf_make_color_rgb(p->color >> 24, p->color >> 16, p->color >> 8);
        color.a = (float)(p->color & 255) / 255.0f * remaining;
        float size = 1.0f + 3.0f * remaining;
        cf_draw_push_color(color);
        cf_draw_quad_fill(
            app_cf_make_aabb_pos_w_h(app_cf_make_v2(p->position.x, -p->position.y), size, size),
            0.0f
        );
        cf_draw_pop_color();
    }
}

static void draw_appearance(
    app_sdf_shapes shapes,
    const GameAppearance *appearance,
    const GameTransform *transform,
    float radians,
    const GameSdfMotion *motion
)
{
    app_cf_v2 center = { transform->x, -transform->y };
    float width = appearance->width * 0.5f;
    float height = appearance->height * 0.5f;
    float border = isfinite(appearance->border_width) && appearance->border_width > 0
                     ? appearance->border_width
                     : 0;
    float glow = isfinite(appearance->glow_radius) && appearance->glow_radius > 0
                   ? appearance->glow_radius
                   : 0;
    cf_draw_push_color(app_cf_make_color_rgba(appearance->color));
    cf_draw_push_interior_border(app_cf_make_color_rgba(appearance->border_color), border);
    cf_draw_push_glow(app_cf_make_color_rgba(appearance->glow_color), glow);
    if (motion && (motion->amplitude > 0 || motion->pluck_amplitude > 0.01f) &&
        app_sdf_shapes_draw(shapes, appearance, transform, radians, motion)) {
        cf_draw_pop_glow();
        cf_draw_pop_outline();
        cf_draw_pop_color();
        return;
    }
    switch (appearance->shape) {
        case GAME_SHAPE_CIRCLE:
            cf_draw_circle_fill2(center, width < height ? width : height);
            break;
        case GAME_SHAPE_CAPSULE: {
            float radius = fminf(width, height);
            CF_V2 offset = width >= height ? (CF_V2){ width - radius, 0 }
                                           : (CF_V2){ 0, height - radius };
            game_appearance_rotate_point(offset, radians, &offset);
            cf_draw_capsule_fill2(
                (app_cf_v2){ center.x - offset.x, center.y - offset.y },
                (app_cf_v2){ center.x + offset.x, center.y + offset.y },
                radius
            );
            break;
        }
        case GAME_SHAPE_DIAMOND:
        case GAME_SHAPE_CHEVRON:
        case GAME_SHAPE_STROKED_CHEVRON:
        default: {
            CF_V2 local[4];
            int count = game_appearance_points(appearance, local);
            if (!count) {
                local[0] = (CF_V2){ -width, -height };
                local[1] = (CF_V2){ width, -height };
                local[2] = (CF_V2){ width, height };
                local[3] = (CF_V2){ -width, height };
                count = 4;
            }
            app_cf_v2 points[4];
            for (int i = 0; i < count; i++) {
                game_appearance_rotate_point(local[i], radians, &local[i]);
                points[i] = (app_cf_v2){ center.x + local[i].x,
                                         center.y + local[i].y };
            }
            if (appearance->shape == GAME_SHAPE_STROKED_CHEVRON) {
                cf_draw_polyline(points, count, appearance->stroke_width, true);
            } else {
                cf_draw_polygon_fill(points, count, 0);
            }
            break;
        }
    }
    cf_draw_pop_glow();
    cf_draw_pop_outline();
    cf_draw_pop_color();
}

app_sdf_shapes app_scene_authoring_shapes(const app_scene_authoring *scene)
{
    return scene->shapes;
}

void app_scene_authoring_render(void *user)
{
    app_scene_authoring *scene = (app_scene_authoring *)user;
    if (!scene) {
        return;
    }
    ecs_world *world = scene->active_runtime_target == APP_RUNTIME_PREVIEW
                         ? game_prototype_preview_world(scene->prototype_preview)
                         : scene->world;
    const game_particles *particles = scene->active_runtime_target == APP_RUNTIME_PREVIEW
                                        ? game_prototype_preview_particles(scene->prototype_preview)
                                        : scene->live->particles;
    bool material_drawn = scene->render_viewport && app_scene_renderer_draw(
                                                        scene->renderer,
                                                        world,
                                                        particles,
                                                        scene->render_viewport
                                                    );
    ecs_component_id required[] = { GAME_COMPONENT_GameTransform.id,
                                    GAME_COMPONENT_GameAppearance.id };
    ecs_query query;
    if (ecs_query_begin(world, &query, required, 2) != ECS_OK) {
        return;
    }
    ecs_entity entity;
    while (ecs_query_next(&query, &entity)) {
        const GameTransform *transform = GAME_GameTransform_get_const(world, entity);
        const GameAppearance *appearance = GAME_GameAppearance_get_const(world, entity);
        app_cf_aabb bounds;
        float radians = actor_orientation(world, entity);
        const GameSdfMotion *motion = GAME_GameSdfMotion_get_const(world, entity);
        if (!appearance_bounds(transform, appearance, radians, motion, false, &bounds)) {
            continue;
        }
        float x = transform->x;
        float y = -transform->y;
        if (!material_drawn) {
            GameAppearance visible = *appearance;
            const GameMaterial *material = GAME_GameMaterial_get_const(world, entity);
            if (material) {
                visible.color = material->scene;
                visible.border_color = material->border_scene;
                visible.glow_radius = 0;
            }
            draw_appearance(scene->shapes, &visible, transform, radians, motion);
        }
        const GameHealth *health = GAME_GameHealth_get_const(world, entity);
        if (!health || health->current >= health->maximum) {
            continue;
        }
        float bar_y = y + appearance->height * 0.5f + 8.0f;
        float maximum = health->maximum > 0 ? (float)health->maximum : 1.0f;
        float ratio = (float)health->current / maximum;
        if (ratio < 0.0f) {
            ratio = 0.0f;
        }
        if (ratio > 1.0f) {
            ratio = 1.0f;
        }
        cf_draw_push_color(app_cf_make_color_rgb(40, 40, 40));
        cf_draw_quad_fill(app_cf_make_aabb_pos_w_h(app_cf_make_v2(x, bar_y), 36.0f, 5.0f), 0.0f);
        cf_draw_pop_color();
        cf_draw_push_color(app_cf_make_color_rgb(80, 210, 90));
        cf_draw_quad_fill(
            app_cf_make_aabb_pos_w_h(app_cf_make_v2(x - 18.0f + 18.0f * ratio, bar_y), 36.0f * ratio, 3.0f),
            0.0f
        );
        cf_draw_pop_color();
    }
    ecs_query_end(&query);
    if (!material_drawn) {
        draw_particles(particles);
    }
    for (int selection = -1; selection < acount(scene->actors); selection++) {
        ecs_entity selected = selection < 0 ? scene->runtime_selected
                                            : scene->actors[selection].entity;
        bool marked = selection < 0 ? selected.slot != 0
                                    : scene->actors[selection].marked ||
                                          selection == scene->selected;
        if (!scene_can_move(scene) || !selected.slot || !marked) {
            continue;
        }
        app_cf_aabb bounds;
        const GameTransform *position = GAME_GameTransform_get_const(world, selected);
        const GameAppearance *appearance = GAME_GameAppearance_get_const(world, selected);
        if (position && appearance &&
            appearance_bounds(
                position,
                appearance,
                actor_orientation(world, selected),
                GAME_GameSdfMotion_get_const(world, selected),
                false,
                &bounds
            )) {
            float scale = scene->selection_scale > 0 ? scene->selection_scale : 1;
            bounds.min.x -= 3 * scale;
            bounds.min.y -= 3 * scale;
            bounds.max.x += 3 * scale;
            bounds.max.y += 3 * scale;
            cf_draw_push_color(app_cf_make_color_rgba(0x82B5FFFF));
            cf_draw_quad(bounds, scale * 0.5f, 0);
            cf_draw_pop_color();
        }
    }
    if (scene->marquee_visible) {
        app_cf_aabb box = { { fminf(scene->marquee_start.x, scene->marquee_end.x),
                              fminf(scene->marquee_start.y, scene->marquee_end.y) },
                            { fmaxf(scene->marquee_start.x, scene->marquee_end.x),
                              fmaxf(scene->marquee_start.y, scene->marquee_end.y) } };
        cf_draw_push_color(app_cf_make_color_rgba(0x82B5FF22));
        cf_draw_quad_fill(box, 0);
        cf_draw_pop_color();
        cf_draw_push_color(app_cf_make_color_rgba(0x82B5FFFF));
        cf_draw_quad(box, scene->selection_scale * 0.5f, 0);
        cf_draw_pop_color();
    }
}

int app_scene_authoring_frame(app_scene_authoring *scene, double monotonic_time)
{
    if (!scene) {
        return FLOW_ERR_ARGUMENT;
    }
    for (int i = 0; i < acount(scene->prototypes); i++) {
        game_entities_register(scene->live->entities, scene->prototypes[i].prototype);
        game_prototype_preview_register_prototype(
            scene->prototype_preview,
            scene->prototypes[i].prototype
        );
    }
    preview_behavior_edits(scene);
    char status[128] = { 0 };
    if (scene->active_runtime_target == APP_RUNTIME_PREVIEW) {
        const scene_prototype_asset *asset = previewed_prototype(scene);
        if (asset) {
            snprintf(
                status,
                sizeof status,
                "%s preview %s",
                asset->prototype->name,
                scene->preview_playing ? "running" : "paused"
            );
        }
    } else if (scene->runtime_snapshot_valid) {
        snprintf(status, sizeof status, "Scene %s", scene->runtime_playing ? "running" : "paused");
    }
    editor_ui_set_runtime_status(status);
    ecs_entity preview_entity = game_prototype_preview_entity(scene->prototype_preview);
    int result = FLOW_OK;
    if (preview_entity.slot && !scene->preview_playing && !scene->preview_step) {
        result = game_prototype_preview_idle(scene->prototype_preview, monotonic_time);
    }
    if (preview_entity.slot && (scene->preview_playing || scene->preview_step)) {
        result = game_prototype_preview_tick(scene->prototype_preview, monotonic_time);
        if (result < 0) {
            snprintf(
                scene->prototype_message,
                sizeof scene->prototype_message,
                "Prototype preview Tick failed (%d).",
                result
            );
        }
    }
    int frame_result = engine_frame(scene->runtime, monotonic_time);
    if (frame_result < 0 && result == FLOW_OK) {
        result = frame_result;
    }
    scene->runtime_step = false;
    scene->preview_step = false;
    return result;
}

int app_scene_authoring_input(app_scene_authoring *scene, const char *action, double value, int pressed)
{
    if (!scene) {
        return FLOW_ERR_ARGUMENT;
    }
    if (scene->active_runtime_target == APP_RUNTIME_PREVIEW) {
        return game_prototype_preview_input(scene->prototype_preview, action, value, pressed, NULL);
    }
    return engine_dispatch_input(scene->runtime, action, value, pressed, NULL);
}

int app_scene_authoring_move(app_scene_authoring *scene, float x, float y)
{
    if (!scene || !isfinite(x) || !isfinite(y)) {
        return FLOW_ERR_ARGUMENT;
    }
    bool preview = scene->active_runtime_target == APP_RUNTIME_PREVIEW;
    if (engine_get_focus(scene->runtime) != ENGINE_FOCUS_VIEWPORT ||
        !(preview ? scene->preview_playing : scene->runtime_playing)) {
        return FLOW_ERR_STATE;
    }
    CF_V2 direction = { x, y };
    pin_value input = { .type = PIN_STRUCT, .type_name = "CF_V2", .ptr = &direction };
    flow_inputs inputs = { &input, 1, NULL };
    if (preview) {
        return game_behavior_dispatch(
            game_prototype_preview_behaviors(scene->prototype_preview),
            game_prototype_preview_entity(scene->prototype_preview),
            "Input",
            inputs,
            NULL
        );
    }
    return game_behavior_dispatch_all(scene->live->behaviors, "Input", inputs, NULL);
}
