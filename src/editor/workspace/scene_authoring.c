#include "assets/asset_source.h"
#include "assets/behavior_loader.h"
#include "assets/prototype.h"
#include "assets/scene_document.h"
#include "editor/collab/actions_scene.h"
#include "editor/collab/collab.h"
#include "editor/documents/component_properties.h"
#include "editor/documents/scene_history.h"
#include "editor/documents/scene_retention.h"
#include "editor/ui/choices.h"
#include "editor/ui/labels.h"
#include "editor/workspace/editor.h"
#include "editor/workspace/scene_debug.h"
#include "editor/workspace/scene_internal.h"
#include "graph/document/serialize.h"
#include "graph/document/serialize_internal.h"
#include "graph/model/pin_fmt.h"
#include "graph/model/registry.h"
#include "graph/model/type.h"
#include "graph/nodes/component_nodes.h"
#include "graph/runtime/value_array.h"
#include "graph/shader/shader_graph.h"
#include "render/appearance_geometry.h"
#include "render/scene_renderer.h"
#include "render/sdf_motion.h"
#include "runtime/behaviors/entities.h"
#include "runtime/generated.h"
#include "runtime/scenes/gameplay.h"
#include "runtime/scenes/prototype_preview.h"
#include "support/array.h"
#include "support/asset_json.h"
#include "support/asset_text.h"
#include "support/text_file.h"
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
    av_doc *document = asset_text_read(json, error, sizeof error);
    if (!document) {
        return -1;
    }
    char *previous = text_file_read(path);
    char *text = previous
                   ? asset_source_reconcile(previous, av_doc_root(document), error, sizeof error)
                   : av_doc_write(document);
    free(previous);
    av_doc_free(document);
    if (!text) {
        return -1;
    }
    int result = write_file_atomic(path, text);
    free(text);
    return result;
}

#ifdef __EMSCRIPTEN__
EM_JS(int, web_delete_asset, (const char *path), {
    return Module['chibiDeleteAsset']
             ? Module['chibiDeleteAsset'](UTF8ToString(path))
             : 0;
})
#endif

int remove_content_file(const char *path)
{
    if (remove(path) != 0) {
        return -1;
    }
#ifdef __EMSCRIPTEN__
    return web_delete_asset(path);
#else
    return 0;
#endif
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

const char *scene_entity_prototype(const app_scene_authoring *scene, ecs_entity entity)
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
    return NULL;
}

const char *runtime_entity_label(const app_scene_authoring *scene, ecs_entity entity)
{
    const char *prototype = scene_entity_prototype(scene, entity);
    return prototype ? prototype : "Entity";
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
        av_value_free(asset->undo[i]);
    }
    for (int i = 0; i < acount(asset->redo); i++) {
        av_value_free(asset->redo[i]);
    }
    afree(asset->undo);
    afree(asset->redo);
}

void prototype_history_before(app_scene_authoring *scene, game_prototype *prototype)
{
    if (!collab_record_history()) {
        return;
    }
    scene_prototype_asset *asset = find_prototype(scene, prototype->name);
    assert(asset && asset->prototype == prototype);
    av_value *value = game_prototype_to_value(prototype);
    if (value) {
        apush(asset->undo, value);
    }
    for (int i = 0; i < acount(asset->redo); i++) {
        av_value_free(asset->redo[i]);
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
    yyjson_mut_doc *doc = NULL;
    yyjson_mut_val *root = scene_action_root(scene, "tab.open_prototype", &doc);
    yyjson_mut_obj_add_strcpy(doc, root, "prototype", name);
    char *action = scene_action_write(doc);
    collab_action_gate gate = action ? scene_action_begin(scene, action)
                                     : COLLAB_ACTION_REJECT;
    if (gate != COLLAB_ACTION_APPLY) {
        free(action);
        return gate == COLLAB_ACTION_PENDING ? 1 : -1;
    }
    open_prototype(scene, (int)(asset - scene->prototypes));
    scene_action_finish(scene, action, true);
    free(action);
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

bool scene_instance_selected(const app_scene_authoring *scene, int index)
{
    return scene->actors[index].marked || index == scene->selected;
}

int scene_selection_count(const app_scene_authoring *scene)
{
    int count = 0;
    for (int i = 0; i < acount(scene->actors); i++) {
        count += scene_instance_selected(scene, i);
    }
    return count;
}

void scene_toggle_instance(app_scene_authoring *scene, int index)
{
    assert(index >= 0 && index < acount(scene->actors));
    finish_transform_edit(scene, false);
    scene->runtime_selected = (ecs_entity){ 0 };
    if (scene->selected >= 0) {
        scene->actors[scene->selected].marked = true;
    }
    bool selected = !scene->actors[index].marked;
    scene->actors[index].marked = selected;
    if (selected) {
        scene->selected = index;
        return;
    }
    /* Unity's: the active object leaves for another selected one. */
    scene->selected = -1;
    for (int i = 0; i < acount(scene->actors); i++) {
        scene->selected = scene->actors[i].marked ? i : scene->selected;
    }
}

void scene_select_range(app_scene_authoring *scene, const int *rows, int from, int to)
{
    finish_transform_edit(scene, false);
    for (int i = 0; i < acount(scene->actors); i++) {
        scene->actors[i].marked = false;
    }
    int step = from <= to ? 1 : -1;
    for (int r = from; r != to + step; r += step) {
        scene->actors[rows[r]].marked = true;
    }
    scene->runtime_selected = (ecs_entity){ 0 };
    scene->selected = rows[to];
}

static void select_spawned(app_scene_authoring *scene, ecs_entity entity)
{
    finish_transform_edit(scene, scene->dragging_actor);
    for (int i = 0; i < acount(scene->actors); i++) {
        scene->actors[i].marked = false;
    }
    scene->runtime_selected = entity;
    scene->selected = -1;
}

int app_scene_authoring_select_runtime_entity(app_scene_authoring *scene, ecs_entity entity)
{
    if (!scene || !game_entities_owns(scene->live->entities, entity)) {
        return -1;
    }
    select_spawned(scene, entity);
    scene_authoring_select_document(scene, APP_EDITOR_DOCUMENT_SCENE, APP_SCENE_DOCUMENT_ID);
    return 0;
}

int app_scene_authoring_select_entity(app_scene_authoring *scene, ecs_entity entity)
{
    for (int i = 0; i < acount(scene->actors); i++) {
        if (ecs_entity_to_u64(scene->actors[i].entity) == ecs_entity_to_u64(entity)) {
            select_instance(scene, i);
            return 0;
        }
    }
    if (!game_entities_owns(scene->live->entities, entity)) {
        return -1;
    }
    select_spawned(scene, entity);
    if (scene->document.kind != APP_EDITOR_DOCUMENT_BEHAVIOR) {
        scene_authoring_select_document(scene, APP_EDITOR_DOCUMENT_SCENE, APP_SCENE_DOCUMENT_ID);
    }
    return 0;
}

bool scene_can_move(const app_scene_authoring *scene)
{
    return scene->active_runtime_target == APP_RUNTIME_SCENE &&
           !scene->runtime_playing && !scene->runtime_step &&
           !game_entities_initializing(scene->live->entities);
}

bool scene_editing(const app_scene_authoring *scene)
{
    return scene_can_move(scene) && !scene->runtime_snapshot_valid;
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
    if (value->container != PIN_CONTAINER_SINGLE) {
        return false;
    }
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
        case PIN_STRUCT: {
            const type_desc *type = registry_type_by_name(value->type_name);
            return value->ptr && type && type_value_is_finite(type, value->ptr);
        }
        default: return false;
    }
}

bool scene_property_value_copy(pin_value *out, pin_value value)
{
    assert(out);
    *out = (pin_value){ 0 };
    if (value.container != PIN_CONTAINER_SINGLE) {
        return false;
    }
    *out = value;
    out->type_name = value.type_name ? xstrdup(value.type_name) : NULL;
    if (value.type == PIN_STR || value.type == PIN_NAME) {
        out->str = xstrdup(value.str ? value.str : "");
        return true;
    }
    if (value.type == PIN_STRUCT) {
        const type_desc *type = registry_type_by_name(value.type_name);
        if (!value.ptr || !type || !type_value_is_finite(type, value.ptr)) {
            free((void *)out->type_name);
            *out = (pin_value){ 0 };
            return false;
        }
        out->ptr = xmalloc(type->size);
        memcpy(out->ptr, value.ptr, type->size);
    }
    return true;
}

void scene_property_value_free(pin_value *value)
{
    assert(value);
    free((void *)value->type_name);
    if (value->type == PIN_STR || value->type == PIN_NAME) {
        free((void *)value->str);
    }
    if (value->type == PIN_STRUCT) {
        free(value->ptr);
    }
    *value = (pin_value){ 0 };
}

static void commit_property_edit(app_scene_authoring *scene)
{
    scene_field_edit *edit = &scene->property_edit;
    scene_actor *actor = NULL;
    uint64_t identity = scene_actor_identity(scene, scene->property_actor);
    for (int i = 0; identity && i < acount(scene->actors); i++) {
        if (scene->actors[i].identity == identity) {
            actor = &scene->actors[i];
            break;
        }
    }
    char *action = NULL;
    collab_action_gate gate = COLLAB_ACTION_APPLY;
    if (actor) {
        const ecs_component_desc *component = NULL;
        for (int i = 0; i < scene->registry.component_count; i++) {
            if (scene->registry.components[i]->id == edit->component) {
                component = scene->registry.components[i];
                break;
            }
        }
        if (component && edit->field >= 0 && edit->field < component->type->field_count) {
            yyjson_mut_doc *doc = NULL;
            yyjson_mut_val *root = scene_action_root(scene, "property.set", &doc);
            yyjson_mut_obj_add_uint(doc, root, "actor", identity);
            yyjson_mut_obj_add_strcpy(doc, root, "component", component->name);
            yyjson_mut_obj_add_strcpy(
                doc,
                root,
                "field",
                component->type->fields[edit->field].name
            );
            av_doc *native = av_doc_new();
            av_value *native_root = av_obj(native);
            serialize_write_value(
                native,
                native_root,
                "value",
                edit->after.type,
                edit->after.type_name,
                &edit->after
            );
            yyjson_mut_obj_add_val(doc, root, "value", av_to_json(doc, av_obj_get(native_root, "value")));
            av_doc_free(native);
            action = scene_action_write(doc);
            gate = action ? scene_action_begin(scene, action) : COLLAB_ACTION_REJECT;
        }
    } else if (collab_active() && !collab_replaying()) {
        gate = COLLAB_ACTION_REJECT;
    }

    bool recorded = false;
    if (gate == COLLAB_ACTION_APPLY && collab_record_history()) {
        recorded = scene_history_push_fields(scene->history, scene->property_actor, edit, 1);
    }
    bool success = gate != COLLAB_ACTION_REJECT &&
                   (recorded || !collab_record_history());
    if (gate == COLLAB_ACTION_REJECT && scene_collab_can_write_locally()) {
        ecs_component_write_field(
            scene->world,
            scene->property_actor,
            edit->component,
            edit->field,
            edit->before
        );
    }
    if (!recorded && scene_collab_can_write_locally()) {
        scene->scene_dirty = scene->property_dirty;
    }
    if (action) {
        scene_action_finish(scene, action, success);
    }
    free(action);
    scene_property_value_free(&edit->before);
    scene_property_value_free(&edit->after);
    *edit = (scene_field_edit){ 0 };
    scene->property_actor = (ecs_entity){ 0 };
}

/* Records each other object a multi-object edit wrote, as the edit itself. */
static void commit_property_others(
    app_scene_authoring *scene,
    scene_field_target *others,
    ecs_component_id component,
    int field
)
{
    for (int i = 0; i < acount(others); i++) {
        pin_value after = { 0 };
        if (ecs_component_read_field(scene->world, others[i].entity, component, field, &after) ==
                ECS_OK &&
            !pin_value_equal(&others[i].before, &after)) {
            scene->property_actor = others[i].entity;
            scene->property_edit = (scene_field_edit){ .component = component,
                                                       .field = field };
            bool copied = scene_property_value_copy(
                              &scene->property_edit.before,
                              others[i].before
                          ) &&
                          scene_property_value_copy(&scene->property_edit.after, after);
            assert(copied);
            scene->property_dirty = true;
            commit_property_edit(scene);
            scene->scene_dirty = true;
        }
        scene_property_value_free(&others[i].before);
    }
}

void finish_property_edit(app_scene_authoring *scene)
{
    if (!scene->property_actor.slot) {
        return;
    }
    scene_field_target *others = scene->property_others;
    scene->property_others = NULL;
    ecs_component_id component = scene->property_edit.component;
    int field = scene->property_edit.field;
    if (acount(others)) {
        scene_history_begin_group(scene->history);
    }
    commit_property_edit(scene);
    commit_property_others(scene, others, component, field);
    if (acount(others)) {
        scene_history_end_group(scene->history);
    }
    afree(others);
}

void begin_transform_edit(app_scene_authoring *scene, ecs_entity entity)
{
    finish_transform_edit(scene, false);
    scene->edit_actor = entity;
    scene->edit_before = *GAME_GameTransform_get_const(scene->world, entity);
    scene->edit_after = scene->edit_before;
    scene->edit_dirty = scene->scene_dirty;
}

void write_transform_edit(app_scene_authoring *scene, ecs_entity entity, GameTransform position)
{
    if (scene->runtime_snapshot_valid) {
        write_position(scene, entity, position);
        return;
    }
    if (scene_collab_is_editor() && scene->edit_actor.slot &&
        ecs_entity_to_u64(scene->edit_actor) == ecs_entity_to_u64(entity)) {
        scene->edit_after = position;
        return;
    }
    if (scene_collab_can_write_locally()) {
        write_position(scene, entity, position);
    }
}

/* A move in the running scene changes only its world, which Stop replaces
   with the authored scene; nothing records it. */
static void finish_live_move(app_scene_authoring *scene, bool cancel)
{
    if (cancel && GAME_GameTransform_has(scene->world, scene->edit_actor)) {
        write_position(scene, scene->edit_actor, scene->edit_before);
    }
    scene->edit_actor = (ecs_entity){ 0 };
    scene->dragging_actor = false;
}

void finish_transform_edit(app_scene_authoring *scene, bool cancel)
{
    finish_property_edit(scene);
    scene_gizmo_finish(scene, cancel);
    if (!scene->edit_actor.slot) {
        return;
    }
    if (scene->runtime_snapshot_valid) {
        finish_live_move(scene, cancel);
        return;
    }
    const GameTransform
        *position = GAME_GameTransform_get_const(scene->world, scene->edit_actor);
    bool editor = scene_collab_is_editor();
    GameTransform after = editor   ? scene->edit_after
                        : position ? *position
                                   : scene->edit_before;
    uint64_t identity = scene_actor_identity(scene, scene->edit_actor);
    bool recorded = false;
    char *action = NULL;
    collab_action_gate gate = COLLAB_ACTION_APPLY;
    if (position && !cancel &&
        (after.x != scene->edit_before.x || after.y != scene->edit_before.y)) {
        if (identity) {
            yyjson_mut_doc *doc = NULL;
            yyjson_mut_val *root = scene_action_root(scene, "transform.set", &doc);
            yyjson_mut_obj_add_uint(doc, root, "actor", identity);
            yyjson_mut_obj_add_real(doc, root, "x", after.x);
            yyjson_mut_obj_add_real(doc, root, "y", after.y);
            action = scene_action_write(doc);
            gate = action ? scene_action_begin(scene, action) : COLLAB_ACTION_REJECT;
        } else if (collab_active() && !collab_replaying()) {
            gate = COLLAB_ACTION_REJECT;
        }
        if (gate == COLLAB_ACTION_APPLY && scene_collab_can_write_locally()) {
            write_position(scene, scene->edit_actor, after);
            if (collab_record_history()) {
                recorded = scene_history_push(
                    scene->history,
                    scene->edit_actor,
                    scene->edit_before,
                    after
                );
            }
        }
        if (gate == COLLAB_ACTION_REJECT && position &&
            scene_collab_can_write_locally()) {
            write_position(scene, scene->edit_actor, scene->edit_before);
        }
        bool success = gate != COLLAB_ACTION_REJECT &&
                       (recorded || !collab_record_history() || editor);
        if (action) {
            scene_action_finish(scene, action, success);
        }
        free(action);
        if (!success && scene_collab_can_write_locally()) {
            scene->scene_dirty = scene->edit_dirty;
        } else if (success && !editor) {
            scene->scene_dirty = true;
        }
    } else if (position && cancel) {
        if (!editor && scene_collab_can_write_locally()) {
            write_position(scene, scene->edit_actor, scene->edit_before);
        }
        scene->scene_dirty = scene->edit_dirty;
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

/* The only live instance of the open behavior, when it has exactly one. */
static bool sole_instance(app_scene_authoring *scene, game_behavior_attachment_info *out)
{
    game_behavior_world *behaviors = debug_behaviors(scene, scene->active_runtime_target);
    int found = 0;
    for (int i = 0; i < game_behavior_attachment_count(behaviors) && found < 2; i++) {
        game_behavior_attachment_info info = game_behavior_attachment_info_index(behaviors, i);
        if (!strcmp(info.asset_name, scene->definition->name)) {
            *out = info;
            found++;
        }
    }
    return found == 1;
}

/* Whether the selection, the open behavior or the session changed since the
   debug target last followed them. A target chosen in between, from the
   graph header or by the debugger, stays until one of them does. */
static bool follow_changed(app_scene_authoring *scene)
{
    app_runtime_target target = scene->active_runtime_target;
    ecs_entity entity = target == APP_RUNTIME_PREVIEW
                          ? game_prototype_preview_entity(scene->prototype_preview)
                          : selected_scene_entity(scene);
    uint64_t session = scene->debug_sessions[target];
    const char *asset = scene->definition->name;
    if (scene->debug_followed.asset && !strcmp(scene->debug_followed.asset, asset) &&
        ecs_entity_to_u64(scene->debug_followed.entity) == ecs_entity_to_u64(entity) &&
        scene->debug_followed.session == session &&
        scene->debug_followed.target == target) {
        return false;
    }
    free(scene->debug_followed.asset);
    scene->debug_followed.asset = xstrdup(asset);
    scene->debug_followed.entity = entity;
    scene->debug_followed.session = session;
    scene->debug_followed.target = target;
    return true;
}

/* Makes the instance the debug target. Its prototype stays with it, so a
   destroyed target keeps its name. */
static void target_instance(
    app_scene_authoring *scene,
    app_runtime_target target,
    const game_behavior_attachment_info *info
)
{
    const char *prototype = target == APP_RUNTIME_SCENE
                              ? scene_entity_prototype(scene, info->entity)
                              : NULL;
    scene->debug_target = target;
    scene_debug_select(&scene->debug, info, scene->debug_sessions[target]);
    scene->debug.prototype = prototype ? xstrdup(prototype) : NULL;
}

static void stop_following_newest(app_scene_authoring *scene)
{
    free(scene->debug_newest);
    scene->debug_newest = NULL;
}

/* Unreal's debug object: the selected object's instance of the open behavior,
   else its only instance; selecting something without it keeps the target.
   A target the selection gives ends Follow newest. */
static void follow_selection(app_scene_authoring *scene)
{
    if (scene->debug_pinned || !scene->definition || !scene->definition->name ||
        !follow_changed(scene)) {
        return;
    }
    game_behavior_attachment_info info;
    if (selected_behavior_attachment(scene, &info)) {
        stop_following_newest(scene);
        target_instance(scene, scene->active_runtime_target, &info);
        return;
    }
    if (!scene->debug_newest && sole_instance(scene, &info)) {
        target_instance(scene, scene->active_runtime_target, &info);
    }
}

/* The scene's latest instance of the open behavior on a `prototype` object:
   attachments count up, so the greatest sequence is the newest. */
static bool newest_instance(app_scene_authoring *scene, const char *prototype, game_behavior_attachment_info *out)
{
    game_behavior_world *behaviors = scene->live->behaviors;
    bool found = false;
    for (int i = game_behavior_attachment_count(behaviors) - 1; i >= 0; i--) {
        game_behavior_attachment_info info = game_behavior_attachment_info_index(behaviors, i);
        if ((found && info.sequence <= out->sequence) ||
            strcmp(info.asset_name, scene->definition->name)) {
            continue;
        }
        const char *made = scene_entity_prototype(scene, info.entity);
        if (made && !strcmp(made, prototype)) {
            *out = info;
            found = true;
        }
    }
    return found;
}

/* Whether the target is a scene instance of this session. */
static bool scene_target(const app_scene_authoring *scene)
{
    return scene->debug.asset_name && scene->debug_target == APP_RUNTIME_SCENE &&
           scene->debug.session == scene->debug_sessions[APP_RUNTIME_SCENE];
}

/* Follow newest (Unreal's debug filter): an instance the game spawns of the
   followed prototype becomes the target, so short-lived ones are watched
   from their first frame; a destroyed one stays until the next spawns. */
static void follow_newest(app_scene_authoring *scene)
{
    game_behavior_attachment_info info;
    if (!scene->debug_newest || scene->debug_pinned || !scene->definition ||
        scene->active_runtime_target != APP_RUNTIME_SCENE ||
        !newest_instance(scene, scene->debug_newest, &info) ||
        (scene_target(scene) && info.sequence <= scene->debug.sequence)) {
        return;
    }
    target_instance(scene, APP_RUNTIME_SCENE, &info);
}

/* The target's live instance, or its retired one once its entity is gone. */
static bool resolve_target(app_scene_authoring *scene, game_behavior_attachment_info *out)
{
    app_runtime_target target = scene->debug_target;
    game_behavior_world *behaviors = debug_behaviors(scene, target);
    uint64_t session = scene->debug_sessions[target];
    if (scene_debug_resolve(&scene->debug, debug_world(scene, target), behaviors, session, out)) {
        return true;
    }
    scene->debug_destroyed = scene_debug_retired(&scene->debug, behaviors, session, out);
    return scene->debug_destroyed;
}

/* A retired instance lives while it is the target. A scene without a
   preview, as game_behavior_inspect allows, has no preview world. */
static void release_retired(app_scene_authoring *scene)
{
    for (int target = APP_RUNTIME_PREVIEW; target <= APP_RUNTIME_SCENE; target++) {
        game_behavior_world *behaviors = debug_behaviors(scene, target);
        game_behavior_attachment_info retired;
        if (behaviors && game_behavior_retired_target(behaviors, &retired) &&
            !(scene->debug_destroyed && target == (int)scene->debug_target)) {
            game_behavior_release_retired(behaviors);
        }
    }
}

void sync_editor_inspection(app_scene_authoring *scene)
{
    follow_selection(scene);
    follow_newest(scene);
    game_behavior_attachment_info info;
    scene->debug_status = APP_DEBUG_NONE;
    scene->debug_destroyed = false;
    if (scene->debug.asset_name) {
        scene->debug_status = APP_DEBUG_UNAVAILABLE;
        if (resolve_target(scene, &info)) {
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
    bool running = available && !scene->debug_destroyed;
    /* Only the target pays for tracing, and only while a graph shows it. */
    bool traced = running && !scene->debug_hidden;
    for (int target = APP_RUNTIME_PREVIEW; target <= APP_RUNTIME_SCENE; target++) {
        game_behavior_inspect(
            debug_behaviors(scene, target),
            traced && target == (int)scene->debug_target ? info.instance : NULL
        );
    }
    bool behavior = scene->graph_document_kind == APP_EDITOR_DOCUMENT_BEHAVIOR;
    editor_set_inspection_blocked(scene->graph_editor, behavior && !available);
    const flow_run *run = NULL;
    for (int i = running ? acount(info.instance->activations) - 1 : -1; i >= 0; i--) {
        flow_state state = flow_run_status(info.instance->activations[i]);
        if (state != FLOW_COMPLETED && state != FLOW_FAILED) {
            run = info.instance->activations[i];
            break;
        }
    }
    editor_set_inspection_instance(scene->graph_editor, available ? info.instance : NULL);
    editor_set_inspection_run(scene->graph_editor, run);
    release_retired(scene);
}

int app_scene_authoring_choose_debug_target(app_scene_authoring *scene, ecs_entity entity)
{
    game_behavior_world *behaviors = debug_behaviors(scene, scene->active_runtime_target);
    game_behavior_attachment_info info = { 0 };
    for (int i = 0; entity.slot && !info.asset_name &&
                    i < game_behavior_attachment_count_for_entity(behaviors, entity);
         i++) {
        game_behavior_attachment_info candidate;
        if (game_behavior_attachment_info_at(behaviors, entity, i, &candidate) == 0 &&
            !strcmp(candidate.asset_name, scene->definition->name)) {
            info = candidate;
        }
    }
    if (entity.slot && !info.asset_name) {
        return -1;
    }
    stop_following_newest(scene);
    scene_debug_clear(&scene->debug);
    if (info.asset_name) {
        target_instance(scene, scene->active_runtime_target, &info);
    }
    sync_editor_inspection(scene);
    return 0;
}

void app_scene_authoring_follow_newest(app_scene_authoring *scene, const char *prototype)
{
    char *name = xstrdup(prototype);
    stop_following_newest(scene);
    scene->debug_newest = name;
    /* The newest running now, else the first to spawn. */
    scene_debug_clear(&scene->debug);
    sync_editor_inspection(scene);
}

void app_scene_authoring_set_debug_hidden(app_scene_authoring *scene, bool hidden)
{
    scene->debug_hidden = hidden;
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
            target_instance(scene, target, &info);
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
    if (!pinned) {
        free(scene->debug_followed.asset);
        scene->debug_followed.asset = NULL; /* follow the selection again */
    }
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
        app_scene_renderer_free(scene->renderer);
        scene_history_free(scene->history);
        free(scene);
        return NULL;
    }
    scene->content_root = xstrdup(content_root);
    scene_view_init(scene);
    scene->asset_index = reference_index_new();
    scene->asset_index_generation = 1;
    content_refresh(scene);
    scene->selected = -1;
    scene->selected_prototype = -1;
    scene->runtime_preview_prototype = -1;
    scene->view_options = (app_scene_view_options){ .lighting = true };
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
    scene_kept_clear(scene);
    scene_selection_free(scene);
    scenario_document_free(scene->scenario);
    av_value_free(scene->scenario_saved_state);
    free(scene->ui);
    free(scene->default_ui);
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
    afree(scene->marquee_base);
    scene_view_free(scene);
    reference_index_free(scene->asset_index);
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
    /* Removing the actors above syncs the debug target. */
    scene_debug_clear(&scene->debug);
    free(scene->debug_followed.asset);
    free(scene->debug_newest);
    for (int i = 0; i < acount(scene->spawn_breaks); i++) {
        free(scene->spawn_breaks[i]);
    }
    afree(scene->spawn_breaks);
    afree(scene->spawn_seen);
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
    scene_selection_track(scene);
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
    behavior_asset *copy = behavior_asset_clone(editor_behavior_asset(scene->graph_editor));
    if (!copy) {
        return -1;
    }
    return game_system_document_replace_behavior_asset(scene->system_document, copy);
}
