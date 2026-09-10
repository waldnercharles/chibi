#include "render/particles.h"

#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#define PARTICLE_SEED UINT32_C(0x9e3779b9)

struct game_particles
{
    game_particle *items;
    uint32_t random;
};

game_particles *game_particles_new()
{
    game_particles *particles = xcalloc(1, sizeof *particles);
    particles->random = PARTICLE_SEED;
    return particles;
}

void game_particles_clear(game_particles *particles)
{
    assert(particles);
    aclear(particles->items);
    particles->random = PARTICLE_SEED;
}

void game_particles_free(game_particles *particles)
{
    assert(particles);
    afree(particles->items);
    free(particles);
}

static float random_unit(game_particles *particles)
{
    uint32_t value = particles->random;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    particles->random = value;
    return (float)(value >> 8) * (1.0f / 16777216.0f);
}

static bool valid_burst(const game_particle_burst *burst)
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

int game_particles_emit(game_particles *particles, const game_particle_burst *burst)
{
    assert(particles && burst);
    if (!valid_burst(burst)) {
        return -1;
    }
    int count = GAME_PARTICLE_LIMIT - acount(particles->items);
    if (count > burst->count) {
        count = burst->count;
    }
    if (!count) {
        return 0;
    }
    asetcap(particles->items, GAME_PARTICLE_LIMIT);
    for (int i = 0; i < count; i++) {
        float angle = random_unit(particles) * 6.2831853071795864769f;
        float speed = burst->speed * (0.4f + 0.6f * random_unit(particles));
        game_particle particle = {
            .position = burst->position,
            .velocity = { cosf(angle) * speed, sinf(angle) * speed },
            .color = burst->color,
            .lifetime = burst->lifetime,
            .end_color = burst->color & 0xffffff00u,
            .size = 1.4f + speed * .015f,
            .size_exponent = .65f,
            .emission = 1.2f,
            .angle = angle,
            .shape = GAME_PARTICLE_STREAK,
        };
        apush(particles->items, particle);
    }
    return count;
}

static bool range(float low, float high)
{
    return isfinite(low) && isfinite(high) && low >= 0 && high >= low;
}

static float sample(game_particles *particles, float low, float high)
{
    return low + (high - low) * random_unit(particles);
}

int game_particles_emit_directed(game_particles *particles, const game_particle_spray *spray)
{
    assert(particles && spray);
    float direction = hypotf(spray->direction.x, spray->direction.y);
    game_particle_burst bound = { .position = spray->position,
                                  .count = spray->count,
                                  .speed = spray->speed_max,
                                  .lifetime = spray->lifetime_max };
    if (!valid_burst(&bound) || !isfinite(direction) || direction <= 0 ||
        !range(spray->speed_min, spray->speed_max) ||
        !range(spray->lifetime_min, spray->lifetime_max) ||
        spray->lifetime_min <= 0 || !range(spray->size_min, spray->size_max) ||
        spray->size_min <= 0 || !range(spray->drag_min, spray->drag_max) ||
        !range(spray->emission_min, spray->emission_max) || !range(0, spray->spread) ||
        !range(0, spray->size_exponent) || !isfinite(spray->elevation) ||
        spray->shape < 0 || spray->shape >= GAME_PARTICLE_SHAPE_COUNT) {
        return -1;
    }
    int count = GAME_PARTICLE_LIMIT - acount(particles->items);
    if (count > spray->count) {
        count = spray->count;
    }
    if (!count) {
        return 0;
    }
    asetcap(particles->items, GAME_PARTICLE_LIMIT);
    float facing = atan2f(spray->direction.y, spray->direction.x);
    for (int i = 0; i < count; i++) {
        float angle = facing + (random_unit(particles) - 0.5f) * spray->spread;
        float speed = sample(particles, spray->speed_min, spray->speed_max);
        game_particle particle = {
            .position = spray->position,
            .velocity = { cosf(angle) * speed, sinf(angle) * speed },
            .color = spray->color,
            .end_color = spray->end_color,
            .lifetime = sample(particles, spray->lifetime_min, spray->lifetime_max),
            .size = sample(particles, spray->size_min, spray->size_max),
            .size_exponent = spray->size_exponent,
            .drag = sample(particles, spray->drag_min, spray->drag_max),
            .emission = sample(particles, spray->emission_min, spray->emission_max),
            .elevation = spray->elevation,
            .shape = spray->shape,
            .angle = angle,
        };
        apush(particles->items, particle);
    }
    return count;
}

int game_particles_emit_particle(game_particles *particles, const game_particle *particle)
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
        particle->shape < 0 || particle->shape >= GAME_PARTICLE_SHAPE_COUNT ||
        fabs((double)particle->position.x) + fabs((double)particle->velocity.x) * remaining >
            FLT_MAX ||
        fabs((double)particle->position.y) + fabs((double)particle->velocity.y) * remaining >
            FLT_MAX ||
        fabs((double)particle->elevation) +
                fabs((double)particle->elevation_velocity) * remaining >
            FLT_MAX) {
        return -1;
    }
    if (acount(particles->items) == GAME_PARTICLE_LIMIT) {
        return 0;
    }
    apush(particles->items, *particle);
    return 1;
}

int game_particles_tick(game_particles *particles, double delta)
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
        game_particle particle = particles->items[i];
        double age = particle.age + delta;
        if (age >= particle.lifetime) {
            continue;
        }
        particle.age = (float)age;
        double travel = delta;
        float decay = 1;
        if (particle.drag > 0) {
            double exponent = -(double)particle.drag * delta;
            decay = (float)exp(exponent);
            travel = -expm1(exponent) / particle.drag;
        }
        particle.position.x = (float)(particle.position.x +
                                      (double)particle.velocity.x * travel);
        particle.position.y = (float)(particle.position.y +
                                      (double)particle.velocity.y * travel);
        particle.elevation = (float)(particle.elevation +
                                     (double)particle.elevation_velocity * travel);
        particle.velocity.x *= decay;
        particle.velocity.y *= decay;
        particle.elevation_velocity *= decay;
        particles->items[keep++] = particle;
    }
    asetlen(particles->items, keep);
    return 0;
}

const game_particle *game_particles_data(const game_particles *particles)
{
    assert(particles);
    return particles->items;
}

int game_particles_count(const game_particles *particles)
{
    assert(particles);
    return acount(particles->items);
}
