#include "authoring/graph_catalog.h"
#include "authoring/project_prepare.h"
#include "execution/module.h"
#include "host/native_default.h"
#include "runtime/component_catalog.h"
#include "runtime/materials.h"
#include "runtime/native_prepared.h"
#include "runtime/scenes/physics.h"
#include "support/arena.h"
#include "support/array.h"
#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"
#include "third_party/yyjson/yyjson.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static native_capabilities *native;
static authoring_graph_catalog *palette;
static const registry_pack **previous_packs;
static bool palette_installed;
static authoring_project_artifact artifact;
static authoring_project_artifact assembled;
static arena module_names;
static package_module *modules;
static player *runtime;
static const char *controller;
static char project[4096], scratch[64];
static yyjson_doc *reply;

typedef struct
{
    int cells, masks;
    bool center_solid;
    uint64_t hash;
} specimen_state;

static void cleanup()
{
    if (runtime) {
        player_free(runtime);
    }
    runtime = NULL;
    authoring_project_artifact_free(&artifact);
    authoring_project_artifact_free(&assembled);
    afree(modules);
    modules = NULL;
    arena_free(&module_names);
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
    yyjson_doc_free(reply);
    reply = NULL;
    if (*scratch) {
        tool_remove_tree(scratch);
        *scratch = '\0';
    }
}

static specimen_state specimen(world *world, bool upper)
{
    specimen_state state = { .hash = UINT64_C(14695981039346656037) };
    ecs_world *ecs = world_ecs(world);
    ecs_component_id ids[] = { COMPONENT_Destructible.id, COMPONENT_Transform.id };
    ecs_query query;
    int status = ecs_query_begin(ecs, &query, ids, 2);
    assert(status == ECS_OK);
    ecs_entity entity;
    while (ecs_query_next(&query, &entity)) {
        const Transform *position = Transform_get_const(ecs, entity);
        const material_mask *mask = materials_mask(world_materials(world), entity);
        if (!mask || materials_is_fragment(world_materials(world), entity) ||
            (position->y > 0) != upper) {
            continue;
        }
        state.center_solid |= material_mask_contains(
            mask,
            -position->x,
            (upper ? 115.f : -120.f) - position->y
        );
        state.masks++;
        state.cells += mask->count;
        for (int i = 0; i < mask->width * mask->height; i++) {
            state.hash = (state.hash ^ mask->cells[i]) * UINT64_C(1099511628211);
        }
    }
    ecs_query_end(&query);
    return state;
}

/* Preparation emits portable shader-values and parts records even for an unshaded
 * project. CPU simulation does not publish it to a renderer. Reject other
 * resources so adding a visual dependency cannot silently widen this test. */
static int cpu_resources;
static void *stage_cpu_resources(void *, const player_asset *assets, int count, size_t *, char *error, size_t size)
{
    for (int i = 0; i < count; i++) {
        if (assets[i].kind != PLAYER_ASSET_SHADER_VALUES &&
            assets[i].kind != PLAYER_ASSET_PARTS) {
            snprintf(
                error,
                size,
                "CPU specimen has unsupported resource kind %d",
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

static bool load_artifact(const authoring_project_artifact *prepared)
{
    char error[512] = { 0 };
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
    runtime = player_prepared_read(prepared->data, prepared->size, &host, BEHAVIOR_BYTECODE, error, sizeof error);
    if (!runtime) {
        fprintf(stderr, "environment player: %s\n", error);
    }
    REQUIRE(runtime);
    return true;
}

static bool prepare_scene(const char *scene_path)
{
    char error[512] = { 0 };
    native = host_native_default_catalog_new(NULL, 0, NULL, 0, error, sizeof error);
    if (!native) {
        fprintf(stderr, "environment native catalog: %s\n", error);
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
        fprintf(stderr, "environment graph catalog: %s\n", error);
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
        .scene = scene_path,
        .native = native,
        .components = { REGISTRY_COMPONENTS, REGISTRY_COMPONENT_COUNT },
        .modules = WORLD_PHYSICS | WORLD_MATERIALS,
    };
    bool prepared = authoring_project_prepare(&options, &artifact, error, sizeof error);
    if (!prepared) {
        fprintf(stderr, "environment preparation: %s\n", error);
    }
    REQUIRE(prepared);
    return load_artifact(&artifact);
}

TEST_CASE(environment_supplied_modules_keep_source_library_fields)
{
    REQUIRE(prepare_scene(NULL));
    REQUIRE(!materials_prepare(world_materials(player_world(runtime))));
    specimen_state solid = specimen(player_world(runtime), true),
                   cut = specimen(player_world(runtime), false);
    REQUIRE(solid.cells > 0 && cut.cells > 0);
    char error[512] = { 0 };
    size_t first, end;
    REQUIRE(
        player_prepared_modules(artifact.data, artifact.size, &module_names, &modules, &first, &end, error, sizeof error)
    );
    REQUIRE(acount(modules) == 3);
    player_free(runtime);
    runtime = NULL;
    authoring_project_options options = {
        .project = project,
        .native = native,
        .components = { REGISTRY_COMPONENTS, REGISTRY_COMPONENT_COUNT },
        .modules = WORLD_PHYSICS | WORLD_MATERIALS,
    };
    bool prepared = authoring_project_assemble(&options, modules, acount(modules), &assembled, error, sizeof error);
    if (!prepared) {
        fprintf(stderr, "environment supplied-module preparation: %s\n", error);
    }
    REQUIRE(prepared);
    REQUIRE(load_artifact(&assembled));
    REQUIRE(!materials_prepare(world_materials(player_world(runtime))));
    specimen_state actual_solid = specimen(player_world(runtime), true),
                   actual_cut = specimen(player_world(runtime), false);
    REQUIRE(actual_solid.cells == solid.cells && actual_solid.hash == solid.hash);
    REQUIRE(actual_cut.cells == cut.cells && actual_cut.hash == cut.hash);
    return true;
}

TEST_CASE(environment_supplied_modules_reject_unprojectable_library)
{
    REQUIRE(prepare_scene(NULL));
    char error[512] = { 0 };
    size_t first, end;
    REQUIRE(
        player_prepared_modules(artifact.data, artifact.size, &module_names, &modules, &first, &end, error, sizeof error)
    );
    player_free(runtime);
    runtime = NULL;
    snprintf(scratch, sizeof scratch, "/tmp/chibi-environment-XXXXXX");
    REQUIRE(mkdtemp(scratch));
    char copied[128], log[128], library[256];
    snprintf(copied, sizeof copied, "%s/project", scratch);
    snprintf(log, sizeof log, "%s/copy.log", scratch);
#ifdef __APPLE__
    const char *copy[] = { "cp", "-c", "-R", project, copied, NULL };
#else
    const char *copy[] = { "cp", "--reflink=auto", "-R", project, copied, NULL };
#endif
    REQUIRE(tool_run(copy, scratch, log) == 0);
    snprintf(library, sizeof library, "%s/content/libraries/FieldShapes.chibi", copied);
    /* Valid executable code can still be outside the native graph backend's
     * scalar expression contract. Supplied modules cannot substitute a graph. */
    tool_write(
        library,
        "FieldShapes :: library {\n"
        " Islands :: (X: number = 0.0, Y: number = 0.0) -> (Value: number = "
        "0.0) "
        "{\n"
        "  if X > 0.0 { return field_circle(X, Y, 1.0); }\n"
        "  return field_circle(X, Y, 2.0);\n"
        " }\n}\n"
    );
    authoring_project_options options = {
        .project = copied,
        .native = native,
        .components = { REGISTRY_COMPONENTS, REGISTRY_COMPONENT_COUNT },
        .modules = WORLD_PHYSICS | WORLD_MATERIALS,
    };
    REQUIRE(
        !authoring_project_assemble(&options, modules, acount(modules), &assembled, error, sizeof error)
    );
    REQUIRE(!assembled.data && !assembled.size);
    REQUIRE(strstr(error, "missing library function") && strstr(error, "FieldShapes"));
    return true;
}

TEST_CASE(environment_authored_samples_generate_and_only_cut_sample_changes)
{
    REQUIRE(prepare_scene(NULL));
    world *world = player_world(runtime);
    const scene_instance *instance = player_scene(runtime);
    const scene *scene = scene_instance_scene(instance);
    REQUIRE(scene_object_count(scene) == 4);
    REQUIRE(!strcmp(scene_object_name(scene, 2), "Solid sample"));
    REQUIRE(!strcmp(scene_object_name(scene, 3), "Cut sample"));
    REQUIRE(scene_object_behavior_count(scene, 2) == 0);
    REQUIRE(scene_object_behavior_count(scene, 3) == 1);
    REQUIRE(!strcmp(scene_object_behavior(scene, 3, 0), "CutMaterial"));
    REQUIRE(!materials_prepare(world_materials(world)));
    specimen_state solid = specimen(world, true), cut = specimen(world, false);
    REQUIRE(solid.masks > 0 && solid.cells > 0 && solid.center_solid && cut.center_solid);
    REQUIRE(
        cut.masks == solid.masks && cut.cells == solid.cells &&
        cut.hash == solid.hash
    );
    engine *engine = world_engine(world);
    REQUIRE(engine_frame(engine, 0) == 0);
    REQUIRE(engine_frame(engine, 1.5 * ENGINE_FIXED_SECONDS) == 0);
    REQUIRE(engine_last_timing(engine)->simulation_steps == 1);
    execution_program *program = source_program(world_sources(world), "CutMaterial");
    REQUIRE(program);
    const execution_program_failure *failure = execution_program_error(program);
    if (failure) {
        fprintf(stderr, "CutMaterial failure: %s\n", failure->message);
    }
    REQUIRE(!failure);
    /* Apply first Tick damage at the next material boundary, without another Tick. */
    REQUIRE(!materials_prepare(world_materials(world)));
    specimen_state solid_after = specimen(world, true),
                   cut_after = specimen(world, false);
    fprintf(
        stderr,
        "terrain cells solid %d -> %d, cut %d -> %d\n",
        solid.cells,
        solid_after.cells,
        cut.cells,
        cut_after.cells
    );
    REQUIRE(solid_after.masks == solid.masks);
    REQUIRE(solid_after.cells == solid.cells && solid_after.hash == solid.hash);
    REQUIRE(cut_after.cells > 0 && cut_after.cells < cut.cells);
    REQUIRE(cut_after.hash != cut.hash);
    REQUIRE(solid_after.center_solid && !cut_after.center_solid);
    REQUIRE(materials_inspect(world_materials(world)).fragments > 0);
    return true;
}

static bool seam_ray(world *world, float x, float y, bool expected)
{
    ray_hit hit;
    REQUIRE(
        physics_raycast(
            world_ecs(world),
            world_entities(world),
            (CF_V2){ x, y - 20 },
            (CF_V2){ x, y + 20 },
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

TEST_CASE(environment_physics_opens_passage_across_chunk_seam)
{
    REQUIRE(prepare_scene("scenes/SeamStudy.kdl"));
    world *world = player_world(runtime);
    ecs_world *ecs = world_ecs(world);
    physics *physics = world_modules(world)->physics;
    REQUIRE(physics);
    const scene_instance *scene = player_scene(runtime);
    ecs_entity terrain = scene_instance_entity(scene, 1);
    ecs_entity solid_probe = scene_instance_entity(scene, 2);
    ecs_entity cut_probe = scene_instance_entity(scene, 3);
    REQUIRE(physics_tick(physics, 0) == 0);
    REQUIRE(seam_ray(world, -1, -32, true));
    REQUIRE(seam_ray(world, 1, -32, true));
    REQUIRE(seam_ray(world, 0, 32, true));
    /* Real continuous physics sweeps block both probes at the solid slab. */
    REQUIRE(physics_tick(physics, (float)ENGINE_FIXED_SECONDS) == 0);
    REQUIRE(Transform_get_const(ecs, solid_probe)->y < 24);
    REQUIRE(Transform_get_const(ecs, cut_probe)->y < -40);
    *Transform_get(ecs, solid_probe) = (Transform){ 0, 12 };
    *Transform_get(ecs, cut_probe) = (Transform){ 0, -52 };
    *Velocity_get(ecs, solid_probe) = (Velocity){ 0, 2400 };
    *Velocity_get(ecs, cut_probe) = (Velocity){ 0, 2400 };
    materials_damage(world_materials(world), terrain, (fluid_vec){ 0, -32 }, 12, 1, 0, (material_impact){ 0 });
    /* Zero-delta synchronization applies queued damage and updates Box2D shapes. */
    REQUIRE(physics_tick(physics, 0) == 0);
    REQUIRE(seam_ray(world, -1, -32, false));
    REQUIRE(seam_ray(world, 1, -32, false));
    REQUIRE(seam_ray(world, 30, -32, true));
    REQUIRE(seam_ray(world, 0, 32, true));
    REQUIRE(physics_tick(physics, (float)ENGINE_FIXED_SECONDS) == 0);
    REQUIRE(Transform_get_const(ecs, solid_probe)->y < 24);
    REQUIRE(Transform_get_const(ecs, cut_probe)->y > -24);
    return true;
}

TEST_CASE(environment_checked_in_project_validates)
{
    snprintf(scratch, sizeof scratch, "/tmp/chibi-environment-XXXXXX");
    REQUIRE(mkdtemp(scratch));
    char input[128], output[128];
    snprintf(input, sizeof input, "%s/request.jsonl", scratch);
    snprintf(output, sizeof output, "%s/reply.jsonl", scratch);
    tool_write(input, "{\"contract\":1,\"operation\":\"validate_project\"}\n");
    const char *args[] = {
        "/bin/sh",  "-c",    "exec \"$0\" --project-root \"$1\" < \"$2\"",
        controller, project, input,
        NULL
    };
    int status = tool_run(args, tool_root(), output);
    char *text = tool_read(output);
    if (status || !text) {
        fprintf(stderr, "environment validate: %s\n", text ? text : "no reply");
    }
    REQUIRE(status == 0 && text);
    const char *json = strstr(text, "{\"contract\"");
    reply = json ? yyjson_read(json, strlen(json), YYJSON_READ_STOP_WHEN_DONE) : NULL;
    free(text);
    REQUIRE(reply);
    yyjson_val *root = yyjson_doc_get_root(reply);
    REQUIRE(yyjson_get_bool(yyjson_obj_get(root, "ok")));
    yyjson_val *result = yyjson_obj_get(root, "result");
    REQUIRE(yyjson_get_bool(yyjson_obj_get(result, "valid")));
    REQUIRE(yyjson_get_int(yyjson_obj_get(result, "errors")) == 0);
    return true;
}

void test_environment_authoring(const char *program)
{
    controller = program;
    int count = snprintf(project, sizeof project, "%s/test/fixtures/environment-authoring", tool_root());
    assert(count > 0 && count < (int)sizeof project);
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(environment_supplied_modules_keep_source_library_fields);
    RUN_TEST_CASE(environment_supplied_modules_reject_unprojectable_library);
    RUN_TEST_CASE(environment_authored_samples_generate_and_only_cut_sample_changes);
    RUN_TEST_CASE(environment_physics_opens_passage_across_chunk_seam);
    RUN_TEST_CASE(environment_checked_in_project_validates);
    pu_clear_setup();
}
