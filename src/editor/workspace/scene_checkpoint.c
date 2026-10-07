#include "assets/code_catalog.h"
#include "assets/scene_document.h"
#include "editor/collab/actions_scene.h"
#include "editor/workspace/checkpoint.h"
#include "editor/workspace/scene_internal.h"
#include "graph/document/serialize.h"
#include "runtime/generated.h"
#include "support/asset_json.h"
#include "support/asset_text.h"
#include "support/text_overlay.h"

#include <assert.h>
#include <stdio.h>

static void owned_text(yyjson_mut_doc *doc, yyjson_mut_val *object, const char *key, char *text)
{
    checkpoint_text(doc, object, key, text);
    free(text);
}

static void checkpoint_behavior(
    yyjson_mut_doc *doc,
    yyjson_mut_val *object,
    const char *key,
    const char *metadata_key,
    const behavior_asset *asset
)
{
    char *metadata = NULL;
    char *text = code_source_format_with_metadata(asset, &metadata);
    owned_text(doc, object, key, text);
    owned_text(doc, object, metadata_key, metadata);
}

static behavior_asset *checkpoint_behavior_load(
    const code_catalog *catalog,
    yyjson_val *object,
    const char *key,
    const char *metadata_key,
    char *error,
    size_t size
)
{
    const char *text = yyjson_get_str(yyjson_obj_get(object, key));
    const char *metadata = yyjson_get_str(yyjson_obj_get(object, metadata_key));
    if (!metadata || !code_source_is_structured(text)) {
        return code_catalog_load_behavior(catalog, text, error, size);
    }
    code_source *source = code_source_parse_context(text, NULL, code_catalog_context(catalog));
    bool restored = code_source_metadata_restore(source, metadata);
    behavior_asset *asset = restored ? code_source_build_behavior(source) : NULL;
    if (!asset && size) {
        snprintf(
            error,
            size,
            "%s",
            acount(source->diagnostics) ? source->diagnostics[0].message
                                        : "Invalid checkpoint identity metadata"
        );
    }
    code_source_free(source);
    return asset;
}

static av_value *checkpoint_native_value(yyjson_val *object, const char *key)
{
    yyjson_val *value = yyjson_obj_get(object, key);
    if (!value || yyjson_is_null(value)) {
        return NULL;
    }
    av_doc *doc = av_doc_new();
    av_value *copy = av_value_copy(av_from_json(doc, value));
    av_doc_free(doc);
    return copy;
}

static yyjson_mut_val *checkpoint_values(yyjson_mut_doc *doc, av_value *const *values)
{
    yyjson_mut_val *array = yyjson_mut_arr(doc);
    for (int i = 0; i < acount(values); i++) {
        yyjson_mut_arr_add_val(array, av_to_json(doc, values[i]));
    }
    return array;
}

static bool checkpoint_read_values(yyjson_val *array, av_value ***out)
{
    if (!yyjson_is_arr(array)) {
        return false;
    }
    size_t i, count;
    yyjson_val *value;
    yyjson_arr_foreach(array, i, count, value)
    {
        av_doc *doc = av_doc_new();
        av_value *root = av_from_json(doc, value);
        if (!av_is_obj(root)) {
            av_doc_free(doc);
            return false;
        }
        apush(*out, av_value_copy(root));
        av_doc_free(doc);
    }
    return true;
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
        yyjson_mut_obj_add_val(doc, item, "scene", av_to_json(doc, tab->scene_value));
        yyjson_mut_obj_add_val(doc, item, "history", av_to_json(doc, tab->scene_history));
        yyjson_mut_obj_add_uint(doc, item, "selected_entity", tab->selected_entity);
        yyjson_mut_obj_add_val(doc, item, "camera", checkpoint_camera(doc, tab->camera));
        yyjson_mut_obj_add_bool(doc, item, "camera_saved", tab->camera_saved);
        yyjson_mut_obj_add_bool(doc, item, "dirty", tab->scene_dirty);
        yyjson_mut_obj_add_int(doc, item, "selected", tab->selected);
        yyjson_mut_obj_add_int(doc, item, "selected_prototype", tab->selected_prototype);
        checkpoint_text(doc, item, "selected_prototype_name", tab->selected_prototype_name);
        yyjson_mut_val *entities = yyjson_mut_obj_add_arr(doc, item, "entities");
        yyjson_mut_val *actor_ids = acount(tab->scene_actor_ids) == tab->scene_entity_count
                                      ? yyjson_mut_obj_add_arr(doc, item, "actor_ids")
                                      : NULL;
        for (int j = 0; j < tab->scene_entity_count; j++) {
            yyjson_mut_arr_add_uint(doc, entities, tab->scene_entities[j]);
            if (actor_ids) {
                yyjson_mut_arr_add_uint(doc, actor_ids, tab->scene_actor_ids[j]);
            }
        }
        yyjson_mut_obj_add_val(doc, item, "system", av_to_json(doc, tab->system_value));
        yyjson_mut_obj_add_val(doc, item, "system_saved", av_to_json(doc, tab->system_saved));
        yyjson_mut_obj_add_val(doc, item, "asset_saved", av_to_json(doc, tab->asset_saved));
        yyjson_mut_obj_add_val(doc, item, "source", av_to_json(doc, tab->source_state));
        yyjson_mut_obj_add_bool(doc, item, "source_dirty", tab->source_dirty);
        yyjson_mut_obj_add_bool(doc, item, "source_only", tab->source_only);
        if (tab->ui) {
            yyjson_mut_obj_add_val(doc, item, "ui", ui_buffer_checkpoint(tab->ui, doc));
        }
    }
}

static void free_checkpoint_tab(scene_document_tab *tab)
{
    free(tab->key);
    free(tab->label);
    free(tab->path);
    av_value_free(tab->scene_value);
    av_value_free(tab->scene_history);
    afree(tab->scene_entities);
    afree(tab->scene_actor_ids);
    free(tab->selected_prototype_name);
    av_value_free(tab->asset_saved);
    av_value_free(tab->system_value);
    av_value_free(tab->system_saved);
    ui_buffer_free(tab->ui);
    av_value_free(tab->source_state);
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

static bool restore_next_actor_id(app_scene_authoring *scene, yyjson_val *root)
{
    yyjson_val *stored = yyjson_obj_get(root, "next_actor_id");
    if (stored && (!yyjson_is_uint(stored) || !yyjson_get_uint(stored) ||
                   yyjson_get_uint(stored) >= UINT64_MAX)) {
        return false;
    }
    uint64_t next = stored ? yyjson_get_uint(stored) : scene->next_actor_id;
    for (int i = 0; i < acount(scene->actors); i++) {
        uint64_t identity = scene->actors[i].identity;
        if (identity >= UINT64_MAX) {
            return false;
        }
        if (identity >= next) {
            next = identity + 1;
        }
    }
    for (int i = 0; i < acount(scene->tabs); i++) {
        const scene_document_tab *tab = &scene->tabs[i];
        for (int j = 0; j < acount(tab->scene_actor_ids); j++) {
            uint64_t identity = tab->scene_actor_ids[j];
            if (identity >= UINT64_MAX) {
                return false;
            }
            if (identity >= next) {
                next = identity + 1;
            }
        }
    }
    if (!next || next >= UINT64_MAX) {
        return false;
    }
    scene->next_actor_id = next;
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
            kind < APP_EDITOR_DOCUMENT_SCENE || kind > APP_EDITOR_DOCUMENT_UI) {
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
            .scene_value = checkpoint_native_value(item, "scene"),
            .scene_history = checkpoint_native_value(item, "history"),
            .selected_entity = yyjson_get_uint(yyjson_obj_get(item, "selected_entity")),
            .camera_saved = yyjson_get_bool(yyjson_obj_get(item, "camera_saved")),
            .scene_dirty = yyjson_get_bool(yyjson_obj_get(item, "dirty")),
            .selected = yyjson_get_int(yyjson_obj_get(item, "selected")),
            .selected_prototype = yyjson_get_int(yyjson_obj_get(item, "selected_prototype")),
            .selected_prototype_name = checkpoint_copy_text(item, "selected_prototype_name"),
            .system_value = checkpoint_native_value(item, "system"),
            .system_saved = checkpoint_native_value(item, "system_saved"),
            .asset_saved = checkpoint_native_value(item, "asset_saved"),
            .source_state = checkpoint_native_value(item, "source"),
            .source_dirty = yyjson_get_bool(yyjson_obj_get(item, "source_dirty")),
            .source_only = yyjson_get_bool(yyjson_obj_get(item, "source_only")),
            .ui = kind == APP_EDITOR_DOCUMENT_UI
                    ? ui_buffer_restore(yyjson_obj_get(item, "ui"))
                    : NULL,
        };
        if (!checkpoint_read_camera(yyjson_obj_get(item, "camera"), &tab.camera) ||
            (kind == APP_EDITOR_DOCUMENT_UI && !tab.ui)) {
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
        yyjson_val *actor_ids = yyjson_obj_get(item, "actor_ids");
        if (actor_ids) {
            if (!yyjson_is_arr(actor_ids) ||
                yyjson_arr_size(actor_ids) != (size_t)tab.scene_entity_count) {
                free_checkpoint_tab(&tab);
                goto fail;
            }
            for (size_t j = 0; j < yyjson_arr_size(actor_ids); j++) {
                yyjson_val *id = yyjson_arr_get(actor_ids, j);
                if (!yyjson_is_uint(id) || !yyjson_get_uint(id)) {
                    free_checkpoint_tab(&tab);
                    goto fail;
                }
                for (int k = 0; k < acount(tab.scene_actor_ids); k++) {
                    if (tab.scene_actor_ids[k] == yyjson_get_uint(id)) {
                        free_checkpoint_tab(&tab);
                        goto fail;
                    }
                }
                apush(tab.scene_actor_ids, yyjson_get_uint(id));
            }
        }
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
    for (int i = 0; i < acount(scene->tabs); i++) {
        const scene_document_tab *tab = &scene->tabs[i];
        const char *valid = av_get_str(av_obj_get(tab->source_state, "valid"));
        if (tab->path && valid &&
            av_get_bool(av_obj_get(tab->source_state, "overlay"))) {
            text_overlay_set(tab->path, valid);
        }
    }
    scene->active_tab = active;
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
    av_value *scene_value = scene_authoring_scene_value(scene);
    yyjson_mut_obj_add_val(doc, root, "scene", av_to_json(doc, scene_value));
    av_value_free(scene_value);
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
    yyjson_mut_obj_add_int(doc, root, "next_scene_key", scene->next_scene_key);
    yyjson_mut_obj_add_uint(doc, root, "next_actor_id", scene_actor_next_identity(scene));
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
    yyjson_mut_val *actors = yyjson_mut_obj_add_arr(doc, root, "actors");
    for (int i = 0; i < acount(scene->actors); i++) {
        yyjson_mut_val *actor = yyjson_mut_arr_add_obj(doc, actors);
        yyjson_mut_obj_add_uint(
            doc,
            actor,
            "entity",
            ecs_entity_to_u64(scene->actors[i].entity)
        );
        yyjson_mut_obj_add_uint(doc, actor, "actor_id", scene->actors[i].identity);
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
        av_value *state_value = state ? game_prototype_to_value(state) : NULL;
        av_value *base_value = game_prototype_to_value(source->base);
        yyjson_mut_obj_add_val(doc, actor, "state", av_to_json(doc, state_value));
        yyjson_mut_obj_add_val(doc, actor, "base", av_to_json(doc, base_value));
        av_value_free(state_value);
        av_value_free(base_value);
        game_prototype_free(captured);
    }
    yyjson_mut_val *prototypes = yyjson_mut_obj_add_arr(doc, root, "prototypes");
    for (int i = 0; i < acount(scene->prototypes); i++) {
        const scene_prototype_asset *asset = &scene->prototypes[i];
        yyjson_mut_val *item = yyjson_mut_arr_add_obj(doc, prototypes);
        checkpoint_text(doc, item, "path", asset->path);
        owned_text(doc, item, "json", game_prototype_save(asset->prototype));
        yyjson_mut_obj_add_bool(doc, item, "dirty", asset->dirty);
        yyjson_mut_obj_add_val(doc, item, "undo", checkpoint_values(doc, asset->undo));
        yyjson_mut_obj_add_val(doc, item, "redo", checkpoint_values(doc, asset->redo));
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
        checkpoint_behavior(doc, item, "working", "working_metadata", definition->working);
        checkpoint_behavior(doc, item, "applied", "applied_metadata", definition->applied);
        yyjson_mut_obj_add_bool(doc, item, "dirty", definition->dirty);
        yyjson_mut_obj_add_bool(doc, item, "unsaved", definition->unsaved);
    }
    checkpoint_text(doc, root, "system_path", scene->system_path);
    char *system_metadata = NULL;
    char *system_text = scene->system_document
                          ? game_system_document_save_with_metadata(
                                scene->system_document,
                                &system_metadata,
                                resolve_component,
                                component_name,
                                NULL
                            )
                          : NULL;
    owned_text(doc, root, "system", system_text);
    owned_text(doc, root, "system_metadata", system_metadata);
    checkpoint_tabs(doc, root, scene);
    char *json = yyjson_mut_write(doc, 0, NULL);
    yyjson_mut_doc_free(doc);
    return json;
}

static bool restore_behavior_models(
    app_scene_authoring *scene,
    yyjson_val *behaviors,
    const code_catalog *applied_catalog,
    const code_catalog *working_catalog
)
{
    scene->definition = NULL;
    bool removed = true;
    while (removed && game_live_definition_count(scene->live)) {
        game_live_definition *definition = game_live_definition_at(scene->live, 0);
        removed = game_live_remove_definition(scene->live, definition->name) == 0;
    }
    refresh_asset_catalog(scene);
    if (!removed) {
        return false;
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
            char error[256];
            behavior_asset *applied = checkpoint_behavior_load(
                applied_catalog,
                item,
                "applied",
                "applied_metadata",
                error,
                sizeof error
            );
            if (!applied) {
                snprintf(scene->message, sizeof scene->message, "Checkpoint applied %s: %.160s", name, error);
                fprintf(stderr, "%s\n", scene->message);
                return false;
            }
            behavior_domain domain = applied->domain;
            int stage = domain == BEHAVIOR_DOMAIN_EVENT ? 2
                      : (domain == BEHAVIOR_DOMAIN_MATERIAL ||
                         domain == BEHAVIOR_DOMAIN_SHAPE ||
                         domain == BEHAVIOR_DOMAIN_MODEL ||
                         domain == BEHAVIOR_DOMAIN_FIELD)
                          ? 1
                          : 0;
            if (stage != pass) {
                behavior_asset_free(applied);
                continue;
            }
            behavior_asset *working = checkpoint_behavior_load(
                working_catalog,
                item,
                "working",
                "working_metadata",
                error,
                sizeof error
            );
            if (!working) {
                snprintf(scene->message, sizeof scene->message, "Checkpoint working %s: %.160s", name, error);
                fprintf(stderr, "%s\n", scene->message);
            }
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

static bool checkpoint_references(code_catalog *applied, code_catalog *working, yyjson_val *prototypes)
{
    size_t i, count;
    yyjson_val *item;
    yyjson_arr_foreach(prototypes, i, count, item)
    {
        const char *text = yyjson_get_str(yyjson_obj_get(item, "json"));
        char error[256];
        av_doc *doc = text ? av_doc_read(text, error, sizeof error) : NULL;
        const av_value *root = doc ? av_doc_root(doc) : NULL;
        const av_value *name = root && av_is_obj(root) ? av_obj_get(root, "name")
                                                       : NULL;
        bool valid = name && av_is_str(name) && *av_get_str(name);
        if (valid) {
            code_catalog_add_reference(applied, CODE_REFERENCE_PROTOTYPE, av_get_str(name));
            code_catalog_add_reference(working, CODE_REFERENCE_PROTOTYPE, av_get_str(name));
        }
        av_doc_free(doc);
        if (!valid) {
            return false;
        }
    }
    return true;
}

static bool restore_behaviors(app_scene_authoring *scene, yyjson_val *behaviors, yyjson_val *prototypes)
{
    const char **applied = NULL, **working = NULL;
    const char **applied_metadata = NULL, **working_metadata = NULL;
    size_t i, count;
    yyjson_val *item;
    yyjson_arr_foreach(behaviors, i, count, item)
    {
        apush(applied, yyjson_get_str(yyjson_obj_get(item, "applied")));
        apush(working, yyjson_get_str(yyjson_obj_get(item, "working")));
        apush(applied_metadata, yyjson_get_str(yyjson_obj_get(item, "applied_metadata")));
        apush(working_metadata, yyjson_get_str(yyjson_obj_get(item, "working_metadata")));
    }
    /* Snapshot declarations, rather than disk sources, own both generations.
     * Unsaved signature changes must not alter the applied generation. */
    code_catalog *applied_catalog = code_catalog_from_texts(applied, acount(applied));
    code_catalog *working_catalog = code_catalog_from_texts(working, acount(working));
    bool restored = checkpoint_references(applied_catalog, working_catalog, prototypes) &&
                    code_catalog_restore_metadata(applied_catalog, applied, applied_metadata, acount(applied)) &&
                    code_catalog_restore_metadata(working_catalog, working, working_metadata, acount(working)) &&
                    restore_behavior_models(scene, behaviors, applied_catalog, working_catalog);
    code_catalog_free(applied_catalog);
    code_catalog_free(working_catalog);
    afree(applied);
    afree(working);
    afree(applied_metadata);
    afree(working_metadata);
    return restored;
}

static bool restore_prototypes(app_scene_authoring *scene, yyjson_val *prototypes)
{
    for (int i = 0; i < acount(scene->prototypes); i++) {
        scene_prototype_asset *asset = &scene->prototypes[i];
        free(asset->path);
        game_prototype_free(asset->prototype);
        clear_prototype_history(asset);
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
        if (!checkpoint_read_values(yyjson_obj_get(item, "undo"), &asset->undo) ||
            !checkpoint_read_values(yyjson_obj_get(item, "redo"), &asset->redo) ||
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
    const behavior_asset **assets = NULL;
    for (int i = 0; i < game_live_definition_count(scene->live); i++) {
        apush(assets, game_live_definition_at(scene->live, i)->working);
    }
    code_catalog *catalog = code_catalog_from_assets(assets, acount(assets));
    for (int i = 0; i < acount(scene->prototypes); i++) {
        code_catalog_add_reference(
            catalog,
            CODE_REFERENCE_PROTOTYPE,
            scene->prototypes[i].prototype->name
        );
    }
    scene->system_document = game_system_document_load_context(
        json,
        code_catalog_context(catalog),
        resolve_component,
        NULL,
        error,
        sizeof error
    );
    if (scene->system_document &&
        yyjson_get_str(yyjson_obj_get(root, "system_metadata"))) {
        behavior_asset *definition = checkpoint_behavior_load(
            catalog,
            root,
            "system",
            "system_metadata",
            error,
            sizeof error
        );
        if (!definition) {
            game_system_document_free(scene->system_document);
            scene->system_document = NULL;
        } else {
            game_system_document_replace_behavior_asset(scene->system_document, definition);
        }
    }
    code_catalog_free(catalog);
    afree(assets);
    if (!scene->system_document) {
        snprintf(scene->message, sizeof scene->message, "Checkpoint system: %.200s", error);
        fprintf(stderr, "%s\n", scene->message);
    }
    return scene->system_document != NULL;
}

static bool restore_scene(app_scene_authoring *scene, yyjson_val *root, yyjson_val *actors)
{
    av_doc *source = av_doc_new();
    av_value *scene_value = av_from_json(source, yyjson_obj_get(root, "scene"));
    char error[256];
    game_scene_document *document = game_scene_document_load_value(scene_value, error, sizeof error);
    av_doc_free(source);
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
        av_doc *native = av_doc_new();
        av_value *state_value = av_from_json(native, yyjson_obj_get(actor, "state"));
        av_value *base_value = av_from_json(native, yyjson_obj_get(actor, "base"));
        scene_prototype_asset
            *prototype = find_prototype(scene, document->entities[i].prototype);
        if (!av_is_obj(state_value) || !av_is_obj(base_value) || !prototype) {
            av_doc_free(native);
            goto done;
        }
        game_prototype *state = game_prototype_load_value(state_value, &scene->registry, error, sizeof error);
        game_prototype *base = game_prototype_load_value(base_value, &scene->registry, error, sizeof error);
        av_doc_free(native);
        apush(bases, base);
        av_value *overrides = state
                                ? game_prototype_overrides_value(prototype->prototype, state)
                                : NULL;
        game_prototype_free(state);
        bool valid = base && overrides &&
                     game_scene_document_set_entity_components(document, i, overrides) ==
                         0;
        av_value_free(overrides);
        if (!valid) {
            goto done;
        }
    }
    av_value *rebased = game_scene_document_to_value(document);
    restored = rebased && scene_authoring_load_value(
                              scene,
                              rebased,
                              yyjson_get_str(yyjson_obj_get(root, "path"))
                          ) == 0;
    av_value_free(rebased);
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
    yyjson_val *scene_value = yyjson_obj_get(root, "scene");
    const char *document = yyjson_get_str(yyjson_obj_get(root, "document"));
    const char *content = yyjson_get_str(yyjson_obj_get(root, "content"));
    uint64_t *before = NULL, *after = NULL, *identities = NULL;
    int result = -1;
    if (yyjson_get_int(yyjson_obj_get(root, "version")) != 1 ||
        !yyjson_is_obj(scene_value) || !document || !content ||
        kind < APP_EDITOR_DOCUMENT_SCENE || kind > APP_EDITOR_DOCUMENT_UI ||
        !yyjson_is_arr(prototypes) || !yyjson_is_arr(behaviors) ||
        !yyjson_is_arr(actors)) {
        goto done;
    }
    if (scene_authoring_load_text(scene, "version 1\nname Checkpoint\n(array)entities\n", NULL) !=
            0 ||
        !restore_behaviors(scene, behaviors, prototypes) ||
        !restore_prototypes(scene, prototypes) ||
        !restore_system(scene, root) || !restore_scene(scene, root, actors) ||
        yyjson_arr_size(actors) != (size_t)acount(scene->actors) ||
        !restore_playback(scene, root)) {
        goto done;
    }
    for (int i = 0; i < acount(scene->actors); i++) {
        yyjson_val *actor = yyjson_arr_get(actors, i);
        yyjson_val *entity = yyjson_obj_get(actor, "entity");
        yyjson_val *identity = yyjson_obj_get(actor, "actor_id");
        if (!yyjson_is_uint(entity) ||
            (identity && (!yyjson_is_uint(identity) || !yyjson_get_uint(identity) ||
                          yyjson_get_uint(identity) >= UINT64_MAX))) {
            goto done;
        }
        uint64_t actor_id = identity ? yyjson_get_uint(identity) : 0;
        for (int j = 0; actor_id && j < acount(identities); j++) {
            if (identities[j] == actor_id) {
                goto done;
            }
        }
        apush(identities, actor_id);
        apush(before, yyjson_get_uint(entity));
        apush(after, ecs_entity_to_u64(scene->actors[i].entity));
        scene->actors[i].marked = yyjson_get_bool(yyjson_obj_get(actor, "marked"));
        scene->actors[i].identity = 0;
    }
    for (int i = 0; i < acount(identities); i++) {
        if (identities[i] >= scene->next_actor_id) {
            scene->next_actor_id = identities[i] + 1;
        }
    }
    for (int i = 0; i < acount(scene->actors); i++) {
        uint64_t identity = identities[i] ? identities[i]
                                          : scene_actor_allocate_identity(scene);
        scene_actor_set_identity(scene, &scene->actors[i], identity);
    }
    if (!scene_history_restore_checkpoint(
            scene->history,
            yyjson_get_str(yyjson_obj_get(root, "history")),
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
    if (scene->selected < -1 || scene->selected >= acount(scene->actors) ||
        scene->selected_prototype < -1 ||
        scene->selected_prototype >= acount(scene->prototypes)) {
        goto done;
    }
    free(scene->content_root);
    scene->content_root = xstrdup(content);
    const char *definition_name = yyjson_get_str(yyjson_obj_get(root, "definition"));
    scene->definition = definition_name
                          ? game_live_find_definition(scene->live, definition_name)
                          : NULL;
    free(scene->graph_document_id);
    scene->graph_document_id = checkpoint_copy_text(root, "graph_document");
    scene->graph_document_kind = yyjson_get_int(yyjson_obj_get(root, "graph_kind"));
    scene_authoring_select_document(scene, kind, document);
    scene->editor_state_seen = false;
    scene->behavior_apply_pending = false;
    app_scene_authoring_refresh_content(scene);
    if (!restore_tabs(scene, root) || !restore_next_actor_id(scene, root)) {
        goto done;
    }
    if (scene->active_tab >= 0 && scene->tabs[scene->active_tab].source_only) {
        free(scene->graph_document_id);
        scene->graph_document_id = NULL;
        scene->definition = NULL;
        scene->behavior_apply_pending = false;
    }
    result = 0;
done:
    afree(before);
    afree(after);
    afree(identities);
    yyjson_doc_free(doc);
    return result;
}
