#include "runtime/material_fluid.h"

#include "diagnostics/tracy.h"
#include "runtime/material_flow.h"
#include "runtime/material_mpm.h"
#include "runtime/material_obstacle_index.h"
#include "runtime/material_particle_index.h"
#include "runtime/particles.h"
#include "support/array.h"
#include "support/cos_sin.h"
#include "support/exponential.h"
#include "support/xalloc.h"

#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct
{
    fluid_vec previous, correction;
    float pressure, near_pressure, heat;
    double thermal_rate;
    bool thermal_interface, contact_corrected;
    int x, y, next;
} particle_work;

typedef struct
{
    int body_index, patch_x, patch_y, next;
    double weight, point_x, point_y, normal_x, normal_y, emission;
    double color[4];
} contact_work;

typedef struct
{
    fluid_obstacle obstacle;
    fluid_body pose;
    cos_sin rotation;
} closure_collider;

struct material_fluid
{
    fluid_particle *particles;
    particle_work *work;
    int *heads, *active, *used;
    int *selected;
    closure_collider *closure;
    fluid_body *closure_bodies;
    fluid_obstacle *closure_shapes;
    int *closure_query;
    material_obstacle_index *closure_index;
    material_particle_index *chunks;
    material_obstacle_index *obstacles;
    float chunk_size, visual_support;
    int *contact_heads, *contact_used;
    fluid_contact *contacts;
    contact_work *contact_work;
    fluid_blast *blasts;
    fluid_reaction reaction;
    fluid_region bounds;
    int capacity, buckets;
    float kernel, delta;
    fluid_contact_settings contact_settings;
    task_system tasks;
    material_flow *flow;
    fluid_flow_settings flow_settings;
    material_mpm *mpm;
    fluid_mpm_settings mpm_settings;
    fluid_mpm_state mpm_state;
    int solver, debug_view, collision_kind;
    int thermal_population, thermal_active;
    float thermal_low, thermal_high;
    bool hot_particles, ignition_candidate, burning, smoke;
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

static fluid_vec body_surface_velocity(fluid_body body, fluid_vec point)
{
    fluid_vec radius = sub(point, add(body.position, body.center_offset));
    float angular = body.angular_velocity + body.angular_impulse * body.inverse_inertia;
    return add(body.velocity, (fluid_vec){ -angular * radius.y, angular * radius.x });
}

static unsigned contact_bucket(const material_fluid *fluid, int body, int x, int y, int kind)
{
    unsigned h = (unsigned)body * 0x9e3779b9u ^ (unsigned)x * 0x85ebca6bu ^
                 (unsigned)y * 0xc2b2ae35u ^ (unsigned)kind * 0x27d4eb2fu;
    h ^= h >> 16;
    return h & (fluid->buckets - 1);
}

static void clear_contacts(material_fluid *fluid)
{
    for (int i = 0; i < acount(fluid->contact_used); i++) {
        fluid->contact_heads[fluid->contact_used[i]] = -1;
    }
    aclear(fluid->contact_used);
    aclear(fluid->contacts);
    aclear(fluid->contact_work);
}

static float contact_channel(unsigned color, int shift)
{
    return (float)((color >> shift) & 255u);
}

static unsigned pack_contact_color(const contact_work *work)
{
    unsigned color = 0;
    for (int shift = 24; shift >= 0; shift -= 8) {
        unsigned channel = (unsigned)
            fmin(255, fmax(0, work->color[(24 - shift) / 8] / work->weight + .5));
        color |= channel << shift;
    }
    return color;
}

static void record_contact(
    material_fluid *fluid,
    const fluid_particle *particle,
    const fluid_body *body,
    int body_index,
    fluid_vec point,
    fluid_vec normal,
    fluid_vec local_point,
    float closing_speed,
    float impulse
)
{
    float threshold = particle->kind == FLUID_GAS
                        ? fluid->contact_settings.gas_speed
                        : fluid->contact_settings.liquid_speed;
    if (threshold <= 0 || closing_speed < threshold) {
        return;
    }
    float patch_size = fluid->kernel * 2;
    int patch_x = (int)floorf(local_point.x / patch_size);
    int patch_y = (int)floorf(local_point.y / patch_size);
    unsigned
        bucket = contact_bucket(fluid, body_index, patch_x, patch_y, particle->kind);
    int at = fluid->contact_heads[bucket];
    while (at >= 0) {
        contact_work *work = &fluid->contact_work[at];
        if (work->body_index == body_index && work->patch_x == patch_x &&
            work->patch_y == patch_y && fluid->contacts[at].kind == particle->kind) {
            break;
        }
        at = work->next;
    }
    if (at < 0) {
        if (acount(fluid->contacts) >= MATERIAL_FLUID_CONTACT_LIMIT) {
            return;
        }
        if (fluid->contact_heads[bucket] < 0) {
            apush(fluid->contact_used, (int)bucket);
        }
        at = acount(fluid->contacts);
        apush(
            fluid->contacts,
            ((fluid_contact){ .body = body->owner, .kind = particle->kind })
        );
        apush(
            fluid->contact_work,
            ((contact_work){ .body_index = body_index,
                             .patch_x = patch_x,
                             .patch_y = patch_y,
                             .next = fluid->contact_heads[bucket] })
        );
        fluid->contact_heads[bucket] = at;
    }
    fluid_contact *contact = &fluid->contacts[at];
    contact_work *work = &fluid->contact_work[at];
    float weight = fmaxf(impulse, particle->mass * closing_speed);
    if (weight <= 0) {
        return;
    }
    work->weight += weight;
    work->point_x += point.x * weight;
    work->point_y += point.y * weight;
    work->normal_x += normal.x * weight;
    work->normal_y += normal.y * weight;
    work->color[0] += contact_channel(particle->color, 24) * weight;
    work->color[1] += contact_channel(particle->color, 16) * weight;
    work->color[2] += contact_channel(particle->color, 8) * weight;
    work->color[3] += contact_channel(particle->color, 0) * weight;
    contact->point = (fluid_vec){ (float)(work->point_x / work->weight),
                                  (float)(work->point_y / work->weight) };
    fluid_vec averaged = { (float)work->normal_x, (float)work->normal_y };
    float length = sqrtf(dot(averaged, averaged));
    contact->normal = length > 1e-6f ? scale(averaged, 1 / length) : normal;
    contact->closing_speed = fmaxf(contact->closing_speed, closing_speed);
    contact->impulse += impulse;
    contact->mass += particle->mass;
    work->emission += particle->emission * weight;
    contact->emission = (float)(work->emission / work->weight);
    contact->contacts++;
    contact->color = pack_contact_color(work);
}

material_fluid *material_fluid_new(float kernel, int capacity)
{
    assert(isfinite(kernel) && kernel > 0 && capacity > 0 && capacity <= 1000000);
    material_fluid *fluid = xcalloc(1, sizeof *fluid);
    fluid->kernel = kernel;
    fluid->capacity = capacity;
    fluid->collision_kind = -1;
    fluid->buckets = 1;
    while (fluid->buckets < capacity * 2) {
        fluid->buckets *= 2;
    }
    fluid->heads = xmalloc((size_t)fluid->buckets * sizeof *fluid->heads);
    fluid->contact_heads = xmalloc((size_t)fluid->buckets * sizeof *fluid->contact_heads);
    memset(fluid->heads, -1, (size_t)fluid->buckets * sizeof *fluid->heads);
    memset(fluid->contact_heads, -1, (size_t)fluid->buckets * sizeof *fluid->contact_heads);
    asetcap(fluid->particles, capacity);
    asetcap(fluid->work, capacity);
    asetcap(fluid->active, capacity);
    asetcap(fluid->contact_used, MATERIAL_FLUID_CONTACT_LIMIT);
    asetcap(fluid->contacts, MATERIAL_FLUID_CONTACT_LIMIT);
    asetcap(fluid->contact_work, MATERIAL_FLUID_CONTACT_LIMIT);
    fluid->flow = material_flow_new();
    fluid->mpm = material_mpm_new();
    fluid->chunk_size = 64;
    fluid->chunks = material_particle_index_new(fluid->chunk_size);
    fluid->obstacles = material_obstacle_index_new();
    fluid->closure_index = material_obstacle_index_new();
    return fluid;
}

void material_fluid_free(material_fluid *fluid)
{
    afree(fluid->particles);
    afree(fluid->work);
    afree(fluid->active);
    afree(fluid->selected);
    afree(fluid->closure);
    afree(fluid->closure_bodies);
    afree(fluid->closure_shapes);
    afree(fluid->closure_query);
    material_obstacle_index_free(fluid->closure_index);
    afree(fluid->used);
    afree(fluid->contact_used);
    afree(fluid->contacts);
    afree(fluid->contact_work);
    afree(fluid->blasts);
    free(fluid->heads);
    free(fluid->contact_heads);
    material_flow_free(fluid->flow);
    material_mpm_free(fluid->mpm);
    material_particle_index_free(fluid->chunks);
    material_obstacle_index_free(fluid->obstacles);
    free(fluid);
}

void material_fluid_clear(material_fluid *fluid)
{
    fluid->thermal_population = 0;
    aclear(fluid->particles);
    aclear(fluid->work);
    aclear(fluid->active);
    aclear(fluid->contact_used);
    aclear(fluid->contacts);
    aclear(fluid->blasts);
    memset(fluid->contact_heads, -1, (size_t)fluid->buckets * sizeof *fluid->contact_heads);
    material_flow_clear(fluid->flow);
    material_mpm_clear(fluid->mpm);
    material_particle_index_clear(fluid->chunks);
    fluid->mpm_state = (fluid_mpm_state){ 0 };
    fluid->visual_support = 0;
}

void material_fluid_set_solver(material_fluid *fluid, int solver, fluid_mpm_settings settings)
{
    assert(solver == FLUID_SOLVER_DDR || solver == FLUID_SOLVER_MPM);
    assert(isfinite(settings.gravity.x) && isfinite(settings.gravity.y));
    assert(
        solver != FLUID_SOLVER_MPM ||
        (isfinite(settings.cell_size) && settings.cell_size > 0 &&
         settings.max_substeps >= 1 && settings.max_substeps <= 128)
    );
    fluid->solver = solver;
    fluid->mpm_settings = settings;
}

int material_fluid_solver(const material_fluid *fluid)
{
    return fluid->solver;
}

void material_fluid_set_debug_view(material_fluid *fluid, int view)
{
    assert(view >= FLUID_VIEW_MATERIAL && view <= FLUID_VIEW_STRAIN);
    fluid->debug_view = view;
}

int material_fluid_debug_view(const material_fluid *fluid)
{
    return fluid->debug_view;
}

float material_fluid_visual_support(const material_fluid *fluid)
{
    return fluid->visual_support;
}

fluid_mpm_state material_fluid_mpm_state(const material_fluid *fluid)
{
    return fluid->mpm_state;
}

void material_fluid_query(const material_fluid *fluid, fluid_region region, int **indices)
{
    material_particle_index_query(fluid->chunks, &region, 1, indices);
}

void material_fluid_set_flow(material_fluid *fluid, fluid_flow_settings settings)
{
    assert(isfinite(settings.cell_size + settings.vorticity + settings.drag));
    assert(isfinite(settings.gravity.x) && isfinite(settings.gravity.y));
    assert(isfinite(settings.buoyancy) && settings.buoyancy >= 0 && settings.buoyancy <= 10);
    assert(isfinite(settings.ambient_heat) && settings.ambient_heat >= 0);
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

void material_fluid_set_contact_settings(material_fluid *fluid, fluid_contact_settings settings)
{
    assert(isfinite(settings.gas_speed + settings.liquid_speed));
    assert(settings.gas_speed >= 0 && settings.liquid_speed >= 0);
    fluid->contact_settings = settings;
}

const fluid_contact *material_fluid_contacts(const material_fluid *fluid, int *count)
{
    *count = acount(fluid->contacts);
    return acount(fluid->contacts) ? fluid->contacts : NULL;
}

uint64_t material_fluid_flow_hash(const material_fluid *fluid)
{
    return fluid->flow_settings.cell_size > 0 ? material_flow_hash(fluid->flow) : 0;
}

void material_fluid_set_reaction(material_fluid *fluid, fluid_reaction reaction)
{
    assert(isfinite(reaction.extinction) && reaction.extinction >= 0);
    assert(!reaction.extinction || reaction.extinction < reaction.ignition);
    fluid->reaction = reaction;
}

const fluid_blast *material_fluid_blasts(const material_fluid *fluid, int *count)
{
    *count = acount(fluid->blasts);
    return fluid->blasts;
}

void material_fluid_set_tasks(material_fluid *fluid, const task_system *tasks)
{
    fluid->tasks = tasks ? *tasks : (task_system){ 0 };
}

bool material_fluid_thermal_valid(fluid_thermal value)
{
    return isfinite(value.capacity) && isfinite(value.conductivity) &&
           value.capacity >= 0 && value.conductivity >= 0 &&
           (value.capacity > 0 || value.conductivity == 0);
}

bool material_fluid_constitutive_valid(fluid_constitutive value)
{
    return isfinite(value.density) && isfinite(value.bulk_modulus) &&
           isfinite(value.shear_modulus) && value.density > 0 &&
           value.bulk_modulus > 0 && value.shear_modulus >= 0;
}

int material_fluid_response(const fluid_body *bodies, int index)
{
    int response = bodies[index].part_of ? bodies[index].part_of - 1 : index;
    assert(response <= index && !bodies[response].part_of);
    return response;
}

bool material_fluid_add(material_fluid *fluid, fluid_particle p)
{
    assert(isfinite(p.position.x) && isfinite(p.position.y));
    assert(isfinite(p.velocity.x) && isfinite(p.velocity.y));
    assert(isfinite(p.mass) && p.mass > 0 && isfinite(p.radius) && p.radius > 0);
    assert(p.radius <= fluid->kernel);
    assert(isfinite(p.visibility) && p.visibility >= 0 && p.visibility <= 1);
    assert(isfinite(p.refraction) && p.refraction >= 0 && p.refraction <= 16);
    assert(
        isfinite(p.grain_radius) && p.grain_radius >= 0 &&
        p.grain_radius <= fluid->kernel
    );
    assert(isfinite(p.fuel) && p.fuel >= 0 && isfinite(p.heat) && p.heat >= 0);
    assert(material_fluid_thermal_valid(p.thermal));
    assert(p.thermal.capacity == 0 || isnormal(p.mass * p.thermal.capacity));
    assert(
        (p.kind == FLUID_GAS || p.kind == FLUID_LIQUID) &&
        isfinite(p.lifetime) && p.lifetime >= 0
    );
    if (acount(fluid->particles) == fluid->capacity) {
        return false;
    }
    p.base_color = p.color;
    p.base_emission = p.emission;
    p.base_glow = p.glow;
    if (fluid->solver == FLUID_SOLVER_MPM && p.kind != FLUID_GAS) {
        assert(material_fluid_constitutive_valid(p.constitutive));
    }
    p.deformation = (fluid_deformation){ .f = { 1, 0, 0, 1 }, .volume = 1 };
    material_particle_index_add(fluid->chunks, acount(fluid->particles), p.position);
    float radius = fmaxf(p.grain_radius, fmaxf(p.radius, p.style.size_end));
    float support = 3.6f * radius + fmaxf(0, p.style.blend);
    fluid->visual_support = fmaxf(fluid->visual_support, support);
    fluid->thermal_population += p.thermal.conductivity > 0;
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

static double profile_clock_ms()
{
    struct timespec now;
    int result = clock_gettime(CLOCK_MONOTONIC, &now);
    assert(!result);
    (void)result;
    return now.tv_sec * 1000.0 + now.tv_nsec * 1e-6;
}

static void index_particles(material_fluid *fluid, material_fluid_profile *profile)
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
    if (profile) {
        profile->index_builds++;
    }
}

static bool pair_blocked(const material_fluid *fluid, fluid_vec a, fluid_vec b)
{
    if (a.x > b.x || (a.x == b.x && a.y > b.y)) {
        fluid_vec swap = a;
        a = b;
        b = swap;
    }
    return material_obstacle_index_blocked(fluid->obstacles, a, b, -1, true);
}

static void densities(int start, int end, uint32_t, void *context)
{
    material_fluid *fluid = context;
    float h = fluid->kernel;
    for (int n = start; n < end; n++) {
        int i = fluid->active[n];
        fluid_particle p = fluid->particles[i];
        particle_work *w = &fluid->work[i];
        if ((fluid->flow_settings.cell_size > 0 && p.kind == FLUID_GAS) ||
            (fluid->solver == FLUID_SOLVER_MPM && p.kind == FLUID_LIQUID)) {
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
                    if (distance >= h * h ||
                        pair_blocked(fluid, p.position, fluid->particles[j].position)) {
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
                    if (fluid->solver == FLUID_SOLVER_MPM &&
                        p.kind == FLUID_LIQUID && other.kind == FLUID_LIQUID) {
                        continue;
                    }
                    if (fluid->flow_settings.cell_size > 0 &&
                        p.kind == FLUID_GAS && other.kind == FLUID_GAS) {
                        continue;
                    }
                    fluid_vec d = sub(other.position, p.position);
                    float distance = dot(d, d);
                    if (distance >= h * h ||
                        pair_blocked(fluid, p.position, other.position)) {
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
                    if (fluid->solver == FLUID_SOLVER_MPM && p.kind != other.kind) {
                        share *= fminf(1, fluid->mpm_state.advanced_time / (2 * dt));
                    }
                    correction = sub(correction, scale(d, share * displacement / distance));
                    fluid_vec relative = sub(other.velocity, p.velocity);
                    correction = add(correction, scale(relative, share * q * q * dt * .04f));
                }
            }
        }
        w->correction = correction;
    }
}

static void run(material_fluid *fluid, task_range *fn, void *context, int grain)
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

bool material_fluid_blocked(
    fluid_vec a,
    fluid_vec b,
    const fluid_body *bodies,
    const fluid_obstacle *obstacles,
    int count,
    int ignore_body
)
{
    return material_obstacle_blocked(bodies, obstacles, count, a, b, ignore_body, false);
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
    bool hot_bodies, hot_particles, exchange;
} reaction_query;

static double thermal_conductance(const reaction_query *query, fluid_particle p, fluid_particle other)
{
    if (other.thermal.capacity <= 0 || p.thermal.conductivity <= 0 ||
        other.thermal.conductivity <= 0) {
        return 0;
    }
    fluid_vec d = sub(other.position, p.position);
    float h = query->fluid->kernel, distance = dot(d, d);
    if (distance >= h * h) {
        return 0;
    }
    /* Both endpoints must make the same occlusion decision, including grazing
     * segments where reversing float arithmetic can change the intersection. */
    fluid_vec from = p.position, to = other.position;
    if (from.x > to.x || (from.x == to.x && from.y > to.y)) {
        from = other.position;
        to = p.position;
    }
    if (material_obstacle_index_blocked(query->fluid->obstacles, from, to, -1, true)) {
        return 0;
    }
    double a = p.thermal.conductivity, b = other.thermal.conductivity;
    double rate = 2 * a * b / (a + b);
    double capacity = fmin(
        (double)p.mass * p.thermal.capacity,
        (double)other.mass * other.thermal.capacity
    );
    double weight = 1 - sqrtf(distance) / h;
    return capacity * rate * weight * weight;
}

static bool thermal_interface(const material_fluid *fluid, int i)
{
    fluid_particle p = fluid->particles[i];
    if (p.thermal.conductivity <= 0) {
        return false;
    }
    const particle_work *w = &fluid->work[i];
    float radius_squared = fluid->kernel * fluid->kernel;
    for (int y = w->y - 1; y <= w->y + 1; y++) {
        for (int x = w->x - 1; x <= w->x + 1; x++) {
            for (int j = fluid->heads[bucket(fluid, x, y)]; j >= 0;
                 j = fluid->work[j].next) {
                const particle_work *other_work = &fluid->work[j];
                if (other_work->x != x || other_work->y != y) {
                    continue;
                }
                fluid_particle other = fluid->particles[j];
                if (other.heat == p.heat || other.thermal.capacity <= 0 ||
                    other.thermal.conductivity <= 0) {
                    continue;
                }
                fluid_vec distance = sub(other.position, p.position);
                if (dot(distance, distance) < radius_squared) {
                    return true;
                }
            }
        }
    }
    return false;
}

static void thermal_interfaces(int start, int end, uint32_t, void *context)
{
    const reaction_query *query = context;
    material_fluid *fluid = query->fluid;
    for (int n = start; n < end; n++) {
        int i = fluid->active[n];
        /* Occlusion can only remove an interface. Conservatively mark both
         * endpoints without tracing any solid boundaries in this cheap pass. */
        fluid->work[i].thermal_interface = thermal_interface(fluid, i);
    }
}

static void thermal_heat(int start, int end, uint32_t, void *context)
{
    const reaction_query *query = context;
    material_fluid *fluid = query->fluid;
    /* Cover rounding in both the rate and transfer reductions. The same
     * margin at both endpoints keeps energy flux symmetric. */
    double margin = 1 + 4 * acount(fluid->active) * DBL_EPSILON;
    for (int n = start; n < end; n++) {
        int i = fluid->active[n];
        fluid_particle p = fluid->particles[i];
        particle_work *w = &fluid->work[i];
        if (!w->thermal_interface) {
            continue;
        }
        double sum = 0, capacity = (double)p.mass * p.thermal.capacity;
        for (int y = w->y - 1; y <= w->y + 1; y++) {
            for (int x = w->x - 1; x <= w->x + 1; x++) {
                for (int j = fluid->heads[bucket(fluid, x, y)]; j >= 0;
                     j = fluid->work[j].next) {
                    particle_work *other_work = &fluid->work[j];
                    if (j == i || other_work->x != x || other_work->y != y) {
                        continue;
                    }
                    fluid_particle other = fluid->particles[j];
                    if (query->exchange && other.heat == p.heat) {
                        continue;
                    }
                    double conductance = thermal_conductance(query, p, other);
                    if (!query->exchange) {
                        sum += conductance / capacity;
                        continue;
                    }
                    /* The symmetric limiter preserves opposite energy fluxes.
                     * Every particle's outgoing weights sum to at most one,
                     * making its new heat a convex combination of the snapshot. */
                    double limit = fmax(
                                       1,
                                       query->dt *
                                           fmax(w->thermal_rate, other_work->thermal_rate)
                                   ) *
                                   margin;
                    sum += query->dt * conductance *
                           ((double)other.heat - p.heat) / (capacity * limit);
                }
            }
        }
        if (query->exchange) {
            w->heat = (float)(p.heat + sum);
        } else {
            w->thermal_rate = sum;
        }
    }
}

static void reaction_heat(int start, int end, uint32_t, void *context)
{
    const reaction_query *query = context;
    material_fluid *fluid = query->fluid;
    const fluid_body *bodies = query->bodies;
    int body_count = query->body_count;
    float dt = query->dt;
    fluid_reaction r = fluid->reaction;
    float h = fluid->kernel;
    for (int n = start; n < end; n++) {
        int i = fluid->active[n];
        fluid_particle p = fluid->particles[i];
        particle_work *w = &fluid->work[i];
        if (p.fuel <= 0 || p.burning) {
            continue;
        }
        for (int k = 0; query->hot_bodies && k < body_count; k++) {
            fluid_body body = bodies[k];
            if (body.heat < r.ignition) {
                continue;
            }
            fluid_vec from = sub(body.position, scale(body.velocity, dt));
            fluid_vec d = sub(body.position, from);
            float t = fmaxf(0, fminf(1, dot(sub(p.position, from), d) / fmaxf(dot(d, d), 1e-8f)));
            fluid_vec near = add(from, scale(d, t)), gap = sub(p.position, near);
            if (dot(gap, gap) <= h * h &&
                !material_obstacle_index_blocked(fluid->obstacles, near, p.position, k, false)) {
                w->heat = fmaxf(w->heat, body.heat);
            }
        }
        if (!query->hot_particles) {
            continue;
        }
        for (int y = w->y - 1; y <= w->y + 1; y++) {
            for (int x = w->x - 1; x <= w->x + 1; x++) {
                for (int j = fluid->heads[bucket(fluid, x, y)]; j >= 0;
                     j = fluid->work[j].next) {
                    fluid_particle other = fluid->particles[j];
                    if (fluid->work[j].x != x || fluid->work[j].y != y ||
                        other.heat <= w->heat ||
                        (p.thermal.capacity > 0 && other.thermal.capacity > 0)) {
                        continue;
                    }
                    fluid_vec d = sub(other.position, p.position);
                    if (dot(d, d) >= h * h || material_obstacle_index_blocked(
                                                  fluid->obstacles,
                                                  other.position,
                                                  p.position,
                                                  -1,
                                                  false
                                              )) {
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
    bool exchange = fluid->thermal_active > 1 && fluid->thermal_low < fluid->thermal_high;
    if (r.ignition <= 0 && !exchange) {
        return;
    }
    reaction_query query = { .fluid = fluid,
                             .dt = dt,
                             .bodies = bodies,
                             .body_count = body_count,
                             .obstacles = obstacles,
                             .obstacle_count = obstacle_count,
                             .hot_particles = fluid->hot_particles };
    if (exchange) {
        run(fluid, thermal_interfaces, &query, 64);
        run(fluid, thermal_heat, &query, 64);
        query.exchange = true;
        run(fluid, thermal_heat, &query, 64);
    }
    if (r.ignition > 0 && fluid->ignition_candidate) {
        for (int i = 0; i < body_count; i++) {
            query.hot_bodies |= bodies[i].heat >= r.ignition;
        }
    }
    if (!exchange && !query.hot_particles && !query.hot_bodies &&
        !fluid->burning && !fluid->smoke) {
        return;
    }
    if (r.ignition > 0 && fluid->ignition_candidate) {
        /* Queries read one immutable snapshot. Thermal pairs already exchange
         * energy conservatively and are excluded from legacy ignition copying. */
        if (query.hot_bodies || query.hot_particles) {
            run(fluid, reaction_heat, &query, 32);
        }
    }
    for (int n = 0; n < acount(fluid->active); n++) {
        int i = fluid->active[n];
        fluid_particle *p = &fluid->particles[i];
        p->heat = fluid->work[i].heat;
        if (r.ignition <= 0) {
            continue;
        }
        if (p->burning && r.extinction > 0 && p->heat < r.extinction) {
            p->burning = false;
            p->color = p->base_color;
            p->emission = p->base_emission;
            p->glow = p->base_glow;
        }
        if (!p->burning && p->fuel > 0 && p->heat >= r.ignition) {
            ignition(fluid, p->position);
            /* Save the current unburned appearance, including prior cooling.
             * Emitter age curves continue independently in the raster. */
            p->base_color = p->color;
            p->base_emission = p->emission;
            p->base_glow = p->glow;
            p->burning = true;
        }
        if (p->burning && p->fuel > 0 && (r.extinction > 0 || p->heat >= r.ignition)) {
            p->glow = 1;
            p->fuel = fmaxf(0, p->fuel - r.burn_rate * dt);
            p->heat = fmaxf(p->heat, r.heat_release);
            p->color = r.flame_color;
            p->emission = r.flame_emission;
            if (p->fuel == 0) {
                p->burning = false;
                p->smoke = true;
                p->kind = FLUID_GAS;
                p->constitutive = (fluid_constitutive){ 0 };
                p->deformation = (fluid_deformation){ .f = { 1, 0, 0, 1 },
                                                      .volume = 1 };
                p->grain_radius = 0;
                p->color = r.smoke_color;
                p->absorption = r.smoke_absorption;
                p->lifetime = r.smoke_lifetime;
                p->style = (fluid_style){ .duration = r.smoke_lifetime };
            }
        }
        if (!p->burning && p->heat > 0) {
            float cooling = deterministic_expf(-r.cooling * dt);
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

static bool segment_contact(
    fluid_vec a,
    fluid_vec b,
    fluid_obstacle obstacle,
    float skin,
    float *fraction,
    fluid_vec *normal
)
{
    fluid_vec d = sub(b, a);
    if (obstacle.radius > 0) {
        float radius = obstacle.radius + skin;
        float aa = dot(d, d), bb = dot(a, d), cc = dot(a, a) - radius * radius;
        if (aa <= 1e-8f || cc < -1e-5f) {
            return false;
        }
        if (cc <= 1e-5f) {
            if (bb >= 0) {
                return false;
            }
            float distance = sqrtf(dot(a, a));
            if (distance <= 1e-6f) {
                return false;
            }
            *normal = scale(a, 1 / distance);
            *fraction = 0;
            return true;
        }
        float projection = -bb / aa;
        fluid_vec closest = add(a, scale(d, projection));
        float remaining = radius * radius - dot(closest, closest);
        if (remaining < 0) {
            return false;
        }
        float t = projection - sqrtf(remaining / aa);
        if (t < 0 || t > 1) {
            return false;
        }
        fluid_vec hit = add(a, scale(d, t));
        *normal = scale(hit, 1 / sqrtf(dot(hit, hit)));
        *fraction = t;
        return true;
    }
    fluid_vec half = { obstacle.half_size.x + skin, obstacle.half_size.y + skin };
    float first = 0, last = 1;
    fluid_vec entering = { 0 };
    float origin[] = { a.x, a.y }, delta[] = { d.x, d.y },
          extent[] = { half.x, half.y };
    bool inside = fabsf(a.x) <= half.x && fabsf(a.y) <= half.y;
    if (inside) {
        float best = 0;
        if (fabsf(fabsf(a.x) - half.x) <= 1e-5f) {
            float sign = a.x < 0 ? -1 : 1;
            if (-delta[0] * sign > best) {
                best = -delta[0] * sign;
                entering = (fluid_vec){ sign, 0 };
            }
        }
        if (fabsf(fabsf(a.y) - half.y) <= 1e-5f) {
            float sign = a.y < 0 ? -1 : 1;
            if (-delta[1] * sign > best) {
                best = -delta[1] * sign;
                entering = (fluid_vec){ 0, sign };
            }
        }
        if (best <= 0) {
            return false;
        }
        *fraction = 0;
        *normal = entering;
        return true;
    }
    for (int axis = 0; axis < 2; axis++) {
        if (fabsf(delta[axis]) < 1e-6f) {
            if (fabsf(origin[axis]) > extent[axis]) {
                return false;
            }
            continue;
        }
        float near = (-extent[axis] - origin[axis]) / delta[axis];
        float far = (extent[axis] - origin[axis]) / delta[axis];
        float sign = -1;
        if (near > far) {
            float swap = near;
            near = far;
            far = swap;
            sign = 1;
        }
        fluid_vec axis_normal = { axis == 0 ? sign : 0, axis == 1 ? sign : 0 };
        if (near > first) {
            first = near;
            entering = axis_normal;
        }
        last = fminf(last, far);
        if (first > last) {
            return false;
        }
    }
    if (first < 0 || first > 1 || dot(entering, entering) < .5f) {
        return false;
    }
    *fraction = first;
    *normal = entering;
    return true;
}

static float lerp_angle(float a, float b, float t)
{
    return a + remainderf(b - a, 6.28318530718f) * t;
}

static fluid_vec body_position_at(fluid_body body, float t)
{
    return add(body.previous_position, scale(sub(body.position, body.previous_position), t));
}

static fluid_body body_at(fluid_body body, float t)
{
    if (!body.swept) {
        return body;
    }
    float angle = lerp_angle(body.previous_angle, body.angle, t);
    cos_sin rotation = cos_sin_compute(angle - body.angle);
    body.center_offset = rotate(body.center_offset, rotation.c, rotation.s);
    body.position = body_position_at(body, t);
    body.angle = angle;
    return body;
}

static float contact_lever(const fluid_body *body, fluid_vec point, fluid_vec normal)
{
    fluid_vec r = sub(point, add(body->position, body->center_offset));
    return r.x * normal.y - r.y * normal.x;
}

static void collide(
    material_fluid *fluid,
    int particle_index,
    fluid_body *body,
    fluid_obstacle obstacle,
    float dt,
    float sweep_start,
    float sweep_end
)
{
    fluid_particle *p = &fluid->particles[particle_index];
    if (fluid->collision_kind >= 0 && p->kind != fluid->collision_kind) {
        return;
    }
    fluid_body at_end = body_at(*body, sweep_end);
    cos_sin rotation = cos_sin_compute(at_end.angle);
    float c = rotation.c, s = rotation.s;
    fluid_vec original = p->position;
    fluid_vec
        local = sub(rotate(sub(original, at_end.position), c, -s), obstacle.center);
    fluid_vec normal;
    float depth, skin = p->radius * .3f;
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
    {
        fluid_vec particle_start = fluid->work[particle_index].previous;
        fluid_vec particle_delta = sub(p->position, particle_start);
        bool particle_moved = dot(particle_delta, particle_delta) > 1e-8f;
        if (body->swept || particle_moved) {
            float a = body->swept
                        ? lerp_angle(body->previous_angle, body->angle, sweep_start)
                        : body->angle;
            float b = body->swept
                        ? lerp_angle(body->previous_angle, body->angle, sweep_end)
                        : body->angle;
            fluid_vec p0 = body->swept ? body_position_at(*body, sweep_start)
                                       : body->position;
            fluid_vec p1 = body->swept ? body_position_at(*body, sweep_end)
                                       : body->position;
            cos_sin rotation_a = cos_sin_compute(a), rotation_b = cos_sin_compute(b);
            fluid_vec from = sub(
                rotate(sub(particle_start, p0), rotation_a.c, -rotation_a.s),
                obstacle.center
            );
            fluid_vec to = sub(
                rotate(sub(p->position, p1), rotation_b.c, -rotation_b.s),
                obstacle.center
            );
            float fraction;
            fluid_vec local_normal;
            if (segment_contact(from, to, obstacle, p->radius, &fraction, &local_normal)) {
                float angle = lerp_angle(a, b, fraction);
                cos_sin contact_rotation = cos_sin_compute(angle);
                normal = rotate(
                    local_normal,
                    contact_rotation.c,
                    contact_rotation.s
                );
                fluid_vec center = add(particle_start, scale(particle_delta, fraction));
                fluid_vec point = sub(center, scale(normal, p->radius));
                float body_t = sweep_start + (sweep_end - sweep_start) * fraction;
                fluid_body at_hit = body_at(*body, body_t);
                fluid_vec surface = body_surface_velocity(at_hit, point);
                surface = add(surface, scale(body->impulse, body->inverse_mass));
                float closing = -dot(sub(p->velocity, surface), normal);
                p->position = add(center, scale(normal, skin + 1e-3f));
                fluid->work[particle_index].contact_corrected = true;
                if (closing > 0) {
                    float lever = contact_lever(&at_hit, point, normal);
                    float impulse = closing / (1 / p->mass + body->inverse_mass +
                                               lever * lever * body->inverse_inertia);
                    p->velocity = add(p->velocity, scale(normal, impulse / p->mass));
                    body->impulse = sub(body->impulse, scale(normal, impulse));
                    body->angular_impulse -= lever * impulse;
                    fluid_vec local_center = add(from, scale(sub(to, from), fraction));
                    fluid_vec local_point = add(
                        sub(local_center, scale(local_normal, p->radius)),
                        obstacle.center
                    );
                    record_contact(fluid, p, body, obstacle.body, point, normal, local_point, closing, impulse);
                }
                return;
            }
        }
    }
    if (depth <= 0) {
        return;
    }
    normal = rotate(normal, c, s);
    fluid_vec point = sub(original, scale(normal, p->radius));
    fluid_vec body_velocity = body_surface_velocity(at_end, point);
    body_velocity = add(body_velocity, scale(body->impulse, body->inverse_mass));
    float closing = -dot(sub(p->velocity, body_velocity), normal);
    p->position = add(p->position, scale(normal, depth));
    fluid->work[particle_index].contact_corrected = true;
    float lever = contact_lever(&at_end, point, normal);
    float impulse = fmaxf(0, closing + fminf(depth / dt, 80) * .2f) /
                    (1 / p->mass + body->inverse_mass +
                     lever * lever * body->inverse_inertia);
    p->velocity = add(p->velocity, scale(normal, impulse / p->mass));
    body->impulse = sub(body->impulse, scale(normal, impulse));
    body->angular_impulse -= lever * impulse;
    fluid_vec local_point = rotate(sub(point, at_end.position), c, -s);
    record_contact(fluid, p, body, obstacle.body, point, normal, local_point, fmaxf(0, closing), impulse);
}

static bool closure_candidates(
    material_fluid *fluid,
    const fluid_body *bodies,
    int body_count,
    const fluid_obstacle *obstacles,
    int count,
    float sweep_end
)
{
    bool dirty = false;
    for (int n = 0; n < acount(fluid->active); n++) {
        dirty |= fluid->work[fluid->active[n]].contact_corrected;
    }
    if (!dirty) {
        return false;
    }
    asetlen(fluid->closure_bodies, body_count);
    for (int i = 0; i < body_count; i++) {
        fluid->closure_bodies[i] = body_at(bodies[i], sweep_end);
    }
    aclear(fluid->closure);
    aclear(fluid->closure_shapes);
    for (int i = 0; i < count; i++) {
        fluid_obstacle obstacle = obstacles[i];
        fluid_body body = fluid->closure_bodies[obstacle.body];
        if (body.inverse_mass > 0 || body.inverse_inertia > 0) {
            continue;
        }
        cos_sin rotation = cos_sin_compute(body.angle);
        apush(fluid->closure_shapes, obstacle);
        apush(fluid->closure, ((closure_collider){ obstacle, body, rotation }));
    }
    material_obstacle_index_build(
        fluid->closure_index,
        fluid->closure_bodies,
        body_count,
        fluid->closure_shapes,
        acount(fluid->closure_shapes),
        fmaxf(fluid->kernel * 4, 16)
    );
    return acount(fluid->closure) > 0;
}

/* Later collider corrections can move a parcel into an earlier static wall.
 * Requery corrected positions each pass, without replaying body trajectories. */
static void close_static_contacts(
    material_fluid *fluid,
    fluid_body *bodies,
    int body_count,
    const fluid_obstacle *obstacles,
    int count,
    float sweep_end
)
{
    enum
    {
        CLOSURE_PASSES = 8
    };
    if (!closure_candidates(fluid, bodies, body_count, obstacles, count, sweep_end)) {
        return;
    }
    for (int pass = 0; pass < CLOSURE_PASSES; pass++) {
        bool corrected = false;
        for (int n = 0; n < acount(fluid->active); n++) {
            int i = fluid->active[n];
            if (!fluid->work[i].contact_corrected) {
                continue;
            }
            fluid->work[i].contact_corrected = false;
            fluid_particle *p = &fluid->particles[i];
            /* A rotated box expands on both local axes: two skins cover
             * its world AABB extension and the rotation approximation. */
            material_obstacle_index_query(
                fluid->closure_index,
                p->position,
                p->radius * .6f,
                &fluid->closure_query
            );
            for (int j = 0; j < acount(fluid->closure_query); j++) {
                closure_collider candidate = fluid->closure[fluid->closure_query[j]];
                fluid_obstacle obstacle = candidate.obstacle;
                fluid_body *body = &bodies[obstacle.body];
                fluid_body at_end = candidate.pose;
                float c = candidate.rotation.c, s = candidate.rotation.s;
                fluid_vec local = sub(
                    rotate(sub(p->position, at_end.position), c, -s),
                    obstacle.center
                );
                float skin = p->radius * .3f, depth;
                fluid_vec normal;
                if (obstacle.radius > 0) {
                    float distance = sqrtf(dot(local, local));
                    depth = obstacle.radius + skin - distance;
                    normal = distance > 1e-6f ? scale(local, 1 / distance)
                                              : (fluid_vec){ 1, 0 };
                } else {
                    float dx = obstacle.half_size.x + skin - fabsf(local.x);
                    float dy = obstacle.half_size.y + skin - fabsf(local.y);
                    depth = fminf(dx, dy);
                    normal = dx < dy ? (fluid_vec){ local.x < 0 ? -1 : 1, 0 }
                                     : (fluid_vec){ 0, local.y < 0 ? -1 : 1 };
                }
                if (depth <= 0) {
                    continue;
                }
                normal = rotate(normal, c, s);
                fluid_vec point = sub(p->position, scale(normal, p->radius));
                float closing = -dot(sub(p->velocity, body_surface_velocity(at_end, point)), normal);
                /* Leave numerical clearance after float transforms/projection. */
                p->position = add(p->position, scale(normal, depth + 1e-4f));
                if (closing > 0) {
                    float impulse = closing * p->mass;
                    p->velocity = add(p->velocity, scale(normal, closing));
                    body->impulse = sub(body->impulse, scale(normal, impulse));
                    body->angular_impulse -= contact_lever(&at_end, point, normal) *
                                             impulse;
                    fluid_vec local_point = rotate(sub(point, at_end.position), c, -s);
                    record_contact(fluid, p, body, obstacle.body, point, normal, local_point, closing, impulse);
                }
                fluid->work[i].contact_corrected = true;
                corrected = true;
            }
        }
        if (!corrected) {
            break;
        }
    }
}

static void collide_obstacles(
    material_fluid *fluid,
    fluid_body *bodies,
    int body_count,
    const fluid_obstacle *obstacles,
    int count,
    float sweep_start,
    float sweep_end,
    material_fluid_profile *profile
)
{
    /* Query the particle hash with each collider's AABB, instead of testing
     * every particle against every cave wall. Corrections use a fresh index. */
    if (!acount(fluid->active)) {
        return;
    }
    double index_start = profile ? profile_clock_ms() : 0;
    index_particles(fluid, profile);
    if (profile) {
        profile->collision_index_ms += profile_clock_ms() - index_start;
    }
    fluid_region bounds = fluid->bounds;
    float particle_motion_x = 0, particle_motion_y = 0;
    for (int n = 0; n < acount(fluid->active); n++) {
        int particle = fluid->active[n];
        fluid->work[particle].contact_corrected = false;
        fluid_vec previous = fluid->work[particle].previous;
        fluid_vec current = fluid->particles[particle].position;
        float high_x = bounds.x + bounds.width, high_y = bounds.y + bounds.height;
        bounds.x = fminf(bounds.x, previous.x);
        bounds.y = fminf(bounds.y, previous.y);
        bounds.width = fmaxf(high_x, previous.x) - bounds.x;
        bounds.height = fmaxf(high_y, previous.y) - bounds.y;
        particle_motion_x = fmaxf(particle_motion_x, fabsf(current.x - previous.x));
        particle_motion_y = fmaxf(particle_motion_y, fabsf(current.y - previous.y));
    }
    for (int i = 0; i < count; i++) {
        fluid_obstacle obstacle = obstacles[i];
        fluid_body *body = &bodies[obstacle.body];
        cos_sin rotation = cos_sin_compute(body->angle);
        float c = rotation.c, s = rotation.s;
        fluid_vec center = add(body->position, rotate(obstacle.center, c, s));
        float ex = obstacle.radius > 0 ? obstacle.radius
                                       : fabsf(c) * obstacle.half_size.x +
                                             fabsf(s) * obstacle.half_size.y;
        float ey = obstacle.radius > 0 ? obstacle.radius
                                       : fabsf(s) * obstacle.half_size.x +
                                             fabsf(c) * obstacle.half_size.y;
        if (body->swept) {
            float reach = fabsf(obstacle.center.x) + fabsf(obstacle.center.y) +
                          (obstacle.radius > 0
                               ? obstacle.radius
                               : obstacle.half_size.x + obstacle.half_size.y);
            fluid_vec previous = body_position_at(*body, sweep_start),
                      current = body_position_at(*body, sweep_end);
            center.x = (previous.x + current.x) * .5f;
            center.y = (previous.y + current.y) * .5f;
            ex = reach + fabsf(previous.x - current.x) * .5f;
            ey = reach + fabsf(previous.y - current.y) * .5f;
        }
        float skin = fluid->kernel;
        float query_ex = ex + particle_motion_x, query_ey = ey + particle_motion_y;
        if (center.x + query_ex + skin < bounds.x ||
            center.y + query_ey + skin < bounds.y ||
            center.x - query_ex - skin > bounds.x + bounds.width ||
            center.y - query_ey - skin > bounds.y + bounds.height) {
            continue;
        }
        int x0 = (int)floorf((center.x - query_ex - fluid->kernel) / fluid->kernel);
        int y0 = (int)floorf((center.y - query_ey - fluid->kernel) / fluid->kernel);
        int x1 = (int)floorf((center.x + query_ex + fluid->kernel) / fluid->kernel);
        int y1 = (int)floorf((center.y + query_ey + fluid->kernel) / fluid->kernel);
        x0 = x0 > (int)floorf(bounds.x / fluid->kernel)
               ? x0
               : (int)floorf(bounds.x / fluid->kernel);
        y0 = y0 > (int)floorf(bounds.y / fluid->kernel)
               ? y0
               : (int)floorf(bounds.y / fluid->kernel);
        x1 = x1 < (int)floorf((bounds.x + bounds.width) / fluid->kernel)
               ? x1
               : (int)floorf((bounds.x + bounds.width) / fluid->kernel);
        y1 = y1 < (int)floorf((bounds.y + bounds.height) / fluid->kernel)
               ? y1
               : (int)floorf((bounds.y + bounds.height) / fluid->kernel);
        if (x0 > x1 || y0 > y1) {
            continue;
        }
        /* Contacts push the body this one is a part of, and see what earlier
         * contacts with its other parts did to it. */
        fluid_body *response = &bodies[material_fluid_response(bodies, obstacle.body)];
        fluid_body part = *body;
        part.impulse = response->impulse;
        part.angular_impulse = response->angular_impulse;
        double query_cells = ((double)x1 - x0 + 1) * ((double)y1 - y0 + 1);
        if (query_cells > acount(fluid->active)) {
            for (int n = 0; n < acount(fluid->active); n++) {
                int particle = fluid->active[n];
                collide(fluid, particle, &part, obstacle, fluid->delta, sweep_start, sweep_end);
            }
        } else {
            for (int y = y0; y <= y1; y++) {
                for (int x = x0; x <= x1; x++) {
                    for (int j = fluid->heads[bucket(fluid, x, y)]; j >= 0;
                         j = fluid->work[j].next) {
                        if (fluid->work[j].x == x && fluid->work[j].y == y) {
                            collide(fluid, j, &part, obstacle, fluid->delta, sweep_start, sweep_end);
                        }
                    }
                }
            }
        }
        response->impulse = part.impulse;
        response->angular_impulse = part.angular_impulse;
    }
    close_static_contacts(fluid, bodies, body_count, obstacles, count, sweep_end);
}

static void mpm_tick(
    material_fluid *fluid,
    float delta,
    fluid_body *bodies,
    int body_count,
    const fluid_obstacle *obstacles,
    int obstacle_count,
    material_fluid_profile *profile
)
{
    fluid_mpm_state *state = &fluid->mpm_state;
    *state = (fluid_mpm_state){ 0 };
    bool liquid = false;
    for (int n = 0; n < acount(fluid->active); n++) {
        if (fluid->particles[fluid->active[n]].kind != FLUID_GAS) {
            liquid = true;
            break;
        }
    }
    if (!liquid) {
        state->advanced_time = delta;
        return;
    }
    fluid->collision_kind = FLUID_LIQUID;
    while (state->advanced_time < delta &&
           state->substeps < fluid->mpm_settings.max_substeps) {
        float left = delta - state->advanced_time;
        float dt = fminf(
            left,
            material_mpm_timestep(
                fluid->mpm_settings,
                fluid->particles,
                fluid->active,
                acount(fluid->active)
            )
        );
        if (!(dt > 0) || state->advanced_time + dt <= state->advanced_time) {
            break;
        }
        for (int n = 0; n < acount(fluid->active); n++) {
            int i = fluid->active[n];
            fluid->work[i].previous = fluid->particles[i].position;
        }
        double start = profile ? profile_clock_ms() : 0;
        bool ok = false;
        for (int retry = 0; retry < 8 && state->advanced_time + dt > state->advanced_time;
             retry++) {
            if (material_mpm_step_colliders(
                    fluid->mpm,
                    fluid->mpm_settings,
                    dt,
                    fluid->particles,
                    fluid->active,
                    acount(fluid->active),
                    &fluid->tasks,
                    (fluid_mpm_colliders){ bodies,
                                           body_count,
                                           obstacles,
                                           obstacle_count,
                                           (state->advanced_time + dt) / delta }
                )) {
                ok = true;
                break;
            }
            dt *= .5f;
        }
        if (profile) {
            profile->mpm_ms += profile_clock_ms() - start;
        }
        if (!ok) {
            break;
        }
        float before = state->advanced_time;
        state->advanced_time += dt;
        state->substeps++;
        state->nodes = material_mpm_node_count(fluid->mpm);
        fluid->delta = dt;
        start = profile ? profile_clock_ms() : 0;
        collide_obstacles(
            fluid,
            bodies,
            body_count,
            obstacles,
            obstacle_count,
            before / delta,
            state->advanced_time / delta,
            profile
        );
        if (profile) {
            profile->collisions_ms += profile_clock_ms() - start;
        }
    }
    state->limited = state->advanced_time < delta * (1 - 1e-6f);
    if (profile) {
        profile->mpm_substeps += state->substeps;
        profile->mpm_limited_steps += state->limited;
    }
    fluid->collision_kind = -1;
}

void material_fluid_tick_profiled(
    material_fluid *fluid,
    float delta,
    float chunk_size,
    const fluid_region *regions,
    int region_count,
    fluid_body *bodies,
    int body_count,
    const fluid_obstacle *obstacles,
    int obstacle_count,
    material_fluid_profile *profile
)
{
    if (profile) {
        *profile = (material_fluid_profile){ 0 };
    }
    assert(delta > 0 && delta <= 1.f / 60 + 1e-6f && isfinite(chunk_size) && chunk_size > 0);
    clear_contacts(fluid);
    for (int i = 0; i < body_count; i++) {
        bodies[i].impulse = (fluid_vec){ 0 };
        bodies[i].angular_impulse = 0;
    }
    for (int i = 0; i < obstacle_count; i++) {
        assert(obstacles[i].body >= 0 && obstacles[i].body < body_count);
    }
    aclear(fluid->active);
    aclear(fluid->blasts);
    fluid->thermal_active = 0;
    fluid->thermal_low = INFINITY;
    fluid->thermal_high = -INFINITY;
    fluid->hot_particles = fluid->ignition_candidate = false;
    fluid->burning = fluid->smoke = false;
    bool reactions = fluid->reaction.ignition > 0 || fluid->thermal_population > 0;
    if (fluid->chunk_size != chunk_size) {
        material_particle_index_free(fluid->chunks);
        fluid->chunks = material_particle_index_new(chunk_size);
        fluid->chunk_size = chunk_size;
        for (int i = 0; i < acount(fluid->particles); i++) {
            material_particle_index_add(fluid->chunks, i, fluid->particles[i].position);
        }
    }
    material_particle_index_query(fluid->chunks, regions, region_count, &fluid->selected);
    for (int n = 0; n < acount(fluid->selected); n++) {
        int i = fluid->selected[n];
        while (i < acount(fluid->particles)) {
            fluid_particle *p = &fluid->particles[i];
            if (!awake(p->position, chunk_size, regions, region_count)) {
                break;
            }
            if (p->lifetime > 0) {
                p->lifetime -= delta;
                if (p->lifetime <= 0) {
                    fluid->thermal_population -= p->thermal.conductivity > 0;
                    int last = acount(fluid->particles) - 1;
                    material_particle_index_swap_remove(fluid->chunks, i, last);
                    *p = alast(fluid->particles);
                    fluid->work[i] = alast(fluid->work);
                    apop(fluid->particles);
                    apop(fluid->work);
                    continue;
                }
            }
            if (p->style.duration > 0) {
                float age = 1 - p->lifetime / p->style.duration;
                float drag = particle_curve_value(
                    p->style.drag,
                    p->style.drag_end,
                    p->style.drag_exponent,
                    age
                );
                float damping = deterministic_expf(-drag * delta);
                p->velocity.x *= damping;
                p->velocity.y *= damping;
            }
            if (reactions) {
                fluid->work[i].heat = p->heat;
                fluid->hot_particles |= p->heat > 0;
                fluid->ignition_candidate |= p->fuel > 0 && !p->burning;
                fluid->burning |= p->burning;
                fluid->smoke |= p->smoke;
                if (p->thermal.conductivity > 0) {
                    fluid->thermal_active++;
                    fluid->thermal_low = fminf(fluid->thermal_low, p->heat);
                    fluid->thermal_high = fmaxf(fluid->thermal_high, p->heat);
                }
            }
            apush(fluid->active, i);
            break;
        }
    }
    material_obstacle_index_build(
        fluid->obstacles,
        bodies,
        body_count,
        obstacles,
        obstacle_count,
        fmaxf(fluid->kernel * 4, 16)
    );
    double phase_start = profile ? profile_clock_ms() : 0;
    index_particles(fluid, profile);
    if (profile) {
        profile->index_ms += profile_clock_ms() - phase_start;
    }
    phase_start = profile ? profile_clock_ms() : 0;
    CHIBI_TRACE_ZONE_BEGIN(reaction_zone, "Fluid reactions");
    react(fluid, delta, bodies, body_count, obstacles, obstacle_count);
    CHIBI_TRACE_ZONE_END(reaction_zone);
    if (profile) {
        profile->reactions_ms = profile_clock_ms() - phase_start;
    }
    if (fluid->flow_settings.cell_size > 0) {
        phase_start = profile ? profile_clock_ms() : 0;
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
            obstacle_count,
            &fluid->tasks
        );
        CHIBI_TRACE_ZONE_END(flow_zone);
        if (profile) {
            profile->gas_flow_ms = profile_clock_ms() - phase_start;
        }
    }
    bool gas = false;
    for (int n = 0; n < acount(fluid->active); n++) {
        gas |= fluid->particles[fluid->active[n]].kind == FLUID_GAS;
    }
    if (fluid->solver == FLUID_SOLVER_MPM) {
        mpm_tick(fluid, delta, bodies, body_count, obstacles, obstacle_count, profile);
    }
    fluid->delta = delta * .5f;
    for (int step = 0; step < 2 && (fluid->solver == FLUID_SOLVER_DDR || gas); step++) {
        for (int n = 0; n < acount(fluid->active); n++) {
            int i = fluid->active[n];
            fluid_particle *p = &fluid->particles[i];
            fluid->work[i].previous = p->position;
            if (fluid->solver == FLUID_SOLVER_DDR || p->kind == FLUID_GAS) {
                if (p->kind != FLUID_GAS) {
                    p->velocity = add(
                        p->velocity,
                        scale(fluid->mpm_settings.gravity, fluid->delta)
                    );
                }
                p->position = add(p->position, scale(p->velocity, fluid->delta));
            }
        }
        CHIBI_TRACE_ZONE_BEGIN(index_zone, "Fluid index");
        phase_start = profile ? profile_clock_ms() : 0;
        index_particles(fluid, profile);
        CHIBI_TRACE_ZONE_END(index_zone);
        if (profile) {
            profile->index_ms += profile_clock_ms() - phase_start;
        }
        phase_start = profile ? profile_clock_ms() : 0;
        CHIBI_TRACE_ZONE_BEGIN(density_zone, "Fluid density");
        run(fluid, densities, fluid, 256);
        CHIBI_TRACE_ZONE_END(density_zone);
        if (profile) {
            profile->density_ms += profile_clock_ms() - phase_start;
        }
        phase_start = profile ? profile_clock_ms() : 0;
        CHIBI_TRACE_ZONE_BEGIN(relax_zone, "Fluid relaxation");
        run(fluid, relax, fluid, 256);
        CHIBI_TRACE_ZONE_END(relax_zone);
        if (profile) {
            profile->relaxation_ms += profile_clock_ms() - phase_start;
        }
        for (int n = 0; n < acount(fluid->active); n++) {
            int i = fluid->active[n];
            fluid_particle *p = &fluid->particles[i];
            if (fluid->solver == FLUID_SOLVER_MPM && p->kind != FLUID_GAS) {
                /* The mechanical step already placed and collided this material.
                 * Gas transfers momentum for its next step, never a second pose. */
                p->velocity = add(
                    p->velocity,
                    scale(fluid->work[i].correction, 1 / fluid->delta)
                );
                continue;
            }
            p->position = add(p->position, fluid->work[i].correction);
            fluid_vec corrected = scale(
                sub(p->position, fluid->work[i].previous),
                1 / fluid->delta
            );
            p->velocity = corrected;
        }
        phase_start = profile ? profile_clock_ms() : 0;
        CHIBI_TRACE_ZONE_BEGIN(collision_zone, "Fluid collisions");
        fluid->collision_kind = fluid->solver == FLUID_SOLVER_MPM ? FLUID_GAS : -1;
        collide_obstacles(
            fluid,
            bodies,
            body_count,
            obstacles,
            obstacle_count,
            step * .5f,
            (step + 1) * .5f,
            profile
        );
        fluid->collision_kind = -1;
        CHIBI_TRACE_ZONE_END(collision_zone);
        if (profile) {
            profile->collisions_ms += profile_clock_ms() - phase_start;
        }
    }
    for (int n = 0; n < acount(fluid->active); n++) {
        int i = fluid->active[n];
        material_particle_index_move(fluid->chunks, i, fluid->particles[i].position);
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
    material_fluid_tick_profiled(
        fluid,
        delta,
        chunk_size,
        regions,
        region_count,
        bodies,
        body_count,
        obstacles,
        obstacle_count,
        NULL
    );
}
