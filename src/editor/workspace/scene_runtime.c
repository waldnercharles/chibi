#include "editor/workspace/scene_internal.h"
#include "render/shader_catalog.h"
#include "runtime/generated.h"

#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

void invalidate_runtime_snapshot(app_scene_authoring *scene)
{
    scene->runtime_selected = (ecs_entity){ 0 };
    for (int i = 0; i < acount(scene->actors); i++) {
        game_prototype_free(scene->actors[i].runtime_snapshot);
        scene->actors[i].runtime_snapshot = NULL;
    }
    scene->runtime_snapshot_valid = false;
}

static int capture_runtime_snapshot(app_scene_authoring *scene)
{
    if (scene->runtime_snapshot_valid) {
        return 0;
    }
    game_prototype **snapshots = NULL;
    for (int i = 0; i < acount(scene->actors); i++) {
        scene_actor *actor = &scene->actors[i];
        game_prototype *captured = game_prototype_capture_entity(
            actor->prototype_name,
            scene->world,
            actor->entity,
            &scene->registry
        );
        if (!captured) {
            goto fail;
        }
        /* Re-clone with the authored identity so prototype tags stay stable. */
        free(captured->identity);
        captured->identity = xstrdup(actor->base->identity);
        char error[256];
        game_prototype *snapshot = game_prototype_clone(captured, error, sizeof error);
        game_prototype_free(captured);
        if (!snapshot) {
            goto fail;
        }
        apush(snapshots, snapshot);
        int count = game_behavior_attachment_count_for_entity(
            scene->live->behaviors,
            actor->entity
        );
        for (int j = 0; j < count; j++) {
            game_behavior_attachment_info info;
            if (game_behavior_attachment_info_at(scene->live->behaviors, actor->entity, j, &info) !=
                    0 ||
                game_prototype_add_graph(snapshot, info.asset_name) != 0) {
                goto fail;
            }
        }
    }
    for (int i = 0; i < acount(snapshots); i++) {
        scene->actors[i].runtime_snapshot = snapshots[i];
    }
    afree(snapshots);
    scene->runtime_snapshot_valid = true;
    return 0;
fail:
    free_behavior_snapshots(snapshots);
    snprintf(scene->message, sizeof scene->message, "Could not capture authored Scene state.");
    return -1;
}

static int restart_runtime_actor(app_scene_authoring *scene, scene_actor *actor)
{
    assert(actor->runtime_snapshot);
    const game_prototype_spawn_hooks hooks = { attach_prototype_behavior, scene };
    char error[256];
    ecs_entity entity = { 0 };
    if (game_prototype_spawn(actor->runtime_snapshot, scene->world, &hooks, &entity, error, sizeof error) !=
        ECS_OK) {
        return -1;
    }
    if (ecs_entity_is_alive(scene->world, actor->entity)) {
        game_behavior_destroy_entity(scene->live->behaviors, actor->entity);
    }
    actor->entity = entity;
    return 0;
}

static int restart_runtime(app_scene_authoring *scene)
{
    engine_reset(scene->runtime);
    render_shader_clear_world(scene->world);
    finish_transform_edit(scene, false);
    if (capture_runtime_snapshot(scene) != 0) {
        return -1;
    }
    scene_history_clear(scene->history);
    scene->debug_sessions[APP_RUNTIME_SCENE]++;
    editor_set_inspection_blocked(scene->graph_editor, true);
    game_behavior_set_deferred(scene->live->behaviors, true);
    scene->runtime_selected = (ecs_entity){ 0 };
    game_entities_reset(scene->live->entities);
    game_particles_clear(scene->live->particles);
    for (int i = 0; i < acount(scene->actors); i++) {
        if (restart_runtime_actor(scene, &scene->actors[i]) != 0) {
            snprintf(scene->message, sizeof scene->message, "Could not restart Scene Runtime.");
            return -1;
        }
    }
    if (game_entities_rebuild_global_bindings(scene->live->entities) != 0) {
        snprintf(scene->message, sizeof scene->message, "Global binding names must be unique.");
        return -1;
    }
    if (scene->selected >= 0 && scene->selected < acount(scene->actors)) {
        select_instance(scene, scene->selected);
    }
    return 0;
}

bool runtime_session(const app_scene_authoring *scene)
{
    return scene->runtime_snapshot_valid || scene->preview_prototype_id;
}

int app_scene_authoring_set_runtime_target(app_scene_authoring *scene, app_runtime_target target)
{
    if (!scene || target < APP_RUNTIME_PREVIEW || target > APP_RUNTIME_SCENE) {
        return -1;
    }
    if (runtime_session(scene)) {
        snprintf(scene->message, sizeof scene->message, "Stop simulation to change target.");
        return -1;
    }
    if (target == APP_RUNTIME_PREVIEW) {
        scene_prototype_asset *asset = selected_prototype(scene);
        if (!asset) {
            return -1;
        }
        scene->runtime_preview_prototype = (int)(asset - scene->prototypes);
    }
    scene->active_runtime_target = target;
    sync_editor_inspection(scene);
    return 0;
}

const ecs_world *app_scene_authoring_preview_world(const app_scene_authoring *scene)
{
    return game_prototype_preview_world(scene->prototype_preview);
}

int app_scene_authoring_preview_prototype(app_scene_authoring *scene, const char *prototype_name)
{
    if (!scene) {
        return -1;
    }
    scene_prototype_asset *asset = find_prototype(scene, prototype_name);
    if (!asset || !asset->prototype) {
        return -1;
    }
    if (scene->runtime_snapshot_valid) {
        snprintf(scene->prototype_message, sizeof scene->prototype_message, "Stop the scene before previewing a prototype.");
        return -1;
    }
    bool continuing = previewed_prototype(scene) == asset;
    bool loop = continuing ? scene->preview_loop
                           : prototype_has_component_id(
                                 asset->prototype,
                                 GAME_COMPONENT_GameProjectile.id
                             );
    bool follow = continuing ? scene->preview_follow : loop;
    char error[256];
    finish_transform_edit(scene, true);
    app_scene_authoring_stop_prototype_preview(scene);
    int configured = game_prototype_preview_configure_physics(
        scene->prototype_preview,
        game_physics_configuration(scene->live->physics)
    );
    assert(configured == 0);
    for (int i = 0; i < game_live_definition_count(scene->live); i++) {
        game_live_definition *definition = game_live_definition_at(scene->live, i);
        if (game_prototype_preview_update_behavior(
                scene->prototype_preview,
                definition->name,
                definition->applied,
                true,
                error,
                sizeof error
            ) != 0) {
            snprintf(scene->prototype_message, sizeof scene->prototype_message, "%s", error);
            return -1;
        }
    }
    int result = game_prototype_preview_launch(scene->prototype_preview, asset->prototype, error, sizeof error);
    if (result == ECS_OK) {
        scene->active_runtime_target = APP_RUNTIME_PREVIEW;
        scene->preview_prototype_id = xstrdup(game_prototype_identity(asset->prototype));
        scene->runtime_preview_prototype = (int)(asset - scene->prototypes);
        const GameTransform *origin = GAME_GameTransform_get_const(
            game_prototype_preview_world(scene->prototype_preview),
            game_prototype_preview_entity(scene->prototype_preview)
        );
        scene->preview_origin = origin ? *origin : (GameTransform){ 0 };
    }
    scene->preview_playing = result == ECS_OK;
    scene->preview_loop = loop;
    scene->preview_follow = follow;
    scene->preview_step = false;
    snprintf(
        scene->prototype_message,
        sizeof scene->prototype_message,
        result == ECS_OK ? "Previewing %s." : "Could not preview %s: %s.",
        prototype_name,
        result == ECS_OK ? "" : error
    );
    return result;
}

void app_scene_authoring_stop_prototype_preview(app_scene_authoring *scene)
{
    if (scene) {
        if (scene->preview_prototype_id) {
            scene->debug_sessions[APP_RUNTIME_PREVIEW]++;
            editor_set_inspection_trace(scene->graph_editor, NULL, 0);
            editor_set_inspection_run(scene->graph_editor, NULL);
            editor_set_inspection_instance(scene->graph_editor, NULL);
        }
        game_prototype_preview_stop(scene->prototype_preview);
        free(scene->preview_prototype_id);
        scene->preview_prototype_id = NULL;
        scene->preview_playing = false;
        scene->preview_step = false;
        scene->active_runtime_target = APP_RUNTIME_SCENE;
        snprintf(scene->prototype_message, sizeof scene->prototype_message, "Prototype preview stopped.");
    }
}

int app_scene_authoring_runtime_command(
    app_scene_authoring *scene,
    app_runtime_target target,
    app_runtime_command command
)
{
    if (!scene || target < APP_RUNTIME_PREVIEW || target > APP_RUNTIME_SCENE ||
        command < APP_RUNTIME_PLAY || command > APP_RUNTIME_STOP) {
        return -1;
    }
    if (target != scene->active_runtime_target && runtime_session(scene)) {
        snprintf(scene->message, sizeof scene->message, "Stop simulation to change target.");
        return -1;
    }
    if (target == APP_RUNTIME_PREVIEW && !scene->preview_prototype_id &&
        command != APP_RUNTIME_STOP) {
        scene_prototype_asset *asset = prototype_at(scene, scene->runtime_preview_prototype);
        if (!asset ||
            app_scene_authoring_preview_prototype(scene, asset->prototype->name) != 0) {
            return -1;
        }
    }
    if (command == APP_RUNTIME_PAUSE || command == APP_RUNTIME_STOP ||
        command == APP_RUNTIME_RESTART) {
        app_scene_authoring_move(scene, 0, 0);
        app_scene_authoring_aim(scene, (CF_V2){ 0 }, false);
    }
    if (command == APP_RUNTIME_RESTART || command == APP_RUNTIME_STOP) {
        app_scene_renderer_reset(scene->renderer);
    }
    bool *playing = target == APP_RUNTIME_PREVIEW ? &scene->preview_playing
                                                  : &scene->runtime_playing;
    finish_transform_edit(scene, false);
    bool *step = target == APP_RUNTIME_PREVIEW ? &scene->preview_step
                                               : &scene->runtime_step;
    if (target == APP_RUNTIME_SCENE) {
        scene->active_runtime_target = APP_RUNTIME_SCENE;
        if (command == APP_RUNTIME_PLAY || command == APP_RUNTIME_STEP) {
            /* The game generates its own world on its first step. */
            if (!scene->runtime_snapshot_valid) {
                scene_world_preview_discard(scene);
            }
            if (capture_runtime_snapshot(scene) != 0) {
                return -1;
            }
            game_behavior_set_deferred(scene->live->behaviors, false);
            if (game_behavior_begin_pending(scene->live->behaviors) != 0) {
                *playing = false;
                *step = false;
                engine_set_playing(scene->runtime, false);
                snprintf(scene->message, sizeof scene->message, "Scene Begin failed. Stop to return to editing.");
                return -1;
            }
        }
        if (command == APP_RUNTIME_PLAY || command == APP_RUNTIME_PAUSE) {
            *playing = command == APP_RUNTIME_PLAY;
            *step = false;
            return engine_set_playing(scene->runtime, *playing);
        }
        if (command == APP_RUNTIME_STEP) {
            *playing = false;
            *step = true;
            return engine_set_playing(scene->runtime, false) == 0
                     ? engine_step(scene->runtime)
                     : -1;
        }
        *playing = false;
        *step = false;
        if (engine_set_playing(scene->runtime, false) != 0) {
            return -1;
        }
        int result = restart_runtime(scene);
        if (result != 0) {
            return result;
        }
        if (command == APP_RUNTIME_STOP) {
            invalidate_runtime_snapshot(scene);
            return 0;
        }
        game_behavior_set_deferred(scene->live->behaviors, false);
        if (game_behavior_begin_pending(scene->live->behaviors) != 0) {
            snprintf(scene->message, sizeof scene->message, "Scene Begin failed. Stop to return to editing.");
            return -1;
        }
        *playing = true;
        return engine_set_playing(scene->runtime, true);
    }
    if (command == APP_RUNTIME_PLAY || command == APP_RUNTIME_PAUSE) {
        scene->active_runtime_target = APP_RUNTIME_PREVIEW;
        *playing = command == APP_RUNTIME_PLAY;
        *step = false;
        return engine_set_playing(game_prototype_preview_runtime(scene->prototype_preview), *playing);
    }
    if (command == APP_RUNTIME_STEP) {
        scene->active_runtime_target = APP_RUNTIME_PREVIEW;
        *playing = false;
        *step = true;
        engine *preview = game_prototype_preview_runtime(scene->prototype_preview);
        engine_set_playing(preview, false);
        return engine_step(preview);
    }
    if (command == APP_RUNTIME_STOP) {
        app_scene_authoring_stop_prototype_preview(scene);
        return 0;
    }
    scene_prototype_asset *asset = previewed_prototype(scene);
    return asset ? app_scene_authoring_preview_prototype(scene, asset->prototype->name)
                 : -1;
}

const engine_frame_timing *app_scene_authoring_runtime_timing(const app_scene_authoring *scene)
{
    engine *runtime = scene->active_runtime_target == APP_RUNTIME_PREVIEW
                        ? game_prototype_preview_runtime(scene->prototype_preview)
                        : scene->runtime;
    return engine_last_timing(runtime);
}

app_runtime_state app_scene_authoring_runtime_state(const app_scene_authoring *scene)
{
    app_runtime_state state = { 0 };
    if (!scene) {
        return state;
    }
    state.target = scene->active_runtime_target;
    state.playing = state.target == APP_RUNTIME_PREVIEW ? scene->preview_playing
                                                        : scene->runtime_playing;
    ecs_world *world = NULL;
    game_behavior_world *behaviors = NULL;
    if (state.target == APP_RUNTIME_PREVIEW) {
        world = game_prototype_preview_world(scene->prototype_preview);
        behaviors = game_prototype_preview_behaviors(scene->prototype_preview);
        state.selected_entity = game_prototype_preview_entity(scene->prototype_preview);
        state.message = scene->prototype_message;
    } else {
        world = scene->world;
        behaviors = scene->live ? scene->live->behaviors : NULL;
        state.selected_entity = selected_scene_entity(scene);
        state.message = scene->message;
    }
    if (world && state.selected_entity.slot) {
        for (int i = 0; i < ecs_world_component_count(world); i++) {
            const ecs_component_desc *desc = ecs_world_component_at(world, i);
            if (desc && ecs_component_has(world, state.selected_entity, desc->id)) {
                state.component_count++;
            }
        }
    }
    if (behaviors && state.selected_entity.slot) {
        state.behavior_count = game_behavior_attachment_count_for_entity(
            behaviors,
            state.selected_entity
        );
    }
    return state;
}

int app_scene_authoring_frame(app_scene_authoring *scene, double monotonic_time)
{
    if (!scene) {
        return FLOW_ERR_ARGUMENT;
    }
    preview_behavior_edits(scene);
    int result = FLOW_OK;
    ecs_entity preview_entity = game_prototype_preview_entity(scene->prototype_preview);
    engine *preview_runtime = game_prototype_preview_runtime(scene->prototype_preview);
    engine_set_focus(preview_runtime, engine_get_focus(scene->runtime));
    if (preview_entity.slot) {
        result = game_prototype_preview_tick(scene->prototype_preview, monotonic_time);
        if (result < 0) {
            snprintf(scene->prototype_message, sizeof scene->prototype_message, "%s", engine_error(preview_runtime));
        }
    }
    if (scene->preview_loop && scene->preview_playing && result >= 0 &&
        (engine_last_timing(preview_runtime)->simulation_time >= 2.0 ||
         !ecs_entity_is_alive(game_prototype_preview_world(scene->prototype_preview), preview_entity))) {
        scene_prototype_asset *asset = previewed_prototype(scene);
        if (asset) {
            result = app_scene_authoring_preview_prototype(
                scene,
                asset->prototype->name
            );
        }
    }
    int frame_result = engine_frame(scene->runtime, monotonic_time);
    if (frame_result < 0) {
        snprintf(scene->message, sizeof scene->message, "%s", engine_error(scene->runtime));
    }
    app_scene_renderer_set_time(
        scene->renderer,
        (float)engine_last_timing(
            scene->active_runtime_target == APP_RUNTIME_PREVIEW ? preview_runtime
                                                                : scene->runtime
        )
            ->simulation_time
    );
    if (frame_result < 0 && result == FLOW_OK) {
        result = frame_result;
    }
    scene->runtime_step = false;
    scene->preview_step = false;
    return result;
}

int app_scene_authoring_input(app_scene_authoring *scene, const char *action, double value, int pressed)
{
    if (!scene) {
        return FLOW_ERR_ARGUMENT;
    }
    if (scene->active_runtime_target == APP_RUNTIME_PREVIEW) {
        engine_set_focus(
            game_prototype_preview_runtime(scene->prototype_preview),
            engine_get_focus(scene->runtime)
        );
        return game_prototype_preview_input(scene->prototype_preview, action, value, pressed, NULL);
    }
    return engine_dispatch_input(scene->runtime, action, value, pressed, NULL);
}

int app_scene_authoring_move(app_scene_authoring *scene, float x, float y)
{
    if (!scene || !isfinite(x) || !isfinite(y)) {
        return FLOW_ERR_ARGUMENT;
    }
    bool preview = scene->active_runtime_target == APP_RUNTIME_PREVIEW;
    if (engine_get_focus(scene->runtime) != ENGINE_FOCUS_VIEWPORT ||
        !(preview ? scene->preview_playing : scene->runtime_playing)) {
        return FLOW_ERR_STATE;
    }
    if (preview) {
        engine_set_focus(
            game_prototype_preview_runtime(scene->prototype_preview),
            engine_get_focus(scene->runtime)
        );
        return game_prototype_preview_sample_movement(scene->prototype_preview, x, y);
    }
    return engine_sample_movement(scene->runtime, x, y);
}

static int sample_aim(app_scene_authoring *scene, CF_V2 value, bool firing, bool point)
{
    engine *runtime = scene->active_runtime_target == APP_RUNTIME_PREVIEW
                        ? game_prototype_preview_runtime(scene->prototype_preview)
                        : scene->runtime;
    engine_set_focus(runtime, engine_get_focus(scene->runtime));
    return engine_sample_aim(runtime, value, firing, point);
}

int app_scene_authoring_aim(app_scene_authoring *scene, CF_V2 direction, bool firing)
{
    return sample_aim(scene, direction, firing, false);
}

int app_scene_authoring_aim_at(app_scene_authoring *scene, CF_V2 point, bool firing)
{
    return sample_aim(scene, point, firing, true);
}
