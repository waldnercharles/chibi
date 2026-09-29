/* MLS-MPM with APIC transfers for liquid, powder and elastic matter, on a
 * sparse grid of 8x8-node blocks around the active particles.
 *
 * Every parallel pass writes only its own outputs: particles are counting-
 * sorted by the cell of their base node, and each grid node gathers from the
 * 3x3 cells whose particles reach it, in a fixed order. Serial and parallel
 * runs therefore sum in the same order and match exactly. */
#include "diagnostics/tracy.h"
#include "runtime/material_fluid_internal.h"
#include "runtime/material_solid.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

enum
{
    BLOCK_SHIFT = 3,
    BLOCK = 1 << BLOCK_SHIFT,
    BLOCK_NODES = BLOCK * BLOCK,
    SUBSTEPS = 4
};
/* The fastest wave the solver carries, in cells per substep. Matter moves
 * at most half that fast, so impacts stay slower than stiff matter's
 * waves and cannot crush it before its stress responds. */
static const float COURANT = .3f, SPEED_LIMIT = .5f;
static const float POISSON = .3f;
/* A liquid's volume ratio stays at least J_MIN. Cohesive liquid may stretch
 * by up to STRETCH times its cohesion, which keeps its tension below the
 * pressure compression can raise. */
static const float J_MIN = .5f, STRETCH = .3f;
/* Singular values of elastic and powder deformation stay within these. */
static const float STRETCH_MIN = .3f, STRETCH_MAX = 3;
/* Rigid bodies weigh one unit of mass per square meter of 32 units. */
static const float RIGID_DENSITY = 1.f / 1024;
/* Material constants scale with the longest step, so matter behaves the
 * same when time runs slower. */
static const float LONGEST_STEP = 1.f / 60;

typedef struct
{
    int x, y;
    /* Blocks of the 3x3 neighborhood, row by row; -1 where absent. */
    int neighbor[9];
    /* First node of this block's ground samples in the tick cache. */
    int ground;
} mpm_block;

typedef struct
{
    int particle;
    /* The cell of the base node: block * BLOCK_NODES + local node. */
    int cell;
    /* Offset from the base node, in cells, within [0.5, 1.5), and the
     * quadratic B-spline weights of the three nodes along each axis. */
    float fx, fy, wx[3], wy[3];
    float mass;
    fluid_vec momentum;
    /* -dt V D^-1 tau + m C, the stress and APIC transfer. */
    fluid_matrix affine;
} mpm_point;

typedef struct
{
    float mass, weight;
    fluid_vec velocity;
    /* The particle that weighs most on this node, for contacts and grip. */
    int rep;
} mpm_node;

typedef struct
{
    float distance;
    fluid_vec normal;
} mpm_ground;

typedef struct
{
    int x, y;
} mpm_cell;

struct material_mpm
{
    int *active, *cells, *start, *order, *order_cells;
    mpm_block *blocks;
    /* Block index or -1 over the particles' bounds, in blocks. */
    int *map;
    int map_x, map_y, map_width, map_height;
    /* Base node of each active particle. */
    mpm_cell *base;
    mpm_point *points;
    mpm_node *nodes;
    /* Terrain distance at nodes, cached per tick by block. */
    uint64_t *ground_keys;
    int *ground_slots;
    int ground_capacity;
    mpm_ground *ground;
    int *pending;
    float dx, dt, speed, viscous;
};

material_mpm *material_mpm_new()
{
    return xcalloc(1, sizeof(material_mpm));
}

void material_mpm_free(material_mpm *m)
{
    afree(m->active);
    afree(m->cells);
    afree(m->start);
    afree(m->order);
    afree(m->order_cells);
    afree(m->blocks);
    afree(m->map);
    afree(m->base);
    afree(m->points);
    afree(m->nodes);
    afree(m->ground_keys);
    free(m->ground_slots);
    afree(m->ground);
    afree(m->pending);
    free(m);
}

static fluid_matrix mat(float xx, float xy, float yx, float yy)
{
    return (fluid_matrix){ xx, xy, yx, yy };
}
static fluid_matrix mat_mul(fluid_matrix a, fluid_matrix b)
{
    return mat(
        a.xx * b.xx + a.xy * b.yx,
        a.xx * b.xy + a.xy * b.yy,
        a.yx * b.xx + a.yy * b.yx,
        a.yx * b.xy + a.yy * b.yy
    );
}
static fluid_matrix mat_transpose(fluid_matrix a)
{
    return mat(a.xx, a.yx, a.xy, a.yy);
}
static fluid_matrix mat_add(fluid_matrix a, fluid_matrix b)
{
    return mat(a.xx + b.xx, a.xy + b.xy, a.yx + b.yx, a.yy + b.yy);
}
static fluid_matrix mat_scale(fluid_matrix a, float s)
{
    return mat(a.xx * s, a.xy * s, a.yx * s, a.yy * s);
}
static float mat_det(fluid_matrix a)
{
    return a.xx * a.yy - a.xy * a.yx;
}
static fluid_matrix rotation(float angle)
{
    float c = cosf(angle), s = sinf(angle);
    return mat(c, -s, s, c);
}
static fluid_vec mat_apply(fluid_matrix a, fluid_vec v)
{
    return (fluid_vec){ a.xx * v.x + a.xy * v.y, a.yx * v.x + a.yy * v.y };
}

/* F = U diag(sigma) V^T through the polar decomposition F = R S. Stretches
 * clamp to [STRETCH_MIN, STRETCH_MAX], which also resolves inversion. */
static void svd(fluid_matrix f, fluid_matrix *u, float sigma[2], fluid_matrix *v)
{
    fluid_matrix r = rotation(atan2f(f.yx - f.xy, f.xx + f.yy));
    fluid_matrix s = mat_mul(mat_transpose(r), f);
    float angle = .5f * atan2f(s.xy + s.yx, s.xx - s.yy);
    *v = rotation(angle);
    fluid_matrix d = mat_mul(mat_mul(mat_transpose(*v), s), *v);
    sigma[0] = fminf(STRETCH_MAX, fmaxf(STRETCH_MIN, d.xx));
    sigma[1] = fminf(STRETCH_MAX, fmaxf(STRETCH_MIN, d.yy));
    *u = mat_mul(r, *v);
}

static fluid_matrix compose(fluid_matrix u, const float sigma[2], fluid_matrix v)
{
    fluid_matrix d = mat(sigma[0], 0, 0, sigma[1]);
    return mat_mul(mat_mul(u, d), mat_transpose(v));
}

typedef struct
{
    float bulk, mu, lambda, viscosity;
} mpm_moduli;

/* Lame parameters from a wave speed that the substeps keep stable. */
static mpm_moduli moduli(const material_mpm *m, const fluid_substance *s)
{
    float rho = s->density * RIGID_DENSITY;
    float c = s->stiffness * m->speed;
    float e = rho * c * c;
    return (mpm_moduli){ .bulk = e,
                         .mu = e / (2 * (1 + POISSON)),
                         .lambda = e * POISSON / ((1 + POISSON) * (1 - 2 * POISSON)),
                         .viscosity = s->viscosity * rho * m->viscous };
}

/* Kirchhoff stress of a particle. */
static fluid_matrix stress(const material_mpm *m, const fluid_substance *s, const fluid_particle *p)
{
    mpm_moduli k = moduli(m, s);
    fluid_matrix f = p->deformation;
    float j = mat_det(f);
    if (s->phase == FLUID_LIQUID) {
        float pressure = k.bulk * (1 - j);
        if (pressure < 0) {
            pressure *= s->cohesion;
        }
        fluid_matrix shear = mat_add(p->affine, mat_transpose(p->affine));
        return mat_add(mat(-pressure * j, 0, 0, -pressure * j), mat_scale(shear, k.viscosity));
    }
    if (s->phase == FLUID_ELASTIC) {
        fluid_matrix r = rotation(atan2f(f.yx - f.xy, f.xx + f.yy));
        fluid_matrix corotated = mat_scale(
            mat_mul(mat_add(f, mat_scale(r, -1)), mat_transpose(f)),
            2 * k.mu
        );
        return mat_add(corotated, mat(k.lambda * (j - 1) * j, 0, 0, k.lambda * (j - 1) * j));
    }
    /* Powder: St. Venant-Kirchhoff on Hencky strain. */
    fluid_matrix u, v;
    float sigma[2];
    svd(f, &u, sigma, &v);
    float e0 = logf(sigma[0]), e1 = logf(sigma[1]), trace = e0 + e1;
    float t[2] = { 2 * k.mu * e0 + k.lambda * trace, 2 * k.mu * e1 + k.lambda * trace };
    return mat_mul(mat_mul(u, mat(t[0], 0, 0, t[1])), mat_transpose(u));
}

/* Drucker-Prager return mapping of a powder's stretches (Klar et al. 2016):
 * expansion leaves the cone's tip, and shear beyond friction flows. */
static void yield(const material_mpm *m, const fluid_substance *s, float sigma[2])
{
    mpm_moduli k = moduli(m, s);
    float e[2] = { logf(sigma[0]), logf(sigma[1]) };
    float trace = e[0] + e[1];
    if (trace >= 0) {
        sigma[0] = sigma[1] = 1;
        return;
    }
    float h[2] = { e[0] - trace * .5f, e[1] - trace * .5f };
    float norm = hypotf(h[0], h[1]);
    float phi = s->friction * .7853982f, sine = sinf(phi);
    float alpha = sqrtf(2.f / 3) * 2 * sine / (3 - sine);
    float gamma = norm + (2 * k.lambda + 2 * k.mu) / (2 * k.mu) * trace * alpha;
    if (norm <= 1e-9f || gamma <= 0) {
        return;
    }
    sigma[0] = expf(e[0] - gamma * h[0] / norm);
    sigma[1] = expf(e[1] - gamma * h[1] / norm);
}

static int floor_block(int n)
{
    return n >> BLOCK_SHIFT;
}

static uint64_t block_key(int x, int y)
{
    return (uint64_t)(uint32_t)y << 32 | (uint32_t)x;
}

static unsigned key_slot(uint64_t key, int capacity)
{
    return (unsigned)((key * UINT64_C(0x9e3779b97f4a7c15)) >> 32) &
           (unsigned)(capacity - 1);
}

/* The block at block coordinates, or -1. */
static int find_block(const material_mpm *m, int x, int y)
{
    x -= m->map_x;
    y -= m->map_y;
    return x < 0 || y < 0 || x >= m->map_width || y >= m->map_height
             ? -1
             : m->map[y * m->map_width + x];
}

static void base_node(const material_mpm *m, fluid_vec position, int *x, int *y)
{
    *x = (int)floorf(position.x / m->dx - .5f);
    *y = (int)floorf(position.y / m->dx - .5f);
}

typedef struct
{
    material_fluid *fluid;
    material_mpm *mpm;
} mpm_pass;

static void locate(int start, int end, uint32_t, void *context)
{
    const mpm_pass *pass = context;
    material_mpm *m = pass->mpm;
    for (int n = start; n < end; n++) {
        base_node(
            m,
            pass->fluid->particles[m->active[n]].position,
            &m->base[n].x,
            &m->base[n].y
        );
    }
}

/* Blocks reached by any particle's stencil, numbered row by row over a
 * dense map of the particles' bounds. */
static void build_blocks(material_fluid *fluid, material_mpm *m)
{
    int count = acount(m->active);
    asetlen(m->base, count);
    mpm_pass pass = { fluid, m };
    material_fluid_run(fluid, locate, count, &pass, 1024);
    int x0 = INT32_MAX, y0 = INT32_MAX, x1 = INT32_MIN, y1 = INT32_MIN;
    for (int n = 0; n < count; n++) {
        x0 = m->base[n].x < x0 ? m->base[n].x : x0;
        y0 = m->base[n].y < y0 ? m->base[n].y : y0;
        x1 = m->base[n].x > x1 ? m->base[n].x : x1;
        y1 = m->base[n].y > y1 ? m->base[n].y : y1;
    }
    m->map_x = floor_block(x0);
    m->map_y = floor_block(y0);
    m->map_width = floor_block(x1 + 2) - m->map_x + 1;
    m->map_height = floor_block(y1 + 2) - m->map_y + 1;
    assert((int64_t)m->map_width * m->map_height <= 1 << 22);
    asetlen(m->map, m->map_width * m->map_height);
    memset(m->map, 0xff, (size_t)m->map_width * m->map_height * sizeof *m->map);
    for (int n = 0; n < count; n++) {
        for (int corner = 0; corner < 4; corner++) {
            int x = floor_block(m->base[n].x + (corner & 1) * 2) - m->map_x;
            int y = floor_block(m->base[n].y + (corner >> 1) * 2) - m->map_y;
            m->map[y * m->map_width + x] = 0;
        }
    }
    aclear(m->blocks);
    for (int i = 0; i < m->map_width * m->map_height; i++) {
        if (m->map[i] < 0) {
            continue;
        }
        m->map[i] = acount(m->blocks);
        apush(
            m->blocks,
            ((mpm_block){ .x = m->map_x + i % m->map_width,
                          .y = m->map_y + i / m->map_width,
                          .ground = -1 })
        );
    }
    for (int b = 0; b < acount(m->blocks); b++) {
        mpm_block *block = &m->blocks[b];
        for (int k = 0; k < 9; k++) {
            block->neighbor[k] = find_block(m, block->x + k % 3 - 1, block->y + k / 3 - 1);
        }
    }
}

/* Counting sort of particles by the cell of their base node. */
static void sort_points(material_mpm *m)
{
    int count = acount(m->active), cells = acount(m->blocks) * BLOCK_NODES;
    asetlen(m->cells, count);
    asetlen(m->start, cells + 1);
    memset(m->start, 0, (size_t)(cells + 1) * sizeof *m->start);
    for (int n = 0; n < count; n++) {
        int x = m->base[n].x, y = m->base[n].y;
        int block = find_block(m, floor_block(x), floor_block(y));
        m->cells[n] = block * BLOCK_NODES + (y & (BLOCK - 1)) * BLOCK +
                      (x & (BLOCK - 1));
        m->start[m->cells[n] + 1]++;
    }
    for (int c = 0; c < cells; c++) {
        m->start[c + 1] += m->start[c];
    }
    asetlen(m->order, count);
    asetlen(m->order_cells, count);
    int *cursor = NULL;
    acopy(cursor, m->start);
    for (int n = 0; n < count; n++) {
        int at = cursor[m->cells[n]]++;
        m->order[at] = m->active[n];
        m->order_cells[at] = m->cells[n];
    }
    afree(cursor);
}

static void weights(float f, float w[3])
{
    w[0] = .5f * (1.5f - f) * (1.5f - f);
    w[1] = .75f - (f - 1) * (f - 1);
    w[2] = .5f * (f - .5f) * (f - .5f);
}

static void prepare_points(int start, int end, uint32_t, void *context)
{
    const mpm_pass *pass = context;
    material_fluid *fluid = pass->fluid;
    material_mpm *m = pass->mpm;
    float scale = -m->dt * 4 / (m->dx * m->dx);
    for (int n = start; n < end; n++) {
        const fluid_particle *p = &fluid->particles[m->order[n]];
        const fluid_substance *s = &fluid->substances[p->substance];
        float gx = p->position.x / m->dx, gy = p->position.y / m->dx;
        float volume = p->mass / (s->density * RIGID_DENSITY);
        fluid_matrix affine = mat_add(
            mat_scale(stress(m, s, p), scale * volume),
            mat_scale(p->affine, p->mass)
        );
        mpm_point *point = &m->points[n];
        *point = (mpm_point){ .particle = m->order[n],
                              .cell = m->order_cells[n],
                              .fx = gx - floorf(gx - .5f),
                              .fy = gy - floorf(gy - .5f),
                              .mass = p->mass,
                              .momentum = fluid_scale(p->velocity, p->mass),
                              .affine = affine };
        weights(point->fx, point->wx);
        weights(point->fy, point->wy);
    }
}

/* Each node gathers from the particles whose base cell lies within two
 * cells below and left of it. */
static void gather(int start, int end, uint32_t, void *context)
{
    const mpm_pass *pass = context;
    material_mpm *m = pass->mpm;
    for (int b = start; b < end; b++) {
        const mpm_block *block = &m->blocks[b];
        for (int local = 0; local < BLOCK_NODES; local++) {
            int nx = block->x * BLOCK + local % BLOCK,
                ny = block->y * BLOCK + local / BLOCK;
            mpm_node node = { .rep = -1 };
            for (int oy = 0; oy < 3; oy++) {
                for (int ox = 0; ox < 3; ox++) {
                    int cx = nx - ox, cy = ny - oy;
                    int neighbor = block->neighbor
                                       [(floor_block(cy) - block->y + 1) * 3 +
                                        floor_block(cx) - block->x + 1];
                    if (neighbor < 0) {
                        continue;
                    }
                    int cell = neighbor * BLOCK_NODES +
                               (cy & (BLOCK - 1)) * BLOCK + (cx & (BLOCK - 1));
                    for (int n = m->start[cell]; n < m->start[cell + 1]; n++) {
                        const mpm_point *p = &m->points[n];
                        float w = p->wx[ox] * p->wy[oy];
                        fluid_vec offset = { (ox - p->fx) * m->dx,
                                             (oy - p->fy) * m->dx };
                        fluid_vec transfer = fluid_add(p->momentum, mat_apply(p->affine, offset));
                        node.mass += w * p->mass;
                        node.velocity = fluid_add(node.velocity, fluid_scale(transfer, w));
                        if (w > node.weight) {
                            node.weight = w;
                            node.rep = p->particle;
                        }
                    }
                }
            }
            m->nodes[b * BLOCK_NODES + local] = node;
        }
    }
}

static void sample_ground(int start, int end, uint32_t, void *context)
{
    const mpm_pass *pass = context;
    material_mpm *m = pass->mpm;
    const material_solids *solids = pass->fluid->step->solids;
    for (int n = start; n < end; n++) {
        const mpm_block *block = &m->blocks[m->pending[n]];
        for (int local = 0; local < BLOCK_NODES; local++) {
            fluid_vec at = { (block->x * BLOCK + local % BLOCK) * m->dx,
                             (block->y * BLOCK + local / BLOCK) * m->dx };
            material_solid_sample s = solids
                                        ? material_solids_sample(solids, at)
                                        : (material_solid_sample){ MATERIAL_SOLID_FAR };
            m->ground[block->ground + local] = (mpm_ground){
                fminf(2 * m->dx, fmaxf(-2 * m->dx, s.distance)),
                s.normal
            };
        }
    }
}

/* Terrain distance at every node, sampled once per tick for each block. */
static void cache_ground(material_fluid *fluid, material_mpm *m)
{
    int needed = acount(m->ground_keys) + acount(m->blocks);
    if (needed * 2 > m->ground_capacity) {
        int capacity = 64;
        while (capacity < needed * 4) {
            capacity *= 2;
        }
        free(m->ground_slots);
        m->ground_slots = xcalloc((size_t)capacity, sizeof *m->ground_slots);
        m->ground_capacity = capacity;
        for (int i = 0; i < acount(m->ground_keys); i++) {
            unsigned slot = key_slot(m->ground_keys[i], capacity);
            while (m->ground_slots[slot]) {
                slot = (slot + 1) & (unsigned)(capacity - 1);
            }
            m->ground_slots[slot] = i + 1;
        }
    }
    aclear(m->pending);
    for (int b = 0; b < acount(m->blocks); b++) {
        mpm_block *block = &m->blocks[b];
        uint64_t key = block_key(block->x, block->y);
        unsigned slot = key_slot(key, m->ground_capacity);
        while (m->ground_slots[slot] &&
               m->ground_keys[m->ground_slots[slot] - 1] != key) {
            slot = (slot + 1) & (unsigned)(m->ground_capacity - 1);
        }
        if (m->ground_slots[slot]) {
            block->ground = (m->ground_slots[slot] - 1) * BLOCK_NODES;
            continue;
        }
        apush(m->ground_keys, key);
        m->ground_slots[slot] = acount(m->ground_keys);
        block->ground = (acount(m->ground_keys) - 1) * BLOCK_NODES;
        asetlen(m->ground, acount(m->ground_keys) * BLOCK_NODES);
        apush(m->pending, b);
    }
    mpm_pass pass = { fluid, m };
    material_fluid_run(fluid, sample_ground, acount(m->pending), &pass, 4);
}

/* Momentum to velocity, gravity, a speed limit, and the terrain: nodes
 * inside solid stop; nodes at the surface lose their approach speed and
 * slide with Coulomb friction. */
static void update_grid(int start, int end, uint32_t, void *context)
{
    const mpm_pass *pass = context;
    material_fluid *fluid = pass->fluid;
    material_mpm *m = pass->mpm;
    fluid_vec gravity = fluid->step->gravity;
    for (int b = start; b < end; b++) {
        const mpm_block *block = &m->blocks[b];
        for (int local = 0; local < BLOCK_NODES; local++) {
            mpm_node *node = &m->nodes[b * BLOCK_NODES + local];
            if (node->mass <= 0) {
                continue;
            }
            fluid_vec v = fluid_add(
                fluid_scale(node->velocity, 1 / node->mass),
                fluid_scale(gravity, m->dt)
            );
            float speed = sqrtf(fluid_dot(v, v)), limit = m->speed * SPEED_LIMIT;
            if (speed > limit) {
                v = fluid_scale(v, limit / speed);
            }
            mpm_ground ground = m->ground[block->ground + local];
            if (ground.distance < -m->dx) {
                v = (fluid_vec){ 0 };
            } else if (ground.distance < m->dx * .5f) {
                float approach = fluid_dot(v, ground.normal);
                if (approach < 0) {
                    float grip = fluid
                                     ->substances[fluid->particles[node->rep].substance]
                                     .friction;
                    fluid_vec slide = fluid_sub(v, fluid_scale(ground.normal, approach));
                    float along = sqrtf(fluid_dot(slide, slide)),
                          loss = -approach * grip;
                    v = along > loss ? fluid_scale(slide, 1 - loss / along)
                                     : (fluid_vec){ 0 };
                }
            }
            node->velocity = v;
        }
    }
}

static int node_index(const material_mpm *m, int x, int y)
{
    int block = find_block(m, floor_block(x), floor_block(y));
    return block < 0
             ? -1
             : block * BLOCK_NODES + (y & (BLOCK - 1)) * BLOCK + (x & (BLOCK - 1));
}

/* Signed distance and outward normal of an obstacle at a local point. */
static float obstacle_distance(fluid_obstacle o, fluid_vec local, fluid_vec *normal)
{
    local = fluid_sub(local, o.center);
    if (o.radius > 0) {
        float length = sqrtf(fluid_dot(local, local));
        *normal = length > 1e-6f ? fluid_scale(local, 1 / length)
                                 : (fluid_vec){ 0, -1 };
        return length - o.radius;
    }
    float qx = fabsf(local.x) - o.half_size.x, qy = fabsf(local.y) - o.half_size.y;
    if (qx > 0 || qy > 0) {
        fluid_vec out = { local.x < 0 ? -fmaxf(qx, 0) : fmaxf(qx, 0),
                          local.y < 0 ? -fmaxf(qy, 0) : fmaxf(qy, 0) };
        float length = sqrtf(fluid_dot(out, out));
        *normal = fluid_scale(out, 1 / length);
        return length;
    }
    *normal = qx > qy ? (fluid_vec){ local.x < 0 ? -1.f : 1.f, 0 }
                      : (fluid_vec){ 0, local.y < 0 ? -1.f : 1.f };
    return fmaxf(qx, qy);
}

/* Each particle that led a node into a body reports one contact, like
 * particles that touch a body directly. */
static void report_contacts(material_fluid *fluid, material_mpm *m)
{
    const fluid_colliders *colliders = &fluid->step->colliders;
    for (int n = 0; n < acount(m->active); n++) {
        int i = m->active[n];
        particle_work *w = &fluid->work[i];
        if (w->contact_obstacle < 0) {
            continue;
        }
        const fluid_particle *p = &fluid->particles[i];
        fluid_obstacle o = colliders->obstacles[w->contact_obstacle];
        const fluid_body *body = &colliders->bodies[o.body];
        fluid_vec point = fluid_sub(p->position, fluid_scale(w->contact_normal, p->radius));
        fluid_vec offset = fluid_sub(point, body->position);
        float c = cosf(body->angle), s = sinf(body->angle);
        fluid_vec local = { c * offset.x + s * offset.y,
                            -s * offset.x + c * offset.y };
        material_fluid_record_contact(
            fluid,
            p,
            body,
            o.body,
            point,
            w->contact_normal,
            local,
            w->contact_closing,
            w->contact_closing * p->mass
        );
        w->contact_obstacle = -1;
        w->contact_closing = 0;
    }
}

/* Moving bodies are velocity boundaries for the grid: nodes at a body lose
 * their speed into it, and that momentum returns to the body. Substeps move
 * swept bodies along their path. */
static void couple_bodies(material_fluid *fluid, material_mpm *m, float t)
{
    const fluid_colliders *colliders = &fluid->step->colliders;
    float skin = m->dx * .5f;
    for (int i = 0; i < colliders->obstacle_count; i++) {
        fluid_obstacle o = colliders->obstacles[i];
        fluid_body *body = &colliders->bodies[o.body];
        fluid_vec position = body->swept
                               ? fluid_add(
                                     body->previous_position,
                                     fluid_scale(fluid_sub(body->position, body->previous_position), t)
                                 )
                               : body->position;
        float angle = body->swept
                        ? body->previous_angle +
                              remainderf(body->angle - body->previous_angle, 6.2831853f) * t
                        : body->angle;
        float c = cosf(angle), s = sinf(angle);
        fluid_vec center = fluid_add(
            position,
            (fluid_vec){ c * o.center.x - s * o.center.y,
                         s * o.center.x + c * o.center.y }
        );
        float reach = (o.radius > 0 ? o.radius
                                    : hypotf(o.half_size.x, o.half_size.y)) +
                      skin;
        for (int y = (int)floorf((center.y - reach) / m->dx);
             y <= (int)ceilf((center.y + reach) / m->dx);
             y++) {
            for (int x = (int)floorf((center.x - reach) / m->dx);
                 x <= (int)ceilf((center.x + reach) / m->dx);
                 x++) {
                int index = node_index(m, x, y);
                if (index < 0 || m->nodes[index].mass <= 0) {
                    continue;
                }
                mpm_node *node = &m->nodes[index];
                fluid_vec at = { x * m->dx, y * m->dx };
                fluid_vec offset = fluid_sub(at, position);
                fluid_vec local = { c * offset.x + s * offset.y,
                                    -s * offset.x + c * offset.y };
                fluid_vec normal;
                if (obstacle_distance(o, local, &normal) >= skin) {
                    continue;
                }
                normal = (fluid_vec){ c * normal.x - s * normal.y,
                                      s * normal.x + c * normal.y };
                fluid_vec surface = fluid_add(
                    fluid_add(
                        body->velocity,
                        (fluid_vec){ -body->angular_velocity * offset.y,
                                     body->angular_velocity * offset.x }
                    ),
                    fluid_scale(body->impulse, body->inverse_mass)
                );
                float approach = fluid_dot(fluid_sub(node->velocity, surface), normal);
                if (approach >= 0) {
                    continue;
                }
                node->velocity = fluid_sub(node->velocity, fluid_scale(normal, approach));
                body->impulse = fluid_add(body->impulse, fluid_scale(normal, approach * node->mass));
                particle_work *w = &fluid->work[node->rep];
                if (-approach > w->contact_closing) {
                    w->contact_closing = -approach;
                    w->contact_normal = normal;
                    w->contact_obstacle = i;
                }
            }
        }
    }
}

/* Particles take the grid's velocity and its gradient (APIC), move, and
 * update their deformation; the interpolated terrain distance keeps them
 * out of solids. */
static void scatter(int start, int end, uint32_t, void *context)
{
    const mpm_pass *pass = context;
    material_fluid *fluid = pass->fluid;
    material_mpm *m = pass->mpm;
    float inverse = 4 / (m->dx * m->dx);
    for (int n = start; n < end; n++) {
        const mpm_point *point = &m->points[n];
        fluid_particle *p = &fluid->particles[point->particle];
        const mpm_block *base = &m->blocks[point->cell / BLOCK_NODES];
        int lx = point->cell % BLOCK, ly = point->cell % BLOCK_NODES / BLOCK;
        const float *wx = point->wx, *wy = point->wy;
        fluid_vec v = { 0 }, normal = { 0 };
        fluid_matrix b = { 0 };
        float distance = 0;
        for (int oy = 0; oy < 3; oy++) {
            for (int ox = 0; ox < 3; ox++) {
                int x = lx + ox, y = ly + oy;
                int block = base->neighbor[(y / BLOCK + 1) * 3 + x / BLOCK + 1];
                int index = block * BLOCK_NODES + (y % BLOCK) * BLOCK + x % BLOCK;
                float w = wx[ox] * wy[oy];
                const mpm_node *node = &m->nodes[index];
                const mpm_ground
                    *ground = &m->ground[m->blocks[index / BLOCK_NODES].ground + index % BLOCK_NODES];
                fluid_vec offset = { (ox - point->fx) * m->dx,
                                     (oy - point->fy) * m->dx };
                fluid_vec nv = fluid_scale(node->velocity, w);
                v = fluid_add(v, nv);
                b = mat_add(
                    b,
                    mat(nv.x * offset.x,
                        nv.x * offset.y,
                        nv.y * offset.x,
                        nv.y * offset.y)
                );
                distance += w * ground->distance;
                normal = fluid_add(normal, fluid_scale(ground->normal, w));
            }
        }
        const fluid_substance *s = &fluid->substances[p->substance];
        p->velocity = v;
        p->affine = mat_scale(b, inverse);
        p->position = fluid_add(p->position, fluid_scale(v, m->dt));
        float length = sqrtf(fluid_dot(normal, normal));
        float skin = m->dx * .25f,
              reach = distance + fluid_dot(v, normal) / fmaxf(length, 1e-6f) * m->dt;
        if (length > 1e-6f && reach < skin) {
            normal = fluid_scale(normal, 1 / length);
            p->position = fluid_add(p->position, fluid_scale(normal, fminf(skin - reach, m->dx)));
            float approach = fluid_dot(p->velocity, normal);
            if (approach < 0) {
                p->velocity = fluid_sub(p->velocity, fluid_scale(normal, approach));
            }
        }
        fluid_matrix f = mat_mul(mat_add(mat(1, 0, 0, 1), mat_scale(p->affine, m->dt)), p->deformation);
        if (s->phase == FLUID_LIQUID) {
            float j = fminf(1 + STRETCH * s->cohesion, fmaxf(J_MIN, mat_det(f)));
            p->deformation = mat(sqrtf(j), 0, 0, sqrtf(j));
            continue;
        }
        fluid_matrix u, vt;
        float sigma[2];
        svd(f, &u, sigma, &vt);
        if (s->phase == FLUID_POWDER) {
            yield(m, s, sigma);
        }
        p->deformation = compose(u, sigma, vt);
    }
}

void material_mpm_step(material_fluid *fluid, material_mpm *m)
{
    aclear(m->active);
    for (int n = 0; n < acount(fluid->active); n++) {
        int i = fluid->active[n];
        if (fluid->particles[i].phase != FLUID_GAS && !fluid->work[i].doomed) {
            fluid->work[i].contact_obstacle = -1;
            fluid->work[i].contact_closing = 0;
            apush(m->active, i);
        }
    }
    aclear(m->ground_keys);
    if (m->ground_capacity) {
        memset(m->ground_slots, 0, (size_t)m->ground_capacity * sizeof *m->ground_slots);
    }
    if (!acount(m->active)) {
        return;
    }
    m->dx = material_fluid_spacing(fluid, FLUID_LIQUID) * 2;
    m->dt = fluid->step->delta / SUBSTEPS;
    float longest = LONGEST_STEP / SUBSTEPS;
    m->speed = COURANT * m->dx / longest;
    m->viscous = .2f * m->dx * m->dx / longest;
    mpm_pass pass = { fluid, m };
    for (int substep = 0; substep < SUBSTEPS; substep++) {
        CHIBI_TRACE_ZONE_BEGIN(build_zone, "MPM blocks");
        build_blocks(fluid, m);
        sort_points(m);
        cache_ground(fluid, m);
        CHIBI_TRACE_ZONE_END(build_zone);
        int count = acount(m->active), blocks = acount(m->blocks);
        asetlen(m->points, count);
        asetlen(m->nodes, blocks * BLOCK_NODES);
        CHIBI_TRACE_ZONE_BEGIN(p2g_zone, "MPM particle to grid");
        material_fluid_run(fluid, prepare_points, count, &pass, 256);
        material_fluid_run(fluid, gather, blocks, &pass, 4);
        CHIBI_TRACE_ZONE_END(p2g_zone);
        CHIBI_TRACE_ZONE_BEGIN(grid_zone, "MPM grid");
        material_fluid_run(fluid, update_grid, blocks, &pass, 8);
        couple_bodies(fluid, m, (substep + 1.f) / SUBSTEPS);
        report_contacts(fluid, m);
        CHIBI_TRACE_ZONE_END(grid_zone);
        CHIBI_TRACE_ZONE_BEGIN(g2p_zone, "MPM grid to particle");
        material_fluid_run(fluid, scatter, count, &pass, 256);
        CHIBI_TRACE_ZONE_END(g2p_zone);
    }
}
