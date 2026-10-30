#include "render/scene3d_terrain.h"
#include "render/scene3d_terrain_volume.h"
#include "runtime/geometry/terrain_volume.h"
#include "runtime/task_pool.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "third_party/pico_headers/pico_unit.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* A bay the size of Voidweaver's basalt cave: 1100 x 760 units, 360 deep,
 * voxel 2 refined from 8, carved at most 64 units from the plane. Cuts are
 * timed through the carve and the scene3d preparation (chunk re-mesh and
 * slab box refit); run it from a release build to compare against the frame
 * budget. */
enum
{
    COARSE_WIDTH = 139,
    COARSE_HEIGHT = 96,
    COARSE_DEPTH = 46,
    REFINE = 4
};
static const float COARSE_CELL = 8, ELEVATION = 8, SLAB = 64;

static double clock_ms()
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return now.tv_sec * 1000.0 + now.tv_nsec * 1e-6;
}

/* Winding noise tunnels that close toward the front and back of the bay;
 * negative is solid. */
static field_program *cave_field()
{
    char error[192];
    field_ir code[] = {
        { .op = FIELD_X },
        { .op = FIELD_Y },
        { .op = FIELD_Z },
        { .op = FIELD_CONSTANT, .value = .012f },
        { .op = FIELD_MULTIPLY, .count = 2, .inputs = { 0, 3 } },
        { .op = FIELD_MULTIPLY, .count = 2, .inputs = { 1, 3 } },
        { .op = FIELD_CONSTANT, .value = 0 },
        { .op = FIELD_NOISE, .count = 3, .inputs = { 4, 5, 6 } },
        { .op = FIELD_CONSTANT, .value = .5f },
        { .op = FIELD_SUBTRACT, .count = 2, .inputs = { 7, 8 } },
        { .op = FIELD_CONSTANT, .value = 120 },
        { .op = FIELD_MULTIPLY, .count = 2, .inputs = { 9, 10 } },
        /* Depth closure: ((z - 150) / 12)^2 / 4. */
        { .op = FIELD_CONSTANT, .value = 150 },
        { .op = FIELD_SUBTRACT, .count = 2, .inputs = { 2, 12 } },
        { .op = FIELD_CONSTANT, .value = 1.f / 12 },
        { .op = FIELD_MULTIPLY, .count = 2, .inputs = { 13, 14 } },
        { .op = FIELD_MULTIPLY, .count = 2, .inputs = { 15, 15 } },
        { .op = FIELD_CONSTANT, .value = .25f },
        { .op = FIELD_MULTIPLY, .count = 2, .inputs = { 16, 17 } },
        { .op = FIELD_SUBTRACT, .count = 2, .inputs = { 11, 18 } },
    };
    int count = (int)(sizeof code / sizeof code[0]);
    return field_program_new(code, count, count - 1, NULL, 0, error, sizeof error);
}

typedef struct
{
    terrain_volume volume;
    unsigned char *open;
    float *removed;
    render_scene3d_terrain_volume *volumes;
    task_pool *pool;
    uint64_t revision;
    /* Plane samples opened since the last carve. */
    int x0, y0, x1, y1;
} bay;

static bool bay_bake(bay *b, task_pool *pool)
{
    char error[256];
    field_program *program = cave_field();
    field_grid plane = { COARSE_WIDTH, COARSE_HEIGHT, COARSE_CELL, 0, 0 };
    task_pool_begin(pool, true);
    bool ok = program &&
              terrain_volume_bake(
                  program,
                  (field_context){ 0 },
                  (terrain_volume_shape){ plane, 0, COARSE_DEPTH, REFINE, false, SLAB },
                  task_pool_tasks(pool),
                  &b->volume,
                  error,
                  sizeof error
              );
    task_pool_end(pool);
    field_program_free(program);
    if (!ok) {
        return false;
    }
    size_t count = (size_t)b->volume.plane.width * b->volume.plane.height;
    b->open = xcalloc(count, 1);
    b->removed = xmalloc(count * sizeof *b->removed);
    b->volumes = render_scene3d_terrain_volume_new();
    b->pool = pool;
    b->x1 = b->volume.plane.width;
    b->y1 = b->volume.plane.height;
    return true;
}

static void bay_free(bay *b)
{
    render_scene3d_terrain_volume_free(b->volumes);
    free(b->removed);
    free(b->open);
    terrain_volume_free(&b->volume);
}

/* Center of a fine plane sample whose coarse surroundings within radius are
 * all solid at the plane, scanning from the plane's middle. */
static bool solid_site(const terrain_volume *v, float radius, int *x, int *y)
{
    int cw = COARSE_WIDTH, reach = (int)ceilf(radius / COARSE_CELL) + 1;
    int front = v->front / v->refine;
    const float *slice = v->coarse + (size_t)front * cw * COARSE_HEIGHT;
    for (int j = COARSE_HEIGHT / 2; j < COARSE_HEIGHT - reach; j++) {
        for (int i = COARSE_WIDTH / 2; i < COARSE_WIDTH - reach; i++) {
            bool solid = true;
            for (int dj = -reach; dj <= reach && solid; dj++) {
                for (int di = -reach; di <= reach && solid; di++) {
                    solid = slice[(j + dj) * cw + i + di] < 0;
                }
            }
            if (solid) {
                *x = i * v->refine;
                *y = j * v->refine;
                return true;
            }
        }
    }
    return false;
}

static void open_disk(bay *b, int cx, int cy, float radius)
{
    int width = b->volume.plane.width, height = b->volume.plane.height;
    float cell = b->volume.plane.cell_size, reach = radius / cell;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            float dx = (float)(x - cx), dy = (float)(y - cy);
            if (dx * dx + dy * dy <= reach * reach) {
                b->open[(size_t)y * width + x] = 1;
                b->x0 = x < b->x0 ? x : b->x0;
                b->y0 = y < b->y0 ? y : b->y0;
                b->x1 = x + 1 > b->x1 ? x + 1 : b->x1;
                b->y1 = y + 1 > b->y1 ? y + 1 : b->y1;
            }
        }
    }
}

typedef struct
{
    double removed, mesh;
    int rebuilds, box_count;
} cut_timing;

/* The carve of what opened since the last one, then one scene3d
 * preparation. */
static cut_timing bay_sync(bay *b)
{
    cut_timing t = { 0 };
    double start = clock_ms();
    terrain_volume_removed(&b->volume, b->open, b->x0, b->y0, b->x1, b->y1, b->removed);
    t.removed = clock_ms() - start;
    b->x0 = b->volume.plane.width;
    b->y0 = b->volume.plane.height;
    b->x1 = b->y1 = 0;
    materials_depth_volume view = { &b->volume, b->removed, 1, ++b->revision };
    start = clock_ms();
    render_scene3d_terrain_volume_begin(b->volumes);
    render_scene3d_volume_surface surface = render_scene3d_terrain_volume_get(
        b->volumes,
        view,
        ELEVATION,
        (CF_V2){ 0 },
        b->pool
    );
    render_scene3d_terrain_volume_end(b->volumes);
    t.mesh = clock_ms() - start;
    t.rebuilds = render_scene3d_terrain_volume_rebuilds(b->volumes);
    t.box_count = surface.box_count;
    return t;
}

static void report(const char *name, cut_timing t)
{
    printf(
        "terrain cut %-10s carve %7.2f ms, prepare %8.2f ms (%4d chunks, "
        "%4d boxes)\n",
        name,
        t.removed,
        t.mesh,
        t.rebuilds,
        t.box_count
    );
}

TEST_CASE(terrain_cut_benchmark_bay)
{
    task_pool *pool = task_pool_acquire();
    bay b = { 0 };
    REQUIRE(bay_bake(&b, pool));
    int side = RENDER_TERRAIN_VOLUME_CHUNK;
    int chunks = (b.volume.plane.width + side - 1) / side *
                 ((b.volume.plane.height + side - 1) / side) *
                 ((b.volume.layers + side - 1) / side);
    cut_timing first = bay_sync(&b);
    report("load", first);
    /* World screen light samples the carve on the coarse lattice. */
    double start = clock_ms();
    float lattice = 0;
    int r = b.volume.refine;
    for (int k = 0; k < b.volume.layers; k += r) {
        for (int j = 0; j < b.volume.plane.height; j += r) {
            for (int i = 0; i < b.volume.plane.width; i += r) {
                lattice += terrain_volume_density(&b.volume, b.removed, i, j, k) < 0;
            }
        }
    }
    printf("terrain cut lattice  %.2f ms for the coarse carve (%.0f solid)\n", clock_ms() - start, lattice);
    REQUIRE(first.rebuilds == chunks && first.box_count > 0);
    int x, y;
    REQUIRE(solid_site(&b.volume, 40, &x, &y));
    size_t samples = (size_t)b.volume.plane.width * b.volume.plane.height;
    float *kept = xmalloc(samples * sizeof *kept);
    memcpy(kept, b.removed, samples * sizeof *kept);
    open_disk(&b, x, y, 4);
    cut_timing hit = bay_sync(&b);
    report("first hit", hit);
    /* World screen light relays only the lattice points the hit can flip. */
    start = clock_ms();
    terrain_volume_bricks points;
    int relaid = 0;
    if (terrain_volume_changed_lattice(&b.volume, kept, b.removed, &points)) {
        for (int k = points.z0; k < points.z1; k++) {
            for (int j = points.y0; j < points.y1; j++) {
                for (int i = points.x0; i < points.x1; i++) {
                    relaid += terrain_volume_density(&b.volume, b.removed, i * r, j * r, k * r) <
                              0;
                }
            }
        }
    }
    printf("terrain cut relay    %.2f ms for the hit's lattice window (%d solid)\n", clock_ms() - start, relaid);
    free(kept);
    /* A hit reshapes only the slab chunks around it. */
    REQUIRE(hit.rebuilds > 0 && hit.rebuilds <= 4);
    open_disk(&b, x + 6, y, 4);
    cut_timing next = bay_sync(&b);
    report("next hit", next);
    REQUIRE(next.rebuilds > 0 && next.rebuilds <= 4);
    open_disk(&b, x, y + 12, 34);
    cut_timing large = bay_sync(&b);
    report("large cut", large);
    REQUIRE(large.rebuilds > 0 && large.rebuilds <= 9);
    bay_free(&b);
    task_pool_release(pool);
    return true;
}

void test_tool_terrain_cut_benchmark()
{
    RUN_TEST_CASE(terrain_cut_benchmark_bay);
}
