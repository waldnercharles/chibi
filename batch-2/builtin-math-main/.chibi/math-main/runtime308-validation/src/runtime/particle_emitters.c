#include "runtime/particle_emitters.h"

#include "runtime/component_catalog.h"
#include "runtime/engine.h"
#include "runtime/visual_transform.h"
#include "support/cos_sin.h"
#include "support/xalloc.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
    EMITTER_LIMIT = 512
};
typedef struct
{
    ecs_entity entity;
    CF_V3 position, direction;
    double time;
    double phase;
    uint32_t random;
    bool seen;
} emitter_source;

struct particle_emitters
{
    const ecs_world *world;
    double time, simulation_time;
    particles *particles;
    materials *materials;
    bool borrowed;
    emitter_source sources[EMITTER_LIMIT];
    particle heads[EMITTER_LIMIT];
    int head_count;
    bool reported_capacity;
};

particle_emitters *particle_emitters_new(particles *destination)
{
    particle_emitters *emitters = xcalloc(1, sizeof *emitters);
    emitters->borrowed = destination != NULL;
    emitters->particles = destination ? destination : particles_new();
    return emitters;
}

void particle_emitters_bind_materials(particle_emitters *emitters, materials *materials)
{
    emitters->materials = materials;
}

void particle_emitters_reset(particle_emitters *emitters)
{
    particles *particles = emitters->particles;
    bool borrowed = emitters->borrowed;
    materials *materials = emitters->materials;
    if (!borrowed) {
        particles_clear(particles);
    }
    memset(emitters, 0, sizeof *emitters);
    emitters->particles = particles;
    emitters->borrowed = borrowed;
    emitters->materials = materials;
}

void particle_emitters_free(particle_emitters *emitters)
{
    if (!emitters->borrowed) {
        particles_free(emitters->particles);
    }
    free(emitters);
}

static bool valid_range(CF_V2 range)
{
    return isfinite(range.x) && isfinite(range.y) && range.x >= 0 &&
           range.y >= range.x;
}

static bool valid_curve(ParticleCurve curve)
{
    return valid_range(curve.start) && valid_range(curve.end) &&
           isfinite(curve.exponent) && curve.exponent >= 0;
}

static bool valid(const ParticleEmitter *style)
{
    return (!style->fluid ||
            ((style->fluid_kind == FLUID_GAS || style->fluid_kind == FLUID_LIQUID) &&
             isfinite(style->fluid_mass) && style->fluid_mass > 0)) &&
           style->shape >= 0 && style->shape < PARTICLE_SHAPE_COUNT &&
           valid_range(style->lifetime) && style->lifetime.x > 0 &&
           valid_range(style->speed) && valid_curve(style->size) &&
           style->size.start.x > 0 && valid_curve(style->drag) &&
           valid_curve(style->emission) && valid_curve(style->antialias) &&
           valid_range(style->stretch) &&
           isfinite(
               style->rate + style->spread + style->velocity_inherit +
               style->absorption + style->metaball_blend + style->offset.x +
               style->offset.y + style->offset.z + style->direction.x +
               style->direction.y + style->direction.z
           ) &&
           style->burst_count >= 0 && style->burst_count <= PARTICLE_LIMIT &&
           style->rate >= 0 && style->spread >= 0 &&
           style->spread <= 6.2831853072f && style->velocity_inherit >= 0 &&
           style->absorption >= 0 && style->metaball_blend >= 0 &&
           style->metaball_group >= 0 && style->metaball_group <= 4 &&
           cf_dot_v3(style->direction, style->direction) > 0;
}

static emitter_source *source_for(particle_emitters *emitters, ecs_entity entity)
{
    emitter_source *empty = NULL;
    for (int i = 0; i < EMITTER_LIMIT; i++) {
        emitter_source *source = &emitters->sources[i];
        if (ecs_entity_to_u64(source->entity) == ecs_entity_to_u64(entity)) {
            return source;
        }
        if (!source->entity.slot && !empty) {
            empty = source;
        }
    }
    if (empty) {
        *empty = (emitter_source){ .entity = entity,
                                   .random = UINT32_C(0x9e3779b9) ^
                                             (uint32_t)ecs_entity_to_u64(entity),
                                   .time = -1 };
        if (!empty->random) {
            empty->random = 1;
        }
    }
    return empty;
}

static float random_unit(emitter_source *source)
{
    uint32_t value = source->random;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    source->random = value;
    return (float)(value >> 8) / 16777216;
}

static CF_V3 cone_direction(emitter_source *source, CF_V3 direction, float spread)
{
    CF_V3 axis = fabsf(direction.y) < .9f ? (CF_V3){ 0, 1, 0 } : (CF_V3){ 1, 0, 0 };
    CF_V3 right = cf_norm_v3(cf_cross_v3(direction, axis));
    CF_V3 up = cf_cross_v3(right, direction);
    float angle = random_unit(source) * spread * .5f;
    float azimuth = random_unit(source) * 6.2831853072f;
    cos_sin angle_cs = cos_sin_compute(angle);
    cos_sin azimuth_cs = cos_sin_compute(azimuth);
    return cf_add_v3(
        cf_mul_v3_f(direction, angle_cs.c),
        cf_mul_v3_f(
            cf_add_v3(
                cf_mul_v3_f(right, azimuth_cs.c),
                cf_mul_v3_f(up, azimuth_cs.s)
            ),
            angle_cs.s
        )
    );
}

static float sample(emitter_source *source, CF_V2 range)
{
    return range.x == range.y ? range.x
                              : range.x + (range.y - range.x) * random_unit(source);
}

static particle particle_at(
    emitter_source *source,
    const ParticleEmitter *style,
    CF_V3 position,
    CF_V3 velocity,
    float angle
)
{
    return (particle){ .position = { position.x, position.z },
                       .velocity = { velocity.x, velocity.z },
                       .elevation = position.y,
                       .elevation_velocity = velocity.y,
                       .angle = style->velocity_align
                                  ? cos_sin_atan2(velocity.z, velocity.x)
                                  : angle,
                       .shape = style->shape,
                       .color = style->color,
                       .end_color = style->end_color,
                       .lifetime = sample(source, style->lifetime),
                       .size = sample(source, style->size.start),
                       .size_end = sample(source, style->size.end),
                       .size_exponent = style->size.exponent,
                       .drag = sample(source, style->drag.start),
                       .drag_end = sample(source, style->drag.end),
                       .drag_exponent = style->drag.exponent,
                       .emission = sample(source, style->emission.start),
                       .emission_end = sample(source, style->emission.end),
                       .emission_exponent = style->emission.exponent,
                       .antialias = sample(source, style->antialias.start),
                       .antialias_end = sample(source, style->antialias.end),
                       .antialias_exponent = style->antialias.exponent,
                       .absorption = style->absorption,
                       .metaball_group = style->metaball_group,
                       .metaball_blend = style->metaball_blend,
                       .velocity_align = style->velocity_align,
                       .stretch = sample(source, style->stretch) };
}

static bool emit_source(
    particle_emitters *emitters,
    emitter_source *source,
    const ParticleEmitter *style,
    CF_V3 position,
    CF_V3 direction,
    CF_V3 inherited_velocity,
    double time
)
{
    source->seen = true;
    bool first = source->time < 0;
    if (first) {
        source->position = position;
        source->direction = direction;
        source->time = time;
    }
    double delta = time - source->time;
    double births = delta * style->rate + source->phase;
    int burst = first ? style->burst_count : 0;
    int count = (int)fmin(floor(births) + burst, PARTICLE_LIMIT);
    source->phase = births - floor(births);
    bool turned = cf_dot_v3(source->direction, direction) < .8660254f;
    /* Chronological births keep random samples identical across display rates. */
    for (int i = count - 1; i >= 0; i--) {
        float age = i < burst || style->rate == 0
                      ? 0
                      : (float)((source->phase + i - burst) / style->rate);
        if (age >= style->lifetime.y) {
            continue;
        }
        /* A chord between sharply rotated nozzles can pass through the source hull. */
        float fraction = !turned && delta > 0 ? (float)fmax(0, 1 - age / delta) : 1;
        CF_V3 origin = cf_lerp_v3(source->position, position, fraction);
        CF_V3 forward = cf_lerp_v3(source->direction, direction, fraction);
        forward = cf_dot_v3(forward, forward) > .00001f ? cf_norm_v3(forward) : direction;
        CF_V3 direction;
        if (style->planar) {
            float angle = cos_sin_atan2(forward.z, forward.x) +
                          (random_unit(source) - .5f) * style->spread;
            cos_sin direction_cs = cos_sin_compute(angle);
            direction = (CF_V3){ direction_cs.c, 0, direction_cs.s };
        } else {
            direction = cone_direction(source, forward, style->spread);
        }
        CF_V3 velocity = cf_add_v3(cf_mul_v3_f(direction, sample(source, style->speed)), inherited_velocity);
        particle particle = particle_at(
            source,
            style,
            origin,
            velocity,
            cos_sin_atan2(forward.z, forward.x)
        );
        if (age >= particle.lifetime) {
            continue;
        }
        particle_advance(&particle, age);
        int emitted;
        if (style->fluid && emitters->materials) {
            const Igniter *igniter = Igniter_get_const(emitters->world, source->entity);
            const Fuel *fuel = Fuel_get_const(emitters->world, source->entity);
            const MaterialThermal
                *thermal = MaterialThermal_get_const(emitters->world, source->entity);
            const MaterialConstitutive
                *model = MaterialConstitutive_get_const(emitters->world, source->entity);
            emitted = materials_emit(
                emitters->materials,
                (fluid_particle){
                    .position = { particle.position.x, particle.position.y },
                    .velocity = { particle.velocity.x, particle.velocity.y },
                    .mass = style->fluid_mass,
                    .radius = fmaxf(.5f, particle.size),
                    .color = particle.color,
                    .kind = style->fluid_kind,
                    .heat = igniter ? igniter->heat : 0,
                    .fuel = fuel ? fuel->amount : 0,
                    .thermal = thermal ? (fluid_thermal){ thermal->capacity,
                                                          thermal->conductivity }
                                       : (fluid_thermal){ 0 },
                    .constitutive = model ? (fluid_constitutive){ model->density,
                                                                  model->bulk_modulus,
                                                                  model->shear_modulus }
                                          : (fluid_constitutive){ 0 },
                    .absorption = particle.absorption,
                    .emission = particle.emission,
                    .lifetime = particle.lifetime - particle.age,
                    .style = { .duration = particle.lifetime,
                               .size_end = particle.size_end,
                               .size_exponent = particle.size_exponent,
                               .emission_end = particle.emission_end,
                               .emission_exponent = particle.emission_exponent,
                               .drag = particle.drag,
                               .drag_end = particle.drag_end,
                               .drag_exponent = particle.drag_exponent,
                               .blend = particle.metaball_blend,
                               .end_color = particle.end_color } }
            );
        } else {
            emitted = particles_emit_particle(emitters->particles, &particle);
        }
        if (emitted < 0) {
            return false;
        }
        if (!emitted) {
            break;
        }
    }
    if (style->rate == 0) {
        return true;
    }
    emitter_source head_source = *source;
    particle head = particle_at(
        &head_source,
        style,
        position,
        (CF_V3){ 0 },
        cos_sin_atan2(direction.z, direction.x)
    );
    head.size *= .65f;
    if (!style->fluid) {
        emitters->heads[emitters->head_count++] = head;
    }
    if (delta > 0) {
        source->position = position;
        source->direction = direction;
        source->time = time;
    }
    return true;
}

bool particle_emitters_collect(
    particle_emitters *emitters,
    ecs_world *world,
    const engine *runtime,
    double time,
    char *error,
    size_t error_size
)
{
    if (!isfinite(time) || time < 0) {
        snprintf(error, error_size, "Particle emitters need finite nonnegative simulation time.");
        return false;
    }
    double simulation_time = time;
    if (runtime) {
        const engine_frame_timing *timing = engine_last_timing(runtime);
        simulation_time = timing->simulation_time;
        time = fmax(0, simulation_time - (1 - timing->alpha) * ENGINE_FIXED_SECONDS);
    }
    if (emitters->world != world || simulation_time < emitters->simulation_time) {
        particle_emitters_reset(emitters);
    }
    /* Pause presents the solved pose (alpha=1). Resuming interpolation can
     * briefly lag that pose; retain its tail until presentation catches up. */
    time = fmax(time, emitters->time);
    if (!emitters->borrowed) {
        particles_tick(emitters->particles, time - emitters->time);
    }
    emitters->world = world;
    emitters->time = time;
    emitters->simulation_time = simulation_time;
    emitters->head_count = 0;
    for (int i = 0; i < EMITTER_LIMIT; i++) {
        emitters->sources[i].seen = false;
    }
    ecs_component_id required[] = { COMPONENT_Transform.id,
                                    COMPONENT_ParticleEmitter.id };
    ecs_query query;
    bool ok = true;
    if (ecs_query_begin(world, &query, required, 2) == ECS_OK) {
        ecs_entity entity;
        while (ecs_query_next(&query, &entity)) {
            if (!engine_entity_visible(runtime, world, entity)) {
                continue;
            }
            const ParticleEmitter *style = ParticleEmitter_get_const(world, entity);
            const Fuel *fuel = Fuel_get_const(world, entity);
            const Igniter *igniter = Igniter_get_const(world, entity);
            const MaterialThermal *thermal = MaterialThermal_get_const(world, entity);
            const MaterialConstitutive *model = MaterialConstitutive_get_const(world, entity);
            const material_fluid *fluid = emitters->materials
                                            ? materials_fluid(emitters->materials)
                                            : NULL;
            bool needs_model = style->fluid_kind != FLUID_GAS && fluid &&
                               material_fluid_solver(fluid) == FLUID_SOLVER_MPM;
            fluid_constitutive constitutive = model ? (
                                                          fluid_constitutive
                                                      ){ model->density,
                                                         model->bulk_modulus,
                                                         model->shear_modulus }
                                                    : (fluid_constitutive){ 0 };
            if (style->fluid && (model || needs_model) &&
                !material_fluid_constitutive_valid(constitutive)) {
                snprintf(error, error_size, "Fluid emitter material needs positive density/bulk and nonnegative shear modulus.");
                ok = false;
                break;
            }
            fluid_thermal heat = thermal ? (fluid_thermal){ thermal->capacity,
                                                            thermal->conductivity }
                                         : (fluid_thermal){ 0 };
            if (style->fluid && (!material_fluid_thermal_valid(heat) ||
                                 (heat.capacity > 0 &&
                                  !isnormal(style->fluid_mass * heat.capacity)))) {
                snprintf(error, error_size, "Fluid emitter thermal properties need finite nonnegative capacity and conductivity, and positive capacity when conductive.");
                ok = false;
                break;
            }
            if (!valid(style) ||
                (style->fluid &&
                 ((fuel && (!isfinite(fuel->amount) || fuel->amount < 0)) ||
                  (igniter && (!isfinite(igniter->heat) || igniter->heat < 0))))) {
                snprintf(error, error_size, "Particle Emitter needs finite ranges, a valid shape and a nonzero direction.");
                ok = false;
                break;
            }
            if (!style->active || (style->rate == 0 && style->burst_count == 0)) {
                continue;
            }
            ecs_entity owner = style->parent ? ecs_entity_from_u64(style->parent)
                                             : entity;
            if (!ecs_entity_is_alive(world, owner) || !Transform_has(world, owner)) {
                continue;
            }
            GlobalTransform2D shown = engine_render_transform(runtime, world, owner);
            Transform position = shown.position;
            const Geometry3D *shape = Geometry3D_get_const(world, owner);
            const VisualTransform *visual = VisualTransform_get_const(world, owner);
            float scale = visual ? visual->scale : 1;
            if (scale == 0) {
                continue;
            }
            CF_V3 rotation = visual && !style->heading_only
                               ? (CF_V3){ visual->pitch, visual->yaw, visual->roll }
                               : (CF_V3){ 0 };
            float yaw = shown.radians, elevation = shape ? shape->elevation : 0;
            if (scale < 0 || !isfinite(
                                 scale + yaw + elevation + position.x +
                                 position.y + rotation.x + rotation.y + rotation.z
                             )) {
                snprintf(error, error_size, "Particle Emitter source needs a finite visual transform and nonnegative scale.");
                ok = false;
                break;
            }
            CF_M4x4 frame = cf_m4_from_trs(
                (CF_V3){ position.x, elevation, position.y },
                visual_rotation(yaw, rotation),
                (CF_V3){ scale, scale, scale }
            );
            CF_V3 origin = cf_m4_transform_point(frame, style->offset);
            CF_V3 direction = cf_norm_v3(cf_m4_transform_dir(frame, style->direction));
            emitter_source *source = source_for(emitters, entity);
            if (!source) {
                if (!emitters->reported_capacity) {
                    fprintf(stderr, "Particle emitters: %d-source budget full; remaining sources omitted.\n", EMITTER_LIMIT);
                    emitters->reported_capacity = true;
                }
                continue;
            }
            const Velocity *velocity = Velocity_get_const(world, owner);
            CF_V3 inherited = velocity ? (CF_V3){ velocity->x, 0, velocity->y }
                                       : (CF_V3){ 0 };
            if (!velocity && source->time >= 0 && time > source->time) {
                inherited = cf_mul_v3_f(
                    cf_sub_v3(origin, source->position),
                    1 / (time - source->time)
                );
            }
            if (!isfinite(
                    origin.x + origin.y + origin.z + direction.x + direction.y +
                    direction.z + inherited.x + inherited.y + inherited.z
                ) ||
                !emit_source(
                    emitters,
                    source,
                    style,
                    origin,
                    direction,
                    cf_mul_v3_f(inherited, style->velocity_inherit),
                    time
                )) {
                snprintf(error, error_size, "Particle Emitter pose or velocity exceeds finite particle bounds.");
                ok = false;
                break;
            }
        }
        ecs_query_end(&query);
    }
    for (int i = 0; i < EMITTER_LIMIT; i++) {
        if (!emitters->sources[i].seen) {
            emitters->sources[i] = (emitter_source){ 0 };
        }
    }
    return ok;
}

const particles *particle_emitters_particles(const particle_emitters *emitters)
{
    return emitters->particles;
}

const particle *particle_emitters_heads(const particle_emitters *emitters, int *count)
{
    *count = emitters->head_count;
    return emitters->heads;
}
