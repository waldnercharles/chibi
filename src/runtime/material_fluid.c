#include "runtime/material_fluid.h"

#include "diagnostics/tracy.h"
#include "render/particles.h"
#include "runtime/material_flow.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    fluid_vec previous, correction;
    float pressure, near_pressure, heat;
    int x, y, next;
} particle_work;

struct material_fluid
{
    fluid_particle *particles;
    particle_work *work;
    int *heads, *active, *used;
    fluid_blast *blasts;
    fluid_reaction reaction;
    fluid_region bounds;
    int capacity, buckets;
    float kernel, delta;
    game_task_system tasks;
    material_flow *flow;
    fluid_flow_settings flow_settings;
};

static fluid_vec add(fluid_vec a, fluid_vec b)
{
    return (fluid_vec){ a.x + b.x, a.y + b.y };
}
static fluid_vec sub(fluid_vec a, fluid_vec b)
{
    return (fluid_vec){ a.x - b.x, a.y - b.y };
}
static fluid_vec scale(fluid_vec a, float s)
{
    return (fluid_vec){ a.x * s, a.y * s };
}
static float dot(fluid_vec a, fluid_vec b)
{
    return a.x * b.x + a.y * b.y;
}

material_fluid *material_fluid_new(float kernel, int capacity)
{
    assert(isfinite(kernel) && kernel > 0 && capacity > 0 && capacity <= 1000000);
    material_fluid *fluid = xcalloc(1, sizeof *fluid);
    fluid->kernel = kernel;
    fluid->capacity = capacity;
    fluid->buckets = 1;
    while (fluid->buckets < capacity * 2) {
        fluid->buckets *= 2;
    }
    fluid->heads = xmalloc((size_t)fluid->buckets * sizeof *fluid->heads);
    memset(fluid->heads, -1, (size_t)fluid->buckets * sizeof *fluid->heads);
    asetcap(fluid->particles, capacity);
    asetcap(fluid->work, capacity);
    asetcap(fluid->active, capacity);
    fluid->flow = material_flow_new();
    return fluid;
}

void material_fluid_free(material_fluid *fluid)
{
    afree(fluid->particles);
    afree(fluid->work);
    afree(fluid->active);
    afree(fluid->used);
    afree(fluid->blasts);
    free(fluid->heads);
    material_flow_free(fluid->flow);
    free(fluid);
}

void material_fluid_clear(material_fluid *fluid)
{
    aclear(fluid->particles);
    aclear(fluid->work);
    aclear(fluid->active);
    aclear(fluid->blasts);
    material_flow_clear(fluid->flow);
}

void material_fluid_set_flow(material_fluid *fluid, fluid_flow_settings settings)
{
    assert(isfinite(settings.cell_size + settings.vorticity + settings.drag));
    assert(settings.cell_size == 0 || (settings.cell_size >= 2 && settings.cell_size <= 64));
    assert(
        settings.vorticity >= 0 && settings.vorticity <= 10 &&
        settings.drag >= 0 && settings.drag <= 10
    );
    if (settings.cell_size != fluid->flow_settings.cell_size) {
        material_flow_clear(fluid->flow);
    }
    fluid->flow_settings = settings;
}

uint64_t material_fluid_flow_hash(const material_fluid *fluid)
{
    return fluid->flow_settings.cell_size > 0 ? material_flow_hash(fluid->flow) : 0;
}

void material_fluid_set_reaction(material_fluid *fluid, fluid_reaction reaction)
{
    fluid->reaction = reaction;
}

const fluid_blast *material_fluid_blasts(const material_fluid *fluid, int *count)
{
    *count = acount(fluid->blasts);
    return fluid->blasts;
}

void material_fluid_set_tasks(material_fluid *fluid, const game_task_system *tasks)
{
    fluid->tasks = tasks ? *tasks : (game_task_system){ 0 };
}

bool material_fluid_add(material_fluid *fluid, fluid_particle p)
{
    assert(isfinite(p.position.x) && isfinite(p.position.y));
    assert(isfinite(p.velocity.x) && isfinite(p.velocity.y));
    assert(isfinite(p.mass) && p.mass > 0 && isfinite(p.radius) && p.radius > 0);
    assert(p.radius <= fluid->kernel);
    assert(isfinite(p.visibility) && p.visibility >= 0 && p.visibility <= 1);
    assert(isfinite(p.refraction) && p.refraction >= 0 && p.refraction <= 16);
    assert(isfinite(p.fuel) && p.fuel >= 0 && isfinite(p.heat) && p.heat >= 0);
    assert(
        (p.kind == FLUID_GAS || p.kind == FLUID_LIQUID) &&
        isfinite(p.lifetime) && p.lifetime >= 0
    );
    if (acount(fluid->particles) == fluid->capacity) {
        return false;
    }
    apush(fluid->particles, p);
    apush(fluid->work, ((particle_work){ 0 }));
    return true;
}

const fluid_particle *material_fluid_particles(const material_fluid *fluid, int *count)
{
    *count = acount(fluid->particles);
    return fluid->particles;
}

int material_fluid_active_count(const material_fluid *fluid)
{
    return acount(fluid->active);
}

static bool awake(fluid_vec p, float size, const fluid_region *regions, int count)
{
    float x = floorf(p.x / size) * size;
    float y = floorf(p.y / size) * size;
    for (int i = 0; i < count; i++) {
        fluid_region r = regions[i];
        if (x < r.x + r.width && x + size > r.x && y < r.y + r.height &&
            y + size > r.y) {
            return true;
        }
    }
    return false;
}

static unsigned bucket(const material_fluid *fluid, int x, int y)
{
    return ((unsigned)x * 73856093u ^ (unsigned)y * 19349663u) & (fluid->buckets - 1);
}

static void index_particles(material_fluid *fluid)
{
    for (int n = 0; n < acount(fluid->used); n++) {
        fluid->heads[fluid->used[n]] = -1;
    }
    aclear(fluid->used);
    fluid_vec low = { INFINITY, INFINITY }, high = { -INFINITY, -INFINITY };
    for (int n = 0; n < acount(fluid->active); n++) {
        int i = fluid->active[n];
        particle_work *w = &fluid->work[i];
        fluid_vec p = fluid->particles[i].position;
        w->x = (int)floorf(p.x / fluid->kernel);
        w->y = (int)floorf(p.y / fluid->kernel);
        unsigned h = bucket(fluid, w->x, w->y);
        if (fluid->heads[h] < 0) {
            apush(fluid->used, (int)h);
        }
        low.x = fminf(low.x, p.x);
        low.y = fminf(low.y, p.y);
        high.x = fmaxf(high.x, p.x);
        high.y = fmaxf(high.y, p.y);
        w->next = fluid->heads[h];
        fluid->heads[h] = i;
    }
    fluid->bounds = (fluid_region){ low.x, low.y, high.x - low.x, high.y - low.y };
}

static void densities(int start, int end, uint32_t, void *context)
{
    material_fluid *fluid = context;
    float h = fluid->kernel;
    for (int n = start; n < end; n++) {
        int i = fluid->active[n];
        fluid_particle p = fluid->particles[i];
        particle_work *w = &fluid->work[i];
        if (fluid->flow_settings.cell_size > 0 && p.kind == FLUID_GAS) {
            w->pressure = w->near_pressure = 0;
            continue;
        }
        float density = 0, near_density = 0;
        for (int y = w->y - 1; y <= w->y + 1; y++) {
            for (int x = w->x - 1; x <= w->x + 1; x++) {
                for (int j = fluid->heads[bucket(fluid, x, y)]; j >= 0;
                     j = fluid->work[j].next) {
                    if (j == i || fluid->work[j].x != x || fluid->work[j].y != y ||
                        fluid->particles[j].kind != p.kind) {
                        continue;
                    }
                    fluid_vec d = sub(fluid->particles[j].position, p.position);
                    float distance = dot(d, d);
                    if (distance >= h * h) {
                        continue;
                    }
                    float q = 1 - sqrtf(distance) / h;
                    density += q * q;
                    near_density += q * q * q;
                }
            }
        }
        /* Double-density relaxation supplies a cheap cohesive liquid. Gas
         * tracers repel only when compressed, so an undisturbed cloud stays. */
        w->pressure = p.kind == FLUID_LIQUID ? 600 * (density - 6)
                                             : 250 * fmaxf(0, density - 5);
        w->near_pressure = p.kind == FLUID_LIQUID ? 1200 * near_density : 0;
    }
}

static void relax(int start, int end, uint32_t, void *context)
{
    material_fluid *fluid = context;
    float h = fluid->kernel, dt = fluid->delta;
    for (int n = start; n < end; n++) {
        int i = fluid->active[n];
        fluid_particle p = fluid->particles[i];
        particle_work *w = &fluid->work[i];
        fluid_vec correction = { 0 };
        for (int y = w->y - 1; y <= w->y + 1; y++) {
            for (int x = w->x - 1; x <= w->x + 1; x++) {
                for (int j = fluid->heads[bucket(fluid, x, y)]; j >= 0;
                     j = fluid->work[j].next) {
                    particle_work *neighbor = &fluid->work[j];
                    if (j == i || neighbor->x != x || neighbor->y != y) {
                        continue;
                    }
                    fluid_particle other = fluid->particles[j];
                    if (fluid->flow_settings.cell_size > 0 &&
                        p.kind == FLUID_GAS && other.kind == FLUID_GAS) {
                        continue;
                    }
                    fluid_vec d = sub(other.position, p.position);
                    float distance = dot(d, d);
                    if (distance >= h * h) {
                        continue;
                    }
                    if (distance < 1e-8f) {
                        d = (fluid_vec){ i < j ? .001f : -.001f, 0 };
                        distance = 1e-6f;
                    }
                    distance = sqrtf(distance);
                    float q = 1 - distance / h;
                    float pressure = 400 * q * q;
                    if (p.kind == other.kind) {
                        pressure = .5f * ((w->pressure + neighbor->pressure) * q +
                                          (w->near_pressure + neighbor->near_pressure) *
                                              q * q);
                    }
                    /* Equal and opposite pair corrections, weighted by mass.
                     * Clamping individual pairs preserves their momentum. */
                    float displacement = fmaxf(-h * .025f, fminf(h * .025f, dt * dt * pressure));
                    float share = other.mass / (p.mass + other.mass);
                    correction = sub(correction, scale(d, share * displacement / distance));
                    fluid_vec relative = sub(other.velocity, p.velocity);
                    correction = add(correction, scale(relative, share * q * q * dt * .04f));
                }
            }
        }
        w->correction = correction;
    }
}

static void run(material_fluid *fluid, game_task_range *fn, void *context, int grain)
{
    int count = acount(fluid->active);
    if (fluid->tasks.workers > 1 && count >= 2048) {
        void *task = fluid->tasks
                         .enqueue(fn, count, grain, context, fluid->tasks.user);
        if (task) {
            fluid->tasks.finish(task, fluid->tasks.user);
        }
        return;
    }
    fn(0, count, 0, context);
}

static fluid_vec rotate(fluid_vec p, float c, float s)
{
    return (fluid_vec){ c * p.x - s * p.y, s * p.x + c * p.y };
}

static bool segment_box(fluid_vec a, fluid_vec b, fluid_vec half)
{
    fluid_vec d = sub(b, a);
    float first = .001f, last = .999f;
    float origin[] = { a.x, a.y }, delta[] = { d.x, d.y },
          extent[] = { half.x, half.y };
    for (int axis = 0; axis < 2; axis++) {
        if (fabsf(delta[axis]) < 1e-6f) {
            if (fabsf(origin[axis]) > extent[axis]) {
                return false;
            }
            continue;
        }
        float l = (-extent[axis] - origin[axis]) / delta[axis];
        float r = (extent[axis] - origin[axis]) / delta[axis];
        first = fmaxf(first, fminf(l, r));
        last = fminf(last, fmaxf(l, r));
        if (first > last) {
            return false;
        }
    }
    return true;
}

bool material_fluid_blocked(
    fluid_vec a,
    fluid_vec b,
    const fluid_body *bodies,
    const fluid_obstacle *obstacles,
    int count,
    int ignore_body
)
{
    fluid_vec low = { fminf(a.x, b.x), fminf(a.y, b.y) },
              high = { fmaxf(a.x, b.x), fmaxf(a.y, b.y) };
    for (int i = 0; i < count; i++) {
        fluid_obstacle o = obstacles[i];
        const fluid_body *body = &bodies[o.body];
        if (o.body == ignore_body || body->heat > 0) {
            continue;
        }
        /* A rotation-independent bound rejects distant compound pieces before
         * the local transform and exact segment test. The L1 radius encloses
         * both the local offset and every rotated box/circle. */
        float reach = fabsf(o.center.x) + fabsf(o.center.y) +
                      (o.radius > 0 ? o.radius : o.half_size.x + o.half_size.y);
        if (body->position.x + reach < low.x || body->position.x - reach > high.x ||
            body->position.y + reach < low.y || body->position.y - reach > high.y) {
            continue;
        }
        float c = 1, s = 0;
        if (body->angle != 0) {
            c = cosf(body->angle);
            s = sinf(body->angle);
        }
        fluid_vec from = sub(rotate(sub(a, body->position), c, -s), o.center);
        fluid_vec to = sub(rotate(sub(b, body->position), c, -s), o.center);
        if (o.radius <= 0) {
            if (segment_box(from, to, o.half_size)) {
                return true;
            }
            continue;
        }
        fluid_vec d = sub(to, from);
        float t = fmaxf(.001f, fminf(.999f, -dot(from, d) / fmaxf(dot(d, d), 1e-8f)));
        fluid_vec nearest = add(from, scale(d, t));
        if (dot(nearest, nearest) < o.radius * o.radius) {
            return true;
        }
    }
    return false;
}

static void ignition(material_fluid *fluid, fluid_vec p)
{
    float size = fluid->kernel * 4;
    int x = (int)floorf(p.x / size), y = (int)floorf(p.y / size);
    for (int i = 0; i < acount(fluid->blasts); i++) {
        fluid_blast *b = &fluid->blasts[i];
        if ((int)floorf(b->position.x / size) == x &&
            (int)floorf(b->position.y / size) == y) {
            b->ignitions++;
            return;
        }
    }
    /* A pathological simultaneous ignition remains burning but has bounded
     * blast work. The earliest positions are stable in particle order. */
    if (acount(fluid->blasts) < 32) {
        apush(fluid->blasts, ((fluid_blast){ p, 1 }));
    }
}

typedef struct
{
    material_fluid *fluid;
    float dt;
    const fluid_body *bodies;
    int body_count;
    const fluid_obstacle *obstacles;
    int obstacle_count;
} reaction_query;

static void reaction_heat(int start, int end, uint32_t, void *context)
{
    const reaction_query *query = context;
    material_fluid *fluid = query->fluid;
    const fluid_body *bodies = query->bodies;
    const fluid_obstacle *obstacles = query->obstacles;
    int body_count = query->body_count, obstacle_count = query->obstacle_count;
    float dt = query->dt;
    fluid_reaction r = fluid->reaction;
    float h = fluid->kernel;
    for (int n = start; n < end; n++) {
        int i = fluid->active[n];
        fluid_particle p = fluid->particles[i];
        particle_work *w = &fluid->work[i];
        w->heat = p.heat;
        if (p.fuel <= 0 || p.burning) {
            continue;
        }
        for (int k = 0; k < body_count; k++) {
            fluid_body body = bodies[k];
            if (body.heat < r.ignition) {
                continue;
            }
            fluid_vec from = sub(body.position, scale(body.velocity, dt));
            fluid_vec d = sub(body.position, from);
            float t = fmaxf(0, fminf(1, dot(sub(p.position, from), d) / fmaxf(dot(d, d), 1e-8f)));
            fluid_vec near = add(from, scale(d, t)), gap = sub(p.position, near);
            if (dot(gap, gap) <= h * h &&
                !material_fluid_blocked(near, p.position, bodies, obstacles, obstacle_count, k)) {
                w->heat = fmaxf(w->heat, body.heat);
            }
        }
        for (int y = w->y - 1; y <= w->y + 1; y++) {
            for (int x = w->x - 1; x <= w->x + 1; x++) {
                for (int j = fluid->heads[bucket(fluid, x, y)]; j >= 0;
                     j = fluid->work[j].next) {
                    fluid_particle other = fluid->particles[j];
                    if (fluid->work[j].x != x || fluid->work[j].y != y ||
                        other.heat <= w->heat) {
                        continue;
                    }
                    fluid_vec d = sub(other.position, p.position);
                    if (dot(d, d) >= h * h ||
                        material_fluid_blocked(other.position, p.position, bodies, obstacles, obstacle_count, -1)) {
                        continue;
                    }
                    w->heat = fmaxf(w->heat, other.heat * .85f);
                }
            }
        }
    }
}

static void react(
    material_fluid *fluid,
    float dt,
    const fluid_body *bodies,
    int body_count,
    const fluid_obstacle *obstacles,
    int obstacle_count
)
{
    fluid_reaction r = fluid->reaction;
    if (r.ignition <= 0) {
        return;
    }
    /* Queries read the same particle snapshot and write only their own heat.
     * Commit ignition and blast events in particle order after workers finish. */
    reaction_query query = { fluid,      dt,        bodies,
                             body_count, obstacles, obstacle_count };
    run(fluid, reaction_heat, &query, 32);
    for (int n = 0; n < acount(fluid->active); n++) {
        int i = fluid->active[n];
        fluid_particle *p = &fluid->particles[i];
        p->heat = fluid->work[i].heat;
        if (p->fuel > 0 && p->heat >= r.ignition) {
            if (!p->burning) {
                ignition(fluid, p->position);
            }
            p->burning = true;
            p->glow = 1;
            p->fuel = fmaxf(0, p->fuel - r.burn_rate * dt);
            p->heat = fmaxf(p->heat, r.heat_release);
            p->color = r.flame_color;
            p->emission = r.flame_emission;
            if (p->fuel == 0) {
                p->burning = false;
                p->smoke = true;
                p->kind = FLUID_GAS;
                p->color = r.smoke_color;
                p->absorption = r.smoke_absorption;
                p->lifetime = r.smoke_lifetime;
                p->style = (fluid_style){ .duration = r.smoke_lifetime };
            }
        }
        if (!p->burning && p->heat > 0) {
            float cooling = expf(-r.cooling * dt);
            p->heat *= cooling;
            p->emission *= cooling;
        }
        if (p->smoke) {
            p->glow = fminf(1, p->heat / r.heat_release);
            unsigned color = 0;
            for (int channel = 0; channel < 4; channel++) {
                int shift = 24 - 8 * channel;
                float cold = (r.smoke_color >> shift) & 255;
                float hot = (r.flame_color >> shift) & 255;
                color |= (unsigned)(cold + (hot - cold) * p->glow) << shift;
            }
            p->color = color;
        }
    }
}

void material_fluid_push(
    material_fluid *fluid,
    fluid_vec point,
    float radius,
    float speed,
    const fluid_body *bodies,
    const fluid_obstacle *obstacles,
    int count
)
{
    for (int n = 0; n < acount(fluid->active); n++) {
        fluid_particle *p = &fluid->particles[fluid->active[n]];
        fluid_vec d = sub(p->position, point);
        float distance = sqrtf(dot(d, d));
        if (distance <= 1e-5f || distance >= radius ||
            material_fluid_blocked(point, p->position, bodies, obstacles, count, -1)) {
            continue;
        }
        p->velocity = add(p->velocity, scale(d, speed * (1 - distance / radius) / distance));
    }
}

static void collide(fluid_particle *p, fluid_body *body, fluid_obstacle obstacle, float dt)
{
    float c = cosf(body->angle), s = sinf(body->angle);
    fluid_vec
        local = sub(rotate(sub(p->position, body->position), c, -s), obstacle.center);
    fluid_vec normal;
    float depth;
    float skin = p->radius * .3f;
    if (obstacle.radius > 0) {
        float distance = sqrtf(dot(local, local));
        depth = obstacle.radius + skin - distance;
        normal = distance > 1e-6f ? scale(local, 1 / distance) : (fluid_vec){ 1, 0 };
    } else {
        float dx = obstacle.half_size.x + skin - fabsf(local.x);
        float dy = obstacle.half_size.y + skin - fabsf(local.y);
        depth = fminf(dx, dy);
        normal = dx < dy ? (fluid_vec){ local.x < 0 ? -1 : 1, 0 }
                         : (fluid_vec){ 0, local.y < 0 ? -1 : 1 };
    }
    if (depth <= 0) {
        return;
    }
    normal = rotate(normal, c, s);
    p->position = add(p->position, scale(normal, depth));
    fluid_vec body_velocity = add(body->velocity, scale(body->impulse, body->inverse_mass));
    float closing = dot(sub(p->velocity, body_velocity), normal);
    float impulse = fmaxf(0, -closing + fminf(depth / dt, 80) * .2f) /
                    (1 / p->mass + body->inverse_mass);
    p->velocity = add(p->velocity, scale(normal, impulse / p->mass));
    body->impulse = sub(body->impulse, scale(normal, impulse));
}

static void collide_obstacles(
    material_fluid *fluid,
    fluid_body *bodies,
    const fluid_obstacle *obstacles,
    int count
)
{
    /* Query the particle hash with each collider's AABB, instead of testing
     * every particle against every cave wall. Corrections use a fresh index. */
    if (!acount(fluid->active)) {
        return;
    }
    index_particles(fluid);
    for (int i = 0; i < count; i++) {
        fluid_obstacle obstacle = obstacles[i];
        fluid_body *body = &bodies[obstacle.body];
        float c = cosf(body->angle), s = sinf(body->angle);
        fluid_vec center = add(body->position, rotate(obstacle.center, c, s));
        float ex = obstacle.radius > 0 ? obstacle.radius
                                       : fabsf(c) * obstacle.half_size.x +
                                             fabsf(s) * obstacle.half_size.y;
        float ey = obstacle.radius > 0 ? obstacle.radius
                                       : fabsf(s) * obstacle.half_size.x +
                                             fabsf(c) * obstacle.half_size.y;
        fluid_region bounds = fluid->bounds;
        float skin = fluid->kernel;
        if (center.x + ex + skin < bounds.x || center.y + ey + skin < bounds.y ||
            center.x - ex - skin > bounds.x + bounds.width ||
            center.y - ey - skin > bounds.y + bounds.height) {
            continue;
        }
        int x0 = (int)floorf((center.x - ex - fluid->kernel) / fluid->kernel);
        int y0 = (int)floorf((center.y - ey - fluid->kernel) / fluid->kernel);
        int x1 = (int)floorf((center.x + ex + fluid->kernel) / fluid->kernel);
        int y1 = (int)floorf((center.y + ey + fluid->kernel) / fluid->kernel);
        for (int y = y0; y <= y1; y++) {
            for (int x = x0; x <= x1; x++) {
                for (int j = fluid->heads[bucket(fluid, x, y)]; j >= 0;
                     j = fluid->work[j].next) {
                    if (fluid->work[j].x == x && fluid->work[j].y == y) {
                        collide(&fluid->particles[j], body, obstacle, fluid->delta);
                    }
                }
            }
        }
    }
}

void material_fluid_tick(
    material_fluid *fluid,
    float delta,
    float chunk_size,
    const fluid_region *regions,
    int region_count,
    fluid_body *bodies,
    int body_count,
    const fluid_obstacle *obstacles,
    int obstacle_count
)
{
    assert(delta > 0 && delta <= 1.f / 60 + 1e-6f && isfinite(chunk_size) && chunk_size > 0);
    for (int i = 0; i < body_count; i++) {
        bodies[i].impulse = (fluid_vec){ 0 };
    }
    for (int i = 0; i < obstacle_count; i++) {
        assert(obstacles[i].body >= 0 && obstacles[i].body < body_count);
    }
    aclear(fluid->active);
    aclear(fluid->blasts);
    for (int i = 0; i < acount(fluid->particles);) {
        fluid_particle *p = &fluid->particles[i];
        if (!awake(p->position, chunk_size, regions, region_count)) {
            i++;
            continue;
        }
        if (p->lifetime > 0) {
            p->lifetime -= delta;
            if (p->lifetime <= 0) {
                *p = alast(fluid->particles);
                apop(fluid->particles);
                apop(fluid->work);
                continue;
            }
        }
        if (p->style.duration > 0) {
            float age = 1 - p->lifetime / p->style.duration;
            float drag = game_particle_curve_value(
                p->style.drag,
                p->style.drag_end,
                p->style.drag_exponent,
                age
            );
            float damping = expf(-drag * delta);
            p->velocity.x *= damping;
            p->velocity.y *= damping;
        }
        apush(fluid->active, i++);
    }
    index_particles(fluid);
    CHIBI_TRACE_ZONE_BEGIN(reaction_zone, "Fluid reactions");
    react(fluid, delta, bodies, body_count, obstacles, obstacle_count);
    CHIBI_TRACE_ZONE_END(reaction_zone);
    if (fluid->flow_settings.cell_size > 0) {
        CHIBI_TRACE_ZONE_BEGIN(flow_zone, "Gas flow");
        material_flow_step(
            fluid->flow,
            fluid->flow_settings,
            delta,
            fluid->particles,
            fluid->active,
            acount(fluid->active),
            bodies,
            obstacles,
            obstacle_count
        );
        CHIBI_TRACE_ZONE_END(flow_zone);
    }
    fluid->delta = delta * .5f;
    for (int step = 0; step < 2; step++) {
        for (int n = 0; n < acount(fluid->active); n++) {
            int i = fluid->active[n];
            fluid_particle *p = &fluid->particles[i];
            fluid->work[i].previous = p->position;
            p->position = add(p->position, scale(p->velocity, fluid->delta));
        }
        CHIBI_TRACE_ZONE_BEGIN(index_zone, "Fluid index");
        index_particles(fluid);
        CHIBI_TRACE_ZONE_END(index_zone);
        CHIBI_TRACE_ZONE_BEGIN(density_zone, "Fluid density");
        run(fluid, densities, fluid, 256);
        CHIBI_TRACE_ZONE_END(density_zone);
        CHIBI_TRACE_ZONE_BEGIN(relax_zone, "Fluid relaxation");
        run(fluid, relax, fluid, 256);
        CHIBI_TRACE_ZONE_END(relax_zone);
        for (int n = 0; n < acount(fluid->active); n++) {
            int i = fluid->active[n];
            fluid_particle *p = &fluid->particles[i];
            p->position = add(p->position, fluid->work[i].correction);
            p->velocity = scale(
                sub(p->position, fluid->work[i].previous),
                1 / fluid->delta
            );
        }
        CHIBI_TRACE_ZONE_BEGIN(collision_zone, "Fluid collisions");
        collide_obstacles(fluid, bodies, obstacles, obstacle_count);
        CHIBI_TRACE_ZONE_END(collision_zone);
    }
}
