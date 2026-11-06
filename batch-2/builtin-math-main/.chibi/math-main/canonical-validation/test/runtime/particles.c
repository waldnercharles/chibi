#include "runtime/particles.h"

#include "third_party/pico_headers/pico_unit.h"

#include <float.h>
#include <limits.h>
#include <math.h>

/* Particle bursts: deterministic bounded emission, finite values, expiration
 * and reset, without a renderer. */

static particles *particle_set, *reference;

static void cleanup()
{
    if (particle_set) {
        particles_free(particle_set);
    }
    if (reference) {
        particles_free(reference);
    }
    particle_set = reference = NULL;
}

static bool equal_particle(const particle *a, const particle *b)
{
    return a->position.x == b->position.x && a->position.y == b->position.y &&
           a->velocity.x == b->velocity.x && a->velocity.y == b->velocity.y &&
           a->color == b->color && a->age == b->age && a->lifetime == b->lifetime;
}

static const particle_burst burst = { .position = { 3, -4 },
                                      .color = 0xff8844ffu,
                                      .count = 1,
                                      .speed = 20,
                                      .lifetime = 1 };

/* Rejected bursts and ticks leave the one live particle as it was. */
static bool rejects_invalid_inputs()
{
    particle original = particles_data(particle_set)[0];
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
        REQUIRE(particles_emit(particle_set, &invalid[i]) == -1);
        REQUIRE(particles_count(particle_set) == 1);
        REQUIRE(equal_particle(&original, particles_data(particle_set)));
    }
    REQUIRE(particles_tick(particle_set, NAN) == -1);
    REQUIRE(particles_tick(particle_set, INFINITY) == -1);
    REQUIRE(particles_tick(particle_set, -1) == -1);
    REQUIRE(particles_tick(particle_set, 0) == 0);
    REQUIRE(equal_particle(&original, particles_data(particle_set)));
    return true;
}

TEST_CASE(particle_curves_and_steps_are_frame_rate_independent)
{
    REQUIRE(fabsf(particle_curve_value(4, 0, 2, .5f) - 3) < .00001f);
    REQUIRE(particle_curve_value(4, 0, 0, .8f) == 4);
    REQUIRE(particle_curve_value(4, 2, 1, 1) == 2);
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
    REQUIRE(fabsf(curved.velocity.x - 100 * expf(-3)) < .00001f);
    REQUIRE(fabsf(curved.position.x - split.position.x) < .0001f);

    particle_set = particles_new();
    reference = particles_new();
    particle spatial = { .position = { 3, 4 },
                         .elevation = 8,
                         .velocity = { 20, -10 },
                         .elevation_velocity = 5,
                         .size = 2,
                         .size_exponent = 1,
                         .lifetime = 1,
                         .drag = 3,
                         .shape = PARTICLE_TRIANGLE };
    REQUIRE(particles_emit_particle(particle_set, &spatial) == 1);
    REQUIRE(particles_emit_particle(reference, &spatial) == 1);
    REQUIRE(particles_tick(particle_set, .5) == 0);
    for (int i = 0; i < 10; i++) {
        REQUIRE(particles_tick(reference, .05) == 0);
    }
    const particle *a = particles_data(particle_set), *b = particles_data(reference);
    REQUIRE(fabsf(a->position.x - b->position.x) < .00001f);
    REQUIRE(fabsf(a->position.y - b->position.y) < .00001f);
    REQUIRE(fabsf(a->elevation - b->elevation) < .00001f);
    REQUIRE(a->shape == PARTICLE_TRIANGLE);
    spatial.shape = PARTICLE_SHAPE_COUNT;
    REQUIRE(particles_emit_particle(particle_set, &spatial) == -1);
    REQUIRE(particles_count(particle_set) == 1);
    return true;
}

TEST_CASE(particle_bursts_move_expire_and_reset_deterministically)
{
    particle_set = particles_new();
    REQUIRE(particles_count(particle_set) == 0);
    REQUIRE(particles_tick(particle_set, 1) == 0);
    REQUIRE(particles_emit(particle_set, &burst) == 1);
    particle first = particles_data(particle_set)[0];
    REQUIRE(
        first.position.x == 3 && first.position.y == -4 && first.color == burst.color
    );
    REQUIRE(first.age == 0 && first.lifetime == 1);
    REQUIRE(rejects_invalid_inputs());
    REQUIRE(particles_tick(particle_set, 0.25) == 0);
    const particle *moved = particles_data(particle_set);
    REQUIRE(moved->age == 0.25f);
    REQUIRE(fabsf(moved->position.x - (3 + first.velocity.x * 0.25f)) < 0.00001f);
    REQUIRE(fabsf(moved->position.y - (-4 + first.velocity.y * 0.25f)) < 0.00001f);
    REQUIRE(particles_tick(particle_set, 0.75) == 0);
    REQUIRE(particles_count(particle_set) == 0);
    particles_clear(particle_set);
    REQUIRE(particles_emit(particle_set, &burst) == 1);
    REQUIRE(equal_particle(&first, particles_data(particle_set)));

    /* Rejected input does not advance the random sequence. */
    reference = particles_new();
    REQUIRE(particles_emit(reference, &burst) == 1);
    REQUIRE(rejects_invalid_inputs());
    REQUIRE(particles_emit(particle_set, &burst) == 1);
    REQUIRE(particles_emit(reference, &burst) == 1);
    REQUIRE(equal_particle(&particles_data(particle_set)[1], &particles_data(reference)[1]));
    return true;
}

TEST_CASE(particle_bursts_stay_bounded_and_finite)
{
    particle_set = particles_new();
    particle_burst many = burst;
    many.count = 0;
    REQUIRE(particles_emit(particle_set, &many) == 0);
    REQUIRE(particles_count(particle_set) == 0);
    many.count = INT_MAX;
    REQUIRE(particles_emit(particle_set, &many) == PARTICLE_LIMIT);
    REQUIRE(particles_count(particle_set) == PARTICLE_LIMIT);
    REQUIRE(particles_emit(particle_set, &many) == 0);
    const particle *items = particles_data(particle_set);
    for (int i = 0; i < particles_count(particle_set); i++) {
        REQUIRE(isfinite(items[i].position.x) && isfinite(items[i].position.y));
        REQUIRE(isfinite(items[i].velocity.x) && isfinite(items[i].velocity.y));
        REQUIRE(hypotf(items[i].velocity.x, items[i].velocity.y) <= many.speed * 1.00001f);
    }
    REQUIRE(particles_tick(particle_set, DBL_MAX) == 0);
    REQUIRE(particles_count(particle_set) == 0);

    /* Each particle expires at its own lifetime. */
    many.count = 1;
    many.speed = 0;
    many.lifetime = 0.5f;
    REQUIRE(particles_emit(particle_set, &many) == 1);
    many.lifetime = 1;
    REQUIRE(particles_emit(particle_set, &many) == 1);
    REQUIRE(particles_tick(particle_set, 0.5) == 0);
    REQUIRE(particles_count(particle_set) == 1);
    REQUIRE(particles_data(particle_set)[0].lifetime == 1);
    REQUIRE(particles_data(particle_set)[0].position.x == 3);
    particles_clear(particle_set);
    REQUIRE(particles_count(particle_set) == 0);
    return true;
}

void test_particles()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(particle_curves_and_steps_are_frame_rate_independent);
    RUN_TEST_CASE(particle_bursts_move_expire_and_reset_deterministically);
    RUN_TEST_CASE(particle_bursts_stay_bounded_and_finite);
    pu_clear_setup();
}
