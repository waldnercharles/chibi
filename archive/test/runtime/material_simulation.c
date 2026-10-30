#include "runtime/material_fluid.h"
#include "support/array.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <math.h>
#include <string.h>

static fluid_mpm_settings settings()
{
    return (fluid_mpm_settings){ .cell_size = 2, .max_substeps = 64 };
}

static fluid_particle parcel(float x, float y)
{
    return (fluid_particle){ .position = { x, y },
                             .mass = 1,
                             .radius = .5f,
                             .kind = FLUID_LIQUID,
                             .constitutive = { 1, 40, 0 } };
}

static void finite_parcel(fluid_particle p)
{
    assert(isfinite(p.position.x) && isfinite(p.position.y));
    assert(isfinite(p.velocity.x) && isfinite(p.velocity.y));
    assert(isfinite(p.deformation.volume) && p.deformation.volume > 0);
    for (int i = 0; i < 4; i++) {
        assert(isfinite(p.deformation.f[i]) && isfinite(p.deformation.affine[i]));
    }
    float determinant = p.deformation.f[0] * p.deformation.f[3] -
                        p.deformation.f[1] * p.deformation.f[2];
    assert(isfinite(determinant) && determinant > 0);
}

TEST_CASE(contained_liquid)
{
    material_fluid *fluid = material_fluid_new(3, 64);
    fluid_mpm_settings config = settings();
    config.gravity = (fluid_vec){ 0, -5 };
    material_fluid_set_solver(fluid, FLUID_SOLVER_MPM, config);
    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
            fluid_particle p = parcel(x - 3.5f, y - 3.5f);
            p.velocity = (fluid_vec){ 5, 2 };
            REQUIRE(material_fluid_add(fluid, p));
        }
    }
    fluid_body body = { 0 };
    fluid_obstacle walls[] = { { .center = { -14, 0 }, .half_size = { 3, 17 } },
                               { .center = { 14, 0 }, .half_size = { 3, 17 } },
                               { .center = { 0, -14 }, .half_size = { 17, 3 } },
                               { .center = { 0, 14 }, .half_size = { 17, 3 } } };
    fluid_region region = { -32, -32, 64, 64 };
    bool contacted = false;
    for (int tick = 0; tick < 240; tick++) {
        material_fluid_tick(fluid, 1.f / 60, 64, &region, 1, &body, 1, walls, 4);
        int count;
        const fluid_particle *p = material_fluid_particles(fluid, &count);
        REQUIRE(count == 64);
        fluid_mpm_state state = material_fluid_mpm_state(fluid);
        REQUIRE(state.advanced_time > 0 && state.advanced_time <= 1.f / 60);
        REQUIRE(state.substeps > 0 && state.substeps <= config.max_substeps);
        for (int i = 0; i < count; i++) {
            finite_parcel(p[i]);
            /* Inner wall faces are at +/-11; contacts retain parcel radius. */
            REQUIRE(fabsf(p[i].position.x) <= 10.501f);
            REQUIRE(fabsf(p[i].position.y) <= 10.501f);
            contacted |= fabsf(p[i].position.x) > 10.4f ||
                         fabsf(p[i].position.y) > 10.4f;
        }
    }
    REQUIRE(contacted);
    material_fluid_free(fluid);
    return true;
}

TEST_CASE(sleeping_deformation)
{
    material_fluid *paused = material_fluid_new(3, 25);
    material_fluid *reference = material_fluid_new(3, 25);
    material_fluid_set_solver(paused, FLUID_SOLVER_MPM, settings());
    material_fluid_set_solver(reference, FLUID_SOLVER_MPM, settings());
    for (int y = 0; y < 5; y++) {
        for (int x = 0; x < 5; x++) {
            fluid_particle p = parcel(x - 2, y - 2);
            p.constitutive.shear_modulus = 8;
            p.velocity = (fluid_vec){ p.position.x * .4f + 2, -p.position.y * .3f + 1 };
            REQUIRE(material_fluid_add(paused, p));
            REQUIRE(material_fluid_add(reference, p));
        }
    }
    fluid_region region = { -64, -64, 128, 128 };
    for (int tick = 0; tick < 30; tick++) {
        material_fluid_tick(paused, 1.f / 60, 64, &region, 1, NULL, 0, NULL, 0);
        material_fluid_tick(reference, 1.f / 60, 64, &region, 1, NULL, 0, NULL, 0);
    }
    int count;
    const fluid_particle *p = material_fluid_particles(paused, &count);
    REQUIRE(count == 25);
    fluid_particle saved[25];
    memcpy(saved, p, sizeof(saved));
    bool deformed = false;
    for (int i = 0; i < count; i++) {
        deformed |= fabsf(p[i].deformation.f[0] - 1) > .001f;
    }
    REQUIRE(deformed);
    for (int tick = 0; tick < 120; tick++) {
        material_fluid_tick(paused, 1.f / 60, 64, NULL, 0, NULL, 0, NULL, 0);
        REQUIRE(material_fluid_active_count(paused) == 0);
        p = material_fluid_particles(paused, &count);
        REQUIRE(count == 25 && !memcmp(saved, p, sizeof(saved)));
    }
    for (int tick = 0; tick < 30; tick++) {
        material_fluid_tick(paused, 1.f / 60, 64, &region, 1, NULL, 0, NULL, 0);
        material_fluid_tick(reference, 1.f / 60, 64, &region, 1, NULL, 0, NULL, 0);
        int other_count;
        p = material_fluid_particles(paused, &count);
        const fluid_particle *other = material_fluid_particles(reference, &other_count);
        REQUIRE(count == other_count && !memcmp(p, other, sizeof(saved)));
    }
    material_fluid_free(paused);
    material_fluid_free(reference);
    return true;
}

TEST_CASE(expiry_and_chunk_queries)
{
    material_fluid *fluid = material_fluid_new(2, 8);
    fluid_particle p = parcel(0, 0);
    p.lifetime = .001f;
    REQUIRE(material_fluid_add(fluid, p));
    REQUIRE(material_fluid_add(fluid, parcel(1000, 0)));
    REQUIRE(material_fluid_add(fluid, parcel(8, 0)));
    fluid_region near = { 0, 0, 16, 16 };
    material_fluid_tick(fluid, 1.f / 60, 64, &near, 1, NULL, 0, NULL, 0);
    int count, *ids = NULL;
    const fluid_particle *particles = material_fluid_particles(fluid, &count);
    REQUIRE(count == 2 && particles[0].position.x == 8 && particles[1].position.x == 1000);
    REQUIRE(material_fluid_active_count(fluid) == 1);
    material_fluid_query(fluid, near, &ids);
    REQUIRE(acount(ids) == 1 && ids[0] == 0);
    material_fluid_query(fluid, (fluid_region){ 1000, 0, 0, 0 }, &ids);
    REQUIRE(acount(ids) == 1 && ids[0] == 1);
    p = parcel(-1000, 0);
    p.velocity.x = 120;
    REQUIRE(material_fluid_add(fluid, p));
    fluid_region far = { -1000, 0, 1, 1 };
    material_fluid_query(fluid, far, &ids);
    REQUIRE(acount(ids) == 1 && ids[0] == 2);
    for (int tick = 0; tick < 32; tick++) {
        material_fluid_tick(fluid, 1.f / 60, 64, &far, 1, NULL, 0, NULL, 0);
    }
    material_fluid_query(fluid, far, &ids);
    REQUIRE(acount(ids) == 0);
    material_fluid_query(fluid, (fluid_region){ -950, 0, 0, 0 }, &ids);
    REQUIRE(acount(ids) == 1 && ids[0] == 2);
    afree(ids);
    material_fluid_free(fluid);
    return true;
}

TEST_CASE(bounded_stiff_work)
{
    material_fluid *fluid = material_fluid_new(3, 4);
    fluid_mpm_settings config = settings();
    config.max_substeps = 1;
    config.gravity = (fluid_vec){ 0, -5 };
    material_fluid_set_solver(fluid, FLUID_SOLVER_MPM, config);
    fluid_particle p = parcel(0, 0);
    p.constitutive.bulk_modulus = 100000000;
    p.constitutive.shear_modulus = 1000000;
    p.velocity.x = 10;
    REQUIRE(material_fluid_add(fluid, p));
    fluid_region region = { -64, -64, 128, 128 };
    for (int tick = 0; tick < 60; tick++) {
        material_fluid_profile profile;
        material_fluid_tick_profiled(fluid, 1.f / 60, 64, &region, 1, NULL, 0, NULL, 0, &profile);
        fluid_mpm_state state = material_fluid_mpm_state(fluid);
        REQUIRE(state.limited && state.substeps == 1);
        REQUIRE(profile.mpm_limited_steps == 1 && profile.mpm_substeps == 1);
        REQUIRE(state.advanced_time > 0 && state.advanced_time < 1.f / 60);
        int count;
        const fluid_particle *particles = material_fluid_particles(fluid, &count);
        REQUIRE(count == 1);
        finite_parcel(particles[0]);
        REQUIRE(particles[0].position.x < .1f);
    }
    material_fluid_free(fluid);
    return true;
}

TEST_CASE(separated_liquid_pressure)
{
    material_fluid *alone = material_fluid_new(6, 32);
    material_fluid *separated = material_fluid_new(6, 32);
    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
            fluid_particle p = parcel(-1.5f - x * .5f, y * .5f - .75f);
            p.radius = .25f;
            REQUIRE(material_fluid_add(alone, p));
            REQUIRE(material_fluid_add(separated, p));
        }
    }
    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
            fluid_particle p = parcel(1.5f + x * .5f, y * .5f - .75f);
            p.radius = .25f;
            REQUIRE(material_fluid_add(separated, p));
        }
    }
    fluid_region region = { -32, -32, 64, 64 };
    fluid_obstacle divider = { .half_size = { .5f, 100 } };
    for (int tick = 0; tick < 30; tick++) {
        fluid_body a = { 0 }, b = { 0 };
        material_fluid_tick(alone, 1.f / 60, 64, &region, 1, &a, 1, &divider, 1);
        material_fluid_tick(separated, 1.f / 60, 64, &region, 1, &b, 1, &divider, 1);
        int count, other_count;
        const fluid_particle *left = material_fluid_particles(alone, &count);
        const fluid_particle *both = material_fluid_particles(separated, &other_count);
        REQUIRE(count == 16 && other_count == 32);
        for (int i = 0; i < count; i++) {
            REQUIRE(
                left[i].position.x == both[i].position.x &&
                left[i].position.y == both[i].position.y
            );
            REQUIRE(
                left[i].velocity.x == both[i].velocity.x &&
                left[i].velocity.y == both[i].velocity.y
            );
        }
    }
    material_fluid_free(alone);
    material_fluid_free(separated);
    return true;
}

void test_material_simulation()
{
    RUN_TEST_CASE(contained_liquid);
    RUN_TEST_CASE(sleeping_deformation);
    RUN_TEST_CASE(expiry_and_chunk_queries);
    RUN_TEST_CASE(bounded_stiff_work);
    RUN_TEST_CASE(separated_liquid_pressure);
}
