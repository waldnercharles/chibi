#include "../game/particles.h"

#include <assert.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>

static void equal_particle(const game_particle *a, const game_particle *b)
{
    assert(a->position.x == b->position.x && a->position.y == b->position.y);
    assert(a->velocity.x == b->velocity.x && a->velocity.y == b->velocity.y);
    assert(a->color == b->color && a->age == b->age && a->lifetime == b->lifetime);
}

static void invalid_inputs(game_particles *particles, game_particle_burst burst)
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
        assert(game_particles_emit(particles, &invalid[i]) == -1);
        assert(game_particles_count(particles) == 1);
        equal_particle(&original, game_particles_data(particles));
    }
    assert(game_particles_tick(particles, NAN) == -1);
    assert(game_particles_tick(particles, INFINITY) == -1);
    assert(game_particles_tick(particles, -1) == -1);
    assert(game_particles_tick(particles, 0) == 0);
    equal_particle(&original, game_particles_data(particles));
}

void test_particles()
{
    game_particles *particles = game_particles_new();
    assert(game_particles_count(particles) == 0);
    assert(game_particles_tick(particles, 1) == 0);
    game_particle_burst burst = { .position = { 3, -4 },
                                  .color = 0xff8844ffu,
                                  .count = 1,
                                  .speed = 20,
                                  .lifetime = 1 };
    assert(game_particles_emit(particles, &burst) == 1);
    game_particle first = game_particles_data(particles)[0];
    assert(
        first.position.x == 3 && first.position.y == -4 && first.color == burst.color
    );
    assert(first.age == 0 && first.lifetime == 1);
    invalid_inputs(particles, burst);
    assert(game_particles_tick(particles, 0.25) == 0);
    const game_particle *moved = game_particles_data(particles);
    assert(moved->age == 0.25f);
    assert(fabsf(moved->position.x - (3 + first.velocity.x * 0.25f)) < 0.00001f);
    assert(fabsf(moved->position.y - (-4 + first.velocity.y * 0.25f)) < 0.00001f);
    assert(game_particles_tick(particles, 0.75) == 0);
    assert(game_particles_count(particles) == 0);
    game_particles_clear(particles);
    assert(game_particles_emit(particles, &burst) == 1);
    equal_particle(&first, game_particles_data(particles));

    game_particles *reference = game_particles_new();
    assert(game_particles_emit(reference, &burst) == 1);
    invalid_inputs(particles, burst);
    assert(game_particles_emit(particles, &burst) == 1);
    assert(game_particles_emit(reference, &burst) == 1);
    equal_particle(&game_particles_data(particles)[1], &game_particles_data(reference)[1]);
    game_particles_free(reference);

    game_particles_clear(particles);
    burst.count = 0;
    assert(game_particles_emit(particles, &burst) == 0);
    assert(game_particles_count(particles) == 0);
    burst.count = INT_MAX;
    assert(game_particles_emit(particles, &burst) == GAME_PARTICLE_LIMIT);
    assert(game_particles_count(particles) == GAME_PARTICLE_LIMIT);
    assert(game_particles_emit(particles, &burst) == 0);
    const game_particle *items = game_particles_data(particles);
    for (int i = 0; i < game_particles_count(particles); i++) {
        assert(isfinite(items[i].position.x) && isfinite(items[i].position.y));
        assert(isfinite(items[i].velocity.x) && isfinite(items[i].velocity.y));
        assert(hypotf(items[i].velocity.x, items[i].velocity.y) <= burst.speed * 1.00001f);
    }
    assert(game_particles_tick(particles, DBL_MAX) == 0);
    assert(game_particles_count(particles) == 0);
    burst.count = 1;
    burst.speed = 0;
    burst.lifetime = 0.5f;
    assert(game_particles_emit(particles, &burst) == 1);
    burst.lifetime = 1;
    assert(game_particles_emit(particles, &burst) == 1);
    assert(game_particles_tick(particles, 0.5) == 0);
    assert(game_particles_count(particles) == 1);
    assert(game_particles_data(particles)[0].lifetime == 1);
    assert(game_particles_data(particles)[0].position.x == 3);
    game_particles_clear(particles);
    assert(game_particles_count(particles) == 0);
    game_particles_free(particles);
    puts("particles PASS: deterministic bounded bursts, finite values, expiration and reset");
}
