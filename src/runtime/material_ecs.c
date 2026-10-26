#include "runtime/material_ecs.h"

#include "diagnostics/tracy.h"
#include "runtime/behaviors/entities.h"
#include "runtime/component_catalog.h"
#include "runtime/material_destruction.h"
#include "runtime/material_terrain.h"
#include "runtime/particles.h"
#include "runtime/task_pool.h"
#include "runtime/transform_propagation.h"
#include "support/array.h"
#include "support/cos_sin.h"
#include "support/xalloc.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

materials *materials_new_prepared(ecs_world *world, entities *entities, particles *particles, material_fields *fields)
{
    materials *materials = xcalloc(1, sizeof *materials);
    materials->world = world;
    materials->entities = entities;
    materials->particles = particles;
    materials->pool = task_pool_acquire();
    materials->destruction = material_destruction_new();
    materials->fields = fields ? fields : material_fields_new();
    return materials;
}

materials *materials_new(ecs_world *world, entities *entities, particles *particles)
{
    return materials_new_prepared(world, entities, particles, NULL);
}

void material_ecs_discard_depth_volume(material_record *record)
{
    if (record->depth_volume) {
        terrain_volume_free(record->depth_volume);
        free(record->depth_volume);
    }
    free(record->depth_removed);
    free(record->depth_open);
    record->depth_volume = NULL;
    record->depth_removed = NULL;
    record->depth_open = NULL;
    record->depth_identity = record->depth_revision = record->depth_hash = 0;
}

void material_ecs_release_record(material_record *record)
{
    if (record->stream) {
        material_stream_free(record->stream);
    }
    if (record->mask && !record->streamed_mask) {
        material_mask_free(record->mask);
        free(record->mask);
    }
    material_ecs_discard_depth_volume(record);
    *record = (material_record){ 0 };
}

static bool terrain_record(const material_record *record)
{
    return record->terrain || ecs_entity_to_u64(record->terrain_source);
}

/* Releases records and destroys the entities materials made for them;
 * terrain's stay when kept. */
static void release_records(materials *materials, bool keep_terrain)
{
    materials->fragments = 0;
    for (int i = 0; i < acount(materials->records); i++) {
        material_record *record = &materials->records[i];
        if (keep_terrain && terrain_record(record)) {
            materials->fragments += record->fragment;
            continue;
        }
        if ((record->fragment || ecs_entity_to_u64(record->terrain_source)) &&
            ecs_entity_is_alive(materials->world, record->entity)) {
            int result = ecs_world_destroy_entity(materials->world, record->entity);
            assert(result == ECS_OK);
            (void)result;
        }
        material_ecs_release_record(record);
    }
}

static void reset_simulation(materials *materials)
{
    material_destruction_reset(materials->destruction);
    aclear(materials->damage_requests);
    aclear(materials->terrain_cuts);
    aclear(materials->regions);
    aclear(materials->debris_tiles);
    aclear(materials->terrain_views);
    aclear(materials->debris_blasts);
    if (materials->debris) {
        material_debris_clear(materials->debris);
    }
    if (materials->fluid) {
        material_fluid_free(materials->fluid);
        materials->fluid = NULL;
    }
    materials->settings = (MaterialFluidWorld){ 0 };
}

void materials_reset(materials *materials)
{
    release_records(materials, false);
    aclear(materials->records);
    aclear(materials->host_views);
    materials->stream_deferred = false;
    reset_simulation(materials);
}

void materials_reset_except_terrain(materials *materials)
{
    release_records(materials, true);
    reset_simulation(materials);
}

void materials_free(materials *materials)
{
    materials_reset(materials);
    afree(materials->records);
    material_destruction_free(materials->destruction);
    afree(materials->damage_requests);
    afree(materials->terrain_cuts);
    material_fields_free(materials->fields);
    afree(materials->regions);
    material_debris_free(materials->debris);
    afree(materials->debris_tiles);
    afree(materials->terrain_views);
    afree(materials->surface_views);
    afree(materials->host_views);
    afree(materials->debris_blasts);
    task_pool_release(materials->pool);
    free(materials);
}

void materials_set_stream_views(materials *materials, const material_stream_view *views, int count)
{
    aclear(materials->host_views);
    for (int i = 0; i < count; i++) {
        apush(materials->host_views, views[i]);
    }
}

bool materials_streaming(const materials *materials)
{
    for (int i = 0; i < acount(materials->records); i++) {
        const material_stream *stream = materials->records[i].stream;
        if (stream && material_stream_waiting(stream)) {
            return true;
        }
    }
    return false;
}

void materials_set_stream_deferred(materials *materials, bool deferred)
{
    materials->stream_deferred = deferred;
}

bool materials_enabled(const materials *materials)
{
    return materials && (materials->fluid || acount(materials->records));
}

const material_fluid *materials_fluid(const materials *materials)
{
    return materials ? materials->fluid : NULL;
}

static uint64_t hash_bytes(uint64_t hash, const void *data, size_t count)
{
    const unsigned char *bytes = data;
    for (size_t i = 0; i < count; i++) {
        hash = (hash ^ bytes[i]) * UINT64_C(1099511628211);
    }
    return hash;
}

static material_state inspect_materials(const materials *materials, material_hash_parts *parts)
{
    material_state state = { 0 };
    if (!materials_enabled(materials)) {
        return state;
    }
    state.hash = UINT64_C(14695981039346656037);
    const fluid_particle
        *particles = materials->fluid
                       ? material_fluid_particles(materials->fluid, &state.particles)
                       : NULL;
    state.active_particles = materials->fluid
                               ? material_fluid_active_count(materials->fluid)
                               : 0;
    state.mpm = materials->fluid ? material_fluid_mpm_state(materials->fluid)
                                 : (fluid_mpm_state){ 0 };
    state.volume_min = state.volume_max = 1;
    uint64_t flow_hash = materials->fluid ? material_fluid_flow_hash(materials->fluid)
                                          : 0;
    if (flow_hash) {
        state.hash = hash_bytes(state.hash, &flow_hash, sizeof flow_hash);
    }
    if (parts) {
        parts->after_flow = state.hash;
    }
    state.fragments = materials->fragments;
    int chunk_count;
    const material_chunk *chunks = materials_chunks(materials, &chunk_count);
    state.chunks = chunk_count;
    for (int i = 0; i < chunk_count; i++) {
        float values[] = { chunks[i].position.x,
                           chunks[i].position.y,
                           chunks[i].angle,
                           chunks[i].age };
        state.hash = hash_bytes(state.hash, values, sizeof values);
    }
    if (parts) {
        parts->after_chunks = state.hash;
        parts->particle_position = UINT64_C(14695981039346656037);
        parts->particle_velocity = UINT64_C(14695981039346656037);
        parts->particle_parameters = UINT64_C(14695981039346656037);
        parts->particle_appearance = UINT64_C(14695981039346656037);
    }
    for (int i = 0; i < acount(materials->records); i++) {
        material_stream *stream = materials->records[i].stream;
        if (!stream) {
            continue;
        }
        material_stream_state s = material_stream_inspect(stream);
        state.streaming.resident += s.resident;
        state.streaming.stored += s.stored;
        state.streaming.loaded += s.loaded;
        state.streaming.unloaded += s.unloaded;
        state.streaming.resident_bytes += s.resident_bytes;
        state.streaming.stored_bytes += s.stored_bytes;
    }
    fluid_vec low = state.particles ? particles[0].position : (fluid_vec){ 0 };
    fluid_vec high = low;
    for (int i = 0; i < state.particles; i++) {
        fluid_particle p = particles[i];
        low.x = fminf(low.x, p.position.x);
        low.y = fminf(low.y, p.position.y);
        high.x = fmaxf(high.x, p.position.x);
        high.y = fmaxf(high.y, p.position.y);
        state.fuel_particles += p.fuel > 0;
        state.burning_particles += p.burning;
        state.fuel_remaining += p.fuel;
        state.thermal_particles += p.thermal.capacity > 0;
        state.thermal_energy += (double)p.mass * p.thermal.capacity * p.heat;
        state.elastic_particles += p.kind != FLUID_GAS &&
                                   p.constitutive.shear_modulus > 0;
        state.volume_min = fminf(state.volume_min, p.deformation.volume);
        state.volume_max = fmaxf(state.volume_max, p.deformation.volume);
        if (p.constitutive.shear_modulus > 0) {
            const float *f = p.deformation.f;
            double a = (double)f[0] * f[0] + (double)f[2] * f[2];
            double b = (double)f[0] * f[1] + (double)f[2] * f[3];
            double d = (double)f[1] * f[1] + (double)f[3] * f[3];
            double discriminant = hypot(a - d, 2 * b);
            double high = sqrt(fmax(0, (a + d + discriminant) * .5));
            double low = sqrt(fmax(0, (a + d - discriminant) * .5));
            state.strain_max = fmaxf(state.strain_max, (float)fmax(fabs(high - 1), fabs(low - 1)));
        }
        float values[] = { p.position.x,
                           p.position.y,
                           p.velocity.x,
                           p.velocity.y,
                           p.mass,
                           p.radius,
                           p.absorption,
                           p.emission,
                           p.lifetime,
                           p.fuel,
                           p.heat,
                           p.thermal.capacity,
                           p.thermal.conductivity,
                           p.base_emission,
                           p.base_glow,
                           p.glow,
                           p.visibility,
                           p.refraction,
                           p.grain_radius,
                           p.style.duration,
                           p.style.size_end,
                           p.style.size_exponent,
                           p.style.emission_end,
                           p.style.emission_exponent,
                           p.style.drag,
                           p.style.drag_end,
                           p.style.drag_exponent,
                           p.style.blend };
        state.hash = hash_bytes(state.hash, &p.burning, sizeof p.burning);
        state.hash = hash_bytes(state.hash, &p.smoke, sizeof p.smoke);
        state.hash = hash_bytes(state.hash, values, sizeof values);
        state.hash = hash_bytes(state.hash, &p.kind, sizeof p.kind);
        state.hash = hash_bytes(state.hash, &p.color, sizeof p.color);
        state.hash = hash_bytes(state.hash, &p.base_color, sizeof p.base_color);
        state.hash = hash_bytes(state.hash, &p.style.end_color, sizeof p.style.end_color);
        const float mechanics[] = {
            p.constitutive.density,       p.constitutive.bulk_modulus,
            p.constitutive.shear_modulus, p.deformation.volume,
            p.deformation.f[0],           p.deformation.f[1],
            p.deformation.f[2],           p.deformation.f[3],
            p.deformation.affine[0],      p.deformation.affine[1],
            p.deformation.affine[2],      p.deformation.affine[3]
        };
        state.hash = hash_bytes(state.hash, mechanics, sizeof mechanics);
        if (parts) {
            parts->particle_position = hash_bytes(
                parts->particle_position,
                &p.position,
                sizeof p.position
            );
            parts->particle_velocity = hash_bytes(
                parts->particle_velocity,
                &p.velocity,
                sizeof p.velocity
            );
            const float parameters[] = { p.mass,
                                         p.radius,
                                         p.absorption,
                                         p.emission,
                                         p.lifetime,
                                         p.fuel,
                                         p.heat,
                                         p.thermal.capacity,
                                         p.thermal.conductivity,
                                         p.base_emission,
                                         p.base_glow,
                                         p.glow,
                                         p.visibility,
                                         p.refraction,
                                         p.grain_radius,
                                         p.style.duration,
                                         p.style.size_end,
                                         p.style.size_exponent,
                                         p.style.emission_end,
                                         p.style.emission_exponent,
                                         p.style.drag,
                                         p.style.drag_end,
                                         p.style.drag_exponent,
                                         p.style.blend };
            parts->particle_parameters = hash_bytes(parts->particle_parameters, parameters, sizeof parameters);
            parts->particle_parameters = hash_bytes(parts->particle_parameters, mechanics, sizeof mechanics);
            parts->particle_appearance = hash_bytes(
                parts->particle_appearance,
                &p.burning,
                sizeof p.burning
            );
            parts->particle_appearance = hash_bytes(
                parts->particle_appearance,
                &p.smoke,
                sizeof p.smoke
            );
            parts->particle_appearance = hash_bytes(
                parts->particle_appearance,
                &p.kind,
                sizeof p.kind
            );
            parts->particle_appearance = hash_bytes(
                parts->particle_appearance,
                &p.color,
                sizeof p.color
            );
            parts->particle_appearance = hash_bytes(
                parts->particle_appearance,
                &p.style.end_color,
                sizeof p.style.end_color
            );
            parts->particle_appearance = hash_bytes(
                parts->particle_appearance,
                &p.base_color,
                sizeof p.base_color
            );
        }
    }
    state.particle_bounds = (fluid_region){ low.x, low.y, high.x - low.x, high.y - low.y };
    if (parts) {
        parts->after_particles = state.hash;
    }
    for (int i = 0; i < acount(materials->records); i++) {
        const material_record *record = &materials->records[i];
        if (!record->mask) {
            continue;
        }
        const material_mask *mask = record->mask;
        uint64_t entity = ecs_entity_to_u64(record->entity);
        int dimensions[] = { mask->width, mask->height };
        state.solids++;
        state.cells += mask->count;
        state.hash = hash_bytes(state.hash, &entity, sizeof entity);
        state.hash = hash_bytes(state.hash, &record->glow_age, sizeof record->glow_age);
        state.hash = hash_bytes(
            state.hash,
            &record->glow_multiplier,
            sizeof record->glow_multiplier
        );
        state
            .hash = hash_bytes(state.hash, &record->initial_cells, sizeof record->initial_cells);
        state.hash = hash_bytes(state.hash, dimensions, sizeof dimensions);
        state.hash = hash_bytes(state.hash, &mask->cell_size, sizeof mask->cell_size);
        state.hash = hash_bytes(state.hash, mask->cells, (size_t)mask->width * mask->height);
        state.hash = hash_bytes(
            state.hash,
            mask->distance,
            (size_t)mask->width * mask->height * sizeof *mask->distance
        );
        if (mask->damage) {
            state.hash = hash_bytes(
                state.hash,
                mask->damage,
                (size_t)mask->width * mask->height * sizeof *mask->damage
            );
        }
    }
    if (parts) {
        parts->after_masks = state.hash;
    }
    for (int i = 0; i < acount(materials->damage_requests); i++) {
        material_damage d = materials->damage_requests[i];
        float values[] = { d.point.x, d.point.y, d.radius, d.energy };
        state.hash = hash_bytes(state.hash, &d.target, sizeof d.target);
        state.hash = hash_bytes(state.hash, &d.cause, sizeof d.cause);
        state.hash = hash_bytes(state.hash, values, sizeof values);
    }
    state.hash = material_destruction_hash(materials->destruction, state.hash);
    if (parts) {
        parts->hash = state.hash;
    }
    return state;
}

material_state materials_inspect(const materials *materials)
{
    return inspect_materials(materials, NULL);
}

material_hash_parts materials_hash_parts(const materials *materials)
{
    material_hash_parts parts = { 0 };
    inspect_materials(materials, &parts);
    return parts;
}

const material_mask *materials_mask(const materials *materials, ecs_entity entity)
{
    if (!materials || entity.slot >= (unsigned)acount(materials->records)) {
        return NULL;
    }
    const material_record *record = &materials->records[entity.slot];
    return ecs_entity_to_u64(record->entity) == ecs_entity_to_u64(entity)
             ? record->mask
             : NULL;
}

bool materials_terrain_volume(const materials *materials, ecs_entity source, materials_depth_volume *out)
{
    if (!materials || source.slot >= (unsigned)acount(materials->records)) {
        return false;
    }
    const material_record *record = &materials->records[source.slot];
    if (ecs_entity_to_u64(record->entity) != ecs_entity_to_u64(source) ||
        !record->depth_volume || !record->depth_revision) {
        return false;
    }
    *out = (materials_depth_volume){ record->depth_volume,
                                     record->depth_removed,
                                     record->depth_identity,
                                     record->depth_revision };
    return true;
}

uint64_t materials_mask_generation(const materials *materials, ecs_entity entity)
{
    return materials_mask(materials, entity)
             ? materials->records[entity.slot].mask_generation
             : 0;
}

bool materials_is_fragment(const materials *materials, ecs_entity entity)
{
    return materials_mask(materials, entity) && materials->records[entity.slot].fragment;
}

bool materials_is_pierceable_fragment(const materials *materials, ecs_entity entity)
{
    if (!materials_is_fragment(materials, entity)) {
        return false;
    }
    return Destructible_get_const(materials->world, entity)->pierceable_fragments;
}

material_record *material_ecs_record(materials *materials, ecs_entity entity)
{
    if (entity.slot >= (unsigned)acount(materials->records)) {
        int previous = acount(materials->records);
        asetlen(materials->records, (int)entity.slot + 1);
        memset(
            materials->records + previous,
            0,
            (size_t)(acount(materials->records) - previous) * sizeof *materials->records
        );
    }
    material_record *record = &materials->records[entity.slot];
    if (ecs_entity_to_u64(record->entity) != ecs_entity_to_u64(entity)) {
        materials->fragments -= record->fragment;
        material_ecs_release_record(record);
        record->entity = entity;
        record->mask_generation = ++materials->next_mask_generation;
        assert(record->mask_generation);
    }
    return record;
}

static const char *configure(materials *materials)
{
    materials->fragment_limit = 0;
    ecs_component_id destruction_id = COMPONENT_MaterialDestructionWorld.id;
    if (ecs_world_component(materials->world, destruction_id)) {
        ecs_query query;
        int result = ecs_query_begin(materials->world, &query, &destruction_id, 1);
        assert(result == ECS_OK);
        (void)result;
        ecs_entity entity;
        int count = 0;
        int debris_limit = 0;
        while (ecs_query_next(&query, &entity)) {
            const MaterialDestructionWorld
                *world = MaterialDestructionWorld_get_const(materials->world, entity);
            materials->fragment_limit = world->fragment_limit;
            debris_limit = world->debris_limit;
            count++;
        }
        ecs_query_end(&query);
        if (count > 1 || materials->fragment_limit < 0 ||
            materials->fragment_limit > 1024 || debris_limit < 0 ||
            debris_limit > 8192) {
            return "Destruction world requires one configuration, 0..1024 "
                   "fragments and 0..8192 debris";
        }
        int capacity = materials->debris ? material_debris_capacity(materials->debris)
                                         : 0;
        if (capacity != debris_limit) {
            material_debris_free(materials->debris);
            materials->debris = debris_limit ? material_debris_new(debris_limit) : NULL;
        }
    }
    return material_ecs_configure_fluid(materials);
}

static const char *create_mask(materials *materials, ecs_entity entity)
{
    const MaterialBreakProfile
        *profile = MaterialBreakProfile_get_const(materials->world, entity);
    if (profile) {
        float values[] = {
            profile->chip_speed,        profile->fragment_speed,
            profile->blast_speed,       profile->release_speed,
            profile->dust_speed_min,    profile->dust_speed_max,
            profile->dust_lifetime_min, profile->dust_lifetime_max,
            profile->dust_size_min,     profile->dust_size_max,
            profile->spark_density,     profile->spark_emission,
            profile->chunk_cells,       profile->chunk_size_min,
            profile->chunk_size_max,    profile->chunk_speed_min,
            profile->chunk_speed_max,   profile->chunk_lifetime,
            profile->smoke_lifetime,    profile->smoke_speed
        };
        for (int i = 0; i < (int)(sizeof values / sizeof *values); i++) {
            if (!isfinite(values[i]) || values[i] < 0) {
                return "Break profile values must be finite and nonnegative";
            }
        }
        if (profile->dust_limit < 0 || profile->dust_limit > 1024 ||
            profile->dust_speed_min > profile->dust_speed_max ||
            profile->dust_lifetime_min > profile->dust_lifetime_max ||
            profile->dust_size_min > profile->dust_size_max ||
            profile->chunk_limit < 0 || profile->chunk_limit > 64 ||
            profile->chunk_size_min > profile->chunk_size_max ||
            profile->chunk_speed_min > profile->chunk_speed_max ||
            profile->smoke_count < 0 || profile->smoke_count > 256 ||
            (profile->chunk_limit &&
             (profile->chunk_cells <= 0 || profile->chunk_size_min <= 0 ||
              profile->chunk_lifetime <= 0))) {
            return "Break profile requires ordered ranges, 0..1024 dust "
                   "particles, "
                   "0..64 chunks with positive size, cells and lifetime, and "
                   "0..256 "
                   "gas parcels";
        }
    }
    const FragmentGlow *glow = FragmentGlow_get_const(materials->world, entity);
    if (glow && (!isfinite(glow->duration + glow->flicker) ||
                 glow->duration < 0 || glow->flicker < 0 || glow->flicker > 1)) {
        return "Fragment glow needs a nonnegative duration and flicker 0..1";
    }
    const Destructible *style = Destructible_get_const(materials->world, entity);
    const Collider *collider = Collider_get_const(materials->world, entity);
    if (!collider || !Transform_get_const(materials->world, entity) ||
        !isfinite(style->cell_size) || style->cell_size <= 0 ||
        style->min_fragment_cells < 1 || !isfinite(style->release_fraction) ||
        style->release_fraction < 0 || style->release_fraction > 1) {
        return "Destructible needs Transform, Collider, a positive cell size "
               "and a minimum fragment size, with release fraction 0..1";
    }
    float width = collider->shape == COLLIDER_CIRCLE ? 2 * collider->radius
                                                     : collider->width;
    float height = collider->shape == COLLIDER_CIRCLE ? 2 * collider->radius
                                                      : collider->height;
    float nx = ceilf(width / style->cell_size), ny = ceilf(height / style->cell_size);
    if (!(nx >= 1 && nx <= 256 && ny >= 1 && ny <= 256)) {
        return "Destructible dimensions must fit 1..256 material cells per "
               "axis";
    }
    const Substance *substance = Substance_get_const(materials->world, entity);
    if (substance && (!isfinite(substance->hardness + substance->blast_multiplier) ||
                      substance->hardness <= 0 || substance->blast_multiplier < 0)) {
        return "Substance needs positive hardness and nonnegative blast "
               "multiplier";
    }
    material_record *record = material_ecs_record(materials, entity);
    if (!record->mask) {
        record->mask = xmalloc(sizeof *record->mask);
        *record->mask = material_mask_new((int)nx, (int)ny, style->cell_size, collider->shape == COLLIDER_CIRCLE);
        const MaterialCavity *cavity = MaterialCavity_get_const(materials->world, entity);
        if (cavity) {
            if (!isfinite(cavity->radius) || cavity->radius <= 0 ||
                cavity->radius >= fminf(width, height) * .5f) {
                return "Material cavity must fit inside its collider";
            }
            material_mask_carve(record->mask, 0, 0, cavity->radius);
        }
        record->initial_cells = record->mask->count;
    }
    return NULL;
}

MaterialBreakProfile material_ecs_break_profile(materials *materials, ecs_entity entity)
{
    const MaterialBreakProfile
        *profile = MaterialBreakProfile_get_const(materials->world, entity);
    return profile ? *profile : (MaterialBreakProfile){ 0 };
}

/* Direction from a crater toward open space: the mean direction to empty
 * cells on two rings around it. Cells outside the mask are unknown. */
static fluid_vec outward(const material_mask *mask, float x, float y)
{
    float nx = 0, ny = 0;
    for (int ring = 1; ring <= 2; ring++) {
        float radius = mask->cell_size * 4 * ring;
        for (int i = 0; i < 16; i++) {
            float angle = i * .39269908f, dx = cosf(angle), dy = sinf(angle);
            float px = floorf((x + dx * radius) / mask->cell_size + mask->width * .5f);
            float py = floorf((y + dy * radius) / mask->cell_size + mask->height * .5f);
            if (px < 0 || py < 0 || px >= mask->width || py >= mask->height ||
                mask->cells[(int)py * mask->width + (int)px]) {
                continue;
            }
            nx += dx;
            ny += dy;
        }
    }
    float length = hypotf(nx, ny);
    return length > 0 ? (fluid_vec){ nx / length, ny / length } : (fluid_vec){ 0 };
}

/* Deterministic jitter in [0, 1) for crater effects. */
static float crater_random(fluid_vec point, int index, int salt)
{
    unsigned h = (unsigned)(int)floorf(point.x * 8) * 0x9e3779b9u ^
                 (unsigned)(int)floorf(point.y * 8) * 0x85ebca6bu ^
                 (unsigned)(index * 4 + salt) * 0xc2b2ae35u;
    h = (h ^ (h >> 16)) * 0x7feb352du;
    h = (h ^ (h >> 15)) * 0x846ca68bu;
    return ((h ^ (h >> 16)) & 0xffffff) / 16777216.0f;
}

/* Grit, sparks, chunks and gas from one crater. Hot strikes throw sparks that
 * start in the striker's light color and cool through the material's spark
 * color. */
static void crater(
    materials *materials,
    const material_event *event,
    fluid_vec normal,
    unsigned color,
    float cell_size,
    uint64_t source,
    MaterialBreakProfile profile
)
{
    int cells = event->dust;
    if (!cells) {
        return;
    }
    fluid_vec point = { event->point.x, event->point.y };
    bool facing = normal.x != 0 || normal.y != 0;
    particle_spray base = {
        .position = { point.x, point.y },
        .direction = { facing ? normal.x : 1, facing ? normal.y : 0 },
        .speed_min = cell_size * profile.dust_speed_min,
        .speed_max = cell_size * profile.dust_speed_max,
        .lifetime_min = profile.dust_lifetime_min,
        .lifetime_max = profile.dust_lifetime_max,
        .size_min = cell_size * profile.dust_size_min,
        .size_max = cell_size * profile.dust_size_max,
        .drag_min = .8f,
        .drag_max = 1.8f,
        .size_exponent = 1,
        .spread = facing ? 2.2f : 6.2831853f,
        .antialias = 1.5f,
    };
    if (materials->particles && profile.dust_limit > 0) {
        particle_spray grit = base;
        unsigned tint = profile.dust_color ? profile.dust_color : color;
        grit.color = tint;
        grit.end_color = tint & 0xffffff00u;
        grit.count = cells < profile.dust_limit ? cells : profile.dust_limit;
        grit.shape = PARTICLE_TRIANGLE;
        particles_emit_directed(materials->particles, &grit);
        int sparks = (int)(cells * profile.spark_density * event->impact.heat + .5f);
        if (sparks > 0) {
            /* Start halfway between the striker's light and the material's
             * incandescence, then cool to the material's color. */
            unsigned hot = profile.spark_color, light = event->impact.color | 255u;
            unsigned start = 255u;
            for (int shift = 8; shift < 32; shift += 8) {
                unsigned a = (hot >> shift) & 255u, b = (light >> shift) & 255u;
                start |= ((a + b) / 2) << shift;
            }
            particle_spray spark = base;
            spark.color = event->impact.color ? start : hot;
            spark.end_color = hot & 0xffffff00u;
            spark.count = sparks < profile.dust_limit * 2 ? sparks
                                                          : profile.dust_limit * 2;
            spark.speed_min *= 1.5f;
            spark.speed_max *= 2;
            spark.size_min *= .3f;
            spark.size_max *= .3f;
            spark.drag_min = .4f;
            spark.drag_max = 1.2f;
            spark.emission_min = profile.spark_emission * .5f;
            spark.emission_max = profile.spark_emission;
            spark.emission_exponent = .5f;
            spark.stretch = .012f;
            spark.antialias = 0;
            spark.shape = PARTICLE_STREAK;
            particles_emit_directed(materials->particles, &spark);
        }
    }
    if (facing && profile.chunk_limit > 0 && materials->debris) {
        material_ecs_throw_chunks(
            materials,
            point,
            normal,
            cells,
            source,
            event->impact.heat,
            profile
        );
    }
    for (int i = 0; i < profile.smoke_count && facing; i++) {
        /* Gas leaves the crater along its normal and spreads as it slows. */
        float along = 1 + 4 * crater_random(point, i, 0);
        float side = (crater_random(point, i, 1) - .5f) * 8;
        float speed = profile.smoke_speed * (.3f + .7f * crater_random(point, i, 2));
        float swirl = (crater_random(point, i, 3) - .5f) * profile.smoke_speed;
        float lifetime = profile.smoke_lifetime *
                         (.7f + .6f * crater_random(point, i, 4));
        materials_emit(
            materials,
            (fluid_particle){
                .position = { point.x + normal.x * along - normal.y * side,
                              point.y + normal.y * along + normal.x * side },
                .velocity = { normal.x * speed - normal.y * swirl,
                              normal.y * speed + normal.x * swirl },
                .mass = .0004f,
                .radius = 3 + 3 * crater_random(point, i, 5),
                .color = profile.smoke_color,
                .kind = FLUID_GAS,
                .absorption = .012f,
                .lifetime = lifetime,
                .style = { .duration = lifetime,
                           .size_end = 2.6f,
                           .size_exponent = .5f,
                           .drag = 1.2f,
                           .drag_end = 3,
                           .drag_exponent = 1,
                           .end_color = profile.smoke_color & 0xffffff00u } }
        );
    }
}

void material_ecs_add_component(
    ecs_world *world,
    ecs_entity entity,
    const ecs_component_desc *component,
    const void *value
)
{
    if (!ecs_world_component(world, component->id)) {
        int result = ecs_world_register_component(world, component, NULL);
        assert(result == ECS_OK);
        (void)result;
    }
    int result = ecs_world_record_add(world, entity, component->id, value, 0);
    assert(result == ECS_OK);
    (void)result;
}

static void detach(materials *materials, ecs_entity source, material_mask mask, fluid_vec hit, float kick)
{
    /* Moving fragments no longer share their terrain source's neighbors. */
    if (mask.halo_radius) {
        material_mask_set_halo(&mask, (material_mask_halo){ 0 });
    }
    ecs_world *world = materials->world;
    /* Copy before entity growth can relocate component columns. The fragment
     * is a root, so it starts at its source's world placement. */
    GlobalTransform2D pose = transform_world_2d(world, source);
    Transform transform = { pose.position.x, pose.position.y };
    Collider collider = *Collider_get_const(world, source);
    Destructible destructible = *Destructible_get_const(world, source);
    destructible.release_fraction = 0;
    const Velocity *movement = Velocity_get_const(world, source);
    Velocity velocity = movement ? *movement : (Velocity){ 0 };
    Orientation orientation = { pose.radians };
    const Appearance *look = Appearance_get_const(world, source);
    Appearance appearance = look ? *look : (Appearance){ .color = 0xffffffffu };
    const Material *surface = Material_get_const(world, source);
    Material material = surface ? *surface : (Material){ 0 };
    const Shader *binding = Shader_get_const(world, source);
    Shader shader = binding ? *binding : (Shader){ 0 };
    bool has_shader = binding != NULL;
    const MaterialSurface3D *mesh_ptr = MaterialSurface3D_get_const(world, source);
    bool has_mesh = mesh_ptr != NULL;
    MaterialSurface3D mesh = mesh_ptr ? *mesh_ptr : (MaterialSurface3D){ 0 };
    /* A detached piece is loose rock, not the cave interior behind a contour. */
    mesh.recess_width = mesh.recess_depth = mesh.recess_smoothing = 0;
    /* Rock with a terrain volume behind it has depth: a piece breaking off is
     * about as deep as it is wide, up to the carve's slab either side,
     * straddling the plane where the section cap faces the camera. A piece
     * of a piece already sits half its source's depth above the plane. */
    const material_record *from = &materials->records[source.slot];
    ecs_entity terrain = from->terrain_source;
    const material_record *rock = terrain.slot < (unsigned)acount(materials->records)
                                    ? &materials->records[terrain.slot]
                                    : NULL;
    if (rock && ecs_entity_to_u64(rock->entity) == ecs_entity_to_u64(terrain) &&
        rock->depth_volume) {
        float side = sqrtf((float)mask.count) * mask.cell_size;
        float
            deep = fminf(fmaxf(side, 2 * mask.cell_size), 2 * rock->depth_volume->slab);
        float plane = mesh.elevation - (from->fragment ? mesh.depth / 2 : 0);
        mesh.elevation = plane + deep / 2;
        mesh.depth = deep;
    }
    /* The fragment's local frame is its source's, so it inherits where that
     * frame sat when the material was laid out. */
    const MaterialFrame *laid_out = MaterialFrame_get_const(world, source);
    MaterialFrame frame = laid_out ? *laid_out
                                   : (MaterialFrame){ transform.x,
                                                      transform.y,
                                                      orientation.radians };
    const Substance *substance_ptr = Substance_get_const(world, source);
    Substance substance = substance_ptr ? *substance_ptr : (Substance){ 0 };
    const Reflection *reflection_ptr = Reflection_get_const(world, source);
    Reflection reflection = reflection_ptr ? *reflection_ptr : (Reflection){ 0 };
    const FragmentGlow *glow_ptr = FragmentGlow_get_const(world, source);
    FragmentGlow glow = glow_ptr ? *glow_ptr : (FragmentGlow){ 0 };
    const Body *source_body = Body_get_const(world, source);
    bool allow_rotation = source_body && source_body->allow_rotation;
    float cx = 0, cy = 0;
    for (int i = 0; i < acount(mask.rects); i++) {
        material_rect r = mask.rects[i];
        float area = r.width * r.height;
        cx += r.x * area;
        cy += r.y * area;
    }
    float area = mask.count * mask.cell_size * mask.cell_size;
    cx /= area;
    cy /= area;
    float c = cosf(orientation.radians), s = sinf(orientation.radians);
    /* Throw chips out of the source hull, rather than driving an edge chip
     * back into its parent from the projectile's surface contact. */
    float dx = c * cx - s * cy, dy = s * cx + c * cy;
    /* Float products fit in double; sqrt shares the mask's rounded norm. */
    float distance = (float)sqrt((double)dx * dx + (double)dy * dy);
    if (distance < .001f) {
        dx = transform.x - hit.x;
        dy = transform.y - hit.y;
        distance = (float)sqrt((double)dx * dx + (double)dy * dy);
    }
    if (distance < .001f) {
        dx = 1;
        dy = 0;
        distance = 1;
    }
    velocity.x += dx / distance * kick;
    velocity.y += dy / distance * kick;
    ecs_entity entity = ecs_world_create_entity(world);
    material_ecs_add_component(world, entity, &COMPONENT_Transform, &transform);
    material_ecs_add_component(world, entity, &COMPONENT_Velocity, &velocity);
    material_ecs_add_component(world, entity, &COMPONENT_Orientation, &orientation);
    material_ecs_add_component(world, entity, &COMPONENT_Collider, &collider);
    material_ecs_add_component(
        world,
        entity,
        &COMPONENT_Body,
        &(Body){ .type = BODY_DYNAMIC, .allow_rotation = allow_rotation }
    );
    material_ecs_add_component(world, entity, &COMPONENT_Destructible, &destructible);
    material_ecs_add_component(world, entity, &COMPONENT_Appearance, &appearance);
    material_ecs_add_component(world, entity, &COMPONENT_Material, &material);
    if (has_mesh) {
        material_ecs_add_component(world, entity, &COMPONENT_MaterialSurface3D, &mesh);
    }
    if (substance.hardness > 0) {
        material_ecs_add_component(world, entity, &COMPONENT_Substance, &substance);
    }
    if (reflection.strength > 0) {
        material_ecs_add_component(world, entity, &COMPONENT_Reflection, &reflection);
    }
    if (has_shader) {
        material_ecs_add_component(world, entity, &COMPONENT_Shader, &shader);
    }
    if (has_shader || has_mesh) {
        material_ecs_add_component(world, entity, &COMPONENT_MaterialFrame, &frame);
    }
    if (glow.duration > 0) {
        material_ecs_add_component(world, entity, &COMPONENT_FragmentGlow, &glow);
    }
    MaterialBreakProfile profile = material_ecs_break_profile(materials, source);
    material_ecs_add_component(world, entity, &COMPONENT_MaterialBreakProfile, &profile);
    int result = ecs_world_apply_commands(world);
    assert(result == ECS_OK);
    (void)result;
    material_record *record = material_ecs_record(materials, entity);
    record->fragment = true;
    record->terrain_source = materials->records[source.slot].terrain_source;
    record->initial_cells = mask.count;
    record->glow_multiplier = material.emission_multiplier;
    record->mask = xmalloc(sizeof *record->mask);
    *record->mask = mask;
    materials->fragments++;
}

void materials_damage(
    materials *materials,
    ecs_entity entity,
    fluid_vec point,
    float radius,
    float energy,
    int cause,
    material_impact impact
)
{
    assert(isfinite(point.x) && isfinite(point.y) && isfinite(radius) && radius >= 0);
    assert(isfinite(energy) && energy >= 0 && isfinite(impact.heat) && impact.heat >= 0);
    apush(
        materials->damage_requests,
        ((
            material_damage
        ){ ecs_entity_to_u64(entity), { point.x, point.y }, radius, energy, cause, impact })
    );
}

static void queue_damage(materials *materials, material_damage damage)
{
    ecs_entity entity = ecs_entity_from_u64(damage.target);
    fluid_vec point = { damage.point.x, damage.point.y };
    float radius = damage.radius;
    assert(isfinite(point.x) && isfinite(point.y) && isfinite(radius) && radius >= 0);
    const material_mask *target = materials_mask(materials, entity);
    uint64_t terrain = target && !materials->records[entity.slot].fragment
                         ? ecs_entity_to_u64(materials->records[entity.slot].terrain_source)
                         : 0;
    if (MaterialTerrain_get_const(materials->world, entity)) {
        terrain = ecs_entity_to_u64(entity);
    }
    if (!terrain) {
        material_destruction_queue_damage(materials->destruction, damage);
        return;
    }
    /* A world-space blast crosses storage boundaries without clipping its disk. */
    for (int i = 0; i < acount(materials->records); i++) {
        material_record record = materials->records[i];
        if (record.fragment || ecs_entity_to_u64(record.terrain_source) != terrain ||
            !record.mask || !ecs_entity_is_alive(materials->world, record.entity)) {
            continue;
        }
        CF_V2 p = transform_world_2d(materials->world, record.entity).position;
        float dx = fmaxf(
            0,
            fabsf(point.x - p.x) - record.mask->width * record.mask->cell_size * .5f
        );
        float dy = fmaxf(
            0,
            fabsf(point.y - p.y) - record.mask->height * record.mask->cell_size * .5f
        );
        if (dx * dx + dy * dy <= radius * radius) {
            material_damage chunk = damage;
            chunk.target = ecs_entity_to_u64(record.entity);
            material_destruction_queue_damage(materials->destruction, chunk);
        }
    }
}

static bool read_source(void *user, uint64_t target, material_source *out)
{
    materials *materials = user;
    ecs_entity entity = ecs_entity_from_u64(target);
    const material_mask *mask = materials_mask(materials, entity);
    if (!mask || !mask->count || !ecs_entity_is_alive(materials->world, entity) ||
        entities_is_retiring(materials->entities, entity)) {
        return false;
    }
    const Destructible *style = Destructible_get_const(materials->world, entity);
    const Substance *substance = Substance_get_const(materials->world, entity);
    GlobalTransform2D pose = transform_world_2d(materials->world, entity);
    material_record *record = &materials->records[entity.slot];
    MaterialBreakProfile profile = material_ecs_break_profile(materials, entity);
    *out = (material_source){
        .mask = record->mask,
        .position = { pose.position.x, pose.position.y },
        .angle = pose.radians,
        .hardness = substance ? substance->hardness : 1,
        .blast_multiplier = substance ? substance->blast_multiplier : 1,
        .release_fraction = style->release_fraction,
        .initial_cells = record->initial_cells,
        .minimum = style->min_fragment_cells,
        .retain_chips = substance && substance->retain_chips,
        .anchored = !record->fragment && ecs_entity_to_u64(record->terrain_source) != 0,
        .chip_speed = profile.chip_speed,
        .fragment_speed = profile.fragment_speed,
        .blast_speed = profile.blast_speed,
        .release_speed = profile.release_speed
    };
    return true;
}

static void create_fragment(void *user, uint64_t source, material_fragment fragment)
{
    materials *materials = user;
    material_record *record = &materials->records[ecs_entity_from_u64(source).slot];
    bool terrain = !record->fragment && ecs_entity_to_u64(record->terrain_source);
    if (record->mask && !record->mask->width && !record->streamed_mask && !terrain) {
        free(record->mask);
        record->mask = NULL;
    }
    detach(
        user,
        ecs_entity_from_u64(source),
        fragment.mask,
        (fluid_vec){ fragment.hit.x, fragment.hit.y },
        fragment.speed
    );
}

static void destroy_source(void *user, uint64_t target)
{
    materials *materials = user;
    material_record *record = material_ecs_record(materials, ecs_entity_from_u64(target));
    if (!record->fragment && ecs_entity_to_u64(record->terrain_source)) {
        /* Empty terrain remains authoritative. Its neighbors need air samples,
         * and a checkpoint must not regenerate a released chunk as solid. A
         * release may have transferred the entire mask into a moving fragment. */
        assert(record->mask);
        if (!record->mask->width) {
            const Collider
                *collider = Collider_get_const(materials->world, record->entity);
            const Destructible
                *style = Destructible_get_const(materials->world, record->entity);
            int width = (int)lroundf(collider->width / style->cell_size);
            int height = (int)lroundf(collider->height / style->cell_size);
            unsigned char *cells = xcalloc((size_t)width * height, 1);
            *record->mask = material_mask_from_cells(width, height, style->cell_size, cells);
            free(cells);
        }
        assert(record->mask->count == 0);
        return;
    }
    entities_request_destroy(materials->entities, ecs_entity_from_u64(target));
}

/* Physics and render adapters consume mask revision at this boundary. */
static void invalidate_geometry(void *, uint64_t) {}

static void material_result(void *user, const material_event *event)
{
    if (event->kind != MATERIAL_EVENT_DAMAGE) {
        return;
    }
    materials *materials = user;
    ecs_entity entity = ecs_entity_from_u64(event->target);
    ecs_world *world = materials->world;
    const Appearance *appearance = Appearance_get_const(world, entity);
    const material_mask *mask = materials_mask(materials, entity);
    /* Find the crater's open side in the mask's local frame, then rotate back. */
    GlobalTransform2D pose = transform_world_2d(world, entity);
    float c = cosf(pose.radians), s = sinf(pose.radians);
    float dx = event->point.x - pose.position.x,
          dy = event->point.y - pose.position.y;
    fluid_vec local = outward(mask, c * dx + s * dy, c * dy - s * dx);
    /* Terrain chunks unload; their authored source keeps the material. */
    ecs_entity terrain = materials->records[entity.slot].terrain_source;
    uint64_t source = ecs_entity_to_u64(terrain) &&
                              !materials->records[entity.slot].fragment
                        ? ecs_entity_to_u64(terrain)
                        : event->target;
    crater(
        materials,
        event,
        (fluid_vec){ c * local.x - s * local.y, s * local.x + c * local.y },
        appearance ? appearance->color : 0xffffffffu,
        mask->cell_size,
        source,
        material_ecs_break_profile(materials, entity)
    );
}

void materials_release(materials *materials, ecs_entity entity)
{
    material_destruction_queue_release(materials->destruction, ecs_entity_to_u64(entity));
}

void material_ecs_fade_fragments(materials *materials, float delta)
{
    for (int i = 0; i < acount(materials->records); i++) {
        material_record *record = &materials->records[i];
        if (!record->fragment) {
            continue;
        }
        const FragmentGlow
            *glow = FragmentGlow_get_const(materials->world, record->entity);
        Material *surface = Material_get(materials->world, record->entity);
        if (!glow || !surface || glow->duration <= 0) {
            continue;
        }
        record->glow_age = fminf(glow->duration, record->glow_age + delta);
        float remaining = 1 - record->glow_age / glow->duration;
        float phase = record->glow_age * 29 + record->entity.slot * 2.399963f;
        float pulse = fmaxf(
            0,
            cos_sin_compute(phase).s * cos_sin_compute(phase * .37f).s
        );
        float power = remaining * remaining * (1 - glow->flicker * pulse);
        surface->emission_multiplier = record->glow_multiplier * power;
    }
}

const char *materials_prepare(materials *materials)
{
    const char *error = configure(materials);
    if (error) {
        return error;
    }
    for (int i = 0; i < acount(materials->records); i++) {
        material_record *record = &materials->records[i];
        if (ecs_entity_to_u64(record->terrain_source) &&
            !MaterialTerrain_get_const(materials->world, record->terrain_source) &&
            ecs_entity_is_alive(materials->world, record->entity)) {
            int result = ecs_world_destroy_entity(materials->world, record->entity);
            assert(result == ECS_OK);
            (void)result;
        }
        if (!ecs_entity_is_alive(materials->world, record->entity)) {
            materials->fragments -= record->fragment;
            material_ecs_release_record(record);
            continue;
        }
        if (record->mask && !Destructible_get_const(materials->world, record->entity)) {
            materials->fragments -= record->fragment;
            material_ecs_release_record(record);
        }
        if (record->terrain &&
            !MaterialTerrain_get_const(materials->world, record->entity)) {
            material_ecs_release_record(record);
        }
    }
    error = material_ecs_generate_terrains(materials);
    if (error) {
        return error;
    }
    ecs_component_id ids[] = { COMPONENT_Destructible.id, COMPONENT_FluidVolume.id };
    for (int i = 0; i < (materials->fluid ? 2 : 1); i++) {
        if (!ecs_world_component(materials->world, ids[i])) {
            continue;
        }
        ecs_query query;
        int result = ecs_query_begin(materials->world, &query, &ids[i], 1);
        assert(result == ECS_OK);
        (void)result;
        ecs_entity entity;
        while (ecs_query_next(&query, &entity)) {
            if (entities_is_retiring(materials->entities, entity)) {
                continue;
            }
            error = i == 0 ? create_mask(materials, entity)
                           : material_ecs_fill_volume(materials, entity);
            if (error) {
                break;
            }
        }
        ecs_query_end(&query);
        if (error) {
            return error;
        }
    }
    /* Resolve authored sources after baking, including damage queued by Begin. */
    for (int i = 0; i < acount(materials->damage_requests); i++) {
        queue_damage(materials, materials->damage_requests[i]);
    }
    aclear(materials->damage_requests);
    material_host host = { .user = materials,
                           .read_source = read_source,
                           .create_fragment = create_fragment,
                           .destroy_source = destroy_source,
                           .invalidate_geometry = invalidate_geometry,
                           .result = material_result,
                           .cut_surface = material_ecs_queue_terrain_cut };
    material_destruction_step(
        materials->destruction,
        &host,
        materials->fragment_limit > materials->fragments
            ? materials->fragment_limit - materials->fragments
            : 0
    );
    material_ecs_apply_terrain_cuts(materials);
    material_ecs_sync_terrain_surfaces(materials);
    return NULL;
}

int materials_dispatch_events(materials *materials)
{
    material_event event;
    while (material_destruction_poll_event(materials->destruction, &event)) {
        CF_V2 point = { event.point.x, event.point.y };
        ecs_entity target = ecs_entity_from_u64(event.target);
        pin_value values[] = {
            { .type = PIN_F32, .f = event.energy },
            { .type = PIN_I32, .i = event.cause },
            { .type = PIN_STRUCT, .type_name = "vec2", .ptr = &point },
            { .type = PIN_I32, .i = event.kind },
            { .type = PIN_I32, .i = event.removed },
            { .type = PIN_I32, .i = event.dust },
            { .type = PIN_HANDLE, .type_name = "ecs.entity", .u64 = event.target }
        };
        bool impact = event.kind == MATERIAL_EVENT_BODY_IMPACT;
        if (!impact && target.slot < (unsigned)acount(materials->records)) {
            material_record *record = &materials->records[target.slot];
            if (ecs_entity_to_u64(record->entity) == event.target &&
                record->terrain_source.slot) {
                target = record->terrain_source;
            }
        }
        int result = entities_request_event(
            materials->entities,
            target,
            impact ? "Material Impact" : "Material Result",
            (execution_arguments){ values, impact ? 3 : 7 }
        );
        if (result < 0) {
            return result;
        }
    }
    return 0;
}

material_fields *materials_fields(materials *materials)
{
    return materials->fields;
}
