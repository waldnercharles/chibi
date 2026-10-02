#include "render/mesh_surface.h"

#include "render/arena_geometry.h"
#include "runtime/generated.h"
#include "support/array.h"

#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

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
    CF_V3 triangles[ARENA_MESH_TRIANGLE_LIMIT][3];
    int count = render_arena_mesh_triangles(primitive, triangles);
    for (int i = 0; i < count; i++) {
        add_triangle(vertices, triangles[i][0], triangles[i][1], triangles[i][2]);
    }
    if (count) {
        return true;
    }
    switch (primitive) {
        case GAME_PRIMITIVE_BOX: {
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
        case GAME_PRIMITIVE_TRIANGLE_PRISM: {
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
        case GAME_PRIMITIVE_OCTAHEDRON: {
            static const CF_V3 p[] = { { .5f, 0, 0 },  { 0, 0, .5f },
                                       { -.5f, 0, 0 }, { 0, 0, -.5f },
                                       { 0, .5f, 0 },  { 0, -.5f, 0 } };
            static const int t[][3] = { { 4, 1, 0 }, { 4, 2, 1 }, { 4, 3, 2 },
                                        { 4, 0, 3 }, { 5, 0, 1 }, { 5, 1, 2 },
                                        { 5, 2, 3 }, { 5, 3, 0 } };
            faces(vertices, p, t, 8);
            return true;
        }
        case GAME_PRIMITIVE_TETRAHEDRON: {
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

void render_mesh_surface_free(render_mesh_surface *surface)
{
    for (int i = 0; i < acount(surface->parts); i++) {
        if (!surface->parts[i].borrowed) {
            afree(surface->parts[i].vertices);
        }
    }
    afree(surface->parts);
    memset(surface, 0, sizeof *surface);
}

bool render_mesh_surface_build(
    const ecs_world *world,
    ecs_entity entity,
    const GameTransform *position,
    render_mesh_surface *surface,
    char *error,
    size_t size
)
{
    render_mesh_surface_free(surface);
    if (!GAME_GameGeometry3D_get_const(world, entity) ||
        (!position && !GAME_GameTransform_get_const(world, entity))) {
        snprintf(error, size, "Mesh Surface 2D requires Geometry3D and Transform.");
        return false;
    }
    const GameMeshAsset *imported = GAME_GameMeshAsset_get_const(world, entity);
    if (imported && imported->source && *imported->source) {
        const GameMeshParts *parts = GAME_GameMeshParts_get_const(world, entity);
        arena_parts parsed;
        if (!render_arena_parts_parse(parts ? parts->parts : NULL, &parsed, error, size)) {
            return false;
        }
        if (parsed.count) {
            snprintf(error, size, "Mesh Asset and Mesh Parts cannot be combined on one visual.");
            return false;
        }
        const render_mesh_asset *asset = render_mesh_asset_load(imported->source, error, size);
        if (!asset) {
            return false;
        }
        const GameGeometry3D *shape = GAME_GameGeometry3D_get_const(world, entity);
        const GameOrientation *heading = GAME_GameOrientation_get_const(world, entity);
        const GameVisualTransform *transform = GAME_GameVisualTransform_get_const(world, entity);
        if (!position) {
            position = GAME_GameTransform_get_const(world, entity);
        }
        float angle = heading ? heading->radians : 0;
        float scale = transform ? transform->scale : 1;
        CF_V3 rotation = transform ? (CF_V3){ transform->pitch,
                                              transform->yaw,
                                              transform->roll }
                                   : (CF_V3){ 0 };
        if (!isfinite(
                position->x + position->y + shape->elevation + angle + scale +
                rotation.x + rotation.y + rotation.z + shape->width +
                shape->height + shape->depth
            ) ||
            scale < 0 || shape->width <= 0 || shape->height <= 0 || shape->depth <= 0) {
            snprintf(error, size, "Imported mesh requires finite transforms and positive dimensions.");
            return false;
        }
        if (scale == 0) {
            surface->min = surface->max = (CF_V3){ 0 };
            return true;
        }
        render_mesh_surface_part part = {
            .visual = { .part = { .primitive = GAME_PRIMITIVE_BOX,
                                  .dimensions = { shape->width,
                                                  shape->height,
                                                  shape->depth } },
                        .frame = cf_m4_from_trs(
                            (CF_V3){ position->x, shape->elevation, position->y },
                            game_visual_rotation(angle, rotation),
                            (CF_V3){ scale, scale, scale }
                        ) },
            .vertices = asset->vertices,
            .asset_identity = asset->identity,
            .borrowed = true
        };
        surface->min = (CF_V3){ FLT_MAX, FLT_MAX, FLT_MAX };
        surface->max = (CF_V3){ -FLT_MAX, -FLT_MAX, -FLT_MAX };
        CF_M4x4 model = render_arena_visual_model(&part.visual);
        for (int i = 0; i < acount(part.vertices); i++) {
            CF_V3 p = cf_m4_transform_point(model, part.vertices[i].position);
            surface->min = cf_min_v3(surface->min, p);
            surface->max = cf_max_v3(surface->max, p);
        }
        apush(surface->parts, part);
        return true;
    }
    arena_visual visuals[ARENA_PART_LIMIT];
    int count = render_arena_visuals(world, entity, position, visuals, error, size);
    if (count < 0) {
        return false;
    }
    surface->min = (CF_V3){ FLT_MAX, FLT_MAX, FLT_MAX };
    surface->max = (CF_V3){ -FLT_MAX, -FLT_MAX, -FLT_MAX };
    for (int i = 0; i < count; i++) {
        render_mesh_surface_part part = { .visual = visuals[i] };
        if (!render_mesh_surface_vertices(part.visual.part.primitive, &part.vertices, error, size)) {
            afree(part.vertices);
            render_mesh_surface_free(surface);
            return false;
        }
        CF_M4x4 model = render_arena_visual_model(&part.visual);
        for (int v = 0; v < acount(part.vertices); v++) {
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

bool render_mesh_surface_bounds(
    const ecs_world *world,
    ecs_entity entity,
    const GameTransform *position,
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
        CF_M4x4 model = render_arena_visual_model(&part->visual);
        for (int v = 0; v < acount(part->vertices); v += 3) {
            CF_V3 a = cf_m4_transform_point(model, part->vertices[v].position);
            CF_V3 b = cf_m4_transform_point(model, part->vertices[v + 1].position);
            CF_V3 c = cf_m4_transform_point(model, part->vertices[v + 2].position);
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
