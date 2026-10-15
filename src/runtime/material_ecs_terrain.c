#include "runtime/behaviors/entities.h"
#include "runtime/material_ecs.h"
#include "runtime/material_terrain.h"
#include "runtime/task_pool.h"
#include "runtime/transform_propagation.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <math.h>
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
    Transform origin = { position.x, position.y };
    if (terrain.stream_radius > 0) {
        return stream_terrain(materials, source, terrain, origin, hash);
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
            ){ mask->width, mask->height, terrain.cell_size, chunk->x, chunk->y },
            mask,
            i,
            false
        );
    }
    material_terrain_chunks_free(chunks);
    material_record *record = material_ecs_record(materials, source);
    record->terrain = true;
    record->terrain_hash = hash;
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
