#include "editor/workspace/scene_internal.h"
#include "runtime/generated.h"

#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

int attach_prototype_behavior(ecs_world *world, ecs_entity entity, const char *asset_name, void *user)
{
    app_scene_authoring *scene = (app_scene_authoring *)user;
    if (!scene || !game_behavior_asset_registered(scene->live->behaviors, asset_name)) {
        return -1;
    }
    return game_behavior_attach(scene->live->behaviors, entity, asset_name);
}

int actor_has_behavior(game_behavior_world *behaviors, const scene_actor *actor, const char *asset_name)
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

void free_behavior_snapshots(game_prototype **snapshots)
{
    for (int i = 0; i < acount(snapshots); i++) {
        game_prototype_free(snapshots[i]);
    }
    afree(snapshots);
}

int add_actor(app_scene_authoring *scene, const char *name, const char *prototype_name)
{
    scene_prototype_asset *asset = find_prototype(scene, prototype_name);
    if (!scene || !name || !name[0] || !asset) {
        return -1;
    }
    if (scene->runtime_snapshot_valid &&
        app_scene_authoring_runtime_command(scene, APP_RUNTIME_SCENE, APP_RUNTIME_STOP) !=
            0) {
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
    if (game_entities_rebuild_global_bindings(scene->live->entities) != 0) {
        game_behavior_destroy_entity(scene->live->behaviors, entity);
        snprintf(scene->message, sizeof scene->message, "Global binding names must be unique.");
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
    scene->scene_dirty = true;
    asset_index_invalidate(scene);
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
    game_prototype_free(actor->runtime_snapshot);
    game_prototype_free(actor->restored_state);
    scene_retention_free(actor->retained);
}

static int remove_actor_at(app_scene_authoring *scene, int index)
{
    scene_actor *actor = &scene->actors[index];
    if (game_behavior_destroy_entity(scene->live->behaviors, actor->entity) != ECS_OK) {
        return -1;
    }
    free_actor(actor);
    int count = acount(scene->actors);
    memmove(
        &scene->actors[index],
        &scene->actors[index + 1],
        (size_t)(count - index - 1) * sizeof *scene->actors
    );
    asetlen(scene->actors, count - 1);
    scene->scene_dirty = true;
    asset_index_invalidate(scene);
    scene->selected = index < count - 1 ? index : count - 2;
    if (scene->selected >= 0) {
        select_instance(scene, scene->selected);
    } else {
        sync_editor_inspection(scene);
    }
    return 0;
}

void remove_selected(app_scene_authoring *scene)
{
    if (!selected_actor(scene)) {
        return;
    }
    finish_transform_edit(scene, true);
    scene_history_clear(scene->history);
    /* Unrecorded teardown/load also removes actors destroyed during simulation. */
    if (!ecs_entity_is_alive(scene->world, scene->actors[scene->selected].entity)) {
        free_actor(&scene->actors[scene->selected]);
        int count = acount(scene->actors);
        memmove(
            &scene->actors[scene->selected],
            &scene->actors[scene->selected + 1],
            (size_t)(count - scene->selected - 1) * sizeof *scene->actors
        );
        asetlen(scene->actors, count - 1);
        scene->selected = count - 2;
        return;
    }
    remove_actor_at(scene, scene->selected);
}

static char *capture_actor_state(app_scene_authoring *scene, const scene_actor *actor)
{
    game_prototype *state = game_prototype_capture_entity(
        actor->name,
        scene->world,
        actor->entity,
        &scene->registry
    );
    if (!state) {
        return NULL;
    }
    free(state->identity);
    state->identity = xstrdup(actor->base->identity);
    int count = game_behavior_attachment_count_for_entity(
        scene->live->behaviors,
        actor->entity
    );
    for (int i = 0; i < count; i++) {
        game_behavior_attachment_info info;
        if (game_behavior_attachment_info_at(scene->live->behaviors, actor->entity, i, &info) !=
                0 ||
            game_prototype_add_graph(state, info.asset_name) != 0) {
            game_prototype_free(state);
            return NULL;
        }
    }
    char *json = game_prototype_save(state);
    game_prototype_free(state);
    return json;
}

static bool actor_name_used(const app_scene_authoring *scene, const char *name)
{
    for (int i = 0; i < acount(scene->actors); i++) {
        if (strcmp(scene->actors[i].name, name) == 0) {
            return true;
        }
    }
    return false;
}

int add_instance_at(app_scene_authoring *scene, const char *prototype_name, const GameTransform *position)
{
    if (!scene || !prototype_name || !find_prototype(scene, prototype_name)) {
        return -1;
    }
    finish_transform_edit(scene, false);
    if (scene->runtime_snapshot_valid &&
        app_scene_authoring_runtime_command(scene, APP_RUNTIME_SCENE, APP_RUNTIME_STOP) !=
            0) {
        return -1;
    }
    char name[256];
    int suffix = 1;
    do {
        if (snprintf(name, sizeof name, "%s_%d", prototype_name, suffix++) >=
            (int)sizeof name) {
            return -1;
        }
    } while (actor_name_used(scene, name));
    bool was_dirty = scene->scene_dirty;
    if (add_actor(scene, name, prototype_name) != 0) {
        return -1;
    }
    scene_actor *actor = &alast(scene->actors);
    if (position && GAME_GameTransform_has(scene->world, actor->entity)) {
        write_position(scene, actor->entity, *position);
    }
    char *state = capture_actor_state(scene, actor);
    char *base = game_prototype_save(actor->base);
    scene_actor_edit edit = { .index = acount(scene->actors) - 1,
                              .name = actor->name,
                              .prototype_name = actor->prototype_name,
                              .base_json = base,
                              .state_json = state,
                              .created = true };
    bool recorded = state && base &&
                    scene_history_push_actor(scene->history, actor->entity, &edit);
    free(state);
    free(base);
    if (!recorded) {
        remove_actor_at(scene, acount(scene->actors) - 1);
        scene->scene_dirty = was_dirty;
        return -1;
    }
    scene_authoring_select_document(scene, APP_EDITOR_DOCUMENT_SCENE, APP_SCENE_DOCUMENT_ID);
    return 0;
}

int app_scene_authoring_add_instance(app_scene_authoring *scene, const char *prototype_name)
{
    return add_instance_at(scene, prototype_name, NULL);
}

char **instance_behavior_names(app_scene_authoring *scene, ecs_entity entity)
{
    char **names = NULL;
    game_behavior_world *world = scene->live->behaviors;
    int count = game_behavior_attachment_count_for_entity(world, entity);
    for (int i = 0; i < count; i++) {
        game_behavior_attachment_info info;
        int result = game_behavior_attachment_info_at(world, entity, i, &info);
        assert(result == 0);
        apush(names, xstrdup(info.asset_name));
    }
    return names;
}

void free_behavior_names(char **names)
{
    for (int i = 0; i < acount(names); i++) {
        free(names[i]);
    }
    afree(names);
}

int app_scene_authoring_set_instance_behavior(app_scene_authoring *scene, const char *asset_name, bool attached)
{
    if (!scene || !asset_name || !selected_actor(scene)) {
        return -1;
    }
    finish_transform_edit(scene, false);
    if (scene->runtime_snapshot_valid &&
        app_scene_authoring_runtime_command(scene, APP_RUNTIME_SCENE, APP_RUNTIME_STOP) !=
            0) {
        return -1;
    }
    scene_actor *actor = selected_actor(scene);
    char **before = instance_behavior_names(scene, actor->entity);
    char **after = NULL;
    bool found = false;
    for (int i = 0; i < acount(before); i++) {
        bool match = strcmp(before[i], asset_name) == 0;
        found |= match;
        if (!match || attached) {
            apush(after, xstrdup(before[i]));
        }
    }
    if (attached && !found) {
        apush(after, xstrdup(asset_name));
    }
    int result = 0;
    if (found != attached) {
        result = game_behavior_set_attachments(
            scene->live->behaviors,
            actor->entity,
            (const char *const *)after,
            acount(after)
        );
        if (result == 0) {
            scene_behavior_edit edit = { .before = (const char *const *)before,
                                         .before_count = acount(before),
                                         .after = (const char *const *)after,
                                         .after_count = acount(after) };
            bool recorded = scene_history_push_behaviors(scene->history, actor->entity, &edit);
            assert(recorded);
            scene->scene_dirty = true;
            asset_index_invalidate(scene);
            scene_authoring_select_document(scene, APP_EDITOR_DOCUMENT_SCENE, APP_SCENE_DOCUMENT_ID);
            sync_editor_inspection(scene);
        }
    }
    if (result != 0) {
        snprintf(scene->message, sizeof scene->message, "Could not update behavior %s.", asset_name);
    }
    free_behavior_names(before);
    free_behavior_names(after);
    return result;
}

int app_scene_authoring_delete_instance(app_scene_authoring *scene)
{
    if (!scene || !selected_actor(scene)) {
        return -1;
    }
    finish_transform_edit(scene, false);
    if (scene->runtime_snapshot_valid &&
        app_scene_authoring_runtime_command(scene, APP_RUNTIME_SCENE, APP_RUNTIME_STOP) !=
            0) {
        return -1;
    }
    scene_actor *actor = selected_actor(scene);
    char *state = capture_actor_state(scene, actor);
    char *base = game_prototype_save(actor->base);
    if (!state || !base) {
        free(state);
        free(base);
        return -1;
    }
    char *name = xstrdup(actor->name), *prototype = xstrdup(actor->prototype_name);
    char *retained = scene_retention_save(actor->retained);
    ecs_entity entity = actor->entity;
    scene_actor_edit edit = { .index = scene->selected,
                              .name = name,
                              .prototype_name = prototype,
                              .base_json = base,
                              .state_json = state,
                              .retained_json = retained };
    int result = remove_actor_at(scene, scene->selected);
    if (!result) {
        bool recorded = scene_history_push_actor(scene->history, entity, &edit);
        assert(recorded);
        scene_authoring_select_document(scene, APP_EDITOR_DOCUMENT_SCENE, APP_SCENE_DOCUMENT_ID);
    }
    free(name);
    free(retained);
    free(prototype);
    free(state);
    free(base);
    return result;
}

static int restore_actor(app_scene_authoring *scene, const scene_actor_edit *edit, ecs_entity *entity)
{
    if (actor_name_used(scene, edit->name) || edit->index < 0 ||
        edit->index > acount(scene->actors)) {
        return -1;
    }
    char error[256];
    game_prototype *base = game_prototype_load(edit->base_json, &scene->registry, error, sizeof error);
    game_prototype *state = game_prototype_load(edit->state_json, &scene->registry, error, sizeof error);
    if (!base || !state) {
        game_prototype_free(base);
        game_prototype_free(state);
        return -1;
    }
    for (int i = 0; i < acount(state->graphs); i++) {
        if (!game_behavior_asset_registered(scene->live->behaviors, state->graphs[i])) {
            game_prototype_free(base);
            game_prototype_free(state);
            return -1;
        }
    }
    const game_prototype_spawn_hooks hooks = { attach_prototype_behavior, scene };
    if (game_prototype_spawn(state, scene->world, &hooks, entity, error, sizeof error) !=
        ECS_OK) {
        game_prototype_free(base);
        game_prototype_free(state);
        return -1;
    }
    scene_actor actor = { .entity = *entity,
                          .name = xstrdup(edit->name),
                          .prototype_name = xstrdup(edit->prototype_name),
                          .base = base,
                          .restored_state = state };
    if (scene_retention_load(edit->retained_json, &actor.retained) != 0) {
        game_behavior_destroy_entity(scene->live->behaviors, *entity);
        free_actor(&actor);
        return -1;
    }
    int count = acount(scene->actors);
    apush(scene->actors, actor);
    memmove(
        &scene->actors[edit->index + 1],
        &scene->actors[edit->index],
        (size_t)(count - edit->index) * sizeof *scene->actors
    );
    scene->actors[edit->index] = actor;
    select_instance(scene, edit->index);
    return 0;
}

int app_scene_authoring_duplicate_instance(app_scene_authoring *scene)
{
    if (!selected_actor(scene)) {
        return -1;
    }
    finish_transform_edit(scene, false);
    if (scene->runtime_snapshot_valid &&
        app_scene_authoring_runtime_command(scene, APP_RUNTIME_SCENE, APP_RUNTIME_STOP) !=
            0) {
        return -1;
    }
    scene_actor *actor = selected_actor(scene);
    char name[256];
    int suffix = 1;
    do {
        if (snprintf(name, sizeof name, "%s_%d", actor->prototype_name, suffix++) >=
            (int)sizeof name) {
            return -1;
        }
    } while (actor_name_used(scene, name));
    char *prototype = xstrdup(actor->prototype_name);
    char *state = capture_actor_state(scene, actor);
    char *base = game_prototype_save(actor->base);
    char *retained = scene_retention_save(actor->retained);
    scene_actor_edit edit = { .index = acount(scene->actors),
                              .name = name,
                              .prototype_name = prototype,
                              .base_json = base,
                              .state_json = state,
                              .retained_json = retained,
                              .created = true };
    ecs_entity entity;
    int result = state && base ? restore_actor(scene, &edit, &entity) : -1;
    if (!result) {
        bool recorded = scene_history_push_actor(scene->history, entity, &edit);
        assert(recorded);
        scene->scene_dirty = true;
        asset_index_invalidate(scene);
        scene_authoring_select_document(scene, APP_EDITOR_DOCUMENT_SCENE, APP_SCENE_DOCUMENT_ID);
    }
    free(prototype);
    free(state);
    free(base);
    free(retained);
    return result;
}

static int actor_index(const app_scene_authoring *scene, ecs_entity entity)
{
    for (int i = 0; i < acount(scene->actors); i++) {
        if (ecs_entity_to_u64(scene->actors[i].entity) == ecs_entity_to_u64(entity)) {
            return i;
        }
    }
    return -1;
}

static void set_actor_name(app_scene_authoring *scene, scene_actor *actor, const char *name)
{
    free(actor->name);
    actor->name = xstrdup(name);
    scene->scene_dirty = true;
    asset_index_invalidate(scene);
}

int app_scene_authoring_rename_instance(app_scene_authoring *scene, ecs_entity entity, const char *name)
{
    int index = actor_index(scene, entity);
    assert(index >= 0);
    if (!strcmp(scene->actors[index].name, name)) {
        return 0;
    }
    if (!name[0]) {
        snprintf(scene->message, sizeof scene->message, "An object needs a name.");
        return -1;
    }
    if (actor_name_used(scene, name)) {
        snprintf(scene->message, sizeof scene->message, "Another object is named %s.", name);
        return -1;
    }
    finish_transform_edit(scene, false);
    if (scene->runtime_snapshot_valid &&
        app_scene_authoring_runtime_command(scene, APP_RUNTIME_SCENE, APP_RUNTIME_STOP) !=
            0) {
        return -1;
    }
    /* Stopping respawns the objects, so the index finds it again. */
    scene_actor *actor = &scene->actors[index];
    bool recorded = scene_history_push_rename(
        scene->history,
        actor->entity,
        actor->name,
        name
    );
    assert(recorded);
    set_actor_name(scene, actor, name);
    scene_authoring_select_document(scene, APP_EDITOR_DOCUMENT_SCENE, APP_SCENE_DOCUMENT_ID);
    return 0;
}

static void apply_physics_settings(app_scene_authoring *scene, game_physics_settings settings)
{
    game_physics_reset(scene->live->physics);
    int result = game_physics_configure(scene->live->physics, settings);
    assert(result == 0);
    scene->scene_dirty = true;
    asset_index_invalidate(scene);
}

const char *app_scene_authoring_page(const app_scene_authoring *scene)
{
    return scene->ui ? scene->ui : scene->default_ui;
}

static void set_name(char **field, const char *name)
{
    free(*field);
    *field = name && name[0] ? xstrdup(name) : NULL;
}

void app_scene_authoring_set_ui(app_scene_authoring *scene, const char *ui)
{
    set_name(&scene->ui, ui);
    scene->scene_dirty = true;
}

void app_scene_authoring_set_default_ui(app_scene_authoring *scene, const char *ui)
{
    set_name(&scene->default_ui, ui);
}

int app_scene_authoring_set_physics(app_scene_authoring *scene, game_physics_settings settings)
{
    if (scene->runtime_snapshot_valid || scene->preview_playing ||
        game_prototype_preview_entity(scene->prototype_preview).slot ||
        !game_physics_settings_valid(settings)) {
        return -1;
    }
    finish_transform_edit(scene, false);
    game_physics_settings previous = game_physics_configuration(scene->live->physics);
    if (!scene_history_push_physics(scene->history, previous, settings)) {
        return 0;
    }
    apply_physics_settings(scene, settings);
    return 0;
}

static int replay_scene_edit_one(app_scene_authoring *scene, bool redo)
{
    if (!scene_can_move(scene)) {
        return -1;
    }
    finish_transform_edit(scene, false);
    const game_physics_settings *physics = scene_history_peek_physics(scene->history, redo);
    if (physics) {
        if (game_prototype_preview_entity(scene->prototype_preview).slot) {
            return -1;
        }
        apply_physics_settings(scene, *physics);
        scene_history_complete_physics(scene->history, redo);
        return 0;
    }
    ecs_entity entity;
    const char *name = scene_history_peek_rename(scene->history, redo, &entity);
    if (name) {
        int index = actor_index(scene, entity);
        if (index < 0 || actor_name_used(scene, name)) {
            return -1;
        }
        set_actor_name(scene, &scene->actors[index], name);
        scene_history_complete_rename(scene->history, redo);
        select_instance(scene, index);
        return 0;
    }
    bool present;
    const scene_actor_edit
        *actor_edit = scene_history_peek_actor(scene->history, redo, &entity, &present);
    if (actor_edit) {
        ecs_entity replacement = { 0 };
        int result = -1;
        if (present) {
            if (!ecs_entity_is_alive(scene->world, entity)) {
                result = restore_actor(scene, actor_edit, &replacement);
            }
        } else {
            for (int i = 0; i < acount(scene->actors); i++) {
                if (ecs_entity_to_u64(scene->actors[i].entity) ==
                    ecs_entity_to_u64(entity)) {
                    result = remove_actor_at(scene, i);
                    break;
                }
            }
        }
        if (result != 0) {
            return -1;
        }
        scene_history_complete_actor(scene->history, redo, replacement);
        scene->scene_dirty = true;
        asset_index_invalidate(scene);
        return 0;
    }
    const scene_behavior_edit
        *behavior_edit = scene_history_peek_behaviors(scene->history, redo, &entity);
    if (behavior_edit) {
        if (scene->runtime_snapshot_valid) {
            snprintf(scene->message, sizeof scene->message, "Stop the scene before changing behavior attachments.");
            return -1;
        }
        const char *const *names = redo ? behavior_edit->after : behavior_edit->before;
        int count = redo ? behavior_edit->after_count : behavior_edit->before_count;
        if (game_behavior_set_attachments(scene->live->behaviors, entity, names, count) !=
            0) {
            return -1;
        }
        scene_history_complete_behaviors(scene->history, redo);
        scene->scene_dirty = true;
        asset_index_invalidate(scene);
        for (int i = 0; i < acount(scene->actors); i++) {
            if (ecs_entity_to_u64(scene->actors[i].entity) == ecs_entity_to_u64(entity)) {
                select_instance(scene, i);
                break;
            }
        }
        sync_editor_inspection(scene);
        return 0;
    }
    int result = redo ? scene_history_redo(scene->history, scene->world, &entity)
                      : scene_history_undo(scene->history, scene->world, &entity);
    if (result > 0) {
        scene->scene_dirty = true;
        asset_index_invalidate(scene);
        for (int i = 0; i < acount(scene->actors); i++) {
            if (ecs_entity_to_u64(scene->actors[i].entity) == ecs_entity_to_u64(entity)) {
                select_instance(scene, i);
                break;
            }
        }
    }
    return result < 0 ? -1 : 0;
}

int replay_scene_edit(app_scene_authoring *scene, bool redo)
{
    uint64_t group = scene_history_group_id(scene->history, redo);
    int applied = 0;
    do {
        if (replay_scene_edit_one(scene, redo) != 0) {
            while (applied-- > 0) {
                int restored = replay_scene_edit_one(scene, !redo);
                assert(restored == 0);
            }
            return -1;
        }
        applied++;
    } while (group && scene_history_group_id(scene->history, redo) == group);
    return 0;
}
