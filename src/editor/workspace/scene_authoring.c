#include "assets/behavior_loader.h"
#include "assets/prototype.h"
#include "assets/scene_document.h"
#include "editor/documents/component_properties.h"
#include "editor/documents/scene_history.h"
#include "editor/documents/scene_retention.h"
#include "editor/ui/choices.h"
#include "editor/ui/labels.h"
#include "editor/workspace/editor.h"
#include "editor/workspace/scene_debug.h"
#include "editor/workspace/scene_internal.h"
#include "graph/document/serialize.h"
#include "graph/model/pin_fmt.h"
#include "graph/model/registry.h"
#include "graph/nodes/component_nodes.h"
#include "graph/runtime/value_array.h"
#include "graph/shader/shader_graph.h"
#include "platform/cute.h"
#include "render/appearance_geometry.h"
#include "render/scene_renderer.h"
#include "render/sdf_motion.h"
#include "render/shader_images.h"
#include "runtime/behaviors/entities.h"
#include "runtime/generated.h"
#include "runtime/scenes/gameplay.h"
#include "runtime/scenes/prototype_preview.h"
#include "support/array.h"
#include "support/asset_text.h"
#include "support/xalloc.h"

#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

enum
{
    CONTENT_PATH_CAP = 512,
};

#ifdef __EMSCRIPTEN__
EM_JS(int, web_save_asset, (const char *path, const char *text), {
    return Module['chibiSaveAsset']
             ? Module['chibiSaveAsset'](UTF8ToString(path), UTF8ToString(text))
             : 0;
})
#endif

int write_file_atomic(const char *path, const char *text)
{
#ifdef __EMSCRIPTEN__
    if (web_save_asset(path, text)) {
        return -1;
    }
#endif
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

int write_text_file_atomic(const char *path, const char *json)
{
    char error[128];
    char *text = asset_text_to_disk(json, error, sizeof error);
    if (!text) {
        return -1;
    }
    int result = write_file_atomic(path, text);
    free(text);
    return result;
}

int save_content_file(app_scene_authoring *scene, const char *path, const char *json)
{
    int result = write_text_file_atomic(path, json);
    if (result == 0) {
        tree_watch_note(&scene->disk, path);
    }
    return result;
}

game_prototype_registry app_component_registry(void)
{
    return (game_prototype_registry){
        GAME_REGISTRY_COMPONENTS,
        GAME_REGISTRY_COMPONENT_COUNT,
        NULL,
        NULL,
    };
}

int restore_retained_components(app_scene_authoring *scene, scene_actor *actor, const game_prototype *prototype);

int refresh_prototype_instances(app_scene_authoring *scene, scene_prototype_asset *asset);
void finish_transform_edit(app_scene_authoring *scene, bool cancel);

static const editor_choices *resolve_pin_choices(const node *n, int output, int pin, void *user)
{
    app_scene_authoring *scene = user;
    if (!output && pin == SHADER_IMAGE_INPUT_IMAGE &&
        n->desc == registry_node_by_name("Shader Image Texture")) {
        return &scene->image_choices;
    }
    component_node_kind component = component_node_kind_of(n->desc);
    if (!output && component && pin == component_node_setting(n)) {
        return component == COMPONENT_NODE_MAKE || component == COMPONENT_NODE_BREAK
                 ? &scene->type_choices
                 : &scene->component_choices;
    }
    if (!output && pin == 0 &&
        (n->desc == registry_node_by_name("Has Entities With Component") ||
         n->desc == registry_node_by_name("Find Entities With Component"))) {
        return &scene->component_choices;
    }
    if (!output && pin == 1 && n->desc == registry_node_by_name("Spawn Prototype")) {
        return scene->prototype_choices.count ? &scene->prototype_choices : NULL;
    }
    if (!output && pin == 0 && n->desc == registry_node_by_name("Get Global Variable")) {
        return scene->global_choices.count ? &scene->global_choices : NULL;
    }
    const node_desc *made = component_node_struct(n);
    const type_desc *type = made ? made->type : NULL;
    return !output && type && made->pure_kind == NODE_PURE_MAKE && pin >= 0 &&
                   pin < type->field_count
             ? property_choices(type, &type->fields[pin])
             : NULL;
}

static void free_prototype_choices(app_scene_authoring *scene)
{
    for (int i = 0; i < scene->prototype_choices.count; i++) {
        free((void *)scene->prototype_choices.items[i].name);
    }
    free((void *)scene->prototype_choices.items);
    scene->prototype_choices = (editor_choices){ 0 };
}

static void refresh_prototype_choices(app_scene_authoring *scene)
{
    int count = acount(scene->prototypes);
    bool unchanged = scene->prototype_choices.count == count;
    for (int i = 0; unchanged && i < count; i++) {
        unchanged = !strcmp(
            scene->prototype_choices.items[i].name,
            scene->prototypes[i].prototype->name
        );
    }
    if (unchanged) {
        return;
    }
    free_prototype_choices(scene);
    if (!count) {
        return;
    }
    editor_choice *choices = xmalloc((size_t)count * sizeof *choices);
    for (int i = 0; i < count; i++) {
        const char *name = xstrdup(scene->prototypes[i].prototype->name);
        choices[i] = (editor_choice){ .label = name, .name = name };
    }
    scene->prototype_choices = (editor_choices){ choices, count };
}

static void free_global_choices(app_scene_authoring *scene)
{
    for (int i = 0; i < scene->global_choices.count; i++) {
        free((void *)scene->global_choices.items[i].name);
        free((void *)scene->global_choices.items[i].label);
    }
    free((void *)scene->global_choices.items);
    scene->global_choices = (editor_choices){ 0 };
}

static void refresh_global_choices(app_scene_authoring *scene)
{
    const game_entities *entities = scene->live ? scene->live->entities : NULL;
    int count = game_entities_global_name_count(entities);
    bool unchanged = scene->global_choices.count == count;
    for (int i = 0; unchanged && i < count; i++) {
        const char *name = game_entities_global_name_at(entities, i);
        unchanged = name && !strcmp(scene->global_choices.items[i].name, name);
    }
    if (unchanged) {
        return;
    }
    free_global_choices(scene);
    if (!count) {
        return;
    }
    editor_choice *choices = xmalloc((size_t)count * sizeof *choices);
    for (int i = 0; i < count; i++) {
        const char *name = game_entities_global_name_at(entities, i);
        char label[128];
        editor_identifier_label(name, label, sizeof label);
        choices[i] = (editor_choice){ .name = xstrdup(name), .label = xstrdup(label) };
    }
    scene->global_choices = (editor_choices){ choices, count };
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

const ecs_component_desc *resolve_system_component(const char *name, void *)
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

const char *system_component_name(ecs_component_id id, void *)
{
    for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
        const ecs_component_desc *component = GAME_REGISTRY_COMPONENTS[i];
        if (component->id == id) {
            return component->key ? component->key : component->name;
        }
    }
    return NULL;
}

char *content_path_join(const char *base, const char *name)
{
    size_t base_size = strlen(base);
    size_t name_size = strlen(name);
    char *path = (char *)xmalloc(base_size + name_size + 2);
    memcpy(path, base, base_size);
    path[base_size] = '/';
    memcpy(path + base_size + 1, name, name_size + 1);
    return path;
}

static bool content_image_path(const char *path)
{
    const char *extension = strrchr(path, '.');
    return extension &&
           (!strcasecmp(extension, ".png") || !strcasecmp(extension, ".jpg") ||
            !strcasecmp(extension, ".jpeg"));
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
        apush(
            scene->content,
            ((scene_content_entry){ .path = child,
                                    .directory = is_directory,
                                    .image = !is_directory && content_image_path(child) })
        );
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

void content_refresh(app_scene_authoring *scene)
{
    asset_index_invalidate(scene);
    free((void *)scene->image_choices.items);
    scene->image_choices = (editor_choices){ 0 };
    for (int i = 0; i < acount(scene->content); i++) {
        free(scene->content[i].path);
    }
    asetlen(scene->content, 0);
    content_scan_directory(scene, scene->content_root);
    qsort(scene->content, (size_t)acount(scene->content), sizeof *scene->content, content_entry_compare);
    if (!acount(scene->content)) {
        return;
    }
    editor_choice *images = xmalloc((size_t)acount(scene->content) * sizeof *images);
    for (int i = 0; i < acount(scene->content); i++) {
        const scene_content_entry *entry = &scene->content[i];
        if (entry->image) {
            images[scene->image_choices.count++] = (editor_choice){
                .label = entry->path + strlen(scene->content_root) + 1,
                .name = entry->path
            };
        }
    }
    scene->image_choices.items = images;
}

void asset_index_invalidate(app_scene_authoring *scene)
{
    if (scene) {
        scene->asset_index_generation++;
    }
}

const char *content_basename(const char *path)
{
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static bool content_path_is_descendant(const char *parent, const char *path)
{
    size_t parent_size = strlen(parent);
    return strlen(path) > parent_size &&
           strncmp(parent, path, parent_size) == 0 && path[parent_size] == '/';
}

bool content_path_in_folder(const app_scene_authoring *scene, const char *path, const char *folder)
{
    char directory[CONTENT_PATH_CAP];
    int length = snprintf(directory, sizeof directory, "%s/%s", scene->content_root, folder);
    return length > 0 && length < (int)sizeof directory &&
           content_path_is_descendant(directory, path);
}

scene_actor *selected_actor(app_scene_authoring *scene)
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

ecs_entity selected_scene_entity(const app_scene_authoring *scene)
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

const char *runtime_entity_label(const app_scene_authoring *scene, ecs_entity entity)
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

scene_prototype_asset *prototype_at(app_scene_authoring *scene, int index)
{
    return scene && index >= 0 && index < acount(scene->prototypes)
             ? &scene->prototypes[index]
             : NULL;
}

scene_prototype_asset *find_prototype(app_scene_authoring *scene, const char *name)
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

scene_prototype_asset *selected_prototype(app_scene_authoring *scene)
{
    return prototype_at(scene, scene ? scene->selected_prototype : -1);
}

scene_prototype_asset *previewed_prototype(app_scene_authoring *scene)
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

void clear_prototype_history(scene_prototype_asset *asset)
{
    for (int i = 0; i < acount(asset->undo); i++) {
        free(asset->undo[i]);
    }
    for (int i = 0; i < acount(asset->redo); i++) {
        free(asset->redo[i]);
    }
    afree(asset->undo);
    afree(asset->redo);
}

void prototype_history_before(app_scene_authoring *scene, game_prototype *prototype)
{
    scene_prototype_asset *asset = find_prototype(scene, prototype->name);
    assert(asset && asset->prototype == prototype);
    char *json = game_prototype_save(prototype);
    if (json) {
        apush(asset->undo, json);
    }
    for (int i = 0; i < acount(asset->redo); i++) {
        free(asset->redo[i]);
    }
    afree(asset->redo);
}

void scene_authoring_select_document(
    app_scene_authoring *scene,
    app_editor_document_kind kind,
    const char *id
)
{
    if (scene->document.kind != kind || !scene->document_id ||
        strcmp(scene->document_id, id) != 0) {
        finish_transform_edit(scene, scene->dragging_actor);
    }
    char *copy = xstrdup(id);
    free(scene->document_id);
    scene->document_id = copy;
    scene->document = (app_editor_document_context){ kind, copy };
    scene_tabs_document_selected(scene, kind, id);
    if (kind != APP_EDITOR_DOCUMENT_BEHAVIOR && kind != APP_EDITOR_DOCUMENT_SYSTEM) {
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
}

int prototype_index(app_scene_authoring *scene, const char *name)
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

void set_behavior_component_context(app_scene_authoring *scene, const char *asset_name)
{
    scene->selected_prototype = behavior_prototype_index(scene, asset_name);
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

void select_prototype(app_scene_authoring *scene, int index)
{
    if (index < 0 || index >= acount(scene->prototypes)) {
        return;
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
    scene_authoring_select_document(
        scene,
        APP_EDITOR_DOCUMENT_PROTOTYPE,
        scene->prototypes[index].prototype->name
    );
}

int app_scene_authoring_open_prototype(app_scene_authoring *scene, const char *name)
{
    scene_prototype_asset *asset = find_prototype(scene, name);
    if (!asset) {
        return -1;
    }
    open_prototype(scene, (int)(asset - scene->prototypes));
    return 0;
}

void select_instance(app_scene_authoring *scene, int actor_index)
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
        scene_authoring_select_document(scene, APP_EDITOR_DOCUMENT_SCENE, APP_SCENE_DOCUMENT_ID);
    }
}

int app_scene_authoring_select_runtime_entity(app_scene_authoring *scene, ecs_entity entity)
{
    if (!scene || !game_entities_owns(scene->live->entities, entity)) {
        return -1;
    }
    finish_transform_edit(scene, scene->dragging_actor);
    scene->runtime_selected = entity;
    scene->selected = -1;
    scene_authoring_select_document(scene, APP_EDITOR_DOCUMENT_SCENE, APP_SCENE_DOCUMENT_ID);
    return 0;
}

bool scene_can_move(const app_scene_authoring *scene)
{
    return scene->active_runtime_target == APP_RUNTIME_SCENE &&
           !scene->runtime_playing && !scene->runtime_step &&
           !game_entities_initializing(scene->live->entities);
}

void write_position(app_scene_authoring *scene, ecs_entity entity, GameTransform position)
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

bool editable_scalar(const pin_value *value)
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
        case PIN_STR:
        case PIN_NAME:
        case PIN_ENUM: return true;
        case PIN_F32: return isfinite(value->f);
        case PIN_F64: return isfinite(value->d);
        default: return false;
    }
}

void finish_property_edit(app_scene_authoring *scene)
{
    if (!scene->property_actor.slot) {
        return;
    }
    if (!scene_history_push_fields(scene->history, scene->property_actor, &scene->property_edit, 1)) {
        scene->scene_dirty = scene->property_dirty;
    }
    scene->property_actor = (ecs_entity){ 0 };
}

void begin_transform_edit(app_scene_authoring *scene, ecs_entity entity)
{
    finish_transform_edit(scene, false);
    scene->edit_actor = entity;
    scene->edit_before = *GAME_GameTransform_get_const(scene->world, entity);
    scene->edit_dirty = scene->scene_dirty;
}

void finish_transform_edit(app_scene_authoring *scene, bool cancel)
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

static ecs_world *debug_world(app_scene_authoring *scene, app_runtime_target target)
{
    return target == APP_RUNTIME_PREVIEW
             ? game_prototype_preview_world(scene->prototype_preview)
             : scene->world;
}

static game_live_world *debug_live(app_scene_authoring *scene, app_runtime_target target)
{
    return target == APP_RUNTIME_PREVIEW
             ? game_prototype_preview_live(scene->prototype_preview)
             : scene->live;
}

static game_behavior_world *debug_behaviors(app_scene_authoring *scene, app_runtime_target target)
{
    return target == APP_RUNTIME_PREVIEW
             ? game_prototype_preview_behaviors(scene->prototype_preview)
             : scene->live->behaviors;
}

void sync_editor_inspection(app_scene_authoring *scene)
{
    game_behavior_attachment_info info;
    if (!scene->debug_pinned && selected_behavior_attachment(scene, &info) &&
        (!scene->debug.asset_name || scene->debug_target != scene->active_runtime_target ||
         ecs_entity_to_u64(scene->debug.entity) != ecs_entity_to_u64(info.entity) ||
         strcmp(scene->debug.asset_name, info.asset_name))) {
        scene->debug_target = scene->active_runtime_target;
        scene_debug_select(&scene->debug, &info, scene->debug_sessions[scene->debug_target]);
    }
    scene->debug_status = APP_DEBUG_NONE;
    if (scene->debug.asset_name) {
        scene->debug_status = APP_DEBUG_UNAVAILABLE;
        if (scene_debug_resolve(
                &scene->debug,
                debug_world(scene, scene->debug_target),
                debug_behaviors(scene, scene->debug_target),
                scene->debug_sessions[scene->debug_target],
                &info
            )) {
            scene->debug_status = APP_DEBUG_OTHER_GRAPH;
            if (scene->graph_document_kind == APP_EDITOR_DOCUMENT_BEHAVIOR &&
                scene->graph_document_id &&
                !strcmp(scene->graph_document_id, info.asset_name)) {
                editor *e = scene->graph_editor;
                game_live_world *live = debug_live(scene, scene->debug_target);
                bool matches = editor_has_pending_edits(e)
                                 ? scene_debug_runs(live, editor_behavior_asset(e), info.definition)
                                 : scene_debug_runs_cached(
                                       &scene->debug,
                                       live,
                                       editor_behavior_asset(e),
                                       editor_edit_revision(e),
                                       info.definition
                                   );
                scene->debug_status = matches ? APP_DEBUG_AVAILABLE : APP_DEBUG_STALE;
            }
        }
    }
    bool available = scene->debug_status == APP_DEBUG_AVAILABLE;
    for (int target = APP_RUNTIME_PREVIEW; target <= APP_RUNTIME_SCENE; target++) {
        game_behavior_inspect(
            debug_behaviors(scene, target),
            available && target == (int)scene->debug_target ? info.instance : NULL
        );
    }
    bool behavior = scene->graph_document_kind == APP_EDITOR_DOCUMENT_BEHAVIOR;
    editor_set_inspection_blocked(scene->graph_editor, behavior && !available);
    const flow_run *run = NULL;
    const behavior_instance_trace_entry *trace = NULL;
    int trace_count = 0;
    if (available) {
        for (int i = acount(info.instance->activations) - 1; i >= 0; i--) {
            flow_state state = flow_run_status(info.instance->activations[i]);
            if (state != FLOW_COMPLETED && state != FLOW_FAILED) {
                run = info.instance->activations[i];
                break;
            }
        }
        trace = behavior_instance_trace(info.instance, &trace_count);
    }
    editor_set_inspection_instance(scene->graph_editor, available ? info.instance : NULL);
    editor_set_inspection_run(scene->graph_editor, run);
    editor_set_inspection_trace(scene->graph_editor, trace, trace_count);
}

int app_scene_authoring_set_debug_target(
    app_scene_authoring *scene,
    app_runtime_target target,
    ecs_entity entity,
    const char *asset_name
)
{
    if (target < APP_RUNTIME_PREVIEW || target > APP_RUNTIME_SCENE ||
        !asset_name || !ecs_entity_is_alive(debug_world(scene, target), entity)) {
        return -1;
    }
    game_behavior_world *behaviors = debug_behaviors(scene, target);
    for (int i = 0; i < game_behavior_attachment_count_for_entity(behaviors, entity);
         i++) {
        game_behavior_attachment_info info;
        if (game_behavior_attachment_info_at(behaviors, entity, i, &info) == 0 &&
            !strcmp(info.asset_name, asset_name)) {
            scene_debug_select(&scene->debug, &info, scene->debug_sessions[target]);
            scene->debug_target = target;
            scene->debug_pinned = true;
            sync_editor_inspection(scene);
            return 0;
        }
    }
    return -1;
}

void app_scene_authoring_set_debug_pinned(app_scene_authoring *scene, bool pinned)
{
    scene->debug_pinned = pinned;
    sync_editor_inspection(scene);
}

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
)
{
    if (!world || !live || !definition || !editor_working || !runtime ||
        !content_root || !content_root[0]) {
        return NULL;
    }
    app_scene_authoring *scene = (app_scene_authoring *)xcalloc(1, sizeof *scene);
    scene->debug_sessions[APP_RUNTIME_PREVIEW] = 1;
    scene->debug_sessions[APP_RUNTIME_SCENE] = 1;
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
    scene->content_root = xstrdup(content_root);
    scene->asset_index = asset_index_new();
    scene->asset_index_generation = 1;
    content_refresh(scene);
    scene->selected = -1;
    scene->selected_prototype = -1;
    scene->runtime_preview_prototype = -1;
    scene_tabs_initialize(scene);
    scene_authoring_select_document(scene, APP_EDITOR_DOCUMENT_SCENE, APP_SCENE_DOCUMENT_ID);
    editor_set_value_formatter(graph_editor, format_entity_value, scene);
    editor_choice *component_choices = xmalloc(
        (size_t)scene->registry.component_count * sizeof *component_choices
    );
    for (int i = 0; i < scene->registry.component_count; i++) {
        const ecs_component_desc *component = scene->registry.components[i];
        char label[128];
        editor_component_label(component->name, label, sizeof label);
        component_choices[i] = (editor_choice){
            .label = xstrdup(label),
            .name = component->key ? component->key : component->name,
        };
    }
    editor_choice *type_choices = NULL;
    for (int i = 0; i < registry_node_count(); i++) {
        const node_desc *make = registry_node(i);
        if (make->pure_kind != NODE_PURE_MAKE || !make->type) {
            continue;
        }
        char label[128];
        editor_component_label(make->type->name, label, sizeof label);
        apush(
            type_choices,
            ((editor_choice){ .label = xstrdup(label), .name = make->type->name })
        );
    }
    scene->type_choices = (editor_choices){ type_choices, acount(type_choices) };
    scene->component_choices = (editor_choices){ component_choices,
                                                 scene->registry.component_count };
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
    if (cf_app_is_running() && !render_image_mount_content(
                                   scene->content_root,
                                   scene->message,
                                   sizeof scene->message
                               )) {
        fprintf(stderr, "%s\n", scene->message);
        app_scene_authoring_free(scene);
        return NULL;
    }
    return scene;
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
    return app_scene_authoring_new_with_content_root(
        world,
        live,
        definition,
        editor_working,
        graph_editor,
        resolve_prototype,
        resolve_user,
        runtime,
        "content"
    );
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
    bool changed = !scene->editor_state_seen ||
                   scene->editor_definition_seen != scene->definition ||
                   scene->editor_behavior_asset_seen != scene->editor_working ||
                   scene->editor_revision_seen != revision;
    if (!changed && !editor_has_pending_edits(scene->graph_editor)) {
        return;
    }
    scene->editor_state_seen = true;
    scene->editor_definition_seen = scene->definition;
    scene->editor_behavior_asset_seen = scene->editor_working;
    scene->editor_revision_seen = revision;
    if (changed) {
        asset_index_invalidate(scene);
    }
    scene->behavior_apply_pending = true;
}

int sync_behavior_working(app_scene_authoring *scene)
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
    refresh_asset_catalog(scene);
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

void preview_behavior_edits(app_scene_authoring *scene)
{
    if (!scene->graph_document_id ||
        scene->graph_document_kind != APP_EDITOR_DOCUMENT_BEHAVIOR) {
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
    bool library = scene->definition->working->domain == BEHAVIOR_DOMAIN_LIBRARY ||
                   scene->definition->working->domain == BEHAVIOR_DOMAIN_SHADER_LIBRARY;
    int prepared = library ? game_prototype_preview_prepare_library(
                                 scene->prototype_preview,
                                 scene->definition->name,
                                 scene->definition->working,
                                 scene->preview_message,
                                 sizeof scene->preview_message
                             )
                           : 0;
    int result = prepared ? prepared
                          : game_live_preview(
                                scene->live,
                                scene->definition,
                                scene->preview_message,
                                sizeof scene->preview_message
                            );
    char prototype_message[256] = { 0 };
    bool mirrored = scene->definition->working->domain == BEHAVIOR_DOMAIN_EVENT ||
                    library ||
                    scene->definition->working->domain == BEHAVIOR_DOMAIN_FIELD ||
                    scene->definition->working->domain == BEHAVIOR_DOMAIN_EVENT_DEFINITION;
    int prototype_result = result == 0 && mirrored
                             ? game_prototype_preview_update_behavior(
                                   scene->prototype_preview,
                                   scene->definition->name,
                                   scene->definition->working,
                                   false,
                                   prototype_message,
                                   sizeof prototype_message
                               )
                             : INSTANCE_UPDATE_READY;
    scene->preview_retry = result == INSTANCE_UPDATE_BUSY ||
                           prototype_result == INSTANCE_UPDATE_BUSY;
    if (result >= 0 && prototype_result != INSTANCE_UPDATE_READY) {
        result = prototype_result;
        snprintf(scene->preview_message, sizeof scene->preview_message, "Prototype: %s", prototype_message);
    }
    if (result < 0) {
        editor_command_apply(scene->graph_editor, (editor_command){ .kind = EDITOR_COMMAND_COMPILE });
    }
    sync_editor_inspection(scene);
}

int app_scene_authoring_apply_pending(app_scene_authoring *scene)
{
    if (!scene || sync_behavior_working(scene) != 0 || !scene->definition) {
        return -1;
    }
    if (scene->definition->working->domain == BEHAVIOR_DOMAIN_LIBRARY ||
        scene->definition->working->domain == BEHAVIOR_DOMAIN_SHADER_LIBRARY) {
        int ready = game_prototype_preview_prepare_library(
            scene->prototype_preview,
            scene->definition->name,
            scene->definition->working,
            scene->preview_message,
            sizeof scene->preview_message
        );
        if (ready != 0) {
            return ready;
        }
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
    if (cf_app_is_running() &&
        !render_image_mount_content(path, scene->message, sizeof scene->message)) {
        return -1;
    }
    char *copy = xstrdup(path);
    free(scene->content_root);
    scene->content_root = copy;
    content_refresh(scene);
    return 0;
}

void app_scene_authoring_refresh_content(app_scene_authoring *scene)
{
    if (scene) {
        content_refresh(scene);
    }
}

void app_scene_authoring_free(app_scene_authoring *scene)
{
    if (!scene) {
        return;
    }
    scene_debug_clear(&scene->debug);
    editor_set_inspection_blocked(scene->graph_editor, false);
    app_scene_renderer_free(scene->renderer);
    finish_transform_edit(scene, true);
    editor_set_inspection_instance(scene->graph_editor, NULL);
    editor_set_inspection_run(scene->graph_editor, NULL);
    editor_set_value_formatter(scene->graph_editor, NULL, NULL);
    editor_set_choices_resolver(scene->graph_editor, NULL, NULL);
    for (int i = 0; i < scene->component_choices.count; i++) {
        free((void *)scene->component_choices.items[i].label);
    }
    free((void *)scene->component_choices.items);
    for (int i = 0; i < scene->type_choices.count; i++) {
        free((void *)scene->type_choices.items[i].label);
    }
    afree(scene->type_choices.items);
    free_prototype_choices(scene);
    free_global_choices(scene);
    while (acount(scene->actors)) {
        scene->selected = acount(scene->actors) - 1;
        remove_selected(scene);
    }
    afree(scene->actors);
    asset_index_free(scene->asset_index);
    scene_tabs_free(scene);
    scene_history_free(scene->history);
    for (int i = 0; i < acount(scene->prototypes); i++) {
        clear_prototype_history(&scene->prototypes[i]);
        free(scene->prototypes[i].path);
        game_prototype_free(scene->prototypes[i].prototype);
    }
    afree(scene->prototypes);
    game_prototype_preview_free(scene->prototype_preview);
    free(scene->preview_prototype_id);
    free((void *)scene->image_choices.items);
    for (int i = 0; i < acount(scene->content); i++) {
        free(scene->content[i].path);
    }
    afree(scene->content);
    tree_watch_free(&scene->disk);
    tree_watch_changes_free(scene->disk_pending);
    free(scene->content_root);
    free(scene->scene_path);
    game_system_document_free(scene->system_document);
    free(scene->system_path);
    free(scene->current_scene_tab_key);
    free(scene->document_id);
    free(scene->graph_document_id);
    free(scene);
}

static int open_library_source(void *user, const char *name, uint64_t graph_id)
{
    app_scene_authoring *scene = user;
    if (open_behavior_asset(scene, name) != 0) {
        return -1;
    }
    return editor_open_graph(scene->graph_editor, graph_id);
}

/* The graph editor borrows every definition's working asset, so whatever
   replaces or removes one refreshes the catalog before the editor reads it. */
void refresh_asset_catalog(app_scene_authoring *scene)
{
    const behavior_asset **assets = NULL;
    for (int i = 0; i < game_live_definition_count(scene->live); i++) {
        apush(assets, game_live_definition_at(scene->live, i)->working);
    }
    editor_set_asset_catalog(scene->graph_editor, assets, acount(assets));
    afree(assets);
}

static void refresh_catalogs(app_scene_authoring *scene)
{
    editor_set_asset_opener(scene->graph_editor, open_library_source, scene);
    refresh_prototype_choices(scene);
    refresh_global_choices(scene);
    const char **prototypes = NULL;
    for (int i = 0; i < acount(scene->prototypes); i++) {
        apush(prototypes, scene->prototypes[i].prototype->name);
    }
    editor_set_prototype_catalog(scene->graph_editor, prototypes, acount(prototypes));
    afree(prototypes);
    refresh_asset_catalog(scene);
    sync_editor_inspection(scene);
    if (scene->runtime_selected.slot &&
        !game_entities_owns(scene->live->entities, scene->runtime_selected)) {
        scene->runtime_selected = (ecs_entity){ 0 };
        sync_editor_inspection(scene);
    }
}

static void refresh_asset_index(app_scene_authoring *scene, bool ui_active)
{
    bool defer_asset_index = false;
    unsigned editor_revision = editor_edit_revision(scene->graph_editor);
    if (editor_revision != scene->asset_index_editor_revision) {
        scene->asset_index_editor_revision = editor_revision;
        asset_index_invalidate(scene);
        defer_asset_index = scene->asset_index_built_generation != 0;
    }
    unsigned system_revision = game_system_document_revision(scene->system_document);
    if (scene->document.kind == APP_EDITOR_DOCUMENT_SYSTEM && scene->system_document &&
        (system_revision != scene->asset_index_system_revision ||
         editor_revision != scene->system_runtime_editor_revision)) {
        scene->system_runtime_editor_revision = editor_revision;
        if (scene_authoring_sync_document(scene) == 0) {
            char *json = game_system_document_save(
                scene->system_document,
                resolve_system_component,
                system_component_name,
                NULL
            );
            const behavior_asset *asset = game_system_document_behavior_asset(
                scene->system_document
            );
            if (json) {
                game_live_register_system(
                    scene->live,
                    asset->name,
                    json,
                    scene->preview_message,
                    sizeof scene->preview_message
                );
                game_prototype_preview_register_system(
                    scene->prototype_preview,
                    asset->name,
                    json,
                    scene->preview_message,
                    sizeof scene->preview_message
                );
                free(json);
            }
        }
    }
    if (system_revision != scene->asset_index_system_revision) {
        scene->asset_index_system_revision = system_revision;
        asset_index_invalidate(scene);
        defer_asset_index = defer_asset_index ||
                            scene->asset_index_built_generation != 0;
    }
    if (scene->asset_index_built_generation != scene->asset_index_generation &&
        !defer_asset_index && !editor_has_pending_edits(scene->graph_editor) &&
        !ui_active) {
        app_scene_authoring_refresh_asset_index(scene);
    }
}

void app_scene_authoring_update(app_scene_authoring *scene, bool ui_active)
{
    refresh_catalogs(scene);
    refresh_asset_index(scene, ui_active);
}

int scene_authoring_sync_document(app_scene_authoring *scene)
{
    finish_transform_edit(scene, false);
    finish_property_edit(scene);
    if (scene->graph_document_kind != APP_EDITOR_DOCUMENT_SYSTEM ||
        !scene->graph_document_id) {
        return scene->definition ? sync_behavior_working(scene) : 0;
    }
    char *json = behavior_asset_save_json(editor_behavior_asset(scene->graph_editor));
    behavior_asset *copy = json ? behavior_asset_load_json(json) : NULL;
    free(json);
    if (!copy) {
        return -1;
    }
    return game_system_document_replace_behavior_asset(scene->system_document, copy);
}
