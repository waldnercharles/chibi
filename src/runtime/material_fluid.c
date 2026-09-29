#include "runtime/material_fluid.h"

#include "diagnostics/tracy.h"
#include "render/particles.h"
#include "runtime/material_fluid_internal.h"
#include "runtime/material_solid.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static fluid_vec body_surface_velocity(fluid_body body, fluid_vec point)
{
    fluid_vec radius = fluid_sub(point, body.position);
    return fluid_add(
        body.velocity,
        (fluid_vec){ -body.angular_velocity * radius.y,
                     body.angular_velocity * radius.x }
    );
}

static unsigned contact_bucket(const material_fluid *fluid, int body, int x, int y, int phase)
{
    unsigned h = (unsigned)body * 0x9e3779b9u ^ (unsigned)x * 0x85ebca6bu ^
                 (unsigned)y * 0xc2b2ae35u ^ (unsigned)phase * 0x27d4eb2fu;
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
    float threshold = particle->phase == FLUID_GAS
                        ? fluid->contact_settings.gas_speed
                        : fluid->contact_settings.liquid_speed;
    if (threshold <= 0 || closing_speed < threshold) {
        return;
    }
    float patch_size = fluid->kernel * 2;
    int patch_x = (int)floorf(local_point.x / patch_size);
    int patch_y = (int)floorf(local_point.y / patch_size);
    unsigned
        bucket = contact_bucket(fluid, body_index, patch_x, patch_y, particle->phase);
    int at = fluid->contact_heads[bucket];
    while (at >= 0) {
        contact_work *work = &fluid->contact_work[at];
        if (work->body_index == body_index && work->patch_x == patch_x &&
            work->patch_y == patch_y && fluid->contacts[at].phase == particle->phase) {
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
            ((fluid_contact){ .body = body->owner, .phase = particle->phase })
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
    float length = sqrtf(fluid_dot(averaged, averaged));
    contact->normal = length > 1e-6f ? fluid_scale(averaged, 1 / length) : normal;
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
    fluid->buckets = 1;
    while (fluid->buckets < capacity * 2) {
        fluid->buckets *= 2;
    }
    fluid->heads = xmalloc((size_t)fluid->buckets * sizeof *fluid->heads);
    fluid->warm = xcalloc((size_t)fluid->buckets, 1);
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
    return fluid;
}

void material_fluid_free(material_fluid *fluid)
{
    afree(fluid->particles);
    afree(fluid->work);
    afree(fluid->active);
    afree(fluid->used);
    afree(fluid->contact_used);
    afree(fluid->contacts);
    afree(fluid->contact_work);
    afree(fluid->blasts);
    afree(fluid->deposits);
    afree(fluid->substances);
    afree(fluid->reactions);
    afree(fluid->by_substance);
    afree(fluid->substance_start);
    afree(fluid->burning);
    afree(fluid->doomed);
    free(fluid->heads);
    free(fluid->warm);
    afree(fluid->warm_used);
    free(fluid->contact_heads);
    material_flow_free(fluid->flow);
    free(fluid);
}

void material_fluid_clear(material_fluid *fluid)
{
    aclear(fluid->particles);
    aclear(fluid->work);
    aclear(fluid->active);
    aclear(fluid->contact_used);
    aclear(fluid->contacts);
    aclear(fluid->blasts);
    aclear(fluid->deposits);
    memset(fluid->contact_heads, -1, (size_t)fluid->buckets * sizeof *fluid->contact_heads);
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

void material_fluid_set_substances(material_fluid *fluid, const fluid_substance *substances, int count)
{
    assert(count >= acount(fluid->substances));
    aclear(fluid->substances);
    for (int i = 0; i < count; i++) {
        fluid_substance s = substances[i];
        assert(s.phase >= FLUID_GAS && s.phase < FLUID_PHASE_COUNT);
        assert(isfinite(s.density) && s.density > 0);
        assert(isfinite(s.temperature + s.cooling + s.lifetime + s.ignite_at + s.burn_rate));
        assert(s.cooling >= 0 && s.lifetime >= 0 && s.ignite_at >= 0);
        assert(s.ignite_at == 0 || s.burn_rate > 0);
        assert(s.burns_to < count && s.burns_to != FLUID_KEEP);
        assert(s.boils_to < count && s.freezes_to < count);
        apush(fluid->substances, s);
    }
}

void material_fluid_set_reactions(material_fluid *fluid, const fluid_reaction *reactions, int count)
{
    int substances = acount(fluid->substances);
    aclear(fluid->reactions);
    for (int i = 0; i < count; i++) {
        fluid_reaction r = reactions[i];
        assert(r.a >= 0 && r.a < substances && r.b >= FLUID_BURNING && r.b < substances);
        assert(isfinite(r.rate + r.heat) && r.rate > 0);
        assert(r.a_becomes < substances && r.b_becomes < substances);
        apush(fluid->reactions, r);
    }
}

void material_fluid_set_heat(material_fluid *fluid, fluid_heat heat)
{
    assert(isfinite(heat.conduction + heat.glow_temperature + heat.glow_emission));
    assert(heat.conduction >= 0 && heat.glow_temperature >= 0 && heat.glow_emission >= 0);
    fluid->heat = heat;
}

fluid_heat material_fluid_heat(const material_fluid *fluid)
{
    return fluid->heat;
}

const fluid_substance *material_fluid_substances(const material_fluid *fluid, int *count)
{
    *count = acount(fluid->substances);
    return fluid->substances;
}

float material_fluid_spacing(const material_fluid *fluid, int phase)
{
    return fluid->kernel * (phase == FLUID_GAS ? .5f : .4f);
}

/* Box2D bodies weigh one unit of mass per square meter of 32 units. */
static const float RIGID_DENSITY = 1.f / 1024;

fluid_particle material_fluid_particle(const material_fluid *fluid, int substance, fluid_vec position)
{
    assert(substance >= 0 && substance < acount(fluid->substances));
    fluid_substance s = fluid->substances[substance];
    float spacing = material_fluid_spacing(fluid, s.phase);
    bool billow = s.phase == FLUID_GAS && s.lifetime > 0;
    return (fluid_particle){
        .position = position,
        .mass = s.density * spacing * spacing * RIGID_DENSITY,
        .radius = s.phase == FLUID_GAS ? fluid->kernel
                                       : fminf(spacing * 1.5f, fluid->kernel),
        .substance = substance,
        .phase = s.phase,
        .color = s.color,
        .absorption = s.absorption,
        .emission = s.emission,
        .visibility = s.visibility,
        .refraction = s.refraction,
        .lifetime = s.lifetime,
        .temperature = s.temperature,
        .fuel = s.ignite_at > 0,
        .billow = billow,
        .style = { .duration = billow ? s.lifetime : 0 },
    };
}

const fluid_blast *material_fluid_blasts(const material_fluid *fluid, int *count)
{
    *count = acount(fluid->blasts);
    return fluid->blasts;
}

const fluid_deposit *material_fluid_deposits(const material_fluid *fluid, int *count)
{
    *count = acount(fluid->deposits);
    return fluid->deposits;
}

void material_fluid_heat_area(material_fluid *fluid, fluid_vec point, float radius, float delta)
{
    assert(isfinite(point.x + point.y + radius + delta) && radius >= 0);
    for (int n = 0; n < acount(fluid->active); n++) {
        fluid_particle *p = &fluid->particles[fluid->active[n]];
        fluid_vec d = fluid_sub(p->position, point);
        if (fluid_dot(d, d) <= radius * radius) {
            p->temperature += delta;
        }
    }
}

void material_fluid_doom(material_fluid *fluid, int i)
{
    if (!fluid->work[i].doomed) {
        fluid->work[i].doomed = true;
        apush(fluid->doomed, i);
    }
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
    assert(isfinite(p.fuel) && p.fuel >= 0 && p.fuel <= 1 && isfinite(p.temperature));
    assert(p.substance >= 0 && p.substance < acount(fluid->substances));
    assert(p.phase == fluid->substances[p.substance].phase);
    assert(isfinite(p.lifetime) && p.lifetime >= 0);
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
        unsigned h = fluid_bucket(fluid, w->x, w->y);
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
        if (fluid->flow_settings.cell_size > 0 && p.phase == FLUID_GAS) {
            w->pressure = w->near_pressure = 0;
            continue;
        }
        float density = 0, near_density = 0;
        for (int y = w->y - 1; y <= w->y + 1; y++) {
            for (int x = w->x - 1; x <= w->x + 1; x++) {
                for (int j = fluid->heads[fluid_bucket(fluid, x, y)]; j >= 0;
                     j = fluid->work[j].next) {
                    if (j == i || fluid->work[j].x != x || fluid->work[j].y != y ||
                        fluid->particles[j].substance != p.substance) {
                        continue;
                    }
                    fluid_vec d = fluid_sub(fluid->particles[j].position, p.position);
                    float distance = fluid_dot(d, d);
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
        w->pressure = p.phase != FLUID_GAS ? 600 * (density - 6)
                                           : 250 * fmaxf(0, density - 5);
        w->near_pressure = p.phase != FLUID_GAS ? 1200 * near_density : 0;
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
                for (int j = fluid->heads[fluid_bucket(fluid, x, y)]; j >= 0;
                     j = fluid->work[j].next) {
                    particle_work *neighbor = &fluid->work[j];
                    if (j == i || neighbor->x != x || neighbor->y != y) {
                        continue;
                    }
                    fluid_particle other = fluid->particles[j];
                    if (fluid->flow_settings.cell_size > 0 &&
                        p.phase == FLUID_GAS && other.phase == FLUID_GAS) {
                        continue;
                    }
                    fluid_vec d = fluid_sub(other.position, p.position);
                    float distance = fluid_dot(d, d);
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
                    if (p.substance == other.substance) {
                        pressure = .5f * ((w->pressure + neighbor->pressure) * q +
                                          (w->near_pressure + neighbor->near_pressure) *
                                              q * q);
                    }
                    /* Equal and opposite pair corrections, weighted by mass.
                     * Clamping individual pairs preserves their momentum. */
                    float displacement = fmaxf(-h * .025f, fminf(h * .025f, dt * dt * pressure));
                    float share = other.mass / (p.mass + other.mass);
                    correction = fluid_sub(correction, fluid_scale(d, share * displacement / distance));
                    fluid_vec relative = fluid_sub(other.velocity, p.velocity);
                    correction = fluid_add(
                        correction,
                        fluid_scale(relative, share * q * q * dt * .04f)
                    );
                }
            }
        }
        w->correction = correction;
    }
}

void material_fluid_run(material_fluid *fluid, game_task_range *fn, int count, void *context, int grain)
{
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
    fluid_vec d = fluid_sub(b, a);
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

static bool obstacle_blocked(fluid_vec a, fluid_vec b, const fluid_colliders *colliders, int ignore_body)
{
    fluid_vec low = { fminf(a.x, b.x), fminf(a.y, b.y) },
              high = { fmaxf(a.x, b.x), fmaxf(a.y, b.y) };
    for (int i = 0; i < colliders->obstacle_count; i++) {
        fluid_obstacle o = colliders->obstacles[i];
        const fluid_body *body = &colliders->bodies[o.body];
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
        fluid_vec from = fluid_sub(rotate(fluid_sub(a, body->position), c, -s), o.center);
        fluid_vec to = fluid_sub(rotate(fluid_sub(b, body->position), c, -s), o.center);
        if (o.radius <= 0) {
            if (segment_box(from, to, o.half_size)) {
                return true;
            }
            continue;
        }
        fluid_vec d = fluid_sub(to, from);
        float t = fmaxf(.001f, fminf(.999f, -fluid_dot(from, d) / fmaxf(fluid_dot(d, d), 1e-8f)));
        fluid_vec nearest = fluid_add(from, fluid_scale(d, t));
        if (fluid_dot(nearest, nearest) < o.radius * o.radius) {
            return true;
        }
    }
    return false;
}

bool material_fluid_blocked(
    fluid_vec a,
    fluid_vec b,
    const fluid_colliders *colliders,
    const material_solids *solids,
    int ignore_body
)
{
    return obstacle_blocked(a, b, colliders, ignore_body) ||
           (solids && material_solids_blocked(solids, a, b));
}

void material_fluid_push(material_fluid *fluid, fluid_vec point, float radius, float speed, const fluid_step *step)
{
    for (int n = 0; n < acount(fluid->active); n++) {
        fluid_particle *p = &fluid->particles[fluid->active[n]];
        fluid_vec d = fluid_sub(p->position, point);
        float distance = sqrtf(fluid_dot(d, d));
        if (distance <= 1e-5f || distance >= radius ||
            material_fluid_blocked(point, p->position, &step->colliders, step->solids, -1)) {
            continue;
        }
        p->velocity = fluid_add(p->velocity, fluid_scale(d, speed * (1 - distance / radius) / distance));
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
    fluid_vec d = fluid_sub(b, a);
    if (obstacle.radius > 0) {
        float radius = obstacle.radius + skin;
        float aa = fluid_dot(d, d), bb = fluid_dot(a, d),
              cc = fluid_dot(a, a) - radius * radius;
        if (aa <= 1e-8f || cc < -1e-5f) {
            return false;
        }
        if (cc <= 1e-5f) {
            if (bb >= 0) {
                return false;
            }
            float distance = sqrtf(fluid_dot(a, a));
            if (distance <= 1e-6f) {
                return false;
            }
            *normal = fluid_scale(a, 1 / distance);
            *fraction = 0;
            return true;
        }
        float projection = -bb / aa;
        fluid_vec closest = fluid_add(a, fluid_scale(d, projection));
        float remaining = radius * radius - fluid_dot(closest, closest);
        if (remaining < 0) {
            return false;
        }
        float t = projection - sqrtf(remaining / aa);
        if (t < 0 || t > 1) {
            return false;
        }
        fluid_vec hit = fluid_add(a, fluid_scale(d, t));
        *normal = fluid_scale(hit, 1 / sqrtf(fluid_dot(hit, hit)));
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
    if (first < 0 || first > 1 || fluid_dot(entering, entering) < .5f) {
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
    return fluid_add(
        body.previous_position,
        fluid_scale(fluid_sub(body.position, body.previous_position), t)
    );
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
    float c = cosf(body->angle), s = sinf(body->angle);
    fluid_vec original = p->position;
    fluid_vec local = fluid_sub(
        rotate(fluid_sub(original, body->position), c, -s),
        obstacle.center
    );
    fluid_vec normal;
    float depth, skin = p->radius * .3f;
    if (obstacle.radius > 0) {
        float distance = sqrtf(fluid_dot(local, local));
        depth = obstacle.radius + skin - distance;
        normal = distance > 1e-6f ? fluid_scale(local, 1 / distance)
                                  : (fluid_vec){ 1, 0 };
    } else {
        float dx = obstacle.half_size.x + skin - fabsf(local.x);
        float dy = obstacle.half_size.y + skin - fabsf(local.y);
        depth = fminf(dx, dy);
        normal = dx < dy ? (fluid_vec){ local.x < 0 ? -1 : 1, 0 }
                         : (fluid_vec){ 0, local.y < 0 ? -1 : 1 };
    }
    if (depth <= 0) {
        fluid_vec particle_start = fluid->work[particle_index].previous;
        fluid_vec particle_delta = fluid_sub(p->position, particle_start);
        bool particle_moved = fluid_dot(particle_delta, particle_delta) > 1e-8f;
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
            fluid_vec from = fluid_sub(
                rotate(fluid_sub(particle_start, p0), cosf(a), -sinf(a)),
                obstacle.center
            );
            fluid_vec to = fluid_sub(
                rotate(fluid_sub(p->position, p1), cosf(b), -sinf(b)),
                obstacle.center
            );
            float fraction;
            fluid_vec local_normal;
            if (segment_contact(from, to, obstacle, p->radius, &fraction, &local_normal)) {
                float angle = lerp_angle(a, b, fraction);
                normal = rotate(local_normal, cosf(angle), sinf(angle));
                fluid_vec center = fluid_add(particle_start, fluid_scale(particle_delta, fraction));
                fluid_vec point = fluid_sub(center, fluid_scale(normal, p->radius));
                fluid_body at_hit = *body;
                if (body->swept) {
                    float body_t = sweep_start + (sweep_end - sweep_start) * fraction;
                    at_hit.position = body_position_at(*body, body_t);
                }
                fluid_vec surface = body_surface_velocity(at_hit, point);
                surface = fluid_add(surface, fluid_scale(body->impulse, body->inverse_mass));
                float closing = -fluid_dot(fluid_sub(p->velocity, surface), normal);
                p->position = fluid_add(center, fluid_scale(normal, skin + 1e-3f));
                if (closing > 0) {
                    float impulse = closing / (1 / p->mass + body->inverse_mass);
                    p->velocity = fluid_add(p->velocity, fluid_scale(normal, impulse / p->mass));
                    body->impulse = fluid_sub(body->impulse, fluid_scale(normal, impulse));
                    fluid_vec local_center = fluid_add(from, fluid_scale(fluid_sub(to, from), fraction));
                    fluid_vec local_point = fluid_add(
                        fluid_sub(local_center, fluid_scale(local_normal, p->radius)),
                        obstacle.center
                    );
                    record_contact(fluid, p, body, obstacle.body, point, normal, local_point, closing, impulse);
                }
            }
        }
        return;
    }
    normal = rotate(normal, c, s);
    fluid_vec point = fluid_sub(original, fluid_scale(normal, p->radius));
    fluid_vec body_velocity = body_surface_velocity(*body, point);
    body_velocity = fluid_add(body_velocity, fluid_scale(body->impulse, body->inverse_mass));
    float closing = -fluid_dot(fluid_sub(p->velocity, body_velocity), normal);
    p->position = fluid_add(p->position, fluid_scale(normal, depth));
    float impulse = fmaxf(0, closing + fminf(depth / dt, 80) * .2f) /
                    (1 / p->mass + body->inverse_mass);
    p->velocity = fluid_add(p->velocity, fluid_scale(normal, impulse / p->mass));
    body->impulse = fluid_sub(body->impulse, fluid_scale(normal, impulse));
    fluid_vec local_point = rotate(fluid_sub(point, body->position), c, -s);
    record_contact(fluid, p, body, obstacle.body, point, normal, local_point, fmaxf(0, closing), impulse);
}

static void collide_obstacles(material_fluid *fluid, float sweep_start, float sweep_end)
{
    fluid_body *bodies = fluid->step->colliders.bodies;
    const fluid_obstacle *obstacles = fluid->step->colliders.obstacles;
    int count = fluid->step->colliders.obstacle_count;
    /* Query the particle hash with each collider's AABB, instead of testing
     * every particle against every cave wall. Corrections use a fresh index. */
    if (!acount(fluid->active)) {
        return;
    }
    index_particles(fluid);
    fluid_region bounds = fluid->bounds;
    float particle_motion_x = 0, particle_motion_y = 0;
    for (int n = 0; n < acount(fluid->active); n++) {
        int particle = fluid->active[n];
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
        float c = cosf(body->angle), s = sinf(body->angle);
        fluid_vec center = fluid_add(body->position, rotate(obstacle.center, c, s));
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
        double query_cells = ((double)x1 - x0 + 1) * ((double)y1 - y0 + 1);
        if (query_cells > acount(fluid->active)) {
            for (int n = 0; n < acount(fluid->active); n++) {
                int particle = fluid->active[n];
                collide(fluid, particle, body, obstacle, fluid->delta, sweep_start, sweep_end);
            }
            continue;
        }
        for (int y = y0; y <= y1; y++) {
            for (int x = x0; x <= x1; x++) {
                for (int j = fluid->heads[fluid_bucket(fluid, x, y)]; j >= 0;
                     j = fluid->work[j].next) {
                    if (fluid->work[j].x == x && fluid->work[j].y == y) {
                        collide(fluid, j, body, obstacle, fluid->delta, sweep_start, sweep_end);
                    }
                }
            }
        }
    }
}

/* Liquid slides along static solids with this Coulomb friction. */
static const float SOLID_FRICTION = .1f;

/* Keeps particles out of static solids. A swept test stops fast particles at
 * thin walls; the signed distance then pushes each one to its skin outside
 * the surface and removes its approach velocity, with friction for liquid. */
static void collide_solids(int start, int end, uint32_t, void *context)
{
    material_fluid *fluid = context;
    const material_solids *solids = fluid->step->solids;
    for (int n = start; n < end; n++) {
        int i = fluid->active[n];
        fluid_particle *p = &fluid->particles[i];
        fluid_vec previous = fluid->work[i].previous;
        fluid_vec moved = fluid_sub(p->position, previous);
        if (fluid_dot(moved, moved) > 1 &&
            material_solids_blocked(solids, previous, p->position)) {
            p->position = previous;
        }
        material_solid_sample s = material_solids_sample(solids, p->position);
        float skin = p->radius * .3f;
        if (s.solid < 0 || s.distance >= skin || !(s.normal.x || s.normal.y)) {
            continue;
        }
        int count;
        float cell = material_solids_items(solids, &count)[s.solid].mask->cell_size;
        p->position = fluid_add(
            p->position,
            fluid_scale(s.normal, fminf(skin - s.distance, cell * 2))
        );
        float approach = fluid_dot(p->velocity, s.normal);
        if (approach >= 0) {
            continue;
        }
        fluid_vec slide = fluid_sub(p->velocity, fluid_scale(s.normal, approach));
        float speed = sqrtf(fluid_dot(slide, slide));
        float friction = p->phase != FLUID_GAS ? SOLID_FRICTION * -approach : 0;
        p->velocity = speed > friction ? fluid_scale(slide, 1 - friction / speed)
                                       : (fluid_vec){ 0 };
    }
}

/* Dense matter falls; gas rises by its buoyancy, faster when hot. */
static void gravitate(material_fluid *fluid, float dt)
{
    fluid_vec g = fluid->step->gravity;
    if (!g.x && !g.y) {
        return;
    }
    for (int n = 0; n < acount(fluid->active); n++) {
        fluid_particle *p = &fluid->particles[fluid->active[n]];
        float lift = p->phase == FLUID_GAS
                       ? -fluid->substances[p->substance].buoyancy *
                             (1 + fmaxf(0, p->temperature))
                       : 1;
        p->velocity = fluid_add(p->velocity, fluid_scale(g, lift * dt));
    }
}

static int compare_indices(const void *left, const void *right)
{
    return *(const int *)left - *(const int *)right;
}

/* Removes doomed particles, highest index first, so each swap moves a
 * surviving particle. The active set stays valid for queries between ticks. */
static void remove_doomed(material_fluid *fluid)
{
    if (!acount(fluid->doomed)) {
        return;
    }
    qsort(fluid->doomed, (size_t)acount(fluid->doomed), sizeof *fluid->doomed, compare_indices);
    int kept = 0;
    for (int n = 0; n < acount(fluid->active); n++) {
        int i = fluid->active[n];
        if (!fluid->work[i].doomed) {
            fluid->work[i].slot = kept;
            fluid->active[kept++] = i;
        }
    }
    asetlen(fluid->active, kept);
    for (int n = acount(fluid->doomed) - 1; n >= 0; n--) {
        int i = fluid->doomed[n], last = acount(fluid->particles) - 1;
        fluid->particles[i] = fluid->particles[last];
        fluid->work[i] = fluid->work[last];
        apop(fluid->particles);
        apop(fluid->work);
        if (i != last && fluid->work[i].slot >= 0) {
            fluid->active[fluid->work[i].slot] = i;
        }
    }
    aclear(fluid->doomed);
}

void material_fluid_tick(material_fluid *fluid, const fluid_step *step)
{
    float delta = step->delta;
    assert(
        delta > 0 && delta <= 1.f / 60 + 1e-6f && isfinite(step->chunk_size) &&
        step->chunk_size > 0 && isfinite(step->gravity.x + step->gravity.y)
    );
    fluid->step = step;
    const fluid_colliders *colliders = &step->colliders;
    clear_contacts(fluid);
    for (int i = 0; i < colliders->body_count; i++) {
        colliders->bodies[i].impulse = (fluid_vec){ 0 };
    }
    for (int i = 0; i < colliders->obstacle_count; i++) {
        assert(
            colliders->obstacles[i].body >= 0 &&
            colliders->obstacles[i].body < colliders->body_count
        );
    }
    aclear(fluid->active);
    aclear(fluid->blasts);
    for (int i = 0; i < acount(fluid->particles);) {
        fluid_particle *p = &fluid->particles[i];
        fluid->work[i].slot = -1;
        if (!awake(p->position, step->chunk_size, step->regions, step->region_count)) {
            i++;
            continue;
        }
        if (p->lifetime > 0) {
            p->lifetime -= delta;
            if (p->lifetime <= 0) {
                *p = alast(fluid->particles);
                fluid->work[i] = alast(fluid->work);
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
        fluid->work[i].slot = acount(fluid->active);
        fluid->work[i].reacted = false;
        apush(fluid->active, i++);
    }
    index_particles(fluid);
    CHIBI_TRACE_ZONE_BEGIN(reaction_zone, "Fluid reactions");
    aclear(fluid->deposits);
    material_fluid_react(fluid);
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
            colliders,
            step->solids
        );
        CHIBI_TRACE_ZONE_END(flow_zone);
    }
    fluid->delta = delta * .5f;
    for (int substep = 0; substep < 2; substep++) {
        gravitate(fluid, fluid->delta);
        for (int n = 0; n < acount(fluid->active); n++) {
            int i = fluid->active[n];
            fluid_particle *p = &fluid->particles[i];
            fluid->work[i].previous = p->position;
            p->position = fluid_add(p->position, fluid_scale(p->velocity, fluid->delta));
        }
        CHIBI_TRACE_ZONE_BEGIN(index_zone, "Fluid index");
        index_particles(fluid);
        CHIBI_TRACE_ZONE_END(index_zone);
        CHIBI_TRACE_ZONE_BEGIN(density_zone, "Fluid density");
        material_fluid_run(fluid, densities, acount(fluid->active), fluid, 256);
        CHIBI_TRACE_ZONE_END(density_zone);
        CHIBI_TRACE_ZONE_BEGIN(relax_zone, "Fluid relaxation");
        material_fluid_run(fluid, relax, acount(fluid->active), fluid, 256);
        CHIBI_TRACE_ZONE_END(relax_zone);
        for (int n = 0; n < acount(fluid->active); n++) {
            int i = fluid->active[n];
            fluid_particle *p = &fluid->particles[i];
            p->position = fluid_add(p->position, fluid->work[i].correction);
            p->velocity = fluid_scale(
                fluid_sub(p->position, fluid->work[i].previous),
                1 / fluid->delta
            );
        }
        CHIBI_TRACE_ZONE_BEGIN(collision_zone, "Fluid collisions");
        collide_obstacles(fluid, substep * .5f, (substep + 1) * .5f);
        if (step->solids) {
            material_fluid_run(fluid, collide_solids, acount(fluid->active), fluid, 256);
        }
        CHIBI_TRACE_ZONE_END(collision_zone);
    }
    remove_doomed(fluid);
    fluid->tick++;
    fluid->step = NULL;
}
