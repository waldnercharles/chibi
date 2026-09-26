#include "editor/workspace/asset_index.h"

#include "assets/prototype.h"
#include "assets/scene_document.h"
#include "assets/ui_document.h"
#include "editor/workspace/scene_internal.h"
#include "graph/document/serialize.h"
#include "support/array.h"
#include "support/asset_text.h"
#include "support/text_file.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* An unsaved scene or system is keyed by its tab. */
static const char *tab_key(const scene_document_tab *tab)
{
    return tab->path ? NULL : tab->key;
}

static const char *tab_name(const scene_document_tab *tab, const char *fallback)
{
    return tab->label && tab->label[0] ? tab->label : fallback;
}

static bool current_scene_tab(const app_scene_authoring *scene, const scene_document_tab *tab)
{
    return scene->current_scene_tab_key && tab->key &&
           !strcmp(scene->current_scene_tab_key, tab->key);
}

static reference_asset_kind content_asset_kind(const app_scene_authoring *scene, const char *path)
{
    static const struct
    {
        const char *folder;
        reference_asset_kind kind;
    } folders[] = {
        { "scenes", REFERENCE_ASSET_SCENE },
        { "prototypes", REFERENCE_ASSET_PROTOTYPE },
        { "behaviors", REFERENCE_ASSET_BEHAVIOR },
        { "events", REFERENCE_ASSET_EVENT },
        { "materials", REFERENCE_ASSET_MATERIAL },
        { "shapes", REFERENCE_ASSET_SHAPE },
        { "fields", REFERENCE_ASSET_FIELD },
        { "libraries", REFERENCE_ASSET_LIBRARY },
        { "shader-libraries", REFERENCE_ASSET_LIBRARY },
        { "systems", REFERENCE_ASSET_SYSTEM },
        { "ui", REFERENCE_ASSET_UI },
    };
    for (int i = 0; i < (int)(sizeof folders / sizeof *folders); i++) {
        if (content_path_in_folder(scene, path, folders[i].folder)) {
            return folders[i].kind;
        }
    }
    return REFERENCE_ASSET_KIND_COUNT;
}

static const char *content_asset_name(const char *path)
{
    const char *base = content_basename(path);
    static char name[256];
    snprintf(name, sizeof name, "%s", base);
    char *extension = strrchr(name, '.');
    if (extension && !strcmp(extension, ASSET_EXTENSION)) {
        *extension = '\0';
    }
    return name;
}

/* ---------- Assets ---------- */

static void add_content_assets(reference_index *index, app_scene_authoring *scene)
{
    for (int i = 0; i < acount(scene->content); i++) {
        scene_content_entry *entry = &scene->content[i];
        reference_asset_kind kind = entry->directory || entry->image
                                      ? REFERENCE_ASSET_KIND_COUNT
                                      : content_asset_kind(scene, entry->path);
        if (kind != REFERENCE_ASSET_KIND_COUNT) {
            reference_index_add_asset(
                index,
                kind,
                content_asset_name(entry->path),
                entry->path,
                NULL
            );
        }
    }
}

static void add_tab_assets(reference_index *index, app_scene_authoring *scene)
{
    for (int i = 0; i < acount(scene->tabs); i++) {
        scene_document_tab *tab = &scene->tabs[i];
        if (tab->kind == APP_EDITOR_DOCUMENT_SCENE) {
            reference_index_add_asset(
                index,
                REFERENCE_ASSET_SCENE,
                tab_name(tab, "Scene"),
                tab->path,
                tab_key(tab)
            );
        }
        if (tab->kind == APP_EDITOR_DOCUMENT_SYSTEM) {
            reference_index_add_asset(
                index,
                REFERENCE_ASSET_SYSTEM,
                tab_name(tab, "System"),
                tab->path,
                tab_key(tab)
            );
        }
    }
}

static int definition_asset(reference_index *index, game_live_definition *definition)
{
    return reference_index_add_asset(
        index,
        reference_domain_kind(definition->working->domain),
        definition->name,
        definition->path,
        definition->path ? NULL : definition->name
    );
}

static int prototype_asset(reference_index *index, scene_prototype_asset *asset)
{
    return reference_index_add_asset(
        index,
        REFERENCE_ASSET_PROTOTYPE,
        asset->prototype->name,
        asset->path,
        asset->path ? NULL : game_prototype_identity(asset->prototype)
    );
}

/* ---------- References ---------- */

static void add_ui_references(reference_index *index, app_scene_authoring *scene)
{
    for (int i = 0; i < acount(scene->content); i++) {
        scene_content_entry *entry = &scene->content[i];
        if (entry->directory || entry->image ||
            content_asset_kind(scene, entry->path) != REFERENCE_ASSET_UI) {
            continue;
        }
        char *text = text_file_read(entry->path);
        ui_document document = { 0 };
        char error[160];
        if (text && ui_document_read(text, NULL, NULL, &document, error, sizeof error)) {
            int source = reference_index_find_asset(
                index,
                REFERENCE_ASSET_UI,
                entry->path,
                NULL
            );
            reference_index_add_ui(index, source, document.rml);
        }
        ui_document_free(&document);
        free(text);
    }
}

/* A saved scene's objects, each also holding what its prototype names. */
static void add_scene_document(
    app_scene_authoring *scene,
    reference_index *index,
    int source,
    const char *json
)
{
    char error[256];
    game_scene_document *document = json ? game_scene_document_load(json, error, sizeof error)
                                         : NULL;
    for (int i = 0; document && i < acount(document->entities); i++) {
        game_scene_entity *entity = &document->entities[i];
        scene_prototype_asset *base = find_prototype(scene, entity->prototype);
        reference_object object = {
            .name = entity->name,
            .prototype = entity->prototype,
            .base = base ? base->prototype : NULL,
            .overrides = entity->components_json,
            .behaviors = (const char *const *)entity->behaviors,
            .behavior_count = acount(entity->behaviors),
        };
        reference_index_add_object(index, source, &object);
    }
    if (document) {
        reference_index_add_page(index, source, document->ui, scene->default_ui);
    }
    game_scene_document_free(document);
}

/* Scene tabs other than the current one, whose objects are live actors. */
static void add_scene_tab_references(reference_index *index, app_scene_authoring *scene)
{
    for (int i = 0; i < acount(scene->tabs); i++) {
        scene_document_tab *tab = &scene->tabs[i];
        if (tab->kind == APP_EDITOR_DOCUMENT_SCENE && !current_scene_tab(scene, tab)) {
            int source = reference_index_find_asset(index, REFERENCE_ASSET_SCENE, tab->path, tab_key(tab));
            add_scene_document(scene, index, source, tab->scene_json);
        }
    }
}

/* The open graph's working copy holds edits live apply has not reached. */
static const behavior_asset *definition_working(app_scene_authoring *scene, game_live_definition *definition)
{
    const behavior_asset *working = definition->working;
    const behavior_asset *current = definition == scene->definition && scene->graph_editor
                                      ? editor_behavior_asset(scene->graph_editor)
                                      : NULL;
    bool same = current && current->name && working->name &&
                current->domain == working->domain &&
                !strcmp(current->name, working->name);
    return same ? current : working;
}

static void add_definitions(reference_index *index, app_scene_authoring *scene, bool references)
{
    for (int i = 0; i < game_live_definition_count(scene->live); i++) {
        game_live_definition *definition = game_live_definition_at(scene->live, i);
        int source = definition_asset(index, definition);
        if (references) {
            reference_index_add_graphs(index, source, definition_working(scene, definition));
        }
    }
}

static void add_prototypes(reference_index *index, app_scene_authoring *scene, bool references)
{
    for (int i = 0; i < acount(scene->prototypes); i++) {
        int source = prototype_asset(index, &scene->prototypes[i]);
        if (references) {
            reference_index_add_prototype(index, source, scene->prototypes[i].prototype);
        }
    }
}

/* A live actor holds its behaviors and overrides as they are now: from the
 * runtime snapshot while playing, otherwise from the world. */
static void add_actor_references(
    app_scene_authoring *scene,
    reference_index *index,
    int source,
    scene_actor *actor
)
{
    const game_prototype *snapshot = scene->runtime_snapshot_valid
                                       ? actor->runtime_snapshot
                                       : NULL;
    assert(!scene->runtime_snapshot_valid || snapshot);
    const char **behaviors = NULL;
    for (int i = 0; snapshot && i < acount(snapshot->graphs); i++) {
        apush(behaviors, snapshot->graphs[i]);
    }
    int count = snapshot ? 0
                         : game_behavior_attachment_count_for_entity(
                               scene->live->behaviors,
                               actor->entity
                           );
    for (int i = 0; i < count; i++) {
        game_behavior_attachment_info info;
        if (game_behavior_attachment_info_at(scene->live->behaviors, actor->entity, i, &info) ==
            0) {
            apush(behaviors, info.asset_name);
        }
    }
    char *overrides = snapshot ? game_prototype_overrides_json(actor->base, snapshot)
                               : game_prototype_capture_overrides_json(
                                     actor->base,
                                     scene->world,
                                     actor->entity
                                 );
    reference_object object = {
        .name = actor->name,
        .prototype = actor->prototype_name,
        .base = actor->base,
        .overrides = overrides,
        .behaviors = behaviors,
        .behavior_count = acount(behaviors),
    };
    reference_index_add_object(index, source, &object);
    free(overrides);
    afree(behaviors);
}

static void add_active_scene_references(reference_index *index, app_scene_authoring *scene)
{
    int source = -1;
    for (int i = 0; source < 0 && i < acount(scene->tabs); i++) {
        scene_document_tab *tab = &scene->tabs[i];
        if (tab->kind == APP_EDITOR_DOCUMENT_SCENE && current_scene_tab(scene, tab)) {
            source = reference_index_find_asset(index, REFERENCE_ASSET_SCENE, tab->path, tab_key(tab));
        }
    }
    for (int i = 0; source >= 0 && i < acount(scene->actors); i++) {
        add_actor_references(scene, index, source, &scene->actors[i]);
    }
    if (source >= 0) {
        reference_index_add_page(index, source, scene->ui, scene->default_ui);
    }
}

/* The active system's graph is its working copy; others load from their tab. */
static void add_system_references(reference_index *index, app_scene_authoring *scene)
{
    scene_document_tab *active = scene->active_tab >= 0 &&
                                         scene->active_tab < acount(scene->tabs)
                                   ? &scene->tabs[scene->active_tab]
                                   : NULL;
    for (int i = 0; i < acount(scene->tabs); i++) {
        scene_document_tab *tab = &scene->tabs[i];
        if (tab->kind != APP_EDITOR_DOCUMENT_SYSTEM) {
            continue;
        }
        int source = reference_index_find_asset(index, REFERENCE_ASSET_SYSTEM, tab->path, tab_key(tab));
        if (active == tab && scene->system_document) {
            const behavior_asset *working = scene->graph_document_kind == APP_EDITOR_DOCUMENT_SYSTEM
                                              ? editor_behavior_asset(scene->graph_editor)
                                              : game_system_document_behavior_asset(
                                                    scene->system_document
                                                );
            reference_index_add_graphs(index, source, working);
            continue;
        }
        behavior_asset *asset = tab->system_json
                                  ? behavior_asset_load_json(tab->system_json)
                                  : NULL;
        if (asset) {
            reference_index_add_graphs(index, source, asset);
            behavior_asset_free(asset);
        }
    }
}

int app_scene_authoring_refresh_asset_index(app_scene_authoring *scene)
{
    if (!scene) {
        return -1;
    }
    if (!scene->asset_index) {
        scene->asset_index = reference_index_new();
    }
    reference_index *index = scene->asset_index;
    reference_index_clear(index);
    /* Every asset first, so references find the event definitions they name. */
    add_content_assets(index, scene);
    add_tab_assets(index, scene);
    add_definitions(index, scene, false);
    add_prototypes(index, scene, false);
    add_ui_references(index, scene);
    add_scene_tab_references(index, scene);
    add_definitions(index, scene, true);
    add_prototypes(index, scene, true);
    add_active_scene_references(index, scene);
    add_system_references(index, scene);
    scene->asset_index_built_generation = scene->asset_index_generation;
    return 0;
}

const reference_index *app_scene_authoring_asset_index(const app_scene_authoring *scene)
{
    return scene ? scene->asset_index : NULL;
}

/* A scene or system opens its tab, or its file when no tab shows it. */
static int open_tab_asset(app_scene_authoring *scene, const reference_asset *asset, int position)
{
    app_editor_document_kind kind = asset->kind == REFERENCE_ASSET_SCENE
                                      ? APP_EDITOR_DOCUMENT_SCENE
                                      : APP_EDITOR_DOCUMENT_SYSTEM;
    for (int i = 0; i < acount(scene->tabs); i++) {
        scene_document_tab *tab = &scene->tabs[i];
        if (tab->kind == kind &&
            reference_index_find_asset(scene->asset_index, asset->kind, tab->path, tab_key(tab)) ==
                position) {
            return scene_tabs_activate(scene, i);
        }
    }
    if (!asset->path) {
        return -1;
    }
    return asset->kind == REFERENCE_ASSET_SCENE
             ? scene_tabs_open_scene(scene, asset->path)
             : open_system_asset(scene, asset->path);
}

int app_scene_authoring_open_asset(app_scene_authoring *scene, int position)
{
    const reference_asset *asset = scene && scene->asset_index
                                     ? reference_index_asset_at(scene->asset_index, position)
                                     : NULL;
    if (!asset || asset->unresolved) {
        return -1;
    }
    switch (asset->kind) {
        case REFERENCE_ASSET_SCENE:
        case REFERENCE_ASSET_SYSTEM:
            return open_tab_asset(scene, asset, position);
        case REFERENCE_ASSET_PROTOTYPE:
            return app_scene_authoring_open_prototype(scene, asset->name);
        case REFERENCE_ASSET_BEHAVIOR:
        case REFERENCE_ASSET_EVENT:
        case REFERENCE_ASSET_MATERIAL:
        case REFERENCE_ASSET_SHAPE:
        case REFERENCE_ASSET_FIELD:
        case REFERENCE_ASSET_LIBRARY:
            return app_scene_authoring_open_behavior(scene, asset->name);
        case REFERENCE_ASSET_UI:
            return asset->path ? scene_tabs_open_ui(scene, asset->path) : -1;
        case REFERENCE_ASSET_KIND_COUNT: break;
    }
    return -1;
}
