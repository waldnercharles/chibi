#include "runtime/material_fluid.h"
#include "support/cos_sin.h"

#include <assert.h>
#include <math.h>
#include <string.h>

static fluid_particle liquid()
{
    return (fluid_particle){ .mass = 1,
                             .radius = .5f,
                             .kind = FLUID_LIQUID,
                             .constitutive = { 1, 10000, 0 } };
}

static material_fluid *world(int substeps, bool gas)
{
    material_fluid *fluid = material_fluid_new(4, 4);
    material_fluid_set_solver(
        fluid,
        FLUID_SOLVER_MPM,
        (fluid_mpm_settings){ .cell_size = 1, .max_substeps = substeps }
    );
    assert(material_fluid_add(fluid, liquid()));
    if (gas) {
        fluid_particle p = { .position = { 32, 0 }, .mass = 2, .radius = .5f, .kind = FLUID_GAS };
        assert(material_fluid_add(fluid, p));
    }
    return fluid;
}

static void tick(material_fluid *fluid, fluid_body *body, const fluid_obstacle *wall)
{
    fluid_region region = { -64, -64, 128, 128 };
    material_fluid_tick(fluid, 1.f / 60, 32, &region, 1, body, body ? 1 : 0, wall, wall ? 1 : 0);
}

static void future_wall_overlap()
{
    material_fluid *fluid = world(1, false);
    fluid_body body = { .position = { 0, 0 },
                        .previous_position = { 4, 0 },
                        .velocity = { -240, 0 },
                        .swept = true };
    fluid_obstacle wall = { .half_size = { 1, 8 } };
    tick(fluid, &body, &wall);
    fluid_mpm_state state = material_fluid_mpm_state(fluid);
    assert(state.limited && state.substeps == 1 && state.advanced_time > 0);
    /* The wall overlaps the parcel only in the frame's future. Its accepted
     * substep endpoint remains safely to the right of the parcel. */
    float wall_x = 4 * (1 - state.advanced_time * 60);
    assert(wall_x > 1.5f);
    int count;
    const fluid_particle *p = material_fluid_particles(fluid, &count);
    assert(count == 1);
    assert(fabsf(p[0].position.x) < 1e-6f && fabsf(p[0].velocity.x) < 1e-6f);
    assert(body.impulse.x == 0 && body.angular_impulse == 0);
    material_fluid_free(fluid);
}

static void unrelated_gas_preserves_contacts(int substeps)
{
    material_fluid *plain = world(substeps, false);
    material_fluid *mixed = world(substeps, true);
    fluid_body a = { .position = { 0, 0 },
                     .previous_position = { 4, 0 },
                     .velocity = { -240, 0 },
                     .swept = true },
               b = a;
    fluid_obstacle wall = { .half_size = { 1, 8 } };
    tick(plain, &a, &wall);
    tick(mixed, &b, &wall);
    int count;
    const fluid_particle *p = material_fluid_particles(plain, &count);
    const fluid_particle *q = material_fluid_particles(mixed, &count);
    assert(
        p[0].position.x == q[0].position.x && p[0].position.y == q[0].position.y
    );
    assert(
        p[0].velocity.x == q[0].velocity.x && p[0].velocity.y == q[0].velocity.y
    );
    assert(memcmp(&p[0].deformation, &q[0].deformation, sizeof p[0].deformation) == 0);
    assert(a.impulse.x == b.impulse.x && a.impulse.y == b.impulse.y);
    assert(a.angular_impulse == b.angular_impulse);
    fluid_mpm_state state = material_fluid_mpm_state(plain);
    fluid_mpm_state other = material_fluid_mpm_state(mixed);
    assert(state.advanced_time == other.advanced_time && state.limited == other.limited);
    if (substeps > 1) {
        assert(!state.limited && fabsf(p[0].velocity.x) > 1);
    }
    material_fluid_free(plain);
    material_fluid_free(mixed);
}

static void mixed_momentum()
{
    material_fluid *fluid = world(1, false);
    fluid_particle gas = { .position = { 1, 0 }, .mass = 3, .radius = .5f, .kind = FLUID_GAS };
    assert(material_fluid_add(fluid, gas));
    tick(fluid, NULL, NULL);
    int count;
    const fluid_particle *p = material_fluid_particles(fluid, &count);
    assert(count == 2 && material_fluid_mpm_state(fluid).limited);
    assert(p[0].velocity.x < 0 && p[1].velocity.x > 0);
    double momentum = (double)p[0].mass * p[0].velocity.x +
                      (double)p[1].mass * p[1].velocity.x;
    assert(fabs(momentum) < 1e-5);
    /* Coupling supplies an impulse; it must not move the liquid beyond its
     * already accepted mechanical step on a saturated frame. */
    assert(fabsf(p[0].position.x) < 1e-6f);
    material_fluid_free(fluid);
}

static void no_active_liquid()
{
    material_fluid *fluid = material_fluid_new(4, 4);
    material_fluid_set_solver(
        fluid,
        FLUID_SOLVER_MPM,
        (fluid_mpm_settings){ .cell_size = 1, .max_substeps = 1, .gravity = { 0, 4096 } }
    );
    tick(fluid, NULL, NULL);
    fluid_mpm_state state = material_fluid_mpm_state(fluid);
    assert(state.substeps == 0 && state.nodes == 0 && !state.limited);
    assert(state.advanced_time == 1.f / 60);

    assert(material_fluid_add(fluid, liquid()));
    tick(fluid, NULL, NULL);
    state = material_fluid_mpm_state(fluid);
    assert(state.substeps == 1 && state.nodes > 0 && state.limited);
    int count;
    fluid_particle sleeping = material_fluid_particles(fluid, &count)[0];
    fluid_particle gas = { .position = { 96, 8 },
                           .velocity = { 3, 0 },
                           .mass = 1,
                           .radius = .5f,
                           .kind = FLUID_GAS };
    assert(material_fluid_add(fluid, gas));
    fluid_region region = { 64, 0, 64, 32 };
    material_fluid_tick(fluid, 1.f / 60, 32, &region, 1, NULL, 0, NULL, 0);
    state = material_fluid_mpm_state(fluid);
    const fluid_particle *p = material_fluid_particles(fluid, &count);
    assert(count == 2 && material_fluid_active_count(fluid) == 1);
    assert(state.substeps == 0 && state.nodes == 0 && !state.limited);
    assert(state.advanced_time == 1.f / 60);
    assert(!memcmp(&sleeping, &p[0], sizeof sleeping));
    assert(fabsf(p[1].position.x - (96 + 3.f / 60)) < 1e-5f);

    fluid_particle saved[2];
    memcpy(saved, p, sizeof saved);
    material_fluid_tick(fluid, 1.f / 60, 32, NULL, 0, NULL, 0, NULL, 0);
    state = material_fluid_mpm_state(fluid);
    p = material_fluid_particles(fluid, &count);
    assert(material_fluid_active_count(fluid) == 0);
    assert(state.substeps == 0 && state.nodes == 0 && !state.limited);
    assert(state.advanced_time == 1.f / 60 && !memcmp(saved, p, sizeof saved));
    material_fluid_free(fluid);
}

static void collider_order_closure(int solver)
{
    material_fluid *fluid = material_fluid_new(4, 4);
    material_fluid_set_solver(fluid, solver, (fluid_mpm_settings){ .cell_size = 1, .max_substeps = 64 });
    fluid_particle p = liquid();
    p.position.y = -.3f;
    assert(material_fluid_add(fluid, p));
    fluid_body bodies[2] = { { .position = { 0, -1 } },
                             { .position = { 0, -.1f }, .angle = .5f, .inverse_mass = 1 } };
    fluid_obstacle obstacles[2] = { { .body = 0, .half_size = { 8, .5f } },
                                    { .body = 1, .half_size = { .8f, .1f } } };
    fluid_region region = { -32, -32, 64, 64 };
    for (int step = 0; step < 10; step++) {
        material_fluid_tick(fluid, 1.f / 60, 32, &region, 1, bodies, 2, obstacles, 2);
        int count;
        const fluid_particle *particles = material_fluid_particles(fluid, &count);
        assert(count == 1);
        /* The rotated released fragment is processed after the ceiling and
         * pushes upward. Final clearance must still include the parcel skin. */
        assert(particles[0].position.y >= -.5f + p.radius * .3f - 1e-6f);
        assert(isfinite(particles[0].velocity.y));
    }
    material_fluid_free(fluid);
}

static void rotated_static_corner()
{
    material_fluid *fluid = material_fluid_new(4, 4);
    cos_sin rotation = cos_sin_compute(.785398163f);
    fluid_particle p = liquid();
    float skin = p.radius * .3f;
    fluid_vec corner = { 1 + .8f * skin, -.2f - .8f * skin };
    fluid_vec target = { rotation.c * corner.x - rotation.s * corner.y,
                         rotation.s * corner.x + rotation.c * corner.y };
    p.position = (fluid_vec){ target.x + .06f, target.y };
    assert(material_fluid_add(fluid, p));
    fluid_body bodies[2] = { { .angle = .785398163f },
                             { .position = { p.position.x + .1f, p.position.y },
                               .inverse_mass = 1 } };
    fluid_obstacle obstacles[2] = { { .body = 0, .half_size = { 1, .2f } },
                                    { .body = 1, .half_size = { .01f, 1 } } };
    fluid_region region = { -32, -32, 64, 64 };
    material_fluid_tick(fluid, 1.f / 60, 32, &region, 1, bodies, 2, obstacles, 2);
    int count;
    const fluid_particle *particles = material_fluid_particles(fluid, &count);
    fluid_vec q = particles[0].position;
    fluid_vec local = { rotation.c * q.x + rotation.s * q.y,
                        -rotation.s * q.x + rotation.c * q.y };
    /* This corner's skin-expanded local box extends beyond the unexpanded
     * world AABB plus one skin. Broad-phase closure must still find it. */
    assert(fabsf(local.x) >= 1 + skin - 1e-6f || fabsf(local.y) >= .2f + skin - 1e-6f);
    material_fluid_free(fluid);
}

void test_material_mixed()
{
    future_wall_overlap();
    unrelated_gas_preserves_contacts(1);
    unrelated_gas_preserves_contacts(64);
    mixed_momentum();
    no_active_liquid();
    collider_order_closure(FLUID_SOLVER_DDR);
    collider_order_closure(FLUID_SOLVER_MPM);
    rotated_static_corner();
}
