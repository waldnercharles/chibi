#include "render/spot_lights.h"

#include "render/mesh_projection.h"
#include "runtime/component_catalog.h"

#include <math.h>
#include <stdio.h>

static bool pack(render_view_2d view, Transform position, const SpotLight *spot, render_spot_light *out)
{
    if (!isfinite(position.x) || !isfinite(position.y) || !isfinite(spot->elevation) ||
        !isfinite(spot->intensity) || !isfinite(spot->radius) ||
        spot->radius <= 0 || spot->intensity <= 0 || !(spot->color >> 8)) {
        return false;
    }
    float length = cf_len_v3(spot->direction);
    if (!isfinite(length) || length <= 0) {
        return false;
    }
    CF_V3 world = { position.x, spot->elevation, position.y };
    CF_V3 p = render_mesh_world_to_camera(view, world);
    CF_V3 axis = cf_div_v3_f(spot->direction, length);
    if (render_mesh_camera_orbit(view)) {
        CF_V3 basis[3];
        render_mesh_camera_basis(view, basis);
        axis = (CF_V3){ cf_dot_v3(axis, basis[0]),
                        cf_dot_v3(axis, basis[1]),
                        cf_dot_v3(axis, basis[2]) };
    }
    if (view.focal_distance > 0 && view.focal_distance - p.y < RENDER_MESH_NEAR) {
        return false;
    }
    p = (CF_V3){ p.x - view.center_x, -p.z - view.center_y, p.y };
    float peak = fmaxf(
                     (spot->color >> 24) & 255u,
                     fmaxf((spot->color >> 16) & 255u, (spot->color >> 8) & 255u)
                 ) /
                 255.f;
    double r2 = (double)spot->radius * spot->radius;
    double distance2 = (double)p.x * p.x + (double)p.y * p.y + (double)p.z * p.z;
    float score = (float)(spot->intensity * peak * r2 / (distance2 + r2));
    *out = (render_spot_light){
        .position_radius = { p.x, p.y, p.z, spot->radius },
        .color_intensity = { ((spot->color >> 24) & 255u) / 255.f,
                             ((spot->color >> 16) & 255u) / 255.f,
                             ((spot->color >> 8) & 255u) / 255.f,
                             spot->intensity },
        .direction_outer = { axis.x, -axis.z, axis.y, cosf(spot->outer_angle * CF_PI / 360) },
        .inner_cos = cosf(spot->inner_angle * CF_PI / 360),
        .shadows = spot->shadows,
        .score = score,
    };
    return isfinite(score);
}

bool render_spot_lights_collect(
    ecs_world *world,
    const engine *runtime,
    render_view_2d view,
    render_spot_lights *out,
    char *error,
    size_t size
)
{
    *out = (render_spot_lights){ 0 };
    ecs_component_id required[] = { COMPONENT_Transform.id, COMPONENT_SpotLight.id };
    ecs_query query;
    if (ecs_query_begin(world, &query, required, 2) != ECS_OK) {
        return true;
    }
    ecs_entity entity;
    bool valid = true;
    while (ecs_query_next(&query, &entity)) {
        if (!engine_entity_visible(runtime, world, entity)) {
            continue;
        }
        const SpotLight *spot = SpotLight_get_const(world, entity);
        if (!(spot->outer_angle >= 2 && spot->outer_angle <= 160) ||
            !(spot->inner_angle >= 0 && spot->inner_angle <= spot->outer_angle)) {
            snprintf(error, size, "Spot Light needs outer angle 2 to 160 and inner angle 0 to outer angle.");
            valid = false;
            break;
        }
        render_spot_light light;
        if (!pack(view, engine_render_transform(runtime, world, entity).position, spot, &light)) {
            continue;
        }
        int slot = out->count;
        if (slot == RENDER_SPOT_LIGHT_LIMIT) {
            slot--;
            if (light.score <= out->lights[slot].score) {
                continue;
            }
        } else {
            out->count++;
        }
        for (; slot > 0 && light.score > out->lights[slot - 1].score; slot--) {
            out->lights[slot] = out->lights[slot - 1];
        }
        out->lights[slot] = light;
    }
    ecs_query_end(&query);
    return valid;
}
