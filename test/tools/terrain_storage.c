#include "runtime/terrain_storage.h"

#include "execution/status.h"
#include "runtime/component_catalog.h"
#include "runtime/materials.h"
#include "runtime/save.h"
#include "runtime/world.h"
#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/catalog.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static world *simulation;
static ecs_world *ecs;
static materials *terrain;
static terrain_storage *storage;
static save *store;
static ecs_entity source;
static unsigned char *snapshots[4];
static size_t lengths[4];
static char directory[128], error[256];
static const char identity[] = "world/quarry";

static void cleanup()
{
    for (int i = 0; i < 4; i++) {
        free(snapshots[i]);
        snapshots[i] = NULL;
        lengths[i] = 0;
    }
    if (simulation) {
        world_free(simulation);
    }
    simulation = NULL;
    if (directory[0]) {
        tool_remove_tree(directory);
        directory[0] = 0;
    }
}

static bool prepare(bool persistent)
{
    REQUIRE(type_catalog_find("vec2"));
    world_options options = { .modules = WORLD_MATERIALS };
    simulation = world_new(&options, error, sizeof error);
    REQUIRE(simulation);
    REQUIRE(engine_set_playing(world_engine(simulation), false) == FLOW_OK);
    ecs = world_ecs(simulation);
    terrain = world_materials(simulation);
    storage = world_modules(simulation)->terrain_storage;
    store = world_modules(simulation)->save;
    REQUIRE(storage && !world_modules(simulation)->physics);
    field_ir constant = { .op = FIELD_CONSTANT, .value = -1 };
    field_program *program = field_program_new(&constant, 1, 0, NULL, 0, error, sizeof error);
    REQUIRE(program);
    REQUIRE(material_field_register(materials_fields(terrain), "Rock", program));
    source = ecs_world_create_entity(ecs);
    MaterialTerrain authored = { .width = 16,
                                 .height = 8,
                                 .cell_size = 1,
                                 .field = "Rock",
                                 .seed = 17,
                                 .min_fragment_cells = 1 };
    Transform transform = { 0 };
    Substance substance = { .hardness = 1 };
    REQUIRE(
        ecs_world_record_add(ecs, source, COMPONENT_MaterialTerrain.id, &authored, 0) == ECS_OK
    );
    REQUIRE(ecs_world_record_add(ecs, source, COMPONENT_Transform.id, &transform, 0) == ECS_OK);
    REQUIRE(ecs_world_record_add(ecs, source, COMPONENT_Substance.id, &substance, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(ecs) == ECS_OK);
    REQUIRE(!materials_prepare(terrain));
    REQUIRE(materials_inspect(terrain).cells == 128);
    if (persistent) {
        snprintf(directory, sizeof directory, "/tmp/chibi-terrain-storage-XXXXXX");
        REQUIRE(mkdtemp(directory));
        char path[160];
        snprintf(path, sizeof path, "%s/save.json", directory);
        REQUIRE(save_open(store, path));
    }
    return true;
}

static bool snapshot(int index)
{
    free(snapshots[index]);
    return materials_terrain_checkpoint_write(
        terrain,
        source,
        identity,
        &snapshots[index],
        &lengths[index],
        error,
        sizeof error
    );
}

static bool same(int a, int b)
{
    return lengths[a] == lengths[b] &&
           !memcmp(snapshots[a], snapshots[b], lengths[a]);
}

static int request(ecs_entity entity, const char *key, bool load)
{
    return terrain_storage_request(storage, entity, key, load, error, sizeof error);
}

static bool completed(int token, bool success)
{
    terrain_storage_state state = terrain_storage_result(storage, token);
    REQUIRE(!state.pending && state.succeeded == success);
    REQUIRE(success ? !state.error[0] : state.error[0] != 0);
    return true;
}

static bool step()
{
    engine *runtime = world_engine(simulation);
    REQUIRE(engine_step(runtime) == FLOW_OK);
    int status = engine_frame(runtime, 0);
    if (status != FLOW_OK) {
        fprintf(
            stderr,
            "terrain storage engine step: %d %s\n",
            status,
            engine_error(runtime) ? engine_error(runtime) : "no diagnostic"
        );
    }
    REQUIRE(status == FLOW_OK);
    REQUIRE(engine_last_timing(runtime)->steps == 1);
    return true;
}

TEST_CASE(terrain_requests_copy_identity_and_wait_for_the_engine_boundary)
{
    REQUIRE(prepare(true));
    char key[] = "world/quarry";
    ecs_component_id id = COMPONENT_MaterialTerrain.id;
    ecs_query query;
    REQUIRE(ecs_query_begin(ecs, &query, &id, 1) == ECS_OK);
    int token = request(source, key, false);
    ecs_query_end(&query);
    REQUIRE(token > 0 && !error[0]);
    key[0] = 'x';
    REQUIRE(terrain_storage_pending(storage));
    terrain_storage_state pending = terrain_storage_result(storage, token);
    REQUIRE(pending.pending && !pending.succeeded && !pending.error[0]);
    REQUIRE(request(source, "other", true) == 0 && error[0]);
    REQUIRE(terrain_storage_result(storage, token).pending);
    REQUIRE(completed(token + 1, false));
    /* A stopped display frame cannot execute the queued snapshot. */
    REQUIRE(engine_frame(world_engine(simulation), 0) == FLOW_OK);
    REQUIRE(terrain_storage_result(storage, token).pending);
    REQUIRE(!save_blob_read(store, identity, &snapshots[0], &lengths[0]));
    REQUIRE(step());
    REQUIRE(completed(token, true));
    REQUIRE(!terrain_storage_pending(storage));
    REQUIRE(save_blob_read(store, identity, &snapshots[0], &lengths[0]));
    REQUIRE(snapshot(1) && same(0, 1));
    int next = request(source, "cancelled", false);
    REQUIRE(next > token);
    REQUIRE(completed(token, false));
    engine_reset_scene(world_engine(simulation));
    REQUIRE(!terrain_storage_pending(storage));
    REQUIRE(completed(next, false));
    terrain_storage_flush(storage);
    REQUIRE(!save_blob_read(store, "cancelled", &snapshots[2], &lengths[2]));
    int after_reset = request(source, identity, false);
    REQUIRE(after_reset > next);
    REQUIRE(step());
    REQUIRE(completed(after_reset, true));
    return true;
}

TEST_CASE(terrain_storage_reports_unbound_and_invalid_source_failures)
{
    REQUIRE(prepare(false));
    REQUIRE(snapshot(0));
    int token = request(source, identity, false);
    REQUIRE(token > 0);
    terrain_storage_flush(storage);
    REQUIRE(completed(token, false));
    REQUIRE(strstr(terrain_storage_result(storage, token).error, "host-bound"));
    REQUIRE(snapshot(1) && same(0, 1));
    REQUIRE(request((ecs_entity){ 0 }, identity, false) == 0 && error[0]);
    REQUIRE(request(source, "", false) == 0 && error[0]);
    char oversized[SAVE_BLOB_KEY_MAX + 2];
    memset(oversized, 'x', sizeof oversized - 1);
    oversized[sizeof oversized - 1] = 0;
    REQUIRE(request(source, oversized, false) == 0 && error[0]);
    ecs_entity ordinary = ecs_world_create_entity(ecs);
    token = request(ordinary, identity, false);
    REQUIRE(token > 0);
    terrain_storage_flush(storage);
    REQUIRE(completed(token, false));
    REQUIRE(strstr(terrain_storage_result(storage, token).error, "source"));
    REQUIRE(ecs_world_destroy_entity(ecs, ordinary) == ECS_OK);
    /* A stale generational handle cannot become a source on a later flush. */
    token = request(ordinary, identity, false);
    REQUIRE(token > 0);
    terrain_storage_flush(storage);
    REQUIRE(completed(token, false));
    REQUIRE(snapshot(1) && same(0, 1));
    return true;
}

TEST_CASE(terrain_storage_applies_queued_edits_and_restores_exact_state_without_physics)
{
    REQUIRE(prepare(true));
    REQUIRE(snapshot(0));
    materials_damage(terrain, source, (fluid_vec){ 0, 0 }, 2, 10, MATERIAL_DAMAGE_CONTACT, (material_impact){ 0 });
    int token = request(source, identity, false);
    REQUIRE(token > 0);
    /* Neither enqueueing damage nor requesting save has changed live cells. */
    REQUIRE(snapshot(1) && same(0, 1));
    REQUIRE(step());
    REQUIRE(completed(token, true));
    REQUIRE(snapshot(1) && !same(0, 1));
    int saved_cells = materials_inspect(terrain).cells;
    REQUIRE(saved_cells > 0 && saved_cells < 128);
    REQUIRE(save_blob_read(store, identity, &snapshots[2], &lengths[2]));
    REQUIRE(same(1, 2));
    materials_damage(
        terrain,
        source,
        (fluid_vec){ 5, 0 },
        1.5f,
        10,
        MATERIAL_DAMAGE_CONTACT,
        (material_impact){ 0 }
    );
    REQUIRE(!materials_prepare(terrain));
    REQUIRE(snapshot(3) && !same(1, 3));
    token = request(source, identity, true);
    REQUIRE(token > 0);
    REQUIRE(snapshot(3) && !same(1, 3));
    REQUIRE(step());
    REQUIRE(completed(token, true));
    REQUIRE(snapshot(3) && same(1, 3));
    REQUIRE(materials_inspect(terrain).cells == saved_cells);
    /* Loaded tiles are new entities. Their render transforms must be published
     * in this same engine step even when the world has no physics module. */
    ecs_component_id id = COMPONENT_Destructible.id;
    ecs_query query;
    REQUIRE(ecs_query_begin(ecs, &query, &id, 1) == ECS_OK);
    ecs_entity entity;
    int tiles = 0;
    bool transforms = true;
    while (ecs_query_next(&query, &entity)) {
        tiles++;
        transforms &= GlobalTransform2D_get_const(ecs, entity) != NULL;
    }
    ecs_query_end(&query);
    REQUIRE(tiles > 0 && transforms);
    return true;
}

TEST_CASE(terrain_storage_failed_loads_preserve_live_terrain_and_failed_writes_preserve_saved_bytes)
{
    REQUIRE(prepare(true));
    int token = request(source, identity, false);
    REQUIRE(token > 0);
    terrain_storage_flush(storage);
    REQUIRE(completed(token, true));
    REQUIRE(save_blob_read(store, identity, &snapshots[0], &lengths[0]));
    REQUIRE(!save_blob_write(store, identity, snapshots[0], (size_t)SAVE_BLOB_BYTES_MAX + 1));
    REQUIRE(save_blob_read(store, identity, &snapshots[1], &lengths[1]));
    REQUIRE(same(0, 1));
    materials_damage(terrain, source, (fluid_vec){ 0, 0 }, 2, 10, MATERIAL_DAMAGE_CONTACT, (material_impact){ 0 });
    REQUIRE(!materials_prepare(terrain));
    REQUIRE(snapshot(2) && !same(0, 2));
    token = request(source, "missing", true);
    REQUIRE(token > 0);
    terrain_storage_flush(storage);
    REQUIRE(completed(token, false));
    REQUIRE(snapshot(3) && same(2, 3));
    /* Renaming the storage key cannot bypass the codec's stable identity. */
    REQUIRE(save_blob_write(store, "wrong-identity", snapshots[0], lengths[0]));
    token = request(source, "wrong-identity", true);
    REQUIRE(token > 0);
    terrain_storage_flush(storage);
    REQUIRE(completed(token, false));
    REQUIRE(snapshot(3) && same(2, 3));
    const unsigned char corrupt[] = { 1, 2, 3 };
    REQUIRE(save_blob_write(store, identity, corrupt, sizeof corrupt));
    token = request(source, identity, true);
    REQUIRE(token > 0);
    terrain_storage_flush(storage);
    REQUIRE(completed(token, false));
    REQUIRE(snapshot(3) && same(2, 3));
    return true;
}

void test_native_catalog_enable();
void test_terrain_storage()
{
    test_native_catalog_enable(); /* Material Result snapshots a CF_V2 payload. */
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(terrain_requests_copy_identity_and_wait_for_the_engine_boundary);
    RUN_TEST_CASE(terrain_storage_reports_unbound_and_invalid_source_failures);
    RUN_TEST_CASE(terrain_storage_applies_queued_edits_and_restores_exact_state_without_physics);
    RUN_TEST_CASE(
        terrain_storage_failed_loads_preserve_live_terrain_and_failed_writes_preserve_saved_bytes
    );
    pu_clear_setup();
}
