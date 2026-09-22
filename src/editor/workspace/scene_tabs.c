#include "assets/behavior_loader.h"
#include "assets/prototype.h"
#include "editor/panels/command_palette.h"
#include "editor/ui/ui_shell.h"
#include "editor/workspace/scene_internal.h"
#include "graph/document/serialize.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <imgui/dcimgui.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static scene_document_tab *active_tab(app_scene_authoring *scene)
{
    return scene->active_tab >= 0 && scene->active_tab < acount(scene->tabs)
             ? &scene->tabs[scene->active_tab]
             : NULL;
}

static void free_tab(scene_document_tab *tab)
{
    free(tab->key);
    free(tab->label);
    free(tab->path);
    free(tab->scene_json);
    free(tab->scene_history);
    afree(tab->scene_entities);
    free(tab->selected_prototype_name);
    free(tab->asset_saved);
    free(tab->system_json);
    free(tab->system_saved);
    *tab = (scene_document_tab){ 0 };
}

static const char *tab_kind_name(app_editor_document_kind kind)
{
    switch (kind) {
        case APP_EDITOR_DOCUMENT_SCENE: return "scene";
        case APP_EDITOR_DOCUMENT_PROTOTYPE: return "prototype";
        case APP_EDITOR_DOCUMENT_BEHAVIOR: return "behavior";
        case APP_EDITOR_DOCUMENT_SYSTEM: return "system";
    }
    return "document";
}

static char *tab_key(app_editor_document_kind kind, const char *id, const char *path)
{
    const char *value = path && path[0] ? path : id;
    if (!value || !value[0]) {
        return NULL;
    }
    size_t size = strlen(value) + strlen(tab_kind_name(kind)) + 2;
    char *key = xmalloc(size);
    snprintf(key, size, "%s:%s", tab_kind_name(kind), value);
    return key;
}

static char *tab_label(app_editor_document_kind kind, const char *id, const char *path)
{
    const char *value = path && path[0] ? content_basename(path) : id;
    if (!value || !value[0]) {
        value = kind == APP_EDITOR_DOCUMENT_SCENE ? "Scene" : "Document";
    }
    char *label = xstrdup(value);
    size_t length = strlen(label);
    if (length > 5 && !strcmp(label + length - 5, ".json")) {
        label[length - 5] = '\0';
    }
    return label;
}

static int tab_find(const app_scene_authoring *scene, const char *key)
{
    for (int i = 0; i < acount(scene->tabs); i++) {
        if (scene->tabs[i].key && strcmp(scene->tabs[i].key, key) == 0) {
            return i;
        }
    }
    return -1;
}

static int scene_tab_for_path(const app_scene_authoring *scene, const char *path)
{
    for (int i = 0; i < acount(scene->tabs); i++) {
        if (scene->tabs[i].kind == APP_EDITOR_DOCUMENT_SCENE &&
            scene->tabs[i].path && strcmp(scene->tabs[i].path, path) == 0) {
            return i;
        }
    }
    return -1;
}

static int document_tab_for_path(
    const app_scene_authoring *scene,
    app_editor_document_kind kind,
    const char *path
)
{
    if (!path) {
        return -1;
    }
    for (int i = 0; i < acount(scene->tabs); i++) {
        if (scene->tabs[i].kind == kind && scene->tabs[i].path &&
            strcmp(scene->tabs[i].path, path) == 0) {
            return i;
        }
    }
    return -1;
}

static int scene_tab_for_current_document(const app_scene_authoring *scene)
{
    if (scene->current_scene_tab_key) {
        int index = tab_find(scene, scene->current_scene_tab_key);
        if (index >= 0 && scene->tabs[index].kind == APP_EDITOR_DOCUMENT_SCENE) {
            return index;
        }
    }
    if (scene->scene_path) {
        return scene_tab_for_path(scene, scene->scene_path);
    }
    for (int i = 0; i < acount(scene->tabs); i++) {
        if (scene->tabs[i].kind == APP_EDITOR_DOCUMENT_SCENE && !scene->tabs[i].path) {
            return i;
        }
    }
    return -1;
}

const char *scene_tabs_current_scene_key(const app_scene_authoring *scene)
{
    if (scene->current_scene_tab_key) {
        int index = tab_find(scene, scene->current_scene_tab_key);
        if (index >= 0 && scene->tabs[index].kind == APP_EDITOR_DOCUMENT_SCENE) {
            return scene->tabs[index].key;
        }
    }
    int index = scene_tab_for_current_document(scene);
    return index >= 0 ? scene->tabs[index].key : NULL;
}

static void set_current_scene_tab(app_scene_authoring *scene, const scene_document_tab *tab)
{
    assert(tab->kind == APP_EDITOR_DOCUMENT_SCENE && tab->key);
    free(scene->current_scene_tab_key);
    scene->current_scene_tab_key = xstrdup(tab->key);
}

static void tab_set_path(scene_document_tab *tab, const char *path)
{
    free(tab->path);
    tab->path = path ? xstrdup(path) : NULL;
}

static void capture_scene_tab(app_scene_authoring *scene, scene_document_tab *tab)
{
    assert(tab->kind == APP_EDITOR_DOCUMENT_SCENE);
    if (scene_authoring_sync_document(scene) != 0) {
        return;
    }
    char *json = scene_authoring_scene_json(scene);
    char *history = scene_history_checkpoint(scene->history);
    if (!json || !history) {
        free(json);
        free(history);
        return;
    }
    free(tab->scene_json);
    tab->scene_json = json;
    free(tab->scene_history);
    tab->scene_history = history;
    afree(tab->scene_entities);
    for (int i = 0; i < acount(scene->actors); i++) {
        apush(tab->scene_entities, ecs_entity_to_u64(scene->actors[i].entity));
    }
    tab->scene_entity_count = acount(tab->scene_entities);
    tab->selected_entity = scene->selected >= 0 &&
                                   scene->selected < acount(scene->actors)
                             ? ecs_entity_to_u64(scene->actors[scene->selected].entity)
                             : 0;
    tab->camera = scene->camera.view;
    tab->camera_saved = scene->camera.saved;
    tab->scene_dirty = scene->scene_dirty;
    tab->selected = scene->selected;
    tab->selected_prototype = scene->selected_prototype;
    tab->selected_attachment = scene->selected_attachment;
    free(tab->selected_prototype_name);
    tab->selected_prototype_name = selected_prototype(scene) &&
                                           selected_prototype(scene)->prototype
                                     ? xstrdup(
                                           selected_prototype(scene)->prototype->name
                                       )
                                     : NULL;
    tab_set_path(tab, scene->scene_path);
}

static void capture_system_tab(app_scene_authoring *scene, scene_document_tab *tab)
{
    assert(tab->kind == APP_EDITOR_DOCUMENT_SYSTEM);
    if (scene_authoring_sync_document(scene) != 0 || !scene->system_document) {
        return;
    }
    char *json = game_system_document_save(
        scene->system_document,
        resolve_system_component,
        system_component_name,
        NULL
    );
    if (!json) {
        return;
    }
    free(tab->system_json);
    tab->system_json = json;
    tab->system_editor_revision = editor_edit_revision(scene->graph_editor);
    tab->system_query_revision = game_system_document_revision(scene->system_document);
    tab_set_path(tab, scene->system_path);
}

static void restore_scene_tab_view(app_scene_authoring *scene, const scene_document_tab *tab)
{
    scene->scene_dirty = tab->scene_dirty;
    scene->camera.view = tab->camera;
    scene->camera.saved = tab->camera_saved;
    scene->selected = -1;
    for (int i = 0; i < acount(scene->actors); i++) {
        uint64_t entity = i < tab->scene_entity_count ? tab->scene_entities[i] : 0;
        scene->actors[i].marked = tab->selected_entity && entity == tab->selected_entity;
        if (scene->actors[i].marked) {
            scene->selected = i;
        }
    }
    if (scene->selected < 0 && tab->selected >= 0 &&
        tab->selected < acount(scene->actors)) {
        scene->selected = tab->selected;
    }
    scene->selected_prototype = tab->selected_prototype_name
                                  ? prototype_index(scene, tab->selected_prototype_name)
                                  : tab->selected_prototype;
    if (scene->selected_prototype < 0 ||
        scene->selected_prototype >= acount(scene->prototypes)) {
        scene->selected_prototype = -1;
    }
    scene->selected_attachment = tab->selected_attachment;
    if (scene->selected >= 0) {
        int count = game_behavior_attachment_count_for_entity(
            scene->live->behaviors,
            scene->actors[scene->selected].entity
        );
        if (scene->selected_attachment < 0 || scene->selected_attachment >= count) {
            scene->selected_attachment = count ? 0 : -1;
        }
    } else {
        scene->selected_attachment = -1;
    }
}

static void capture_asset_baseline(app_scene_authoring *scene, scene_document_tab *tab)
{
    if (tab->kind == APP_EDITOR_DOCUMENT_PROTOTYPE && !tab->asset_saved) {
        scene_prototype_asset *asset = find_prototype(scene, tab->key + strlen("prototype:"));
        if (asset && asset->prototype) {
            char *json = game_prototype_save(asset->prototype);
            if (json) {
                free(tab->asset_saved);
                tab->asset_saved = json;
            }
        }
        return;
    }
    if (tab->kind == APP_EDITOR_DOCUMENT_BEHAVIOR && !tab->asset_saved) {
        game_live_definition *definition = game_live_find_definition(
            scene->live,
            tab->key + strlen("behavior:")
        );
        if (definition && definition->working) {
            char *json = behavior_asset_save_json(definition->working);
            if (json) {
                free(tab->asset_saved);
                tab->asset_saved = json;
            }
        }
    }
}

void scene_tabs_sync_current(app_scene_authoring *scene)
{
    scene_document_tab *tab = active_tab(scene);
    if (!tab) {
        return;
    }
    int current_scene = scene_tab_for_current_document(scene);
    if (current_scene >= 0) {
        capture_scene_tab(scene, &scene->tabs[current_scene]);
    }
    if (tab->kind == APP_EDITOR_DOCUMENT_SYSTEM) {
        capture_system_tab(scene, tab);
    }
}

void scene_tabs_mark_saved(app_scene_authoring *scene)
{
    scene_document_tab *tab = active_tab(scene);
    if (!tab) {
        return;
    }
    if (tab->kind == APP_EDITOR_DOCUMENT_SYSTEM) {
        free(tab->system_saved);
        tab->system_saved = tab->system_json ? xstrdup(tab->system_json) : NULL;
    } else if (tab->kind == APP_EDITOR_DOCUMENT_PROTOTYPE || tab->kind == APP_EDITOR_DOCUMENT_BEHAVIOR) {
        free(tab->asset_saved);
        tab->asset_saved = NULL;
        capture_asset_baseline(scene, tab);
    }
}

void scene_tabs_mark_document_saved(
    app_scene_authoring *scene,
    app_editor_document_kind kind,
    const char *id
)
{
    const char *path = NULL;
    if (kind == APP_EDITOR_DOCUMENT_SYSTEM) {
        path = scene->system_path;
    } else if (kind == APP_EDITOR_DOCUMENT_PROTOTYPE) {
        scene_prototype_asset *asset = find_prototype(scene, id);
        path = asset ? asset->path : NULL;
    } else if (kind == APP_EDITOR_DOCUMENT_BEHAVIOR) {
        game_live_definition *definition = game_live_find_definition(scene->live, id);
        path = definition ? game_live_definition_path(definition) : NULL;
    }
    char *key = tab_key(kind, id, kind == APP_EDITOR_DOCUMENT_SYSTEM ? scene->system_path : NULL);
    int index = document_tab_for_path(scene, kind, path);
    if (index < 0) {
        index = key ? tab_find(scene, key) : -1;
    }
    free(key);
    if (index < 0) {
        return;
    }
    scene_document_tab *tab = &scene->tabs[index];
    tab_set_path(tab, path);
    free(tab->label);
    tab->label = tab_label(kind, id, path);
    free(tab->asset_saved);
    tab->asset_saved = NULL;
    if (kind == APP_EDITOR_DOCUMENT_SYSTEM) {
        free(tab->system_saved);
        tab->system_saved = tab->system_json ? xstrdup(tab->system_json) : NULL;
    } else {
        capture_asset_baseline(scene, tab);
    }
}

static scene_document_tab *add_tab(
    app_scene_authoring *scene,
    app_editor_document_kind kind,
    const char *id,
    const char *path
)
{
    char *key = tab_key(
        kind,
        id,
        kind == APP_EDITOR_DOCUMENT_SCENE || kind == APP_EDITOR_DOCUMENT_SYSTEM ? path : NULL
    );
    if (!key) {
        return NULL;
    }
    int index = document_tab_for_path(scene, kind, path);
    if (index < 0) {
        index = tab_find(scene, key);
    }
    if (index >= 0) {
        free(key);
        return &scene->tabs[index];
    }
    scene_document_tab tab = {
        .kind = kind,
        .key = key,
        .label = tab_label(kind, id, path),
        .path = path ? xstrdup(path) : NULL,
    };
    apush(scene->tabs, tab);
    return &alast(scene->tabs);
}

void scene_tabs_initialize(app_scene_authoring *scene)
{
    editor_ui_set_document_bar_visible(true);
    scene->active_tab = -1;
    scene->next_scene_key = 1;
    scene->close_tab = -1;
    scene_document_tab *tab = add_tab(scene, APP_EDITOR_DOCUMENT_SCENE, "scene-1", NULL);
    assert(tab);
    scene->active_tab = 0;
    set_current_scene_tab(scene, tab);
}

void scene_tabs_free(app_scene_authoring *scene)
{
    for (int i = 0; i < acount(scene->tabs); i++) {
        free_tab(&scene->tabs[i]);
    }
    afree(scene->tabs);
    scene->active_tab = -1;
    free(scene->current_scene_tab_key);
    scene->current_scene_tab_key = NULL;
    editor_ui_set_document_bar_visible(false);
}

void scene_tabs_document_selected(
    app_scene_authoring *scene,
    app_editor_document_kind kind,
    const char *id
)
{
    int previous = scene->active_tab;
    const char *path = kind == APP_EDITOR_DOCUMENT_SCENE  ? scene->scene_path
                     : kind == APP_EDITOR_DOCUMENT_SYSTEM ? scene->system_path
                     : kind == APP_EDITOR_DOCUMENT_PROTOTYPE
                         ? (find_prototype(scene, id)
                                ? find_prototype(scene, id)->path
                                : NULL)
                         : (game_live_find_definition(scene->live, id)
                                ? game_live_definition_path(
                                      game_live_find_definition(scene->live, id)
                                  )
                                : NULL);
    scene_document_tab *current = active_tab(scene);
    if (current && current->kind == kind && !scene->tabs_transitioning) {
        bool same = kind == APP_EDITOR_DOCUMENT_SCENE || kind == APP_EDITOR_DOCUMENT_SYSTEM
                      ? ((!path && !current->path) ||
                         (path && current->path && !strcmp(path, current->path)))
                      : !strcmp(current->key + strlen(tab_kind_name(kind)) + 1, id);
        if (same) {
            return;
        }
    }
    if (previous >= 0 && !scene->tabs_transitioning) {
        scene_tabs_sync_current(scene);
    }
    if (kind == APP_EDITOR_DOCUMENT_SCENE && !path && scene->active_tab >= 0 &&
        scene->active_tab < acount(scene->tabs) &&
        scene->tabs[scene->active_tab].kind == APP_EDITOR_DOCUMENT_SCENE &&
        !scene->tabs[scene->active_tab].path) {
        return;
    }
    char generated[64];
    if (kind == APP_EDITOR_DOCUMENT_SCENE && !path) {
        snprintf(generated, sizeof generated, "scene-%d", scene->next_scene_key);
        id = generated;
    }
    char *key = tab_key(
        kind,
        id,
        kind == APP_EDITOR_DOCUMENT_SCENE || kind == APP_EDITOR_DOCUMENT_SYSTEM ? path : NULL
    );
    if (!key) {
        return;
    }
    int index = document_tab_for_path(scene, kind, path);
    if (index < 0) {
        index = tab_find(scene, key);
    }
    free(key);
    if (index < 0 && kind == APP_EDITOR_DOCUMENT_SCENE && acount(scene->tabs) == 1 &&
        scene->tabs[0].kind == APP_EDITOR_DOCUMENT_SCENE && !scene->tabs[0].scene_json) {
        index = 0;
        scene_document_tab *tab = &scene->tabs[0];
        free(tab->key);
        tab->key = tab_key(kind, id, path);
        free(tab->label);
        tab->label = tab_label(kind, id, path);
        tab_set_path(tab, path);
        scene->next_scene_key++;
    }
    if (index < 0) {
        scene_document_tab *tab = add_tab(scene, kind, id, path);
        index = tab ? (int)(tab - scene->tabs) : -1;
        if (index >= 0 && kind == APP_EDITOR_DOCUMENT_SCENE && !path) {
            scene->next_scene_key++;
        }
    }
    if (index >= 0) {
        scene->active_tab = index;
        if (kind == APP_EDITOR_DOCUMENT_SCENE) {
            set_current_scene_tab(scene, &scene->tabs[index]);
        }
        if (index != previous && kind == APP_EDITOR_DOCUMENT_SCENE &&
            scene->tabs[index].scene_json) {
            restore_scene_tab_view(scene, &scene->tabs[index]);
        }
        if (kind != APP_EDITOR_DOCUMENT_SCENE || path || scene->tabs[index].scene_json) {
            capture_asset_baseline(scene, &scene->tabs[index]);
        }
    }
}

static int restore_scene_tab_contents(app_scene_authoring *scene, scene_document_tab *tab)
{
    assert(tab->kind == APP_EDITOR_DOCUMENT_SCENE && tab->scene_json);
    uint64_t *old = tab->scene_entities;
    int count = tab->scene_entity_count;
    if (scene_authoring_load_json(scene, tab->scene_json, tab->path) != 0) {
        return -1;
    }
    uint64_t *replacement = NULL;
    int replacement_count = acount(scene->actors);
    for (int i = 0; i < replacement_count; i++) {
        apush(replacement, ecs_entity_to_u64(scene->actors[i].entity));
    }
    int mapped = count < replacement_count ? count : replacement_count;
    if (tab->scene_history &&
        !scene_history_restore_checkpoint(scene->history, tab->scene_history, old, replacement, mapped)) {
        afree(replacement);
        return -1;
    }
    scene_navigation_remap_entities(scene->navigation, tab->key, old, replacement, mapped);
    if (!tab->scene_history) {
        scene_history_clear(scene->history);
    }
    afree(replacement);
    restore_scene_tab_view(scene, tab);
    return 0;
}

int scene_tabs_activate(app_scene_authoring *scene, int index)
{
    if (index < 0 || index >= acount(scene->tabs) || index == scene->active_tab) {
        return index == scene->active_tab ? 0 : -1;
    }
    int previous = scene->active_tab;
    int previous_scene = scene_tab_for_current_document(scene);
    scene_document_tab rollback = { .kind = APP_EDITOR_DOCUMENT_SCENE };
    scene_tabs_sync_current(scene);
    bool rollback_ready = previous_scene >= 0;
    if (rollback_ready) {
        capture_scene_tab(scene, &rollback);
        rollback_ready = rollback.scene_json != NULL;
    }
    scene->tabs_transitioning = true;
    scene->active_tab = index;
    scene_document_tab *target = &scene->tabs[index];
    int result = 0;
    if (target->kind == APP_EDITOR_DOCUMENT_SCENE) {
        /* Returning from an asset tab keeps the current scene session. */
        if (index != previous_scene) {
            if (target->scene_json) {
                result = restore_scene_tab_contents(scene, target);
            } else {
                capture_scene_tab(scene, target);
            }
        }
        if (result == 0) {
            scene_authoring_select_document(scene, APP_EDITOR_DOCUMENT_SCENE, APP_SCENE_DOCUMENT_ID);
        }
    } else if (target->kind == APP_EDITOR_DOCUMENT_PROTOTYPE) {
        result = app_scene_authoring_open_prototype(scene, target->key + strlen("prototype:"));
    } else if (target->kind == APP_EDITOR_DOCUMENT_BEHAVIOR) {
        result = open_behavior_asset(scene, target->key + strlen("behavior:"));
    } else if (target->kind == APP_EDITOR_DOCUMENT_SYSTEM) {
        result = target->system_json ? load_system_document_json(
                                           scene,
                                           target->system_json,
                                           target->path,
                                           false
                                       )
                                     : open_system_asset(scene, target->path);
    }
    if (result == 0) {
        if (target->kind == APP_EDITOR_DOCUMENT_SCENE) {
            set_current_scene_tab(scene, target);
        }
        app_scene_authoring_navigation_sync(scene);
    } else {
        scene->active_tab = previous;
        if (target->kind == APP_EDITOR_DOCUMENT_SCENE && rollback_ready) {
            scene->active_tab = previous_scene;
            int restored = restore_scene_tab_contents(scene, &rollback);
            assert(restored == 0);
            if (restored != 0) {
                scene->active_tab = previous;
                free_tab(&rollback);
                scene->tabs_transitioning = false;
                return result;
            }
            scene->active_tab = previous;
            scene_document_tab *prior = &scene->tabs[previous];
            if (prior->kind == APP_EDITOR_DOCUMENT_PROTOTYPE) {
                app_scene_authoring_open_prototype(scene, prior->key + strlen("prototype:"));
            } else if (prior->kind == APP_EDITOR_DOCUMENT_BEHAVIOR) {
                open_behavior_asset(scene, prior->key + strlen("behavior:"));
            } else if (prior->kind == APP_EDITOR_DOCUMENT_SYSTEM) {
                load_system_document_json(scene, prior->system_json, prior->path, false);
            }
            scene->active_tab = previous;
        }
    }
    scene->tabs_transitioning = false;
    free_tab(&rollback);
    return result;
}

int scene_tabs_open_scene(app_scene_authoring *scene, const char *path)
{
    if (!path) {
        return -1;
    }
    char *json = read_scene_file(path);
    if (!json) {
        return -1;
    }
    char *key = tab_key(APP_EDITOR_DOCUMENT_SCENE, path, path);
    int index = scene_tab_for_path(scene, path);
    if (index < 0) {
        index = key ? tab_find(scene, key) : -1;
    }
    free(key);
    bool created = false;
    if (index < 0) {
        scene_document_tab *tab = add_tab(scene, APP_EDITOR_DOCUMENT_SCENE, path, path);
        if (!tab) {
            free(json);
            return -1;
        }
        tab->scene_json = json;
        tab->scene_dirty = false;
        index = (int)(tab - scene->tabs);
        created = true;
    } else {
        free(json);
    }
    int result = scene_tabs_activate(scene, index);
    if (result != 0 && created) {
        free_tab(&scene->tabs[index]);
        adel(scene->tabs, index);
        if (index < scene->active_tab) {
            scene->active_tab--;
        }
    }
    return result;
}

bool scene_tabs_path_in_use(
    const app_scene_authoring *scene,
    app_editor_document_kind kind,
    const char *path
)
{
    if (!scene || !path) {
        return false;
    }
    int current = kind == APP_EDITOR_DOCUMENT_SCENE
                    ? scene_tab_for_current_document(scene)
                    : scene->active_tab;
    for (int i = 0; i < acount(scene->tabs); i++) {
        const scene_document_tab *tab = &scene->tabs[i];
        if (i != current && tab->kind == kind && tab->path &&
            strcmp(tab->path, path) == 0) {
            return true;
        }
    }
    return false;
}

static bool tab_dirty(app_scene_authoring *scene, const scene_document_tab *tab)
{
    if (tab->kind == APP_EDITOR_DOCUMENT_SCENE) {
        int current = scene_tab_for_current_document(scene);
        return current >= 0 && tab == &scene->tabs[current] ? scene->scene_dirty
                                                            : tab->scene_dirty;
    }
    if (tab->kind == APP_EDITOR_DOCUMENT_PROTOTYPE) {
        const char *name = tab->key + strlen("prototype:");
        scene_prototype_asset *asset = find_prototype(scene, name);
        return asset && asset->dirty;
    }
    if (tab->kind == APP_EDITOR_DOCUMENT_BEHAVIOR) {
        const char *name = tab->key + strlen("behavior:");
        game_live_definition *definition = game_live_find_definition(scene->live, name);
        return definition && definition->unsaved;
    }
    if (tab->kind == APP_EDITOR_DOCUMENT_SYSTEM) {
        if (tab == active_tab(scene) &&
            (editor_edit_revision(scene->graph_editor) != tab->system_editor_revision ||
             game_system_document_revision(scene->system_document) !=
                 tab->system_query_revision)) {
            return true;
        }
        return !tab->system_saved || !tab->system_json ||
               strcmp(tab->system_saved, tab->system_json) != 0;
    }
    return false;
}

static int discard_prototype(app_scene_authoring *scene, scene_document_tab *tab)
{
    if (!tab->asset_saved) {
        return -1;
    }
    const char *name = tab->key + strlen("prototype:");
    scene_prototype_asset *asset = find_prototype(scene, name);
    if (!asset) {
        return -1;
    }
    char error[256];
    game_prototype *prototype = game_prototype_load(tab->asset_saved, &scene->registry, error, sizeof error);
    if (!prototype) {
        return -1;
    }
    game_prototype *previous = asset->prototype;
    asset->prototype = prototype;
    if (refresh_prototype_instances(scene, asset) != 0) {
        asset->prototype = previous;
        game_prototype_free(prototype);
        return -1;
    }
    game_prototype_free(previous);
    clear_prototype_history(asset);
    asset->dirty = 0;
    return 0;
}

static int discard_behavior(app_scene_authoring *scene, scene_document_tab *tab)
{
    if (!tab->asset_saved) {
        return -1;
    }
    const char *name = tab->key + strlen("behavior:");
    game_live_definition *definition = game_live_find_definition(scene->live, name);
    behavior_asset *working = behavior_asset_load_json(tab->asset_saved);
    graph *graph = working ? behavior_asset_find_graph(working, 0) : NULL;
    if (!definition || !working || !graph) {
        behavior_asset_free(working);
        return -1;
    }
    behavior_asset *previous = definition->working;
    int dirty = definition->dirty;
    bool unsaved = definition->unsaved;
    definition->working = working;
    definition->dirty = 1;
    definition->unsaved = false;
    int result = game_live_apply(
        scene->live,
        definition,
        scene->message,
        sizeof scene->message
    );
    if (result != 0) {
        definition->working = previous;
        definition->dirty = dirty;
        definition->unsaved = unsaved;
        behavior_asset_free(working);
        return -1;
    }
    result = editor_reload_behavior_asset(scene->graph_editor, working, graph->graph_id, name);
    assert(result == 0);
    behavior_asset_free(previous);
    scene->definition = definition;
    scene->editor_working = editor_behavior_asset(scene->graph_editor);
    scene->editor_revision_seen = editor_edit_revision(scene->graph_editor);
    scene->editor_behavior_asset_seen = scene->editor_working;
    scene->editor_definition_seen = definition;
    scene->editor_state_seen = true;
    scene->behavior_apply_pending = false;
    return 0;
}

static int discard_system(app_scene_authoring *scene, scene_document_tab *tab)
{
    return tab->system_saved && tab->path
             ? load_system_document_json(scene, tab->system_saved, tab->path, true)
             : -1;
}

static int discard_tab(app_scene_authoring *scene, int index)
{
    scene_document_tab *tab = &scene->tabs[index];
    switch (tab->kind) {
        case APP_EDITOR_DOCUMENT_SCENE: return 0;
        case APP_EDITOR_DOCUMENT_PROTOTYPE:
            return discard_prototype(scene, tab);
        case APP_EDITOR_DOCUMENT_BEHAVIOR: return discard_behavior(scene, tab);
        case APP_EDITOR_DOCUMENT_SYSTEM: return discard_system(scene, tab);
    }
    return -1;
}

static bool can_close_tab(const app_scene_authoring *scene, int index)
{
    if (acount(scene->tabs) <= 1) {
        return false;
    }
    if (scene->tabs[index].kind != APP_EDITOR_DOCUMENT_SCENE) {
        return true;
    }
    for (int i = 0; i < acount(scene->tabs); i++) {
        if (i != index && scene->tabs[i].kind == APP_EDITOR_DOCUMENT_SCENE) {
            return true;
        }
    }
    return false;
}

static int remove_tab(app_scene_authoring *scene, int index)
{
    if (!can_close_tab(scene, index)) {
        return -1;
    }
    const char *scene_key = scene_tabs_current_scene_key(scene);
    bool underlying = scene_key && !strcmp(scene_key, scene->tabs[index].key);
    if (underlying || index == scene->active_tab) {
        int next = -1;
        for (int i = 0; i < acount(scene->tabs); i++) {
            if (i != index &&
                (!underlying || scene->tabs[i].kind == APP_EDITOR_DOCUMENT_SCENE)) {
                next = i;
                break;
            }
        }
        assert(next >= 0);
        if (scene_tabs_activate(scene, next) != 0) {
            return -1;
        }
    }
    free_tab(&scene->tabs[index]);
    int count = acount(scene->tabs);
    memmove(
        &scene->tabs[index],
        &scene->tabs[index + 1],
        (size_t)(count - index - 1) * sizeof *scene->tabs
    );
    asetlen(scene->tabs, count - 1);
    if (index < scene->active_tab) {
        scene->active_tab--;
    }
    return 0;
}

int scene_tabs_discard(app_scene_authoring *scene, int index)
{
    if (!scene || index < 0 || index >= acount(scene->tabs)) {
        return -1;
    }
    scene_document_tab *tab = &scene->tabs[index];
    if (tab->kind != APP_EDITOR_DOCUMENT_SCENE && index != scene->active_tab &&
        scene_tabs_activate(scene, index) != 0) {
        return -1;
    }
    if (discard_tab(scene, index) != 0) {
        return -1;
    }
    return remove_tab(scene, index);
}

static void request_close(app_scene_authoring *scene, int index)
{
    scene->close_tab = index;
    if (tab_dirty(scene, &scene->tabs[index])) {
        return;
    }
    remove_tab(scene, index);
    scene->close_tab = -1;
}

static void draw_close_popup(app_scene_authoring *scene)
{
    if (scene->close_tab < 0 || scene->close_tab >= acount(scene->tabs) ||
        !ImGui_BeginPopupModal("workspace.close", NULL, 0)) {
        return;
    }
    scene_document_tab *tab = &scene->tabs[scene->close_tab];
    ImGui_Text("Save changes to %s?", tab->label);
    bool save = ImGui_Button("Save");
    editor_ui_track_item("workspace.close.save");
    if (save) {
        int index = scene->close_tab;
        if (scene_tabs_activate(scene, index) == 0 &&
            save_active_document(scene) == 0 && remove_tab(scene, index) == 0) {
            scene->close_tab = -1;
            ImGui_CloseCurrentPopup();
        }
    }
    ImGui_SameLine();
    bool discard = ImGui_Button("Discard");
    editor_ui_track_item("workspace.close.discard");
    if (discard) {
        int index = scene->close_tab;
        if (scene_tabs_discard(scene, index) == 0) {
            scene->close_tab = -1;
            ImGui_CloseCurrentPopup();
        }
    }
    ImGui_SameLine();
    bool cancel = ImGui_Button("Cancel");
    editor_ui_track_item("workspace.close.cancel");
    if (cancel) {
        scene->close_tab = -1;
        ImGui_CloseCurrentPopup();
    }
    ImGui_EndPopup();
}

void scene_tabs_draw(app_scene_authoring *scene)
{
    if (!scene || !ImGui_GetCurrentContext()) {
        return;
    }
    bool visible = editor_ui_begin_document_bar();
    int activate = -1;
    int close = -1;
    if (visible) {
        if (ImGui_Button("Quick Open")) {
            asset_browser_request_quick_open(scene);
        }
        editor_ui_track_item("content.quick_open");
        if (ImGui_IsItemHovered(0)) {
            ImGui_SetTooltip("Open an asset (Cmd+P / Ctrl+P)");
        }
        ImGui_SameLine();
        if (ImGui_Button("Commands")) {
            editor_command_palette_open(scene);
        }
        editor_ui_track_control("content.commands", "Commands", "button", NULL);
        if (ImGui_IsItemHovered(0)) {
            ImGui_SetTooltip("Commands (Cmd+K / Ctrl+K)");
        }
        ImGui_SameLine();
        if (editor_ui_icon_button("workspace.documents.list", EDITOR_ICON_CODICON_CHEVRON_DOWN, true)) {
            ImGui_OpenPopup("workspace.document_list", 0);
        }
        editor_ui_track_item("workspace.documents.list");
        if (ImGui_IsItemHovered(0)) {
            ImGui_SetTooltip("Open documents");
        }
        if (ImGui_BeginPopup("workspace.document_list", 0)) {
            for (int i = 0; i < acount(scene->tabs); i++) {
                scene_document_tab *tab = &scene->tabs[i];
                ImGui_PushID(tab->key);
                if (ImGui_MenuItemEx(
                        tab->label,
                        tab_dirty(scene, tab) ? "Modified" : NULL,
                        i == scene->active_tab,
                        true
                    )) {
                    activate = i;
                }
                char item_id[320];
                snprintf(item_id, sizeof item_id, "workspace.document.%s", tab->key);
                editor_ui_track_item(item_id);
                if (ImGui_IsItemHovered(0)) {
                    ImGui_SetTooltip("%s", tab->path ? tab->path : tab->label);
                }
                ImGui_PopID();
            }
            ImGui_EndPopup();
        }
        ImGui_SameLine();
        if (ImGui_BeginTabBar("workspace.documents", ImGuiTabBarFlags_FittingPolicyScroll)) {
            for (int i = 0; i < acount(scene->tabs); i++) {
                scene_document_tab *tab = &scene->tabs[i];
                bool open = true;
                char label[256];
                snprintf(label, sizeof label, "%s###%s", tab->label, tab->key);
                ImGui_PushID(tab->key);
                int flags = i == scene->active_tab ? ImGuiTabItemFlags_SetSelected : 0;
                if (tab_dirty(scene, tab)) {
                    flags |= ImGuiTabItemFlags_UnsavedDocument;
                }
                ImGui_SetNextItemWidth(
                    fminf(220.0f, ImGui_CalcTextSize(tab->label).x + 40.0f)
                );
                bool active = ImGui_BeginTabItem(label, can_close_tab(scene, i) ? &open : NULL, flags);
                char item_id[320];
                snprintf(item_id, sizeof item_id, "workspace.tab.%s", tab->key);
                editor_ui_track_item(item_id);
                if ((ImGui_IsItemClicked() || ImGui_IsItemActivated()) &&
                    i != scene->active_tab) {
                    activate = i;
                }
                if (ImGui_IsItemHovered(0)) {
                    ImGui_SetTooltip("%s", tab->path ? tab->path : tab->label);
                }
                if (ImGui_BeginPopupContextItemEx("tab_actions", 1)) {
                    if (ImGui_MenuItemEx("Close", NULL, false, can_close_tab(scene, i))) {
                        close = i;
                    }
                    editor_ui_track_item("workspace.tab.close");
                    ImGui_EndPopup();
                }
                if (active) {
                    ImGui_EndTabItem();
                }
                ImGui_PopID();
                if (!open) {
                    close = i;
                }
            }
            ImGui_EndTabBar();
        }
        if (close >= 0) {
            request_close(scene, close);
            activate = -1;
        }
        if (scene->close_tab >= 0 && scene->close_tab < acount(scene->tabs) &&
            tab_dirty(scene, &scene->tabs[scene->close_tab])) {
            editor_ui_open_popup("workspace.close");
            draw_close_popup(scene);
        }
    }
    ImGui_End();
    if (activate >= 0) {
        scene->close_tab = -1;
        scene_tabs_activate(scene, activate);
    }
}
