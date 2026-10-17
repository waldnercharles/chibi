#include "runtime/camera.h"

#include "render/view.h"
#include "runtime/component_catalog.h"
#include "runtime/transform_propagation.h"

#include <math.h>

/* Unity's smallest field of view, 0.00001 degrees. Narrower ones push the
 * default clips past float range at any usable span. */
static const float narrowest_fov = 1.745329e-7f;

/* The 3D lens of a camera, looking along its global -Z with +Y up from its
 * offset position when it has a Transform3D. False for values the 3D path
 * can't use. */
static bool camera_lens(const Camera *camera, const GlobalTransform3D *pose, engine_viewport_lens *lens)
{
    float closest = camera->near_clip, farthest = camera->far_clip;
    *lens = (engine_viewport_lens){ .fov = camera->fov,
                                    .orthographic = camera->orthographic,
                                    .near_clip = closest,
                                    .far_clip = farthest };
    bool clips = (closest == 0 && farthest == 0) ||
                 (closest > 0 && farthest > closest &&
                  farthest / closest <= ENGINE_VIEWPORT_CLIP_RATIO_MAX);
    bool fov = camera->fov == 0 ||
               (camera->fov >= narrowest_fov && camera->fov < CF_PI);
    if (!(fov && clips)) {
        return false;
    }
    if (!pose) {
        return true;
    }
    lens->eye = cf_add_v3(
        pose->position,
        (CF_V3){ camera->offset.x, 0, camera->offset.y }
    );
    lens->forward = cf_safe_norm_v3(cf_neg_v3(pose->z_axis));
    lens->up = cf_safe_norm_v3(pose->y_axis);
    CF_V3 side = cf_cross_v3(lens->forward, lens->up);
    float turn = cf_dot_v3(side, side);
    return turn > 0 && isfinite(lens->eye.x) && isfinite(lens->eye.y) &&
           isfinite(lens->eye.z);
}

int camera_view(ecs_world *world, const engine *runtime, engine_viewport_view *out)
{
    if (!ecs_world_component(world, COMPONENT_Camera.id) ||
        transform_propagate(world) != ECS_OK) {
        return 0;
    }
    ecs_component_id required[] = { COMPONENT_Camera.id };
    ecs_query query;
    if (ecs_query_begin(world, &query, required, 1) != ECS_OK) {
        return 0;
    }
    int found = 0;
    ecs_entity entity;
    while (ecs_query_next(&query, &entity)) {
        const Camera *camera = Camera_get_const(world, entity);
        if (camera->disabled || ecs_entity_is_retiring(world, entity)) {
            continue;
        }
        /* Drawn as bodies are, between physics steps, so a camera riding one
         * holds still against it. */
        GlobalTransform3D shown;
        const GlobalTransform3D *pose = NULL;
        if (Transform3D_has(world, entity)) {
            shown = engine_render_transform_3d(runtime, world, entity);
            pose = &shown;
        }
        if (!pose && !Transform_has(world, entity)) {
            found = -1;
            break;
        }
        CF_V2 position = pose ? (CF_V2){ pose->position.x, pose->position.z }
                              : engine_render_transform(runtime, world, entity).position;
        *out = (engine_viewport_view){ .center_x = position.x + camera->offset.x,
                                       .center_y = -position.y - camera->offset.y,
                                       .width = camera->width,
                                       .height = camera->height,
                                       .focal_distance = camera->focal_distance,
                                       .yaw = camera->yaw,
                                       .pitch = camera->pitch,
                                       .target_elevation = camera->target_elevation,
                                       .roll = camera->roll };
        if (found || !camera_lens(camera, pose, &out->lens) ||
            !isfinite(out->center_x) || !isfinite(out->center_y) ||
            !isfinite(out->width) || !isfinite(out->height) || out->width <= 0 ||
            out->height <= 0 || !isfinite(out->focal_distance) ||
            out->focal_distance < 0 || !isfinite(out->yaw) || !isfinite(out->pitch) ||
            !isfinite(out->roll) || !isfinite(out->target_elevation) ||
            (out->focal_distance > 0 && out->focal_distance <= 1)) {
            found = -1;
            break;
        }
        found = 1;
    }
    ecs_query_end(&query);
    return found;
}
