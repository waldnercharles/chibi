#include "render/scene3d_geometry.h"

#include "runtime/components.h"
#include "runtime/visual_parts.h"
#include "support/xalloc.h"

#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdlib.h>

enum
{
    TRIANGLES = 8,
    VERTICES = TRIANGLES * 3
};

typedef struct
{
    CF_V3 position, normal;
} vertex;

static CF_Mesh flat_mesh(const CF_V3 points[], const int triangles[][3], int count)
{
    vertex vertices[VERTICES];
    for (int i = 0; i < count; i++) {
        CF_V3 a = points[triangles[i][0]];
        CF_V3 b = points[triangles[i][1]];
        CF_V3 c = points[triangles[i][2]];
        CF_V3 normal = cf_norm_v3(cf_cross_v3(cf_sub_v3(b, a), cf_sub_v3(c, a)));
        for (int j = 0; j < 3; j++) {
            vertices[i * 3 + j] = (vertex){ points[triangles[i][j]], normal };
        }
    }
    CF_VertexAttribute attributes[] = {
        { .name = "in_pos",
          .format = CF_VERTEX_FORMAT_FLOAT3,
          .offset = offsetof(vertex, position) },
        { .name = "in_normal",
          .format = CF_VERTEX_FORMAT_FLOAT3,
          .offset = offsetof(vertex, normal) },
    };
    CF_Mesh mesh = cf_make_mesh((int)sizeof vertices, attributes, 2, (int)sizeof(vertex));
    cf_mesh_update_vertex_data(mesh, vertices, count * 3);
    return mesh;
}

static void triangle(CF_V3 out[][3], int *count, CF_V3 a, CF_V3 b, CF_V3 c)
{
    assert(*count < SCENE3D_MESH_TRIANGLE_LIMIT);
    out[*count][0] = a;
    out[*count][1] = b;
    out[*count][2] = c;
    (*count)++;
}

static void quad(CF_V3 out[][3], int *count, CF_V3 a, CF_V3 b, CF_V3 c, CF_V3 d, bool two_sided)
{
    triangle(out, count, a, b, c);
    triangle(out, count, a, c, d);
    if (two_sided) {
        triangle(out, count, c, b, a);
        triangle(out, count, d, c, a);
    }
}

static void beam(CF_V3 out[][3], int *count, CF_V3 center, CF_V3 size)
{
    CF_V3 p[8];
    for (int i = 0; i < 8; i++) {
        p[i] = (CF_V3){ center.x + (i & 1 ? .5f : -.5f) * size.x,
                        center.y + (i & 2 ? .5f : -.5f) * size.y,
                        center.z + (i & 4 ? .5f : -.5f) * size.z };
    }
    static const int faces[][4] = { { 0, 4, 6, 2 }, { 1, 3, 7, 5 },
                                    { 0, 1, 5, 4 }, { 2, 6, 7, 3 },
                                    { 0, 2, 3, 1 }, { 4, 5, 7, 6 } };
    for (int i = 0; i < 6; i++) {
        quad(out, count, p[faces[i][0]], p[faces[i][1]], p[faces[i][2]], p[faces[i][3]], false);
    }
}

int render_scene3d_mesh_triangles(int primitive, CF_V3 out[SCENE3D_MESH_TRIANGLE_LIMIT][3])
{
    int count = 0;
    switch (primitive) {
        case PRIMITIVE_BOX_FRAME: {
            Geometry3D shape = { .primitive = primitive, .width = 1, .height = 1, .depth = 1 };
            for (int i = 0; i < visual_part_count(primitive); i++) {
                visual_part part = visual_part_at(shape, i);
                beam(out, &count, part.center, part.dimensions);
            }
            break;
        }
        case PRIMITIVE_CHEVRON_PYRAMID:
            for (int face = 0; face < 4; face++) {
                /* Filled NeonSwarm chevron faces share the +X pyramid tip.
                 * The tip-to-notch diagonal preserves the concave rear cutout. */
                CF_V3 p[4] = { { .5f, 0, 0 },
                               { -.5f, .5f, -.5f },
                               { -.3f, .4f, 0 },
                               { -.5f, .5f, .5f } };
                float angle = face * 1.5707963268f, c = cosf(angle), s = sinf(angle);
                for (int i = 0; i < 4; i++) {
                    float y = p[i].y;
                    p[i].y = c * y - s * p[i].z;
                    p[i].z = s * y + c * p[i].z;
                }
                quad(out, &count, p[0], p[1], p[2], p[3], true);
            }
            break;
        case PRIMITIVE_TRIANGLE:
        case PRIMITIVE_TRIANGLE_OUTLINE: {
            /* Unit width/depth with the triangle centroid at its rotation origin. */
            CF_V3 p[] = { { 2.0f / 3, 0, 0 },
                          { -1.0f / 3, 0, .5f },
                          { -1.0f / 3, 0, -.5f } };
            if (primitive == PRIMITIVE_TRIANGLE) {
                triangle(out, &count, p[0], p[1], p[2]);
                triangle(out, &count, p[2], p[1], p[0]);
                break;
            }
            CF_V3 inner[3];
            for (int i = 0; i < 3; i++) {
                inner[i] = cf_mul_v3_f(p[i], .76f);
            }
            for (int i = 0; i < 3; i++) {
                int j = (i + 1) % 3;
                quad(out, &count, p[i], p[j], inner[j], inner[i], true);
            }
            break;
        }
        default: break;
    }
    return count;
}

CF_Mesh render_scene3d_polygon_mesh(int primitive)
{
    CF_V3 triangles[SCENE3D_MESH_TRIANGLE_LIMIT][3];
    int count = render_scene3d_mesh_triangles(primitive, triangles);
    assert(count > 0);
    vertex vertices[SCENE3D_MESH_TRIANGLE_LIMIT * 3];
    for (int i = 0; i < count; i++) {
        CF_V3 normal = cf_norm_v3(cf_cross_v3(
            cf_sub_v3(triangles[i][1], triangles[i][0]),
            cf_sub_v3(triangles[i][2], triangles[i][0])
        ));
        for (int j = 0; j < 3; j++) {
            vertices[i * 3 + j] = (vertex){ triangles[i][j], normal };
        }
    }
    CF_VertexAttribute attributes[] = {
        { .name = "in_pos",
          .format = CF_VERTEX_FORMAT_FLOAT3,
          .offset = offsetof(vertex, position) },
        { .name = "in_normal",
          .format = CF_VERTEX_FORMAT_FLOAT3,
          .offset = offsetof(vertex, normal) },
    };
    CF_Mesh mesh = cf_make_mesh(count * 3 * (int)sizeof(vertex), attributes, 2, (int)sizeof(vertex));
    cf_mesh_update_vertex_data(mesh, vertices, count * 3);
    return mesh;
}

CF_Mesh render_scene3d_prism_mesh()
{
    /* A triangular footprint pointing +X, extruded along Y; CCW from outside. */
    static const CF_V3 points[] = {
        { -0.5f, -0.5f, -0.5f }, { 0.5f, -0.5f, 0 }, { -0.5f, -0.5f, 0.5f },
        { -0.5f, 0.5f, -0.5f },  { 0.5f, 0.5f, 0 },  { -0.5f, 0.5f, 0.5f },
    };
    static const int triangles[TRIANGLES][3] = {
        { 0, 1, 2 }, { 3, 5, 4 }, { 0, 3, 4 }, { 0, 4, 1 },
        { 1, 4, 5 }, { 1, 5, 2 }, { 2, 5, 3 }, { 2, 3, 0 },
    };
    return flat_mesh(points, triangles, TRIANGLES);
}

CF_Mesh render_scene3d_sphere_mesh()
{
    enum
    {
        STACKS = 48,
        SLICES = 96,
        COUNT = STACKS * SLICES * 6
    };
    vertex *vertices = xmalloc(COUNT * sizeof *vertices);
    int n = 0;
    for (int i = 0; i < STACKS; i++) {
        for (int j = 0; j < SLICES; j++) {
            CF_V3 corner[4];
            for (int k = 0; k < 4; k++) {
                float latitude = CF_PI * ((float)(i + (k >> 1)) / STACKS - .5f);
                float longitude = 2 * CF_PI * (float)(j + ((k + (k >> 1)) & 1)) / SLICES;
                corner[k] = (CF_V3){ cosf(latitude) * cosf(longitude),
                                     sinf(latitude),
                                     cosf(latitude) * sinf(longitude) };
            }
            /* Corners run around the cell; both triangles face outward. */
            static const int order[6] = { 0, 2, 1, 0, 3, 2 };
            for (int k = 0; k < 6; k++) {
                CF_V3 p = corner[order[k]];
                vertices[n++] = (vertex){ cf_mul_v3_f(p, .5f), p };
            }
        }
    }
    CF_VertexAttribute attributes[] = {
        { .name = "in_pos",
          .format = CF_VERTEX_FORMAT_FLOAT3,
          .offset = offsetof(vertex, position) },
        { .name = "in_normal",
          .format = CF_VERTEX_FORMAT_FLOAT3,
          .offset = offsetof(vertex, normal) },
    };
    CF_Mesh mesh = cf_make_mesh(COUNT * (int)sizeof(vertex), attributes, 2, (int)sizeof(vertex));
    cf_mesh_update_vertex_data(mesh, vertices, COUNT);
    free(vertices);
    return mesh;
}

CF_Mesh render_scene3d_octahedron_mesh()
{
    static const CF_V3 points[] = {
        { 0.5f, 0, 0 },  { 0, 0, 0.5f }, { -0.5f, 0, 0 },
        { 0, 0, -0.5f }, { 0, 0.5f, 0 }, { 0, -0.5f, 0 },
    };
    static const int triangles[TRIANGLES][3] = {
        { 4, 1, 0 }, { 4, 2, 1 }, { 4, 3, 2 }, { 4, 0, 3 },
        { 5, 0, 1 }, { 5, 1, 2 }, { 5, 2, 3 }, { 5, 3, 0 },
    };
    return flat_mesh(points, triangles, TRIANGLES);
}

CF_Mesh render_scene3d_tetrahedron_mesh()
{
    static const CF_V3 points[] = {
        { -0.5f, -0.5f, -0.5f },
        { 0.5f, -0.5f, -0.5f },
        { -0.5f, 0.5f, -0.5f },
        { -0.5f, -0.5f, 0.5f },
    };
    static const int triangles[][3] = { { 0, 2, 1 }, { 0, 1, 3 }, { 0, 3, 2 }, { 1, 2, 3 } };
    return flat_mesh(points, triangles, 4);
}
