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
    behavior_asset *asset = json ? behavior_asset_load(json) : NULL;
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
    behavior_asset *asset = json ? behavior_asset_load(json) : NULL;
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
        av_doc *native = asset_text_read(json, error, sizeof error);
        assert(native);
        av_value_free(tab->system_saved);
        tab->system_saved = av_value_copy(av_doc_root(native));
        av_value_free(tab->system_value);
        tab->system_value = av_value_copy(av_doc_root(native));
        av_doc_free(native);
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
    char *active = scene->active_tab >= 0 && scene->active_tab < acount(scene->tabs)
                     ? xstrdup(scene->tabs[scene->active_tab].key)
                     : NULL;
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
    for (int i = 0; active && i < acount(scene->tabs); i++) {
        if (!strcmp(scene->tabs[i].key, active)) {
            if (scene_tabs_activate(scene, i) != 0) {
                result = -1;
            }
            break;
        }
    }
    free(active);
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
    char error[256];
    av_doc *native = asset_text_read(json, error, sizeof error);
    free(json);
    if (!native) {
        snprintf(scene->message, sizeof scene->message, "Could not reload %s: %s", content_basename(path), error);
        return APP_DISK_FAILED;
    }
    av_value_free(tab->scene_value);
    tab->scene_value = av_value_copy(av_doc_root(native));
    av_doc_free(native);
    av_value_free(tab->scene_history);
    tab->scene_history = NULL;
    tab->scene_dirty = false;
    afree(tab->scene_entities);
    tab->scene_entity_count = 0;
    afree(tab->scene_actor_ids);
    tab->selected_entity = 0;
    tab->selected = -1;
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

static void add_change(app_disk_change **changes, app_disk_result result, const char *path, const char *message)
{
    app_disk_change change = { .result = result, .path = xstrdup(path) };
    snprintf(change.message, sizeof change.message, "%s", message);
    apush(*changes, change);
}

/* Open UI documents follow their files wherever they are, as a ui asset of
 * another project or a stylesheet: one without edits reloads. */
static app_disk_change *sync_ui_documents(app_scene_authoring *scene, double now)
{
    app_disk_change *changes = NULL;
    for (int i = 0; i < acount(scene->tabs); i++) {
        scene_document_tab *tab = &scene->tabs[i];
        if (tab->kind != APP_EDITOR_DOCUMENT_UI || !ui_buffer_poll_disk(tab->ui, now)) {
            continue;
        }
        app_disk_result result = ui_buffer_dirty(tab->ui) ? APP_DISK_UNSAVED
                               : reload_ui_document(scene, tab) == 0
                                   ? APP_DISK_RELOADED
                                   : APP_DISK_FAILED;
        add_change(&changes, result, tab->path, scene->message);
    }
    return changes;
}

app_disk_change *app_scene_authoring_sync_disk(app_scene_authoring *scene, double now)
{
    app_disk_change *changes = sync_ui_documents(scene, now);
    if (!scene->content_root) {
        return changes;
    }
    char **changed = tree_watch_poll(&scene->disk, scene->content_root, ASSET_EXTENSION, now);
    if (!changed && (!acount(scene->disk_pending) || runtime_session(scene))) {
        return changes;
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
    for (int i = 0; i < acount(paths); i++) {
        if (waits_for_runtime(scene, paths[i])) {
            push_unique(&scene->disk_pending, paths[i]);
            continue;
        }
        app_disk_result result = sync_path(scene, paths[i], false);
        if (result != APP_DISK_NONE) {
            add_change(&changes, result, paths[i], scene->message);
        }
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
    scene_document_tab *ui = tab_at(scene, APP_EDITOR_DOCUMENT_UI, path);
    if (ui) {
        return reload_ui_document(scene, ui) == 0 ? APP_DISK_RELOADED : APP_DISK_FAILED;
    }
    return sync_path(scene, path, true);
}

/* ---------- Refactors ---------- */

bool scene_same_file(const char *a, const char *b)
{
    if (!a || !b) {
        return false;
    }
    char *left = realpath(a, NULL), *right = realpath(b, NULL);
    bool same = left && right ? !strcmp(left, right) : !strcmp(a, b);
    free(left);
    free(right);
    return same;
}

static game_live_definition *definition_file(app_scene_authoring *scene, const char *path)
{
    for (int i = 0; i < game_live_definition_count(scene->live); i++) {
        game_live_definition *definition = game_live_definition_at(scene->live, i);
        if (scene_same_file(game_live_definition_path(definition), path)) {
            return definition;
        }
    }
    return NULL;
}

static scene_prototype_asset *prototype_file(app_scene_authoring *scene, const char *path)
{
    for (int i = 0; i < acount(scene->prototypes); i++) {
        if (scene_same_file(scene->prototypes[i].path, path)) {
            return &scene->prototypes[i];
        }
    }
    return NULL;
}

static void rename_id(char **id, const char *from, const char *to)
{
    if (*id && !strcmp(*id, from)) {
        free(*id);
        *id = xstrdup(to);
    }
}

/* The open document, the graph editor's and the tabs follow a rename. */
static void rename_document(
    app_scene_authoring *scene,
    app_editor_document_kind kind,
    const char *from,
    const char *to,
    const char *path
)
{
    scene_tabs_rename(scene, kind, from, to, path);
    if (scene->document.kind == kind) {
        rename_id(&scene->document_id, from, to);
        scene->document.id = scene->document_id;
    }
    if (scene->graph_document_kind == kind) {
        rename_id(&scene->graph_document_id, from, to);
    }
}

/* A renamed prototype keeps its place, tab and instances. */
static void move_prototype(app_scene_authoring *scene, scene_prototype_asset *asset, const char *to)
{
    char *json = read_scene_file(to);
    char error[256];
    game_prototype *loaded = json
                               ? game_prototype_load(json, &scene->registry, error, sizeof error)
                               : NULL;
    free(json);
    if (!loaded) {
        snprintf(scene->message, sizeof scene->message, "Could not load %s.", content_basename(to));
        return;
    }
    char *from = xstrdup(asset->prototype->name);
    for (int i = 0; i < acount(scene->actors); i++) {
        rename_id(&scene->actors[i].prototype_name, from, loaded->name);
    }
    for (int i = 0; i < acount(scene->tabs); i++) {
        rename_id(&scene->tabs[i].selected_prototype_name, from, loaded->name);
    }
    free(asset->prototype->name);
    asset->prototype->name = xstrdup(loaded->name);
    free(asset->path);
    asset->path = xstrdup(to);
    rename_document(scene, APP_EDITOR_DOCUMENT_PROTOTYPE, from, loaded->name, to);
    game_prototype_free(loaded);
    reload_prototype_asset(scene, asset);
    free(from);
}

/* A renamed definition keeps its place, tab, attachments and history. */
static void move_definition(app_scene_authoring *scene, game_live_definition *definition, const char *to)
{
    char *json = read_scene_file(to);
    behavior_asset *loaded = json ? behavior_asset_load(json) : NULL;
    free(json);
    char *from = xstrdup(definition->name);
    char *name = loaded ? xstrdup(loaded->name) : NULL;
    behavior_asset_free(loaded);
    if (!name || game_live_rename_definition(scene->live, from, name) != 0) {
        snprintf(scene->message, sizeof scene->message, "Could not load %s.", content_basename(to));
        free(from);
        free(name);
        return;
    }
    game_live_definition_set_path(definition, to);
    editor_rename_behavior_asset(scene->graph_editor, from, name);
    rename_document(scene, APP_EDITOR_DOCUMENT_BEHAVIOR, from, name, to);
    if (scene->prototype_preview) {
        game_prototype_preview_register_behavior(
            scene->prototype_preview,
            name,
            definition->working
        );
    }
    reload_behavior_from_disk(scene, name);
    free(from);
    free(name);
}

/* Another definition for the graph editor while one goes. */
static game_live_definition *other_definition(app_scene_authoring *scene, const game_live_definition *leaving)
{
    for (int i = 0; i < game_live_definition_count(scene->live); i++) {
        game_live_definition *definition = game_live_definition_at(scene->live, i);
        if (definition != leaving) {
            return definition;
        }
    }
    return NULL;
}

/* A definition whose file went: its tab closes, the graph editor turns to
   another, and it leaves the live world unless the world still runs it. */
static void forget_definition(app_scene_authoring *scene, game_live_definition *leaving)
{
    char *name = xstrdup(leaving->name);
    scene_tabs_close(scene, APP_EDITOR_DOCUMENT_BEHAVIOR, name);
    game_live_definition *shown = other_definition(scene, leaving);
    if (scene->definition == leaving && shown) {
        open_behavior_graph(scene, shown->name);
    }
    editor_forget_behavior_asset(scene->graph_editor, name);
    if (scene->definition == leaving) {
        game_live_definition_set_path(leaving, NULL);
        free(name);
        return;
    }
    if (scene->editor_definition_seen == leaving) {
        scene->editor_definition_seen = scene->definition;
    }
    /* Attached instances keep it, with its path, until they go. */
    game_live_remove_definition(scene->live, name);
    free(name);
}

static void forget_index(int *index, int removed)
{
    *index = *index == removed ? -1 : *index - (*index > removed);
}

/* A deleted prototype goes, unless the open scene still places it; then it
   stays, unsaved. */
static void forget_prototype(app_scene_authoring *scene, scene_prototype_asset *asset)
{
    const char *name = asset->prototype->name;
    for (int i = 0; i < acount(scene->actors); i++) {
        if (!strcmp(scene->actors[i].prototype_name, name)) {
            free(asset->path);
            asset->path = NULL;
            asset->dirty = 1;
            return;
        }
    }
    if (previewed_prototype(scene) == asset) {
        app_scene_authoring_stop_prototype_preview(scene);
    }
    scene_tabs_close(scene, APP_EDITOR_DOCUMENT_PROTOTYPE, name);
    int removed = (int)(asset - scene->prototypes);
    forget_index(&scene->selected_prototype, removed);
    forget_index(&scene->runtime_preview_prototype, removed);
    for (int i = 0; i < acount(scene->tabs); i++) {
        forget_index(&scene->tabs[i].selected_prototype, removed);
    }
    game_prototype_free(asset->prototype);
    free(asset->path);
    clear_prototype_history(asset);
    int count = acount(scene->prototypes);
    memmove(asset, asset + 1, (size_t)(count - removed - 1) * sizeof *asset);
    asetlen(scene->prototypes, count - 1);
}

static bool named_file(const char *path, const char *name)
{
    const char *base = content_basename(path);
    size_t length = strlen(name);
    return !strncmp(base, name, length) && !strcmp(base + length, ASSET_EXTENSION);
}

/* A file that comes back, as Undo brings back a deleted one, takes up what
   Studio kept of it without a path: a prototype the scene still places, or
   the definition the graph editor still shows. */
static bool adopt_file(app_scene_authoring *scene, const char *path)
{
    bool prototypes = content_path_in_folder(scene, path, "prototypes");
    for (int i = 0; prototypes && i < acount(scene->prototypes); i++) {
        scene_prototype_asset *asset = &scene->prototypes[i];
        if (!asset->path && named_file(path, asset->prototype->name)) {
            asset->path = xstrdup(path);
            asset->dirty = 0;
            reload_prototype_asset(scene, asset);
            return true;
        }
    }
    bool graphs = in_graph_folder(scene, path);
    for (int i = 0; graphs && i < game_live_definition_count(scene->live); i++) {
        game_live_definition *definition = game_live_definition_at(scene->live, i);
        if (!game_live_definition_path(definition) &&
            named_file(path, definition->name)) {
            game_live_definition_set_path(definition, path);
            reload_behavior_from_disk(scene, definition->name);
            return true;
        }
    }
    return false;
}

static void forget_file(app_scene_authoring *scene, const char *from)
{
    game_live_definition *definition = definition_file(scene, from);
    scene_prototype_asset *prototype = prototype_file(scene, from);
    if (definition) {
        forget_definition(scene, definition);
    }
    if (prototype) {
        forget_prototype(scene, prototype);
    }
}

void scene_disk_follow(app_scene_authoring *scene, const scene_move *moves, int count)
{
    for (int i = 0; i < count; i++) {
        if (moves[i].from) {
            tree_watch_note(&scene->disk, moves[i].from);
        }
        if (moves[i].to) {
            tree_watch_note(&scene->disk, moves[i].to);
        }
    }
    content_refresh(scene);
    /* New and renamed files first, so the files that name them find them. */
    for (int i = 0; i < count; i++) {
        const scene_move *move = &moves[i];
        bool moved = move->to && (!move->from || strcmp(move->from, move->to));
        scene_prototype_asset *prototype = moved ? prototype_file(scene, move->from)
                                                 : NULL;
        game_live_definition *definition = moved ? definition_file(scene, move->from)
                                                 : NULL;
        if (prototype) {
            move_prototype(scene, prototype, move->to);
        } else if (definition) {
            move_definition(scene, definition, move->to);
        } else if (moved && (move->from || !adopt_file(scene, move->to))) {
            sync_path(scene, move->to, true);
        }
    }
    for (int i = 0; i < count; i++) {
        if (moves[i].from && moves[i].to && !strcmp(moves[i].from, moves[i].to)) {
            sync_path(scene, moves[i].to, true);
        }
    }
    for (int i = 0; i < count; i++) {
        if (moves[i].from && !moves[i].to) {
            forget_file(scene, moves[i].from);
        }
    }
    refresh_asset_catalog(scene);
    asset_index_invalidate(scene);
}
