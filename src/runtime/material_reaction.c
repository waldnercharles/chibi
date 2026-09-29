/* Heat, burning, phase changes and reactions between touching substances.
 * Workers only read the particle snapshot and write their own scratch;
 * every change to matter commits serially in particle order. */
#include "runtime/material_fluid_internal.h"
#include "runtime/material_solid.h"
#include "support/array.h"

#include <assert.h>
#include <math.h>

/* Flammable matter takes this fraction of a hotter neighbor's heat at once,
 * so flames race through fuel instead of diffusing. */
static const float FLAME_SPREAD = .85f;
/* Fraction of a hot or cold body's temperature difference a touching
 * particle takes per second. */
static const float BODY_CONDUCTION = 6;
/* Temperatures this close to ambient are ambient. */
static const float AMBIENT = 1e-3f;
/* Pairs react within this fraction of the kernel. */
static const float CONTACT = .7f;
enum
{
    /* Simultaneous ignitions keep burning, with bounded blast work. */
    BLAST_LIMIT = 32
};

static const fluid_substance *substance_of(const material_fluid *fluid, const fluid_particle *p)
{
    return &fluid->substances[p->substance];
}

static bool flammable(const material_fluid *fluid, const fluid_particle *p)
{
    return substance_of(fluid, p)->ignite_at > 0 && !p->burning;
}

/* Hot and cold bodies heat or chill the matter along their swept path;
 * hot bodies ignite flammable matter they reach. Walls shield it. */
static void body_heat(material_fluid *fluid)
{
    const fluid_step *step = fluid->step;
    const fluid_colliders *colliders = &step->colliders;
    float h = fluid->kernel, dt = step->delta;
    fluid_region b = fluid->bounds;
    for (int k = 0; k < colliders->body_count; k++) {
        fluid_body body = colliders->bodies[k];
        if (body.heat == 0) {
            continue;
        }
        fluid_vec from = fluid_sub(body.position, fluid_scale(body.velocity, dt));
        fluid_vec d = fluid_sub(body.position, from);
        float x0 = fmaxf(fminf(from.x, body.position.x) - h, b.x);
        float y0 = fmaxf(fminf(from.y, body.position.y) - h, b.y);
        float x1 = fminf(fmaxf(from.x, body.position.x) + h, b.x + b.width);
        float y1 = fminf(fmaxf(from.y, body.position.y) + h, b.y + b.height);
        for (int y = (int)floorf(y0 / h); y <= (int)floorf(y1 / h); y++) {
            for (int x = (int)floorf(x0 / h); x <= (int)floorf(x1 / h); x++) {
                for (int i = fluid->heads[fluid_bucket(fluid, x, y)]; i >= 0;
                     i = fluid->work[i].next) {
                    fluid_particle *p = &fluid->particles[i];
                    if (fluid->work[i].x != x || fluid->work[i].y != y) {
                        continue;
                    }
                    float t = fmaxf(
                        0,
                        fminf(1, fluid_dot(fluid_sub(p->position, from), d) / fmaxf(fluid_dot(d, d), 1e-8f))
                    );
                    fluid_vec near = fluid_add(from, fluid_scale(d, t)),
                              gap = fluid_sub(p->position, near);
                    if (fluid_dot(gap, gap) > h * h ||
                        material_fluid_blocked(near, p->position, colliders, step->solids, k)) {
                        continue;
                    }
                    if (flammable(fluid, p) && body.heat > p->temperature) {
                        p->temperature = body.heat;
                        continue;
                    }
                    p->temperature += (body.heat - p->temperature) *
                                      fminf(1, dt * BODY_CONDUCTION);
                }
            }
        }
    }
}

static bool warm(const fluid_particle *p)
{
    return p->burning || fabsf(p->temperature) > AMBIENT;
}

/* Marks the hash buckets holding warm matter; colliding buckets only cost
 * a wasted search. */
static void mark_warm(material_fluid *fluid)
{
    for (int n = 0; n < acount(fluid->warm_used); n++) {
        fluid->warm[fluid->warm_used[n]] = 0;
    }
    aclear(fluid->warm_used);
    for (int n = 0; n < acount(fluid->active); n++) {
        int i = fluid->active[n];
        unsigned bucket = fluid_bucket(fluid, fluid->work[i].x, fluid->work[i].y);
        if (warm(&fluid->particles[i]) && !fluid->warm[bucket]) {
            fluid->warm[bucket] = 1;
            apush(fluid->warm_used, (int)bucket);
        }
    }
}

static bool warm_near(const material_fluid *fluid, const particle_work *w)
{
    for (int y = w->y - 1; y <= w->y + 1; y++) {
        for (int x = w->x - 1; x <= w->x + 1; x++) {
            if (fluid->warm[fluid_bucket(fluid, x, y)]) {
                return true;
            }
        }
    }
    return false;
}

/* Touching matter exchanges heat; flames leap between flammable parcels
 * unless a wall separates them. */
static void conduct(int start, int end, uint32_t, void *context)
{
    material_fluid *fluid = context;
    const fluid_step *step = fluid->step;
    float h = fluid->kernel;
    float rate = fminf(1, step->delta * fluid->heat.conduction);
    for (int n = start; n < end; n++) {
        int i = fluid->active[n];
        fluid_particle p = fluid->particles[i];
        particle_work *w = &fluid->work[i];
        w->heat = p.temperature;
        bool burnable = flammable(fluid, &p);
        if ((!burnable && rate == 0) || !warm_near(fluid, w)) {
            continue;
        }
        float weights = 0, sum = 0, spread = p.temperature;
        for (int y = w->y - 1; y <= w->y + 1; y++) {
            for (int x = w->x - 1; x <= w->x + 1; x++) {
                for (int j = fluid->heads[fluid_bucket(fluid, x, y)]; j >= 0;
                     j = fluid->work[j].next) {
                    if (j == i || fluid->work[j].x != x || fluid->work[j].y != y) {
                        continue;
                    }
                    fluid_particle other = fluid->particles[j];
                    fluid_vec d = fluid_sub(other.position, p.position);
                    float distance = fluid_dot(d, d);
                    if (distance >= h * h) {
                        continue;
                    }
                    float q = 1 - sqrtf(distance) / h;
                    weights += q;
                    sum += q * other.temperature;
                    if (burnable && other.temperature * FLAME_SPREAD > spread &&
                        !material_fluid_blocked(other.position, p.position, &step->colliders, step->solids, -1)) {
                        spread = other.temperature * FLAME_SPREAD;
                    }
                }
            }
        }
        float heat = p.temperature;
        if (weights > 0) {
            heat += (sum / weights - heat) * rate;
        }
        w->heat = burnable ? fmaxf(heat, spread) : heat;
    }
}

static void ignite(material_fluid *fluid, fluid_vec p, float weight)
{
    if (weight <= 0) {
        return;
    }
    float size = fluid->kernel * 4;
    int x = (int)floorf(p.x / size), y = (int)floorf(p.y / size);
    for (int i = 0; i < acount(fluid->blasts); i++) {
        fluid_blast *b = &fluid->blasts[i];
        if ((int)floorf(b->position.x / size) == x &&
            (int)floorf(b->position.y / size) == y) {
            b->ignitions += weight;
            return;
        }
    }
    /* The earliest positions are stable in particle order. */
    if (acount(fluid->blasts) < BLAST_LIMIT) {
        apush(fluid->blasts, ((fluid_blast){ p, weight }));
    }
}

/* Particle i turns into product. Returns false when it left the world. */
static bool become(material_fluid *fluid, int i, int product)
{
    fluid_particle *p = &fluid->particles[i];
    if (product == FLUID_KEEP) {
        return true;
    }
    if (product <= FLUID_DEPOSIT) {
        float spacing = material_fluid_spacing(fluid, p->phase);
        apush(fluid->deposits, ((fluid_deposit){ p->position, FLUID_DEPOSIT - product, spacing * spacing }));
    }
    if (product < 0) {
        material_fluid_doom(fluid, i);
        return false;
    }
    fluid_particle next = material_fluid_particle(fluid, product, p->position);
    next.velocity = p->velocity;
    next.temperature = fmaxf(p->temperature, next.temperature);
    *p = next;
    return true;
}

static float glow(const material_fluid *fluid, const fluid_particle *p)
{
    float full = fluid->heat.glow_temperature;
    if (p->burning) {
        return 1;
    }
    /* Emitted parcels keep their authored look. */
    if ((p->style.duration > 0 && !p->billow) || full <= 0) {
        return 0;
    }
    return fminf(1, fmaxf(0, 2 * p->temperature / full - 1));
}

/* Cooling, burning and phase changes, in particle order. */
static void transition(material_fluid *fluid)
{
    float dt = fluid->step->delta;
    for (int n = 0; n < acount(fluid->active); n++) {
        int i = fluid->active[n];
        fluid_particle *p = &fluid->particles[i];
        const fluid_substance *s = substance_of(fluid, p);
        float heat = fluid->work[i].heat;
        if (!p->burning) {
            heat *= expf(-s->cooling * dt);
        }
        p->temperature = fabsf(heat) < AMBIENT ? 0 : heat;
        bool kept = true;
        if (p->burning) {
            p->temperature = fmaxf(p->temperature, s->burn_heat);
            p->fuel = fmaxf(0, p->fuel - s->burn_rate * dt);
            if (p->fuel == 0) {
                kept = become(fluid, i, s->burns_to);
            }
        } else if (s->ignite_at > 0 && p->temperature >= s->ignite_at) {
            p->burning = true;
            p->temperature = fmaxf(p->temperature, s->burn_heat);
            ignite(fluid, p->position, s->blast);
        } else if (s->boils_to != FLUID_KEEP && p->temperature >= s->boil_at) {
            kept = become(fluid, i, s->boils_to);
        } else if (s->freezes_to != FLUID_KEEP && p->temperature <= s->freeze_at) {
            kept = become(fluid, i, s->freezes_to);
        }
        if (kept) {
            p->glow = glow(fluid, p);
        }
    }
}

/* Active particles grouped by substance, in particle order, and the burning. */
static void group(material_fluid *fluid)
{
    int substances = acount(fluid->substances);
    asetlen(fluid->substance_start, substances + 1);
    for (int s = 0; s <= substances; s++) {
        fluid->substance_start[s] = 0;
    }
    aclear(fluid->burning);
    for (int n = 0; n < acount(fluid->active); n++) {
        int i = fluid->active[n];
        if (fluid->work[i].doomed) {
            continue;
        }
        fluid->substance_start[fluid->particles[i].substance + 1]++;
        if (fluid->particles[i].burning) {
            apush(fluid->burning, i);
        }
    }
    for (int s = 0; s < substances; s++) {
        fluid->substance_start[s + 1] += fluid->substance_start[s];
    }
    asetlen(fluid->by_substance, fluid->substance_start[substances]);
    int *cursor = NULL;
    acopy(cursor, fluid->substance_start);
    for (int n = 0; n < acount(fluid->active); n++) {
        int i = fluid->active[n];
        if (!fluid->work[i].doomed) {
            fluid->by_substance[cursor[fluid->particles[i].substance]++] = i;
        }
    }
    afree(cursor);
}

typedef struct
{
    material_fluid *fluid;
    const int *origins;
    /* The substance partners need, or FLUID_BURNING. */
    int partner;
    float chance;
    unsigned salt;
} reaction_search;

static float roll(unsigned tick, int i, unsigned salt)
{
    unsigned h = tick * 0x9e3779b9u ^ (unsigned)i * 0x85ebca6bu ^ salt * 0xc2b2ae35u;
    h = (h ^ (h >> 16)) * 0x7feb352du;
    h = (h ^ (h >> 15)) * 0x846ca68bu;
    return ((h ^ (h >> 16)) & 0xffffff) / 16777216.0f;
}

static bool matches(const material_fluid *fluid, int j, int partner)
{
    const fluid_particle *p = &fluid->particles[j];
    return partner == FLUID_BURNING ? p->burning : p->substance == partner;
}

/* Each origin proposes its nearest-indexed eligible partner in contact. */
static void propose(int start, int end, uint32_t, void *context)
{
    const reaction_search *search = context;
    material_fluid *fluid = search->fluid;
    float reach = fluid->kernel * CONTACT;
    for (int n = start; n < end; n++) {
        int i = search->origins[n];
        particle_work *w = &fluid->work[i];
        w->partner = -1;
        if (w->reacted || w->doomed ||
            roll(fluid->tick, i, search->salt) >= search->chance) {
            continue;
        }
        fluid_vec position = fluid->particles[i].position;
        for (int y = w->y - 1; y <= w->y + 1; y++) {
            for (int x = w->x - 1; x <= w->x + 1; x++) {
                for (int j = fluid->heads[fluid_bucket(fluid, x, y)]; j >= 0;
                     j = fluid->work[j].next) {
                    const particle_work *other = &fluid->work[j];
                    if (j == i || other->x != x || other->y != y || other->reacted ||
                        other->doomed || (w->partner >= 0 && j > w->partner) ||
                        !matches(fluid, j, search->partner)) {
                        continue;
                    }
                    fluid_vec d = fluid_sub(fluid->particles[j].position, position);
                    if (fluid_dot(d, d) < reach * reach) {
                        w->partner = j;
                    }
                }
            }
        }
    }
}

static void finish(material_fluid *fluid, int i, int product, float heat)
{
    fluid->work[i].reacted = true;
    if (become(fluid, i, product)) {
        fluid->particles[i].temperature += heat;
    }
}

static void react_pairs(material_fluid *fluid)
{
    float dt = fluid->step->delta;
    for (int r = 0; r < acount(fluid->reactions); r++) {
        fluid_reaction reaction = fluid->reactions[r];
        const int *as = fluid->by_substance + fluid->substance_start[reaction.a];
        int a_count = fluid->substance_start[reaction.a + 1] -
                      fluid->substance_start[reaction.a];
        bool burning = reaction.b == FLUID_BURNING;
        const int *bs = burning ? fluid->burning
                                : fluid->by_substance +
                                      fluid->substance_start[reaction.b];
        int b_count = burning ? acount(fluid->burning)
                              : fluid->substance_start[reaction.b + 1] -
                                    fluid->substance_start[reaction.b];
        if (!a_count || !b_count) {
            continue;
        }
        /* Search from the rarer side; each pair is found either way. */
        bool from_a = a_count <= b_count;
        reaction_search search = { fluid,
                                   from_a ? as : bs,
                                   from_a ? reaction.b : reaction.a,
                                   1 - expf(-reaction.rate * dt),
                                   (unsigned)r + 1 };
        int count = from_a ? a_count : b_count;
        material_fluid_run(fluid, propose, count, &search, 64);
        for (int n = 0; n < count; n++) {
            int i = search.origins[n], j = fluid->work[i].partner;
            if (j < 0 || fluid->work[i].reacted || fluid->work[j].reacted) {
                continue;
            }
            int a = from_a ? i : j, b = from_a ? j : i;
            finish(fluid, a, reaction.a_becomes, reaction.heat);
            finish(fluid, b, reaction.b_becomes, reaction.heat);
        }
    }
}

void material_fluid_react(material_fluid *fluid)
{
    if (!acount(fluid->active)) {
        return;
    }
    body_heat(fluid);
    mark_warm(fluid);
    material_fluid_run(fluid, conduct, acount(fluid->active), fluid, 64);
    transition(fluid);
    if (acount(fluid->reactions)) {
        group(fluid);
        react_pairs(fluid);
    }
}
