#include "authoring/graph_catalog.h"
#include "authoring/project_prepare.h"
#include "execution/module.h"
#include "host/native_default.h"
#include "runtime/component_catalog.h"
#include "runtime/native_prepared.h"
#include "support/array.h"
#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *binary, *project, *directory, *phase;
static behavior_backend backend;
static char scratch[64];
static native_capabilities *native;
static authoring_graph_catalog *palette;
static const registry_pack **previous_packs;
static bool palette_installed;
static authoring_project_artifact artifact;
static player *runtime;
static int cpu_resources, steps;

typedef struct
{
    const material_mask *mask;
    Transform pose;
    ecs_entity entity;
} terrain_mask;
typedef struct
{
    int left, right, far;
    uint64_t near_hash, far_hash;
} terrain_evidence;

static void cleanup()
{
    if (runtime) {
        player_free(runtime);
    }
    runtime = NULL;
    authoring_project_artifact_free(&artifact);
    if (palette_installed) {
        registry_set_packs(previous_packs, acount(previous_packs));
    }
    palette_installed = false;
    afree(previous_packs);
    previous_packs = NULL;
    authoring_graph_catalog_free(palette);
    palette = NULL;
    native_capabilities_free(native);
    native = NULL;
    if (*scratch) {
        tool_remove_tree(scratch);
        *scratch = '\0';
    }
}

/* The CPU host retains no graphics handles. UI state is a headless world
 * service, so authored storage status still runs without an RML presenter. */
static void *stage_cpu_resources(void *, const player_asset *assets, int count, size_t *, char *error, size_t size)
{
    for (int i = 0; i < count; i++) {
        if (assets[i].kind != PLAYER_ASSET_SHADER_VALUES &&
            assets[i].kind != PLAYER_ASSET_PARTS && assets[i].kind != PLAYER_ASSET_UI &&
            assets[i].kind != PLAYER_ASSET_FONT) {
            snprintf(
                error,
                size,
                "CPU terrain playable has unsupported resource kind %d",
                assets[i].kind
            );
            return NULL;
        }
    }
    cpu_resources = count;
    return &cpu_resources;
}
static bool cpu_scene(void *, void *, const scene *, char *, size_t)
{
    return true;
}
static void commit_cpu_resources(void *, void *, world *) {}
static void release_cpu_resources(void *, void *) {}

static bool prepare()
{
    char error[512] = { 0 };
    REQUIRE(behavior_backend_available(backend));
    native = host_native_default_catalog_new(NULL, 0, NULL, 0, error, sizeof error);
    if (!native) {
        fprintf(stderr, "terrain native catalog: %s\n", error);
    }
    REQUIRE(native);
    palette = authoring_graph_catalog_new(
        (authoring_graph_catalog_config){
            .native = native,
            .types = TYPE_PACK,
            .components = { REGISTRY_COMPONENTS, REGISTRY_COMPONENT_COUNT },
        },
        error,
        sizeof error
    );
    if (!palette) {
        fprintf(stderr, "terrain graph catalog: %s\n", error);
    }
    REQUIRE(palette);
    for (int i = 0; i < registry_pack_count(); i++) {
        apush(previous_packs, registry_pack_at(i));
    }
    int pack_count;
    const registry_pack *const *packs = authoring_graph_catalog_packs(palette, &pack_count);
    registry_set_packs(packs, pack_count);
    palette_installed = true;
    authoring_project_options options = {
        .project = project,
        .native = native,
        .components = { REGISTRY_COMPONENTS, REGISTRY_COMPONENT_COUNT },
        .modules = WORLD_PHYSICS | WORLD_MATERIALS | WORLD_INPUT | WORLD_UI,
    };
    bool prepared = authoring_project_prepare(&options, &artifact, error, sizeof error);
    if (!prepared) {
        fprintf(stderr, "terrain preparation: %s\n", error);
    }
    REQUIRE(prepared);
    native_prepared_context context = { .native = native };
    execution_prepared_linker linker = native_capabilities_prepared_linker(&context);
    player_host host = {
        .components = { REGISTRY_COMPONENTS, REGISTRY_COMPONENT_COUNT },
        .linker = &linker,
        .resources = { .stage = stage_cpu_resources,
                       .scene = cpu_scene,
                       .commit = commit_cpu_resources,
                       .discard = release_cpu_resources,
                       .retire = release_cpu_resources },
    };
    runtime = player_prepared_read(artifact.data, artifact.size, &host, backend, error, sizeof error);
    if (!runtime) {
        fprintf(stderr, "terrain player: %s\n", error);
    }
    REQUIRE(runtime);
    world *world = player_world(runtime);
    char path[4096];
    int n = snprintf(path, sizeof path, "%s/terrain-save.json", directory);
    REQUIRE(n > 0 && n < (int)sizeof path);
    REQUIRE(save_open(world_modules(world)->save, path));
    REQUIRE(!materials_prepare(world_materials(world)));
    REQUIRE(physics_tick(world_modules(world)->physics, 0) == 0);
    engine *engine = world_engine(world);
    REQUIRE(engine_set_focus(engine, ENGINE_FOCUS_VIEWPORT) == 0);
    REQUIRE(engine_set_playing(engine, true) == 0);
    REQUIRE(engine_frame(engine, 0) == 0);
    steps = 0;
    return true;
}

static ecs_entity explorer()
{
    const scene_instance *instance = player_scene(runtime);
    const scene *scene = scene_instance_scene(instance);
    for (int i = 0; i < scene_object_count(scene); i++) {
        if (!strcmp(scene_object_name(scene, i), "Explorer")) {
            return scene_instance_entity(instance, i);
        }
    }
    return (ecs_entity){ 0 };
}

static bool step(uint64_t controls, float horizontal)
{
    engine *engine = world_engine(player_world(runtime));
    bool firing;
    REQUIRE(engine_sample_movement(engine, horizontal, 0) == 0);
    REQUIRE(engine_sample_controls(engine, controls, &firing) == 0);
    int result = engine_frame(engine, (++steps + .5) * ENGINE_FIXED_SECONDS);
    if (result) {
        fprintf(stderr, "terrain step: %s\n", engine_error(engine));
    }
    REQUIRE(result == 0);
    REQUIRE(engine_last_timing(engine)->simulation_steps == (uint64_t)steps);
    execution_program *program = source_program(world_sources(player_world(runtime)), "Explorer");
    REQUIRE(program);
    REQUIRE(execution_module_backend(execution_program_module(program)) == backend);
    const execution_program_failure *failure = execution_program_error(program);
    if (failure) {
        fprintf(stderr, "terrain Explorer: %s\n", failure->message);
    }
    REQUIRE(!failure);
    return true;
}

static terrain_mask *masks()
{
    world *world = player_world(runtime);
    ecs_world *ecs = world_ecs(world);
    ecs_component_id ids[] = { COMPONENT_Destructible.id, COMPONENT_Transform.id };
    ecs_query query;
    int status = ecs_query_begin(ecs, &query, ids, 2);
    assert(status == ECS_OK);
    terrain_mask *records = NULL;
    ecs_entity entity;
    while (ecs_query_next(&query, &entity)) {
        const material_mask *mask = materials_mask(world_materials(world), entity);
        if (mask && !materials_is_fragment(world_materials(world), entity)) {
            apush(records, ((terrain_mask){ mask, *Transform_get_const(ecs, entity), entity }));
        }
    }
    ecs_query_end(&query);
    return records;
}

static ecs_entity occupied(const terrain_mask *records, float x, float y)
{
    for (int i = 0; i < acount(records); i++) {
        if (material_mask_contains(
                records[i].mask,
                x - records[i].pose.x,
                y - records[i].pose.y
            )) {
            return records[i].entity;
        }
    }
    return (ecs_entity){ 0 };
}

/* Sample cell centers in fixed spatial order. These hashes do not depend on
 * transient ECS handles, allocation order, fragments or player state. */
static terrain_evidence evidence()
{
    terrain_mask *records = masks();
    terrain_evidence out = {
        .near_hash = UINT64_C(14695981039346656037),
        .far_hash = UINT64_C(14695981039346656037),
    };
    for (int y = 0; y < 64; y++) {
        for (int x = 0; x < 64; x++) {
            bool solid = occupied(records, x - 31.5f, 47.5f - y).slot != 0;
            if (x < 32) {
                out.left += solid;
            } else {
                out.right += solid;
            }
            out.near_hash = (out.near_hash ^ solid) * UINT64_C(1099511628211);
        }
    }
    for (int y = 0; y < 32; y++) {
        for (int x = 0; x < 32; x++) {
            bool solid = occupied(records, x + 240.5f, 47.5f - y).slot != 0;
            out.far += solid;
            out.far_hash = (out.far_hash ^ solid) * UINT64_C(1099511628211);
        }
    }
    afree(records);
    return out;
}

static bool ray(float x, bool expected)
{
    world *world = player_world(runtime);
    ray_hit hit;
    REQUIRE(
        physics_raycast(
            world_ecs(world),
            world_entities(world),
            (CF_V2){ x, 20 },
            (CF_V2){ x, 4 },
            8,
            (ecs_entity){ 0 },
            false,
            &hit
        ) == 0
    );
    REQUIRE(hit.found == expected);
    if (expected) {
        physics_body_state body;
        REQUIRE(physics_inspect_body(world_modules(world)->physics, hit.entity, &body));
        REQUIRE(body.shape_count > 0);
    }
    return true;
}

static bool pristine(terrain_evidence *out)
{
    terrain_mask *records = masks();
    ecs_entity left = occupied(records, -.5f, 12), right = occupied(records, .5f, 12);
    afree(records);
    REQUIRE(left.slot && right.slot);
    REQUIRE(ecs_entity_to_u64(left) != ecs_entity_to_u64(right));
    REQUIRE(ray(-1, true));
    REQUIRE(ray(1, true));
    REQUIRE(ray(256, true));
    *out = evidence();
    REQUIRE(out->left > 0 && out->right > 0 && out->far == 1024);
    return true;
}

static bool status(const char *expected)
{
    ui *ui = world_modules(player_world(runtime))->ui;
    for (int i = 0; i < ui_element_count(ui); i++) {
        ui_element *element = ui_element_at(ui, i);
        if (!strcmp(element->selector, "#status") && element->text &&
            strstr(element->text, expected)) {
            return true;
        }
    }
    REQUIRE(false);
    return false;
}

static bool completed()
{
    terrain_storage_state state = terrain_storage_result(
        world_modules(player_world(runtime))->terrain_storage,
        1
    );
    if (!state.succeeded) {
        fprintf(stderr, "terrain storage: %s\n", state.error ? state.error : "pending");
    }
    REQUIRE(!state.pending && state.succeeded);
    return true;
}

TEST_CASE(terrain_child_uses_authored_controls)
{
    REQUIRE(prepare());
    terrain_evidence base;
    REQUIRE(pristine(&base));
    char path[4096];
    int n = snprintf(path, sizeof path, "%s/evidence.txt", directory);
    REQUIRE(n > 0 && n < (int)sizeof path);
    if (!strcmp(phase, "write")) {
        ecs_world *ecs = world_ecs(player_world(runtime));
        ecs_entity actor = explorer();
        REQUIRE(actor.slot);
        Transform start = *Transform_get_const(ecs, actor);
        REQUIRE(step(UINT64_C(1) << CONTROL_D, 1));
        REQUIRE(Transform_get_const(ecs, actor)->x > start.x + 1);
        float grounded = Transform_get_const(ecs, actor)->y;
        REQUIRE(step(UINT64_C(1) << CONTROL_SPACE, 0));
        REQUIRE(Transform_get_const(ecs, actor)->y < grounded - 1);
        REQUIRE(Velocity_get_const(ecs, actor)->y < -100);
        REQUIRE(step(UINT64_C(1) << CONTROL_F, 0));
        REQUIRE(ray(-1, false));
        REQUIRE(ray(1, false));
        REQUIRE(ray(256, true));
        terrain_evidence cut = evidence();
        REQUIRE(cut.left < base.left && cut.right < base.right);
        REQUIRE(cut.near_hash != base.near_hash);
        REQUIRE(cut.far == base.far && cut.far_hash == base.far_hash);
        REQUIRE(step(UINT64_C(1) << CONTROL_S, 0));
        REQUIRE(completed());
        REQUIRE(step(0, 0));
        REQUIRE(status("Terrain saved."));
        char text[512];
        snprintf(
            text,
            sizeof text,
            "%d %d %d %" PRIu64 " %" PRIu64 "\n%d %d %d %" PRIu64 " %" PRIu64 "\n",
            base.left,
            base.right,
            base.far,
            base.near_hash,
            base.far_hash,
            cut.left,
            cut.right,
            cut.far,
            cut.near_hash,
            cut.far_hash
        );
        tool_write(path, text);
        fprintf(
            stderr,
            "%s wrote seam cells %d/%d -> %d/%d; far patch %d unchanged\n",
            behavior_backend_name(backend),
            base.left,
            base.right,
            cut.left,
            cut.right,
            cut.far
        );
        return true;
    }
    REQUIRE(!strcmp(phase, "read"));
    char *text = tool_read(path);
    REQUIRE(text);
    terrain_evidence before, saved;
    int fields = sscanf(
        text,
        "%d %d %d %" SCNu64 " %" SCNu64 "\n%d %d %d %" SCNu64 " %" SCNu64,
        &before.left,
        &before.right,
        &before.far,
        &before.near_hash,
        &before.far_hash,
        &saved.left,
        &saved.right,
        &saved.far,
        &saved.near_hash,
        &saved.far_hash
    );
    free(text);
    REQUIRE(fields == 10);
    REQUIRE(
        base.left == before.left && base.right == before.right &&
        base.far == before.far
    );
    REQUIRE(base.near_hash == before.near_hash && base.far_hash == before.far_hash);
    REQUIRE(base.near_hash != saved.near_hash);
    REQUIRE(step(UINT64_C(1) << CONTROL_L, 0));
    REQUIRE(completed());
    /* PollStorage observes the completed request and the next ordinary physics
     * boundary refreshes shapes before these collision queries. */
    REQUIRE(step(0, 0));
    REQUIRE(status("Saved excavation loaded."));
    terrain_evidence loaded = evidence();
    REQUIRE(
        loaded.left == saved.left && loaded.right == saved.right &&
        loaded.far == saved.far
    );
    REQUIRE(loaded.near_hash == saved.near_hash && loaded.far_hash == saved.far_hash);
    REQUIRE(ray(-1, false));
    REQUIRE(ray(1, false));
    REQUIRE(ray(256, true));
    fprintf(
        stderr,
        "%s fresh process regenerated pristine cells, then restored seam edit "
        "and unchanged reference patch\n",
        behavior_backend_name(backend)
    );
    return true;
}

int test_terrain_playable_child(
    const char *project_path,
    const char *save_directory,
    const char *backend_name,
    const char *child_phase
)
{
    project = project_path;
    directory = save_directory;
    phase = child_phase;
    if (!strcmp(backend_name, "vm")) {
        backend = BEHAVIOR_BYTECODE;
    } else if (!strcmp(backend_name, "sljit")) {
        backend = BEHAVIOR_SLJIT;
    } else {
        return 2;
    }
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(terrain_child_uses_authored_controls);
    pu_clear_setup();
    return pu_test_failed() ? 1 : 0;
}

static bool restarted(const char *backend_name)
{
    snprintf(scratch, sizeof scratch, "/tmp/chibi-terrain-playable-XXXXXX");
    REQUIRE(mkdtemp(scratch));
    char log[128];
    const char *phases[] = { "write", "read" };
    for (int i = 0; i < 2; i++) {
        snprintf(log, sizeof log, "%s/%s.log", scratch, phases[i]);
        const char *args[] = { binary,       "--terrain-playable-child",
                               project,      scratch,
                               backend_name, phases[i],
                               NULL };
        REQUIRE(tool_run(args, tool_root(), log) == 0);
        char *output = tool_read(log);
        REQUIRE(output);
        fputs(output, stdout);
        free(output);
    }
    return true;
}
TEST_CASE(terrain_playable_vm_survives_process_restart)
{
    return restarted("vm");
}
TEST_CASE(terrain_playable_sljit_survives_process_restart)
{
    return restarted("sljit");
}
void test_terrain_playable(const char *program, const char *project_path)
{
    binary = program;
    project = project_path;
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(terrain_playable_vm_survives_process_restart);
    RUN_TEST_CASE(terrain_playable_sljit_survives_process_restart);
    pu_clear_setup();
}
