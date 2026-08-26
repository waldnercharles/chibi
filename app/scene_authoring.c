#include "scene_authoring.h"

#include "../core/array.h"
#include "../core/serialize.h"
#include "../core/xalloc.h"
#include "../editor/editor.h"
#include "../editor/ui_shell.h"
#include "../game/generated.h"
#include "../game/prototype.h"
#include "../game/scene_document.h"
#include "scene_draw.h"
#include "scene_imgui.h"

#include <ctype.h>
#include <dirent.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static const char APP_SCENE_DOCUMENT_ID[] = "Scene";
static const char CONTENT_FOLDER_ICON[] = "\xef\x81\xbb";
static const char CONTENT_FOLDER_OPEN_ICON[] = "\xef\x81\xbc";

enum
{
    CONTENT_PATH_CAP = 512,
    CONTENT_HISTORY_LIMIT = 20,
    CONTENT_POPUP_NO_OPEN_OVER_ITEMS = 1 << 8,
    CONTENT_ICON_BACK = 0xf060,
    CONTENT_ICON_FORWARD = 0xf061,
    CONTENT_ICON_CHEVRON_RIGHT = 0xf054,
    CONTENT_ICON_SETTINGS = 0xf013,
};

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
} scene_actor;

typedef struct
{
    char *path;
    game_prototype *prototype;
    int dirty;
} scene_prototype_asset;

typedef struct
{
    char *path;
    bool directory;
} scene_content_entry;

static const float SCENE_CONTEXT_WIDTH = 270.0f;

#define CONTENT_DRAWER_FONT_SIZE 14.0f
#define CONTENT_DRAWER_SOURCE_WIDTH 250.0f
#define CONTENT_DRAWER_SOURCE_WIDTH_NARROW 205.0f

struct app_scene_authoring
{
    ecs_world *world;
    game_live_world *live;
    game_live_definition *definition;
    const blueprint *editor_working;
    editor *graph_editor;
    unsigned editor_revision_seen;
    const blueprint *editor_blueprint_seen;
    game_live_definition *editor_definition_seen;
    bool editor_state_seen;
    bool behavior_apply_pending;
    app_scene_prototype_resolve_fn resolve_prototype;
    void *resolve_user;
    game_prototype_registry registry;
    engine *runtime;
    scene_actor *actors;
    scene_prototype_asset *prototypes;
    scene_content_entry *content;
    char *content_root;
    char *content_dir;
    char *content_selected_path;
    char **content_history;
    int content_history_index;
    bool content_show_thumbnails;
    int selected;
    int selected_prototype;
    int selected_attachment;
    char asset_search[128];
    app_editor_document_context document;
    char message[256];
    char prototype_message[256];
};

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

static bool content_path_is_root(const app_scene_authoring *scene, const char *path)
{
    return strcmp(scene->content_root, path) == 0;
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

static bool content_path_is_breadcrumb(const app_scene_authoring *scene, const char *path)
{
    return strcmp(scene->content_dir, path) == 0 ||
           content_path_is_descendant(path, scene->content_dir);
}

static bool content_folder_has_children(const app_scene_authoring *scene, const char *path)
{
    for (int i = 0; i < acount(scene->content); i++) {
        const scene_content_entry *entry = &scene->content[i];
        if (entry->directory && content_path_is_child(path, entry->path)) {
            return true;
        }
    }
    return false;
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

static void content_history_reset(app_scene_authoring *scene)
{
    for (int i = 0; i < acount(scene->content_history); i++) {
        free(scene->content_history[i]);
    }
    asetlen(scene->content_history, 0);
    scene->content_history_index = -1;
}

static void content_history_push(app_scene_authoring *scene, const char *path)
{
    if (scene->content_history_index >= 0 &&
        strcmp(scene->content_history[scene->content_history_index], path) == 0) {
        return;
    }
    while (scene->content_history_index + 1 < acount(scene->content_history)) {
        free(scene->content_history[acount(scene->content_history) - 1]);
        asetlen(scene->content_history, acount(scene->content_history) - 1);
    }
    apush(scene->content_history, xstrdup(path));
    scene->content_history_index = acount(scene->content_history) - 1;
    if (acount(scene->content_history) > CONTENT_HISTORY_LIMIT) {
        free(scene->content_history[0]);
        memmove(
            scene->content_history,
            scene->content_history + 1,
            (size_t)(acount(scene->content_history) - 1) * sizeof *scene->content_history
        );
        asetlen(scene->content_history, acount(scene->content_history) - 1);
        scene->content_history_index--;
    }
}

static void content_change_dir(app_scene_authoring *scene, const char *path, bool push_history)
{
    if (!path || !path[0] ||
        (!content_path_is_root(scene, path) &&
         !content_path_is_descendant(scene->content_root, path))) {
        return;
    }
    free(scene->content_dir);
    scene->content_dir = xstrdup(path);
    free(scene->content_selected_path);
    scene->content_selected_path = NULL;
    if (push_history) {
        content_history_push(scene, path);
    }
}

static void content_go_back(app_scene_authoring *scene)
{
    if (scene->content_history_index < 1) {
        return;
    }
    scene->content_history_index--;
    content_change_dir(scene, scene->content_history[scene->content_history_index], false);
}

static void content_go_forward(app_scene_authoring *scene)
{
    if (scene->content_history_index + 1 >= acount(scene->content_history)) {
        return;
    }
    scene->content_history_index++;
    content_change_dir(scene, scene->content_history[scene->content_history_index], false);
}

static scene_actor *selected_actor(app_scene_authoring *scene)
{
    if (!scene || scene->selected < 0 || scene->selected >= acount(scene->actors)) {
        return NULL;
    }
    return &scene->actors[scene->selected];
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

static void set_document(app_scene_authoring *scene, app_editor_document_kind kind, const char *id)
{
    scene->document = (app_editor_document_context){ kind, id };
    switch (kind) {
        case APP_EDITOR_DOCUMENT_SCENE:
            editor_ui_request_focus(EDITOR_UI_FOCUS_SCENE);
            return;
        case APP_EDITOR_DOCUMENT_BEHAVIOR:
            editor_ui_request_focus(EDITOR_UI_FOCUS_NODE_CANVAS);
            return;
        case APP_EDITOR_DOCUMENT_PROTOTYPE:
            editor_ui_request_focus(EDITOR_UI_FOCUS_NONE);
            return;
    }
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
    }
    return false;
}

static int prototype_index(app_scene_authoring *scene, const char *name)
{
    scene_prototype_asset *asset = find_prototype(scene, name);
    return asset ? (int)(asset - scene->prototypes) : -1;
}

static int selected_attachment(app_scene_authoring *scene, game_behavior_attachment_info *out)
{
    scene_actor *actor = selected_actor(scene);
    if (!actor || !out) {
        return 0;
    }
    game_behavior_world *behaviors = scene->live->behaviors;
    int count = game_behavior_attachment_count_for_entity(behaviors, actor->entity);
    if (count == 0) {
        scene->selected_attachment = -1;
        return 0;
    }
    if (scene->selected_attachment < 0 || scene->selected_attachment >= count) {
        scene->selected_attachment = 0;
    }
    return game_behavior_attachment_info_at(behaviors, actor->entity, scene->selected_attachment, out) ==
           0;
}

static void select_prototype(app_scene_authoring *scene, int index)
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
    set_document(
        scene,
        APP_EDITOR_DOCUMENT_PROTOTYPE,
        scene->prototypes[index].prototype->name
    );
}

static void select_instance(app_scene_authoring *scene, int actor_index)
{
    if (actor_index < 0 || actor_index >= acount(scene->actors)) {
        return;
    }
    scene->selected = actor_index;
    set_document(scene, APP_EDITOR_DOCUMENT_SCENE, APP_SCENE_DOCUMENT_ID);
    scene->selected_attachment = game_behavior_attachment_count_for_entity(
                                     scene->live->behaviors,
                                     scene->actors[actor_index].entity
                                 )
                                   ? 0
                                   : -1;
}

static void sync_editor_inspection(app_scene_authoring *scene)
{
    game_behavior_attachment_info info;
    int has_attachment = selected_attachment(scene, &info);
    const flow_run *run = NULL;
    const blueprint_instance_trace_entry *trace = NULL;
    int trace_count = 0;
    if (has_attachment) {
        for (int i = acount(info.instance->activations) - 1; i >= 0; i--) {
            flow_state state = flow_run_status(info.instance->activations[i]);
            if (state != FLOW_COMPLETED && state != FLOW_FAILED) {
                run = info.instance->activations[i];
                break;
            }
        }
        trace = blueprint_instance_trace(info.instance, &trace_count);
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
}

static void remove_selected(app_scene_authoring *scene)
{
    scene_actor *actor = selected_actor(scene);
    if (!actor) {
        return;
    }
    game_behavior_destroy_entity(scene->live->behaviors, actor->entity);
    free_actor(actor);
    adel(scene->actors, scene->selected);
    scene->selected_attachment = -1;
    if (scene->selected >= acount(scene->actors)) {
        scene->selected = acount(scene->actors) - 1;
    }
    if (scene->selected >= 0) {
        select_instance(scene, scene->selected);
    }
}

app_scene_authoring *app_scene_authoring_new(
    ecs_world *world,
    game_live_world *live,
    game_live_definition *definition,
    const blueprint *editor_working,
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
    scene->world = world;
    scene->live = live;
    scene->definition = definition;
    scene->editor_working = editor_working;
    scene->graph_editor = graph_editor;
    scene->editor_revision_seen = editor_edit_revision(graph_editor);
    scene->editor_blueprint_seen = editor_working;
    scene->editor_definition_seen = definition;
    scene->editor_state_seen = true;
    scene->resolve_prototype = resolve_prototype;
    scene->resolve_user = resolve_user;
    scene->registry = app_component_registry();
    scene->runtime = runtime;
    scene->content_root = xstrdup("content");
    scene->content_dir = xstrdup(scene->content_root);
    scene->content_show_thumbnails = true;
    scene->content_history_index = -1;
    content_history_push(scene, scene->content_dir);
    content_refresh(scene);
    scene->selected = -1;
    scene->selected_prototype = -1;
    scene->selected_attachment = -1;
    set_document(scene, APP_EDITOR_DOCUMENT_SCENE, APP_SCENE_DOCUMENT_ID);
    snprintf(scene->message, sizeof scene->message, "No scene entities yet.");
    snprintf(scene->prototype_message, sizeof scene->prototype_message, "Select a prototype in Content Drawer.");
    return scene;
}

void app_scene_authoring_set_editor_working(app_scene_authoring *scene, const blueprint *editor_working)
{
    if (scene && editor_working) {
        scene->editor_working = editor_working;
    }
}

static void queue_behavior_apply(app_scene_authoring *scene)
{
    unsigned revision = editor_edit_revision(scene->graph_editor);
    if (scene->editor_state_seen && scene->editor_definition_seen == scene->definition &&
        scene->editor_blueprint_seen == scene->editor_working &&
        scene->editor_revision_seen == revision) {
        return;
    }
    scene->editor_state_seen = true;
    scene->editor_definition_seen = scene->definition;
    scene->editor_blueprint_seen = scene->editor_working;
    scene->editor_revision_seen = revision;
    scene->behavior_apply_pending = true;
}

int app_scene_authoring_apply_pending(app_scene_authoring *scene)
{
    if (!scene) {
        return -1;
    }
    queue_behavior_apply(scene);
    if (!scene->behavior_apply_pending) {
        return 0;
    }
    scene->behavior_apply_pending = false;
    if (!scene->definition || !scene->editor_working ||
        game_live_sync_working(scene->definition, scene->editor_working) != 0) {
        snprintf(scene->message, sizeof scene->message, "Could not synchronize behavior graph.");
        return -1;
    }
    int result = game_live_apply(
        scene->live,
        scene->definition,
        scene->message,
        sizeof scene->message
    );
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
    content_history_reset(scene);
    content_change_dir(scene, scene->content_root, true);
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
    FILE *file = fopen(path, "wb");
    if (!file) {
        free(json);
        return -1;
    }
    int result = fputs(json, file) >= 0 ? 0 : -1;
    fclose(file);
    free(json);
    if (result == 0) {
        free(asset->path);
        asset->path = xstrdup(path);
        asset->dirty = 0;
    }
    return result;
}

int app_scene_authoring_seed_default(app_scene_authoring *scene)
{
    if (!scene) {
        return -1;
    }
    int behavior_actor = -1;
    for (int i = 0; i < acount(scene->prototypes); i++) {
        const char *prototype_name = scene->prototypes[i].prototype->name;
        char actor_name[96];
        snprintf(actor_name, sizeof actor_name, "%s_1", prototype_name);
        if (add_actor(scene, actor_name, prototype_name) != 0) {
            return -1;
        }
        int actor = acount(scene->actors) - 1;
        if (game_behavior_attachment_count_for_entity(
                scene->live->behaviors,
                scene->actors[actor].entity
            )) {
            behavior_actor = actor;
        }
    }
    if (behavior_actor >= 0) {
        select_instance(scene, behavior_actor);
    }
    return 0;
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

static int name_in_list(const char *const *names, int count, const char *name)
{
    for (int i = 0; i < count; i++) {
        if (strcmp(names[i], name) == 0) {
            return 1;
        }
    }
    return 0;
}

int app_scene_authoring_save(const app_scene_authoring *scene, const char *path)
{
    if (!scene || !path) {
        return -1;
    }
    game_scene_document *document = game_scene_document_new("Scene");
    if (!document) {
        return -1;
    }
    game_behavior_world *behaviors = scene->live->behaviors;
    for (int i = 0; i < acount(scene->actors); i++) {
        const scene_actor *actor = &scene->actors[i];
        const char **names = NULL;
        const char **additions = NULL;
        const char **removals = NULL;
        int count = game_behavior_attachment_count_for_entity(behaviors, actor->entity);
        for (int j = 0; j < count; j++) {
            game_behavior_attachment_info info;
            if (game_behavior_attachment_info_at(behaviors, actor->entity, j, &info) != 0) {
                afree(names);
                afree(additions);
                afree(removals);
                game_scene_document_free(document);
                return -1;
            }
            apush(names, info.asset_name);
            if (!game_prototype_has_graph(actor->base, info.asset_name)) {
                apush(additions, info.asset_name);
            }
        }
        for (int j = 0; actor->base && j < acount(actor->base->graphs); j++) {
            const char *name = actor->base->graphs[j];
            if (!name_in_list(names, acount(names), name)) {
                apush(removals, name);
            }
        }
        int result = game_scene_document_add_entity(
            document,
            actor->name,
            actor->prototype_name,
            names,
            acount(names)
        );
        if (result == 0) {
            char *components = game_prototype_capture_overrides_json(
                actor->base,
                scene->world,
                actor->entity
            );
            result = game_scene_document_set_entity_components(document, i, components);
            free(components);
        }
        if (result == 0) {
            result = game_scene_document_set_entity_behavior_overrides(
                document,
                i,
                additions,
                acount(additions),
                removals,
                acount(removals)
            );
        }
        afree(names);
        afree(additions);
        afree(removals);
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
    FILE *file = fopen(path, "wb");
    if (!file) {
        free(json);
        return -1;
    }
    int result = fputs(json, file) >= 0 ? 0 : -1;
    fclose(file);
    free(json);
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
    blueprint *working = blueprint_load_json(json);
    free(json);
    if (!working || !working->name ||
        game_live_find_definition(scene->live, working->name)) {
        blueprint_free(working);
        return 0;
    }
    game_live_definition *definition = game_live_definition_new(working->name, working);
    if (!definition) {
        blueprint_free(working);
        return 0;
    }
    if (game_live_add_definition(scene->live, definition) != 0) {
        game_live_definition_free(definition);
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
    int loaded = 0;
    for (int i = 0; i < acount(scene->content); i++) {
        if (!scene->content[i].directory &&
            content_path_in_folder(scene, scene->content[i].path, "behaviors")) {
            loaded += load_content_behavior(scene, scene->content[i].path);
        }
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
        game_scene_entity *entity = &document->entities[i];
        if (!ensure_prototype(scene, entity->prototype)) {
            snprintf(
                scene->message,
                sizeof scene->message,
                "Unknown scene prototype %s.",
                entity->prototype
            );
            game_scene_document_free(document);
            return -1;
        }
        char **names = entity->has_behavior_overrides ? entity->behavior_additions
                                                      : entity->behaviors;
        for (int j = 0; j < acount(names); j++) {
            if (!game_behavior_asset_registered(behaviors, names[j])) {
                snprintf(scene->message, sizeof scene->message, "Unknown behavior asset %s.", names[j]);
                game_scene_document_free(document);
                return -1;
            }
        }
        if (entity->has_behavior_overrides) {
            for (int j = 0; j < acount(entity->behavior_removals); j++) {
                if (!game_behavior_asset_registered(behaviors, entity->behavior_removals[j])) {
                    snprintf(
                        scene->message,
                        sizeof scene->message,
                        "Unknown behavior asset %s.",
                        entity->behavior_removals[j]
                    );
                    game_scene_document_free(document);
                    return -1;
                }
            }
        }
    }
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
        if (entity->has_behavior_overrides) {
            for (int j = 0; j < acount(entity->behavior_removals); j++) {
                game_behavior_detach_asset(behaviors, actor->entity, entity->behavior_removals[j]);
            }
            for (int j = 0; j < acount(entity->behavior_additions); j++) {
                if (game_behavior_attach(behaviors, actor->entity, entity->behavior_additions[j]) !=
                    0) {
                    game_scene_document_free(document);
                    snprintf(scene->message, sizeof scene->message, "Could not attach scene behavior.");
                    return -1;
                }
            }
        } else {
            game_behavior_detach_entity(behaviors, actor->entity);
            for (int j = 0; j < acount(entity->behaviors); j++) {
                if (game_behavior_attach(behaviors, actor->entity, entity->behaviors[j]) !=
                    0) {
                    game_scene_document_free(document);
                    snprintf(scene->message, sizeof scene->message, "Could not attach scene behavior.");
                    return -1;
                }
            }
        }
    }
    game_scene_document_free(document);
    snprintf(scene->message, sizeof scene->message, "Loaded scene from %s.", path);
    return 0;
}

const char *app_scene_authoring_message(const app_scene_authoring *scene)
{
    return scene ? scene->message : NULL;
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
    editor_set_inspection_instance(scene->graph_editor, NULL);
    editor_set_inspection_run(scene->graph_editor, NULL);
    while (acount(scene->actors)) {
        scene->selected = acount(scene->actors) - 1;
        remove_selected(scene);
    }
    afree(scene->actors);
    for (int i = 0; i < acount(scene->prototypes); i++) {
        free(scene->prototypes[i].path);
        game_prototype_free(scene->prototypes[i].prototype);
    }
    afree(scene->prototypes);
    for (int i = 0; i < acount(scene->content); i++) {
        free(scene->content[i].path);
    }
    afree(scene->content);
    free(scene->content_root);
    free(scene->content_dir);
    free(scene->content_selected_path);
    for (int i = 0; i < acount(scene->content_history); i++) {
        free(scene->content_history[i]);
    }
    afree(scene->content_history);
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
        select_prototype(scene, acount(scene->prototypes) - 1);
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
        blueprint *working = blueprint_new(name);
        if (!working || blueprint_set_owner_type(working, "ecs.entity") != 0) {
            blueprint_free(working);
            break;
        }
        game_live_definition *definition = game_live_definition_new(name, working);
        if (!definition || game_live_add_definition(scene->live, definition) != 0) {
            if (definition) {
                game_live_definition_free(definition);
            } else {
                blueprint_free(working);
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

static int open_behavior_asset(app_scene_authoring *scene, const char *asset_name)
{
    game_live_definition *target = game_live_find_definition(scene->live, asset_name);
    if (!target || !scene->graph_editor) {
        return -1;
    }
    (void)app_scene_authoring_apply_pending(scene);
    blueprint *working = target->working;
    graph *default_graph = blueprint_find_graph(working, 0);
    if (!default_graph) {
        return -1;
    }
    if (editor_open_blueprint(scene->graph_editor, working, default_graph->graph_id) != 0) {
        return -1;
    }
    scene->definition = target;
    set_document(scene, APP_EDITOR_DOCUMENT_BEHAVIOR, target->name);
    scene->editor_working = editor_blueprint(scene->graph_editor);
    snprintf(scene->message, sizeof scene->message, "Editing behavior asset %s.", asset_name);
    return 0;
}

int app_scene_authoring_open_behavior(app_scene_authoring *scene, const char *asset_name)
{
    return open_behavior_asset(scene, asset_name);
}

static int prototype_instance_count(const app_scene_authoring *scene, const char *prototype_name)
{
    int count = 0;
    for (int i = 0; i < acount(scene->actors); i++) {
        count += strcmp(scene->actors[i].prototype_name, prototype_name) == 0;
    }
    return count;
}

static int spawn_selected_prototype(app_scene_authoring *scene)
{
    scene_prototype_asset *asset = selected_prototype(scene);
    if (!asset) {
        return -1;
    }
    const char *prototype_name = asset->prototype->name;
    int number = prototype_instance_count(scene, prototype_name) + 1;
    char name[96];
    snprintf(name, sizeof name, "%s_%d", prototype_name, number);
    return add_actor(scene, name, prototype_name);
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

static void draw_content_breadcrumbs(app_scene_authoring *scene)
{
    if (scene->asset_search[0]) {
        ImGui_TextUnformatted("Search results");
        return;
    }

    if (editor_ui_link("content_breadcrumb_all", "All")) {
        content_change_dir(scene, scene->content_root, true);
    }
    ImGui_SameLine();
    editor_ui_breadcrumb_separator(CONTENT_ICON_CHEVRON_RIGHT);
    ImGui_SameLine();
    if (editor_ui_link("content_breadcrumb_content", "Content")) {
        content_change_dir(scene, scene->content_root, true);
    }

    if (content_path_is_root(scene, scene->content_dir)) {
        return;
    }

    size_t root_size = strlen(scene->content_root);
    const char *cursor = scene->content_dir + root_size;
    if (*cursor == '/') {
        cursor++;
    }
    char prefix[CONTENT_PATH_CAP];
    snprintf(prefix, sizeof prefix, "%s", scene->content_root);
    int breadcrumb = 1;
    while (*cursor) {
        const char *slash = strchr(cursor, '/');
        size_t component_size = slash ? (size_t)(slash - cursor) : strlen(cursor);
        size_t prefix_size = strlen(prefix);
        if (prefix_size + component_size + 2 >= sizeof prefix) {
            return;
        }
        prefix[prefix_size++] = '/';
        memcpy(prefix + prefix_size, cursor, component_size);
        prefix[prefix_size + component_size] = '\0';

        ImGui_SameLine();
        editor_ui_breadcrumb_separator(CONTENT_ICON_CHEVRON_RIGHT);
        ImGui_SameLine();
        char component[CONTENT_PATH_CAP];
        memcpy(component, cursor, component_size);
        component[component_size] = '\0';
        char link_id[32];
        snprintf(link_id, sizeof link_id, "content_breadcrumb_%d", breadcrumb++);
        if (editor_ui_link(link_id, component)) {
            content_change_dir(scene, prefix, true);
        }
        if (!slash) {
            return;
        }
        cursor = slash + 1;
    }
}

static void draw_source_children(app_scene_authoring *scene, const char *parent)
{
    for (int i = 0; i < acount(scene->content); i++) {
        scene_content_entry *entry = &scene->content[i];
        if (!entry->directory || !content_path_is_child(parent, entry->path)) {
            continue;
        }
        bool breadcrumb = content_path_is_breadcrumb(scene, entry->path);
        bool has_children = content_folder_has_children(scene, entry->path);
        char label[CONTENT_PATH_CAP];
        snprintf(
            label,
            sizeof label,
            "%s %s",
            breadcrumb ? CONTENT_FOLDER_OPEN_ICON : CONTENT_FOLDER_ICON,
            content_basename(entry->path)
        );
        ImGui_PushIDInt(i);
        if (breadcrumb) {
            ImGui_SetNextItemOpen(true, ImGuiCond_Always);
        }
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth;
        if (strcmp(scene->content_dir, entry->path) == 0) {
            flags |= ImGuiTreeNodeFlags_Selected;
        }
        if (!has_children) {
            flags |= ImGuiTreeNodeFlags_Leaf;
        }
        bool open = ImGui_TreeNodeEx(label, flags);
        if (ImGui_IsItemHovered(0)) {
            ImGui_SetMouseCursor(ImGuiMouseCursor_Hand);
        }
        if (ImGui_IsItemClicked()) {
            content_change_dir(scene, entry->path, true);
        }
        if (open) {
            if (has_children) {
                draw_source_children(scene, entry->path);
            }
            ImGui_TreePop();
        }
        ImGui_PopID();
    }
}

static void draw_content_sources(app_scene_authoring *scene)
{
    ImGui_TextUnformatted("TestProject");
    ImGui_SetNextItemOpen(true, ImGuiCond_Always);
    bool all_open = ImGui_TreeNodeEx("\xef\x81\xbc All", ImGuiTreeNodeFlags_SpanAvailWidth);
    if (ImGui_IsItemHovered(0)) {
        ImGui_SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    if (!all_open) {
        return;
    }
    ImGui_PushIDInt(1);
    bool content_breadcrumb = content_path_is_breadcrumb(scene, scene->content_root);
    bool has_children = content_folder_has_children(scene, scene->content_root);
    char label[CONTENT_PATH_CAP];
    snprintf(label, sizeof label, "%s Content", content_breadcrumb ? CONTENT_FOLDER_OPEN_ICON : CONTENT_FOLDER_ICON);
    ImGui_SetNextItemOpen(content_breadcrumb, ImGuiCond_Always);
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth;
    if (strcmp(scene->content_dir, scene->content_root) == 0) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }
    if (!has_children) {
        flags |= ImGuiTreeNodeFlags_Leaf;
    }
    bool content_open = ImGui_TreeNodeEx(label, flags);
    if (ImGui_IsItemHovered(0)) {
        ImGui_SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    if (ImGui_IsItemClicked()) {
        content_change_dir(scene, scene->content_root, true);
    }
    if (content_open) {
        if (has_children) {
            draw_source_children(scene, scene->content_root);
        }
        ImGui_TreePop();
    }
    ImGui_PopID();
    ImGui_TreePop();
}

static void draw_content_settings(app_scene_authoring *scene)
{
    if (editor_ui_right_icon_button("content_settings", CONTENT_ICON_SETTINGS)) {
        ImGui_OpenPopup("content_settings_popup", 0);
    }
    if (!ImGui_BeginPopup("content_settings_popup", 0)) {
        return;
    }
    ImGui_TextUnformatted("Asset View");
    ImGui_Separator();
    if (ImGui_MenuItemEx("Thumbnails", NULL, scene->content_show_thumbnails, true)) {
        scene->content_show_thumbnails = true;
    }
    if (ImGui_MenuItemEx("List", NULL, !scene->content_show_thumbnails, true)) {
        scene->content_show_thumbnails = false;
    }
    if (ImGui_MenuItem("Refresh content")) {
        app_scene_authoring_refresh_content(scene);
        ImGui_CloseCurrentPopup();
    }
    ImGui_EndPopup();
}

static void draw_content_toolbar(app_scene_authoring *scene)
{
    bool can_back = scene->content_history_index > 0;
    bool can_forward = scene->content_history_index + 1 <
                       acount(scene->content_history);
    if (editor_ui_icon_button("content_back", CONTENT_ICON_BACK, can_back)) {
        content_go_back(scene);
    }
    ImGui_SameLine();
    if (editor_ui_icon_button("content_forward", CONTENT_ICON_FORWARD, can_forward)) {
        content_go_forward(scene);
    }
    ImGui_SameLine();
    draw_content_breadcrumbs(scene);
    draw_content_settings(scene);
    ImGui_Separator();
}

static editor_ui_asset_kind content_entry_kind(
    const app_scene_authoring *scene,
    const char *path,
    int *prototype_index,
    int *behavior_index
)
{
    *prototype_index = content_prototype_index(scene, path);
    *behavior_index = *prototype_index >= 0 ? -1 : content_behavior_index(scene, path);
    if (*prototype_index >= 0) {
        return EDITOR_UI_ASSET_PROTOTYPE;
    }
    if (*behavior_index >= 0) {
        return EDITOR_UI_ASSET_BEHAVIOR;
    }
    return EDITOR_UI_ASSET_FILE;
}

static editor_ui_asset_selection content_entry_selection(
    const app_scene_authoring *scene,
    const char *path,
    editor_ui_asset_kind kind,
    int prototype_index,
    int behavior_index
)
{
    if (!scene->content_selected_path ||
        strcmp(scene->content_selected_path, path) != 0) {
        return EDITOR_UI_ASSET_NOT_SELECTED;
    }
    if (kind == EDITOR_UI_ASSET_PROTOTYPE &&
        document_asset_active(
            scene,
            APP_EDITOR_DOCUMENT_PROTOTYPE,
            scene->prototypes[prototype_index].prototype->name
        )) {
        return EDITOR_UI_ASSET_ACTIVE;
    }
    if (kind == EDITOR_UI_ASSET_BEHAVIOR) {
        game_behavior_world *behaviors = scene->live->behaviors;
        if (document_asset_active(
                scene,
                APP_EDITOR_DOCUMENT_BEHAVIOR,
                game_behavior_asset_name_at(behaviors, behavior_index)
            )) {
            return EDITOR_UI_ASSET_ACTIVE;
        }
    }
    return EDITOR_UI_ASSET_SELECTED;
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

static void open_content_entry(app_scene_authoring *scene, const scene_content_entry *entry)
{
    if (entry->directory) {
        content_change_dir(scene, entry->path, true);
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
    }
}

static int content_entry_at(const app_scene_authoring *scene, const char *path)
{
    for (int i = 0; i < acount(scene->content); i++) {
        if (strcmp(scene->content[i].path, path) == 0) {
            return i;
        }
    }
    return -1;
}

static bool draw_content_item(app_scene_authoring *scene, int index)
{
    scene_content_entry *entry = &scene->content[index];
    int prototype_index = -1;
    int behavior_index = -1;
    editor_ui_asset_kind kind = entry->directory
                                  ? EDITOR_UI_ASSET_FOLDER
                                  : content_entry_kind(scene, entry->path, &prototype_index, &behavior_index);
    const char *name = content_basename(entry->path);
    if (prototype_index >= 0) {
        name = scene->prototypes[prototype_index].prototype->name;
    } else if (behavior_index >= 0) {
        game_behavior_world *behaviors = scene->live->behaviors;
        name = game_behavior_asset_name_at(behaviors, behavior_index);
    }
    editor_ui_asset_selection
        selection = content_entry_selection(scene, entry->path, kind, prototype_index, behavior_index);
    bool pressed = scene->content_show_thumbnails
                     ? editor_ui_asset_card(entry->path, name, kind, selection)
                     : editor_ui_asset_row(entry->path, name, entry->path, kind, selection);
    if (pressed) {
        select_content_entry(scene, entry);
        if (entry->directory) {
            open_content_entry(scene, entry);
            return true;
        }
        if (ImGui_IsMouseDoubleClicked(0)) {
            open_content_entry(scene, entry);
        }
    }
    if (ImGui_IsItemHovered(0) && ImGui_IsMouseReleased(1)) {
        select_content_entry(scene, entry);
        ImGui_OpenPopup("content_item_context", 0);
    }
    return false;
}

static int *content_visible_entries(const app_scene_authoring *scene)
{
    int *visible = NULL;
    bool searching = scene->asset_search[0] != '\0';
    if (searching) {
        for (int i = 0; i < acount(scene->content); i++) {
            const scene_content_entry *entry = &scene->content[i];
            if (!entry->directory && asset_search_score(scene, entry->path) >= 0) {
                apush(visible, i);
                for (int j = acount(visible) - 1; j > 0; j--) {
                    int current = visible[j];
                    int previous = visible[j - 1];
                    int current_score = asset_search_score(
                        scene,
                        scene->content[current].path
                    );
                    int previous_score = asset_search_score(
                        scene,
                        scene->content[previous].path
                    );
                    if (current_score < previous_score ||
                        (current_score == previous_score &&
                         strcmp(
                             scene->content[current].path,
                             scene->content[previous].path
                         ) >= 0)) {
                        break;
                    }
                    visible[j] = previous;
                    visible[j - 1] = current;
                }
            }
        }
        return visible;
    }
    for (int directory = 1; directory >= 0; directory--) {
        for (int i = 0; i < acount(scene->content); i++) {
            const scene_content_entry *entry = &scene->content[i];
            if ((int)entry->directory == directory &&
                content_path_is_child(scene->content_dir, entry->path)) {
                apush(visible, i);
            }
        }
    }
    return visible;
}

static void draw_content_item_popup(app_scene_authoring *scene)
{
    if (!ImGui_BeginPopup("content_item_context", 0)) {
        return;
    }
    int index = scene->content_selected_path
                  ? content_entry_at(scene, scene->content_selected_path)
                  : -1;
    if (index >= 0) {
        scene_content_entry *entry = &scene->content[index];
        ImGui_TextUnformatted(content_basename(entry->path));
        ImGui_Separator();
        if (ImGui_MenuItem("Open")) {
            open_content_entry(scene, entry);
            ImGui_CloseCurrentPopup();
        }
        if (ImGui_MenuItem("Refresh")) {
            app_scene_authoring_refresh_content(scene);
            ImGui_CloseCurrentPopup();
        }
    }
    ImGui_EndPopup();
}

static void draw_content_background_popup(app_scene_authoring *scene)
{
    if (!ImGui_BeginPopupContextWindowEx("content_background_context", CONTENT_POPUP_NO_OPEN_OVER_ITEMS)) {
        return;
    }
    if (ImGui_MenuItem("Refresh")) {
        app_scene_authoring_refresh_content(scene);
        ImGui_CloseCurrentPopup();
    }
    if (ImGui_MenuItem("Back")) {
        content_go_back(scene);
        ImGui_CloseCurrentPopup();
    }
    if (ImGui_MenuItem("Forward")) {
        content_go_forward(scene);
        ImGui_CloseCurrentPopup();
    }
    ImGui_EndPopup();
}

static void draw_content_items(app_scene_authoring *scene)
{
    int *visible = content_visible_entries(scene);
    if (!ImGui_BeginChild("content_items", (ImVec2){ 0.0f, 0.0f }, 0, 0)) {
        ImGui_EndChild();
        afree(visible);
        return;
    }
    if (!acount(visible)) {
        ImGui_TextUnformatted(
            scene->asset_search[0] ? "No matching assets." : "This folder is empty."
        );
    }
    float width = ImGui_GetContentRegionAvail().x;
    int columns = (int)(width / (EDITOR_UI_ASSET_CARD_WIDTH + 12.0f));
    if (columns < 1) {
        columns = 1;
    }
    int column = 0;
    for (int i = 0; i < acount(visible); i++) {
        ImGui_PushIDInt(visible[i]);
        bool changed_directory = draw_content_item(scene, visible[i]);
        ImGui_PopID();
        if (changed_directory) {
            break;
        }
        if (scene->content_show_thumbnails) {
            column++;
            if (column < columns) {
                ImGui_SameLine();
            } else {
                column = 0;
            }
        }
    }
    draw_content_item_popup(scene);
    draw_content_background_popup(scene);
    ImGui_EndChild();
    afree(visible);
}

static void draw_prototype_details(app_scene_authoring *scene)
{
    ImGui_SetNextWindowSize((ImVec2){ 320.0f, 420.0f }, ImGuiCond_FirstUseEver);
    if (!ImGui_Begin("Prototype Details", NULL, 0)) {
        ImGui_End();
        return;
    }
    scene_prototype_asset *asset = selected_prototype(scene);
    if (!asset || !asset->prototype) {
        ImGui_TextUnformatted("Prototype");
        ImGui_Separator();
        ImGui_TextUnformatted("Select a prototype in Content Drawer.");
        ImGui_End();
        return;
    }

    const char *prototype_name = asset->prototype->name;
    ImGui_TextUnformatted("Prototype");
    ImGui_TextUnformatted(prototype_name);
    if (asset->dirty) {
        ImGui_SameLine();
        ImGui_TextUnformatted("(modified)");
    }
    ImGui_Separator();
    ImGui_TextUnformatted("Behaviors");
    int graph_count = acount(asset->prototype->graphs);
    if (!graph_count) {
        ImGui_TextUnformatted("No behaviors attached.");
    }
    for (int i = 0; i < graph_count; i++) {
        ImGui_PushIDInt(i);
        ImGui_TextUnformatted(asset->prototype->graphs[i]);
        ImGui_PopID();
    }
    if (ImGui_Button("Add Behavior")) {
        ImGui_OpenPopup("prototype_add_behavior", 0);
    }
    if (ImGui_BeginPopup("prototype_add_behavior", 0)) {
        game_behavior_world *behaviors = scene->live->behaviors;
        int available = 0;
        for (int i = 0; i < game_behavior_asset_count(behaviors); i++) {
            const char *asset_name = game_behavior_asset_name_at(behaviors, i);
            if (game_prototype_has_graph(asset->prototype, asset_name)) {
                continue;
            }
            available++;
            ImGui_PushIDInt(i);
            if (ImGui_MenuItem(asset_name)) {
                app_scene_authoring_attach_prototype_behavior(scene, prototype_name, asset_name);
                ImGui_CloseCurrentPopup();
            }
            ImGui_PopID();
        }
        if (!available) {
            ImGui_TextUnformatted("No unassigned behavior assets.");
        }
        ImGui_EndPopup();
    }
    ImGui_Separator();
    ImGui_TextUnformatted(scene->prototype_message);
    ImGui_End();
}

static void draw_scene_hierarchy(app_scene_authoring *scene)
{
    ImGui_SetNextWindowSize((ImVec2){ SCENE_CONTEXT_WIDTH, 360.0f }, ImGuiCond_FirstUseEver);
    if (!ImGui_Begin("Scene hierarchy", NULL, 0)) {
        ImGui_End();
        return;
    }
    if (!acount(scene->actors)) {
        ImGui_TextUnformatted("No scene actors.");
    }
    for (int i = 0; i < acount(scene->actors); i++) {
        ImGui_PushIDInt(i);
        if (ImGui_SelectableEx(scene->actors[i].name, i == scene->selected, 0, (ImVec2){ 0.0f, 0.0f })) {
            select_instance(scene, i);
        }
        ImGui_PopID();
    }
    ImGui_End();
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

static void draw_runtime_inspection(app_scene_authoring *scene)
{
    ImGui_SetNextWindowSize((ImVec2){ 360.0f, 440.0f }, ImGuiCond_FirstUseEver);
    if (!ImGui_Begin("Runtime inspection", NULL, 0)) {
        ImGui_End();
        return;
    }

    game_behavior_attachment_info info;
    if (!selected_attachment(scene, &info)) {
        ImGui_TextUnformatted("Select an actor with a behavior attachment.");
        ImGui_End();
        return;
    }

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
            flow_run_status(run) == FLOW_SUSPENDED) {
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
        ImGui_TextUnformatted("No active execution.");
    }

    int trace_count = 0;
    const blueprint_instance_trace_entry *trace = blueprint_instance_trace(info.instance, &trace_count);
    ImGui_Separator();
    snprintf(line, sizeof line, "Execution trace: %d", trace_count);
    ImGui_TextUnformatted(line);
    for (int i = trace_count - 1; i >= 0 && i >= trace_count - 12; i--) {
        const blueprint_instance_trace_entry *entry = &trace[i];
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
    ImGui_End();
}

static void draw_content_drawer(app_scene_authoring *scene)
{
    if (!editor_ui_content_drawer_open()) {
        return;
    }
    ImGui_SetNextWindowSize((ImVec2){ SCENE_CONTEXT_WIDTH, 520.0f }, ImGuiCond_FirstUseEver);
    static bool first_frame = true;
    if (first_frame || editor_ui_take_focus(EDITOR_UI_FOCUS_CONTENT_DRAWER)) {
        ImGui_SetNextWindowFocus();
        ImGui_SetWindowFocusStr("Content Drawer");
        first_frame = false;
    }
    bool open = editor_ui_content_drawer_open();
    if (!ImGui_Begin("Content Drawer", &open, 0)) {
        ImGui_End();
        editor_ui_set_content_drawer_open(open);
        return;
    }
    ImGui_PushFontFloat(NULL, CONTENT_DRAWER_FONT_SIZE);
    draw_content_toolbar(scene);
    ImVec2 available = ImGui_GetContentRegionAvail();
    float source_width = available.x < 720.0f ? CONTENT_DRAWER_SOURCE_WIDTH_NARROW
                                              : CONTENT_DRAWER_SOURCE_WIDTH;
    if (ImGui_BeginChild("content_sources", (ImVec2){ source_width, 0.0f }, 0, 0)) {
        draw_content_sources(scene);
    }
    ImGui_EndChild();
    ImGui_SameLine();
    if (ImGui_BeginChild("content_browser", (ImVec2){ 0.0f, 0.0f }, 0, 0)) {
        editor_ui_search_field(
            "content_search",
            scene->asset_search,
            sizeof scene->asset_search,
            "Search..."
        );
        ImGui_Separator();
        draw_content_items(scene);
    }
    ImGui_EndChild();
    ImGui_PopFont();
    ImGui_End();
    editor_ui_set_content_drawer_open(open);
}

void app_scene_authoring_draw(app_scene_authoring *scene)
{
    if (!scene) {
        return;
    }
    draw_scene_hierarchy(scene);
    draw_runtime_inspection(scene);
    draw_content_drawer(scene);
    draw_prototype_details(scene);
    sync_editor_inspection(scene);
}

void app_scene_authoring_render(void *user)
{
    app_scene_authoring *scene = (app_scene_authoring *)user;
    if (!scene) {
        return;
    }
    for (int i = 0; i < acount(scene->actors); i++) {
        scene_actor *actor = &scene->actors[i];
        const GameTransform
            *transform = GAME_GameTransform_get_const(scene->world, actor->entity);
        const GameHealth *health = GAME_GameHealth_get_const(scene->world, actor->entity);
        if (!transform || !health) {
            continue;
        }
        float x = transform->x;
        float y = -transform->y;
        cf_draw_push_color(app_cf_make_color_rgb(115, 145, 190));
        cf_draw_quad_fill(app_cf_make_aabb_pos_w_h(app_cf_make_v2(x, y), 32.0f, 32.0f), 0.0f);
        cf_draw_pop_color();
        float maximum = health->maximum > 0 ? (float)health->maximum : 1.0f;
        float ratio = (float)health->current / maximum;
        if (ratio < 0.0f) {
            ratio = 0.0f;
        }
        if (ratio > 1.0f) {
            ratio = 1.0f;
        }
        cf_draw_push_color(app_cf_make_color_rgb(40, 40, 40));
        cf_draw_quad_fill(app_cf_make_aabb_pos_w_h(app_cf_make_v2(x, y + 24.0f), 36.0f, 5.0f), 0.0f);
        cf_draw_pop_color();
        cf_draw_push_color(app_cf_make_color_rgb(80, 210, 90));
        cf_draw_quad_fill(
            app_cf_make_aabb_pos_w_h(app_cf_make_v2(x - 18.0f + 18.0f * ratio, y + 24.0f), 36.0f * ratio, 3.0f),
            0.0f
        );
        cf_draw_pop_color();
    }
}

int app_scene_authoring_frame(app_scene_authoring *scene, double monotonic_time)
{
    if (!scene) {
        return FLOW_ERR_ARGUMENT;
    }
    for (int i = 0; i < acount(scene->actors); i++) {
        flow_error error = { 0 };
        (void)game_behavior_dispatch(
            scene->live->behaviors,
            scene->actors[i].entity,
            "Tick",
            (flow_inputs){ 0 },
            &error
        );
    }
    return engine_frame(scene->runtime, monotonic_time);
}

int app_scene_authoring_input(app_scene_authoring *scene, const char *action, double value, int pressed)
{
    return scene ? engine_dispatch_input(scene->runtime, action, value, pressed, NULL)
                 : FLOW_ERR_ARGUMENT;
}
