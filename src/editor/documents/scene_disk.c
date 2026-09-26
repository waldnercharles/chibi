/* Content another program changed on disk reloads in place. Graph assets and
 * systems swap into the running world; prototypes refresh their instances and
 * the open scene reloads, keeping the view and selection, once the runtime
 * stops. A document with unsaved edits keeps them until reloaded by path. */
#include "editor/workspace/scene_internal.h"
#include "graph/document/serialize.h"
#include "support/array.h"
#include "support/asset_text.h"
#include "support/xalloc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static const char *const GRAPH_FOLDERS[] = {
    "behaviors", "events",           "fields", "libraries",
    "materials", "shader-libraries", "shapes",
};

static bool in_graph_folder(const app_scene_authoring *scene, const char *path)
{
    for (size_t i = 0; i < sizeof GRAPH_FOLDERS / sizeof *GRAPH_FOLDERS; i++) {
        if (content_path_in_folder(scene, path, GRAPH_FOLDERS[i])) {
            return true;
        }
    }
    return false;
}

/* The runtime's world holds prototypes and the scene while it runs. */
static bool waits_for_runtime(const app_scene_authoring *scene, const char *path)
{
    return runtime_session(scene) &&
           (content_path_in_folder(scene, path, "prototypes") ||
            content_path_in_folder(scene, path, "scenes"));
}

static scene_document_tab *tab_at(
    app_scene_authoring *scene,
    app_editor_document_kind kind,
    const char *path
)
{
    for (int i = 0; i < acount(scene->tabs); i++) {
        scene_document_tab *tab = &scene->tabs[i];
        if (tab->kind == kind && tab->path && !strcmp(tab->path, path)) {
            return tab;
        }
    }
    return NULL;
}

static app_disk_result add_definition(app_scene_authoring *scene, const char *path)
{
    char *json = read_scene_file(path);
    behavior_asset *asset = json ? behavior_asset_load_json(json) : NULL;
    free(json);
    game_live_definition *definition = asset
                                         ? game_live_definition_new(asset->name, asset)
                                         : NULL;
    if (!definition) {
        behavior_asset_free(asset);
        snprintf(
            scene->message,
            sizeof scene->message,
            "Could not load %s: %s",
            content_basename(path),
            graph_last_error()
        );
        return APP_DISK_FAILED;
    }
    game_live_definition_set_path(definition, path);
    if (game_live_add_definition(scene->live, definition) != 0) {
        snprintf(
            scene->message,
            sizeof scene->message,
            "Could not add %s.",
            definition->name
        );
        game_live_definition_free(definition);
        return APP_DISK_FAILED;
    }
    if (scene->prototype_preview) {
        game_prototype_preview_register_behavior(
            scene->prototype_preview,
            definition->name,
            definition->working
        );
    }
    snprintf(scene->message, sizeof scene->message, "Added %s.", definition->name);
    return APP_DISK_RELOADED;
}

static app_disk_result sync_graph(app_scene_authoring *scene, const char *path, bool force)
{
    for (int i = 0; i < game_live_definition_count(scene->live); i++) {
        game_live_definition *definition = game_live_definition_at(scene->live, i);
        const char *at = game_live_definition_path(definition);
        if (!at || strcmp(at, path)) {
            continue;
        }
        if (definition->unsaved && !force) {
            return APP_DISK_UNSAVED;
        }
        return reload_behavior_from_disk(scene, definition->name) == 0
                 ? APP_DISK_RELOADED
                 : APP_DISK_FAILED;
    }
    return add_definition(scene, path);
}

/* The runtime takes the new version at once; an open document follows it. */
static app_disk_result sync_system(app_scene_authoring *scene, const char *path, bool force)
{
    scene_document_tab *tab = tab_at(scene, APP_EDITOR_DOCUMENT_SYSTEM, path);
    if (tab && scene_tabs_dirty(scene, tab) && !force) {
        return APP_DISK_UNSAVED;
    }
    char *json = read_scene_file(path);
    behavior_asset *asset = json ? behavior_asset_load_json(json) : NULL;
    char error[256] = "not a system";
    bool registered = asset &&
                      game_live_register_system(scene->live, asset->name, json, error, sizeof error) ==
                          0 &&
                      (!scene->prototype_preview || game_prototype_preview_register_system(
                                                        scene->prototype_preview,
                                                        asset->name,
                                                        json,
                                                        error,
                                                        sizeof error
                                                    ) == 0);
    behavior_asset_free(asset);
    if (!registered) {
        free(json);
        snprintf(scene->message, sizeof scene->message, "Could not reload %s: %s", content_basename(path), error);
        return APP_DISK_FAILED;
    }
    if (scene->system_path && !strcmp(scene->system_path, path)) {
        free(json);
        return load_system_asset(scene, path, true) == 0 ? APP_DISK_RELOADED
                                                         : APP_DISK_FAILED;
    }
    if (tab) {
        free(tab->system_saved);
        tab->system_saved = xstrdup(json);
        free(tab->system_json);
        tab->system_json = json;
        json = NULL;
    }
    free(json);
    snprintf(scene->message, sizeof scene->message, "Reloaded %s.", content_basename(path));
    return APP_DISK_RELOADED;
}

static app_disk_result sync_prototype(app_scene_authoring *scene, const char *path, bool force)
{
    for (int i = 0; i < acount(scene->prototypes); i++) {
        scene_prototype_asset *asset = &scene->prototypes[i];
        if (!asset->path || strcmp(asset->path, path)) {
            continue;
        }
        if (asset->dirty && !force) {
            return APP_DISK_UNSAVED;
        }
        return reload_prototype_asset(scene, asset) == 0 ? APP_DISK_RELOADED
                                                         : APP_DISK_FAILED;
    }
    char *json = read_scene_file(path);
    bool added = json &&
                 app_scene_authoring_register_prototype(scene, json, path) == 0;
    free(json);
    snprintf(
        scene->message,
        sizeof scene->message,
        added ? "Added %s." : "Could not load %s.",
        content_basename(path)
    );
    return added ? APP_DISK_RELOADED : APP_DISK_FAILED;
}

/* Reloading the open scene keeps the camera and the selection by name. */
static int reload_scene(app_scene_authoring *scene)
{
    scene_camera camera = scene->camera;
    int target = scene->camera_target;
    scene_actor *actor = selected_actor(scene);
    char *selected = actor ? xstrdup(actor->name) : NULL;
    char *path = xstrdup(scene->scene_path);
    int result = app_scene_authoring_load(scene, path);
    free(path);
    if (result == 0) {
        scene->camera = camera;
        scene->camera_target = target;
        scene->selected = -1;
        for (int i = 0; selected && i < acount(scene->actors); i++) {
            if (!strcmp(scene->actors[i].name, selected)) {
                select_instance(scene, i);
            }
        }
    }
    free(selected);
    return result;
}

/* The open scene reloads; another open scene takes the file's text for when
 * its tab returns. */
static app_disk_result sync_scene(app_scene_authoring *scene, const char *path, bool force)
{
    if (scene->scene_path && !strcmp(scene->scene_path, path)) {
        if (scene->scene_dirty && !force) {
            return APP_DISK_UNSAVED;
        }
        return reload_scene(scene) == 0 ? APP_DISK_RELOADED : APP_DISK_FAILED;
    }
    scene_document_tab *tab = tab_at(scene, APP_EDITOR_DOCUMENT_SCENE, path);
    if (!tab) {
        return APP_DISK_NONE;
    }
    if (tab->scene_dirty && !force) {
        return APP_DISK_UNSAVED;
    }
    char *json = read_scene_file(path);
    if (!json) {
        snprintf(scene->message, sizeof scene->message, "Could not read %s.", content_basename(path));
        return APP_DISK_FAILED;
    }
    free(tab->scene_json);
    tab->scene_json = json;
    free(tab->scene_history);
    tab->scene_history = NULL;
    snprintf(scene->message, sizeof scene->message, "Reloaded %s.", content_basename(path));
    return APP_DISK_RELOADED;
}

/* A removed file leaves what the editor loaded from it. */
static app_disk_result sync_path(app_scene_authoring *scene, const char *path, bool force)
{
    struct stat info;
    if (stat(path, &info) != 0) {
        return APP_DISK_NONE;
    }
    if (in_graph_folder(scene, path)) {
        return sync_graph(scene, path, force);
    }
    if (content_path_in_folder(scene, path, "systems")) {
        return sync_system(scene, path, force);
    }
    if (content_path_in_folder(scene, path, "prototypes")) {
        return sync_prototype(scene, path, force);
    }
    if (content_path_in_folder(scene, path, "scenes")) {
        return sync_scene(scene, path, force);
    }
    return APP_DISK_NONE;
}

static void push_unique(char ***paths, const char *path)
{
    for (int i = 0; i < acount(*paths); i++) {
        if (!strcmp((*paths)[i], path)) {
            return;
        }
    }
    apush(*paths, xstrdup(path));
}

app_disk_change *app_scene_authoring_sync_disk(app_scene_authoring *scene, double now)
{
    if (!scene->content_root) {
        return NULL;
    }
    char **changed = tree_watch_poll(&scene->disk, scene->content_root, ASSET_EXTENSION, now);
    if (!changed && (!acount(scene->disk_pending) || runtime_session(scene))) {
        return NULL;
    }
    if (changed) {
        content_refresh(scene);
    }
    char **paths = scene->disk_pending;
    scene->disk_pending = NULL;
    for (int i = 0; i < acount(changed); i++) {
        push_unique(&paths, changed[i]);
    }
    tree_watch_changes_free(changed);
    app_disk_change *changes = NULL;
    for (int i = 0; i < acount(paths); i++) {
        if (waits_for_runtime(scene, paths[i])) {
            push_unique(&scene->disk_pending, paths[i]);
            continue;
        }
        app_disk_result result = sync_path(scene, paths[i], false);
        if (result == APP_DISK_NONE) {
            continue;
        }
        app_disk_change change = { .result = result, .path = xstrdup(paths[i]) };
        snprintf(change.message, sizeof change.message, "%s", scene->message);
        apush(changes, change);
    }
    tree_watch_changes_free(paths);
    return changes;
}

void app_disk_changes_free(app_disk_change *changes)
{
    for (int i = 0; i < acount(changes); i++) {
        free(changes[i].path);
    }
    afree(changes);
}

app_disk_result app_scene_authoring_reload_path(app_scene_authoring *scene, const char *path)
{
    return sync_path(scene, path, true);
}
