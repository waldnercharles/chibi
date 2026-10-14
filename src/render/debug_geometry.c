#include "render/debug_geometry.h"

#include "platform/cute.h"
#include "runtime/camera.h"
#include "runtime/component_catalog.h"
#include "runtime/transform_propagation.h"
#include "support/xalloc.h"

#include <math.h>
#include <stdlib.h>
typedef struct
{
    render_debug_segment *out;
    int capacity, visited;
    render_debug_geometry_result result;
    ecs_entity entity;
    uint32_t color;
} builder;
static bool finite3(CF_V3 v)
{
    return isfinite(v.x) && isfinite(v.y) && isfinite(v.z);
}
static void line(builder *b, CF_V3 a, CF_V3 c)
{
    if (!finite3(a) || !finite3(c)) {
        b->result.invalid = true;
        return;
    }
    if (b->result.segments >= b->capacity) {
        b->result.truncated = true;
        return;
    }
    b->out[b->result.segments++] = (render_debug_segment){ a, c, b->color, b->entity };
}
static void arc(builder *b, CF_V3 center, CF_V3 u, CF_V3 v, float turns)
{
    int count = (int)(32 * turns);
    CF_V3 last = cf_add_v3(center, u);
    for (int i = 1; i <= count; i++) {
        float angle = 2 * CF_PI * turns * (float)i / (float)count;
        CF_V3 next = cf_add_v3(
            center,
            cf_add_v3(cf_mul_v3_f(u, cosf(angle)), cf_mul_v3_f(v, sinf(angle)))
        );
        line(b, last, next);
        last = next;
    }
}
static void box(builder *b, CF_V3 center, CF_V3 axes[3])
{
    for (int k = 0; k < 3; k++) {
        for (int corner = 0; corner < 4; corner++) {
            CF_V3 mid = cf_add_v3(
                center,
                cf_add_v3(
                    cf_mul_v3_f(axes[(k + 1) % 3], corner & 1 ? 1 : -1),
                    cf_mul_v3_f(axes[(k + 2) % 3], corner & 2 ? 1 : -1)
                )
            );
            line(b, cf_sub_v3(mid, axes[k]), cf_add_v3(mid, axes[k]));
        }
    }
}
static void collider3d(builder *b, ecs_world *world, ecs_entity entity, const Collider3D *c)
{
    if (!Transform3D_has(world, entity)) {
        b->result.unsupported++;
        return;
    }
    GlobalTransform3D global = transform_world_3d(world, entity);
    CF_V3 center, scale;
    CF_Quat rotation;
    cf_m4_decompose(transform_matrix_3d(&global), &center, &rotation, &scale);
    scale = cf_abs_v3(scale);
    if (!finite3(center) || !finite3(scale) || scale.x < 1e-6f ||
        scale.y < 1e-6f || scale.z < 1e-6f) {
        b->result.invalid = true;
        return;
    }
    CF_V3 axes[3] = { cf_mul_q_v3(rotation, (CF_V3){ 1, 0, 0 }),
                      cf_mul_q_v3(rotation, (CF_V3){ 0, 1, 0 }),
                      cf_mul_q_v3(rotation, (CF_V3){ 0, 0, 1 }) };
    if (c->shape == COLLIDER3D_BOX) {
        if (!finite3(c->size) || c->size.x <= 0 || c->size.y <= 0 || c->size.z <= 0) {
            b->result.invalid = true;
            return;
        }
        CF_V3 reach = cf_mul_v3_f(cf_mul_v3(c->size, scale), .5f);
        axes[0] = cf_mul_v3_f(axes[0], reach.x);
        axes[1] = cf_mul_v3_f(axes[1], reach.y);
        axes[2] = cf_mul_v3_f(axes[2], reach.z);
        box(b, center, axes);
        return;
    }
    if (c->shape != COLLIDER3D_SPHERE && c->shape != COLLIDER3D_CAPSULE) {
        b->result.unsupported++;
        return;
    }
    float radius = c->radius * (c->shape == COLLIDER3D_SPHERE
                                    ? fmaxf(scale.x, fmaxf(scale.y, scale.z))
                                    : fmaxf(scale.x, scale.z));
    if (!isfinite(radius) || radius <= 0) {
        b->result.invalid = true;
        return;
    }
    if (c->shape == COLLIDER3D_SPHERE) {
        for (int k = 0; k < 3; k++) {
            arc(b,
                center,
                cf_mul_v3_f(axes[(k + 1) % 3], radius),
                cf_mul_v3_f(axes[(k + 2) % 3], radius),
                1);
        }
        return;
    }
    if (!isfinite(c->height) || c->height <= 0) {
        b->result.invalid = true;
        return;
    }
    CF_V3 half = cf_mul_v3_f(axes[1], fmaxf(c->height * scale.y * .5f - radius, 0));
    CF_V3 x = cf_mul_v3_f(axes[0], radius), z = cf_mul_v3_f(axes[2], radius),
          up = cf_mul_v3_f(axes[1], radius);
    for (int end = 0; end < 2; end++) {
        CF_V3 at = end ? cf_sub_v3(center, half) : cf_add_v3(center, half),
              cap = end ? cf_neg_v3(up) : up;
        arc(b, at, x, z, 1);
        arc(b, at, x, cap, .5f);
        arc(b, at, z, cap, .5f);
    }
    CF_V3 sides[] = { x, cf_neg_v3(x), z, cf_neg_v3(z) };
    for (int i = 0; i < 4; i++) {
        line(
            b,
            cf_add_v3(cf_add_v3(center, half), sides[i]),
            cf_add_v3(cf_sub_v3(center, half), sides[i])
        );
    }
}
static void collider2d(builder *b, ecs_world *world, ecs_entity entity, const Collider *c)
{
    if (!Transform_has(world, entity)) {
        b->result.unsupported++;
        return;
    }
    GlobalTransform2D pose = transform_world_2d(world, entity);
    CF_V3 center = { pose.position.x, 0, pose.position.y };
    float co = cosf(pose.radians), si = sinf(pose.radians);
    CF_V3 x = { co, 0, si }, y = { -si, 0, co };
    if (c->shape == COLLIDER_CIRCLE) {
        if (!isfinite(c->radius) || c->radius <= 0) {
            b->result.invalid = true;
            return;
        }
        arc(b, center, cf_mul_v3_f(x, c->radius), cf_mul_v3_f(y, c->radius), 1);
        return;
    }
    if (c->shape != COLLIDER_BOX || !isfinite(c->width) ||
        !isfinite(c->height) || c->width <= 0 || c->height <= 0) {
        b->result.invalid = true;
        return;
    }
    x = cf_mul_v3_f(x, c->width * .5f);
    y = cf_mul_v3_f(y, c->height * .5f);
    CF_V3 points[4] = { cf_add_v3(center, cf_add_v3(x, y)),
                        cf_add_v3(center, cf_sub_v3(x, y)),
                        cf_sub_v3(center, cf_add_v3(x, y)),
                        cf_add_v3(center, cf_sub_v3(y, x)) };
    for (int i = 0; i < 4; i++) {
        line(b, points[i], points[(i + 1) % 4]);
    }
}
static void camera_geometry(builder *b, ecs_world *world, const engine *engine, ecs_entity entity)
{
    engine_viewport_view view;
    if (!camera_entity_view(world, engine, entity, &view)) {
        b->result.invalid = true;
        return;
    }
    if (Transform3D_has(world, entity)) {
        engine_viewport_camera frame = { .center_x = view.center_x,
                                         .center_y = view.center_y,
                                         .span_width = view.width,
                                         .span_height = view.height,
                                         .lens = view.lens };
        render_scene3d_camera camera = render_scene3d_camera_frame(&frame);
        CF_V3 points[8];
        for (int i = 0; i < 4; i++) {
            CF_Ray3 ray = render_scene3d_ray(&camera, (CF_V2){ i & 1 ? 1 : -1, i & 2 ? 1 : -1 });
            points[i] = ray.p;
            points[i + 4] = cf_add_v3(ray.p, cf_mul_v3_f(ray.d, ray.t));
        }
        int edges[4][2] = { { 0, 1 }, { 1, 3 }, { 3, 2 }, { 2, 0 } };
        for (int i = 0; i < 4; i++) {
            line(b, points[edges[i][0]], points[edges[i][1]]);
            line(b, points[edges[i][0] + 4], points[edges[i][1] + 4]);
            line(b, points[i], points[i + 4]);
        }
    } else {
        CF_V3 points[4] = {
            { view.center_x - view.width * .5f, 0, -view.center_y - view.height * .5f },
            { view.center_x + view.width * .5f, 0, -view.center_y - view.height * .5f },
            { view.center_x + view.width * .5f, 0, -view.center_y + view.height * .5f },
            { view.center_x - view.width * .5f, 0, -view.center_y + view.height * .5f }
        };
        for (int i = 0; i < 4; i++) {
            line(b, points[i], points[(i + 1) % 4]);
        }
    }
}
render_debug_geometry_result render_debug_geometry_collect(
    ecs_world *world,
    const engine *engine,
    bool colliders,
    bool cameras,
    render_skip skip,
    render_debug_segment *out,
    int capacity
)
{
    builder b = { .out = out, .capacity = capacity };
    if (!world || !out || capacity < 1 || capacity > 8192 ||
        transform_propagate(world) != ECS_OK) {
        b.result.invalid = true;
        return b.result;
    }
    ecs_query query;
    if (ecs_query_begin(world, &query, NULL, 0) != ECS_OK) {
        b.result.invalid = true;
        return b.result;
    }
    ecs_entity entity;
    while (ecs_query_next(&query, &entity)) {
        if (++b.visited > 4096 || b.result.truncated) {
            b.result.truncated = true;
            break;
        }
        const RenderState *state = RenderState_get_const(world, entity);
        if ((state && state->hidden) || render_skip_has(skip, entity)) {
            continue;
        }
        b.entity = entity;
        if (colliders) {
            b.color = 0x8ce07aff;
            const Collider3D *c3 = Collider3D_get_const(world, entity);
            const Collider *c2 = Collider_get_const(world, entity);
            if (c3) {
                collider3d(&b, world, entity, c3);
            } else if (c2) {
                collider2d(&b, world, entity, c2);
            }
        }
        if (cameras && Camera_has(world, entity)) {
            b.color = 0xeac36bff;
            camera_geometry(&b, world, engine, entity);
        }
    }
    ecs_query_end(&query);
    return b.result;
}
render_debug_geometry_result render_debug_geometry_draw(
    ecs_world *world,
    const engine *engine,
    engine_viewport *viewport,
    bool colliders,
    bool cameras,
    render_skip skip
)
{
    render_debug_segment *segments = xmalloc(8192 * sizeof *segments);
    render_debug_geometry_result
        result = render_debug_geometry_collect(world, engine, colliders, cameras, skip, segments, 8192);
    engine_viewport_camera frame;
    engine_viewport_get_camera(viewport, &frame);
    int width, height;
    engine_viewport_pixel_size(viewport, &width, &height);
    (void)height;
    float stroke = 1.5f * frame.span_width / fmaxf(width, 1);
    render_scene3d_camera camera;
    engine_viewport_get_camera3d(viewport, &camera);
    for (int i = 0; i < result.segments; i++) {
        CF_V3 a = segments[i].a, b = segments[i].b;
        CF_V2 from, to;
        if (engine_viewport_perspective(viewport)) {
            a = render_scene3d_project(&camera, a);
            b = render_scene3d_project(&camera, b);
            if (!finite3(a) || !finite3(b) || a.z < 0 || a.z > 1 || b.z < 0 || b.z > 1) {
                continue;
            }
            from = (CF_V2){ frame.center_x + a.x * frame.span_width * .5f,
                            frame.center_y + a.y * frame.span_height * .5f };
            to = (CF_V2){ frame.center_x + b.x * frame.span_width * .5f,
                          frame.center_y + b.y * frame.span_height * .5f };
        } else {
            from = (CF_V2){ a.x, -a.z };
            to = (CF_V2){ b.x, -b.z };
        }
        uint32_t color = segments[i].color;
        cf_draw_push_color(cf_make_color_rgba(color >> 24, color >> 16, color >> 8, color));
        cf_draw_line(from, to, stroke);
        cf_draw_pop_color();
    }
    free(segments);
    return result;
}
