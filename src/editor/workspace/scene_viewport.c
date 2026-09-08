#include "../src/platform/cute.h"
#include "editor/ui/ui_shell.h"
#include "editor/workspace/scene_internal.h"
#include "render/appearance_geometry.h"
#include "render/sdf_motion.h"
#include "render/shader_catalog.h"
#include "runtime/generated.h"

#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static float actor_orientation(const ecs_world *world, ecs_entity entity)
{
    const GameOrientation *orientation = GAME_GameOrientation_get_const(world, entity);
    return orientation ? orientation->radians : 0;
}

static bool appearance_bounds(
    const GameTransform *transform,
    const GameAppearance *appearance,
    const GameShader *shader,
    float radians,
    const GameSdfMotion *motion,
    bool effects,
    CF_Aabb *out
)
{
    CF_V2 min, max;
    GameAppearance bounded = *appearance;
    float extent[2];
    if (shader && render_shader_bounds(render_shader_find(shader->shape), extent)) {
        bounded.shape = GAME_SHAPE_BOX;
        bounded.width = 2 * (shader->bounds_x > 0 ? shader->bounds_x : extent[0]);
        bounded.height = 2 * (shader->bounds_y > 0 ? shader->bounds_y : extent[1]);
        appearance = &bounded;
    }
    if (!isfinite(transform->x) || !isfinite(transform->y) ||
        !game_appearance_rotated_bounds(appearance, radians, &min, &max)) {
        return false;
    }
    float padding = 0;
    if (motion && !game_sdf_motion_padding(motion, &padding)) {
        return false;
    }
    if (effects) {
        if (isfinite(appearance->glow_radius) && appearance->glow_radius > 0) {
            padding += appearance->glow_radius;
        }
    }
    *out = (CF_Aabb){
        { transform->x + min.x - padding, -transform->y + min.y - padding },
        { transform->x + max.x + padding, -transform->y + max.y + padding }
    };
    return isfinite(out->min.x) && isfinite(out->min.y) &&
           isfinite(out->max.x) && isfinite(out->max.y);
}

void fit_scene_view(engine_viewport *viewport, ecs_world *world, ecs_entity only)
{
    ecs_component_id required[] = { GAME_COMPONENT_GameTransform.id,
                                    GAME_COMPONENT_GameAppearance.id };
    ecs_query query;
    if (ecs_query_begin(world, &query, required, 2) != ECS_OK) {
        return;
    }
    CF_Aabb total = { 0 };
    bool found = false;
    ecs_entity entity;
    while (ecs_query_next(&query, &entity)) {
        if (only.slot &&
            (entity.slot != only.slot || entity.generation != only.generation)) {
            continue;
        }
        CF_Aabb bounds;
        if (!appearance_bounds(
                GAME_GameTransform_get_const(world, entity),
                GAME_GameAppearance_get_const(world, entity),
                GAME_GameShader_get_const(world, entity),
                actor_orientation(world, entity),
                GAME_GameSdfMotion_get_const(world, entity),
                true,
                &bounds
            )) {
            continue;
        }
        if (!found) {
            total = bounds;
        }
        total.min.x = fminf(total.min.x, bounds.min.x);
        total.min.y = fminf(total.min.y, bounds.min.y);
        total.max.x = fmaxf(total.max.x, bounds.max.x);
        total.max.y = fmaxf(total.max.y, bounds.max.y);
        found = true;
    }
    ecs_query_end(&query);
    if (found) {
        engine_viewport_fit(
            viewport,
            total.min.x,
            total.min.y,
            total.max.x,
            total.max.y
        );
    }
}

static ecs_entity pick_scene_entity(app_scene_authoring *scene, float x, float y)
{
    ecs_component_id required[] = { GAME_COMPONENT_GameTransform.id,
                                    GAME_COMPONENT_GameAppearance.id };
    ecs_query query;
    if (ecs_query_begin(scene->world, &query, required, 2) != ECS_OK) {
        return (ecs_entity){ 0 };
    }
    ecs_entity picked = { 0 };
    ecs_entity entity;
    while (ecs_query_next(&query, &entity)) {
        const GameAppearance *appearance = GAME_GameAppearance_get_const(scene->world, entity);
        const GameTransform *transform = GAME_GameTransform_get_const(scene->world, entity);
        CF_Aabb bounds;
        float radians = actor_orientation(scene->world, entity);
        const GameSdfMotion *motion = GAME_GameSdfMotion_get_const(scene->world, entity);
        CF_V2 local;
        const GameShader *shader = GAME_GameShader_get_const(scene->world, entity);
        bool custom_shape = shader &&
                            render_shader_is_shape(render_shader_find(shader->shape));
        GameAppearance selectable = *appearance;
        if (selectable.shape == GAME_SHAPE_STROKED_CHEVRON) {
            selectable.shape = GAME_SHAPE_CHEVRON;
        }
        if (!appearance_bounds(transform, appearance, shader, radians, motion, false, &bounds) ||
            !game_appearance_rotate_point(
                (CF_V2){ x - transform->x, y + transform->y },
                -radians,
                &local
            ) ||
            (motion && !game_sdf_motion_warp(motion, local, &local)) ||
            !(custom_shape ? render_shader_contains(
                                 shader,
                                 scene->world,
                                 entity,
                                 (float[2]){ local.x, local.y },
                                 (float[2]){ x, y },
                                 app_scene_renderer_time(scene->renderer)
                             )
                           : game_appearance_contains(&selectable, local))) {
            continue;
        }
        /* The renderer uses this same ECS query order; the last drawn hit
         * wins, regardless of whether it was authored or spawned at runtime. */
        if (game_entities_owns(scene->live->entities, entity)) {
            picked = entity;
            continue;
        }
        for (int i = 0; i < acount(scene->actors); i++) {
            if (ecs_entity_to_u64(scene->actors[i].entity) == ecs_entity_to_u64(entity)) {
                picked = entity;
                break;
            }
        }
    }
    ecs_query_end(&query);
    return picked;
}

static void edit_scene_pointer(app_scene_authoring *scene, engine_viewport *viewport)
{
    engine_viewport_pointer pointer;
    engine_viewport_get_pointer(viewport, &pointer);
    if (pointer.world_per_pixel > 0) {
        scene->selection_scale = pointer.world_per_pixel;
    }
    if (pointer.delete_pressed && scene_can_move(scene) &&
        !scene->runtime_snapshot_valid) {
        finish_transform_edit(scene, false);
        ecs_entity *remove = NULL;
        for (int i = 0; i < acount(scene->actors); i++) {
            if (scene->actors[i].marked || i == scene->selected) {
                apush(remove, scene->actors[i].entity);
            }
        }
        scene_history_begin_group(scene->history);
        for (int j = acount(remove) - 1; j >= 0; j--) {
            for (int i = 0; i < acount(scene->actors); i++) {
                if (ecs_entity_to_u64(scene->actors[i].entity) ==
                    ecs_entity_to_u64(remove[j])) {
                    scene->selected = i;
                    app_scene_authoring_delete_instance(scene);
                    break;
                }
            }
        }
        scene_history_end_group(scene->history);
        afree(remove);
        scene->selected = -1;
        scene->runtime_selected = (ecs_entity){ 0 };
        scene->selected_attachment = -1;
        for (int i = 0; i < acount(scene->actors); i++) {
            scene->actors[i].marked = false;
        }
        sync_editor_inspection(scene);
        return;
    }
    if (pointer.dropped_prototype[0] && scene_can_move(scene) &&
        !scene->runtime_snapshot_valid) {
        GameTransform position = { pointer.world_x, -pointer.world_y };
        if (add_instance_at(scene, pointer.dropped_prototype, &position) == 0) {
            scene->reveal_actor = (ecs_entity){ 0 };
        }
        return;
    }
    if (!scene_can_move(scene) || pointer.cancel) {
        scene->marquee = scene->marquee_visible = false;
        if (scene->dragging_actor) {
            finish_transform_edit(scene, true);
        }
        return;
    }
    if (pointer.pressed) {
        finish_transform_edit(scene, false);
        ecs_entity picked = pick_scene_entity(scene, pointer.world_x, pointer.world_y);
        scene_authoring_select_document(scene, APP_EDITOR_DOCUMENT_SCENE, APP_SCENE_DOCUMENT_ID);
        if (!picked.slot) {
            scene->selected = -1;
            scene->runtime_selected = (ecs_entity){ 0 };
            scene->selected_attachment = -1;
            for (int i = 0; i < acount(scene->actors); i++) {
                scene->actors[i].marked = false;
            }
            scene->marquee = true;
            scene->marquee_start = (GameTransform){ pointer.world_x, pointer.world_y };
            scene->marquee_end = scene->marquee_start;
            return;
        }
        if (game_entities_owns(scene->live->entities, picked)) {
            app_scene_authoring_select_runtime_entity(scene, picked);
            return;
        }
        for (int i = 0; i < acount(scene->actors); i++) {
            if (ecs_entity_to_u64(scene->actors[i].entity) == ecs_entity_to_u64(picked)) {
                select_instance(scene, i);
                break;
            }
        }
        begin_transform_edit(scene, picked);
        scene->dragging_actor = true;
        scene->drag_origin = (GameTransform){ pointer.world_x, -pointer.world_y };
    }
    if (scene->marquee) {
        scene->marquee_end = (GameTransform){ pointer.world_x, pointer.world_y };
        scene->marquee_visible = pointer.dragging;
        if (pointer.dragging) {
            float min_x = fminf(scene->marquee_start.x, pointer.world_x);
            float max_x = fmaxf(scene->marquee_start.x, pointer.world_x);
            float min_y = fminf(scene->marquee_start.y, pointer.world_y);
            float max_y = fmaxf(scene->marquee_start.y, pointer.world_y);
            scene->selected = -1;
            for (int i = 0; i < acount(scene->actors); i++) {
                scene_actor *actor = &scene->actors[i];
                CF_Aabb bounds;
                actor->marked =
                    GAME_GameTransform_has(scene->world, actor->entity) &&
                    GAME_GameAppearance_has(scene->world, actor->entity) &&
                    appearance_bounds(
                        GAME_GameTransform_get_const(scene->world, actor->entity),
                        GAME_GameAppearance_get_const(scene->world, actor->entity),
                        GAME_GameShader_get_const(scene->world, actor->entity),
                        actor_orientation(scene->world, actor->entity),
                        GAME_GameSdfMotion_get_const(scene->world, actor->entity),
                        false,
                        &bounds
                    ) &&
                    bounds.max.x >= min_x && bounds.min.x <= max_x &&
                    bounds.max.y >= min_y && bounds.min.y <= max_y;
                if (actor->marked) {
                    scene->selected = i;
                }
            }
        }
        if (pointer.released) {
            scene->marquee = scene->marquee_visible = false;
        }
        return;
    }
    if (!scene->dragging_actor) {
        return;
    }
    if (pointer.dragging) {
        GameTransform position = {
            scene->edit_before.x + pointer.world_x - scene->drag_origin.x,
            scene->edit_before.y - pointer.world_y - scene->drag_origin.y,
        };
        write_position(scene, scene->edit_actor, position);
        scene->scene_dirty = true;
    }
    if (pointer.released) {
        finish_transform_edit(scene, false);
    }
}

int app_scene_authoring_draw_viewport(app_scene_authoring *scene, engine_viewport *viewport)
{
    scene->render_viewport = viewport;
    app_runtime_target target = scene->active_runtime_target;
    scene_prototype_asset *prototype = previewed_prototype(scene);
    bool preview = target == APP_RUNTIME_PREVIEW && prototype;
    /* Prototype records are append-only; indices survive array reallocations. */
    int camera_target = preview ? (int)(prototype - scene->prototypes) + 1 : 0;
    scene_camera *camera = preview ? &prototype->camera : &scene->camera;
    if (camera_target != scene->camera_target) {
        if (camera->saved) {
            engine_viewport_set_view(viewport, &camera->view);
        } else {
            engine_viewport_reset_camera(viewport);
            fit_scene_view(
                viewport,
                preview ? game_prototype_preview_world(scene->prototype_preview)
                        : scene->world,
                (ecs_entity){ 0 }
            );
        }
        app_scene_renderer_reset(scene->renderer);
        scene->camera_target = camera_target;
    }
    if (scene->reveal_actor.slot) {
        fit_scene_view(viewport, scene->world, scene->reveal_actor);
        scene->reveal_actor = (ecs_entity){ 0 };
    }
    if (preview && scene->preview_follow &&
        (scene->preview_playing || scene->preview_step)) {
        ecs_world *world = game_prototype_preview_world(scene->prototype_preview);
        const GameTransform *position = GAME_GameTransform_get_const(
            world,
            game_prototype_preview_entity(scene->prototype_preview)
        );
        if (position) {
            engine_viewport_view view;
            engine_viewport_get_view(viewport, &view);
            view.center_x = position->x;
            view.center_y = -position->y;
            engine_viewport_set_view(viewport, &view);
        }
    }
    engine_viewport_controls controls = {
        .session = target == APP_RUNTIME_PREVIEW ? prototype != NULL
                                                 : scene->runtime_snapshot_valid,
        .editing = scene_can_move(scene),
        .playing = target == APP_RUNTIME_PREVIEW ? scene->preview_playing
                                                 : scene->runtime_playing,
    };
    app_runtime_command command;
    engine_viewport_action action = engine_viewport_draw(viewport, &controls);
    edit_scene_pointer(scene, viewport);
    if (action == ENGINE_VIEWPORT_FIT) {
        fit_scene_view(
            viewport,
            preview ? game_prototype_preview_world(scene->prototype_preview)
                    : scene->world,
            (ecs_entity){ 0 }
        );
    }
    engine_viewport_get_view(viewport, &camera->view);
    camera->saved = true;
    switch (action) {
        case ENGINE_VIEWPORT_NONE: return 0;
        case ENGINE_VIEWPORT_FIT: return 0;
        case ENGINE_VIEWPORT_PLAY: command = APP_RUNTIME_PLAY; break;
        case ENGINE_VIEWPORT_PAUSE: command = APP_RUNTIME_PAUSE; break;
        case ENGINE_VIEWPORT_STEP: command = APP_RUNTIME_STEP; break;
        case ENGINE_VIEWPORT_RESTART: command = APP_RUNTIME_RESTART; break;
        case ENGINE_VIEWPORT_STOP: command = APP_RUNTIME_STOP; break;
        default: return -1;
    }
    return app_scene_authoring_runtime_command(scene, target, command);
}

static CF_Color packed_color(unsigned color)
{
    return cf_make_color_rgba(color >> 24, color >> 16, color >> 8, color);
}

static void draw_particles(const game_particles *particles)
{
    const game_particle *items = game_particles_data(particles);
    for (int i = 0; i < game_particles_count(particles); i++) {
        const game_particle *p = &items[i];
        float remaining = 1.0f - p->age / p->lifetime;
        CF_Color color = cf_make_color_rgb(p->color >> 24, p->color >> 16, p->color >> 8);
        color.a = (float)(p->color & 255) / 255.0f * remaining;
        float size = 1.0f + 3.0f * remaining;
        cf_draw_push_color(color);
        cf_draw_quad_fill(cf_make_aabb_pos_w_h(cf_v2(p->position.x, -p->position.y), size, size), 0.0f);
        cf_draw_pop_color();
    }
}

static void draw_appearance(
    app_sdf_shapes shapes,
    const GameAppearance *appearance,
    const GameTransform *transform,
    float radians,
    const GameSdfMotion *motion
)
{
    CF_V2 center = { transform->x, -transform->y };
    float width = appearance->width * 0.5f;
    float height = appearance->height * 0.5f;
    float border = isfinite(appearance->border_width) && appearance->border_width > 0
                     ? appearance->border_width
                     : 0;
    float glow = isfinite(appearance->glow_radius) && appearance->glow_radius > 0
                   ? appearance->glow_radius
                   : 0;
    cf_draw_push_color(packed_color(appearance->color));
    cf_draw_push_interior_border(packed_color(appearance->border_color), border);
    cf_draw_push_glow(packed_color(appearance->glow_color), glow);
    if (motion && (motion->amplitude > 0 || motion->pluck_amplitude > 0.01f) &&
        app_sdf_shapes_draw(shapes, appearance, transform, radians, motion)) {
        cf_draw_pop_glow();
        cf_draw_pop_outline();
        cf_draw_pop_color();
        return;
    }
    switch (appearance->shape) {
        case GAME_SHAPE_CIRCLE:
            cf_draw_circle_fill2(center, width < height ? width : height);
            break;
        case GAME_SHAPE_CAPSULE: {
            float radius = fminf(width, height);
            CF_V2 offset = width >= height ? (CF_V2){ width - radius, 0 }
                                           : (CF_V2){ 0, height - radius };
            game_appearance_rotate_point(offset, radians, &offset);
            cf_draw_capsule_fill2(
                (CF_V2){ center.x - offset.x, center.y - offset.y },
                (CF_V2){ center.x + offset.x, center.y + offset.y },
                radius
            );
            break;
        }
        case GAME_SHAPE_DIAMOND:
        case GAME_SHAPE_CHEVRON:
        case GAME_SHAPE_STROKED_CHEVRON:
        default: {
            CF_V2 local[4];
            int count = game_appearance_points(appearance, local);
            if (!count) {
                local[0] = (CF_V2){ -width, -height };
                local[1] = (CF_V2){ width, -height };
                local[2] = (CF_V2){ width, height };
                local[3] = (CF_V2){ -width, height };
                count = 4;
            }
            CF_V2 points[4];
            for (int i = 0; i < count; i++) {
                game_appearance_rotate_point(local[i], radians, &local[i]);
                points[i] = (CF_V2){ center.x + local[i].x, center.y + local[i].y };
            }
            if (appearance->shape == GAME_SHAPE_STROKED_CHEVRON) {
                cf_draw_polyline(points, count, appearance->stroke_width, true);
            } else {
                cf_draw_polygon_fill(points, count, 0);
            }
            break;
        }
    }
    cf_draw_pop_glow();
    cf_draw_pop_outline();
    cf_draw_pop_color();
}

app_sdf_shapes app_scene_authoring_shapes(const app_scene_authoring *scene)
{
    return scene->shapes;
}

void app_scene_authoring_render(void *user)
{
    app_scene_authoring *scene = (app_scene_authoring *)user;
    if (!scene) {
        return;
    }
    ecs_world *world = scene->active_runtime_target == APP_RUNTIME_PREVIEW
                         ? game_prototype_preview_world(scene->prototype_preview)
                         : scene->world;
    const game_particles *particles = scene->active_runtime_target == APP_RUNTIME_PREVIEW
                                        ? game_prototype_preview_particles(scene->prototype_preview)
                                        : scene->live->particles;
    bool material_drawn = scene->render_viewport && app_scene_renderer_draw(
                                                        scene->renderer,
                                                        world,
                                                        particles,
                                                        scene->render_viewport
                                                    );
    ecs_component_id required[] = { GAME_COMPONENT_GameTransform.id,
                                    GAME_COMPONENT_GameAppearance.id };
    ecs_query query;
    if (ecs_query_begin(world, &query, required, 2) != ECS_OK) {
        return;
    }
    ecs_entity entity;
    while (ecs_query_next(&query, &entity)) {
        const GameTransform *transform = GAME_GameTransform_get_const(world, entity);
        const GameAppearance *appearance = GAME_GameAppearance_get_const(world, entity);
        CF_Aabb bounds;
        float radians = actor_orientation(world, entity);
        const GameSdfMotion *motion = GAME_GameSdfMotion_get_const(world, entity);
        if (!appearance_bounds(
                transform,
                appearance,
                GAME_GameShader_get_const(world, entity),
                radians,
                motion,
                false,
                &bounds
            )) {
            continue;
        }
        float x = transform->x;
        float y = -transform->y;
        if (!material_drawn) {
            GameAppearance visible = *appearance;
            const GameMaterial *material = GAME_GameMaterial_get_const(world, entity);
            if (material) {
                visible.color = material->scene;
                visible.border_color = material->border_scene;
                visible.glow_radius = 0;
            }
            draw_appearance(scene->shapes, &visible, transform, radians, motion);
        }
        const GameHealth *health = GAME_GameHealth_get_const(world, entity);
        if (!health || health->current >= health->maximum) {
            continue;
        }
        float bar_y = y + appearance->height * 0.5f + 8.0f;
        float maximum = health->maximum > 0 ? (float)health->maximum : 1.0f;
        float ratio = (float)health->current / maximum;
        if (ratio < 0.0f) {
            ratio = 0.0f;
        }
        if (ratio > 1.0f) {
            ratio = 1.0f;
        }
        cf_draw_push_color(cf_make_color_rgb(40, 40, 40));
        cf_draw_quad_fill(cf_make_aabb_pos_w_h(cf_v2(x, bar_y), 36.0f, 5.0f), 0.0f);
        cf_draw_pop_color();
        cf_draw_push_color(cf_make_color_rgb(80, 210, 90));
        cf_draw_quad_fill(
            cf_make_aabb_pos_w_h(cf_v2(x - 18.0f + 18.0f * ratio, bar_y), 36.0f * ratio, 3.0f),
            0.0f
        );
        cf_draw_pop_color();
    }
    ecs_query_end(&query);
    if (!material_drawn) {
        draw_particles(particles);
    }
    if (scene->active_runtime_target == APP_RUNTIME_PREVIEW && scene->preview_follow) {
        const GameTransform *position = GAME_GameTransform_get_const(
            world,
            game_prototype_preview_entity(scene->prototype_preview)
        );
        if (position) {
            CF_V2 path[] = { { scene->preview_origin.x, -scene->preview_origin.y },
                             { position->x, -position->y } };
            engine_viewport_camera camera;
            engine_viewport_get_camera(scene->render_viewport, &camera);
            int width, height;
            engine_viewport_pixel_size(scene->render_viewport, &width, &height);
            float stroke = camera.span_width / fmaxf(1, width);
            cf_draw_push_color(packed_color(0x82B5FF55));
            cf_draw_polyline(path, 2, stroke, false);
            cf_draw_circle_fill2(path[0], stroke * 3);
            cf_draw_pop_color();
        }
    }
    for (int selection = -1; selection < acount(scene->actors); selection++) {
        ecs_entity selected = selection < 0 ? scene->runtime_selected
                                            : scene->actors[selection].entity;
        bool marked = selection < 0 ? selected.slot != 0
                                    : scene->actors[selection].marked ||
                                          selection == scene->selected;
        if (!scene_can_move(scene) || !selected.slot || !marked) {
            continue;
        }
        CF_Aabb bounds;
        const GameTransform *position = GAME_GameTransform_get_const(world, selected);
        const GameAppearance *appearance = GAME_GameAppearance_get_const(world, selected);
        if (position && appearance &&
            appearance_bounds(
                position,
                appearance,
                GAME_GameShader_get_const(world, selected),
                actor_orientation(world, selected),
                GAME_GameSdfMotion_get_const(world, selected),
                false,
                &bounds
            )) {
            float scale = scene->selection_scale > 0 ? scene->selection_scale : 1;
            bounds.min.x -= 3 * scale;
            bounds.min.y -= 3 * scale;
            bounds.max.x += 3 * scale;
            bounds.max.y += 3 * scale;
            cf_draw_push_color(packed_color(0x82B5FFFF));
            cf_draw_quad(bounds, scale * 0.5f, 0);
            cf_draw_pop_color();
        }
    }
    if (scene->marquee_visible) {
        CF_Aabb box = { { fminf(scene->marquee_start.x, scene->marquee_end.x),
                          fminf(scene->marquee_start.y, scene->marquee_end.y) },
                        { fmaxf(scene->marquee_start.x, scene->marquee_end.x),
                          fmaxf(scene->marquee_start.y, scene->marquee_end.y) } };
        cf_draw_push_color(packed_color(0x82B5FF22));
        cf_draw_quad_fill(box, 0);
        cf_draw_pop_color();
        cf_draw_push_color(packed_color(0x82B5FFFF));
        cf_draw_quad(box, scene->selection_scale * 0.5f, 0);
        cf_draw_pop_color();
    }
}
