#include "runtime/geometry/material_surface_mesh.h"

#include "runtime/material_terrain.h"
#include "support/array.h"
#include "third_party/pico_headers/pico_unit.h"

#include <math.h>
#include <string.h>

static MaterialSurface3D options()
{
    return (MaterialSurface3D){ .enabled = true,
                                .elevation = 12,
                                .depth = 8,
                                .bevel_width = 2,
                                .relief = 1,
                                .relief_scale = 7,
                                .spacing = 1,
                                .seed = 52 };
}
static bool build(const material_mask *mask, MaterialFrame frame, render_mesh_asset *mesh)
{
    char error[256] = { 0 };
    return material_surface_mesh_build(material_mask_occupancy(mask), options(), frame, mesh, error, sizeof error);
}
static bool plane_height(void *context, CF_V2 point, float *height)
{
    (void)context;
    *height = .02f * point.x - .03f * point.y;
    return true;
}
static bool triangle_contains_origin(CF_V3 a, CF_V3 b, CF_V3 c)
{
    float area = (b.x - a.x) * (c.z - a.z) - (b.z - a.z) * (c.x - a.x);
    if (fabsf(area) < 1e-8f) {
        return false;
    }
    float u = (b.x * c.z - b.z * c.x) / area;
    float v = (c.x * a.z - c.z * a.x) / area;
    float w = (a.x * b.z - a.z * b.x) / area;
    return u >= 0 && v >= 0 && w >= 0;
}
static bool valid_vertices(const render_mesh_asset *mesh)
{
    for (int i = 0; i < acount(mesh->vertices); i++) {
        render_mesh_surface_vertex v = mesh->vertices[i];
        float n = v.normal.x * v.normal.x + v.normal.y * v.normal.y +
                  v.normal.z * v.normal.z;
        if (!isfinite(v.position.y) || fabsf(n - 1) > 1e-4f) {
            return false;
        }
    }
    return true;
}
static bool relief_and_cut_are_actual_geometry()
{
    material_mask mask = material_mask_new(12, 12, 1, false);
    render_mesh_asset intact = { 0 }, cut = { 0 };
    REQUIRE(build(&mask, (MaterialFrame){ 0 }, &intact));
    REQUIRE(valid_vertices(&intact));
    REQUIRE(intact.max.y - intact.min.y > 1);
    int bottom_vertices = 0;
    for (int i = 0; i < acount(intact.vertices); i++) {
        render_mesh_surface_vertex v = intact.vertices[i];
        if (v.normal.y == 0 && v.position.y == options().elevation - options().depth) {
            REQUIRE(v.part_position.y == v.position.y);
            bottom_vertices++;
        }
    }
    REQUIRE(bottom_vertices > 0);
    material_mask_carve(&mask, 0, 0, 2.5f);
    REQUIRE(build(&mask, (MaterialFrame){ 0 }, &cut));
    REQUIRE(valid_vertices(&cut));
    for (int i = 0; i < acount(cut.vertices); i++) {
        CF_V3 p = cut.vertices[i].position;
        REQUIRE(p.x * p.x + p.z * p.z > 3);
    }
    for (int i = 0; i < acount(cut.indices); i += 3) {
        REQUIRE(!triangle_contains_origin(
            cut.vertices[cut.indices[i]].position,
            cut.vertices[cut.indices[i + 1]].position,
            cut.vertices[cut.indices[i + 2]].position
        ));
    }
    REQUIRE(acount(cut.indices) != acount(intact.indices));
    render_mesh_asset_free(&intact);
    render_mesh_asset_free(&cut);
    material_mask_free(&mask);
    return true;
}
static bool neighbor_has_matching_height_and_no_wall()
{
    float field[130 * 4];
    for (int i = 0; i < 130 * 4; i++) {
        field[i] = -10;
    }
    material_terrain_chunk
        *chunks = material_terrain_chunks(field, (field_grid){ 130, 4, 1, 0, 0 });
    REQUIRE(acount(chunks) == 2);
    material_terrain_view views[2] = { { &chunks[0].mask, chunks[0].x, chunks[0].y },
                                       { &chunks[1].mask, chunks[1].x, chunks[1].y } };
    material_terrain_sync_views(views, 2, 4, 0);
    render_mesh_asset meshes[2] = { 0 };
    material_surface_height height = { plane_height, NULL };
    char error[256];
    for (int j = 0; j < 2; j++) {
        REQUIRE(material_surface_mesh_build_height(
            material_mask_occupancy(&chunks[j].mask),
            options(),
            (MaterialFrame){ chunks[j].x, chunks[j].y, 0 },
            &height,
            &meshes[j],
            error,
            sizeof error
        ));
    }
    float seam = chunks[0].x + chunks[0].mask.width * .5f;
    int matches = 0;
    for (int i = 0; i < acount(meshes[0].vertices); i++) {
        render_mesh_surface_vertex a = meshes[0].vertices[i];
        if (fabsf(a.position.x + chunks[0].x - seam) > 1e-5f ||
            fabsf(a.position.z) >= 1.99f) {
            continue;
        }
        REQUIRE(a.normal.y > 0);
        for (int k = 0; k < acount(meshes[1].vertices); k++) {
            render_mesh_surface_vertex b = meshes[1].vertices[k];
            if (fabsf(b.position.x + chunks[1].x - seam) > 1e-5f ||
                fabsf(a.position.z - b.position.z) > 1e-5f) {
                continue;
            }
            REQUIRE(fabsf(a.position.y - b.position.y) < 1e-5f);
            REQUIRE(fabsf(a.normal.x - b.normal.x) < .001f);
            matches++;
            break;
        }
    }
    REQUIRE(matches > 0);
    for (int i = 0; i < 2; i++) {
        render_mesh_asset_free(&meshes[i]);
    }
    material_terrain_chunks_free(chunks);
    return true;
}
static bool authored_height_plane_uses_retained_frame_and_normals()
{
    material_mask mask = material_mask_new(12, 12, 1, false);
    render_mesh_asset mesh = { 0 };
    MaterialSurface3D style = options();
    style.bevel_width = 0;
    MaterialFrame frame = { 30, 10, .2f };
    material_surface_height height = { plane_height, NULL };
    char error[256];
    REQUIRE(material_surface_mesh_build_height(
        material_mask_occupancy(&mask),
        style,
        frame,
        &height,
        &mesh,
        error,
        sizeof error
    ));
    float dx = .02f * cosf(frame.radians) - .03f * sinf(frame.radians);
    float dz = -.02f * sinf(frame.radians) - .03f * cosf(frame.radians);
    float length = sqrtf(dx * dx + dz * dz + 1);
    int top_vertices = 0;
    for (int i = 0; i < acount(mesh.vertices); i++) {
        render_mesh_surface_vertex v = mesh.vertices[i];
        if (v.normal.y <= 0) {
            continue;
        }
        float expected;
        REQUIRE(plane_height(NULL, (CF_V2){ v.part_position.x, v.part_position.z }, &expected));
        REQUIRE(fabsf(v.position.y - style.elevation - expected) < 1e-5f);
        REQUIRE(fabsf(v.normal.x + dx / length) < .0002f);
        REQUIRE(fabsf(v.normal.z + dz / length) < .0002f);
        top_vertices++;
    }
    REQUIRE(top_vertices > 0);
    material_mask_carve(&mask, 0, 0, 2.5f);
    render_mesh_asset_free(&mesh);
    REQUIRE(material_surface_mesh_build_height(
        material_mask_occupancy(&mask),
        style,
        frame,
        &height,
        &mesh,
        error,
        sizeof error
    ));
    for (int i = 0; i < acount(mesh.indices); i += 3) {
        REQUIRE(!triangle_contains_origin(
            mesh.vertices[mesh.indices[i]].position,
            mesh.vertices[mesh.indices[i + 1]].position,
            mesh.vertices[mesh.indices[i + 2]].position
        ));
    }
    REQUIRE(valid_vertices(&mesh));
    render_mesh_asset_free(&mesh);
    material_mask_free(&mask);
    return true;
}
typedef struct
{
    float value;
    int calls;
    bool fail;
} bad_height;
static bool constant_height(void *context, CF_V2 point, float *height)
{
    (void)point;
    *height = *(const float *)context;
    return true;
}
static bool authored_displacement_stays_inside_depth_envelope()
{
    material_mask mask = material_mask_new(12, 12, 1, false);
    material_mask_carve(&mask, 0, 0, 2.5f);
    float values[2] = { -7.5f, 7.5f };
    char error[256];
    for (int j = 0; j < 2; j++) {
        render_mesh_asset mesh = { 0 };
        material_surface_height height = { constant_height, &values[j] };
        REQUIRE(material_surface_mesh_build_height(
            material_mask_occupancy(&mask),
            options(),
            (MaterialFrame){ 0 },
            &height,
            &mesh,
            error,
            sizeof error
        ));
        REQUIRE(valid_vertices(&mesh));
        REQUIRE(mesh.min.y >= options().elevation - options().depth);
        REQUIRE(mesh.max.y < options().elevation + options().depth);
        int top_vertices = 0;
        for (int i = 0; i < acount(mesh.vertices); i++) {
            render_mesh_surface_vertex v = mesh.vertices[i];
            if (v.normal.y <= 0) {
                continue;
            }
            REQUIRE(v.position.y > options().elevation - options().depth);
            top_vertices++;
        }
        REQUIRE(top_vertices > 0);
        render_mesh_asset_free(&mesh);
    }
    material_mask_free(&mask);
    return true;
}
static bool rejected_height(void *context, CF_V2 point, float *height)
{
    (void)point;
    bad_height *bad = context;
    if (++bad->calls < 12) {
        *height = 0;
        return true;
    }
    *height = bad->value;
    return !bad->fail;
}
static bool height_sampling_failures_preserve_previous_output()
{
    material_mask mask = material_mask_new(4, 4, 1, false);
    render_mesh_asset mesh = { 0 };
    REQUIRE(build(&mask, (MaterialFrame){ 0 }, &mesh));
    render_mesh_asset previous = mesh;
    char error[256];
    bad_height cases[] = { { NAN, 0, false },
                           { INFINITY, 0, false },
                           { 8, 0, false },
                           { -8, 0, false },
                           { 0, 0, true } };
    for (int i = 0; i < 5; i++) {
        material_surface_height height = { rejected_height, &cases[i] };
        REQUIRE(!material_surface_mesh_build_height(
            material_mask_occupancy(&mask),
            options(),
            (MaterialFrame){ 0 },
            &height,
            &mesh,
            error,
            sizeof error
        ));
        REQUIRE(strstr(error, "height sampler") != NULL);
        REQUIRE(!memcmp(&mesh, &previous, sizeof mesh));
    }
    render_mesh_asset_free(&mesh);
    material_mask_free(&mask);
    return true;
}
static bool stable_frame_and_rejected_options()
{
    material_mask mask = material_mask_new(4, 4, 1, false);
    render_mesh_asset a = { 0 }, b = { 0 };
    char error[256];
    REQUIRE(build(&mask, (MaterialFrame){ 30, 10, .2f }, &a));
    REQUIRE(build(&mask, (MaterialFrame){ 30, 10, .2f }, &b));
    REQUIRE(acount(a.vertices) == acount(b.vertices));
    for (int i = 0; i < acount(a.vertices); i++) {
        render_mesh_surface_vertex v = a.vertices[i];
        float c = cosf(.2f), s = sinf(.2f);
        REQUIRE(
            fabsf(v.part_position.x - (30 + c * v.position.x - s * v.position.z)) < 1e-5f
        );
        REQUIRE(
            fabsf(v.part_position.z - (10 + s * v.position.x + c * v.position.z)) < 1e-5f
        );
        REQUIRE(v.part_position.y == v.position.y);
        REQUIRE(v.part_from_object[0][0] == c);
        REQUIRE(v.part_from_object[0][2] == -s);
        REQUIRE(v.part_from_object[2][0] == s);
        REQUIRE(v.part_from_object[2][2] == c);
        REQUIRE(v.part_from_object[1][1] == 1);
    }
    REQUIRE(
        !memcmp(a.vertices, b.vertices, (size_t)acount(a.vertices) * sizeof *a.vertices)
    );
    MaterialSurface3D bad = options();
    bad.spacing = 0;
    render_mesh_asset rejected = { 0 };
    REQUIRE(!material_surface_mesh_build(
        material_mask_occupancy(&mask),
        bad,
        (MaterialFrame){ 0 },
        &rejected,
        error,
        sizeof error
    ));
    REQUIRE(!rejected.vertices && !rejected.indices);
    render_mesh_asset_free(&a);
    render_mesh_asset_free(&b);
    material_mask_free(&mask);
    return true;
}
void test_tool_material_surface_mesh()
{
    RUN_TEST_CASE(relief_and_cut_are_actual_geometry);
    RUN_TEST_CASE(neighbor_has_matching_height_and_no_wall);
    RUN_TEST_CASE(stable_frame_and_rejected_options);
    RUN_TEST_CASE(authored_height_plane_uses_retained_frame_and_normals);
    RUN_TEST_CASE(height_sampling_failures_preserve_previous_output);
    RUN_TEST_CASE(authored_displacement_stays_inside_depth_envelope);
}
