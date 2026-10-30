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
    MaterialSurface3D deep = options();
    deep.recess_width = 2;
    deep.recess_depth = deep.depth;
    REQUIRE(!material_surface_mesh_build(
        material_mask_occupancy(&mask),
        deep,
        (MaterialFrame){ 0 },
        &rejected,
        error,
        sizeof error
    ));
    deep.recess_depth = -1;
    REQUIRE(!material_surface_mesh_build(
        material_mask_occupancy(&mask),
        deep,
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
static MaterialSurface3D recess_options()
{
    MaterialSurface3D style = options();
    style.depth = 12;
    style.bevel_width = 1;
    style.relief = 0;
    style.recess_width = 6;
    style.recess_depth = 4;
    return style;
}
/* Expected recess height beside a straight contour, d units into the air. */
static float recess_height(MaterialSurface3D style, float d)
{
    float x = 1 - fminf(1, d / style.recess_width);
    return style.elevation - style.bevel_width - style.recess_depth * (1 - x * x);
}
static bool recess_recedes_into_cut_air_and_keeps_wide_openings()
{
    material_mask mask = material_mask_new(40, 40, 1, false);
    REQUIRE(material_mask_carve(&mask, 0, 0, 8) > 0);
    MaterialSurface3D style = recess_options();
    render_mesh_asset mesh = { 0 };
    char error[256];
    REQUIRE(material_surface_mesh_build(
        material_mask_occupancy(&mask),
        style,
        (MaterialFrame){ 0 },
        &mesh,
        error,
        sizeof error
    ));
    REQUIRE(valid_vertices(&mesh));
    REQUIRE(mesh.min.y >= style.elevation - style.depth);
    int recessed = 0, floor = 0;
    for (int i = 0; i < acount(mesh.vertices); i++) {
        render_mesh_surface_vertex v = mesh.vertices[i];
        float r = sqrtf(v.position.x * v.position.x + v.position.z * v.position.z);
        REQUIRE(
            v.position.y > style.elevation - style.depth ||
            (v.normal.y == 0 && v.position.y == style.elevation - style.depth)
        );
        if (v.normal.y <= 0 || r > 7.5f) {
            continue;
        }
        /* Air inside the cut: never at cap height, deeper away from its rim. */
        REQUIRE(v.position.y < style.elevation - style.bevel_width + .01f);
        float d = 8 - r;
        if (d > 1.5f && d < 5) {
            REQUIRE(fabsf(v.position.y - recess_height(style, d)) < .8f);
            recessed++;
        }
        floor += v.position.y <
                 style.elevation - style.bevel_width - style.recess_depth + .5f;
    }
    REQUIRE(recessed > 8);
    REQUIRE(floor > 0);
    /* Air farther than recess_width from any solid stays open behind. */
    for (int i = 0; i < acount(mesh.indices); i += 3) {
        REQUIRE(!triangle_contains_origin(
            mesh.vertices[mesh.indices[i]].position,
            mesh.vertices[mesh.indices[i + 1]].position,
            mesh.vertices[mesh.indices[i + 2]].position
        ));
    }
    render_mesh_asset_free(&mesh);
    /* A cut narrower than the recess fills with receding rock instead. */
    material_mask narrow = material_mask_new(40, 40, 1, false);
    REQUIRE(material_mask_carve(&narrow, 0, 0, 3) > 0);
    REQUIRE(material_surface_mesh_build(
        material_mask_occupancy(&narrow),
        style,
        (MaterialFrame){ 0 },
        &mesh,
        error,
        sizeof error
    ));
    bool covered = false;
    for (int i = 0; i < acount(mesh.indices) && !covered; i += 3) {
        CF_V3 a = mesh.vertices[mesh.indices[i]].position;
        covered = triangle_contains_origin(
                      a,
                      mesh.vertices[mesh.indices[i + 1]].position,
                      mesh.vertices[mesh.indices[i + 2]].position
                  ) &&
                  a.y < style.elevation - style.bevel_width - 1;
    }
    REQUIRE(covered);
    render_mesh_asset_free(&mesh);
    material_mask_free(&narrow);
    material_mask_free(&mask);
    return true;
}
static bool recess_matches_across_chunk_seam_and_needs_halo()
{
    /* Solid only in the first chunk; the second chunk is air within reach. */
    float field[132 * 8];
    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 132; x++) {
            field[y * 132 + x] = x < 126 ? -10.f : x - 125.5f;
        }
    }
    material_terrain_chunk
        *chunks = material_terrain_chunks(field, (field_grid){ 132, 8, 1, 0, 0 });
    REQUIRE(acount(chunks) == 2);
    material_terrain_view views[2] = { { &chunks[0].mask, chunks[0].x, chunks[0].y },
                                       { &chunks[1].mask, chunks[1].x, chunks[1].y } };
    MaterialSurface3D style = recess_options();
    char error[256];
    render_mesh_asset meshes[2] = { 0 };
    material_terrain_sync_views(views, 2, 2, 0);
    REQUIRE(!material_surface_mesh_build(
        material_mask_occupancy(&chunks[1].mask),
        style,
        (MaterialFrame){ chunks[1].x, chunks[1].y, 0 },
        &meshes[1],
        error,
        sizeof error
    ));
    REQUIRE(strstr(error, "8-cell neighbor halo") != NULL);
    material_terrain_sync_views(views, 2, 8, 0);
    for (int j = 0; j < 2; j++) {
        REQUIRE(material_surface_mesh_build(
            material_mask_occupancy(&chunks[j].mask),
            style,
            (MaterialFrame){ chunks[j].x, chunks[j].y, 0 },
            &meshes[j],
            error,
            sizeof error
        ));
        REQUIRE(valid_vertices(&meshes[j]));
    }
    float seam = chunks[0].x + chunks[0].mask.width * .5f;
    int matches = 0;
    for (int i = 0; i < acount(meshes[0].vertices); i++) {
        render_mesh_surface_vertex a = meshes[0].vertices[i];
        if (fabsf(a.position.x + chunks[0].x - seam) > 1e-5f) {
            continue;
        }
        /* No invented wall where the recess continues into the neighbor. */
        REQUIRE(a.normal.y > 0);
        REQUIRE(a.position.y < style.elevation - style.bevel_width);
        for (int k = 0; k < acount(meshes[1].vertices); k++) {
            render_mesh_surface_vertex b = meshes[1].vertices[k];
            if (fabsf(b.position.x + chunks[1].x - seam) > 1e-5f ||
                fabsf(a.position.z - b.position.z) > 1e-5f) {
                continue;
            }
            REQUIRE(fabsf(a.position.y - b.position.y) < 1e-5f);
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
/* Deepest top-surface height near a gameplay point; NAN when uncovered. */
static float height_near(const render_mesh_asset *mesh, float x, float z)
{
    float found = NAN;
    for (int i = 0; i < acount(mesh->vertices); i++) {
        render_mesh_surface_vertex v = mesh->vertices[i];
        if (v.normal.y > 0 && fabsf(v.position.x - x) <= .5f &&
            fabsf(v.position.z - z) <= .5f) {
            found = isnan(found) ? v.position.y : fminf(found, v.position.y);
        }
    }
    return found;
}
static bool recess_smoothing_lets_thin_spikes_hang_in_front()
{
    /* A ceiling over open air with one two-cell lavacicle hanging below it. */
    unsigned char cells[40 * 40] = { 0 };
    for (int y = 0; y < 40; y++) {
        for (int x = 0; x < 40; x++) {
            cells[y * 40 + x] = y < 12 || (y < 22 && (x == 20 || x == 21));
        }
    }
    material_mask mask = material_mask_from_cells(40, 40, 1, cells);
    MaterialSurface3D style = recess_options();
    style.recess_width = 12;
    style.recess_depth = 6;
    render_mesh_asset exact = { 0 }, smooth = { 0 };
    char error[256];
    REQUIRE(material_surface_mesh_build(
        material_mask_occupancy(&mask),
        style,
        (MaterialFrame){ 0 },
        &exact,
        error,
        sizeof error
    ));
    style.recess_smoothing = 2;
    REQUIRE(material_surface_mesh_build(
        material_mask_occupancy(&mask),
        style,
        (MaterialFrame){ 0 },
        &smooth,
        error,
        sizeof error
    ));
    REQUIRE(valid_vertices(&smooth));
    /* Just below the spike tip: the exact recess follows the spike, while the
     * smoothed one keeps receding by distance from the ceiling. */
    float below_exact = height_near(&exact, 1, 4),
          below_smooth = height_near(&smooth, 1, 4);
    REQUIRE(!isnan(below_exact) && !isnan(below_smooth));
    REQUIRE(below_smooth < below_exact - 1);
    /* Away from the spike both agree, and the spike itself still renders. */
    REQUIRE(fabsf(height_near(&exact, -12, 4) - height_near(&smooth, -12, 4)) < .3f);
    REQUIRE(height_near(&smooth, 1, -1) > style.elevation - style.bevel_width - .01f);
    render_mesh_asset_free(&exact);
    render_mesh_asset_free(&smooth);
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
    RUN_TEST_CASE(recess_recedes_into_cut_air_and_keeps_wide_openings);
    RUN_TEST_CASE(recess_matches_across_chunk_seam_and_needs_halo);
    RUN_TEST_CASE(recess_smoothing_lets_thin_spikes_hang_in_front);
}
