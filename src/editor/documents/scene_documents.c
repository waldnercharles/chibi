#include "editor/workspace/scene_internal.h"
#include "graph/document/serialize.h"
#include "graph/shader/shader_graph.h"
#include "runtime/generated.h"
#include "support/asset_text.h"

#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static const mode_t DOCUMENT_DIRECTORY_MODE = 511;

int open_behavior_asset(app_scene_authoring *scene, const char *asset_name);

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
        editor_visit_behavior_asset(scene->graph_editor, working, default_graph->graph_id, asset_name) !=
            0) {
        return -1;
    }
    scene->definition = target;
    scene->editor_working = editor_behavior_asset(scene->graph_editor);
    return 0;
}

int open_behavior_asset(app_scene_authoring *scene, const char *asset_name)
{
    if (open_behavior_graph(scene, asset_name) != 0) {
        return -1;
    }
    set_behavior_component_context(scene, asset_name);
    scene_authoring_select_document(scene, APP_EDITOR_DOCUMENT_BEHAVIOR, asset_name);
    snprintf(scene->message, sizeof scene->message, "Editing behavior asset %s.", asset_name);
    return 0;
}

int app_scene_authoring_open_behavior(app_scene_authoring *scene, const char *asset_name)
{
    return open_behavior_asset(scene, asset_name);
}

static int content_behavior_index(const app_scene_authoring *scene, const char *path)
{
    const char *name = content_basename(path);
    for (int i = 0; i < game_live_definition_count(scene->live); i++) {
        const char *asset_name = game_live_definition_at(scene->live, i)->name;
        char filename[256];
        snprintf(filename, sizeof filename, "%s" ASSET_EXTENSION, asset_name);
        if (strcmp(name, asset_name) == 0 || strcmp(name, filename) == 0) {
            return i;
        }
    }
    return -1;
}

static const char *behavior_path(const app_scene_authoring *scene, const char *asset_name)
{
    for (int i = 0; i < acount(scene->content); i++) {
        const scene_content_entry *entry = &scene->content[i];
        if (entry->directory || content_behavior_index(scene, entry->path) < 0) {
            continue;
        }
        int index = content_behavior_index(scene, entry->path);
        if (strcmp(game_live_definition_at(scene->live, index)->name, asset_name) == 0) {
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
    if (scene_tabs_path_in_use(scene, APP_EDITOR_DOCUMENT_BEHAVIOR, path)) {
        snprintf(scene->message, sizeof scene->message, "Behavior path is already open.");
        return -1;
    }
    char *json = behavior_asset_save_json(definition->working);
    if (!json) {
        return -1;
    }
    int result = write_text_file_atomic(path, json);
    if (result == 0) {
        game_live_definition_set_path(definition, path);
        definition->unsaved = false;
        scene_tabs_mark_document_saved(
            scene,
            APP_EDITOR_DOCUMENT_BEHAVIOR,
            definition->name
        );
    }
    free(json);
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
        const char *folders[] = { "behaviors", "libraries", "materials",
                                  "shapes",    "events",    "shader-libraries",
                                  "fields" };
        folder = scene->definition ? folders[scene->definition->working->domain]
                                   : "behaviors";
        name = scene->definition ? scene->definition->name : NULL;
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
    size_t filename_size = strlen(name) + strlen(ASSET_EXTENSION) + 1;
    char *filename = (char *)xmalloc(filename_size);
    snprintf(filename, filename_size, "%s" ASSET_EXTENSION, name);
    char *path = content_path_join(directory, filename);
    free(filename);
    free(directory);
    return path;
}

static int reload_behavior_from_disk(app_scene_authoring *scene, const char *asset_name)
{
    game_live_definition *definition = asset_name
                                         ? game_live_find_definition(scene->live, asset_name)
                                         : scene->definition;
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
    const bool active_document = scene->document.kind == APP_EDITOR_DOCUMENT_BEHAVIOR &&
                                 scene->document.id &&
                                 !strcmp(scene->document.id, definition->name);
    const graph *active = active_document ? editor_active_graph(scene->graph_editor)
                                          : NULL;
    uint64_t graph_id = active ? active->graph_id : loaded->graph->graph_id;
    if (!behavior_asset_find_graph(loaded, graph_id)) {
        graph_id = loaded->graph->graph_id;
    }
    int opened = active_document ? editor_reload_behavior_asset(
                                       scene->graph_editor,
                                       loaded,
                                       graph_id,
                                       definition->name
                                   )
                                 : editor_reload_behavior_asset_document(
                                       scene->graph_editor,
                                       loaded,
                                       definition->name
                                   );
    if (opened != 0) {
        behavior_asset_free(loaded);
        snprintf(scene->message, sizeof scene->message, "Could not open reloaded %s.", path);
        return -1;
    }
    if (!game_live_definition_path(definition)) {
        game_live_definition_set_path(definition, path);
    }
    behavior_asset *previous = definition->working;
    definition->working = loaded;
    definition->dirty = 1;
    char apply_message[256];
    int applied = game_live_preview(scene->live, definition, apply_message, sizeof apply_message);
    definition->unsaved = false;
    scene->preview_retry = true;
    behavior_asset_free(previous);
    scene_tabs_mark_document_saved(
        scene,
        APP_EDITOR_DOCUMENT_BEHAVIOR,
        definition->name
    );
    if (active_document) {
        scene->definition = definition;
        scene->editor_working = editor_behavior_asset(scene->graph_editor);
        scene->editor_revision_seen = editor_edit_revision(scene->graph_editor);
        scene->editor_behavior_asset_seen = scene->editor_working;
        scene->editor_definition_seen = definition;
        scene->editor_state_seen = true;
        scene->behavior_apply_pending = false;
    }
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

static int reload_behavior_document(app_scene_authoring *scene)
{
    return reload_behavior_from_disk(scene, NULL);
}

static int load_system_asset(app_scene_authoring *scene, const char *path, bool reload);

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
    clear_prototype_history(asset);
    snprintf(scene->message, sizeof scene->message, "Reloaded %s.", asset->path);
    return 0;
}

static int reload_active_document(app_scene_authoring *scene)
{
    if (scene->document.kind == APP_EDITOR_DOCUMENT_SYSTEM) {
        return scene->system_path ? load_system_asset(scene, scene->system_path, true)
                                  : -1;
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

int load_system_document_json(app_scene_authoring *scene, const char *json, const char *path, bool reload)
{
    char error[256] = { 0 };
    game_system_document *document = game_system_document_load(
        json,
        resolve_system_component,
        NULL,
        error,
        sizeof error
    );
    if (!document) {
        snprintf(scene->message, sizeof scene->message, "Could not open System: %s", error);
        return -1;
    }
    if (!scene->tabs_transitioning &&
        scene->document.kind == APP_EDITOR_DOCUMENT_SYSTEM && scene->system_document &&
        (!scene->system_path || strcmp(scene->system_path, path) != 0)) {
        scene_tabs_sync_current(scene);
    }
    const behavior_asset *source = game_system_document_behavior_asset(document);
    graph *graph = behavior_asset_find_graph((behavior_asset *)source, 0);
    char *document_name = source ? xstrdup(source->name) : NULL;
    int opened = !graph ? -1
               : reload ? editor_reload_behavior_asset(
                              scene->graph_editor,
                              source,
                              graph->graph_id,
                              path
                          )
                        : editor_visit_behavior_asset(
                              scene->graph_editor,
                              source,
                              graph->graph_id,
                              path
                          );
    if (opened != 0) {
        game_system_document_free(document);
        free(document_name);
        return -1;
    }
    bool was_transitioning = scene->tabs_transitioning;
    scene->tabs_transitioning = true;
    char *saved_path = xstrdup(path);
    game_system_document_free(scene->system_document);
    free(scene->system_path);
    scene->system_document = document;
    scene->system_path = saved_path;
    scene->editor_working = editor_behavior_asset(scene->graph_editor);
    scene_authoring_select_document(scene, APP_EDITOR_DOCUMENT_SYSTEM, document_name);
    scene_tabs_sync_current(scene);
    scene->tabs_transitioning = was_transitioning;
    scene_document_tab *tab = scene->active_tab >= 0 ? &scene->tabs[scene->active_tab]
                                                     : NULL;
    if (tab && !tab->system_saved) {
        tab->system_saved = xstrdup(json);
    }
    snprintf(
        scene->message,
        sizeof scene->message,
        "Editing System asset %s.",
        scene->document.id
    );
    free(document_name);
    return 0;
}

static int load_system_asset(app_scene_authoring *scene, const char *path, bool reload)
{
    char *json = read_scene_file(path);
    if (!json) {
        return -1;
    }
    int result = load_system_document_json(scene, json, path, reload);
    free(json);
    return result;
}

int open_system_asset(app_scene_authoring *scene, const char *path)
{
    return load_system_asset(scene, path, false);
}

int app_scene_authoring_open_system(app_scene_authoring *scene, const char *path)
{
    return scene && path ? open_system_asset(scene, path) : -1;
}

static int save_system_document(app_scene_authoring *scene, const char *path)
{
    if (scene_tabs_path_in_use(scene, APP_EDITOR_DOCUMENT_SYSTEM, path)) {
        snprintf(scene->message, sizeof scene->message, "System path is already open.");
        return -1;
    }
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
        scene_tabs_sync_current(scene);
        scene_tabs_mark_document_saved(
            scene,
            APP_EDITOR_DOCUMENT_SYSTEM,
            scene->document.id
        );
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
    if (scene->document.kind == APP_EDITOR_DOCUMENT_PROTOTYPE) {
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
    }
    free(path);
    return result;
}

/* Saves the behavior open in the graph editor, whichever document is active. */
int save_behavior_document(app_scene_authoring *scene)
{
    if (!scene->definition) {
        return -1;
    }
    if (!game_live_definition_path(scene->definition) &&
        !behavior_path(scene, scene->definition->name)) {
        if (scene->document.kind == APP_EDITOR_DOCUMENT_BEHAVIOR) {
            return save_as_active_document(scene);
        }
        snprintf(scene->message, sizeof scene->message, "Behavior requires Save As.");
        return -1;
    }
    int synced = sync_behavior_working(scene);
    int saved = synced == 0 ? save_behavior_definition(scene, scene->definition, NULL)
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

int save_active_document(app_scene_authoring *scene)
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
            return asset ? save_as_active_document(scene) : -1;
        }
        return app_scene_authoring_save_prototype(
            scene,
            asset->prototype->name,
            asset->path
        );
    }
    if (scene->document.kind == APP_EDITOR_DOCUMENT_BEHAVIOR) {
        return save_behavior_document(scene);
    }
    if (!scene->scene_path) {
        snprintf(scene->message, sizeof scene->message, "Scene requires Save As.");
        return -1;
    }
    return app_scene_authoring_save(scene, scene->scene_path);
}

int app_scene_authoring_reload(app_scene_authoring *scene)
{
    return scene ? reload_active_document(scene) : -1;
}
