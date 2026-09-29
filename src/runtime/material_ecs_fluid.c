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

static void free_fluid(game_materials *materials)
{
    material_fluid_free(materials->fluid);
    materials->fluid = NULL;
    material_ecs_free_substances(materials);
    for (int i = 0; i < acount(materials->records); i++) {
        materials->records[i].volume = false;
    }
}

static const char *validate_responses(MaterialFluidWorld settings)
{
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
    return NULL;
}

static const char *validate_world(MaterialFluidWorld settings)
{
    if (!isfinite(settings.chunk_size + settings.buffer + settings.kernel) ||
        settings.chunk_size < 32 || settings.chunk_size > 1024 ||
        settings.buffer < settings.kernel || settings.buffer > 2048 ||
        settings.kernel < 2 || settings.kernel > 64 ||
        settings.particle_limit < 1 || settings.particle_limit > 1000000) {
        return "Fluid world requires one configuration, chunks 32..1024, "
               "kernel 2..64, a buffer at least one kernel, 1..1000000 "
               "particles";
    }
    if (!isfinite(settings.gas_cell_size + settings.gas_vorticity + settings.gas_drag) ||
        (settings.gas_cell_size != 0 &&
         (settings.gas_cell_size < 2 || settings.gas_cell_size > 64)) ||
        settings.gas_vorticity < 0 || settings.gas_vorticity > 10 ||
        settings.gas_drag < 0 || settings.gas_drag > 10) {
        return "Gas flow requires cell size 0 or 2..64, vorticity 0..10 and "
               "drag 0..10";
    }
    if (!isfinite(
            settings.blast_radius + settings.blast_speed + settings.blast_damage +
            settings.heat_conduction + settings.glow_temperature + settings.glow_emission
        ) ||
        settings.blast_radius < 0 || settings.blast_radius > COMBUSTION_BLAST_RADIUS_MAX ||
        settings.blast_speed < 0 || settings.blast_damage < 0 ||
        settings.heat_conduction < 0 || settings.glow_temperature < 0 ||
        settings.glow_emission < 0) {
        return "Fluid world blasts need radius 0..1024 and nonnegative speed "
               "and damage; heat needs nonnegative conduction and glow";
    }
    return validate_responses(settings);
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
    while (ecs_query_next(&query, &entity)) {
        settings = *GAME_MaterialFluidWorld_get_const(materials->world, entity);
        count++;
    }
    ecs_query_end(&query);
    if (!count) {
        if (materials->fluid) {
            free_fluid(materials);
        }
        return NULL;
    }
    const char *error = count == 1 ? validate_world(settings)
                                   : "Fluid world requires one configuration";
    if (error) {
        return error;
    }
    if (materials->fluid &&
        (settings.kernel != materials->settings.kernel ||
         settings.particle_limit != materials->settings.particle_limit)) {
        return "Restart the scene after changing the material kernel or "
               "particle capacity";
    }
    materials->settings = settings;
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
    material_fluid_set_heat(
        materials->fluid,
        (fluid_heat){ settings.heat_conduction,
                      settings.glow_temperature,
                      settings.glow_color,
                      settings.glow_emission }
    );
    return material_ecs_load_substances(materials);
}

static float volume_density(int phase, GameFluidVolume v, float x, float y)
{
    float nx = 2 * x / v.width, ny = 2 * y / v.height;
    float radius = hypotf(nx, ny);
    if (phase != FLUID_GAS) {
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

/* A volume's look fields replace the substance's where they are nonzero. */
static fluid_particle volume_particle(game_materials *materials, GameFluidVolume v, int substance, fluid_vec position)
{
    fluid_particle p = material_fluid_particle(materials->fluid, substance, position);
    p.color = v.color ? v.color : p.color;
    p.absorption = v.absorption ? v.absorption : p.absorption;
    p.emission = v.emission ? v.emission : p.emission;
    p.visibility = v.visibility ? v.visibility : p.visibility;
    p.refraction = v.refraction ? v.refraction : p.refraction;
    return p;
}

const char *material_ecs_fill_volume(game_materials *materials, ecs_entity entity)
{
    GameFluidVolume v = *GAME_GameFluidVolume_get_const(materials->world, entity);
    const GameTransform *origin = GAME_GameTransform_get_const(materials->world, entity);
    int substance = game_materials_substance(materials, v.substance);
    if (substance < 0) {
        return "Fluid volume names an unknown substance";
    }
    if (!origin || !isfinite(origin->x + origin->y) ||
        !isfinite(
            v.width + v.height + v.absorption + v.emission + v.visibility +
            v.refraction + v.velocity.x + v.velocity.y
        ) ||
        v.width <= 0 || v.height <= 0 || v.width > 8192 || v.height > 8192 ||
        v.absorption < 0 || v.emission < 0 || v.visibility < 0 ||
        v.visibility > 1 || v.refraction < 0 || v.refraction > 16) {
        return "Fluid volume needs Transform, positive dimensions up to 8192 "
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
    int count;
    int phase = material_fluid_substances(materials->fluid, &count)[substance].phase;
    float spacing = material_fluid_spacing(materials->fluid, phase);
    int nx = (int)ceilf(v.width / spacing), ny = (int)ceilf(v.height / spacing);
    int required = 0, existing;
    material_fluid_particles(materials->fluid, &existing);
    for (int y = 0; y < ny; y++) {
        for (int x = 0; x < nx; x++) {
            float px = (x + .5f - nx * .5f) * spacing;
            float py = (y + .5f - ny * .5f) * spacing;
            fluid_vec position = { origin->x + c * px - s * py,
                                   origin->y + s * px + c * py };
            required += volume_density(phase, v, px, py) > 0 &&
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
            float px = (x + .5f - nx * .5f) * spacing;
            float py = (y + .5f - ny * .5f) * spacing;
            float fill = volume_density(phase, v, px, py);
            fluid_vec position = { origin->x + c * px - s * py,
                                   origin->y + s * px + c * py };
            if (fill <= 0 || in_solid(materials, position)) {
                continue;
            }
            fluid_particle p = volume_particle(materials, v, substance, position);
            if (phase == FLUID_GAS) {
                /* Fixed local texture travels with the particles. Jitter
                 * removes lattice banding; broad opacity variation gives
                 * an art-directed cloud without a second fine fluid solve. */
                unsigned hash = ((unsigned)x * 73856093u ^ (unsigned)y * 19349663u) *
                                1597334677u;
                float density = .6f + .25f * sinf(px * .023f) * cosf(py * .037f) +
                                .15f * sinf(px * .087f + py * .049f);
                p.color = (p.color & 0xffffff00u) |
                          (unsigned)((p.color & 255) * density * fill);
                px += (((hash & 65535) / 65535.f) - .5f) * spacing * .65f;
                py += (((hash >> 16) / 65535.f) - .5f) * spacing * .65f;
                p.position = (fluid_vec){ origin->x + c * px - s * py,
                                          origin->y + s * px + c * py };
            }
            p.velocity = (fluid_vec){ c * v.velocity.x - s * v.velocity.y,
                                      s * v.velocity.x + c * v.velocity.y };
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

static void blasts(game_materials *materials, const fluid_step *step)
{
    fluid_body *bodies = step->colliders.bodies;
    int body_count = step->colliders.body_count;
    int count;
    const fluid_blast *events = material_fluid_blasts(materials->fluid, &count);
    MaterialFluidWorld settings = materials->settings;
    for (int e = 0; e < count; e++) {
        fluid_blast event = events[e];
        float density = fminf(1, event.ignitions / 8.f);
        float radius = settings.blast_radius * (.35f + .65f * density);
        if (radius <= 0) {
            continue;
        }
        material_fluid_push(materials->fluid, event.position, radius, settings.blast_speed * density, step);
        for (int i = 0; i < body_count; i++) {
            fluid_body *b = &bodies[i];
            float dx = b->position.x - event.position.x,
                  dy = b->position.y - event.position.y;
            float distance = hypotf(dx, dy);
            if (b->inverse_mass <= 0 || distance < .001f || distance >= radius ||
                material_fluid_blocked(event.position, b->position, &step->colliders, step->solids, i)) {
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
                        (material_impact){ settings.glow_color, settings.glow_temperature }
                    );
                    hit = true;
                    break;
                }
            }
        }
    }
}

/* Corrosive matter dissolves the solids it touches; hot matter melts those
 * with a melting point. */
static void touch_solids(game_materials *materials, const fluid_step *step)
{
    int count, solids;
    const fluid_touch *touches = material_fluid_touches(materials->fluid, &count);
    const material_solid *items = count ? material_solids_items(step->solids, &solids)
                                        : NULL;
    for (int i = 0; i < count; i++) {
        fluid_touch t = touches[i];
        material_solid solid = items[t.solid];
        ecs_entity entity = ecs_entity_from_u64(solid.owner);
        float radius = solid.mask->cell_size * 1.5f;
        if (t.corrosion > 0) {
            game_materials_damage(
                materials,
                entity,
                t.point,
                radius,
                t.corrosion,
                MATERIAL_DAMAGE_CORROSION,
                (material_impact){ 0 }
            );
        }
        if (t.melts) {
            game_materials_damage(
                materials,
                entity,
                t.point,
                radius,
                MATERIAL_MELT_ENERGY,
                MATERIAL_DAMAGE_MELT,
                (material_impact){ 0 }
            );
        }
    }
}

void game_materials_heat_area(game_materials *materials, fluid_vec point, float radius, float delta)
{
    assert(isfinite(point.x + point.y + radius + delta) && radius >= 0);
    if (materials->fluid) {
        material_fluid_heat_area(materials->fluid, point, radius, delta);
    }
    for (int i = 0; delta > 0 && i < acount(materials->records); i++) {
        material_record *record = &materials->records[i];
        if (!record->mask || !ecs_entity_is_alive(materials->world, record->entity) ||
            !game_materials_static_solid(materials, record->entity)) {
            continue;
        }
        const GameSubstance
            *solid = GAME_GameSubstance_get_const(materials->world, record->entity);
        const GameTransform
            *p = GAME_GameTransform_get_const(materials->world, record->entity);
        float reach = hypotf(record->mask->width, record->mask->height) *
                          record->mask->cell_size * .5f +
                      radius;
        if (!solid || solid->melts_at <= 0 || delta < solid->melts_at ||
            hypotf(point.x - p->x, point.y - p->y) > reach) {
            continue;
        }
        game_materials_damage(
            materials,
            record->entity,
            point,
            radius,
            MATERIAL_MELT_ENERGY,
            MATERIAL_DAMAGE_MELT,
            (material_impact){ 0 }
        );
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
        bool gas = contact.phase == FLUID_GAS;
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

void material_ecs_tick_fluid(game_materials *materials, fluid_step *step)
{
    if (!materials->fluid) {
        return;
    }
    camera_regions(materials);
    float buffer = materials->settings.buffer;
    for (int i = 0; i < step->colliders.body_count; i++) {
        fluid_body b = step->colliders.bodies[i];
        if (b.velocity.x * b.velocity.x + b.velocity.y * b.velocity.y > 1) {
            apush(
                materials->regions,
                ((fluid_region){ b.position.x - buffer, b.position.y - buffer, 2 * buffer, 2 * buffer })
            );
        }
    }
    step->regions = materials->regions;
    step->region_count = acount(materials->regions);
    game_task_pool_begin(materials->pool, true);
    material_fluid_tick(materials->fluid, step);
    game_task_pool_end(materials->pool);
    int count;
    const fluid_deposit *deposits = material_fluid_deposits(materials->fluid, &count);
    for (int i = 0; i < count; i++) {
        apush(materials->deposit_requests, deposits[i]);
    }
    touch_solids(materials, step);
    CHIBI_TRACE_ZONE_BEGIN(contact_zone, "Fluid contact effects");
    emit_contact_effects(materials);
    CHIBI_TRACE_ZONE_END(contact_zone);
    CHIBI_TRACE_ZONE_BEGIN(blast_zone, "Material blast effects");
    blasts(materials, step);
    CHIBI_TRACE_ZONE_END(blast_zone);
}
