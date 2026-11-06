#include "runtime/behaviors/entities.h"
#include "runtime/material_checkpoint.h"
#include "runtime/material_ecs.h"
#include "runtime/material_terrain.h"
#include "runtime/task_pool.h"
#include "runtime/transform_propagation.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t hash_bytes(uint64_t hash, const void *data, size_t count)
{
    const unsigned char *bytes = data;
    for (size_t i = 0; i < count; i++) {
        hash = (hash ^ bytes[i]) * UINT64_C(1099511628211);
    }
    return hash;
}

static bool terrain_recipe(
    materials *materials,
    MaterialTerrain terrain,
    uint64_t *fingerprint,
    char *error,
    size_t size
)
{
    field_context context;
    if (!material_field_context(
            materials->fields,
            terrain.field,
            terrain.parameters,
            terrain.seed,
            &context,
            error,
            size
        )) {
        return false;
    }
    return material_checkpoint_fingerprint(
        (material_checkpoint_recipe){
            .fields = materials->fields,
            .field = terrain.field,
            .context = context,
            .grid = { .width = (int)ceilf(terrain.width / terrain.cell_size),
                      .height = (int)ceilf(terrain.height / terrain.cell_size),
                      .cell_size = terrain.cell_size },
            .connectivity = terrain.connectivity,
            .border_cells = terrain.border_cells,
            .island_cells = terrain.island_cells,
            .streamed = terrain.stream_radius > 0 },
        fingerprint,
        error,
        size
    );
}

static void spawn_tile(
    materials *materials,
    ecs_entity source,
    MaterialTerrain terrain,
    Transform origin,
    field_grid grid,
    material_mask *mask,
    uint64_t key,
    bool streamed
)
{
    ecs_world *world = materials->world;
    /* Snapshot the controlled component bundle before entity growth. */
    const Appearance *look = Appearance_get_const(world, source);
    Appearance appearance = look ? *look : (Appearance){ .color = 0xffffffffu };
    const Material *surface = Material_get_const(world, source);
    Material material = surface ? *surface : (Material){ 0 };
    const Shader *binding = Shader_get_const(world, source);
    bool has_shader = binding != NULL;
    Shader shader = binding ? *binding : (Shader){ 0 };
    const MaterialSurface3D *mesh_ptr = MaterialSurface3D_get_const(world, source);
    bool has_mesh = mesh_ptr != NULL;
    MaterialSurface3D mesh = mesh_ptr ? *mesh_ptr : (MaterialSurface3D){ 0 };
    const MaterialFrame *frame_ptr = MaterialFrame_get_const(world, source);
    MaterialFrame mesh_frame = frame_ptr ? *frame_ptr
                                         : (MaterialFrame){ origin.x, origin.y, 0 };
    float frame_c = cosf(mesh_frame.radians), frame_s = sinf(mesh_frame.radians);
    mesh_frame.x += frame_c * grid.center_x - frame_s * grid.center_y;
    mesh_frame.y += frame_s * grid.center_x + frame_c * grid.center_y;
    const Substance *sub = Substance_get_const(world, source);
    Substance substance = sub ? *sub : (Substance){ 0 };
    const Reflection *ref = Reflection_get_const(world, source);
    Reflection reflection = ref ? *ref : (Reflection){ 0 };
    MaterialBreakProfile profile = material_ecs_break_profile(materials, source);
    ecs_entity entity = ecs_world_create_entity(world);
    Transform transform = { origin.x + grid.center_x, origin.y + grid.center_y };
    Collider collider = { .shape = COLLIDER_BOX,
                          .width = grid.width * terrain.cell_size,
                          .height = grid.height * terrain.cell_size,
                          .category = terrain.category,
                          .mask = terrain.mask };
    Destructible destructible = { .cell_size = terrain.cell_size,
                                  .min_fragment_cells = terrain.min_fragment_cells };
    appearance.width = collider.width;
    appearance.height = collider.height;
    material_ecs_add_component(world, entity, &COMPONENT_Transform, &transform);
    material_ecs_add_component(world, entity, &COMPONENT_Collider, &collider);
    material_ecs_add_component(world, entity, &COMPONENT_Destructible, &destructible);
    material_ecs_add_component(world, entity, &COMPONENT_Appearance, &appearance);
    material_ecs_add_component(world, entity, &COMPONENT_Material, &material);
    if (has_mesh) {
        material_ecs_add_component(world, entity, &COMPONENT_MaterialSurface3D, &mesh);
        material_ecs_add_component(world, entity, &COMPONENT_MaterialFrame, &mesh_frame);
    }
    material_ecs_add_component(world, entity, &COMPONENT_MaterialBreakProfile, &profile);
    if (substance.hardness > 0) {
        material_ecs_add_component(world, entity, &COMPONENT_Substance, &substance);
    }
    if (reflection.strength > 0) {
        material_ecs_add_component(world, entity, &COMPONENT_Reflection, &reflection);
    }
    if (has_shader) {
        material_ecs_add_component(world, entity, &COMPONENT_Shader, &shader);
    }
    int result = ecs_world_apply_commands(world);
    assert(result == ECS_OK);
    (void)result;
    material_record *record = material_ecs_record(materials, entity);
    record->terrain_source = source;
    record->mask = mask;
    record->streamed_mask = streamed;
    record->terrain_key = key;
    record->initial_cells = record->mask->count;
}

/* Distinguishes successive depth volumes across resets and address reuse. */
static _Atomic uint64_t next_depth_identity = 1;

/* Why an enabled TerrainVolume cannot bake, or NULL. */
static const char *depth_volume_error(const TerrainVolume *style)
{
    float coarse = style->voxel * style->refine;
    if (!(isfinite(style->voxel) && style->voxel > 0 && style->refine >= 1 &&
          style->refine <= TERRAIN_VOLUME_REFINE_MAX &&
          isfinite(style->depth) && style->depth >= 2 * coarse &&
          isfinite(style->elevation) && isfinite(style->front) &&
          style->front >= 0 && isfinite(style->slab) && style->slab > 0)) {
        return "TerrainVolume requires a positive voxel, refine from 1 to 8, "
               "depth of at least two coarse voxels, a nonnegative front, a "
               "positive slab and a finite elevation";
    }
    return NULL;
}

/* The next surface sync carves the whole plane again from the live mask. */
static void recarve_depth_volume(material_record *record)
{
    free(record->depth_open);
    record->depth_open = NULL;
    record->depth_hash = 0;
}

/* Samples the terrain's own field in depth on its plane into *out, which
 * stays NULL without an enabled TerrainVolume. The mask stays the gameplay
 * slice; the carve follows it in material_ecs_sync_terrain_surfaces. */
static const char *bake_depth_volume(
    materials *materials,
    ecs_entity source,
    const MaterialTerrain *terrain,
    field_context context,
    terrain_volume **out
)
{
    *out = NULL;
    const TerrainVolume *style = TerrainVolume_get_const(materials->world, source);
    if (!style || !style->enabled) {
        return NULL;
    }
    assert(!depth_volume_error(style));
    float coarse = style->voxel * style->refine;
    int front = (int)ceilf(style->front / coarse);
    field_grid plane = { .width = (int)ceilf(terrain->width / coarse) + 1,
                         .height = (int)ceilf(terrain->height / coarse) + 1,
                         .cell_size = coarse };
    terrain_volume volume;
    terrain_volume_shape shape = { plane,
                                   front,
                                   front + (int)ceilf(style->depth / coarse) + 1,
                                   style->refine,
                                   style->open_back,
                                   style->slab };
    task_pool_begin(materials->pool, true);
    bool baked = terrain_volume_bake(
        material_field_program(materials->fields, terrain->field),
        context,
        shape,
        task_pool_tasks(materials->pool),
        &volume,
        materials->error,
        sizeof materials->error
    );
    task_pool_end(materials->pool);
    if (!baked) {
        return materials->error;
    }
    *out = xmalloc(sizeof volume);
    **out = volume;
    return NULL;
}

/* Gives the source a freshly baked volume, carved before it is reported. */
static void attach_depth_volume(material_record *record, terrain_volume *volume)
{
    material_ecs_discard_depth_volume(record);
    if (!volume) {
        return;
    }
    record->depth_volume = volume;
    record->depth_removed = xcalloc(
        (size_t)volume->plane.width * volume->plane.height,
        sizeof *record->depth_removed
    );
    record->depth_identity = atomic_fetch_add_explicit(&next_depth_identity, 1, memory_order_relaxed);
}

/* Releases the source's tiles, fragments and stream; its depth volume stays
 * for the caller to keep or replace. */
static void discard_terrain(materials *materials, ecs_entity source)
{
    for (int i = 0; i < acount(materials->records); i++) {
        material_record *r = &materials->records[i];
        if (ecs_entity_to_u64(r->terrain_source) != ecs_entity_to_u64(source)) {
            continue;
        }
        if (ecs_entity_is_alive(materials->world, r->entity)) {
            int result = ecs_world_destroy_entity(materials->world, r->entity);
            assert(result == ECS_OK);
            (void)result;
        }
        materials->fragments -= r->fragment;
        material_ecs_release_record(r);
    }
    material_record *r = material_ecs_record(materials, source);
    if (r->stream) {
        material_stream_free(r->stream);
        r->stream = NULL;
    }
}

static const char *stream_terrain(
    materials *materials,
    ecs_entity source,
    MaterialTerrain terrain,
    Transform origin,
    uint64_t hash
)
{
    material_record *record = material_ecs_record(materials, source);
    if (record->terrain_hash != hash) {
        field_context context;
        if (!material_field_context(
                materials->fields,
                terrain.field,
                terrain.parameters,
                terrain.seed,
                &context,
                materials->error,
                sizeof materials->error
            )) {
            return materials->error;
        }
        discard_terrain(materials, source);
        record = material_ecs_record(materials, source);
        material_ecs_discard_depth_volume(record);
        record->stream = material_stream_new((material_stream_desc){
            .grid = { .width = (int)ceilf(terrain.width / terrain.cell_size),
                      .height = (int)ceilf(terrain.height / terrain.cell_size),
                      .cell_size = terrain.cell_size },
            .field = terrain.field,
            .context = context,
            .island_cells = terrain.island_cells,
            .tasks = task_pool_tasks(materials->pool) });
        record->terrain = true;
        record->terrain_hash = hash;
    }
    material_stream *stream = record->stream;
    aclear(materials->terrain_views);
    ecs_world *world = materials->world;
    ecs_component_id ids[] = { COMPONENT_Camera.id, COMPONENT_Transform.id };
    if (ecs_world_component(world, ids[0])) {
        ecs_query query;
        int result = ecs_query_begin(world, &query, ids, 2);
        assert(result == ECS_OK);
        (void)result;
        ecs_entity e;
        while (ecs_query_next(&query, &e)) {
            const Camera *camera = Camera_get_const(world, e);
            if (camera->disabled) {
                continue;
            }
            CF_V2 p = transform_world_2d(world, e).position;
            float seen = fmaxf(camera->width, camera->height) * .5f;
            apush(
                materials->terrain_views,
                ((material_stream_view){ p.x + camera->offset.x - origin.x,
                                         p.y + camera->offset.y - origin.y,
                                         terrain.stream_radius + seen,
                                         materials->stream_deferred ? 0 : seen })
            );
        }
        ecs_query_end(&query);
    }
    for (int i = 0; i < acount(materials->host_views); i++) {
        material_stream_view view = materials->host_views[i];
        view.x -= origin.x;
        view.y -= origin.y;
        apush(materials->terrain_views, view);
    }
    /* A moving body can retain a local interaction outside the camera buffer. */
    ecs_component_id moving[] = { COMPONENT_Body.id,
                                  COMPONENT_Transform.id,
                                  COMPONENT_Velocity.id };
    if (ecs_world_component(world, moving[0]) &&
        ecs_world_component(world, moving[2])) {
        ecs_query query;
        int result = ecs_query_begin(world, &query, moving, 3);
        assert(result == ECS_OK);
        (void)result;
        ecs_entity e;
        while (ecs_query_next(&query, &e)) {
            const Velocity *velocity = Velocity_get_const(world, e);
            if (fabsf(velocity->x) + fabsf(velocity->y) < 1) {
                continue;
            }
            CF_V2 p = transform_world_2d(world, e).position;
            apush(
                materials->terrain_views,
                ((material_stream_view){ p.x - origin.x,
                                         p.y - origin.y,
                                         128,
                                         materials->stream_deferred ? 0 : 64 })
            );
        }
        ecs_query_end(&query);
    }
    task_pool_begin(materials->pool, true);
    bool ok = material_stream_update(
        stream,
        materials->fields,
        materials->terrain_views,
        acount(materials->terrain_views),
        terrain.stream_budget,
        materials->error,
        sizeof materials->error
    );
    task_pool_end(materials->pool);
    aclear(materials->terrain_views);
    int count;
    const material_stream_tile *tiles = material_stream_tiles(stream, &count);
    for (int i = 0; i < acount(materials->records); i++) {
        material_record *r = &materials->records[i];
        if (!r->streamed_mask ||
            ecs_entity_to_u64(r->terrain_source) != ecs_entity_to_u64(source)) {
            continue;
        }
        bool found = false;
        for (int j = 0; j < count; j++) {
            found |= tiles[j].key == r->terrain_key;
        }
        if (!found) {
            if (ecs_entity_is_alive(world, r->entity)) {
                int result = ecs_world_destroy_entity(world, r->entity);
                assert(result == ECS_OK);
                (void)result;
            }
            material_ecs_release_record(r);
        }
    }
    if (!ok) {
        return materials->error;
    }
    for (int i = 0; i < count; i++) {
        if (!tiles[i].mask->count) {
            continue;
        }
        bool found = false;
        for (int j = 0; j < acount(materials->records); j++) {
            material_record *r = &materials->records[j];
            found |= r->streamed_mask && r->terrain_key == tiles[i].key &&
                     ecs_entity_to_u64(r->terrain_source) == ecs_entity_to_u64(source);
        }
        if (!found) {
            spawn_tile(
                materials,
                source,
                terrain,
                origin,
                tiles[i].grid,
                tiles[i].mask,
                tiles[i].key,
                true
            );
        }
    }
    return NULL;
}

static const char *generate_terrain(materials *materials, ecs_entity source)
{
    ecs_world *world = materials->world;
    MaterialTerrain terrain = *MaterialTerrain_get_const(world, source);
    if (!Transform_get_const(world, source)) {
        return "Terrain requires a Transform";
    }
    /* Tiles are placed in the world, at their source's world position, and
     * laid out along the world's axes. */
    GlobalTransform2D pose = transform_world_2d(world, source);
    if (pose.radians != 0) {
        return "Terrain requires an unrotated placement";
    }
    CF_V2 position = pose.position;
    bool unbounded = terrain.stream_radius > 0 && terrain.width == 0 &&
                     terrain.height == 0;
    if (!isfinite(position.x) || !isfinite(position.y) ||
        !isfinite(terrain.width) || !isfinite(terrain.height) ||
        !isfinite(terrain.cell_size) || terrain.cell_size <= 0 ||
        (!unbounded && (terrain.width < terrain.cell_size ||
                        terrain.height < terrain.cell_size)) ||
        terrain.width / terrain.cell_size > (terrain.stream_radius > 0 ? 65536 : 4096) ||
        terrain.height / terrain.cell_size > (terrain.stream_radius > 0 ? 65536 : 4096) ||
        terrain.min_fragment_cells < 1 || terrain.border_cells < 0 ||
        terrain.border_cells > 2048 || terrain.island_cells < 0 ||
        terrain.connectivity < MATERIAL_CONNECTIVITY_ALL ||
        terrain.connectivity > MATERIAL_CONNECTIVITY_FIRST_AIR) {
        return "Terrain requires 1..4096 cells per axis (65536 or zero bounds "
               "when streamed), "
               "positive cell size and fragment minimum, and valid "
               "connectivity";
    }
    if (!isfinite(terrain.stream_radius) || terrain.stream_radius < 0 ||
        terrain.stream_radius > 4096 ||
        (terrain.stream_radius > 0 &&
         (terrain.stream_budget < 1 || terrain.stream_budget > 64 ||
          terrain.connectivity != MATERIAL_CONNECTIVITY_ALL ||
          terrain.border_cells != 0))) {
        return "Streamed terrain requires radius 0..4096, budget 1..64, all "
               "connectivity and no border";
    }
    unsigned revision = material_field_revision(materials->fields, terrain.field);
    if (!revision) {
        return "Terrain references an unavailable procedural field";
    }
    float dimensions[] = { position.x,        position.y,
                           terrain.width,     terrain.height,
                           terrain.cell_size, terrain.stream_radius };
    unsigned settings[] = { terrain.seed,
                            terrain.category,
                            terrain.mask,
                            (unsigned)terrain.min_fragment_cells,
                            (unsigned)terrain.connectivity,
                            (unsigned)terrain.border_cells,
                            revision,
                            (unsigned)terrain.stream_budget,
                            (unsigned)terrain.island_cells };
    uint64_t hash = hash_bytes(UINT64_C(14695981039346656037), dimensions, sizeof dimensions);
    hash = hash_bytes(hash, settings, sizeof settings);
    hash = hash_bytes(hash, terrain.field, strlen(terrain.field));
    if (terrain.parameters) {
        hash = hash_bytes(hash, terrain.parameters, strlen(terrain.parameters));
    }
    const TerrainVolume *depth = TerrainVolume_get_const(world, source);
    if (depth && depth->enabled) {
        if (terrain.stream_radius > 0) {
            return "TerrainVolume requires an unstreamed MaterialTerrain";
        }
        const char *error = depth_volume_error(depth);
        if (error) {
            return error;
        }
        /* Elevation only places the drawn volume, so it needs no rebake. */
        float volume[] = { depth->voxel, depth->depth, depth->front, depth->slab };
        hash = hash_bytes(hash, volume, sizeof volume);
        hash = hash_bytes(hash, &depth->refine, sizeof depth->refine);
        hash = hash_bytes(hash, &depth->open_back, sizeof depth->open_back);
    }
    /* Replacing a named argument record does not advance the field revision.
     * Include its typed values in the cheap live-generation change detector. */
    field_context arguments;
    if (!material_field_context(
            materials->fields,
            terrain.field,
            terrain.parameters,
            terrain.seed,
            &arguments,
            materials->error,
            sizeof materials->error
        )) {
        return materials->error;
    }
    for (int i = 0; i < arguments.parameter_count; i++) {
        field_parameter p = arguments.parameters[i];
        hash = hash_bytes(hash, p.name, strlen(p.name));
        hash = hash_bytes(hash, &p.value.type, sizeof p.value.type);
        switch (p.value.type) {
            case PIN_F32:
                hash = hash_bytes(hash, &p.value.f, sizeof p.value.f);
                break;
            case PIN_I32:
                hash = hash_bytes(hash, &p.value.i, sizeof p.value.i);
                break;
            case PIN_U32:
                hash = hash_bytes(hash, &p.value.u, sizeof p.value.u);
                break;
            default: assert(false);
        }
    }
    material_record *source_record = material_ecs_record(materials, source);
    uint64_t recipe = source_record->terrain_recipe;
    if (source_record->terrain_hash != hash) {
        if (!terrain_recipe(
                materials,
                terrain,
                &recipe,
                materials->error,
                sizeof materials->error
            )) {
            return materials->error;
        }
        if (source_record->terrain_checkpoint) {
            return recipe != source_record->terrain_recipe
                     ? "Imported terrain recipe changed; explicitly reset "
                       "before regeneration"
                     : "Imported terrain configuration changed; explicitly "
                       "reset before regeneration";
        }
    }
    Transform origin = { position.x, position.y };
    if (terrain.stream_radius > 0) {
        const char *error = stream_terrain(materials, source, terrain, origin, hash);
        if (material_ecs_record(materials, source)->terrain_hash == hash) {
            material_ecs_record(materials, source)->terrain_recipe = recipe;
        }
        return error;
    }
    if (material_ecs_record(materials, source)->terrain_hash == hash) {
        return NULL;
    }
    field_grid grid = { .width = (int)ceilf(terrain.width / terrain.cell_size),
                        .height = (int)ceilf(terrain.height / terrain.cell_size),
                        .cell_size = terrain.cell_size };
    float *field = xmalloc((size_t)grid.width * grid.height * sizeof(float));
    field_context context;
    if (!material_field_context(
            materials->fields,
            terrain.field,
            terrain.parameters,
            terrain.seed,
            &context,
            materials->error,
            sizeof materials->error
        )) {
        free(field);
        return materials->error;
    }
    task_pool_begin(materials->pool, true);
    bool baked = material_field_bake(
        materials->fields,
        terrain.field,
        context,
        grid,
        field,
        task_pool_tasks(materials->pool),
        materials->error,
        sizeof materials->error
    );
    task_pool_end(materials->pool);
    if (!baked) {
        free(field);
        return materials->error;
    }
    /* Every fallible step precedes replacing the terrain. */
    terrain_volume *volume;
    const char *error = bake_depth_volume(materials, source, &terrain, context, &volume);
    if (error) {
        free(field);
        return error;
    }
    material_terrain_connect(field, grid, terrain.connectivity, terrain.border_cells);
    material_terrain_tidy(field, grid, terrain.island_cells);
    material_terrain_chunk *chunks = material_terrain_chunks(field, grid);
    free(field);
    discard_terrain(materials, source);
    for (int i = 0; i < acount(chunks); i++) {
        material_terrain_chunk *chunk = &chunks[i];
        material_mask *mask = xmalloc(sizeof *mask);
        *mask = chunk->mask;
        chunk->mask = (material_mask){ 0 };
        spawn_tile(
            materials,
            source,
            terrain,
            origin,
            (
                field_grid
            ){ mask->width, mask->height, terrain.cell_size, chunk->x, chunk->y, 0 },
            mask,
            (uint64_t)lroundf(
                (chunk->y / terrain.cell_size + grid.height * .5f - mask->height * .5f) / MATERIAL_TERRAIN_CHUNK_CELLS
            ) * ((grid.width + MATERIAL_TERRAIN_CHUNK_CELLS - 1) /
                 MATERIAL_TERRAIN_CHUNK_CELLS) +
                (uint64_t)lroundf(
                    (chunk->x / terrain.cell_size + grid.width * .5f - mask->width * .5f) / MATERIAL_TERRAIN_CHUNK_CELLS
                ),
            false
        );
    }
    material_terrain_chunks_free(chunks);
    material_record *record = material_ecs_record(materials, source);
    attach_depth_volume(record, volume);
    record->terrain = true;
    record->terrain_hash = hash;
    record->terrain_recipe = recipe;
    return NULL;
}

const char *material_ecs_generate_terrains(materials *materials)
{
    ecs_component_id id = COMPONENT_MaterialTerrain.id;
    if (!ecs_world_component(materials->world, id)) {
        return NULL;
    }
    ecs_entity *sources = NULL, entity;
    ecs_query query;
    int result = ecs_query_begin(materials->world, &query, &id, 1);
    assert(result == ECS_OK);
    (void)result;
    while (ecs_query_next(&query, &entity)) {
        if (!entities_is_retiring(materials->entities, entity)) {
            apush(sources, entity);
        }
    }
    ecs_query_end(&query);
    const char *error = NULL;
    for (int i = 0; i < acount(sources) && !error; i++) {
        error = generate_terrain(materials, sources[i]);
    }
    afree(sources);
    return error;
}

static bool checkpoint_source(
    materials *materials,
    ecs_entity source,
    MaterialTerrain *terrain,
    uint64_t *recipe,
    char *error,
    size_t size
)
{
    const MaterialTerrain *component = MaterialTerrain_get_const(materials->world, source);
    if (!component || source.slot >= (unsigned)acount(materials->records) ||
        ecs_entity_to_u64(materials->records[source.slot].entity) !=
            ecs_entity_to_u64(source) ||
        !materials->records[source.slot].terrain) {
        snprintf(error, size, "Terrain checkpoint requires a prepared source");
        return false;
    }
    if (!Transform_get_const(materials->world, source)) {
        snprintf(error, size, "Terrain checkpoint requires a placed source");
        return false;
    }
    GlobalTransform2D pose = transform_world_2d(materials->world, source);
    if (pose.radians != 0 || !isfinite(pose.position.x) ||
        !isfinite(pose.position.y)) {
        snprintf(error, size, "Terrain checkpoint requires a finite unrotated source");
        return false;
    }
    *terrain = *component;
    /* Validate before float-to-int grid conversion, including changed authored inputs. */
    float width = terrain->width / terrain->cell_size,
          height = terrain->height / terrain->cell_size;
    if (!isfinite(width) || !isfinite(height) || width < 0 || height < 0 ||
        width > 65536 || height > 65536 || !isfinite(terrain->cell_size) ||
        terrain->cell_size <= 0 ||
        !terrain_recipe(materials, *terrain, recipe, error, size) ||
        *recipe != materials->records[source.slot].terrain_recipe) {
        snprintf(error, size, "Terrain recipe changed since preparation");
        return false;
    }
    return true;
}
bool materials_terrain_checkpoint_write(
    materials *materials,
    ecs_entity source,
    const char *identity,
    unsigned char **bytes,
    size_t *length,
    char *error,
    size_t size
)
{
    *bytes = NULL;
    *length = 0;
    MaterialTerrain terrain;
    uint64_t recipe;
    if (!checkpoint_source(materials, source, &terrain, &recipe, error, size)) {
        return false;
    }
    material_checkpoint checkpoint = { 0 };
    material_record *record = &materials->records[source.slot];
    if (record->stream) {
        if (!material_stream_checkpoint(record->stream, &checkpoint, error, size)) {
            return false;
        }
    } else {
        field_grid grid = { .width = (int)ceilf(terrain.width / terrain.cell_size),
                            .height = (int)ceilf(terrain.height / terrain.cell_size),
                            .cell_size = terrain.cell_size };
        int columns = (grid.width + MATERIAL_TERRAIN_CHUNK_CELLS - 1) /
                      MATERIAL_TERRAIN_CHUNK_CELLS;
        int rows = (grid.height + MATERIAL_TERRAIN_CHUNK_CELLS - 1) /
                   MATERIAL_TERRAIN_CHUNK_CELLS;
        if (columns * rows > MATERIAL_CHECKPOINT_MAX_TILES) {
            snprintf(error, size, "Terrain snapshot exceeds tile limit");
            return false;
        }
        for (int i = 0; i < columns * rows; i++) {
            field_grid tile;
            bool valid = material_stream_tile_grid(grid, (uint64_t)i, &tile);
            assert(valid);
            (void)valid;
            material_mask *mask = NULL;
            for (int j = 0; j < acount(materials->records); j++) {
                material_record *r = &materials->records[j];
                if (!r->fragment && r->terrain_key == (uint64_t)i &&
                    ecs_entity_to_u64(r->terrain_source) == ecs_entity_to_u64(source) &&
                    ecs_entity_is_alive(materials->world, r->entity)) {
                    mask = r->mask;
                }
            }
            apush(
                checkpoint.tiles,
                ((material_stream_tile){ (uint64_t)i,
                                         tile,
                                         material_checkpoint_copy_mask(mask, tile) })
            );
        }
    }
    bool ok = material_checkpoint_write(&checkpoint, identity, recipe, bytes, length, error, size);
    material_checkpoint_free(&checkpoint);
    return ok;
}
bool materials_terrain_checkpoint_read(
    materials *materials,
    ecs_entity source,
    const char *identity,
    const void *bytes,
    size_t length,
    char *error,
    size_t size
)
{
    MaterialTerrain terrain;
    uint64_t recipe;
    if (!checkpoint_source(materials, source, &terrain, &recipe, error, size)) {
        return false;
    }
    material_checkpoint checkpoint = { 0 };
    if (!material_checkpoint_read(bytes, length, identity, recipe, &checkpoint, error, size)) {
        return false;
    }
    field_grid grid = { .width = (int)ceilf(terrain.width / terrain.cell_size),
                        .height = (int)ceilf(terrain.height / terrain.cell_size),
                        .cell_size = terrain.cell_size };
    material_stream *stream = NULL;
    if (terrain.stream_radius > 0) {
        field_context context;
        if (!material_field_context(
                materials->fields,
                terrain.field,
                terrain.parameters,
                terrain.seed,
                &context,
                error,
                size
            )) {
            goto fail;
        }
        stream = material_stream_new((material_stream_desc){
            .grid = grid,
            .field = terrain.field,
            .context = context,
            .island_cells = terrain.island_cells,
            .tasks = task_pool_tasks(materials->pool) });
        if (!material_stream_restore_checkpoint(stream, &checkpoint, error, size)) {
            goto fail;
        }
    } else {
        int columns = (grid.width + MATERIAL_TERRAIN_CHUNK_CELLS - 1) /
                      MATERIAL_TERRAIN_CHUNK_CELLS;
        int rows = (grid.height + MATERIAL_TERRAIN_CHUNK_CELLS - 1) /
                   MATERIAL_TERRAIN_CHUNK_CELLS;
        if (acount(checkpoint.tiles) != columns * rows) {
            goto fail;
        }
        for (int i = 0; i < acount(checkpoint.tiles); i++) {
            material_stream_tile tile = checkpoint.tiles[i];
            field_grid expected;
            if (!material_stream_tile_grid(grid, tile.key, &expected) ||
                expected.width != tile.grid.width ||
                expected.height != tile.grid.height ||
                expected.cell_size != tile.grid.cell_size ||
                expected.center_x != tile.grid.center_x ||
                expected.center_y != tile.grid.center_y) {
                goto fail;
            }
            material_mask_rebuild(tile.mask);
        }
    }
    /* All validation and fallible temporary-file IO precedes publication. ECS
     * allocation follows the runtime's fail-fast allocation contract. */
    discard_terrain(materials, source);
    material_record *record = material_ecs_record(materials, source);
    recarve_depth_volume(record);
    record->stream = stream;
    record->terrain_checkpoint = true;
    if (!stream) {
        CF_V2 position = transform_world_2d(materials->world, source).position;
        for (int i = 0; i < acount(checkpoint.tiles); i++) {
            material_stream_tile *tile = &checkpoint.tiles[i];
            spawn_tile(
                materials,
                source,
                terrain,
                (Transform){ position.x, position.y },
                tile->grid,
                tile->mask,
                tile->key,
                false
            );
            tile->mask = NULL;
        }
    }
    /* Streamed tiles become resident at the next prepare boundary. */
    for (int i = 0; i < acount(checkpoint.tiles); i++) {
        if (checkpoint.tiles[i].mask) {
            material_mask_free(checkpoint.tiles[i].mask);
            free(checkpoint.tiles[i].mask);
        }
    }
    afree(checkpoint.tiles);
    material_ecs_sync_terrain_surfaces(materials);
    return true;
fail:
    if (stream) {
        material_stream_free(stream);
    }
    material_checkpoint_free(&checkpoint);
    snprintf(error, size, "Terrain checkpoint does not match the source grid or cannot be staged");
    return false;
}
