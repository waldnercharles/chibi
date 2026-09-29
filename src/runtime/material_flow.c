#include "runtime/material_flow.h"

#include "runtime/material_solid.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

enum
{
    LEFT,
    RIGHT,
    DOWN,
    UP,
    PRESSURE_ITERATIONS = 32
};
typedef struct
{
    int x, y, adjacent[4];
    /* X velocity lives on the left face, Y velocity on the lower face. */
    fluid_vec velocity, source, weight, boundary;
    float pressure, divergence, curl;
    bool solid;
} flow_cell;
typedef struct
{
    flow_cell *cells;
    int *slots;
    int capacity;
} flow_grid;
struct material_flow
{
    flow_grid grid, previous;
};

material_flow *material_flow_new()
{
    return xcalloc(1, sizeof(material_flow));
}

void material_flow_clear(material_flow *flow)
{
    aclear(flow->grid.cells);
    aclear(flow->previous.cells);
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

static void insert(flow_grid *grid, int x, int y)
{
    unsigned slot = bucket(grid, x, y);
    while (grid->slots[slot]) {
        flow_cell *cell = &grid->cells[grid->slots[slot]];
        if (cell->x == x && cell->y == y) {
            return;
        }
        slot = (slot + 1) & (grid->capacity - 1);
    }
    grid->slots[slot] = acount(grid->cells);
    apush(grid->cells, ((flow_cell){ .x = x, .y = y }));
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

static void deposit(flow_grid *grid, fluid_particle p, float size, int axis)
{
    float x = p.position.x / size - (axis ? .5f : 0);
    float y = p.position.y / size - (axis ? 0 : .5f);
    int ix = (int)floorf(x), iy = (int)floorf(y);
    float fx = x - ix, fy = y - iy;
    for (int dy = 0; dy < 2; dy++) {
        for (int dx = 0; dx < 2; dx++) {
            int i = find(grid, ix + dx, iy + dy);
            float weight = p.mass * (dx ? fx : 1 - fx) * (dy ? fy : 1 - fy);
            flow_cell *cell = &grid->cells[i];
            if (axis) {
                cell->source.y += p.velocity.y * weight;
                cell->weight.y += weight;
            } else {
                cell->source.x += p.velocity.x * weight;
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
    int capacity = 64;
    /* Each gas parcel inserts at most 25 cells, keeping the hash below 40%. */
    while (capacity < count * 64) {
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
        for (int dy = -2; dy <= 2; dy++) {
            for (int dx = -2; dx <= 2; dx++) {
                insert(grid, x + dx, y + dy);
            }
        }
        deposit(grid, p, size, 0);
        deposit(grid, p, size, 1);
    }
    for (int i = 1; i < acount(grid->cells); i++) {
        flow_cell *c = &grid->cells[i];
        c->adjacent[LEFT] = find(grid, c->x - 1, c->y);
        c->adjacent[RIGHT] = find(grid, c->x + 1, c->y);
        c->adjacent[DOWN] = find(grid, c->x, c->y - 1);
        c->adjacent[UP] = find(grid, c->x, c->y + 1);
    }
}

/* Cells within half a cell of static solids hold still. */
static void solid_boundaries(flow_grid *grid, float size, const material_solids *solids)
{
    for (int i = 1; i < acount(grid->cells); i++) {
        flow_cell *c = &grid->cells[i];
        fluid_vec center = { (c->x + .5f) * size, (c->y + .5f) * size };
        if (material_solids_sample(solids, center).distance < size * .5f) {
            c->solid = true;
            c->boundary = (fluid_vec){ 0 };
        }
    }
}

static void boundaries(flow_grid *grid, float size, const fluid_colliders *colliders)
{
    for (int i = 0; i < colliders->obstacle_count; i++) {
        fluid_obstacle o = colliders->obstacles[i];
        fluid_body b = colliders->bodies[o.body];
        float c = cosf(b.angle), s = sinf(b.angle);
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
                    grid->cells[at].solid = true;
                    grid->cells[at].boundary = (fluid_vec){
                        b.velocity.x - b.angular_velocity * dy,
                        b.velocity.y + b.angular_velocity * dx
                    };
                }
            }
        }
    }
}

static void advect(material_flow *flow, fluid_flow_settings settings, float dt)
{
    flow_grid *grid = &flow->grid;
    float damping = expf(-settings.drag * dt);
    for (int i = 1; i < acount(grid->cells); i++) {
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
            }
            if (axis) {
                c->velocity.y = v * damping;
            } else {
                c->velocity.x = v * damping;
            }
        }
    }
}

static void confine(flow_grid *grid, fluid_flow_settings settings, float dt)
{
    float h = settings.cell_size;
    for (int i = 1; i < acount(grid->cells); i++) {
        flow_cell *c = &grid->cells[i];
        c->curl = (grid->cells[c->adjacent[RIGHT]].velocity.y -
                   grid->cells[c->adjacent[LEFT]].velocity.y -
                   grid->cells[c->adjacent[UP]].velocity.x +
                   grid->cells[c->adjacent[DOWN]].velocity.x) /
                  (2 * h);
    }
    for (int i = 1; i < acount(grid->cells); i++) {
        flow_cell *c = &grid->cells[i];
        float nx = fabsf(grid->cells[c->adjacent[RIGHT]].curl) -
                   fabsf(grid->cells[c->adjacent[LEFT]].curl);
        float ny = fabsf(grid->cells[c->adjacent[UP]].curl) -
                   fabsf(grid->cells[c->adjacent[DOWN]].curl);
        float length = hypotf(nx, ny);
        if (length > 1e-6f && !c->solid) {
            float force = dt * settings.vorticity * h * c->curl / length;
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

static void project(flow_grid *grid, float h, float dt)
{
    constrain(grid);
    for (int i = 1; i < acount(grid->cells); i++) {
        flow_cell *c = &grid->cells[i];
        c->divergence = (grid->cells[c->adjacent[RIGHT]].velocity.x -
                         c->velocity.x + grid->cells[c->adjacent[UP]].velocity.y -
                         c->velocity.y) *
                        h / dt;
    }
    for (int step = 0; step < PRESSURE_ITERATIONS; step++) {
        for (int parity = 0; parity < 2; parity++) {
            for (int i = 1; i < acount(grid->cells); i++) {
                flow_cell *c = &grid->cells[i];
                if (c->solid || ((c->x ^ c->y) & 1) != parity) {
                    continue;
                }
                float sum = 0;
                int neighbors = 0;
                for (int k = 0; k < 4; k++) {
                    flow_cell *other = &grid->cells[c->adjacent[k]];
                    if (!other->solid) {
                        sum += other->pressure;
                        neighbors++;
                    }
                }
                c->pressure = neighbors ? (sum - c->divergence) / neighbors : 0;
            }
        }
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

void material_flow_step(
    material_flow *flow,
    fluid_flow_settings settings,
    float dt,
    fluid_particle *particles,
    const int *active,
    int count,
    const fluid_colliders *colliders,
    const material_solids *solids
)
{
    seed(flow, particles, active, count, settings.cell_size);
    if (solids) {
        solid_boundaries(&flow->grid, settings.cell_size, solids);
    }
    boundaries(&flow->grid, settings.cell_size, colliders);
    advect(flow, settings, dt);
    confine(&flow->grid, settings, dt);
    project(&flow->grid, settings.cell_size, dt);
    for (int n = 0; n < count; n++) {
        fluid_particle *p = &particles[active[n]];
        /* Ballistic exhaust stirs the grid but keeps its authored motion. */
        if (p->kind != FLUID_GAS || (p->style.duration > 0 && !p->smoke)) {
            continue;
        }
        float x = p->position.x / settings.cell_size,
              y = p->position.y / settings.cell_size;
        p->velocity = (fluid_vec){ sample(&flow->grid, x, y, 0),
                                   sample(&flow->grid, x, y, 1) };
    }
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
