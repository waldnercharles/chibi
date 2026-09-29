#include "diagnostics/tracy.h"
#include "render/particles.h"
#include "runtime/behaviors/entities.h"
#include "runtime/material_ecs.h"
#include "support/array.h"

#include <assert.h>
#include <math.h>
#include <string.h>

enum
{
    FLUID_CONTACT_PARTICLE_LIMIT = 256,
    /* Blasts push every active particle and chunk in range. */
    COMBUSTION_BLAST_RADIUS_MAX = 1024
};

bool game_materials_emit(game_materials *materials, fluid_particle particle)
{
    particle.radius = fminf(particle.radius, materials->settings.kernel);
    return materials->fluid && material_fluid_add(materials->fluid, particle);
}

const char *material_ecs_configure_fluid(game_materials *materials)
{
    ecs_component_id id = GAME_COMPONENT_MaterialFluidWorld.id;
    if (!ecs_world_component(materials->world, id)) {
        return NULL;
    }
    ecs_query query;
    int result = ecs_query_begin(materials->world, &query, &id, 1);
    assert(result == ECS_OK);
    ecs_entity entity;
    int count = 0;
    MaterialFluidWorld settings = { 0 };
    bool reactive = false;
    while (ecs_query_next(&query, &entity)) {
        settings = *GAME_MaterialFluidWorld_get_const(materials->world, entity);
        const GameCombustion *reaction = GAME_GameCombustion_get_const(materials->world, entity);
        reactive = reaction != NULL;
        materials->combustion = reaction ? *reaction : (GameCombustion){ 0 };
        count++;
    }
    ecs_query_end(&query);
    if (!count) {
        if (materials->fluid) {
            material_fluid_free(materials->fluid);
            materials->fluid = NULL;
            for (int i = 0; i < acount(materials->records); i++) {
                materials->records[i].volume = false;
            }
        }
        return NULL;
    }
    if (count != 1 ||
        !isfinite(settings.chunk_size + settings.buffer + settings.kernel) ||
        settings.chunk_size < 32 || settings.chunk_size > 1024 ||
        settings.buffer < settings.kernel || settings.buffer > 2048 ||
        settings.kernel < 2 || settings.kernel > 64 ||
        settings.particle_limit < 1 || settings.particle_limit > 1000000) {
        return "Fluid world requires one configuration, chunks 32..1024, "
               "kernel 2..64, a buffer at least one kernel, 1..1000000 "
               "particles";
    }
    if (materials->fluid &&
        (settings.kernel != materials->settings.kernel ||
         settings.particle_limit != materials->settings.particle_limit)) {
        return "Restart the scene after changing the material kernel or "
               "particle capacity";
    }
    if (!isfinite(settings.gas_cell_size + settings.gas_vorticity + settings.gas_drag) ||
        (settings.gas_cell_size != 0 &&
         (settings.gas_cell_size < 2 || settings.gas_cell_size > 64)) ||
        settings.gas_vorticity < 0 || settings.gas_vorticity > 10 ||
        settings.gas_drag < 0 || settings.gas_drag > 10) {
        return "Gas flow requires cell size 0 or 2..64, vorticity 0..10 and "
               "drag 0..10";
    }
    float response_values[] = {
        settings.gas_wake_speed,         settings.gas_wake_strength,
        settings.gas_wake_lifetime,      settings.liquid_splash_speed,
        settings.liquid_splash_strength, settings.liquid_splash_lifetime
    };
    for (int i = 0; i < (int)(sizeof response_values / sizeof *response_values); i++) {
        if (!isfinite(response_values[i])) {
            return "Fluid contact responses require finite speed, strength and "
                   "lifetime";
        }
    }
    if (settings.gas_wake_speed < 0 || settings.gas_wake_speed > 1000 ||
        settings.gas_wake_strength < 0 || settings.gas_wake_strength > 10 ||
        settings.gas_wake_lifetime < 0 || settings.gas_wake_lifetime > 3 ||
        settings.gas_wake_count < 0 || settings.gas_wake_count > 8 ||
        settings.liquid_splash_speed < 0 || settings.liquid_splash_speed > 1000 ||
        settings.liquid_splash_strength < 0 || settings.liquid_splash_strength > 10 ||
        settings.liquid_splash_lifetime < 0 || settings.liquid_splash_lifetime > 3 ||
        settings.liquid_splash_count < 0 || settings.liquid_splash_count > 8 ||
        (settings.gas_wake_count && (!settings.gas_wake_speed || !settings.gas_wake_strength ||
                                     !settings.gas_wake_lifetime)) ||
        (settings.liquid_splash_count &&
         (!settings.liquid_splash_speed || !settings.liquid_splash_strength ||
          !settings.liquid_splash_lifetime))) {
        return "Fluid contact responses need speed 0..1000, strength 0..10, "
               "lifetime 0..3 and count 0..8; enabled responses need all "
               "positive values";
    }
    materials->settings = settings;
    GameCombustion r = materials->combustion;
    if (reactive && (!isfinite(
                         r.ignition + r.cooling + r.burn_rate + r.heat_release +
                         r.smoke_lifetime + r.smoke_absorption + r.flame_emission +
                         r.blast_radius + r.blast_speed + r.blast_damage
                     ) ||
                     r.ignition <= 0 || r.cooling < 0 || r.burn_rate <= 0 ||
                     r.heat_release <= r.ignition || r.smoke_lifetime <= 0 ||
                     r.smoke_absorption < 0 || r.flame_emission < 0 ||
                     r.blast_radius < 0 || r.blast_radius > COMBUSTION_BLAST_RADIUS_MAX ||
                     r.blast_speed < 0 || r.blast_damage < 0)) {
        return "Combustion requires finite nonnegative tuning, positive "
               "ignition/burn/lifetime, heat above ignition and blast radius "
               "<=1024";
    }
    if (!materials->fluid) {
        materials->fluid = material_fluid_new(settings.kernel, settings.particle_limit);
        material_fluid_set_tasks(
            materials->fluid,
            game_task_pool_tasks(materials->pool)
        );
    }
    material_fluid_set_flow(
        materials->fluid,
        (fluid_flow_settings){ settings.gas_cell_size,
                               settings.gas_vorticity,
                               settings.gas_drag }
    );
    material_fluid_set_contact_settings(
        materials->fluid,
        (fluid_contact_settings){
            settings.gas_wake_count ? settings.gas_wake_speed : 0,
            settings.liquid_splash_count ? settings.liquid_splash_speed : 0 }
    );
    material_fluid_set_reaction(
        materials->fluid,
        (fluid_reaction){ r.ignition,
                          r.cooling,
                          r.burn_rate,
                          r.heat_release,
                          r.smoke_lifetime,
                          r.smoke_color,
                          r.smoke_absorption,
                          r.flame_color,
                          r.flame_emission }
    );
    return NULL;
}

static float volume_density(GameFluidVolume v, float x, float y)
{
    float nx = 2 * x / v.width, ny = 2 * y / v.height;
    float radius = hypotf(nx, ny);
    if (v.kind == FLUID_LIQUID) {
        return radius <= 1;
    }
    float angle = atan2f(ny, nx);
    float edge = .76f + .12f * sinf(3 * angle) + .07f * cosf(5 * angle + 1) +
                 .05f * sinf(7 * angle + 2);
    return fmaxf(0, fminf(1, (edge - radius) / .22f));
}

static bool in_solid(game_materials *materials, fluid_vec point)
{
    for (int i = 0; i < acount(materials->records); i++) {
        material_record record = materials->records[i];
        if (!record.mask) {
            continue;
        }
        const GameTransform
            *p = GAME_GameTransform_get_const(materials->world, record.entity);
        const GameOrientation
            *heading = GAME_GameOrientation_get_const(materials->world, record.entity);
        float angle = heading ? heading->radians : 0, c = cosf(angle), s = sinf(angle);
        float x = point.x - p->x, y = point.y - p->y;
        if (material_mask_contains(record.mask, c * x + s * y, -s * x + c * y)) {
            return true;
        }
    }
    return false;
}

const char *material_ecs_fill_volume(game_materials *materials, ecs_entity entity)
{
    GameFluidVolume v = *GAME_GameFluidVolume_get_const(materials->world, entity);
    const GameFuel *fuel = GAME_GameFuel_get_const(materials->world, entity);
    const GameIgniter *igniter = GAME_GameIgniter_get_const(materials->world, entity);
    if ((fuel && (!isfinite(fuel->amount) || fuel->amount < 0)) ||
        (igniter && (!isfinite(igniter->heat) || igniter->heat < 0))) {
        return "Fuel and heat must be finite and nonnegative";
    }
    const GameTransform *origin = GAME_GameTransform_get_const(materials->world, entity);
    if (!origin || !isfinite(origin->x + origin->y) ||
        (v.kind != FLUID_GAS && v.kind != FLUID_LIQUID) ||
        !isfinite(
            v.width + v.height + v.spacing + v.mass + v.absorption + v.emission +
            v.visibility + v.refraction + v.velocity.x + v.velocity.y
        ) ||
        v.width <= 0 || v.height <= 0 || v.width > 8192 || v.height > 8192 ||
        v.spacing < materials->settings.kernel * .3f ||
        v.spacing > materials->settings.kernel || v.mass <= 0 ||
        v.absorption < 0 || v.emission < 0 || v.visibility < 0 ||
        v.visibility > 1 || v.refraction < 0 || v.refraction > 16) {
        return "Fluid volume needs Transform, gas/liquid kind, positive "
               "dimensions up to 8192, spacing 0.3..1 kernels, positive mass "
               "and nonnegative lighting";
    }
    material_record *record = material_ecs_record(materials, entity);
    if (record->volume) {
        return NULL;
    }
    const GameOrientation
        *orientation = GAME_GameOrientation_get_const(materials->world, entity);
    float angle = orientation ? orientation->radians : 0, c = cosf(angle),
          s = sinf(angle);
    int nx = (int)ceilf(v.width / v.spacing), ny = (int)ceilf(v.height / v.spacing);
    int required = 0, existing;
    material_fluid_particles(materials->fluid, &existing);
    for (int y = 0; y < ny; y++) {
        for (int x = 0; x < nx; x++) {
            float px = (x + .5f - nx * .5f) * v.spacing;
            float py = (y + .5f - ny * .5f) * v.spacing;
            fluid_vec position = { origin->x + c * px - s * py,
                                   origin->y + s * px + c * py };
            required += volume_density(v, px, py) > 0 &&
                        !in_solid(materials, position);
        }
    }
    if (required > materials->settings.particle_limit - existing) {
        return "Fluid volumes exceed the world's particle capacity; reduce "
               "their size or raise the limit";
    }
    record->volume = true;
    for (int y = 0; y < ny; y++) {
        for (int x = 0; x < nx; x++) {
            float px = (x + .5f - nx * .5f) * v.spacing;
            float py = (y + .5f - ny * .5f) * v.spacing;
            float fill = volume_density(v, px, py);
            fluid_vec position = { origin->x + c * px - s * py,
                                   origin->y + s * px + c * py };
            if (fill <= 0 || in_solid(materials, position)) {
                continue;
            }
            unsigned color = v.color;
            if (v.kind == FLUID_GAS) {
                /* Fixed local texture travels with the particles. Jitter
                 * removes lattice banding; broad opacity variation gives
                 * an art-directed cloud without a second fine fluid solve. */
                unsigned hash = ((unsigned)x * 73856093u ^ (unsigned)y * 19349663u) *
                                1597334677u;
                float density = .6f + .25f * sinf(px * .023f) * cosf(py * .037f) +
                                .15f * sinf(px * .087f + py * .049f);
                color = (color & 0xffffff00u) |
                        (unsigned)((color & 255) * density * fill);
                px += (((hash & 65535) / 65535.f) - .5f) * v.spacing * .65f;
                py += (((hash >> 16) / 65535.f) - .5f) * v.spacing * .65f;
            }
            fluid_particle p = {
                .position = { origin->x + c * px - s * py, origin->y + s * px + c * py },
                .velocity = { c * v.velocity.x - s * v.velocity.y,
                              s * v.velocity.x + c * v.velocity.y },
                .mass = v.mass,
                .radius = v.kind == FLUID_GAS
                            ? materials->settings.kernel
                            : fminf(v.spacing * 1.5f, materials->settings.kernel),
                .kind = v.kind,
                .color = color,
                .absorption = v.absorption,
                .fuel = fuel ? fuel->amount : 0,
                .heat = igniter ? igniter->heat : 0,
                .emission = v.emission,
                .visibility = v.visibility,
                .refraction = v.refraction
            };
            if (!material_fluid_add(materials->fluid, p)) {
                return "Fluid volumes exceed the world's particle capacity; "
                       "reduce their size or raise the limit";
            }
        }
    }
    return NULL;
}

static void camera_regions(game_materials *materials)
{
    aclear(materials->regions);
    ecs_component_id ids[] = { GAME_COMPONENT_GameCamera.id,
                               GAME_COMPONENT_GameTransform.id };
    if (!ecs_world_component(materials->world, ids[0]) ||
        !ecs_world_component(materials->world, ids[1])) {
        return;
    }
    ecs_query query;
    int result = ecs_query_begin(materials->world, &query, ids, 2);
    assert(result == ECS_OK);
    ecs_entity entity;
    while (ecs_query_next(&query, &entity)) {
        const GameCamera *camera = GAME_GameCamera_get_const(materials->world, entity);
        if (camera->disabled) {
            continue;
        }
        const GameTransform *position = GAME_GameTransform_get_const(materials->world, entity);
        float buffer = materials->settings.buffer;
        apush(
            materials->regions,
            ((fluid_region){ position->x + camera->offset.x - camera->width * .5f - buffer,
                             position->y + camera->offset.y - camera->height * .5f - buffer,
                             camera->width + 2 * buffer,
                             camera->height + 2 * buffer })
        );
    }
    ecs_query_end(&query);
}

static void blasts(
    game_materials *materials,
    fluid_body *bodies,
    int body_count,
    const fluid_obstacle *obstacles,
    int obstacle_count
)
{
    int count;
    const fluid_blast *events = material_fluid_blasts(materials->fluid, &count);
    GameCombustion settings = materials->combustion;
    for (int e = 0; e < count; e++) {
        fluid_blast event = events[e];
        float density = fminf(1, event.ignitions / 8.f);
        float radius = settings.blast_radius * (.35f + .65f * density);
        if (radius <= 0) {
            continue;
        }
        material_fluid_push(
            materials->fluid,
            event.position,
            radius,
            settings.blast_speed * density,
            bodies,
            obstacles,
            obstacle_count
        );
        for (int i = 0; i < body_count; i++) {
            fluid_body *b = &bodies[i];
            float dx = b->position.x - event.position.x,
                  dy = b->position.y - event.position.y;
            float distance = hypotf(dx, dy);
            if (b->inverse_mass <= 0 || distance < .001f || distance >= radius ||
                material_fluid_blocked(event.position, b->position, bodies, obstacles, obstacle_count, i)) {
                continue;
            }
            float impulse = settings.blast_speed * density *
                            (1 - distance / radius) / b->inverse_mass;
            b->impulse.x += dx / distance * impulse;
            b->impulse.y += dy / distance * impulse;
            ecs_entity entity = ecs_entity_from_u64(b->owner);
            if (!game_materials_mask(materials, entity)) {
                material_destruction_emit_event(
                    materials->destruction,
                    (material_event){
                        .kind = MATERIAL_EVENT_BODY_IMPACT,
                        .target = b->owner,
                        .point = { event.position.x, event.position.y },
                        .energy = settings.blast_damage * density * (1 - distance / radius),
                        .cause = MATERIAL_DAMAGE_BLAST }
                );
            }
        }
        /* Trace spokes to the first surviving cell. A blast chips the facing
         * wall but cannot damage a second wall/body through it in this tick. */
        for (int spoke = 0; spoke < 24; spoke++) {
            float angle = spoke * (6.2831853f / 24), dx = cosf(angle),
                  dy = sinf(angle);
            bool hit = false;
            for (float distance = 2; distance <= radius && !hit; distance += 2) {
                fluid_vec point = { event.position.x + dx * distance,
                                    event.position.y + dy * distance };
                for (int i = 0; i < acount(materials->records); i++) {
                    material_record record = materials->records[i];
                    if (!record.mask ||
                        !ecs_entity_is_alive(materials->world, record.entity)) {
                        continue;
                    }
                    const GameTransform *p = GAME_GameTransform_get_const(
                        materials->world,
                        record.entity
                    );
                    const GameOrientation *o = GAME_GameOrientation_get_const(
                        materials->world,
                        record.entity
                    );
                    float c = o ? cosf(o->radians) : 1, s = o ? sinf(o->radians) : 0;
                    float x = point.x - p->x, y = point.y - p->y;
                    if (!material_mask_contains(record.mask, c * x + s * y, -s * x + c * y)) {
                        continue;
                    }
                    game_materials_damage(
                        materials,
                        record.entity,
                        point,
                        5,
                        settings.blast_damage * density * (1 - distance / radius),
                        MATERIAL_DAMAGE_BLAST,
                        (material_impact){ materials->combustion.flame_color,
                                           materials->combustion.heat_release }
                    );
                    hit = true;
                    break;
                }
            }
        }
    }
}

static float clamp01(float value)
{
    return fminf(1, fmaxf(0, value));
}

static unsigned contact_alpha(unsigned color, float factor)
{
    unsigned alpha = color & 255u;
    return (color & 0xffffff00u) | (unsigned)(alpha * clamp01(factor) + .5f);
}

static int contact_count(int maximum, float intensity)
{
    if (maximum <= 0 || intensity <= 0) {
        return 0;
    }
    int count = (int)ceilf(maximum * intensity);
    return count > maximum ? maximum : count;
}

static void emit_contact_effects(game_materials *materials)
{
    if (!materials->particles) {
        return;
    }
    int count;
    const fluid_contact *contacts = material_fluid_contacts(materials->fluid, &count);
    MaterialFluidWorld settings = materials->settings;
    int emitted = 0;
    for (int i = 0; i < count; i++) {
        if (emitted >= FLUID_CONTACT_PARTICLE_LIMIT) {
            break;
        }
        fluid_contact contact = contacts[i];
        bool gas = contact.kind == FLUID_GAS;
        float threshold = gas ? settings.gas_wake_speed : settings.liquid_splash_speed;
        float strength = gas ? settings.gas_wake_strength
                             : settings.liquid_splash_strength;
        float lifetime = gas ? settings.gas_wake_lifetime
                             : settings.liquid_splash_lifetime;
        int maximum = gas ? settings.gas_wake_count : settings.liquid_splash_count;
        if (threshold <= 0 || !maximum) {
            continue;
        }
        float impact = clamp01((contact.closing_speed / threshold - 1) * .5f);
        float impulse = clamp01(contact.impulse / (contact.mass * threshold * 2));
        float density = sqrtf(clamp01(contact.contacts / 4.f));
        float intensity = clamp01(impact * impulse * density * strength);
        int particles = contact_count(maximum, intensity);
        if (!particles) {
            continue;
        }
        float speed = contact.closing_speed * strength;
        game_particle_spray spray = {
            .position = { contact.point.x, contact.point.y },
            .direction = { contact.normal.x, contact.normal.y },
            .count = particles,
            .color = gas ? contact_alpha(contact.color, .55f) : contact.color,
            .end_color = contact_alpha(contact.color, 0),
            .speed_min = speed * (gas ? .04f : .12f),
            .speed_max = speed * (gas ? .2f : .5f),
            .lifetime_min = lifetime * .7f,
            .lifetime_max = lifetime,
            .size_min = gas ? 3.f : 1.4f,
            .size_max = gas ? 6.f : 3.2f,
            .spread = gas ? 1.8f : 1.1f,
            .drag_min = gas ? 1.5f : .25f,
            .drag_max = gas ? 3.f : .8f,
            .emission_min = 0,
            .emission_max = fminf(10, contact.emission),
            .size_exponent = .8f,
            .emission_exponent = 1,
            .stretch = gas ? 0 : .004f,
            .antialias = 1.5f,
            .shape = GAME_PARTICLE_ROUND,
        };
        spray.count = spray.count < FLUID_CONTACT_PARTICLE_LIMIT - emitted
                        ? spray.count
                        : FLUID_CONTACT_PARTICLE_LIMIT - emitted;
        int emitted_now = game_particles_emit_directed(materials->particles, &spray);
        emitted += emitted_now > 0 ? emitted_now : 0;
    }
}

void material_ecs_tick_fluid(
    game_materials *materials,
    float delta,
    fluid_body *bodies,
    int body_count,
    const fluid_obstacle *obstacles,
    int obstacle_count
)
{
    if (!materials->fluid) {
        return;
    }
    camera_regions(materials);
    float buffer = materials->settings.buffer;
    for (int i = 0; i < body_count; i++) {
        fluid_body b = bodies[i];
        if (b.velocity.x * b.velocity.x + b.velocity.y * b.velocity.y > 1) {
            apush(
                materials->regions,
                ((fluid_region){ b.position.x - buffer, b.position.y - buffer, 2 * buffer, 2 * buffer })
            );
        }
    }
    game_task_pool_begin(materials->pool, true);
    material_fluid_tick(
        materials->fluid,
        delta,
        materials->settings.chunk_size,
        materials->regions,
        acount(materials->regions),
        bodies,
        body_count,
        obstacles,
        obstacle_count
    );
    game_task_pool_end(materials->pool);
    CHIBI_TRACE_ZONE_BEGIN(contact_zone, "Fluid contact effects");
    emit_contact_effects(materials);
    CHIBI_TRACE_ZONE_END(contact_zone);
    CHIBI_TRACE_ZONE_BEGIN(blast_zone, "Material blast effects");
    blasts(materials, bodies, body_count, obstacles, obstacle_count);
    CHIBI_TRACE_ZONE_END(blast_zone);
}
