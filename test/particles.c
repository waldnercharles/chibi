#include "runtime/particles.h"

#include <assert.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>

static void equal_particle(const particle *a, const particle *b)
{
    assert(a->position.x == b->position.x && a->position.y == b->position.y);
    assert(a->velocity.x == b->velocity.x && a->velocity.y == b->velocity.y);
    assert(a->color == b->color && a->age == b->age && a->lifetime == b->lifetime);
}

static void invalid_inputs(particles *particles, particle_burst burst)
{
    particle original = particles_data(particles)[0];
    particle_burst invalid[] = { burst, burst, burst, burst,
                                 burst, burst, burst, burst };
    invalid[0].position.x = NAN;
    invalid[1].position.y = INFINITY;
    invalid[2].speed = NAN;
    invalid[3].speed = -1;
    invalid[4].lifetime = INFINITY;
    invalid[5].lifetime = 0;
    invalid[6].count = -1;
    invalid[7].speed = FLT_MAX;
    invalid[7].lifetime = FLT_MAX;
    for (int i = 0; i < 8; i++) {
        assert(particles_emit(particles, &invalid[i]) == -1);
        assert(particles_count(particles) == 1);
        equal_particle(&original, particles_data(particles));
    }
    assert(particles_tick(particles, NAN) == -1);
    assert(particles_tick(particles, INFINITY) == -1);
    assert(particles_tick(particles, -1) == -1);
    assert(particles_tick(particles, 0) == 0);
    equal_particle(&original, particles_data(particles));
}

void test_particles()
{
    assert(fabsf(particle_curve_value(4, 0, 2, .5f) - 3) < .00001f);
    assert(particle_curve_value(4, 0, 0, .8f) == 4);
    assert(particle_curve_value(4, 2, 1, 1) == 2);
    particle curved = { .lifetime = 2,
                        .velocity = { 100, 0 },
                        .drag = 2,
                        .drag_end = 6,
                        .drag_exponent = 1 };
    particle split = curved;
    particle_advance(&curved, 1);
    for (int i = 0; i < 60; i++) {
        particle_advance(&split, 1.0 / 60);
    }
    assert(fabsf(curved.velocity.x - 100 * expf(-3)) < .00001f);
    assert(fabsf(curved.position.x - split.position.x) < .0001f);
    particles *one_step = particles_new(), *many_steps = particles_new();
    particle spatial = { .position = { 3, 4 },
                         .elevation = 8,
                         .velocity = { 20, -10 },
                         .elevation_velocity = 5,
                         .size = 2,
                         .size_exponent = 1,
                         .lifetime = 1,
                         .drag = 3,
                         .shape = PARTICLE_TRIANGLE };
    assert(particles_emit_particle(one_step, &spatial) == 1);
    assert(particles_emit_particle(many_steps, &spatial) == 1);
    assert(particles_tick(one_step, .5) == 0);
    for (int i = 0; i < 10; i++) {
        assert(particles_tick(many_steps, .05) == 0);
    }
    const particle *a = particles_data(one_step), *b = particles_data(many_steps);
    assert(
        fabsf(a->position.x - b->position.x) < .00001f &&
        fabsf(a->position.y - b->position.y) < .00001f
    );
    assert(fabsf(a->elevation - b->elevation) < .00001f && a->shape == PARTICLE_TRIANGLE);
    spatial.shape = PARTICLE_SHAPE_COUNT;
    assert(particles_emit_particle(one_step, &spatial) == -1 && particles_count(one_step) == 1);
    particles_free(one_step);
    particles_free(many_steps);
    particles *particles = particles_new();
    assert(particles_count(particles) == 0);
    assert(particles_tick(particles, 1) == 0);
    particle_burst burst = { .position = { 3, -4 },
                             .color = 0xff8844ffu,
                             .count = 1,
                             .speed = 20,
                             .lifetime = 1 };
    assert(particles_emit(particles, &burst) == 1);
    particle first = particles_data(particles)[0];
    assert(
        first.position.x == 3 && first.position.y == -4 && first.color == burst.color
    );
    assert(first.age == 0 && first.lifetime == 1);
    invalid_inputs(particles, burst);
    assert(particles_tick(particles, 0.25) == 0);
    const particle *moved = particles_data(particles);
    assert(moved->age == 0.25f);
    assert(fabsf(moved->position.x - (3 + first.velocity.x * 0.25f)) < 0.00001f);
    assert(fabsf(moved->position.y - (-4 + first.velocity.y * 0.25f)) < 0.00001f);
    assert(particles_tick(particles, 0.75) == 0);
    assert(particles_count(particles) == 0);
    particles_clear(particles);
    assert(particles_emit(particles, &burst) == 1);
    equal_particle(&first, particles_data(particles));

    particles *reference = particles_new();
    assert(particles_emit(reference, &burst) == 1);
    invalid_inputs(particles, burst);
    assert(particles_emit(particles, &burst) == 1);
    assert(particles_emit(reference, &burst) == 1);
    equal_particle(&particles_data(particles)[1], &particles_data(reference)[1]);
    particles_free(reference);

    particles_clear(particles);
    burst.count = 0;
    assert(particles_emit(particles, &burst) == 0);
    assert(particles_count(particles) == 0);
    burst.count = INT_MAX;
    assert(particles_emit(particles, &burst) == PARTICLE_LIMIT);
    assert(particles_count(particles) == PARTICLE_LIMIT);
    assert(particles_emit(particles, &burst) == 0);
    const particle *items = particles_data(particles);
    for (int i = 0; i < particles_count(particles); i++) {
        assert(isfinite(items[i].position.x) && isfinite(items[i].position.y));
        assert(isfinite(items[i].velocity.x) && isfinite(items[i].velocity.y));
        assert(hypotf(items[i].velocity.x, items[i].velocity.y) <= burst.speed * 1.00001f);
    }
    assert(particles_tick(particles, DBL_MAX) == 0);
    assert(particles_count(particles) == 0);
    burst.count = 1;
    burst.speed = 0;
    burst.lifetime = 0.5f;
    assert(particles_emit(particles, &burst) == 1);
    burst.lifetime = 1;
    assert(particles_emit(particles, &burst) == 1);
    assert(particles_tick(particles, 0.5) == 0);
    assert(particles_count(particles) == 1);
    assert(particles_data(particles)[0].lifetime == 1);
    assert(particles_data(particles)[0].position.x == 3);
    particles_clear(particles);
    assert(particles_count(particles) == 0);
    particles_free(particles);
    puts("particles PASS: deterministic bounded bursts, finite values, expiration and reset");
}
