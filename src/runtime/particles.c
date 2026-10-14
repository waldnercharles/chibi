#include "runtime/particles.h"

#include "support/array.h"
#include "support/cos_sin.h"
#include "support/xalloc.h"

#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#define PARTICLE_SEED UINT32_C(0x9e3779b9)

struct particles
{
    particle *items;
    uint32_t random;
};

float particle_curve_value(float start, float end, float exponent, float age)
{
    float t = exponent > 0 ? powf(fminf(1, fmaxf(0, age)), exponent) : 0;
    return start + (end - start) * t;
}

particles *particles_new()
{
    particles *particles = xcalloc(1, sizeof *particles);
    particles->random = PARTICLE_SEED;
    return particles;
}

void particles_clear(particles *particles)
{
    assert(particles);
    aclear(particles->items);
    particles->random = PARTICLE_SEED;
}

void particles_free(particles *particles)
{
    assert(particles);
    afree(particles->items);
    free(particles);
}

static float random_unit(particles *particles)
{
    uint32_t value = particles->random;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    particles->random = value;
    return (float)(value >> 8) * (1.0f / 16777216.0f);
}

static bool valid_burst(const particle_burst *burst)
{
    if (!isfinite(burst->position.x) || !isfinite(burst->position.y) ||
        !isfinite(burst->speed) || !isfinite(burst->lifetime) ||
        burst->count < 0 || burst->speed < 0 || burst->lifetime <= 0) {
        return false;
    }
    double travel = (double)burst->speed * burst->lifetime;
    return fabs((double)burst->position.x) + travel <= FLT_MAX &&
           fabs((double)burst->position.y) + travel <= FLT_MAX;
}

int particles_emit(particles *particles, const particle_burst *burst)
{
    assert(particles && burst);
    if (!valid_burst(burst)) {
        return -1;
    }
    int count = PARTICLE_LIMIT - acount(particles->items);
    if (count > burst->count) {
        count = burst->count;
    }
    if (!count) {
        return 0;
    }
    asetcap(particles->items, PARTICLE_LIMIT);
    for (int i = 0; i < count; i++) {
        float angle = random_unit(particles) * 6.2831853071795864769f;
        float speed = burst->speed * (0.4f + 0.6f * random_unit(particles));
        cos_sin direction = cos_sin_compute(angle);
        particle particle = {
            .position = burst->position,
            .velocity = { direction.c * speed, direction.s * speed },
            .color = burst->color,
            .lifetime = burst->lifetime,
            .end_color = burst->color & 0xffffff00u,
            .size = 1.4f + speed * .015f,
            .size_exponent = .65f,
            .emission = 1.2f,
            .angle = angle,
            .antialias = 1.5f,
            .absorption = .01f,
            .shape = PARTICLE_STREAK,
        };
        apush(particles->items, particle);
    }
    return count;
}

static bool range(float low, float high)
{
    return isfinite(low) && isfinite(high) && low >= 0 && high >= low;
}

static float sample(particles *particles, float low, float high)
{
    return low + (high - low) * random_unit(particles);
}

int particles_emit_directed(particles *particles, const particle_spray *spray)
{
    assert(particles && spray);
    float direction = hypotf(spray->direction.x, spray->direction.y);
    particle_burst bound = { .position = spray->position,
                             .count = spray->count,
                             .speed = spray->speed_max,
                             .lifetime = spray->lifetime_max };
    if (!valid_burst(&bound) || !isfinite(direction) || direction <= 0 ||
        !range(spray->speed_min, spray->speed_max) ||
        !range(spray->lifetime_min, spray->lifetime_max) ||
        spray->lifetime_min <= 0 || !range(spray->size_min, spray->size_max) ||
        spray->size_min <= 0 || !range(spray->drag_min, spray->drag_max) ||
        !range(spray->emission_min, spray->emission_max) ||
        !range(0, spray->spread) || !range(0, spray->size_exponent) ||
        !range(0, spray->emission_exponent) || !range(0, spray->stretch) ||
        !range(0, spray->antialias) || !isfinite(spray->elevation) ||
        spray->shape < 0 || spray->shape >= PARTICLE_SHAPE_COUNT) {
        return -1;
    }
    int count = PARTICLE_LIMIT - acount(particles->items);
    if (count > spray->count) {
        count = spray->count;
    }
    if (!count) {
        return 0;
    }
    asetcap(particles->items, PARTICLE_LIMIT);
    float facing = cos_sin_atan2(spray->direction.y, spray->direction.x);
    for (int i = 0; i < count; i++) {
        float angle = facing + (random_unit(particles) - 0.5f) * spray->spread;
        float speed = sample(particles, spray->speed_min, spray->speed_max);
        cos_sin direction = cos_sin_compute(angle);
        particle particle = {
            .position = spray->position,
            .velocity = { direction.c * speed, direction.s * speed },
            .color = spray->color,
            .end_color = spray->end_color,
            .lifetime = sample(particles, spray->lifetime_min, spray->lifetime_max),
            .size = sample(particles, spray->size_min, spray->size_max),
            .size_exponent = spray->size_exponent,
            .drag = sample(particles, spray->drag_min, spray->drag_max),
            .emission = sample(particles, spray->emission_min, spray->emission_max),
            .emission_exponent = spray->emission_exponent,
            .stretch = spray->stretch,
            .elevation = spray->elevation,
            .shape = spray->shape,
            .angle = angle,
            .antialias = spray->antialias,
            .absorption = .01f,
        };
        apush(particles->items, particle);
    }
    return count;
}

int particles_emit_particle(particles *particles, const particle *particle)
{
    assert(particles && particle);
    double remaining = (double)particle->lifetime - particle->age;
    if (!isfinite(particle->position.x) || !isfinite(particle->position.y) ||
        !isfinite(particle->velocity.x) || !isfinite(particle->velocity.y) ||
        !isfinite(particle->elevation) ||
        !isfinite(particle->elevation_velocity) || !isfinite(particle->age) ||
        particle->age < 0 || !isfinite(particle->lifetime) || remaining <= 0 ||
        !isfinite(particle->size) || particle->size <= 0 ||
        !range(0, particle->size_exponent) || !range(0, particle->drag) ||
        !range(0, particle->emission) || !isfinite(particle->angle) ||
        particle->shape < 0 || particle->shape >= PARTICLE_SHAPE_COUNT ||
        fabs((double)particle->position.x) + fabs((double)particle->velocity.x) * remaining >
            FLT_MAX ||
        fabs((double)particle->position.y) + fabs((double)particle->velocity.y) * remaining >
            FLT_MAX ||
        fabs((double)particle->elevation) +
                fabs((double)particle->elevation_velocity) * remaining >
            FLT_MAX) {
        return -1;
    }
    if (acount(particles->items) == PARTICLE_LIMIT) {
        return 0;
    }
    apush(particles->items, *particle);
    return 1;
}

static double drag_integral(const particle *p, double age, double delta)
{
    double power = p->drag_exponent + 1.0;
    return p->drag * delta + (p->drag_end - p->drag) *
                                 (pow(age + delta, power) - pow(age, power)) /
                                 (power * pow(p->lifetime, p->drag_exponent));
}

/* Constant drag has a closed-form displacement. For age-varying drag, velocity
 * uses the exact integral and eight-point Gaussian quadrature integrates travel. */
static double curved_drag_travel(const particle *p, double delta)
{
    static const double nodes[] = { .1834346424956498,
                                    .5255324099163290,
                                    .7966664774136267,
                                    .9602898564975363 };
    static const double weights[] = { .3626837833783620,
                                      .3137066458778873,
                                      .2223810344533745,
                                      .1012285362903763 };
    double travel = 0;
    for (int i = 0; i < 4; i++) {
        double left = delta * .5 * (1 - nodes[i]);
        double right = delta * .5 * (1 + nodes[i]);
        travel += weights[i] * (exp(-drag_integral(p, p->age, left)) +
                                exp(-drag_integral(p, p->age, right)));
    }
    return travel * delta * .5;
}

void particle_advance(particle *particle, double delta)
{
    double travel = delta;
    float decay = 1;
    if (particle->drag_exponent > 0 && particle->drag != particle->drag_end) {
        decay = (float)exp(-drag_integral(particle, particle->age, delta));
        travel = curved_drag_travel(particle, delta);
    } else if (particle->drag > 0) {
        double exponent = -(double)particle->drag * delta;
        decay = (float)exp(exponent);
        travel = -expm1(exponent) / particle->drag;
    }
    particle->position.x = (float)(particle->position.x +
                                   (double)particle->velocity.x * travel);
    particle->position.y = (float)(particle->position.y +
                                   (double)particle->velocity.y * travel);
    particle->elevation = (float)(particle->elevation +
                                  (double)particle->elevation_velocity * travel);
    particle->velocity.x *= decay;
    particle->velocity.y *= decay;
    particle->elevation_velocity *= decay;
    particle->age += (float)delta;
}

int particles_tick(particles *particles, double delta)
{
    assert(particles);
    if (!isfinite(delta) || delta < 0) {
        return -1;
    }
    if (delta == 0) {
        return 0;
    }
    int keep = 0;
    for (int i = 0; i < acount(particles->items); i++) {
        particle particle = particles->items[i];
        double age = particle.age + delta;
        if (age >= particle.lifetime) {
            continue;
        }
        particle_advance(&particle, delta);
        particles->items[keep++] = particle;
    }
    asetlen(particles->items, keep);
    return 0;
}

const particle *particles_data(const particles *particles)
{
    assert(particles);
    return particles->items;
}

int particles_count(const particles *particles)
{
    assert(particles);
    return acount(particles->items);
}
