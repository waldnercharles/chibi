#include "runtime/particles.h"

#include "third_party/pico_headers/pico_unit.h"

#include <float.h>
#include <limits.h>
#include <math.h>

/* Particle bursts: deterministic bounded emission, finite values, expiration
 * and reset, without a renderer. */

static game_particles *particles, *reference;

static void cleanup()
{
    if (particles) {
        game_particles_free(particles);
    }
    if (reference) {
        game_particles_free(reference);
    }
    particles = reference = NULL;
}

static bool equal_particle(const game_particle *a, const game_particle *b)
{
    return a->position.x == b->position.x && a->position.y == b->position.y &&
           a->velocity.x == b->velocity.x && a->velocity.y == b->velocity.y &&
           a->color == b->color && a->age == b->age && a->lifetime == b->lifetime;
}

static const game_particle_burst burst = { .position = { 3, -4 },
                                           .color = 0xff8844ffu,
                                           .count = 1,
                                           .speed = 20,
                                           .lifetime = 1 };

/* Rejected bursts and ticks leave the one live particle as it was. */
static bool rejects_invalid_inputs()
{
    game_particle original = game_particles_data(particles)[0];
    game_particle_burst invalid[] = { burst, burst, burst, burst,
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
        REQUIRE(game_particles_emit(particles, &invalid[i]) == -1);
        REQUIRE(game_particles_count(particles) == 1);
        REQUIRE(equal_particle(&original, game_particles_data(particles)));
    }
    REQUIRE(game_particles_tick(particles, NAN) == -1);
    REQUIRE(game_particles_tick(particles, INFINITY) == -1);
    REQUIRE(game_particles_tick(particles, -1) == -1);
    REQUIRE(game_particles_tick(particles, 0) == 0);
    REQUIRE(equal_particle(&original, game_particles_data(particles)));
    return true;
}

TEST_CASE(particle_curves_and_steps_are_frame_rate_independent)
{
    REQUIRE(fabsf(game_particle_curve_value(4, 0, 2, .5f) - 3) < .00001f);
    REQUIRE(game_particle_curve_value(4, 0, 0, .8f) == 4);
    REQUIRE(game_particle_curve_value(4, 2, 1, 1) == 2);
    game_particle curved = { .lifetime = 2,
                             .velocity = { 100, 0 },
                             .drag = 2,
                             .drag_end = 6,
                             .drag_exponent = 1 };
    game_particle split = curved;
    game_particle_advance(&curved, 1);
    for (int i = 0; i < 60; i++) {
        game_particle_advance(&split, 1.0 / 60);
    }
    REQUIRE(fabsf(curved.velocity.x - 100 * expf(-3)) < .00001f);
    REQUIRE(fabsf(curved.position.x - split.position.x) < .0001f);

    particles = game_particles_new();
    reference = game_particles_new();
    game_particle spatial = { .position = { 3, 4 },
                              .elevation = 8,
                              .velocity = { 20, -10 },
                              .elevation_velocity = 5,
                              .size = 2,
                              .size_exponent = 1,
                              .lifetime = 1,
                              .drag = 3,
                              .shape = GAME_PARTICLE_TRIANGLE };
    REQUIRE(game_particles_emit_particle(particles, &spatial) == 1);
    REQUIRE(game_particles_emit_particle(reference, &spatial) == 1);
    REQUIRE(game_particles_tick(particles, .5) == 0);
    for (int i = 0; i < 10; i++) {
        REQUIRE(game_particles_tick(reference, .05) == 0);
    }
    const game_particle *a = game_particles_data(particles),
                        *b = game_particles_data(reference);
    REQUIRE(fabsf(a->position.x - b->position.x) < .00001f);
    REQUIRE(fabsf(a->position.y - b->position.y) < .00001f);
    REQUIRE(fabsf(a->elevation - b->elevation) < .00001f);
    REQUIRE(a->shape == GAME_PARTICLE_TRIANGLE);
    spatial.shape = GAME_PARTICLE_SHAPE_COUNT;
    REQUIRE(game_particles_emit_particle(particles, &spatial) == -1);
    REQUIRE(game_particles_count(particles) == 1);
    return true;
}

TEST_CASE(particle_bursts_move_expire_and_reset_deterministically)
{
    particles = game_particles_new();
    REQUIRE(game_particles_count(particles) == 0);
    REQUIRE(game_particles_tick(particles, 1) == 0);
    REQUIRE(game_particles_emit(particles, &burst) == 1);
    game_particle first = game_particles_data(particles)[0];
    REQUIRE(
        first.position.x == 3 && first.position.y == -4 && first.color == burst.color
    );
    REQUIRE(first.age == 0 && first.lifetime == 1);
    REQUIRE(rejects_invalid_inputs());
    REQUIRE(game_particles_tick(particles, 0.25) == 0);
    const game_particle *moved = game_particles_data(particles);
    REQUIRE(moved->age == 0.25f);
    REQUIRE(fabsf(moved->position.x - (3 + first.velocity.x * 0.25f)) < 0.00001f);
    REQUIRE(fabsf(moved->position.y - (-4 + first.velocity.y * 0.25f)) < 0.00001f);
    REQUIRE(game_particles_tick(particles, 0.75) == 0);
    REQUIRE(game_particles_count(particles) == 0);
    game_particles_clear(particles);
    REQUIRE(game_particles_emit(particles, &burst) == 1);
    REQUIRE(equal_particle(&first, game_particles_data(particles)));

    /* Rejected input does not advance the random sequence. */
    reference = game_particles_new();
    REQUIRE(game_particles_emit(reference, &burst) == 1);
    REQUIRE(rejects_invalid_inputs());
    REQUIRE(game_particles_emit(particles, &burst) == 1);
    REQUIRE(game_particles_emit(reference, &burst) == 1);
    REQUIRE(
        equal_particle(&game_particles_data(particles)[1], &game_particles_data(reference)[1])
    );
    return true;
}

TEST_CASE(particle_bursts_stay_bounded_and_finite)
{
    particles = game_particles_new();
    game_particle_burst many = burst;
    many.count = 0;
    REQUIRE(game_particles_emit(particles, &many) == 0);
    REQUIRE(game_particles_count(particles) == 0);
    many.count = INT_MAX;
    REQUIRE(game_particles_emit(particles, &many) == GAME_PARTICLE_LIMIT);
    REQUIRE(game_particles_count(particles) == GAME_PARTICLE_LIMIT);
    REQUIRE(game_particles_emit(particles, &many) == 0);
    const game_particle *items = game_particles_data(particles);
    for (int i = 0; i < game_particles_count(particles); i++) {
        REQUIRE(isfinite(items[i].position.x) && isfinite(items[i].position.y));
        REQUIRE(isfinite(items[i].velocity.x) && isfinite(items[i].velocity.y));
        REQUIRE(hypotf(items[i].velocity.x, items[i].velocity.y) <= many.speed * 1.00001f);
    }
    REQUIRE(game_particles_tick(particles, DBL_MAX) == 0);
    REQUIRE(game_particles_count(particles) == 0);

    /* Each particle expires at its own lifetime. */
    many.count = 1;
    many.speed = 0;
    many.lifetime = 0.5f;
    REQUIRE(game_particles_emit(particles, &many) == 1);
    many.lifetime = 1;
    REQUIRE(game_particles_emit(particles, &many) == 1);
    REQUIRE(game_particles_tick(particles, 0.5) == 0);
    REQUIRE(game_particles_count(particles) == 1);
    REQUIRE(game_particles_data(particles)[0].lifetime == 1);
    REQUIRE(game_particles_data(particles)[0].position.x == 3);
    game_particles_clear(particles);
    REQUIRE(game_particles_count(particles) == 0);
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
