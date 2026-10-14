#include "render/mesh_surface.h"

#include "render/scene3d_geometry.h"
#include "render/shader_catalog.h"
#include "runtime/component_catalog.h"
#include "support/array.h"

#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/* Shared immutable proxy; dimensions and transforms belong to each surface. */
static render_mesh_surface_vertex sdf_proxy[] = {
    { .position = { -0.5, -0.5, -0.5 }, .normal = { -1, 0, 0 } },
    { .position = { -0.5, -0.5, 0.5 }, .normal = { -1, 0, 0 } },
    { .position = { -0.5, 0.5, 0.5 }, .normal = { -1, 0, 0 } },
    { .position = { -0.5, -0.5, -0.5 }, .normal = { -1, 0, 0 } },
    { .position = { -0.5, 0.5, 0.5 }, .normal = { -1, 0, 0 } },
    { .position = { -0.5, 0.5, -0.5 }, .normal = { -1, 0, 0 } },
    { .position = { 0.5, -0.5, -0.5 }, .normal = { 1, 0, 0 } },
    { .position = { 0.5, 0.5, -0.5 }, .normal = { 1, 0, 0 } },
    { .position = { 0.5, 0.5, 0.5 }, .normal = { 1, 0, 0 } },
    { .position = { 0.5, -0.5, -0.5 }, .normal = { 1, 0, 0 } },
    { .position = { 0.5, 0.5, 0.5 }, .normal = { 1, 0, 0 } },
    { .position = { 0.5, -0.5, 0.5 }, .normal = { 1, 0, 0 } },
    { .position = { -0.5, -0.5, -0.5 }, .normal = { 0, -1, 0 } },
    { .position = { 0.5, -0.5, -0.5 }, .normal = { 0, -1, 0 } },
    { .position = { 0.5, -0.5, 0.5 }, .normal = { 0, -1, 0 } },
    { .position = { -0.5, -0.5, -0.5 }, .normal = { 0, -1, 0 } },
    { .position = { 0.5, -0.5, 0.5 }, .normal = { 0, -1, 0 } },
    { .position = { -0.5, -0.5, 0.5 }, .normal = { 0, -1, 0 } },
    { .position = { -0.5, 0.5, -0.5 }, .normal = { 0, 1, 0 } },
    { .position = { -0.5, 0.5, 0.5 }, .normal = { 0, 1, 0 } },
    { .position = { 0.5, 0.5, 0.5 }, .normal = { 0, 1, 0 } },
    { .position = { -0.5, 0.5, -0.5 }, .normal = { 0, 1, 0 } },
    { .position = { 0.5, 0.5, 0.5 }, .normal = { 0, 1, 0 } },
    { .position = { 0.5, 0.5, -0.5 }, .normal = { 0, 1, 0 } },
    { .position = { -0.5, -0.5, -0.5 }, .normal = { 0, 0, -1 } },
    { .position = { -0.5, 0.5, -0.5 }, .normal = { 0, 0, -1 } },
    { .position = { 0.5, 0.5, -0.5 }, .normal = { 0, 0, -1 } },
    { .position = { -0.5, -0.5, -0.5 }, .normal = { 0, 0, -1 } },
    { .position = { 0.5, 0.5, -0.5 }, .normal = { 0, 0, -1 } },
    { .position = { 0.5, -0.5, -0.5 }, .normal = { 0, 0, -1 } },
    { .position = { -0.5, -0.5, 0.5 }, .normal = { 0, 0, 1 } },
    { .position = { 0.5, -0.5, 0.5 }, .normal = { 0, 0, 1 } },
    { .position = { 0.5, 0.5, 0.5 }, .normal = { 0, 0, 1 } },
    { .position = { -0.5, -0.5, 0.5 }, .normal = { 0, 0, 1 } },
    { .position = { 0.5, 0.5, 0.5 }, .normal = { 0, 0, 1 } },
    { .position = { -0.5, 0.5, 0.5 }, .normal = { 0, 0, 1 } },
};

const char *render_mesh_surface_lod_shape(const SdfSurface3D *sdf, float pixels, bool disabled)
{
    if (disabled || !sdf || !sdf->enabled || !sdf->low_detail_shape ||
        !*sdf->low_detail_shape) {
        return NULL;
    }
    return pixels <= sdf->low_detail_pixels ? sdf->low_detail_shape : NULL;
}

int render_mesh_surface_corner_count(const render_mesh_surface_part *part)
{
    return part->corner_count ? part->corner_count : acount(part->vertices);
}

const render_mesh_surface_vertex *render_mesh_surface_corner(
    const render_mesh_surface_part *part,
    int corner
)
{
    return &part->vertices[part->indices ? part->indices[corner] : corner];
}

static void add_triangle(render_mesh_surface_vertex **vertices, CF_V3 a, CF_V3 b, CF_V3 c)
{
    CF_V3 normal = cf_norm_v3(cf_cross_v3(cf_sub_v3(b, a), cf_sub_v3(c, a)));
    apush(*vertices, ((render_mesh_surface_vertex){ .position = a, .normal = normal }));
    apush(*vertices, ((render_mesh_surface_vertex){ .position = b, .normal = normal }));
    apush(*vertices, ((render_mesh_surface_vertex){ .position = c, .normal = normal }));
}

static void faces(
    render_mesh_surface_vertex **vertices,
    const CF_V3 points[],
    const int triangles[][3],
    int count
)
{
    for (int i = 0; i < count; i++) {
        add_triangle(
            vertices,
            points[triangles[i][0]],
            points[triangles[i][1]],
            points[triangles[i][2]]
        );
    }
}

bool render_mesh_surface_vertices(
    int primitive,
    render_mesh_surface_vertex **vertices,
    char *error,
    size_t size
)
{
    assert(vertices);
    aclear(*vertices);
    CF_V3 triangles[SCENE3D_MESH_TRIANGLE_LIMIT][3];
    int count = render_scene3d_mesh_triangles(primitive, triangles);
    for (int i = 0; i < count; i++) {
        add_triangle(vertices, triangles[i][0], triangles[i][1], triangles[i][2]);
    }
    if (count) {
        return true;
    }
    switch (primitive) {
        case PRIMITIVE_BOX: {
            CF_V3 p[8];
            for (int i = 0; i < 8; i++) {
                p[i] = (CF_V3){ i & 1 ? .5f : -.5f,
                                i & 2 ? .5f : -.5f,
                                i & 4 ? .5f : -.5f };
            }
            static const int quads[][4] = { { 0, 4, 6, 2 }, { 1, 3, 7, 5 },
                                            { 0, 1, 5, 4 }, { 2, 6, 7, 3 },
                                            { 0, 2, 3, 1 }, { 4, 5, 7, 6 } };
            for (int i = 0; i < 6; i++) {
                add_triangle(vertices, p[quads[i][0]], p[quads[i][1]], p[quads[i][2]]);
                add_triangle(vertices, p[quads[i][0]], p[quads[i][2]], p[quads[i][3]]);
            }
            return true;
        }
        case PRIMITIVE_TRIANGLE_PRISM: {
            static const CF_V3 p[] = {
                { -.5f, -.5f, -.5f }, { .5f, -.5f, 0 }, { -.5f, -.5f, .5f },
                { -.5f, .5f, -.5f },  { .5f, .5f, 0 },  { -.5f, .5f, .5f }
            };
            static const int t[][3] = { { 0, 1, 2 }, { 3, 5, 4 }, { 0, 3, 4 },
                                        { 0, 4, 1 }, { 1, 4, 5 }, { 1, 5, 2 },
                                        { 2, 5, 3 }, { 2, 3, 0 } };
            faces(vertices, p, t, 8);
            return true;
        }
        case PRIMITIVE_OCTAHEDRON: {
            static const CF_V3 p[] = { { .5f, 0, 0 },  { 0, 0, .5f },
                                       { -.5f, 0, 0 }, { 0, 0, -.5f },
                                       { 0, .5f, 0 },  { 0, -.5f, 0 } };
            static const int t[][3] = { { 4, 1, 0 }, { 4, 2, 1 }, { 4, 3, 2 },
                                        { 4, 0, 3 }, { 5, 0, 1 }, { 5, 1, 2 },
                                        { 5, 2, 3 }, { 5, 3, 0 } };
            faces(vertices, p, t, 8);
            return true;
        }
        case PRIMITIVE_TETRAHEDRON: {
            static const CF_V3 p[] = { { -.5f, -.5f, -.5f },
                                       { .5f, -.5f, -.5f },
                                       { -.5f, .5f, -.5f },
                                       { -.5f, -.5f, .5f } };
            static const int t[][3] = { { 0, 2, 1 }, { 0, 1, 3 }, { 0, 3, 2 }, { 1, 2, 3 } };
            faces(vertices, p, t, 4);
            return true;
        }
        default:
            snprintf(error, size, "Mesh Surface 2D supports polygonal primitives; primitive %d is unsupported.", primitive);
            return false;
    }
}

static void clear_surface(render_mesh_surface *surface)
{
    for (int i = 0; i < acount(surface->parts); i++) {
        if (!surface->parts[i].borrowed) {
            afree(surface->parts[i].vertices);
        }
    }
    aclear(surface->parts);
    surface->min = surface->max = (CF_V3){ 0 };
}

void render_mesh_surface_free(render_mesh_surface *surface)
{
    clear_surface(surface);
    afree(surface->parts);
}

/* An imported mesh's parts: one per material group, or the whole mesh. */
static void add_mesh_asset(
    render_mesh_surface *surface,
    const scene3d_visual *visual,
    const render_mesh_asset *asset
)
{
    render_mesh_surface_part part = { .visual = *visual,
                                      .vertices = asset->vertices,
                                      .indices = asset->indices,
                                      .corner_count = acount(asset->indices),
                                      .asset_identity = asset->identity,
                                      .part_coordinates = asset->has_part_coordinates,
                                      .uv_bounds = asset->has_uv_bounds,
                                      .uv_min = asset->uv_min,
                                      .uv_max = asset->uv_max,
                                      .borrowed = true };
    CF_M4x4 model = render_scene3d_visual_model(&part.visual);
    for (int i = 0; i < render_mesh_surface_corner_count(&part); i++) {
        CF_V3 p = cf_m4_transform_point(
            model,
            render_mesh_surface_corner(&part, i)->position
        );
        surface->min = cf_min_v3(surface->min, p);
        surface->max = cf_max_v3(surface->max, p);
    }
    if (!acount(asset->groups)) {
        apush(surface->parts, part);
        return;
    }
    for (int i = 0; i < acount(asset->groups); i++) {
        const render_mesh_group *group = &asset->groups[i];
        render_mesh_surface_part slice = part;
        slice.indices = asset->indices + group->first;
        slice.corner_count = group->count;
        slice.first_index = group->first;
        slice.material_scope = group->material_scope;
        snprintf(
            slice.visual.part.material,
            sizeof slice.visual.part.material,
            "%s",
            group->material
        );
        apush(surface->parts, slice);
    }
}

bool render_mesh_surface_build_with_bounds(
    const ecs_world *world,
    ecs_entity entity,
    const Transform *position,
    const GlobalTransform3D *transform,
    const model_bounds *override,
    render_mesh_surface *surface,
    char *error,
    size_t size
)
{
    clear_surface(surface);
    if (!Geometry3D_get_const(world, entity) ||
        (!transform && !position && !Transform_get_const(world, entity))) {
        snprintf(error, size, "Mesh Surface 2D requires Geometry3D and Transform.");
        return false;
    }
    const SdfSurface3D *sdf = SdfSurface3D_get_const(world, entity);
    if (sdf && sdf->enabled) {
        if (sdf->low_detail_shape && *sdf->low_detail_shape &&
            (!isfinite(sdf->low_detail_pixels) || sdf->low_detail_pixels <= 0)) {
            snprintf(error, size, "SDF low-detail graph requires a finite positive pixel cutoff.");
            return false;
        }
        const Geometry3D *shape = Geometry3D_get_const(world, entity);
        CF_M4x4 frame;
        scene3d_frame_state state = render_scene3d_visual_frame(world, entity, position, transform, &frame);
        CF_V3 dimensions = { shape->width, shape->height, shape->depth };
        CF_V3 center = { 0 };
        const Shader *binding = Shader_get_const(world, entity);
        int id = binding ? render_shader_find(binding->shape) : 0;
        if (override) {
            if (override->empty) {
                return true;
            }
            dimensions = (CF_V3){ override->max[0] - override->min[0],
                                  override->max[1] - override->min[1],
                                  override->max[2] - override->min[2] };
            center = (CF_V3){ (override->min[0] + override->max[0]) * .5f,
                              (override->min[1] + override->max[1]) * .5f,
                              (override->min[2] + override->max[2]) * .5f };
        } else if (render_shader_is_model(id)) {
            float parameters[32][4], min[3], max[3];
            bool empty;
            shader_error diagnostic = { 0 };
            if (!render_shader_model_snapshot(
                    id,
                    (ecs_world *)world,
                    entity,
                    binding->parameters,
                    parameters,
                    min,
                    max,
                    &empty,
                    &diagnostic
                )) {
                snprintf(error, size, "%s", diagnostic.message);
                return false;
            }
            if (empty) {
                return true;
            }
            dimensions = (CF_V3){ max[0] - min[0], max[1] - min[1], max[2] - min[2] };
            center = (CF_V3){ max[0] * .5f + min[0] * .5f,
                              max[1] * .5f + min[1] * .5f,
                              max[2] * .5f + min[2] * .5f };
        }
        if (state == SCENE3D_FRAME_INVALID || !isfinite(dimensions.x) ||
            !isfinite(dimensions.y) || !isfinite(dimensions.z) ||
            !isfinite(center.x) || !isfinite(center.y) || !isfinite(center.z) ||
            dimensions.x <= 0 || dimensions.y <= 0 || dimensions.z <= 0) {
            snprintf(error, size, "SDF surface requires finite transforms and positive bounds.");
            return false;
        }
        if (state == SCENE3D_FRAME_FLAT) {
            return true;
        }
        render_mesh_surface_part part = {
            .visual = { .part = { .primitive = PRIMITIVE_BOX, .dimensions = dimensions },
                        .frame = cf_mul_m4(frame, cf_m4_translate(center)) },
            .sdf_center = center,
        };
        state = render_scene3d_visual_state(&part.visual);
        if (state == SCENE3D_FRAME_INVALID) {
            snprintf(error, size, "SDF surface requires finite transforms and positive bounds.");
            return false;
        }
        if (state == SCENE3D_FRAME_FLAT) {
            return true;
        }
        part.vertices = sdf_proxy;
        part.corner_count = 36;
        part.borrowed = true;
        surface->min = (CF_V3){ FLT_MAX, FLT_MAX, FLT_MAX };
        surface->max = (CF_V3){ -FLT_MAX, -FLT_MAX, -FLT_MAX };
        CF_M4x4 model = render_scene3d_visual_model(&part.visual);
        for (int i = 0; i < 8; i++) {
            CF_V3 corner = { i & 1 ? .5f : -.5f, i & 2 ? .5f : -.5f, i & 4 ? .5f : -.5f };
            CF_V3 p = cf_m4_transform_point(model, corner);
            surface->min = cf_min_v3(surface->min, p);
            surface->max = cf_max_v3(surface->max, p);
        }
        apush(surface->parts, part);
        return true;
    }
    scene3d_visual visuals[SCENE3D_PART_LIMIT];
    const render_mesh_asset *asset;
    int count = render_scene3d_visuals(world, entity, position, transform, visuals, &asset, error, size);
    if (count < 0) {
        return false;
    }
    surface->min = (CF_V3){ FLT_MAX, FLT_MAX, FLT_MAX };
    surface->max = (CF_V3){ -FLT_MAX, -FLT_MAX, -FLT_MAX };
    if (asset) {
        add_mesh_asset(surface, &visuals[0], asset);
        return true;
    }
    for (int i = 0; i < count; i++) {
        render_mesh_surface_part part = { .visual = visuals[i] };
        if (!render_mesh_surface_vertices(part.visual.part.primitive, &part.vertices, error, size)) {
            afree(part.vertices);
            render_mesh_surface_free(surface);
            return false;
        }
        CF_M4x4 model = render_scene3d_visual_model(&part.visual);
        for (int v = 0; v < render_mesh_surface_corner_count(&part); v++) {
            CF_V3 p = cf_m4_transform_point(model, part.vertices[v].position);
            surface->min = cf_min_v3(surface->min, p);
            surface->max = cf_max_v3(surface->max, p);
        }
        apush(surface->parts, part);
    }
    if (!count) {
        surface->min = surface->max = (CF_V3){ 0 };
    }
    return true;
}

bool render_mesh_surface_build(
    const ecs_world *world,
    ecs_entity entity,
    const Transform *position,
    render_mesh_surface *surface,
    char *error,
    size_t size
)
{
    return render_mesh_surface_build_with_bounds(world, entity, position, NULL, NULL, surface, error, size);
}

bool render_mesh_surface_bounds(
    const ecs_world *world,
    ecs_entity entity,
    const Transform *position,
    CF_V2 *min,
    CF_V2 *max,
    char *error,
    size_t size
)
{
    render_mesh_surface surface = { 0 };
    bool found = render_mesh_surface_build(world, entity, position, &surface, error, size) &&
                 acount(surface.parts) > 0;
    if (found) {
        *min = (CF_V2){ surface.min.x, surface.min.z };
        *max = (CF_V2){ surface.max.x, surface.max.z };
    }
    render_mesh_surface_free(&surface);
    return found;
}

CF_V3 render_mesh_surface_normal(CF_M4x4 model, CF_V3 normal)
{
    CF_V3 a = { model.elements[0], model.elements[1], model.elements[2] };
    CF_V3 b = { model.elements[4], model.elements[5], model.elements[6] };
    CF_V3 c = { model.elements[8], model.elements[9], model.elements[10] };
    return cf_norm_v3(cf_add_v3(
        cf_add_v3(
            cf_mul_v3_f(cf_cross_v3(b, c), normal.x),
            cf_mul_v3_f(cf_cross_v3(c, a), normal.y)
        ),
        cf_mul_v3_f(cf_cross_v3(a, b), normal.z)
    ));
}

bool render_mesh_surface_pick(const render_mesh_surface *surface, CF_V2 position, float *height)
{
    bool found = false;
    float front = -FLT_MAX;
    for (int i = 0; i < acount(surface->parts); i++) {
        const render_mesh_surface_part *part = &surface->parts[i];
        CF_M4x4 model = render_scene3d_visual_model(&part->visual);
        for (int v = 0; v < render_mesh_surface_corner_count(part); v += 3) {
            CF_V3 a = cf_m4_transform_point(
                model,
                render_mesh_surface_corner(part, v)->position
            );
            CF_V3 b = cf_m4_transform_point(
                model,
                render_mesh_surface_corner(part, v + 1)->position
            );
            CF_V3 c = cf_m4_transform_point(
                model,
                render_mesh_surface_corner(part, v + 2)->position
            );
            float bx = b.x - a.x, bz = b.z - a.z, cx = c.x - a.x, cz = c.z - a.z;
            float area = bx * cz - bz * cx;
            if (fabsf(area) <= 1e-8f) {
                continue;
            }
            float x = position.x - a.x, z = position.y - a.z;
            float u = (x * cz - z * cx) / area;
            float w = (bx * z - bz * x) / area;
            if (u < -1e-5f || w < -1e-5f || u + w > 1.00001f) {
                continue;
            }
            float y = a.y + u * (b.y - a.y) + w * (c.y - a.y);
            if (y > front) {
                front = y;
                found = true;
            }
        }
    }
    if (found) {
        *height = front;
    }
    return found;
}
