#include "runtime/material_flow.h"

#include "support/array.h"
#include "support/cos_sin.h"
#include "support/exponential.h"
#include "support/xalloc.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum
{
    LEFT,
    RIGHT,
    DOWN,
    UP,
    HALO_RADIUS = 2,
    HALO_SIDE = 2 * HALO_RADIUS + 1,
    PRESSURE_ITERATIONS = 32
};
typedef struct
{
    int x, y, adjacent[4];
    /* X velocity lives on the left face, Y velocity on the lower face. */
    fluid_vec velocity, source, weight, heat, boundary;
    float pressure, divergence, curl;
    bool solid;
} flow_cell;
typedef struct
{
    flow_cell *cells;
    int *slots;
    int capacity;
} flow_grid;
typedef struct
{
    int cell, adjacent[4], neighbors;
    float divergence;
} pressure_row;
struct material_flow
{
    flow_grid grid, previous;
    pressure_row *pressure_rows[2];
    float *pressures;
};

material_flow *material_flow_new()
{
    return xcalloc(1, sizeof(material_flow));
}

void material_flow_clear(material_flow *flow)
{
    aclear(flow->grid.cells);
    aclear(flow->previous.cells);
    aclear(flow->pressure_rows[0]);
    aclear(flow->pressure_rows[1]);
    aclear(flow->pressures);
    if (flow->grid.capacity) {
        memset(flow->grid.slots, 0, (size_t)flow->grid.capacity * sizeof(int));
    }
    if (flow->previous.capacity) {
        memset(flow->previous.slots, 0, (size_t)flow->previous.capacity * sizeof(int));
    }
}

void material_flow_free(material_flow *flow)
{
    afree(flow->grid.cells);
    afree(flow->previous.cells);
    free(flow->grid.slots);
    free(flow->previous.slots);
    afree(flow->pressure_rows[0]);
    afree(flow->pressure_rows[1]);
    afree(flow->pressures);
    free(flow);
}

static unsigned bucket(const flow_grid *grid, int x, int y)
{
    return ((unsigned)x * 73856093u ^ (unsigned)y * 19349663u) & (grid->capacity - 1);
}

static int find(const flow_grid *grid, int x, int y)
{
    if (!acount(grid->cells)) {
        return 0;
    }
    unsigned slot = bucket(grid, x, y);
    while (grid->slots[slot]) {
        int i = grid->slots[slot];
        if (grid->cells[i].x == x && grid->cells[i].y == y) {
            return i;
        }
        slot = (slot + 1) & (grid->capacity - 1);
    }
    return 0;
}

static int insert(flow_grid *grid, int x, int y)
{
    unsigned slot = bucket(grid, x, y);
    while (grid->slots[slot]) {
        int id = grid->slots[slot];
        flow_cell *cell = &grid->cells[id];
        if (cell->x == x && cell->y == y) {
            return id;
        }
        slot = (slot + 1) & (grid->capacity - 1);
    }
    int id = acount(grid->cells);
    grid->slots[slot] = id;
    apush(grid->cells, ((flow_cell){ .x = x, .y = y }));
    return id;
}

static float component(fluid_vec v, int axis)
{
    return axis ? v.y : v.x;
}

static float sample(const flow_grid *grid, float x, float y, int axis)
{
    if (!acount(grid->cells)) {
        return 0;
    }
    x -= axis ? .5f : 0;
    y -= axis ? 0 : .5f;
    int ix = (int)floorf(x), iy = (int)floorf(y);
    float fx = x - ix, fy = y - iy, value = 0;
    for (int dy = 0; dy < 2; dy++) {
        for (int dx = 0; dx < 2; dx++) {
            int i = find(grid, ix + dx, iy + dy);
            float weight = (dx ? fx : 1 - fx) * (dy ? fy : 1 - fy);
            value += weight * component(grid->cells[i].velocity, axis);
        }
    }
    return value;
}

static void deposit(flow_grid *grid, fluid_particle p, float size, int axis, const int *halo)
{
    float x = p.position.x / size - (axis ? .5f : 0);
    float y = p.position.y / size - (axis ? 0 : .5f);
    int ix = (int)floorf(x), iy = (int)floorf(y);
    float fx = x - ix, fy = y - iy;
    const flow_cell *center = &grid->cells[halo[HALO_RADIUS * HALO_SIDE + HALO_RADIUS]];
    int column = ix - center->x + HALO_RADIUS, row = iy - center->y + HALO_RADIUS;
    assert(column >= 0 && column + 1 < HALO_SIDE && row >= 0 && row + 1 < HALO_SIDE);
    for (int dy = 0; dy < 2; dy++) {
        for (int dx = 0; dx < 2; dx++) {
            int i = halo[(row + dy) * HALO_SIDE + column + dx];
            float weight = p.mass * (dx ? fx : 1 - fx) * (dy ? fy : 1 - fy);
            flow_cell *cell = &grid->cells[i];
            if (axis) {
                cell->source.y += p.velocity.y * weight;
                cell->heat.y += p.heat * weight;
                cell->weight.y += weight;
            } else {
                cell->source.x += p.velocity.x * weight;
                cell->heat.x += p.heat * weight;
                cell->weight.x += weight;
            }
        }
    }
}

static void seed(material_flow *flow, const fluid_particle *particles, const int *active, int count, float size)
{
    flow_grid swap = flow->previous;
    flow->previous = flow->grid;
    flow->grid = swap;
    flow_grid *grid = &flow->grid;
    int gas_count = 0;
    for (int n = 0; n < count; n++) {
        gas_count += particles[active[n]].kind == FLUID_GAS;
    }
    int capacity = 64;
    /* Each gas parcel inserts at most 25 cells, keeping the hash below 40%. */
    while (capacity < gas_count * 64) {
        capacity *= 2;
    }
    if (capacity > grid->capacity) {
        grid->capacity = capacity;
        grid->slots = xrealloc(grid->slots, (size_t)capacity * sizeof(int));
    }
    memset(grid->slots, 0, (size_t)grid->capacity * sizeof(int));
    aclear(grid->cells);
    apush(grid->cells, ((flow_cell){ 0 })); /* Open boundary sentinel. */
    for (int n = 0; n < count; n++) {
        fluid_particle p = particles[active[n]];
        if (p.kind != FLUID_GAS) {
            continue;
        }
        int x = (int)floorf(p.position.x / size), y = (int)floorf(p.position.y / size);
        /* Integer IDs survive growth of the flow-cell storage. */
        int halo[HALO_SIDE * HALO_SIDE];
        for (int dy = -HALO_RADIUS; dy <= HALO_RADIUS; dy++) {
            for (int dx = -HALO_RADIUS; dx <= HALO_RADIUS; dx++) {
                halo[(dy + HALO_RADIUS) * HALO_SIDE + dx + HALO_RADIUS] = insert(grid, x + dx, y + dy);
            }
        }
        deposit(grid, p, size, 0, halo);
        deposit(grid, p, size, 1, halo);
    }
    for (int i = 1; i < acount(grid->cells); i++) {
        flow_cell *c = &grid->cells[i];
        c->adjacent[LEFT] = find(grid, c->x - 1, c->y);
        c->adjacent[RIGHT] = find(grid, c->x + 1, c->y);
        c->adjacent[DOWN] = find(grid, c->x, c->y - 1);
        c->adjacent[UP] = find(grid, c->x, c->y + 1);
    }
}

static void boundaries(
    flow_grid *grid,
    float size,
    const fluid_body *bodies,
    const fluid_obstacle *obstacles,
    int count
)
{
    for (int i = 0; i < count; i++) {
        fluid_obstacle o = obstacles[i];
        fluid_body b = bodies[o.body];
        cos_sin rotation = cos_sin_compute(b.angle);
        float c = rotation.c, s = rotation.s;
        fluid_vec center = { b.position.x + c * o.center.x - s * o.center.y,
                             b.position.y + s * o.center.x + c * o.center.y };
        float ex = o.radius > 0
                     ? o.radius
                     : fabsf(c) * o.half_size.x + fabsf(s) * o.half_size.y;
        float ey = o.radius > 0
                     ? o.radius
                     : fabsf(s) * o.half_size.x + fabsf(c) * o.half_size.y;
        /* Conservatively cover thin colliders on the coarser flow grid.
         * Particle collision still resolves the original geometry. */
        float skin = size * .5f;
        int x0 = (int)floorf((center.x - ex - skin) / size);
        int y0 = (int)floorf((center.y - ey - skin) / size);
        int x1 = (int)floorf((center.x + ex + skin) / size);
        int y1 = (int)floorf((center.y + ey + skin) / size);
        for (int y = y0; y <= y1; y++) {
            for (int x = x0; x <= x1; x++) {
                int at = find(grid, x, y);
                if (!at) {
                    continue;
                }
                float dx = (x + .5f) * size - center.x,
                      dy = (y + .5f) * size - center.y;
                float lx = c * dx + s * dy, ly = -s * dx + c * dy;
                bool inside = o.radius > 0
                                ? dx * dx + dy * dy <=
                                      (o.radius + skin) * (o.radius + skin)
                                : fabsf(lx) <= o.half_size.x + skin &&
                                      fabsf(ly) <= o.half_size.y + skin;
                if (inside) {
                    /* A body's velocity is its center of mass's. */
                    float rx = (x + .5f) * size - b.position.x - b.center_offset.x,
                          ry = (y + .5f) * size - b.position.y - b.center_offset.y;
                    grid->cells[at].solid = true;
                    grid->cells[at].boundary = (fluid_vec){
                        b.velocity.x - b.angular_velocity * ry,
                        b.velocity.y + b.angular_velocity * rx
                    };
                }
            }
        }
    }
}

typedef struct
{
    material_flow *flow;
    fluid_flow_settings settings;
    float dt;
    fluid_particle *particles;
    const int *active;
} flow_work;

static void run(const task_system *tasks, task_range *function, int count, flow_work *work)
{
    if (tasks->workers > 1 && count >= 4096) {
        void *task = tasks->enqueue(function, count, 1024, work, tasks->user);
        if (task) {
            tasks->finish(task, tasks->user);
        }
        return;
    }
    function(0, count, 0, work);
}

static void advect(int start, int end, uint32_t, void *context)
{
    flow_work *work = context;
    material_flow *flow = work->flow;
    fluid_flow_settings settings = work->settings;
    float dt = work->dt;
    flow_grid *grid = &flow->grid;
    float damping = deterministic_expf(-settings.drag * dt);
    for (int i = start + 1; i <= end; i++) {
        flow_cell *c = &grid->cells[i];
        bool existed = find(&flow->previous, c->x, c->y) != 0;
        for (int axis = 0; axis < 2; axis++) {
            float x = c->x + (axis ? .5f : 0), y = c->y + (axis ? 0 : .5f);
            float vx = sample(&flow->previous, x, y, 0);
            float vy = sample(&flow->previous, x, y, 1);
            float v = sample(
                &flow->previous,
                x - vx * dt / settings.cell_size,
                y - vy * dt / settings.cell_size,
                axis
            );
            float weight = component(c->weight, axis);
            if (weight > 0) {
                float source = component(c->source, axis) / weight;
                v += (source - v) * (existed ? fminf(1, dt * 12) : 1);
                /* Heat remains on parcels. This weighted face sample only
                 * supplies Boussinesq acceleration to the velocity solve. */
                float heat = component(c->heat, axis) / weight;
                v -= dt * component(settings.gravity, axis) *
                     settings.buoyancy * (heat - settings.ambient_heat);
            }
            if (axis) {
                c->velocity.y = v * damping;
            } else {
                c->velocity.x = v * damping;
            }
        }
    }
}

static void curls(int start, int end, uint32_t, void *context)
{
    flow_work *work = context;
    flow_grid *grid = &work->flow->grid;
    float h = work->settings.cell_size;
    for (int i = start + 1; i <= end; i++) {
        flow_cell *c = &grid->cells[i];
        c->curl = (grid->cells[c->adjacent[RIGHT]].velocity.y -
                   grid->cells[c->adjacent[LEFT]].velocity.y -
                   grid->cells[c->adjacent[UP]].velocity.x +
                   grid->cells[c->adjacent[DOWN]].velocity.x) /
                  (2 * h);
    }
}

static void confine(int start, int end, uint32_t, void *context)
{
    flow_work *work = context;
    flow_grid *grid = &work->flow->grid;
    float h = work->settings.cell_size, dt = work->dt;
    for (int i = start + 1; i <= end; i++) {
        flow_cell *c = &grid->cells[i];
        float nx = fabsf(grid->cells[c->adjacent[RIGHT]].curl) -
                   fabsf(grid->cells[c->adjacent[LEFT]].curl);
        float ny = fabsf(grid->cells[c->adjacent[UP]].curl) -
                   fabsf(grid->cells[c->adjacent[DOWN]].curl);
        float length = sqrtf(nx * nx + ny * ny);
        if (length > 1e-6f && !c->solid) {
            float force = dt * work->settings.vorticity * h * c->curl / length;
            c->velocity.x += ny * force;
            c->velocity.y -= nx * force;
        }
    }
}

static void constrain(flow_grid *grid)
{
    for (int i = 1; i < acount(grid->cells); i++) {
        flow_cell *c = &grid->cells[i];
        flow_cell *left = &grid->cells[c->adjacent[LEFT]],
                  *down = &grid->cells[c->adjacent[DOWN]];
        if (c->solid || left->solid) {
            c->velocity.x = c->solid ? c->boundary.x : left->boundary.x;
        }
        if (c->solid || down->solid) {
            c->velocity.y = c->solid ? c->boundary.y : down->boundary.y;
        }
    }
}

typedef struct
{
    float *values;
    const pressure_row *rows;
} pressure_work;

static void pressure(int start, int end, uint32_t, void *context)
{
    pressure_work *work = context;
    for (int row = start; row < end; row++) {
#if !defined(CHIBI_FLOW_SCALAR) && (defined(__clang__) || defined(__GNUC__)) && \
    (defined(__ARM_NEON) || defined(__SSE2__) || defined(__wasm_simd128__))
        /* Interior rows have four open neighbors. Their same-color results
         * are independent, so SIMD keeps each lane's original addition order. */
        if (row + 3 < end && work->rows[row].neighbors == 4 &&
            work->rows[row + 1].neighbors == 4 && work->rows[row + 2].neighbors == 4 &&
            work->rows[row + 3].neighbors == 4) {
            typedef float lanes __attribute__((vector_size(16)));
            const pressure_row *c = &work->rows[row];
            lanes sum = { 0, 0, 0, 0 };
            for (int k = 0; k < 4; k++) {
                lanes next = { work->values[c[0].adjacent[k]],
                               work->values[c[1].adjacent[k]],
                               work->values[c[2].adjacent[k]],
                               work->values[c[3].adjacent[k]] };
                sum += next;
            }
            lanes divergence = { c[0].divergence,
                                 c[1].divergence,
                                 c[2].divergence,
                                 c[3].divergence };
            lanes result = (sum - divergence) / (lanes){ 4, 4, 4, 4 };
            for (int lane = 0; lane < 4; lane++) {
                work->values[c[lane].cell] = result[lane];
            }
            row += 3;
            continue;
        }
#endif
        const pressure_row *c = &work->rows[row];
        float sum = 0;
        for (int k = 0; k < 4; k++) {
            if (c->adjacent[k] >= 0) {
                sum += work->values[c->adjacent[k]];
            }
        }
        work->values[c->cell] = c->neighbors ? (sum - c->divergence) / c->neighbors
                                             : 0;
    }
}

static void project(material_flow *flow, float h, float dt, const task_system *tasks)
{
    flow_grid *grid = &flow->grid;
    constrain(grid);
    aclear(flow->pressure_rows[0]);
    aclear(flow->pressure_rows[1]);
    asetlen(flow->pressures, acount(grid->cells));
    memset(flow->pressures, 0, (size_t)acount(flow->pressures) * sizeof *flow->pressures);
    for (int i = 1; i < acount(grid->cells); i++) {
        flow_cell *c = &grid->cells[i];
        c->divergence = (grid->cells[c->adjacent[RIGHT]].velocity.x -
                         c->velocity.x + grid->cells[c->adjacent[UP]].velocity.y -
                         c->velocity.y) *
                        h / dt;
        if (!c->solid) {
            pressure_row row = { .cell = i, .divergence = c->divergence };
            for (int k = 0; k < 4; k++) {
                int other = c->adjacent[k];
                row.adjacent[k] = grid->cells[other].solid ? -1 : other;
                row.neighbors += row.adjacent[k] >= 0;
            }
            apush(flow->pressure_rows[(c->x ^ c->y) & 1], row);
        }
    }
    /* Same-color cells have no shared pressure dependencies. Keep the original
     * neighbor addition order and join every color before starting the next. */
    for (int step = 0; step < PRESSURE_ITERATIONS; step++) {
        for (int parity = 0; parity < 2; parity++) {
            pressure_work work = { flow->pressures, flow->pressure_rows[parity] };
            int count = acount(flow->pressure_rows[parity]);
            if (tasks->workers > 1 && count >= 16384) {
                void *task = tasks->enqueue(pressure, count, 1024, &work, tasks->user);
                if (task) {
                    tasks->finish(task, tasks->user);
                }
            } else {
                pressure(0, count, 0, &work);
            }
        }
    }
    for (int i = 1; i < acount(grid->cells); i++) {
        grid->cells[i].pressure = flow->pressures[i];
    }
    for (int i = 1; i < acount(grid->cells); i++) {
        flow_cell *c = &grid->cells[i];
        flow_cell *left = &grid->cells[c->adjacent[LEFT]],
                  *down = &grid->cells[c->adjacent[DOWN]];
        if (!c->solid && !left->solid) {
            c->velocity.x -= dt / h * (c->pressure - left->pressure);
        }
        if (!c->solid && !down->solid) {
            c->velocity.y -= dt / h * (c->pressure - down->pressure);
        }
    }
}

static void transfer(int start, int end, uint32_t, void *context)
{
    flow_work *work = context;
    for (int n = start; n < end; n++) {
        fluid_particle *p = &work->particles[work->active[n]];
        /* Ballistic exhaust stirs the grid but keeps its authored motion. */
        if (p->kind != FLUID_GAS || (p->style.duration > 0 && !p->smoke)) {
            continue;
        }
        float x = p->position.x / work->settings.cell_size,
              y = p->position.y / work->settings.cell_size;
        p->velocity = (fluid_vec){ sample(&work->flow->grid, x, y, 0),
                                   sample(&work->flow->grid, x, y, 1) };
    }
}

void material_flow_step(
    material_flow *flow,
    fluid_flow_settings settings,
    float dt,
    fluid_particle *particles,
    const int *active,
    int count,
    const fluid_body *bodies,
    const fluid_obstacle *obstacles,
    int obstacle_count,
    const task_system *tasks
)
{
    seed(flow, particles, active, count, settings.cell_size);
    boundaries(&flow->grid, settings.cell_size, bodies, obstacles, obstacle_count);
    flow_work work = { flow, settings, dt, particles, active };
    int cells = acount(flow->grid.cells) - 1;
    run(tasks, advect, cells, &work);
    run(tasks, curls, cells, &work);
    run(tasks, confine, cells, &work);
    project(flow, settings.cell_size, dt, tasks);
    run(tasks, transfer, count, &work);
}

uint64_t material_flow_hash(const material_flow *flow)
{
    uint64_t hash = UINT64_C(14695981039346656037);
    for (int i = 1; i < acount(flow->grid.cells); i++) {
        flow_cell c = flow->grid.cells[i];
        unsigned data[4] = { (unsigned)c.x, (unsigned)c.y };
        memcpy(data + 2, &c.velocity, sizeof c.velocity);
        for (int j = 0; j < 4; j++) {
            hash = (hash ^ data[j]) * UINT64_C(1099511628211);
        }
    }
    return hash;
}
