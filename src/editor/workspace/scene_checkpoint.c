#include "assets/scene_document.h"
#include "editor/ui/ui_shell.h"
#include "editor/workspace/checkpoint.h"
#include "editor/workspace/scene_internal.h"
#include "graph/document/serialize.h"
#include "runtime/generated.h"

#include <assert.h>
#include <stdio.h>

static void owned_text(yyjson_mut_doc *doc, yyjson_mut_val *object, const char *key, char *text)
{
    checkpoint_text(doc, object, key, text);
    free(text);
}

static const ecs_component_desc *resolve_component(const char *name, void *)
{
    for (int i = 0; name && i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
        if (!strcmp(name, GAME_REGISTRY_COMPONENTS[i]->name)) {
            return GAME_REGISTRY_COMPONENTS[i];
        }
    }
    return NULL;
}

static const char *component_name(ecs_component_id id, void *)
{
    for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
        if (GAME_REGISTRY_COMPONENTS[i]->id == id) {
            return GAME_REGISTRY_COMPONENTS[i]->name;
        }
    }
    return NULL;
}

static void checkpoint_tabs(yyjson_mut_doc *doc, yyjson_mut_val *root, app_scene_authoring *scene)
{
    scene_tabs_sync_current(scene);
    yyjson_mut_obj_add_int(doc, root, "active_tab", scene->active_tab);
    checkpoint_text(doc, root, "scene_tab_key", scene_tabs_current_scene_key(scene));
    yyjson_mut_val *tabs = yyjson_mut_obj_add_arr(doc, root, "tabs");
    for (int i = 0; i < acount(scene->tabs); i++) {
        const scene_document_tab *tab = &scene->tabs[i];
        yyjson_mut_val *item = yyjson_mut_arr_add_obj(doc, tabs);
        yyjson_mut_obj_add_int(doc, item, "kind", tab->kind);
        checkpoint_text(doc, item, "key", tab->key);
        checkpoint_text(doc, item, "label", tab->label);
        checkpoint_text(doc, item, "path", tab->path);
        checkpoint_text(doc, item, "scene", tab->scene_json);
        checkpoint_text(doc, item, "history", tab->scene_history);
        yyjson_mut_obj_add_uint(doc, item, "selected_entity", tab->selected_entity);
        yyjson_mut_obj_add_val(doc, item, "camera", checkpoint_camera(doc, tab->camera));
        yyjson_mut_obj_add_bool(doc, item, "camera_saved", tab->camera_saved);
        yyjson_mut_obj_add_bool(doc, item, "dirty", tab->scene_dirty);
        yyjson_mut_obj_add_int(doc, item, "selected", tab->selected);
        yyjson_mut_obj_add_int(doc, item, "selected_prototype", tab->selected_prototype);
        yyjson_mut_obj_add_int(doc, item, "selected_attachment", tab->selected_attachment);
        checkpoint_text(doc, item, "selected_prototype_name", tab->selected_prototype_name);
        yyjson_mut_val *entities = yyjson_mut_obj_add_arr(doc, item, "entities");
        for (int j = 0; j < tab->scene_entity_count; j++) {
            yyjson_mut_arr_add_uint(doc, entities, tab->scene_entities[j]);
        }
        checkpoint_text(doc, item, "system", tab->system_json);
        checkpoint_text(doc, item, "system_saved", tab->system_saved);
        checkpoint_text(doc, item, "asset_saved", tab->asset_saved);
    }
}

static void free_checkpoint_tab(scene_document_tab *tab)
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
}

static bool restore_next_scene_key(app_scene_authoring *scene, yyjson_val *root)
{
    int next = yyjson_get_int(yyjson_obj_get(root, "next_scene_key"));
    scene->next_scene_key = next > 0 ? next : 1;
    for (int i = 0; i < acount(scene->tabs); i++) {
        const scene_document_tab *tab = &scene->tabs[i];
        int key = 0;
        if (tab->kind == APP_EDITOR_DOCUMENT_SCENE && tab->key &&
            sscanf(tab->key, "scene:scene-%d", &key) == 1 &&
            key >= scene->next_scene_key) {
            scene->next_scene_key = key + 1;
        }
    }
    return true;
}

static bool restore_tabs(app_scene_authoring *scene, yyjson_val *root)
{
    yyjson_val *stored = yyjson_obj_get(root, "tabs");
    if (!stored) {
        return true;
    }
    if (!yyjson_is_arr(stored)) {
        return false;
    }
    scene_document_tab *candidate = NULL;
    size_t i, count;
    yyjson_val *item;
    yyjson_arr_foreach(stored, i, count, item)
    {
        int kind = yyjson_get_int(yyjson_obj_get(item, "kind"));
        const char *key = yyjson_get_str(yyjson_obj_get(item, "key"));
        const char *label = yyjson_get_str(yyjson_obj_get(item, "label"));
        if (!yyjson_is_obj(item) || !key || !label ||
            kind < APP_EDITOR_DOCUMENT_SCENE || kind > APP_EDITOR_DOCUMENT_SYSTEM) {
            goto fail;
        }
        for (int j = 0; j < acount(candidate); j++) {
            if (strcmp(candidate[j].key, key) == 0) {
                goto fail;
            }
        }
        scene_document_tab tab = {
            .kind = kind,
            .key = xstrdup(key),
            .label = xstrdup(label),
            .path = checkpoint_copy_text(item, "path"),
            .scene_json = checkpoint_copy_text(item, "scene"),
            .scene_history = checkpoint_copy_text(item, "history"),
            .selected_entity = yyjson_get_uint(yyjson_obj_get(item, "selected_entity")),
            .camera_saved = yyjson_get_bool(yyjson_obj_get(item, "camera_saved")),
            .scene_dirty = yyjson_get_bool(yyjson_obj_get(item, "dirty")),
            .selected = yyjson_get_int(yyjson_obj_get(item, "selected")),
            .selected_prototype = yyjson_get_int(yyjson_obj_get(item, "selected_prototype")),
            .selected_attachment = yyjson_get_int(yyjson_obj_get(item, "selected_attachment")),
            .selected_prototype_name = checkpoint_copy_text(item, "selected_prototype_name"),
            .system_json = checkpoint_copy_text(item, "system"),
            .system_saved = checkpoint_copy_text(item, "system_saved"),
            .asset_saved = checkpoint_copy_text(item, "asset_saved"),
        };
        if (!checkpoint_read_camera(yyjson_obj_get(item, "camera"), &tab.camera)) {
            free_checkpoint_tab(&tab);
            goto fail;
        }
        yyjson_val *entities = yyjson_obj_get(item, "entities");
        if (!yyjson_is_arr(entities)) {
            free_checkpoint_tab(&tab);
            goto fail;
        }
        for (size_t j = 0; j < yyjson_arr_size(entities); j++) {
            yyjson_val *entity = yyjson_arr_get(entities, j);
            if (!yyjson_is_uint(entity)) {
                free_checkpoint_tab(&tab);
                goto fail;
            }
            apush(tab.scene_entities, yyjson_get_uint(entity));
        }
        tab.scene_entity_count = acount(tab.scene_entities);
        apush(candidate, tab);
    }
    if (!acount(candidate)) {
        goto fail;
    }
    int active = yyjson_get_int(yyjson_obj_get(root, "active_tab"));
    if (active < 0 || active >= acount(candidate)) {
        goto fail;
    }
    scene_tabs_free(scene);
    scene->tabs = candidate;
    scene->active_tab = active;
    scene->close_tab = -1;
    editor_ui_set_document_bar_visible(true);
    free(scene->current_scene_tab_key);
    scene->current_scene_tab_key = NULL;
    const char *scene_tab_key = yyjson_get_str(yyjson_obj_get(root, "scene_tab_key"));
    if (scene_tab_key) {
        for (int i = 0; i < acount(scene->tabs); i++) {
            if (scene->tabs[i].kind == APP_EDITOR_DOCUMENT_SCENE &&
                !strcmp(scene->tabs[i].key, scene_tab_key)) {
                scene->current_scene_tab_key = xstrdup(scene_tab_key);
                break;
            }
        }
    }
    if (!scene->current_scene_tab_key &&
        scene->tabs[active].kind == APP_EDITOR_DOCUMENT_SCENE) {
        scene->current_scene_tab_key = xstrdup(scene->tabs[active].key);
    }
    restore_next_scene_key(scene, root);
    return true;
fail:
    for (int j = 0; j < acount(candidate); j++) {
        free_checkpoint_tab(&candidate[j]);
    }
    afree(candidate);
    return false;
}

char *app_scene_authoring_checkpoint(app_scene_authoring *scene)
{
    if (scene_authoring_sync_document(scene) != 0) {
        return NULL;
    }
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_int(doc, root, "version", 1);
    checkpoint_text(doc, root, "content", scene->content_root);
    checkpoint_text(doc, root, "path", scene->scene_path);
    owned_text(doc, root, "scene", scene_authoring_scene_json(scene));
    yyjson_mut_obj_add_bool(doc, root, "dirty", scene->scene_dirty);
    yyjson_mut_obj_add_val(
        doc,
        root,
        "camera",
        checkpoint_camera(doc, scene->camera.view)
    );
    yyjson_mut_obj_add_bool(doc, root, "camera_saved", scene->camera.saved);
    yyjson_mut_obj_add_int(doc, root, "document_kind", scene->document.kind);
    checkpoint_text(doc, root, "document", scene->document.id);
    checkpoint_text(doc, root, "graph_document", scene->graph_document_id);
    yyjson_mut_obj_add_int(doc, root, "graph_kind", scene->graph_document_kind);
    checkpoint_text(
        doc,
        root,
        "definition",
        scene->definition ? scene->definition->name : NULL
    );
    yyjson_mut_obj_add_int(doc, root, "selected", scene->selected);
    yyjson_mut_obj_add_int(doc, root, "selected_prototype", scene->selected_prototype);
    checkpoint_text(
        doc,
        root,
        "selected_prototype_name",
        selected_prototype(scene) && selected_prototype(scene)->prototype
            ? selected_prototype(scene)->prototype->name
            : NULL
    );
    yyjson_mut_obj_add_int(doc, root, "selected_attachment", scene->selected_attachment);
    yyjson_mut_obj_add_int(doc, root, "next_scene_key", scene->next_scene_key);
    checkpoint_text(doc, root, "selected_content", scene->content_selected_path);
    checkpoint_text(doc, root, "scene_search", scene->scene_search);
    checkpoint_text(doc, root, "asset_search", scene->asset_search);
    yyjson_mut_obj_add_bool(doc, root, "navigator_scene", scene->navigator_scene);
    yyjson_mut_obj_add_int(doc, root, "runtime_target", scene->active_runtime_target);
    yyjson_mut_obj_add_bool(
        doc,
        root,
        "runtime_session",
        scene->runtime_snapshot_valid || scene->preview_prototype_id
    );
    yyjson_mut_obj_add_bool(
        doc,
        root,
        "playing",
        scene->active_runtime_target == APP_RUNTIME_SCENE ? scene->runtime_playing
                                                          : scene->preview_playing
    );
    yyjson_mut_obj_add_int(doc, root, "preview_prototype", scene->runtime_preview_prototype);
    yyjson_mut_obj_add_bool(doc, root, "preview_loop", scene->preview_loop);
    yyjson_mut_obj_add_bool(doc, root, "preview_follow", scene->preview_follow);
    owned_text(doc, root, "history", scene_history_checkpoint(scene->history));
    owned_text(doc, root, "navigation", scene_navigation_checkpoint(scene->navigation));
    yyjson_mut_val *actors = yyjson_mut_obj_add_arr(doc, root, "actors");
    for (int i = 0; i < acount(scene->actors); i++) {
        yyjson_mut_val *actor = yyjson_mut_arr_add_obj(doc, actors);
        yyjson_mut_obj_add_uint(
            doc,
            actor,
            "entity",
            ecs_entity_to_u64(scene->actors[i].entity)
        );
        yyjson_mut_obj_add_bool(doc, actor, "marked", scene->actors[i].marked);
        const scene_actor *source = &scene->actors[i];
        game_prototype *captured = scene->runtime_snapshot_valid
                                     ? NULL
                                     : game_prototype_capture_entity(
                                           source->name,
                                           scene->world,
                                           source->entity,
                                           &scene->registry
                                       );
        const game_prototype *state = scene->runtime_snapshot_valid
                                        ? source->runtime_snapshot
                                        : captured;
        owned_text(doc, actor, "state", state ? game_prototype_save(state) : NULL);
        owned_text(doc, actor, "base", game_prototype_save(source->base));
        game_prototype_free(captured);
    }
    yyjson_mut_val *prototypes = yyjson_mut_obj_add_arr(doc, root, "prototypes");
    for (int i = 0; i < acount(scene->prototypes); i++) {
        const scene_prototype_asset *asset = &scene->prototypes[i];
        yyjson_mut_val *item = yyjson_mut_arr_add_obj(doc, prototypes);
        checkpoint_text(doc, item, "path", asset->path);
        owned_text(doc, item, "json", game_prototype_save(asset->prototype));
        yyjson_mut_obj_add_bool(doc, item, "dirty", asset->dirty);
        yyjson_mut_obj_add_val(doc, item, "undo", checkpoint_strings(doc, asset->undo));
        yyjson_mut_obj_add_val(doc, item, "redo", checkpoint_strings(doc, asset->redo));
        yyjson_mut_obj_add_val(
            doc,
            item,
            "camera",
            checkpoint_camera(doc, asset->camera.view)
        );
        yyjson_mut_obj_add_bool(doc, item, "camera_saved", asset->camera.saved);
    }
    yyjson_mut_val *behaviors = yyjson_mut_obj_add_arr(doc, root, "behaviors");
    for (int i = 0; i < game_live_definition_count(scene->live); i++) {
        const game_live_definition *definition = game_live_definition_at(scene->live, i);
        yyjson_mut_val *item = yyjson_mut_arr_add_obj(doc, behaviors);
        checkpoint_text(doc, item, "name", definition->name);
        checkpoint_text(doc, item, "path", definition->path);
        owned_text(doc, item, "working", behavior_asset_save_json(definition->working));
        owned_text(doc, item, "applied", behavior_asset_save_json(definition->applied));
        yyjson_mut_obj_add_bool(doc, item, "dirty", definition->dirty);
        yyjson_mut_obj_add_bool(doc, item, "unsaved", definition->unsaved);
    }
    checkpoint_text(doc, root, "system_path", scene->system_path);
    owned_text(
        doc,
        root,
        "system",
        scene->system_document
            ? game_system_document_save(scene->system_document, resolve_component, component_name, NULL)
            : NULL
    );
    checkpoint_tabs(doc, root, scene);
    char *json = yyjson_mut_write(doc, 0, NULL);
    yyjson_mut_doc_free(doc);
    return json;
}

static bool restore_behaviors(app_scene_authoring *scene, yyjson_val *behaviors)
{
    scene->definition = NULL;
    while (game_live_definition_count(scene->live)) {
        game_live_definition *definition = game_live_definition_at(scene->live, 0);
        if (game_live_remove_definition(scene->live, definition->name) != 0) {
            return false;
        }
    }
    game_prototype_preview_free(scene->prototype_preview);
    scene->prototype_preview = game_prototype_preview_new(&scene->registry);
    if (!scene->prototype_preview) {
        return false;
    }
    /* Definitions precede their consumers regardless of their saved order. */
    for (int pass = 0; pass < 3; pass++) {
        size_t i, count;
        yyjson_val *item;
        yyjson_arr_foreach(behaviors, i, count, item)
        {
            const char *name = yyjson_get_str(yyjson_obj_get(item, "name"));
            const char *applied_json = yyjson_get_str(yyjson_obj_get(item, "applied"));
            const char *working_json = yyjson_get_str(yyjson_obj_get(item, "working"));
            if (!name || !applied_json || !working_json) {
                return false;
            }
            behavior_asset *applied = behavior_asset_load_json(applied_json);
            if (!applied) {
                return false;
            }
            behavior_domain domain = applied->domain;
            int stage = domain == BEHAVIOR_DOMAIN_EVENT ? 2
                      : (domain == BEHAVIOR_DOMAIN_MATERIAL || domain == BEHAVIOR_DOMAIN_SHAPE)
                          ? 1
                          : 0;
            if (stage != pass) {
                behavior_asset_free(applied);
                continue;
            }
            behavior_asset *working = behavior_asset_load_json(working_json);
            game_live_definition *definition = working
                                                 ? game_live_definition_new(name, applied)
                                                 : NULL;
            if (!definition) {
                behavior_asset_free(applied);
                behavior_asset_free(working);
                return false;
            }
            if (game_live_add_definition(scene->live, definition) != 0) {
                game_live_definition_free(definition);
                behavior_asset_free(working);
                return false;
            }
            if (game_prototype_preview_register_behavior(
                    scene->prototype_preview,
                    name,
                    definition->applied
                ) != 0) {
                behavior_asset_free(working);
                return false;
            }
            behavior_asset_free(definition->working);
            definition->working = working;
            definition->path = checkpoint_copy_text(item, "path");
            definition->dirty = yyjson_get_bool(yyjson_obj_get(item, "dirty"));
            definition->unsaved = yyjson_get_bool(yyjson_obj_get(item, "unsaved"));
        }
    }
    return true;
}

static bool restore_prototypes(app_scene_authoring *scene, yyjson_val *prototypes)
{
    for (int i = 0; i < acount(scene->prototypes); i++) {
        scene_prototype_asset *asset = &scene->prototypes[i];
        free(asset->path);
        game_prototype_free(asset->prototype);
        checkpoint_free_strings(asset->undo);
        checkpoint_free_strings(asset->redo);
    }
    aclear(scene->prototypes);
    scene->selected_prototype = -1;
    size_t i, count;
    yyjson_val *item;
    yyjson_arr_foreach(prototypes, i, count, item)
    {
        const char *json = yyjson_get_str(yyjson_obj_get(item, "json"));
        if (!json || app_scene_authoring_register_prototype(
                         scene,
                         json,
                         yyjson_get_str(yyjson_obj_get(item, "path"))
                     ) != 0) {
            return false;
        }
        scene_prototype_asset *asset = &alast(scene->prototypes);
        asset->dirty = yyjson_get_bool(yyjson_obj_get(item, "dirty"));
        asset->camera.saved = yyjson_get_bool(yyjson_obj_get(item, "camera_saved"));
        if (!checkpoint_read_strings(yyjson_obj_get(item, "undo"), &asset->undo) ||
            !checkpoint_read_strings(yyjson_obj_get(item, "redo"), &asset->redo) ||
            !checkpoint_read_camera(
                yyjson_obj_get(item, "camera"),
                &asset->camera.view
            )) {
            return false;
        }
        if (game_entities_register(scene->live->entities, asset->prototype) < 0 ||
            game_prototype_preview_register_prototype(
                scene->prototype_preview,
                asset->prototype
            ) != 0) {
            return false;
        }
    }
    return true;
}

static bool restore_system(app_scene_authoring *scene, yyjson_val *root)
{
    const char *json = yyjson_get_str(yyjson_obj_get(root, "system"));
    game_system_document_free(scene->system_document);
    scene->system_document = NULL;
    free(scene->system_path);
    scene->system_path = checkpoint_copy_text(root, "system_path");
    if (!json) {
        return true;
    }
    char error[256];
    scene->system_document = game_system_document_load(json, resolve_component, NULL, error, sizeof error);
    return scene->system_document != NULL;
}

static bool restore_scene(app_scene_authoring *scene, yyjson_val *root, yyjson_val *actors)
{
    const char *json = yyjson_get_str(yyjson_obj_get(root, "scene"));
    char error[256];
    game_scene_document *document = game_scene_document_load(json, error, sizeof error);
    if (!document) {
        return false;
    }
    game_prototype **bases = NULL;
    bool restored = false;
    if ((size_t)acount(document->entities) != yyjson_arr_size(actors)) {
        goto done;
    }
    for (int i = 0; i < acount(document->entities); i++) {
        yyjson_val *actor = yyjson_arr_get(actors, i);
        const char *state_json = yyjson_get_str(yyjson_obj_get(actor, "state"));
        const char *base_json = yyjson_get_str(yyjson_obj_get(actor, "base"));
        scene_prototype_asset
            *prototype = find_prototype(scene, document->entities[i].prototype);
        if (!state_json || !base_json || !prototype) {
            goto done;
        }
        game_prototype *state = game_prototype_load(state_json, &scene->registry, error, sizeof error);
        game_prototype *base = game_prototype_load(base_json, &scene->registry, error, sizeof error);
        apush(bases, base);
        char *overrides = state
                            ? game_prototype_overrides_json(prototype->prototype, state)
                            : NULL;
        game_prototype_free(state);
        bool valid = base && overrides &&
                     game_scene_document_set_entity_components(document, i, overrides) ==
                         0;
        free(overrides);
        if (!valid) {
            goto done;
        }
    }
    char *rebased = game_scene_document_save(document);
    restored = rebased && scene_authoring_load_json(
                              scene,
                              rebased,
                              yyjson_get_str(yyjson_obj_get(root, "path"))
                          ) == 0;
    free(rebased);
    if (restored) {
        for (int i = 0; i < acount(bases); i++) {
            game_prototype_free(scene->actors[i].base);
            scene->actors[i].base = bases[i];
            bases[i] = NULL;
        }
    }
done:
    for (int i = 0; i < acount(bases); i++) {
        game_prototype_free(bases[i]);
    }
    afree(bases);
    game_scene_document_free(document);
    return restored;
}

static bool restore_playback(app_scene_authoring *scene, yyjson_val *root)
{
    int target = yyjson_get_int(yyjson_obj_get(root, "runtime_target"));
    if (target != APP_RUNTIME_SCENE && target != APP_RUNTIME_PREVIEW) {
        return false;
    }
    scene->active_runtime_target = target;
    scene->runtime_preview_prototype = yyjson_get_int(
        yyjson_obj_get(root, "preview_prototype")
    );
    if (!yyjson_get_bool(yyjson_obj_get(root, "runtime_session"))) {
        return true;
    }
    if (target == APP_RUNTIME_PREVIEW) {
        int index = scene->runtime_preview_prototype;
        if (index < 0 || index >= acount(scene->prototypes) ||
            app_scene_authoring_preview_prototype(
                scene,
                scene->prototypes[index].prototype->name
            ) != 0) {
            return false;
        }
        scene->preview_loop = yyjson_get_bool(yyjson_obj_get(root, "preview_loop"));
        scene->preview_follow = yyjson_get_bool(yyjson_obj_get(root, "preview_follow"));
    } else if (app_scene_authoring_runtime_command(scene, APP_RUNTIME_SCENE, APP_RUNTIME_PLAY) != 0) {
        return false;
    }
    return yyjson_get_bool(yyjson_obj_get(root, "playing")) ||
           app_scene_authoring_runtime_command(scene, target, APP_RUNTIME_PAUSE) == 0;
}

int app_scene_authoring_restore_checkpoint(app_scene_authoring *scene, const char *json)
{
    yyjson_doc *doc = json ? yyjson_read(json, strlen(json), 0) : NULL;
    yyjson_val *root = yyjson_doc_get_root(doc);
    yyjson_val *prototypes = yyjson_obj_get(root, "prototypes"),
               *behaviors = yyjson_obj_get(root, "behaviors"),
               *actors = yyjson_obj_get(root, "actors");
    int kind = yyjson_get_int(yyjson_obj_get(root, "document_kind"));
    const char *scene_json = yyjson_get_str(yyjson_obj_get(root, "scene"));
    const char *document = yyjson_get_str(yyjson_obj_get(root, "document"));
    const char *content = yyjson_get_str(yyjson_obj_get(root, "content"));
    uint64_t *before = NULL, *after = NULL;
    int result = -1;
    if (yyjson_get_int(yyjson_obj_get(root, "version")) != 1 || !scene_json ||
        !document || !content || kind < APP_EDITOR_DOCUMENT_SCENE ||
        kind > APP_EDITOR_DOCUMENT_SYSTEM || !yyjson_is_arr(prototypes) ||
        !yyjson_is_arr(behaviors) || !yyjson_is_arr(actors)) {
        goto done;
    }
    /* Candidate-only mutation. The host retains the old module and world until
     * the entire workspace, including graph history, has passed restoration. */
    if (scene_authoring_load_json(scene, "{\"version\":1,\"name\":\"Checkpoint\",\"entities\":[]}", NULL) !=
            0 ||
        !restore_behaviors(scene, behaviors) || !restore_prototypes(scene, prototypes) ||
        !restore_system(scene, root) || !restore_scene(scene, root, actors) ||
        yyjson_arr_size(actors) != (size_t)acount(scene->actors) ||
        !restore_playback(scene, root)) {
        goto done;
    }
    for (int i = 0; i < acount(scene->actors); i++) {
        yyjson_val *actor = yyjson_arr_get(actors, i);
        yyjson_val *entity = yyjson_obj_get(actor, "entity");
        if (!yyjson_is_uint(entity)) {
            goto done;
        }
        apush(before, yyjson_get_uint(entity));
        apush(after, ecs_entity_to_u64(scene->actors[i].entity));
        scene->actors[i].marked = yyjson_get_bool(yyjson_obj_get(actor, "marked"));
    }
    if (!scene_history_restore_checkpoint(
            scene->history,
            yyjson_get_str(yyjson_obj_get(root, "history")),
            before,
            after,
            acount(before)
        ) ||
        !scene_navigation_restore_checkpoint(
            scene->navigation,
            yyjson_get_str(yyjson_obj_get(root, "navigation")),
            before,
            after,
            acount(before)
        ) ||
        !checkpoint_read_camera(yyjson_obj_get(root, "camera"), &scene->camera.view)) {
        goto done;
    }
    scene->camera.saved = yyjson_get_bool(yyjson_obj_get(root, "camera_saved"));
    scene->scene_dirty = yyjson_get_bool(yyjson_obj_get(root, "dirty"));
    scene->selected = yyjson_get_int(yyjson_obj_get(root, "selected"));
    const char *selected_prototype_name = yyjson_get_str(
        yyjson_obj_get(root, "selected_prototype_name")
    );
    scene->selected_prototype = selected_prototype_name
                                  ? prototype_index(scene, selected_prototype_name)
                                  : yyjson_get_int(yyjson_obj_get(root, "selected_prototype"));
    scene->selected_attachment = yyjson_get_int(yyjson_obj_get(root, "selected_attachment"));
    if (scene->selected < -1 || scene->selected >= acount(scene->actors) ||
        scene->selected_prototype < -1 ||
        scene->selected_prototype >= acount(scene->prototypes)) {
        goto done;
    }
    free(scene->content_root);
    scene->content_root = xstrdup(content);
    free(scene->content_selected_path);
    scene->content_selected_path = checkpoint_copy_text(root, "selected_content");
    const char *scene_search = yyjson_get_str(yyjson_obj_get(root, "scene_search"));
    const char *asset_search = yyjson_get_str(yyjson_obj_get(root, "asset_search"));
    if (!scene_search || !asset_search) {
        goto done;
    }
    snprintf(scene->scene_search, sizeof scene->scene_search, "%s", scene_search);
    snprintf(scene->asset_search, sizeof scene->asset_search, "%s", asset_search);
    const char *definition_name = yyjson_get_str(yyjson_obj_get(root, "definition"));
    scene->definition = definition_name
                          ? game_live_find_definition(scene->live, definition_name)
                          : NULL;
    free(scene->graph_document_id);
    scene->graph_document_id = checkpoint_copy_text(root, "graph_document");
    scene->graph_document_kind = yyjson_get_int(yyjson_obj_get(root, "graph_kind"));
    scene_authoring_select_document(scene, kind, document);
    scene->navigator_scene = yyjson_get_bool(yyjson_obj_get(root, "navigator_scene"));
    scene->editor_state_seen = false;
    scene->behavior_apply_pending = false;
    app_scene_authoring_refresh_content(scene);
    if (!restore_tabs(scene, root)) {
        goto done;
    }
    result = 0;
done:
    afree(before);
    afree(after);
    yyjson_doc_free(doc);
    return result;
}
