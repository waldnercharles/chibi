#include "runtime/geometry/terrain_volume.h"

#include "runtime/geometry/material_surface_mesh.h"
#include "support/array.h"
#include "third_party/pico_headers/pico_unit.h"

#include <math.h>
#include <string.h>

enum
{
    SIDE = 32,
    LAYERS = 24
};
static const field_grid PLANE = { SIDE, SIDE, 1, 0, 0 };
static const float CENTER_DEPTH = 10, RADIUS = 6;
/* Deeper than any volume here, so carves reach as far as their cones. */
static const float SLAB = 64;

/* Field: air inside a sphere centered on the plane origin at CENTER_DEPTH
 * behind it, or a constant when sphere is false. Negative is solid. */
static field_program *field(bool sphere, float constant)
{
    char error[192];
    field_ir code[] = {
        { .op = FIELD_X },
        { .op = FIELD_Y },
        { .op = FIELD_Z },
        { .op = FIELD_CONSTANT, .value = CENTER_DEPTH },
        { .op = FIELD_SUBTRACT, .count = 2, .inputs = { 2, 3 } },
        { .op = FIELD_MULTIPLY, .count = 2, .inputs = { 0, 0 } },
        { .op = FIELD_MULTIPLY, .count = 2, .inputs = { 1, 1 } },
        { .op = FIELD_MULTIPLY, .count = 2, .inputs = { 4, 4 } },
        { .op = FIELD_ADD, .count = 2, .inputs = { 5, 6 } },
        { .op = FIELD_ADD, .count = 2, .inputs = { 8, 7 } },
        { .op = FIELD_CONSTANT, .value = RADIUS * RADIUS },
        /* (r^2 - d^2) / (2r) is a distance-scaled sphere field near its surface. */
        { .op = FIELD_SUBTRACT, .count = 2, .inputs = { 10, 9 } },
        { .op = FIELD_CONSTANT, .value = 2 * RADIUS },
        { .op = FIELD_DIVIDE, .count = 2, .inputs = { 11, 12 } },
        { .op = FIELD_CONSTANT, .value = constant },
    };
    return field_program_new(code, 15, sphere ? 13 : 14, NULL, 0, error, sizeof error);
}
static bool bake_front(bool sphere, float constant, int front, terrain_volume *volume)
{
    char error[192];
    field_program *program = field(sphere, constant);
    bool ok = program &&
              terrain_volume_bake(
                  program,
                  (field_context){ 0 },
                  (terrain_volume_shape){ PLANE, front, LAYERS + front, 1, false, SLAB },
                  NULL,
                  volume,
                  error,
                  sizeof error
              );
    field_program_free(program);
    return ok;
}
static bool bake(bool sphere, float constant, terrain_volume *volume)
{
    return bake_front(sphere, constant, 0, volume);
}
static void carve(const terrain_volume *volume, const unsigned char *open, float *removed)
{
    terrain_volume_removed(
        volume,
        open,
        0,
        0,
        volume->plane.width,
        volume->plane.height,
        removed
    );
}
/* The surfaces behind the plane, or the caster rock ahead of it. */
static void mesh_side(
    const terrain_volume *volume,
    const float *removed,
    int x0,
    int x1,
    bool behind,
    float elevation,
    render_mesh_asset *out
)
{
    int z0 = behind ? volume->front : 0, z1 = behind ? volume->layers : volume->front;
    *out = (render_mesh_asset){ 0 };
    if (z0 < z1) {
        terrain_volume_mesh(volume, removed, x0, 0, x1, volume->plane.height, z0, z1, elevation, (CF_V2){ 0 }, out);
    }
}
static terrain_volume_bricks all_bricks(const terrain_volume *volume)
{
    int r = volume->refine;
    return (terrain_volume_bricks){ 0,
                                    0,
                                    0,
                                    (volume->plane.width - 1) / r,
                                    (volume->plane.height - 1) / r,
                                    (volume->layers - 1) / r };
}
static float *nothing_removed(const terrain_volume *volume)
{
    static unsigned char open[SIDE * SIDE];
    static float removed[SIDE * SIDE];
    memset(open, 0, sizeof open);
    carve(volume, open, removed);
    return removed;
}
/* Sign of the winding normal against the vertex normals of a mesh's
 * triangles: +1 or -1 when every triangle agrees, 0 otherwise. */
static int winding(const render_mesh_asset *mesh)
{
    int positive = 0, negative = 0;
    for (int i = 0; i < acount(mesh->indices); i += 3) {
        render_mesh_surface_vertex a = mesh->vertices[mesh->indices[i]],
                                   b = mesh->vertices[mesh->indices[i + 1]],
                                   c = mesh->vertices[mesh->indices[i + 2]];
        CF_V3 face = cf_cross(cf_sub_v3(b.position, a.position), cf_sub_v3(c.position, a.position));
        CF_V3 normal = cf_add_v3(cf_add_v3(a.normal, b.normal), c.normal);
        float facing = cf_dot_v3(face, normal);
        positive += facing > 0;
        negative += facing < 0;
    }
    return negative == 0 && positive > 0 ? 1
         : positive == 0 && negative > 0 ? -1
                                         : 0;
}
static bool sphere_cavity_meshes_closed_and_faces_into_air()
{
    terrain_volume volume = { 0 };
    REQUIRE(bake(true, 0, &volume));
    /* Layer zero is the field's planar bake exactly. */
    char error[192];
    float planar[SIDE * SIDE];
    field_program *program = field(true, 0);
    REQUIRE(
        field_program_bake(program, (field_context){ 0 }, PLANE, planar, NULL, error, sizeof error)
    );
    field_program_free(program);
    REQUIRE(!memcmp(planar, volume.coarse, sizeof planar));
    float *removed = nothing_removed(&volume);
    render_mesh_asset mesh = { 0 };
    mesh_side(&volume, removed, 0, SIDE, true, 50, &mesh);
    REQUIRE(acount(mesh.indices) > 300);
    CF_V3 center = { 0, 50 - CENTER_DEPTH, 0 };
    for (int i = 0; i < acount(mesh.vertices); i++) {
        render_mesh_surface_vertex v = mesh.vertices[i];
        CF_V3 to_center = cf_sub_v3(center, v.position);
        float distance = sqrtf(cf_dot_v3(to_center, to_center));
        REQUIRE(fabsf(distance - RADIUS) < .6f);
        REQUIRE(cf_dot_v3(v.normal, to_center) > .9f * distance);
        REQUIRE(
            v.part_position.x == v.position.x && v.part_position.z == v.position.z
        );
    }
    /* The volume winds like MaterialSurface3D's own caps. */
    material_mask mask = material_mask_new(8, 8, 1, false);
    MaterialSurface3D style = { .enabled = true,
                                .elevation = 4,
                                .depth = 2,
                                .relief_scale = 8,
                                .spacing = 1 };
    render_mesh_asset cap = { 0 };
    REQUIRE(material_surface_mesh_build(
        material_mask_occupancy(&mask),
        style,
        (MaterialFrame){ 0 },
        &cap,
        error,
        sizeof error
    ));
    REQUIRE(winding(&mesh) != 0 && winding(&mesh) == winding(&cap));
    render_mesh_asset_free(&cap);
    material_mask_free(&mask);
    /* Two halves seam exactly into the whole. */
    render_mesh_asset left = { 0 }, right = { 0 };
    mesh_side(&volume, removed, 0, SIDE / 2, true, 50, &left);
    mesh_side(&volume, removed, SIDE / 2, SIDE, true, 50, &right);
    REQUIRE(acount(left.indices) + acount(right.indices) == acount(mesh.indices));
    int shared = 0;
    for (int i = 0; i < acount(left.vertices); i++) {
        for (int k = 0; k < acount(right.vertices); k++) {
            shared += !memcmp(&left.vertices[i].position, &right.vertices[k].position, sizeof(CF_V3));
        }
    }
    REQUIRE(shared > 0);
    render_mesh_asset_free(&left);
    render_mesh_asset_free(&right);
    render_mesh_asset_free(&mesh);
    terrain_volume_free(&volume);
    return true;
}
static bool removed_plane_carves_a_cone_and_opens_gi()
{
    terrain_volume volume = { 0 };
    REQUIRE(bake(false, -3, &volume));
    float *removed = nothing_removed(&volume);
    render_mesh_asset mesh = { 0 };
    mesh_side(&volume, removed, 0, SIDE, true, 0, &mesh);
    REQUIRE(!acount(mesh.indices));
    terrain_volume_box *boxes = NULL;
    REQUIRE(terrain_volume_boxes(&volume, removed, all_bricks(&volume), 64, 0, &boxes) == 1);
    afree(boxes);
    /* Open a disk of radius 5 on the plane, as a cut would. */
    unsigned char open[SIDE * SIDE] = { 0 };
    for (int j = 0; j < SIDE; j++) {
        for (int i = 0; i < SIDE; i++) {
            float x = i + .5f - SIDE * .5f, y = j + .5f - SIDE * .5f;
            open[j * SIDE + i] = x * x + y * y <= 25;
        }
    }
    float carved[SIDE * SIDE];
    carve(&volume, open, carved);
    REQUIRE(carved[(SIDE / 2) * SIDE + SIDE / 2] > 3);
    /* Far samples hold the carve's floor a few samples below zero. */
    REQUIRE(carved[0] < -5 && carved[0] == carved[SIDE - 1]);
    mesh_side(&volume, carved, 0, SIDE, true, 0, &mesh);
    REQUIRE(acount(mesh.indices) > 30);
    for (int i = 0; i < acount(mesh.vertices); i++) {
        CF_V3 p = mesh.vertices[i].position;
        float r = sqrtf(p.x * p.x + p.z * p.z);
        REQUIRE(r < 6.5f && -p.y < 6.5f);
        /* The cone: depth falls off one-to-one with distance from the axis. */
        REQUIRE(fabsf(-p.y - (5 - r)) < 1.6f);
    }
    REQUIRE(terrain_volume_boxes(&volume, carved, all_bricks(&volume), 64, 0, &boxes) > 1);
    for (int i = 0; i < acount(boxes); i++) {
        terrain_volume_box b = boxes[i];
        bool axis = b.min.x < 0 && b.max.x > 0 && b.min.z < 0 && b.max.z > 0 &&
                    b.max.y > -3;
        REQUIRE(!axis);
    }
    afree(boxes);
    render_mesh_asset_free(&mesh);
    terrain_volume_free(&volume);
    return true;
}
static bool open_field_closes_at_the_back_unless_open_and_rejects_oversize()
{
    terrain_volume volume = { 0 };
    REQUIRE(bake(false, 3, &volume));
    render_mesh_asset mesh = { 0 };
    mesh_side(&volume, nothing_removed(&volume), 0, SIDE, true, 0, &mesh);
    REQUIRE(acount(mesh.indices) > 0);
    for (int i = 0; i < acount(mesh.vertices); i++) {
        render_mesh_surface_vertex v = mesh.vertices[i];
        REQUIRE(-v.position.y > LAYERS - 2 && -v.position.y < LAYERS - 1);
        REQUIRE(v.normal.y > .99f);
    }
    render_mesh_asset_free(&mesh);
    terrain_volume_free(&volume);
    char error[192];
    field_program *program = field(false, 1);
    /* An open back leaves the air open onto the sky: nothing to mesh. */
    terrain_volume_shape open = { PLANE, 0, LAYERS, 1, true, SLAB };
    REQUIRE(
        terrain_volume_bake(program, (field_context){ 0 }, open, NULL, &volume, error, sizeof error)
    );
    mesh_side(&volume, nothing_removed(&volume), 0, SIDE, true, 0, &mesh);
    REQUIRE(!acount(mesh.indices));
    render_mesh_asset_free(&mesh);
    terrain_volume_free(&volume);
    terrain_volume untouched = { 0 };
    field_grid huge = { 4096, 4096, 1, 0, 0 };
    REQUIRE(!terrain_volume_bake(
        program,
        (field_context){ 0 },
        (terrain_volume_shape){ huge, 0, 4, 1, false, SLAB },
        NULL,
        &untouched,
        error,
        sizeof error
    ));
    REQUIRE(!untouched.coarse);
    field_program_free(program);
    return true;
}
static bool front_rock_casts_and_leaves_the_visible_mesh_alone()
{
    terrain_volume behind = { 0 }, both = { 0 };
    REQUIRE(bake(true, 0, &behind));
    REQUIRE(bake_front(true, 0, 8, &both));
    REQUIRE(both.front == 8 && both.layers == LAYERS + 8);
    /* The plane layer is still the planar bake. */
    REQUIRE(!memcmp(behind.coarse, both.coarse + 8 * SIDE * SIDE, SIDE * SIDE * sizeof(float)));
    float *removed = nothing_removed(&both);
    render_mesh_asset seen = { 0 }, seen_alone = { 0 }, cast = { 0 };
    mesh_side(&both, removed, 0, SIDE, true, 50, &seen);
    mesh_side(&behind, removed, 0, SIDE, true, 50, &seen_alone);
    REQUIRE(acount(seen.indices) == acount(seen_alone.indices));
    for (int i = 0; i < acount(seen.vertices); i++) {
        CF_V3 a = seen.vertices[i].position, b = seen_alone.vertices[i].position;
        CF_V3 offset = cf_sub_v3(a, b);
        REQUIRE(cf_dot_v3(offset, offset) < 1e-8f);
    }
    /* Ahead of a sphere centered behind the plane the rock is solid: no
     * caster faces, and none at all without front layers. */
    mesh_side(&both, removed, 0, SIDE, false, 50, &cast);
    REQUIRE(!acount(cast.indices));
    mesh_side(&behind, removed, 0, SIDE, false, 50, &cast);
    REQUIRE(!acount(cast.indices));
    render_mesh_asset_free(&seen);
    render_mesh_asset_free(&seen_alone);
    terrain_volume_free(&behind);
    terrain_volume_free(&both);
    /* A cut opens a double cone: caster faces appear ahead of the plane too,
     * and indirect boxes still cover the rock ahead of it. */
    terrain_volume solid = { 0 };
    REQUIRE(bake_front(false, -3, 8, &solid));
    unsigned char open[SIDE * SIDE] = { 0 };
    for (int j = 0; j < SIDE; j++) {
        for (int i = 0; i < SIDE; i++) {
            float x = i + .5f - SIDE * .5f, y = j + .5f - SIDE * .5f;
            open[j * SIDE + i] = x * x + y * y <= 25;
        }
    }
    float carved[SIDE * SIDE];
    carve(&solid, open, carved);
    mesh_side(&solid, carved, 0, SIDE, false, 0, &cast);
    REQUIRE(acount(cast.indices) > 30);
    for (int i = 0; i < acount(cast.vertices); i++) {
        REQUIRE(cast.vertices[i].position.y >= -1e-5f);
    }
    terrain_volume_box *boxes = NULL;
    terrain_volume_boxes(&solid, carved, all_bricks(&solid), 64, 0, &boxes);
    float nearest = -INFINITY;
    for (int i = 0; i < acount(boxes); i++) {
        nearest = fmaxf(nearest, boxes[i].max.y);
    }
    REQUIRE(nearest > 4);
    afree(boxes);
    render_mesh_asset_free(&cast);
    terrain_volume_free(&solid);
    return true;
}
/* Air inside an axis box behind the plane: exact faces at x = +-5, y = +-6
 * and depth 4..16, offset from the sample grid so edges fall inside cells. */
static field_program *box_field()
{
    char error[192];
    field_ir code[] = {
        { .op = FIELD_X },
        { .op = FIELD_Y },
        { .op = FIELD_Z },
        { .op = FIELD_CONSTANT, .value = 10 },
        { .op = FIELD_SUBTRACT, .count = 2, .inputs = { 2, 3 } },
        { .op = FIELD_CONSTANT, .value = 5.3f },
        { .op = FIELD_CONSTANT, .value = 6.3f },
        { .op = FIELD_BOX, .count = 4, .inputs = { 0, 1, 5, 6 } },
        { .op = FIELD_ABS, .count = 1, .inputs = { 4 } },
        { .op = FIELD_CONSTANT, .value = 6.3f },
        { .op = FIELD_SUBTRACT, .count = 2, .inputs = { 8, 9 } },
        { .op = FIELD_MAX, .count = 2, .inputs = { 7, 10 } },
        { .op = FIELD_NEGATE, .count = 1, .inputs = { 11 } },
    };
    return field_program_new(code, 13, 12, NULL, 0, error, sizeof error);
}
static bool dual_contouring_keeps_flat_faces_and_crisp_edges()
{
    char error[192];
    field_program *program = box_field();
    REQUIRE(program);
    terrain_volume volume = { 0 };
    REQUIRE(terrain_volume_bake(
        program,
        (field_context){ 0 },
        (terrain_volume_shape){ PLANE, 0, LAYERS, 1, false, SLAB },
        NULL,
        &volume,
        error,
        sizeof error
    ));
    field_program_free(program);
    render_mesh_asset mesh = { 0 };
    mesh_side(&volume, nothing_removed(&volume), 0, SIDE, true, 0, &mesh);
    REQUIRE(acount(mesh.indices) > 0);
    int on_faces = 0, corners = 0;
    for (int i = 0; i < acount(mesh.vertices); i++) {
        CF_V3 p = mesh.vertices[i].position;
        float x = fabsf(p.x) - 5.3f, y = fabsf(p.z) - 6.3f, z = fabsf(-p.y - 10) - 6.3f;
        /* Every vertex stays on the box surface within a sixth of a cell;
         * averaged surface nets vertices sag far more at edges. */
        REQUIRE(fabsf(fmaxf(x, fmaxf(y, z))) < .16f);
        on_faces++;
        /* Grid-estimated normals still round each corner by under half a cell. */
        corners += fabsf(x) < .5f && fabsf(y) < .5f && fabsf(z) < .5f;
    }
    REQUIRE(on_faces > 100);
    REQUIRE(corners >= 8);
    render_mesh_asset_free(&mesh);
    terrain_volume_free(&volume);
    return true;
}
/* Same counts, and every vertex of a lies within tolerance of one of b. */
static bool same_vertices(const render_mesh_asset *a, const render_mesh_asset *b, float tolerance)
{
    if (acount(a->vertices) != acount(b->vertices) ||
        acount(a->indices) != acount(b->indices)) {
        return false;
    }
    for (int i = 0; i < acount(a->vertices); i++) {
        bool found = false;
        for (int k = 0; k < acount(b->vertices) && !found; k++) {
            CF_V3 d = cf_sub_v3(a->vertices[i].position, b->vertices[k].position);
            found = cf_dot_v3(d, d) <= tolerance * tolerance;
        }
        if (!found) {
            return false;
        }
    }
    return true;
}
static bool refined_volume_matches_the_dense_fine_bake()
{
    enum
    {
        REFINE = 4,
        COARSE_SIDE = 17,
        COARSE_LAYERS = 9,
        FINE_SIDE = (COARSE_SIDE - 1) * REFINE + 1,
        FINE_LAYERS = (COARSE_LAYERS - 1) * REFINE + 1
    };
    char error[192];
    field_program *programs[2] = { field(true, 0), box_field() };
    for (int f = 0; f < 2; f++) {
        REQUIRE(programs[f]);
        terrain_volume dense = { 0 }, refined = { 0 };
        field_grid fine = { FINE_SIDE, FINE_SIDE, 1, 0, 0 };
        field_grid coarse = { COARSE_SIDE, COARSE_SIDE, REFINE, 0, 0 };
        REQUIRE(terrain_volume_bake(
            programs[f],
            (field_context){ 0 },
            (terrain_volume_shape){ fine, 0, FINE_LAYERS, 1, false, SLAB },
            NULL,
            &dense,
            error,
            sizeof error
        ));
        REQUIRE(terrain_volume_bake(
            programs[f],
            (field_context){ 0 },
            (terrain_volume_shape){ coarse, 0, COARSE_LAYERS, REFINE, false, SLAB },
            NULL,
            &refined,
            error,
            sizeof error
        ));
        REQUIRE(refined.plane.width == FINE_SIDE && refined.plane.height == FINE_SIDE);
        REQUIRE(refined.layers == FINE_LAYERS && refined.plane.cell_size == 1);
        /* Only cells near the surface hold fine samples. */
        int cells = (COARSE_SIDE - 1) * (COARSE_SIDE - 1) * (COARSE_LAYERS - 1);
        REQUIRE(refined.detail_count > 0 && refined.detail_count < cells / 2);
        static unsigned char open[FINE_SIDE * FINE_SIDE];
        static float removed[FINE_SIDE * FINE_SIDE],
            dense_removed[FINE_SIDE * FINE_SIDE];
        /* Uncut, then with a disk cut through the plane. */
        for (int cut = 0; cut < 2; cut++) {
            for (int j = 0; j < FINE_SIDE; j++) {
                for (int i = 0; i < FINE_SIDE; i++) {
                    float x = i - FINE_SIDE / 2, y = j - FINE_SIDE / 2;
                    open[j * FINE_SIDE + i] = cut && x * x + y * y <= 36;
                }
            }
            carve(&dense, open, dense_removed);
            carve(&refined, open, removed);
            REQUIRE(!memcmp(removed, dense_removed, sizeof removed));
            render_mesh_asset a = { 0 }, b = { 0 };
            mesh_side(&dense, dense_removed, 0, FINE_SIDE, true, 0, &a);
            mesh_side(&refined, removed, 0, FINE_SIDE, true, 0, &b);
            REQUIRE(acount(a.indices) > 300);
            REQUIRE(same_vertices(&a, &b, 1e-4f));
            render_mesh_asset_free(&a);
            render_mesh_asset_free(&b);
            terrain_volume_box *dense_boxes = NULL, *boxes = NULL;
            int dense_count = terrain_volume_boxes(
                &dense,
                dense_removed,
                all_bricks(&dense),
                1 << 20,
                0,
                &dense_boxes
            );
            int count = terrain_volume_boxes(&refined, removed, all_bricks(&refined), 1 << 20, 0, &boxes);
            /* Refined boxes are whole coarse cells; the solid they cover is
             * the dense boxes' solid, so its volume can only be smaller. */
            float dense_volume = 0, volume = 0;
            for (int i = 0; i < dense_count; i++) {
                CF_V3 e = cf_sub_v3(dense_boxes[i].max, dense_boxes[i].min);
                dense_volume += e.x * e.y * e.z;
            }
            for (int i = 0; i < count; i++) {
                CF_V3 e = cf_sub_v3(boxes[i].max, boxes[i].min);
                volume += e.x * e.y * e.z;
            }
            REQUIRE(count > 0 && volume > dense_volume * .5f && volume <= dense_volume + 1e-3f);
            afree(dense_boxes);
            afree(boxes);
        }
        terrain_volume_free(&dense);
        terrain_volume_free(&refined);
        field_program_free(programs[f]);
    }
    return true;
}
static bool slab_carve_is_local_and_meshes_in_depth_chunks()
{
    char error[192];
    terrain_volume volume = { 0 };
    REQUIRE(bake(false, -3, &volume));
    /* A carve of the changed window reproduces the whole plane exactly. */
    static unsigned char open[SIDE * SIDE];
    static float before[SIDE * SIDE], whole[SIDE * SIDE], local[SIDE * SIDE];
    memset(open, 0, sizeof open);
    carve(&volume, open, before);
    memcpy(local, before, sizeof local);
    for (int j = 10; j < 14; j++) {
        for (int i = 10; i < 14; i++) {
            open[j * SIDE + i] = 1;
        }
    }
    carve(&volume, open, whole);
    terrain_volume_removed(&volume, open, 10, 10, 14, 14, local);
    REQUIRE(!memcmp(whole, local, sizeof whole));
    /* Only layers near the plane and samples near the cut can change. */
    int k0, k1;
    REQUIRE(terrain_volume_changed_layers(&volume, before, whole, 10, 10, 14, 14, &k0, &k1));
    REQUIRE(k0 == 0 && k1 > 1 && k1 < volume.layers);
    REQUIRE(!terrain_volume_changed_layers(&volume, before, whole, 0, 0, 2, 2, &k0, &k1));
    terrain_volume_free(&volume);
    /* The slab caps the cone however wide the cut. */
    field_program *program = field(false, -3);
    REQUIRE(
        program && terrain_volume_bake(
                       program,
                       (field_context){ 0 },
                       (terrain_volume_shape){ PLANE, 0, LAYERS, 1, false, 2 },
                       NULL,
                       &volume,
                       error,
                       sizeof error
                   )
    );
    field_program_free(program);
    for (int j = 4; j < 28; j++) {
        for (int i = 4; i < 28; i++) {
            open[j * SIDE + i] = 1;
        }
    }
    carve(&volume, open, whole);
    REQUIRE(whole[16 * SIDE + 16] == 2);
    render_mesh_asset mesh = { 0 };
    mesh_side(&volume, whole, 0, SIDE, true, 0, &mesh);
    REQUIRE(acount(mesh.indices) > 0);
    for (int i = 0; i < acount(mesh.vertices); i++) {
        REQUIRE(-mesh.vertices[i].position.y <= 2.5f);
    }
    render_mesh_asset_free(&mesh);
    terrain_volume_free(&volume);
    /* Depth chunks seam exactly into the whole. */
    REQUIRE(bake(true, 0, &volume));
    float *removed = nothing_removed(&volume);
    render_mesh_asset near = { 0 }, far = { 0 };
    mesh_side(&volume, removed, 0, SIDE, true, 0, &mesh);
    terrain_volume_mesh(&volume, removed, 0, 0, SIDE, SIDE, 0, 10, 0, (CF_V2){ 0 }, &near);
    terrain_volume_mesh(&volume, removed, 0, 0, SIDE, SIDE, 10, LAYERS, 0, (CF_V2){ 0 }, &far);
    REQUIRE(acount(near.indices) && acount(far.indices));
    REQUIRE(acount(near.indices) + acount(far.indices) == acount(mesh.indices));
    int shared = 0;
    for (int i = 0; i < acount(near.vertices); i++) {
        for (int k = 0; k < acount(far.vertices); k++) {
            shared += !memcmp(&near.vertices[i].position, &far.vertices[k].position, sizeof(CF_V3));
        }
    }
    REQUIRE(shared > 0);
    render_mesh_asset_free(&near);
    render_mesh_asset_free(&far);
    render_mesh_asset_free(&mesh);
    terrain_volume_free(&volume);
    return true;
}

/* Every lattice point a change of the carve flips lies in the changed
 * range, and no change names none. */
static bool changed_lattice_holds_every_flipped_point()
{
    terrain_volume volume = { 0 };
    REQUIRE(bake(false, -3, &volume));
    static unsigned char open[SIDE * SIDE];
    static float before[SIDE * SIDE], after[SIDE * SIDE];
    memset(open, 0, sizeof open);
    carve(&volume, open, before);
    for (int j = 9; j < 17; j++) {
        for (int i = 11; i < 19; i++) {
            open[j * SIDE + i] = 1;
        }
    }
    carve(&volume, open, after);
    terrain_volume_bricks points;
    REQUIRE(terrain_volume_changed_lattice(&volume, before, after, &points));
    int flipped = 0;
    for (int k = 0; k < volume.layers; k++) {
        for (int j = 0; j < SIDE; j++) {
            for (int i = 0; i < SIDE; i++) {
                bool was = terrain_volume_density(&volume, before, i, j, k) < 0;
                bool is = terrain_volume_density(&volume, after, i, j, k) < 0;
                if (was == is) {
                    continue;
                }
                flipped++;
                REQUIRE(
                    points.x0 <= i && i < points.x1 && points.y0 <= j &&
                    j < points.y1
                );
                REQUIRE(points.z0 <= k && k < points.z1);
            }
        }
    }
    REQUIRE(flipped > 0);
    REQUIRE(!terrain_volume_changed_lattice(&volume, after, after, &points));
    terrain_volume_free(&volume);
    return true;
}

/* Every carved sample of brick (i, j, k), faces included, is solid. */
static bool brick_solid(const terrain_volume *volume, const float *removed, int i, int j, int k)
{
    int r = volume->refine;
    for (int z = k * r; z <= (k + 1) * r; z++) {
        for (int y = j * r; y <= (j + 1) * r; y++) {
            for (int x = i * r; x <= (i + 1) * r; x++) {
                if (terrain_volume_density(volume, removed, x, y, z) >= 0) {
                    return false;
                }
            }
        }
    }
    return true;
}

/* Some box, fitted at elevation zero, holds the center of brick (i, j, k). */
static bool brick_boxed(const terrain_volume *volume, const terrain_volume_box *boxes, int i, int j, int k)
{
    const field_grid *p = &volume->plane;
    float brick = volume->refine * p->cell_size;
    CF_V3 c = { p->center_x + (.5f - p->width * .5f) * p->cell_size + (i + .5f) * brick,
                volume->front * p->cell_size - (k + .5f) * brick,
                p->center_y + (.5f - p->height * .5f) * p->cell_size + (j + .5f) * brick };
    for (int n = 0; n < acount(boxes); n++) {
        CF_V3 low = boxes[n].min, high = boxes[n].max;
        if (low.x < c.x && c.x < high.x && low.y < c.y && c.y < high.y &&
            low.z < c.z && c.z < high.z) {
            return true;
        }
    }
    return false;
}

/* Coarsened boxes fit their limit and never cover air. Groups at a range's
 * far faces are narrower rather than dropped, and coarsening never leaves
 * no box while a finer span had some. */
static bool boxes_coarsen_within_the_limit_and_keep_far_bricks()
{
    terrain_volume volume = { 0 };
    REQUIRE(bake(false, -3, &volume));
    /* One open plane sample: only the four bricks around it at the plane
     * hold air, at the range's low corner. */
    static unsigned char open[SIDE * SIDE];
    static float removed[SIDE * SIDE];
    memset(open, 0, sizeof open);
    open[SIDE + 1] = 1;
    carve(&volume, open, removed);
    /* No side a power of two; span 4 leaves narrower groups on every side. */
    terrain_volume_bricks range = { 0, 0, 0, 7, 5, 3 };
    /* Uncoarsened every solid brick is boxed. Span 4 keeps two boxes, the
     * groups from brick 4 across and from brick 4 down; with room for one,
     * the larger stays rather than span 8, whose one group holds air. */
    int limits[] = { 1 << 20, 2, 1 };
    for (int l = 0; l < 3; l++) {
        terrain_volume_box *boxes = NULL;
        int count = terrain_volume_boxes(&volume, removed, range, limits[l], 0, &boxes);
        REQUIRE(count == acount(boxes) && count > 0 && count <= limits[l]);
        int air = 0;
        for (int k = range.z0; k < range.z1; k++) {
            for (int j = range.y0; j < range.y1; j++) {
                for (int i = range.x0; i < range.x1; i++) {
                    bool solid = brick_solid(&volume, removed, i, j, k);
                    bool kept = l == 0 || i >= 4 || (l == 1 && j >= 4);
                    air += !solid;
                    REQUIRE(brick_boxed(&volume, boxes, i, j, k) == (solid && kept));
                }
            }
        }
        REQUIRE(air == 4);
        afree(boxes);
    }
    terrain_volume_box *boxes = NULL;
    REQUIRE(!terrain_volume_boxes(&volume, removed, range, 0, 0, &boxes) && !boxes);
    terrain_volume_free(&volume);
    return true;
}

void test_tool_terrain_volume()
{
    RUN_TEST_CASE(sphere_cavity_meshes_closed_and_faces_into_air);
    RUN_TEST_CASE(removed_plane_carves_a_cone_and_opens_gi);
    RUN_TEST_CASE(open_field_closes_at_the_back_unless_open_and_rejects_oversize);
    RUN_TEST_CASE(front_rock_casts_and_leaves_the_visible_mesh_alone);
    RUN_TEST_CASE(dual_contouring_keeps_flat_faces_and_crisp_edges);
    RUN_TEST_CASE(refined_volume_matches_the_dense_fine_bake);
    RUN_TEST_CASE(slab_carve_is_local_and_meshes_in_depth_chunks);
    RUN_TEST_CASE(changed_lattice_holds_every_flipped_point);
    RUN_TEST_CASE(boxes_coarsen_within_the_limit_and_keep_far_bricks);
}
