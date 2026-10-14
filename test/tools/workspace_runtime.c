#include "authoring/workspace_runtime.h"

#include "authoring/workspace_runtime_definition.h"
#include "authoring/workspace_runtime_evidence.h"
#include "authoring/workspace_runtime_model.h"
#include "authoring/workspace_saved.h"
#include "authoring/workspace_simulation.h"
#include "authoring/workspace_snapshot.h"
#include "execution/function.h"
#include "execution/function_builder.h"
#include "execution/module_builder.h"
#include "execution/random.h"
#include "execution/status.h"
#include "graph/authoring/graph_edit.h"
#include "graph/modeling/model_compile.h"
#include "graph/shader/shader_graph.h"
#include "platform/cute.h"
#include "render/geometry_2d.h"
#include "render/shader_catalog.h"
#include "render/shader_publish.h"
#include "runtime/component_catalog.h"
#include "runtime/declared_components.h"
#include "runtime/models/prepared.h"
#include "runtime/player_prepared.h"
#include "runtime/save.h"
#include "runtime/shaders/prepared.h"
#include "support/array.h"
#include "support/asset_json.h"
#include "support/sha256.h"
#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/catalog.h"

#include <assert.h>
#include <errno.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
#include <sys/stat.h>
#include <unistd.h>
#endif
static char *saved_timeline;
static arena native_storage;
static ecs_world *properties_world;
static bool properties_types_installed, properties_had_previous_types;
static type_catalog_pack properties_previous_types;
static workspace_documents *documents;
static workspace_snapshot *snapshot;
static declared_components *app_types, *target_types;
static declared_components_scope type_scopes[2];
static player *decoded;
static workspace_runtime *runtime;
static workspace_simulation *simulation;
static workspace_runtime_evidence_plan *evidence_plan;
static workspace_files *evidence_files;
static void *evidence_png;
static execution_prepared_linker fixture_linker;
static scene *source_scene;
static unsigned char *artifact;
static char root[1024];
static behavior_function_builder *begin_builder;
static execution_module_plan *begin_plan;
static execution_module *begin_module;
static unsigned char *begin_wire;
static size_t begin_wire_bytes;
static int begin_count, begin_values[16], begin_save_counts[16];
static behavior_asset *fixture_model_source;
static model_program fixture_model_program;
static unsigned char *fixture_model_bytes, *fixture_shader_bytes;
static size_t fixture_model_size, fixture_shader_size;
static char *fixture_model_replay;
static int retire_count;
static bool retire_scoped;
static const char *begin_random(void *user, const pin_value *in, pin_value *out)
{
    (void)user;
    (void)in;
    (void)out;
    assert(begin_count < 16);
    begin_save_counts[begin_count] = save_count(
        world_modules(workspace_runtime_world(runtime))->save
    );
    begin_values[begin_count++] = behavior_random_int();
    return NULL;
}
static const node_host_call BEGIN_CALL = { .invoke = begin_random };
static const node_access BEGIN_ACCESS = { .flags = NODE_EFFECT_HOST_SINK };
static bool begin_host(void *user, uint64_t id, execution_prepared_host *out)
{
    (void)user;
    if (id != 12001) {
        return false;
    }
    *out = (execution_prepared_host){ .call = &BEGIN_CALL,
                                      .access = &BEGIN_ACCESS,
                                      .abi = 12002 };
    return true;
}
static bool begin_fixture(char *error, size_t size)
{
    begin_count = 0;
    begin_builder = behavior_function_builder_new();
    pin_value shared_initial = { .type = PIN_I32, .i = 0 };
    behavior_function_builder_signature(begin_builder, NULL, 0, NULL, 0, &shared_initial, 1);
    int host = behavior_function_builder_host(begin_builder, &BEGIN_CALL, &BEGIN_ACCESS, NULL, 0, NULL, 0);
    if (!behavior_function_builder_host_identity(begin_builder, host, 12001, 12002)) {
        return false;
    }
    behavior_function_builder_emit(
        begin_builder,
        (behavior_function_instruction){ .op = BF_HOST_CALL,
                                         .target = host,
                                         .left = BEHAVIOR_FUNCTION_HOST_DATA }
    );
    behavior_function_builder_emit(begin_builder, (behavior_function_instruction){ .op = BF_END });
    behavior_function *body = behavior_function_builder_finish(begin_builder, error, size);
    behavior_function_builder_free(begin_builder);
    begin_builder = NULL;
    if (!body) {
        return false;
    }
    begin_plan = execution_module_plan_new();
    begin_plan->valid = true;
    begin_plan->id = 12010;
    apush(
        begin_plan->events,
        ((execution_module_event_declaration){ .id = 12011, .abi = 12012, .name = "Begin" })
    );
    begin_plan->event_count = 1;
    apush(
        begin_plan->states,
        ((execution_module_state){ .id = 12014,
                                   .initial = { .type = PIN_I32, .i = 0 },
                                   .shared = true })
    );
    begin_plan->state_count = 1;
    apush(
        begin_plan->functions,
        ((execution_module_function){ .id = 12013,
                                      .event = 12011,
                                      .event_abi = 12012,
                                      .kind = EXECUTION_FUNCTION_HANDLER,
                                      .body = body })
    );
    begin_plan->function_count = 1;
    begin_module = execution_module_finish(&begin_plan, BEHAVIOR_BYTECODE, error, size);
    return begin_module &&
           execution_prepared_write(begin_module, &begin_wire, &begin_wire_bytes, error, size);
}
static void retire_target(void *user, render_ui *ui)
{
    (void)user;
    retire_count++;
    retire_scoped = !ui && type_catalog_find("ScopedRuntimeTarget") &&
                    !type_catalog_find("ScopedRuntimeApp");
}
static void cleanup()
{
    free(fixture_model_replay);
    fixture_model_replay = NULL;
    free(fixture_model_bytes);
    free(fixture_shader_bytes);
    fixture_model_bytes = fixture_shader_bytes = NULL;
    model_program_free(&fixture_model_program);
    fixture_model_program = (model_program){ 0 };
    behavior_asset_free(fixture_model_source);
    fixture_model_source = NULL;
    free(saved_timeline);
    saved_timeline = NULL;
    arena_free(&native_storage);
    ecs_world_free(properties_world);
    properties_world = NULL;
    if (properties_types_installed) {
        type_catalog_set_project(NULL);
        if (properties_had_previous_types) {
            type_catalog_set_project(&properties_previous_types);
        }
        properties_types_installed = properties_had_previous_types = false;
    }
    workspace_runtime_evidence_free(evidence_plan);
    evidence_plan = NULL;
    workspace_files_free(evidence_files);
    evidence_files = NULL;
    cf_free(evidence_png);
    evidence_png = NULL;
    workspace_simulation_free(simulation);
    simulation = NULL;
    workspace_runtime_free(runtime);
    runtime = NULL;
    if (decoded) {
        player_free(decoded);
    }
    decoded = NULL;
    for (int i = 1; i >= 0; i--) {
        if (type_scopes[i].active) {
            bool ok = declared_components_scope_leave(&type_scopes[i]);
            assert(ok);
            (void)ok;
        }
    }
    declared_components_free(app_types);
    declared_components_free(target_types);
    app_types = target_types = NULL;
    scene_free(source_scene);
    source_scene = NULL;
    execution_module_free(begin_module);
    begin_module = NULL;
    execution_module_plan_free(begin_plan);
    begin_plan = NULL;
    if (begin_builder) {
        behavior_function_builder_free(begin_builder);
        begin_builder = NULL;
    }
    free(begin_wire);
    begin_wire = NULL;
    begin_wire_bytes = 0;
    free(artifact);
    artifact = NULL;
    workspace_snapshot_free(snapshot);
    snapshot = NULL;
    workspace_documents_free(documents);
    documents = NULL;
    if (root[0]) {
        tool_remove_tree(root);
        root[0] = 0;
    }
}
static bool types()
{
    char error[512];
    declared_field app_field = { .name = "value", .type = "int" },
                   target_field = { .name = "value", .type = "float" };
    declared_component app = { .name = "ScopedRuntimeApp",
                               .fields = &app_field,
                               .field_count = 1 },
                       target = { .name = "ScopedRuntimeTarget",
                                  .fields = &target_field,
                                  .field_count = 1 };
    app_types = declared_components_authoring(&app, 1, error, sizeof error);
    target_types = declared_components_authoring(&target, 1, error, sizeof error);
    return app_types && target_types;
}
TEST_CASE(runtime_reflection_scopes_restore_previous_project)
{
    REQUIRE(types());
    const script_catalog *previous = script_catalog_project();
    REQUIRE(declared_components_scope_enter(app_types, &type_scopes[0]));
    REQUIRE(type_catalog_find("ScopedRuntimeApp") && !type_catalog_find("ScopedRuntimeTarget"));
    ecs_component_catalog app = project_components();
    REQUIRE(app.component_count == REGISTRY_COMPONENT_COUNT + 1);
    REQUIRE(!strcmp(app.components[app.component_count - 1]->name, "ScopedRuntimeApp"));
    REQUIRE(declared_components_scope_enter(target_types, &type_scopes[1]));
    REQUIRE(!type_catalog_find("ScopedRuntimeApp") && type_catalog_find("ScopedRuntimeTarget"));
    ecs_component_catalog target = project_components();
    REQUIRE(target.component_count == REGISTRY_COMPONENT_COUNT + 1);
    REQUIRE(!strcmp(target.components[target.component_count - 1]->name, "ScopedRuntimeTarget"));
    REQUIRE(!declared_components_scope_leave(&type_scopes[0]));
    REQUIRE(declared_components_scope_leave(&type_scopes[1]));
    REQUIRE(type_catalog_find("ScopedRuntimeApp") && !type_catalog_find("ScopedRuntimeTarget"));
    REQUIRE(declared_components_scope_enter(NULL, &type_scopes[1]));
    REQUIRE(project_components().component_count == REGISTRY_COMPONENT_COUNT);
    REQUIRE(!type_catalog_find("ScopedRuntimeApp"));
    REQUIRE(declared_components_scope_leave(&type_scopes[1]));
    REQUIRE(project_components().component_count == REGISTRY_COMPONENT_COUNT + 1);
    REQUIRE(declared_components_scope_leave(&type_scopes[0]));
    REQUIRE(script_catalog_project() == previous);
    return true;
}
TEST_CASE(detached_prepared_player_does_not_publish_target_project_types)
{
    REQUIRE(types());
    retire_count = 0;
    retire_scoped = false;
    REQUIRE(declared_components_scope_enter(app_types, &type_scopes[0]));
    char error[512] = "";
    float initial = 1.25f;
    ecs_component_catalog declared = declared_components_ecs(target_types);
    Transform position = { .x = 3, .y = 4 };
    Appearance lower = { .shape = SHAPE_BOX, .width = 4, .height = 2, .layer = 1 },
               upper = { .shape = SHAPE_CIRCLE, .width = 1, .height = 1, .layer = 2 };
    Collider lower_collider = { .shape = COLLIDER_BOX, .width = 4, .height = 2 },
             upper_collider = { .shape = COLLIDER_CIRCLE, .radius = .5f };
    Camera camera = { .width = 10, .height = 6 };
    Transform3D box_pose = { .position = { 1, 2, 3 },
                             .rotation = { .w = 1 },
                             .scale = { 1, 1, 1 } };
    Collider3D box_collider = { .shape = COLLIDER3D_BOX, .size = { 2, 4, 6 } };
    bool begin_ready = begin_fixture(error, sizeof error);
    if (!begin_ready) {
        fprintf(stderr, "prepared Begin fixture: %s\n", error);
    }
    REQUIRE(begin_ready);
    const char *begin_behaviors[] = { "BeginRandom" };
    Input controls = { 0 };
    scene_component components[] = { { declared.components[0], &initial },
                                     { &COMPONENT_Input, &controls },
                                     { &COMPONENT_Transform, &position },
                                     { &COMPONENT_Appearance, &lower },
                                     { &COMPONENT_Collider, &lower_collider },
                                     { &COMPONENT_Camera, &camera } },
                    top_components[] = { { &COMPONENT_Transform, &position },
                                         { &COMPONENT_Appearance, &upper },
                                         { &COMPONENT_Collider, &upper_collider } },
                    box_components[] = { { &COMPONENT_Transform3D, &box_pose },
                                         { &COMPONENT_Collider3D, &box_collider } };
    scene_object objects[] = {
        { .name = "Only",
          .components = components,
          .component_count = 6,
          .behaviors = begin_behaviors,
          .behavior_count = 1 },
        { .name = "Top", .components = top_components, .component_count = 3 },
        { .name = "Box3D", .components = box_components, .component_count = 2 }
    };
    source_scene = scene_prepare(
        &(scene_spec){ .name = "ScopedEmpty",
                       .physics = physics_defaults(),
                       .objects = objects,
                       .object_count = 3 },
        error,
        sizeof error
    );
    if (!source_scene) {
        fprintf(stderr, "Scoped runtime scene: %s\n", error);
    }
    REQUIRE(source_scene);
    fixture_linker.host = begin_host;
    execution_prepared_linker *linker = &fixture_linker;
    package_module begin_record = { .name = "BeginRandom",
                                    .id = execution_module_id(begin_module),
                                    .data = begin_wire,
                                    .length = begin_wire_bytes };
    player_spec spec = { .declared = target_types,
                         .scene = source_scene,
                         .modules = &begin_record,
                         .module_count = 1,
                         .world = { .modules = WORLD_INPUT,
                                    .components = { REGISTRY_COMPONENTS,
                                                    REGISTRY_COMPONENT_COUNT } },
                         .linker = linker };
    size_t length = 0;
    player_application app = { .project_name = "ScopedRuntime",
                               .title = "Scoped runtime" };
    /* The compiler encodes under the target project's reflected catalog;
     * restore the unrelated app catalog before testing detached decode. */
    REQUIRE(declared_components_scope_enter(target_types, &type_scopes[1]));
    bool written = player_prepared_write(&spec, &app, &artifact, &length, error, sizeof error);
    REQUIRE(declared_components_scope_leave(&type_scopes[1]));
    if (!written) {
        fprintf(stderr, "Scoped runtime prepared writer: %s\n", error);
    }
    REQUIRE(written);
    player_host host = { .components = { REGISTRY_COMPONENTS, REGISTRY_COMPONENT_COUNT },
                         .linker = linker,
                         .detached_types = true,
                         .resources = render_prepared_resources_detached(NULL) };
    decoded = player_prepared_read(artifact, length, &host, behavior_default_backend(), error, sizeof error);
    REQUIRE(decoded);
    REQUIRE(type_catalog_find("ScopedRuntimeApp") && !type_catalog_find("ScopedRuntimeTarget"));
    REQUIRE(
        declared_components_scope_enter(player_declared_components(decoded), &type_scopes[1])
    );
    REQUIRE(type_catalog_find("ScopedRuntimeTarget") && !type_catalog_find("ScopedRuntimeApp"));
    ecs_world *world = world_ecs(player_world(decoded));
    ecs_entity top = scene_instance_entity(player_scene(decoded), 1),
               bottom = scene_instance_entity(player_scene(decoded), 0);
    REQUIRE(
        ecs_entity_to_u64(
            render_geometry_2d_pick(world, (CF_V2){ 3, 4 }, 0, (render_skip){ 0 })
        ) == ecs_entity_to_u64(top)
    );
    CF_V2 minimum, maximum;
    REQUIRE(render_geometry_2d_bounds(world, bottom, &minimum, &maximum));
    REQUIRE(minimum.x == 1 && maximum.x == 5 && minimum.y == 3 && maximum.y == 5);
    uint64_t excluded = ecs_entity_to_u64(top);
    REQUIRE(
        ecs_entity_to_u64(
            render_geometry_2d_pick(world, (CF_V2){ 3, 4 }, 0, (render_skip){ &excluded, 1 })
        ) == ecs_entity_to_u64(bottom)
    );
    REQUIRE(!render_geometry_2d_pick(world, (CF_V2){ 30, 40 }, 0, (render_skip){ 0 })
                 .slot);
    REQUIRE(
        ecs_entity_to_u64(
            render_geometry_2d_pick(world, (CF_V2){ 4.5f, 4 }, 0, (render_skip){ 0 })
        ) == ecs_entity_to_u64(bottom)
    );
    render_debug_segment debug_segments[64];
    render_debug_geometry_result debug = render_debug_geometry_collect(
        world,
        world_engine(player_world(decoded)),
        true,
        true,
        (render_skip){ 0 },
        debug_segments,
        64
    );
    REQUIRE(debug.segments == 52 && !debug.invalid && !debug.truncated && !debug.unsupported);
    ecs_entity debug_box = scene_instance_entity(player_scene(decoded), 2);
    CF_V3 debug_min = { FLT_MAX, FLT_MAX, FLT_MAX },
          debug_max = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
    int box_lines = 0;
    for (int i = 0; i < debug.segments; i++) {
        if (ecs_entity_to_u64(debug_segments[i].entity) == ecs_entity_to_u64(debug_box)) {
            debug_min = cf_min_v3(
                debug_min,
                cf_min_v3(debug_segments[i].a, debug_segments[i].b)
            );
            debug_max = cf_max_v3(
                debug_max,
                cf_max_v3(debug_segments[i].a, debug_segments[i].b)
            );
            box_lines++;
        }
    }
    REQUIRE(
        box_lines == 12 && debug_min.x == 0 && debug_min.y == 0 &&
        debug_min.z == 0 && debug_max.x == 2 && debug_max.y == 4 && debug_max.z == 6
    );
    debug = render_debug_geometry_collect(
        world,
        world_engine(player_world(decoded)),
        true,
        true,
        (render_skip){ 0 },
        debug_segments,
        2
    );
    REQUIRE(debug.segments == 2 && debug.truncated);
    debug = render_debug_geometry_collect(
        world,
        world_engine(player_world(decoded)),
        true,
        true,
        (render_skip){ &excluded, 1 },
        debug_segments,
        64
    );
    REQUIRE(debug.segments == 20 && !debug.truncated);
    REQUIRE(declared_components_scope_leave(&type_scopes[1]));
    player_free(decoded);
    decoded = NULL;
    REQUIRE(type_catalog_find("ScopedRuntimeApp") && !type_catalog_find("ScopedRuntimeTarget"));
    snprintf(root, sizeof root, "/tmp/chibi-runtime-prepared-XXXXXX");
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
    REQUIRE(mkdtemp(root));
    char path[2048];
    snprintf(path, sizeof path, "%s/runtime.chibip", root);
    FILE *file = fopen(path, "wb");
    REQUIRE(file);
    REQUIRE(fwrite(artifact, 1, length, file) == length);
    REQUIRE(!fclose(file));
    documents = workspace_documents_new(root, NULL);
    REQUIRE(documents);
    workspace_runtime_config config = {
        .documents = documents,
        .root = root,
        .host = host,
        .backend = behavior_default_backend(),
        .engine_version = "fixture",
        .build_id = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789"
                    "abcdef",
        .manifest_hash = "0123456789abcdef0123456789abcdef0123456789abcdef01234"
                         "56789abcdef",
        .ui_retire = retire_target
    };
    runtime = workspace_runtime_new(&config, error, sizeof error);
    REQUIRE(runtime);
    workspace_runtime_result opened = workspace_runtime_open_prepared(runtime, path, root, 0, 0);
    REQUIRE(opened.ok && opened.ready && !opened.texture);
    char loaded_sha[65];
    sha256_hex(artifact, length, loaded_sha);
    REQUIRE(!strcmp(workspace_runtime_artifact_sha256(runtime), loaded_sha));
    REQUIRE(workspace_runtime_artifact_bytes(runtime) == length);
    REQUIRE(workspace_runtime_verify_artifact(runtime, opened.session, error, sizeof error));
    save *inspected_save = workspace_runtime_save(runtime, opened.session, false, error, sizeof error);
    REQUIRE(
        inspected_save &&
        workspace_runtime_save(runtime, opened.session, true, error, sizeof error) == inspected_save
    );
    REQUIRE(!workspace_runtime_save(runtime, opened.session + 1, false, error, sizeof error));
    REQUIRE(save_set(inspected_save, "inspector-fixture", "001"));
    uint64_t inspected_revision = save_revision(inspected_save);
    REQUIRE(
        workspace_saved_write(inspected_save, inspected_revision, "inspector-fixture", "7", error, sizeof error)
    );
    REQUIRE(
        !workspace_saved_write(inspected_save, inspected_revision, "inspector-fixture", "8", error, sizeof error)
    );
    REQUIRE(!strcmp(save_get(inspected_save, "inspector-fixture"), "7"));
    REQUIRE(save_remove(inspected_save, "inspector-fixture"));

    REQUIRE(!workspace_runtime_source_root(runtime)[0]);
    const type_catalog_pack *model_previous = type_catalog_project();
    workspace_model_bounds model_bounds;
    REQUIRE(
        !workspace_runtime_model_snapshot(runtime, opened.session, 0, -1, &model_bounds, error, sizeof error)
    );
    REQUIRE(model_bounds.empty && type_catalog_project() == model_previous);
    REQUIRE(
        !workspace_runtime_model_snapshot(runtime, opened.session + 1, 0, -1, &model_bounds, error, sizeof error)
    );
    workspace_model_parameter_edit parameter_edit = { .slot = 0 };
    workspace_model_parameter parameter_row;
    REQUIRE(!workspace_runtime_model_parameter(
        runtime,
        opened.session,
        0,
        &parameter_edit,
        &native_storage,
        &parameter_row,
        error,
        sizeof error
    ));
    REQUIRE(!parameter_row.found && type_catalog_project() == model_previous);
    REQUIRE(!workspace_runtime_model_inspection(
        runtime,
        opened.session,
        0,
        -1,
        true,
        (float[3]){ 0, 1, 0 },
        0,
        error,
        sizeof error
    ));
    REQUIRE(
        type_catalog_project() == model_previous &&
        workspace_runtime_current(runtime).session == opened.session
    );

    char subject[64];
    runtime_subject_token(scene_instance_entity(workspace_runtime_scene(runtime), 0), subject);
    char resolved_subject[64];
    REQUIRE(
        workspace_runtime_object_subject(runtime, opened.session, 0, resolved_subject, error, sizeof error)
    );
    REQUIRE(!strcmp(subject, resolved_subject));
    REQUIRE(
        !workspace_runtime_object_subject(runtime, opened.session + 1, 0, resolved_subject, error, sizeof error) &&
        !*resolved_subject
    );
    REQUIRE(
        !workspace_runtime_object_subject(runtime, opened.session, -1, resolved_subject, error, sizeof error) &&
        !*resolved_subject
    );
    REQUIRE(
        !workspace_runtime_object_subject(runtime, opened.session, 9999, resolved_subject, error, sizeof error) &&
        !*resolved_subject
    );

    workspace_runtime_subject_info origin_info;
    REQUIRE(
        workspace_runtime_subject_origin(runtime, opened.session, subject, &origin_info, error, sizeof error)
    );
    REQUIRE(
        origin_info.object == 0 && !origin_info.known && !origin_info.spawned &&
        !origin_info.authored && !*origin_info.prototype && !*origin_info.path
    );
    free(origin_info.source_path);
    free(origin_info.prototype);
    free(origin_info.path);
    REQUIRE(
        !workspace_runtime_subject_origin(runtime, opened.session + 1, subject, &origin_info, error, sizeof error)
    );
    REQUIRE(origin_info.object == -1 && !origin_info.prototype && !origin_info.path);

    unsigned captured = 99;
    bool typing = true, dispatched = true;
    rml_input input = { .type = RML_INPUT_POINTER_DOWN };
    REQUIRE(
        !workspace_runtime_ui_input(runtime, opened.session, &input, &captured, &typing, &dispatched, error, sizeof error)
    );
    REQUIRE(captured == 99 && typing && dispatched);
    REQUIRE(!workspace_runtime_prepare_scene_source(runtime, NULL, "scene").ok);
    int base;
    REQUIRE(
        workspace_document_create(documents, "source.kdl", "name \"Preview source\"", &base) ==
        WORKSPACE_DOCUMENT_OK
    );
    workspace_document_view base_view;
    REQUIRE(workspace_document_read(documents, base, &base_view) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(!workspace_runtime_prepare_document_scene_source(runtime, 0, 0, "preview.kdl", "version 1")
                 .ok);
    REQUIRE(!workspace_runtime_prepare_document_scene_source(
                 runtime,
                 base,
                 base_view.revision + 1,
                 "preview.kdl",
                 "version 1"
    )
                 .ok);
    REQUIRE(!workspace_runtime_prepare_document_scene_source(
                 runtime,
                 base,
                 base_view.revision,
                 "../outside.kdl",
                 "version 1"
    )
                 .ok);
    REQUIRE(!workspace_runtime_prepare_document_scene_source(
                 runtime,
                 base,
                 base_view.revision,
                 "preview.kdl",
                 "\xff"
    )
                 .ok);
    const char *preview_sources[] = {
        "version 1\nname \"Mesh preview\"\n(array)entities { - name=\"Mesh\" { "
        "components { Transform x=2 y=4 } } }",
        "version 1\nname \"Primitive preview\"\n(array)entities { - "
        "name=\"Primitive\" { components { Transform x=-3 y=7 } } }"
    };
    for (int i = 0; i < 2; i++) {
        workspace_runtime_result refused = workspace_runtime_prepare_document_scene_source(
            runtime,
            base,
            base_view.revision,
            "preview.kdl",
            preview_sources[i]
        );
        REQUIRE(!refused.ok && !strcmp(refused.status, "unavailable"));
        REQUIRE(workspace_runtime_current(runtime).session == opened.session);
    }

    REQUIRE(!workspace_runtime_prepare_scene_source(runtime, "../escape.kdl", "scene")
                 .ok);
    REQUIRE(workspace_runtime_current(runtime).session == opened.session);

    REQUIRE(
        !workspace_runtime_evidence_begin(runtime, opened.session, "capture").ok
    );
    workspace_runtime_evidence_result stale_capture = workspace_runtime_evidence_poll(runtime, 1);
    REQUIRE(!stale_capture.ok && !stale_capture.pending && stale_capture.job == 0);
    engine_viewport_view active_camera = { 0 };
    REQUIRE(
        workspace_runtime_camera(runtime, opened.session, &active_camera, error, sizeof error) == 1
    );
    REQUIRE(
        active_camera.center_x == 3 && active_camera.center_y == -4 &&
        active_camera.width == 10 && active_camera.height == 6
    );
    REQUIRE(workspace_runtime_apply_camera(runtime, opened.session, error, sizeof error));
    REQUIRE(workspace_runtime_presentation(runtime, opened.session, true, true, true, true));
    REQUIRE(
        !workspace_runtime_presentation(runtime, opened.session + 1, false, false, false, false)
    );
    CF_Ray3 unrendered_ray = { .t = 7 };
    CF_V3 unrendered_point = { 9, 9, 9 };
    bool unrendered_visible = true;
    REQUIRE(
        !workspace_runtime_ray(runtime, opened.session, (CF_V2){ 0 }, &unrendered_ray, error, sizeof error) &&
        unrendered_ray.t == 7
    );
    REQUIRE(
        !workspace_runtime_project(
            runtime,
            opened.session,
            (CF_V3){ 0 },
            &unrendered_point,
            &unrendered_visible,
            error,
            sizeof error
        ) &&
        unrendered_point.x == 9 && unrendered_visible
    );
    char *json = NULL;
    REQUIRE(
        workspace_runtime_value(runtime, opened.session, 0, "ScopedRuntimeTarget", "value", &json, error, sizeof error)
    );
    REQUIRE(json && !strcmp(json, "1.25"));
    free(json);
    json = NULL;
    REQUIRE(
        workspace_runtime_value(runtime, opened.session, 2, "Transform3D", "position", &json, error, sizeof error)
    );
    REQUIRE(json && strstr(json, "\"x\"") && strstr(json, "\"z\""));
    free(json);
    json = NULL;
    REQUIRE(
        workspace_runtime_value(runtime, opened.session, 2, "Transform3D", "position.x", &json, error, sizeof error)
    );
    fprintf(stderr, "runtime reflected position.x JSON: %s\n", json ? json : "<missing>");
    av_doc *position_json = json ? av_json_read(json) : NULL;
    bool exact_position = position_json && av_is_num(av_doc_root(position_json)) &&
                          av_get_num(av_doc_root(position_json)) == 1;
    av_doc_free(position_json);
    REQUIRE(exact_position);
    free(json);
    json = NULL;
    REQUIRE(
        !workspace_runtime_value(runtime, opened.session + 1, 0, "ScopedRuntimeTarget", "value", &json, error, sizeof error) &&
        !json
    );
    REQUIRE(
        !workspace_runtime_value(runtime, opened.session, 0, "ScopedRuntimeTarget", "missing", &json, error, sizeof error) &&
        !json
    );
    REQUIRE(
        !workspace_runtime_value(NULL, opened.session, 0, "ScopedRuntimeTarget", "value", &json, error, sizeof error) &&
        !json
    );
    REQUIRE(type_catalog_find("ScopedRuntimeApp") && !type_catalog_find("ScopedRuntimeTarget"));
    double number = 0;
    REQUIRE(workspace_runtime_number(
        runtime,
        opened.session,
        0,
        "ScopedRuntimeTarget",
        "value",
        false,
        0,
        &number,
        error,
        sizeof error
    ));
    REQUIRE(number == 1.25);
    REQUIRE(workspace_runtime_number(
        runtime,
        opened.session,
        0,
        "ScopedRuntimeTarget",
        "value",
        true,
        4.5,
        &number,
        error,
        sizeof error
    ));
    REQUIRE(number == 4.5);
    REQUIRE(!workspace_runtime_number(
        runtime,
        opened.session + 1,
        0,
        "ScopedRuntimeTarget",
        "value",
        true,
        9,
        &number,
        error,
        sizeof error
    ));
    REQUIRE(!workspace_runtime_number(
        runtime,
        opened.session,
        0,
        "ScopedRuntimeTarget",
        "missing",
        true,
        9,
        &number,
        error,
        sizeof error
    ));
    REQUIRE(workspace_runtime_number(
        runtime,
        opened.session,
        0,
        "ScopedRuntimeTarget",
        "value",
        false,
        0,
        &number,
        error,
        sizeof error
    ));
    REQUIRE(number == 4.5);
    REQUIRE(!workspace_runtime_number(
        runtime,
        opened.session,
        0,
        "ScopedRuntimeTarget",
        "value",
        true,
        DBL_MAX,
        &number,
        error,
        sizeof error
    ));
    REQUIRE(workspace_runtime_number(
        runtime,
        opened.session,
        0,
        "ScopedRuntimeTarget",
        "value",
        false,
        0,
        &number,
        error,
        sizeof error
    ));
    REQUIRE(number == 4.5);
    simulation = workspace_simulation_new(runtime);
    REQUIRE(simulation);
    REQUIRE(
        !workspace_simulation_time_scale(simulation, opened.session, true, 0).ok
    );
    REQUIRE(
        !workspace_simulation_time_scale(simulation, opened.session, true, NAN).ok
    );
    REQUIRE(
        workspace_simulation_time_scale(simulation, opened.session, true, .5).ok
    );
    REQUIRE(workspace_simulation_time_scale(simulation, opened.session, true, 1).ok);
    engine *target_engine = world_engine(workspace_runtime_world(runtime));
    REQUIRE(engine_has_input(target_engine));
    char subject_token[64];
    runtime_subject_token(scene_instance_entity(workspace_runtime_scene(runtime), 0), subject_token);
    pin_array *subject_rows, *subject_choices;
    int subject_total;
    char *subject_name;
    REQUIRE(workspace_runtime_subject_properties(
        runtime,
        opened.session,
        subject_token,
        0,
        64,
        &native_storage,
        &subject_rows,
        &subject_choices,
        &subject_total,
        &subject_name,
        error,
        sizeof error
    ));
    REQUIRE(subject_rows->count > 0 && subject_total >= subject_rows->count && subject_name);
    REQUIRE(workspace_runtime_subject_write(
        runtime,
        opened.session,
        subject_token,
        "ScopedRuntimeTarget",
        "value",
        "4.5",
        error,
        sizeof error
    ));
    REQUIRE(!workspace_runtime_subject_write(
        runtime,
        opened.session + 1,
        subject_token,
        "ScopedRuntimeTarget",
        "value",
        "7",
        error,
        sizeof error
    ));
    bool component_present = false;
    REQUIRE(
        workspace_runtime_component(runtime, opened.session, 0, "ScopedRuntimeTarget", &component_present, error, sizeof error) &&
        component_present
    );
    REQUIRE(
        workspace_runtime_component(
            runtime,
            opened.session,
            0,
            "AbsentFixtureComponent",
            &component_present,
            error,
            sizeof error
        ) &&
        !component_present
    );
    REQUIRE(!workspace_runtime_component(
        runtime,
        opened.session + 1,
        0,
        "ScopedRuntimeTarget",
        &component_present,
        error,
        sizeof error
    ));
    REQUIRE(workspace_runtime_control(runtime, opened.session, "play").ok);
    REQUIRE(workspace_runtime_subject_write(
        runtime,
        opened.session,
        subject_token,
        "ScopedRuntimeTarget",
        "value",
        "7",
        error,
        sizeof error
    ));
    REQUIRE(!workspace_runtime_subject_write(
        runtime,
        opened.session,
        subject_token,
        "ScopedRuntimeTarget",
        "value",
        "\"invalid number\"",
        error,
        sizeof error
    ));
    for (int i = 0; i < 5; i++) {
        REQUIRE(workspace_runtime_control(runtime, opened.session, "advance").ok);
    }
    REQUIRE(workspace_runtime_current(runtime).steps > 0 && begin_count == 1);
    REQUIRE(workspace_runtime_number(
        runtime,
        opened.session,
        0,
        "ScopedRuntimeTarget",
        "value",
        false,
        0,
        &number,
        error,
        sizeof error
    ));
    REQUIRE(number == 7);
    save *target_save = world_modules(workspace_runtime_world(runtime))->save;
    REQUIRE(save_set_number(target_save, "prior-play", 99) && save_commit(target_save));
    uint64_t old_actor = ecs_entity_to_u64(
        scene_instance_entity(workspace_runtime_scene(runtime), 0)
    );
    execution_instance *source_actor = execution_program_instance(
        source_program(world_sources(workspace_runtime_world(runtime)), "BeginRandom"),
        old_actor
    );
    REQUIRE(
        source_actor &&
        execution_instance_set_state(source_actor, 12014, (pin_value){ .type = PIN_I32, .i = 99 }) == FLOW_OK
    );
    workspace_runtime_result prior_restart = workspace_runtime_current(runtime);
    bool restart_admitted = true;
    player_restart_input invalid_restart = { .seed = 7,
                                             .initial_save_json = "[]",
                                             .record_limit = 4096 };
    REQUIRE(!workspace_runtime_restart(runtime, opened.session, &invalid_restart, &restart_admitted)
                 .ok);
    REQUIRE(!restart_admitted && begin_count == 1 && retire_count == 0);
    REQUIRE(
        workspace_runtime_current(runtime).steps == prior_restart.steps &&
        save_count(target_save) == 1
    );
    REQUIRE(execution_instance_state(source_actor, 12014)->i == 99);
    REQUIRE(workspace_simulation_timeline(simulation, opened.session, "record", 7, "")
                .ok);
    REQUIRE(
        workspace_runtime_current(runtime).steps == 0 &&
        workspace_runtime_current(runtime).time == 0
    );
    REQUIRE(begin_count == 2 && begin_save_counts[1] == 0);
    REQUIRE(save_count(target_save) == 0);
    int recorded_begin = begin_values[1];
    behavior_random expected_random = { 0 };
    behavior_random_seed(&expected_random, 7);
    REQUIRE(recorded_begin == (int)(behavior_random_u32(&expected_random) >> 1));
    REQUIRE(
        ecs_entity_to_u64(scene_instance_entity(workspace_runtime_scene(runtime), 0)) != old_actor
    );
    source_actor = execution_program_instance(
        source_program(world_sources(workspace_runtime_world(runtime)), "BeginRandom"),
        ecs_entity_to_u64(scene_instance_entity(workspace_runtime_scene(runtime), 0))
    );
    REQUIRE(source_actor && execution_instance_state(source_actor, 12014)->i == 0);
    REQUIRE(workspace_runtime_control(runtime, opened.session, "play").ok);
    workspace_simulation_input physical = {
        .controls = UINT64_C(1) << CONTROL_W,
        .movement = { .25f, .5f },
        .controllers = { { .connected = true, .buttons = UINT32_C(1) << PAD_A } },
        .pointer = { .origin = { 1, 2, 3 }, .direction = { 0, -1, 0 }, .active = true }
    };
    workspace_simulation_result
        accepted = workspace_simulation_sample(simulation, opened.session, &physical);
    REQUIRE(accepted.ok && accepted.admitted && accepted.recording);
    REQUIRE(engine_get_focus(target_engine) == ENGINE_FOCUS_VIEWPORT);
    REQUIRE(type_catalog_find("ScopedRuntimeApp") && !type_catalog_find("ScopedRuntimeTarget"));
    physical.movement.x = NAN;
    accepted = workspace_simulation_sample(simulation, opened.session, &physical);
    REQUIRE(!accepted.ok && !accepted.admitted);
    REQUIRE(workspace_runtime_control(runtime, opened.session, "advance").ok);
    REQUIRE(workspace_runtime_control(runtime, opened.session, "advance").ok);
    int recorded_count = 0;
    const input_keyframe *recorded = engine_recorded_input(target_engine, &recorded_count);
    REQUIRE(
        recorded_count > 0 && recorded[0].step == 0 &&
        recorded[recorded_count - 1].controls == (UINT64_C(1) << CONTROL_W)
    );
    REQUIRE(
        recorded[recorded_count - 1].movement.x == .25f &&
        recorded[recorded_count - 1].controllers[0].connected
    );
    workspace_simulation_result
        exported = workspace_simulation_timeline(simulation, opened.session, "export", 0, "");
    REQUIRE(exported.ok && exported.timeline && exported.hash);
    saved_timeline = malloc(strlen(exported.timeline) + 1);
    REQUIRE(saved_timeline);
    strcpy(saved_timeline, exported.timeline);
    char saved_hash[65];
    snprintf(saved_hash, sizeof saved_hash, "%s", exported.hash);
    REQUIRE(workspace_runtime_control(runtime, opened.session, "pause").ok);
    REQUIRE(workspace_runtime_number(
        runtime,
        opened.session,
        0,
        "ScopedRuntimeTarget",
        "value",
        true,
        9.5,
        &number,
        error,
        sizeof error
    ));
    workspace_runtime_result before = workspace_runtime_current(runtime);
    artifact[length - 1] ^= 1;
    file = fopen(path, "wb");
    REQUIRE(file);
    REQUIRE(fwrite(artifact, 1, length, file) == length);
    REQUIRE(!fclose(file));
    artifact[length - 1] ^= 1;
    REQUIRE(!workspace_runtime_verify_artifact(runtime, opened.session, error, sizeof error));
    workspace_simulation_result
        refused = workspace_simulation_timeline(simulation, opened.session, "replay", 0, saved_timeline);
    REQUIRE(!refused.ok && !refused.admitted && refused.recording);
    REQUIRE(!strcmp(refused.hash, saved_hash) && !strcmp(refused.timeline, saved_timeline));
    REQUIRE(
        !strcmp(workspace_runtime_artifact_sha256(runtime), loaded_sha) &&
        workspace_runtime_artifact_bytes(runtime) == length
    );
    workspace_runtime_result after = workspace_runtime_current(runtime);
    REQUIRE(
        after.session == before.session && after.steps == before.steps &&
        after.time == before.time && after.playing == before.playing
    );
    REQUIRE(
        workspace_runtime_number(
            runtime,
            opened.session,
            0,
            "ScopedRuntimeTarget",
            "value",
            false,
            0,
            &number,
            error,
            sizeof error
        ) &&
        number == 9.5
    );
    file = fopen(path, "wb");
    REQUIRE(file);
    REQUIRE(fwrite(artifact, 1, length, file) == length);
    REQUIRE(!fclose(file));
    REQUIRE(workspace_runtime_verify_artifact(runtime, opened.session, error, sizeof error));
    exported = workspace_simulation_timeline(simulation, opened.session, "export", 0, "");
    REQUIRE(
        exported.ok && !strcmp(exported.hash, saved_hash) &&
        !strcmp(exported.timeline, saved_timeline)
    );
    REQUIRE(
        execution_instance_set_state(source_actor, 12014, (pin_value){ .type = PIN_I32, .i = 77 }) == FLOW_OK
    );
    REQUIRE(save_set_number(target_save, "recorded-play", 77) && save_commit(target_save));
    workspace_simulation_result replayed = workspace_simulation_timeline(
        simulation,
        opened.session,
        "replay",
        0,
        exported.timeline
    );
    REQUIRE(replayed.ok && replayed.admitted && replayed.replaying);
    REQUIRE(
        workspace_runtime_current(runtime).steps == 0 &&
        workspace_runtime_current(runtime).time == 0
    );
    REQUIRE(begin_count == 3 && begin_values[2] == recorded_begin && begin_save_counts[2] == 0);
    REQUIRE(save_count(target_save) == 0);
    source_actor = execution_program_instance(
        source_program(world_sources(workspace_runtime_world(runtime)), "BeginRandom"),
        ecs_entity_to_u64(scene_instance_entity(workspace_runtime_scene(runtime), 0))
    );
    REQUIRE(source_actor && execution_instance_state(source_actor, 12014)->i == 0);
    REQUIRE(workspace_runtime_control(runtime, opened.session, "play").ok);
    REQUIRE(workspace_runtime_control(runtime, opened.session, "advance").ok);
    REQUIRE(workspace_runtime_control(runtime, opened.session, "advance").ok);
    REQUIRE(
        workspace_runtime_number(runtime, opened.session, 0, "Input", "move.x", false, 0, &number, error, sizeof error) &&
        number == .25
    );
    REQUIRE(
        workspace_runtime_number(runtime, opened.session, 0, "Input", "move.y", false, 0, &number, error, sizeof error) &&
        number == .5
    );
    REQUIRE(!strcmp(replayed.timeline, saved_timeline) && !strcmp(replayed.hash, saved_hash));
    REQUIRE(workspace_simulation_clear(simulation, opened.session).ok);
    REQUIRE(
        engine_get_focus(target_engine) == ENGINE_FOCUS_NONE &&
        !engine_recording_input(target_engine)
    );
    REQUIRE(workspace_simulation_timeline(simulation, opened.session, "status", 0, "")
                .ok);
    workspace_simulation_free(simulation);
    simulation = NULL;
    REQUIRE(type_catalog_find("ScopedRuntimeApp") && !type_catalog_find("ScopedRuntimeTarget"));
    workspace_runtime_free(runtime);
    runtime = NULL;
    REQUIRE(retire_count == 3 && retire_scoped);
    REQUIRE(type_catalog_find("ScopedRuntimeApp") && !type_catalog_find("ScopedRuntimeTarget"));
    config.read_only_presentation = true;
    runtime = workspace_runtime_new(&config, error, sizeof error);
    REQUIRE(runtime && workspace_runtime_read_only(runtime));
    opened = workspace_runtime_open_prepared(runtime, path, root, 0, 0);
    REQUIRE(opened.ok && opened.ready);
    REQUIRE(workspace_runtime_save(runtime, opened.session, false, error, sizeof error));
    REQUIRE(!workspace_runtime_save(runtime, opened.session, true, error, sizeof error));
    REQUIRE(!workspace_runtime_save(runtime, opened.session + 1, false, error, sizeof error));

    int static_begins = begin_count;
    REQUIRE(!workspace_runtime_control(runtime, opened.session, "play").ok);
    REQUIRE(!workspace_runtime_control(runtime, opened.session, "stop").ok);
    REQUIRE(begin_count == static_begins);
    REQUIRE(!workspace_runtime_draw(runtime, opened.session, 16, 16, .01).ok);
    REQUIRE(!workspace_runtime_number(
        runtime,
        opened.session,
        0,
        "ScopedRuntimeTarget",
        "value",
        true,
        9,
        &number,
        error,
        sizeof error
    ));
    REQUIRE(
        workspace_runtime_number(
            runtime,
            opened.session,
            0,
            "ScopedRuntimeTarget",
            "value",
            false,
            0,
            &number,
            error,
            sizeof error
        ) &&
        number == 1.25
    );
    simulation = workspace_simulation_new(runtime);
    physical.movement.x = 0;
    REQUIRE(!workspace_simulation_sample(simulation, opened.session, &physical).ok);
    workspace_simulation_free(simulation);
    simulation = NULL;
    workspace_runtime_free(runtime);
    runtime = NULL;
    REQUIRE(retire_count == 4 && retire_scoped);
#endif
    return true;
}
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
TEST_CASE(runtime_private_sources_are_owned_exclusive_and_atomic)
{
    snprintf(root, sizeof root, "/tmp/chibi-runtime-private-XXXXXX");
    REQUIRE(mkdtemp(root));
    char path[2048], error[256] = "";
    snprintf(path, sizeof path, "%s/content", root);
    REQUIRE(!mkdir(path, 0700));
    snprintf(path, sizeof path, "%s/project.kdl", root);
    tool_write(path, "version 25\nname \"Private sources\"\ncontent \"content\"\nstart_scene \"scenes/Start.kdl\"\n");
    snprintf(path, sizeof path, "%s/content/Existing.chibi", root);
    tool_write(path, "original");
    documents = workspace_documents_new(root, NULL);
    REQUIRE(documents);
    const char *paths[] = { "content/prototypes/Private.kdl",
                            "content/scenes/Private.kdl" };
    char source[] = "name \"Private\"\n";
    const char *texts[] = { source, "version 1\nname \"Private Scene\"\n(array)entities\n" };
    snapshot = workspace_snapshot_new_sources(root, documents, paths, texts, 2, error, sizeof error);
    REQUIRE(snapshot);
    source[0] = 'X';
    snprintf(path, sizeof path, "%s/content/prototypes/Private.kdl", workspace_snapshot_root(snapshot));
    char *text = tool_read(path);
    REQUIRE(text && !strcmp(text, "name \"Private\"\n"));
    free(text);
    snprintf(path, sizeof path, "%s/content/prototypes/Private.kdl", root);
    REQUIRE(access(path, F_OK) != 0);
    workspace_snapshot_free(snapshot);
    snapshot = NULL;
    const char *duplicate[] = { paths[0], paths[0] };
    REQUIRE(
        !workspace_snapshot_new_sources(root, documents, duplicate, texts, 2, error, sizeof error)
    );
    const char *escape[] = { "../escape.kdl" };
    REQUIRE(
        !workspace_snapshot_new_sources(root, documents, escape, texts, 1, error, sizeof error)
    );
    const char *invalid[] = { "\xff" };
    REQUIRE(
        !workspace_snapshot_new_sources(root, documents, paths, invalid, 1, error, sizeof error)
    );
    const char *conflict[] = { paths[0], "content/Existing.chibi" };
    REQUIRE(
        !workspace_snapshot_new_sources(root, documents, conflict, texts, 2, error, sizeof error)
    );
    snprintf(path, sizeof path, "%s/content/Existing.chibi", root);
    text = tool_read(path);
    REQUIRE(text && !strcmp(text, "original"));
    free(text);
    int draft;
    REQUIRE(
        workspace_document_draft(documents, paths[0], "open buffer", &draft) == WORKSPACE_DOCUMENT_OK
    );
    REQUIRE(!workspace_snapshot_new_sources(root, documents, paths, texts, 2, error, sizeof error));
    REQUIRE(
        !workspace_snapshot_new_sources(root, documents, paths, texts, 17, error, sizeof error)
    );
    return true;
}

TEST_CASE(runtime_snapshot_owns_frozen_overlays_and_refuses_special_sources)
{
    snprintf(root, sizeof root, "/tmp/chibi-runtime-input-XXXXXX");
    REQUIRE(mkdtemp(root));
    char path[2048];
    snprintf(path, sizeof path, "%s/content", root);
    REQUIRE(!mkdir(path, 0700));
    snprintf(path, sizeof path, "%s/project.kdl", root);
    tool_write(path, "version 25\nname \"Frozen\"\ncontent \"content\"\nstart_scene \"scenes/Start.kdl\"\n");
    snprintf(path, sizeof path, "%s/content/Read.chibi", root);
    tool_write(path, "old source");
    documents = workspace_documents_new(root, NULL);
    REQUIRE(documents);
    int document;
    REQUIRE(workspace_document_open(documents, "content/Read.chibi", &document) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(
        workspace_document_edit(documents, document, 1, 0, 10, "unsaved source") == WORKSPACE_DOCUMENT_OK
    );
    int draft;
    REQUIRE(
        workspace_document_draft(documents, "content/Draft.chibi", "named unsaved draft", &draft) ==
        WORKSPACE_DOCUMENT_OK
    );
    snprintf(path, sizeof path, "%s/content/Draft.chibi", root);
    REQUIRE(access(path, F_OK) != 0);
    char error[512] = "";
    snapshot = workspace_snapshot_new(root, documents, error, sizeof error);
    REQUIRE(snapshot);
    snprintf(path, sizeof path, "%s/content/Draft.chibi", workspace_snapshot_root(snapshot));
    char *draft_text = tool_read(path);
    REQUIRE(draft_text && !strcmp(draft_text, "named unsaved draft"));
    free(draft_text);
    workspace_document_view draft_view;
    REQUIRE(
        workspace_document_read(documents, draft, &draft_view) == WORKSPACE_DOCUMENT_OK &&
        draft_view.dirty
    );
    snprintf(path, sizeof path, "%s/content/Draft.chibi", root);
    REQUIRE(access(path, F_OK) != 0);
    snprintf(path, sizeof path, "%s/content/Read.chibi", workspace_snapshot_root(snapshot));
    char *text = tool_read(path);
    REQUIRE(text && !strcmp(text, "unsaved source"));
    free(text);
    workspace_document_view view;
    REQUIRE(
        workspace_document_read(documents, document, &view) == WORKSPACE_DOCUMENT_OK &&
        view.dirty && view.revision == 2
    );
    snprintf(path, sizeof path, "%s/content/Read.chibi", root);
    text = tool_read(path);
    REQUIRE(text && !strcmp(text, "old source"));
    free(text);
    tool_write(path, "later disk source");
    snprintf(path, sizeof path, "%s/content/Read.chibi", workspace_snapshot_root(snapshot));
    text = tool_read(path);
    REQUIRE(text && !strcmp(text, "unsaved source"));
    free(text);
    workspace_runtime_evidence_input evidence = {
        .frozen_root = workspace_snapshot_root(snapshot),
        .artifact_relative = "content/Read.chibi",
        .scene = "Synthetic",
        .engine_version = "fixture",
        .build_id = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789"
                    "abcdef",
        .manifest_hash = "0123456789abcdef0123456789abcdef0123456789abcdef01234"
                         "56789abcdef",
        .backend = "synthetic CPU fixture; no GPU rendered",
        .video_driver = "none",
        .replay_trace_json = "[]",
        .session = 1,
        .document = document,
        .revision = view.revision,
        .width = 16,
        .height = 16,
        .steps = 12,
        .simulation_time = .2,
        .host_time = .2,
        .camera = { .width = 24, .height = 24 },
        .untracked = true
    };
    char timeline[512], artifact_hash[65], combined[197], timeline_identity[65];
    sha256_hex("unsaved source", strlen("unsaved source"), artifact_hash);
    evidence.executed_artifact_sha256 = artifact_hash;
    evidence.executed_artifact_bytes = strlen("unsaved source");
    snprintf(
        combined,
        sizeof combined,
        "%s:%s:%s",
        artifact_hash,
        evidence.build_id,
        evidence.manifest_hash
    );
    sha256_hex(combined, strlen(combined), timeline_identity);
    snprintf(
        timeline,
        sizeof timeline,
        "{\"version\":2,\"initial_save\":{},\"identity\":\"%s\",\"seed\":7,"
        "\"steps\":12,\"keys\":[["
        "0,0,0,0,0,false,false,\"0000000000000000\",[[false,0],[false,0],["
        "false,0],[false,0]],false,0,0,0,0,0,0]]}",
        timeline_identity
    );
    evidence.input_timeline_json = timeline;
    evidence_plan = workspace_runtime_evidence_prepare(&evidence, error, sizeof error);
    REQUIRE(evidence_plan);
    timeline[0] = '!';
    evidence_files = workspace_files_new(root, error, sizeof error);
    REQUIRE(evidence_files);
    REQUIRE(
        !workspace_runtime_evidence_finalize(evidence_plan, evidence_files, "capture", "invalid", 7, error, sizeof error)
    );
    CF_Pixel pixels[16 * 16] = { 0 };
    CF_Image image = { 16, 16, pixels };
    int png_bytes = 0;
    REQUIRE(!cf_is_error(cf_image_save_png_to_memory(&image, &evidence_png, &png_bytes)));
    /* Planner owns bytes: later frozen-root changes cannot alter this bundle. */
    snprintf(path, sizeof path, "%s/content/Read.chibi", workspace_snapshot_root(snapshot));
    tool_write(path, "changed frozen fixture");
    REQUIRE(workspace_runtime_evidence_finalize(
        evidence_plan,
        evidence_files,
        "capture",
        evidence_png,
        (size_t)png_bytes,
        error,
        sizeof error
    ));
    REQUIRE(!workspace_runtime_evidence_finalize(
        evidence_plan,
        evidence_files,
        "capture",
        evidence_png,
        (size_t)png_bytes,
        error,
        sizeof error
    ));
    snprintf(path, sizeof path, "%s/capture/project/content/Read.chibi", root);
    text = tool_read(path);
    REQUIRE(text && !strcmp(text, "unsaved source"));
    free(text);
    snprintf(path, sizeof path, "%s/capture/manifest.json", root);
    text = tool_read(path);
    REQUIRE(
        text && strstr(text, "untracked_mutations") &&
        strstr(text, "gameplay_validated") && strstr(text, "sha256")
    );
    free(text);
    snprintf(path, sizeof path, "%s/capture/input-timeline.json", root);
    text = tool_read(path);
    REQUIRE(text && text[0] == '{' && strstr(text, "\"steps\":12"));
    free(text);
    evidence.input_timeline_json = "{\"keys\":[[[[[[[0]]]]]]]}";
    REQUIRE(!workspace_runtime_evidence_prepare(&evidence, error, sizeof error));
    evidence.input_timeline_json = NULL;
    REQUIRE(!workspace_runtime_evidence_prepare(&evidence, error, sizeof error));
    REQUIRE(strstr(error, "differs from the bytes actually decoded"));
    evidence.manifest_hash = "invalid";
    REQUIRE(!workspace_runtime_evidence_prepare(&evidence, error, sizeof error));
    workspace_runtime_evidence_free(evidence_plan);
    evidence_plan = NULL;
    workspace_files_free(evidence_files);
    evidence_files = NULL;
    cf_free(evidence_png);
    evidence_png = NULL;
    char frozen_root[4096];
    snprintf(frozen_root, sizeof frozen_root, "%s", workspace_snapshot_root(snapshot));
    workspace_snapshot_free(snapshot);
    snapshot = NULL;
    struct stat retired;
    errno = 0;
    REQUIRE(lstat(frozen_root, &retired) != 0 && errno == ENOENT);
    snprintf(path, sizeof path, "%s/content/pipe", root);
    REQUIRE(!mkfifo(path, 0600));
    REQUIRE(!workspace_snapshot_new(root, documents, error, sizeof error));
    REQUIRE(error[0]);
    REQUIRE(!unlink(path));
    snprintf(path, sizeof path, "%s/content/link", root);
    REQUIRE(!symlink("Read.chibi", path));
    REQUIRE(!workspace_snapshot_new(root, documents, error, sizeof error));
    REQUIRE(error[0]);
    REQUIRE(!unlink(path));
    return true;
}
#endif
static const char *native_string(void *storage, const char *text)
{
    size_t bytes = strlen(text) + 1;
    char *copy = arena_alloc(storage, bytes);
    memcpy(copy, text, bytes);
    return copy;
}
TEST_CASE(runtime_subject_properties_preserve_exact_values_and_generation)
{
    typedef struct
    {
        int64_t signed_value;
        uint64_t unsigned_value;
        bool enabled;
        double weight;
        const char *label;
        uint64_t target, asset;
    } values;
    static const type_field fields[] = {
        { .name = "signed_value",
          .type = PIN_I64,
          .offset = offsetof(values, signed_value),
          .size = sizeof(int64_t) },
        { .name = "unsigned_value",
          .type = PIN_U64,
          .offset = offsetof(values, unsigned_value),
          .size = sizeof(uint64_t) },
        { .name = "enabled",
          .type = PIN_BOOL,
          .offset = offsetof(values, enabled),
          .size = sizeof(bool) },
        { .name = "weight",
          .type = PIN_F64,
          .offset = offsetof(values, weight),
          .size = sizeof(double),
          .min = 0,
          .max = 10,
          .range_contract = true },
        { .name = "label",
          .type = PIN_STR,
          .type_name = "const char *",
          .offset = offsetof(values, label),
          .size = sizeof(const char *) },
        { .name = "target",
          .type = PIN_HANDLE,
          .type_name = "ecs.entity",
          .offset = offsetof(values, target),
          .size = sizeof(uint64_t) },
        { .name = "asset",
          .type = PIN_HANDLE,
          .type_name = "material",
          .offset = offsetof(values, asset),
          .size = sizeof(uint64_t) },
    };
    static const type_desc layout = { .name = "RuntimeSubjectFixture",
                                      .size = sizeof(values),
                                      .fields = fields,
                                      .field_count = 7 };
    properties_world = ecs_world_new();
    ecs_component_id component;
    ecs_component_desc desc = { .id = 98001,
                                .key = "fixture.subject",
                                .name = "RuntimeSubjectFixture",
                                .element_size = sizeof(values),
                                .type = &layout };
    REQUIRE(ecs_world_register_component(properties_world, &desc, &component) == ECS_OK);
    ecs_entity entity = ecs_world_create_entity(properties_world),
               target = ecs_world_create_entity(properties_world);
    values initial = { .signed_value = INT64_MIN,
                       .unsigned_value = UINT64_MAX,
                       .enabled = true,
                       .weight = 3,
                       .label = "first",
                       .target = ecs_entity_to_u64(target) };
    REQUIRE(ecs_world_record_add(properties_world, entity, component, &initial, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(properties_world) == ECS_OK);
    char token[64], error[512] = "";
    runtime_subject_token(entity, token);
    ecs_entity resolved;
    REQUIRE(runtime_subject_resolve(properties_world, token, &resolved));
    REQUIRE(ecs_entity_to_u64(resolved) == ecs_entity_to_u64(entity));
    pin_array *rows, *choices;
    int total = 0;
    REQUIRE(
        runtime_properties_query(properties_world, entity, 0, 16, &native_storage, &rows, &choices, &total, error, sizeof error)
    );
    REQUIRE(total == 7 && rows->count == 7 && !choices->count);
    REQUIRE(!strcmp(((runtime_property *)rows->items[0].ptr)->presentation_category, "object"));
    component_presentation policy = { 0 };
    const ecs_component_desc *descriptors[] = { &desc };
    REQUIRE(component_presentation_read(
        "components { \"fixture.subject\" tab=\"Gameplay\" icon=\"health\" { "
        "fields { weight unit=\"charge\" { (array)range 0 10; } } } }",
        (ecs_component_catalog){ descriptors, 1 },
        &policy,
        error,
        sizeof error
    ));
    runtime_properties_present(properties_world, rows, &native_storage, &policy);
    component_presentation_free(&policy);
    const runtime_property *weight_row = rows->items[3].ptr;
    REQUIRE(!strcmp(weight_row->presentation_category, "gameplay"));
    REQUIRE(!strcmp(weight_row->icon, "health"));
    REQUIRE(
        !strcmp(weight_row->unit, "charge") && weight_row->bounded &&
        weight_row->min == 0 && weight_row->max == 10
    );

    REQUIRE(!((runtime_property *)rows->items[6].ptr)->writable);
    const runtime_property *signed_row = rows->items[0].ptr,
                           *unsigned_row = rows->items[1].ptr,
                           *target_row = rows->items[5].ptr;
    REQUIRE(!strcmp(signed_row->json, "-9223372036854775808"));
    REQUIRE(!strcmp(unsigned_row->json, "18446744073709551615"));
    REQUIRE(runtime_subject_resolve(properties_world, target_row->target, &resolved));
    REQUIRE(runtime_properties_write(
        properties_world,
        entity,
        "fixture.subject",
        "signed_value",
        "9223372036854775807",
        error,
        sizeof error
    ));
    REQUIRE(runtime_properties_write(
        properties_world,
        entity,
        "fixture.subject",
        "unsigned_value",
        "9007199254740993",
        error,
        sizeof error
    ));
    REQUIRE(
        runtime_properties_write(properties_world, entity, "fixture.subject", "enabled", "false", error, sizeof error)
    );
    REQUIRE(
        runtime_properties_write(properties_world, entity, "fixture.subject", "weight", "15", error, sizeof error)
    );
    REQUIRE(runtime_properties_write(
        properties_world,
        entity,
        "fixture.subject",
        "label",
        "\"owned Unicode \u00e9\"",
        error,
        sizeof error
    ));
    const values *written = ecs_component_data_const(properties_world, entity, component);
    REQUIRE(
        written->signed_value == INT64_MAX &&
        written->unsigned_value == UINT64_C(9007199254740993)
    );
    REQUIRE(
        !written->enabled && written->weight == 10 &&
        !strcmp(written->label, "owned Unicode \u00e9")
    );
    REQUIRE(!runtime_properties_write(
        properties_world,
        entity,
        "fixture.subject",
        "unsigned_value",
        "18446744073709551616",
        error,
        sizeof error
    ));
    REQUIRE(
        !runtime_properties_write(properties_world, entity, "fixture.subject", "enabled", "1", error, sizeof error)
    );
    REQUIRE(
        !runtime_properties_write(properties_world, entity, "fixture.subject", "weight", "1e999", error, sizeof error)
    );
    REQUIRE(
        !runtime_properties_write(properties_world, entity, "fixture.subject", "missing", "0", error, sizeof error)
    );
    REQUIRE(
        !runtime_properties_write(properties_world, entity, "fixture.subject", "asset", "0", error, sizeof error)
    );
    REQUIRE(!runtime_properties_write(
        properties_world,
        entity,
        "fixture.subject",
        "target",
        "18446744073709551615",
        error,
        sizeof error
    ));
    REQUIRE(
        !runtime_properties_write(properties_world, entity, "fixture.subject", "label", "{}", error, sizeof error)
    );
    REQUIRE(ecs_world_record_name(properties_world, entity, "named-subject", 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(properties_world) == ECS_OK);
    REQUIRE(
        runtime_properties_query(properties_world, entity, 0, 16, &native_storage, &rows, &choices, &total, error, sizeof error)
    );
    bool name_readonly = false;
    for (int i = 0; i < rows->count; i++) {
        const runtime_property *row = rows->items[i].ptr;
        if (!strcmp(row->component, "ecs.Name")) {
            name_readonly = !row->writable;
        }
    }
    REQUIRE(name_readonly);
    REQUIRE(
        !runtime_properties_write(properties_world, entity, "ecs.Name", "value", "\"rename\"", error, sizeof error)
    );
    REQUIRE(ecs_world_destroy_entity(properties_world, entity) == ECS_OK);
    ecs_entity replacement = ecs_world_create_entity(properties_world);
    REQUIRE(!runtime_subject_resolve(properties_world, token, &resolved));
    REQUIRE(
        !runtime_properties_write(properties_world, entity, "fixture.subject", "enabled", "true", error, sizeof error)
    );
    runtime_subject_token(replacement, token);
    REQUIRE(runtime_subject_resolve(properties_world, token, &resolved));
    REQUIRE(
        !workspace_runtime_subject_write(NULL, 1, token, "fixture.subject", "enabled", "true", error, sizeof error)
    );
    return true;
}

TEST_CASE(runtime_subject_properties_nested_offsets_and_named_enum_choices)
{
    typedef struct
    {
        double x;
    } nested;
    typedef struct
    {
        uint64_t padding;
        nested position;
        int mode;
    } values;
    static const type_field nested_fields[] = { { .name = "x",
                                                  .type = PIN_F64,
                                                  .offset = offsetof(nested, x),
                                                  .size = sizeof(double) } };
    static const type_desc nested_layout = { .name = "RuntimePropertyNested",
                                             .size = sizeof(nested),
                                             .fields = nested_fields,
                                             .field_count = 1 };
    static const type_field fields[] = {
        { .name = "position",
          .type = PIN_STRUCT,
          .type_name = "RuntimePropertyNested",
          .offset = offsetof(values, position),
          .size = sizeof(nested) },
        { .name = "mode",
          .type = PIN_ENUM,
          .type_name = "RuntimePropertyMode",
          .offset = offsetof(values, mode),
          .size = sizeof(int) },
    };
    static const type_desc layout = { .name = "RuntimePropertyEnumFixture",
                                      .size = sizeof(values),
                                      .fields = fields,
                                      .field_count = 2 };
    static const type_desc *const types[] = { &nested_layout, &layout };
    static const enum_value_desc options[] = { { 1, "default", "Default mode" },
                                               { 3, "alternate", "Alternate mode" },
                                               { 5, "unnamed", NULL } };
    static const enum_desc enumeration = { .name = "RuntimePropertyMode",
                                           .values = options,
                                           .value_count = 3 };
    static const enum_desc *const enums[] = { &enumeration };
    static const type_catalog_pack pack = { .types = types,
                                            .type_count = 2,
                                            .enums = enums,
                                            .enum_count = 1 };
    const type_catalog_pack *previous = type_catalog_project();
    if (previous) {
        properties_previous_types = *previous;
        properties_had_previous_types = true;
        type_catalog_set_project(NULL);
    }
    type_catalog_set_project(&pack);
    properties_types_installed = true;
    properties_world = ecs_world_new();
    ecs_component_id component;
    ecs_component_desc desc = { .id = 98002,
                                .key = "fixture.nested",
                                .name = "RuntimePropertyEnumFixture",
                                .element_size = sizeof(values),
                                .type = &layout };
    REQUIRE(ecs_world_register_component(properties_world, &desc, &component) == ECS_OK);
    ecs_entity entity = ecs_world_create_entity(properties_world);
    values initial = { .padding = 123, .position = { 2 }, .mode = 1 };
    REQUIRE(ecs_world_record_add(properties_world, entity, component, &initial, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(properties_world) == ECS_OK);
    pin_array *rows, *choices;
    int total;
    char error[512] = "";
    REQUIRE(
        runtime_properties_query(properties_world, entity, 0, 2, &native_storage, &rows, &choices, &total, error, sizeof error)
    );
    REQUIRE(total == 2 && rows->count == 2 && choices->count == 3);
    runtime_property *position = rows->items[0].ptr, *mode = rows->items[1].ptr;
    runtime_property_choice *choice = choices->items[0].ptr;
    REQUIRE(!strcmp(position->field, "position.x") && position->number == 2);
    REQUIRE(!strcmp(mode->json, "\"Default mode\"") && !strcmp(choice->json, mode->json));
    REQUIRE(
        runtime_properties_write(properties_world, entity, "fixture.nested", "position.x", "4.25", error, sizeof error)
    );
    REQUIRE(
        runtime_properties_write(properties_world, entity, "fixture.nested", "mode", "\"Alternate mode\"", error, sizeof error)
    );
    const values *written = ecs_component_data_const(properties_world, entity, component);
    REQUIRE(written->padding == 123 && written->position.x == 4.25 && written->mode == 3);
    REQUIRE(
        runtime_properties_write(properties_world, entity, "fixture.nested", "mode", "\"unnamed\"", error, sizeof error)
    );
    REQUIRE(
        ((const values *)ecs_component_data_const(properties_world, entity, component))
            ->mode == 5
    );
    REQUIRE(
        !runtime_properties_write(properties_world, entity, "fixture.nested", "mode", "7", error, sizeof error)
    );
    REQUIRE(
        runtime_properties_query(properties_world, entity, 1, 1, &native_storage, &rows, &choices, &total, error, sizeof error)
    );
    REQUIRE(
        total == 2 && rows->count == 1 &&
        !strcmp(((runtime_property *)rows->items[0].ptr)->field, "mode")
    );
    REQUIRE(!runtime_properties_query(
        properties_world,
        entity,
        0,
        257,
        &native_storage,
        &rows,
        &choices,
        &total,
        error,
        sizeof error
    ));
    return true;
}

TEST_CASE(runtime_optional_bindings_refuse_inactive_project_without_reading_inputs)
{
    behavior_function_context context = { .value_storage = &native_storage,
                                          .copy_string = native_string };
    const native_definition_pack *pack = &WORKSPACE_RUNTIME_DEFINITION_PACK;
    REQUIRE(pack->binding_count == WORKSPACE_RUNTIME_OPERATION_COUNT);
    for (int b = 0; b < pack->binding_count; b++) {
        const native_definition_binding *binding = &pack->bindings[b];
        const node_host_call *call = binding->implementation.call;
        REQUIRE(call && call->optional_service && call->context);
        pin_value out[32];
        memset(out, 0xa5, sizeof out);
        REQUIRE(!call->context(&context, NULL, NULL, out));
        const native_capability_definition *definition = NULL;
        for (int d = 0; d < pack->definition_count; d++) {
            if (!strcmp(binding->canonical_name, pack->definitions[d].canonical_name)) {
                definition = &pack->definitions[d];
            }
        }
        REQUIRE(definition);
        bool error = false;
        int slot = 0;
        for (int i = 0; i < definition->output_count; i++) {
            const native_parameter_definition *port = &definition->outputs[i];
            if (port->kind == NATIVE_PARAMETER_EXEC) {
                continue;
            }
            const pin_value *value = &out[slot++];
            if (port->container == PIN_CONTAINER_ARRAY) {
                const pin_array *rows = value->ptr;
                REQUIRE(rows && !rows->count && !rows->items);
            } else if (!strcmp(port->name, "Error")) {
                REQUIRE(value->str && strstr(value->str, "unavailable"));
                error = true;
            } else if (!strcmp(port->name, "Ok") || !strcmp(port->name, "Ready") || !strcmp(port->name, "Playing")) {
                REQUIRE(!value->b);
            } else if (!strcmp(port->name, "Session") || !strcmp(port->name, "Document")) {
                REQUIRE(!value->i);
            }
        }
        REQUIRE(error);
    }
    return true;
}
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
static bool runtime_model_fixture(float initial_radius)
{
    const type_catalog_pack *prior = type_catalog_project();
    properties_had_previous_types = prior != NULL;
    if (prior) {
        properties_previous_types = *prior;
    }
    type_catalog_pack types = { .types = SHADER_PACK.types,
                                .type_count = SHADER_PACK.type_count,
                                .enums = SHADER_PACK.enums,
                                .enum_count = SHADER_PACK.enum_count };
    type_catalog_set_project(&types);
    properties_types_installed = true;
    model_program_free(&fixture_model_program);
    behavior_asset_free(fixture_model_source);
    free(fixture_model_bytes);
    free(fixture_shader_bytes);
    fixture_model_bytes = fixture_shader_bytes = NULL;
    fixture_model_source = behavior_asset_new("CpuRuntimeParameterizedSphere");
    fixture_model_source->domain = BEHAVIOR_DOMAIN_MODEL;
    graph *g = fixture_model_source->graph;
    node *radius = graph_add_node(g, &NODE_ModelParameter),
         *sphere = graph_add_node(g, &NODE_ModelSphere),
         *part = graph_add_node(g, &NODE_ModelPart);
    REQUIRE(!graph_set_node_reference(g, radius, 0, "Radius"));
    REQUIRE(!graph_set_node_reference(g, part, 0, "Surface"));
    const float values[] = { initial_radius, 1, 4 };
    for (int i = 0; i < 3; i++) {
        pin_value value = { .type = PIN_F32, .f = values[i] };
        REQUIRE(!graph_set_pin_default(g, radius, false, i, &value));
    }
    REQUIRE(!graph_connect_data(g, radius, 0, sphere, 0));
    REQUIRE(!graph_connect_data(g, sphere, 0, part, 0));
    REQUIRE(!graph_connect_data(g, part, 0, model_graph_add_output(g), 0));
    model_program_init(&fixture_model_program);
    shader_error problem = { 0 };
    REQUIRE(model_compile(fixture_model_source, &fixture_model_program, &problem));
    shader_prepared_asset envelope = { .name = fixture_model_source->name,
                                       .graph = fixture_model_program.shader,
                                       .shape = true,
                                       .bounds = { 4, 4 } };
    for (int i = 0; i < acount(fixture_model_program.parameters); i++) {
        const model_parameter *parameter = &fixture_model_program.parameters[i];
        memcpy(
            envelope.defaults[parameter->slot],
            parameter->defaults,
            sizeof envelope.defaults[parameter->slot]
        );
        if (parameter->hidden) {
            continue;
        }
        envelope.names[parameter->slot] = parameter->name;
        envelope.widths[parameter->slot] = parameter->width;
    }
    REQUIRE(
        shader_asset_prepared_write(&envelope, &fixture_shader_bytes, &fixture_shader_size, &problem)
    );
    REQUIRE(
        model_program_prepared_write(&fixture_model_program, &fixture_model_bytes, &fixture_model_size, &problem)
    );
    type_catalog_set_project(
        properties_had_previous_types ? &properties_previous_types : NULL
    );
    properties_types_installed = properties_had_previous_types = false;
    return true;
}
TEST_CASE(runtime_model_edits_replay_and_preserve_owned_metadata)
{
    REQUIRE(runtime_model_fixture(2));
    snprintf(root, sizeof root, "/tmp/chibi-runtime-model-XXXXXX");
    REQUIRE(mkdtemp(root));
    documents = workspace_documents_new(root, NULL);
    player_host host = { .components = { REGISTRY_COMPONENTS, REGISTRY_COMPONENT_COUNT },
                         .linker = &fixture_linker };
    workspace_runtime_config config = { .documents = documents, .root = root, .host = host };
    char error[512] = "";
    runtime = workspace_runtime_new(&config, error, sizeof error);
    REQUIRE(runtime);
    const char *model_name = "CpuRuntimeParameterizedSphere";
    REQUIRE(!render_shader_find(model_name));
    player_asset resources[] = { { .name = model_name,
                                   .kind = PLAYER_ASSET_SHADER,
                                   .data = fixture_shader_bytes,
                                   .length = fixture_shader_size },
                                 { .name = model_name,
                                   .kind = PLAYER_ASSET_MODEL,
                                   .data = fixture_model_bytes,
                                   .length = fixture_model_size } };
    workspace_model_parameter retained = { 0 };
    int previous_session = 0;
    for (int variant = 0; variant < 2; variant++) {
        scene_free(source_scene);
        source_scene = NULL;
        if (variant) {
            REQUIRE(runtime_model_fixture(1));
        }
        resources[0].data = fixture_shader_bytes;
        resources[0].length = fixture_shader_size;
        resources[1].data = fixture_model_bytes;
        resources[1].length = fixture_model_size;
        Shader binding = { .shape = model_name, .parameters = "" };
        scene_component component = { &COMPONENT_Shader, &binding };
        scene_object object = { .name = variant ? "Small" : "Large",
                                .components = &component,
                                .component_count = 1 };
        source_scene = scene_prepare(
            &(scene_spec){ .name = variant ? "Small scene" : "Large scene",
                           .physics = physics_defaults(),
                           .objects = &object,
                           .object_count = 1 },
            error,
            sizeof error
        );
        REQUIRE(source_scene);
        player_spec spec = { .scene = source_scene,
                             .world = { .components = { REGISTRY_COMPONENTS,
                                                        REGISTRY_COMPONENT_COUNT } },
                             .linker = &fixture_linker,
                             .assets = resources,
                             .asset_count = 2 };
        free(artifact);
        artifact = NULL;
        size_t bytes;
        REQUIRE(player_prepared_write(
            &spec,
            &(player_application){ .project_name = "RuntimeModel" },
            &artifact,
            &bytes,
            error,
            sizeof error
        ));
        char path[2048];
        snprintf(path, sizeof path, "%s/model%d.chibip", root, variant);
        FILE *file = fopen(path, "wb");
        REQUIRE(file);
        REQUIRE(fwrite(artifact, 1, bytes, file) == bytes);
        REQUIRE(!fclose(file));
        workspace_runtime_result
            opened = workspace_runtime_open_prepared(runtime, path, root, 0, 0);
        if (!opened.ok) {
            fprintf(stderr, "model prepared open: %s\n", opened.error);
        }
        REQUIRE(opened.ok && opened.ready && opened.session != previous_session);
        workspace_model_bounds bounds;
        REQUIRE(
            workspace_runtime_model_snapshot(runtime, opened.session, 0, -1, &bounds, error, sizeof error)
        );
        REQUIRE(!bounds.empty && fabsf(bounds.max[0] - (variant ? 1 : 2)) < .0001f);
        workspace_model_parameter_edit edit = { .slot = 0 };
        workspace_model_parameter row;
        REQUIRE(
            workspace_runtime_model_parameter(runtime, opened.session, 0, &edit, &native_storage, &row, error, sizeof error)
        );
        REQUIRE(
            row.found && row.width == 1 && !strcmp(row.name, "Radius") &&
            row.minimum[0] == 1 && row.maximum[0] == 4
        );
        if (variant) {
            REQUIRE(!strcmp(retained.name, "Radius") && !strcmp(retained.asset, model_name));
            REQUIRE(
                !workspace_runtime_model_snapshot(runtime, previous_session, 0, -1, &bounds, error, sizeof error)
            );
        }
        retained = row;
        edit = (workspace_model_parameter_edit){ .slot = 0,
                                                 .write = true,
                                                 .width = row.width,
                                                 .node = row.node,
                                                 .asset = row.asset,
                                                 .scope = row.scope,
                                                 .values = { 3 } };
        REQUIRE(
            workspace_runtime_model_parameter(runtime, opened.session, 0, &edit, &native_storage, &row, error, sizeof error)
        );
        REQUIRE(row.values[0] == 3);
        char *trace;
        REQUIRE(workspace_runtime_trace(runtime, opened.session, &trace, error, sizeof error));
        av_doc *actions = av_json_read(trace);
        free(trace);
        REQUIRE(actions);
        free(fixture_model_replay);
        fixture_model_replay = av_json_write(av_arr_get(av_doc_root(actions), 0));
        av_doc_free(actions);
        REQUIRE(fixture_model_replay);
        edit.values[0] = 7;
        REQUIRE(
            !workspace_runtime_model_parameter(runtime, opened.session, 0, &edit, &native_storage, &row, error, sizeof error)
        );
        REQUIRE(row.values[0] == 3);
        edit.values[0] = 2;
        edit.node++;
        REQUIRE(
            !workspace_runtime_model_parameter(runtime, opened.session, 0, &edit, &native_storage, &row, error, sizeof error)
        );
        REQUIRE(row.values[0] == 3);
        edit.node--;
        REQUIRE(
            workspace_runtime_model_parameter(runtime, opened.session, 0, &edit, &native_storage, &row, error, sizeof error)
        );
        REQUIRE(row.values[0] == 2);
        REQUIRE(
            workspace_runtime_model_replay(runtime, opened.session, fixture_model_replay, error, sizeof error)
        );
        edit.write = false;
        REQUIRE(
            workspace_runtime_model_parameter(runtime, opened.session, 0, &edit, &native_storage, &row, error, sizeof error)
        );
        REQUIRE(row.values[0] == 3);
        REQUIRE(workspace_runtime_model_inspection(
            runtime,
            opened.session,
            0,
            0,
            true,
            (float[3]){ 0, 1, 0 },
            .25f,
            error,
            sizeof error
        ));
        REQUIRE(!workspace_runtime_model_inspection(
            runtime,
            opened.session,
            0,
            1,
            true,
            (float[3]){ 0, 1, 0 },
            .25f,
            error,
            sizeof error
        ));
        REQUIRE(
            workspace_runtime_model_snapshot(runtime, opened.session, 0, 0, &bounds, error, sizeof error)
        );
        REQUIRE(fabsf(bounds.min[0] + 3) < .0001f && fabsf(bounds.max[0] - 3) < .0001f);
        REQUIRE(workspace_runtime_trace(runtime, opened.session, &trace, error, sizeof error));
        actions = av_json_read(trace);
        free(trace);
        REQUIRE(actions);
        REQUIRE(av_arr_size(av_doc_root(actions)) == 4);
        av_value *last = av_arr_get(av_doc_root(actions), av_arr_size(av_doc_root(actions)) - 1);
        char *inspection = av_json_write(last);
        REQUIRE(inspection);
        REQUIRE(
            workspace_runtime_model_replay(runtime, opened.session, inspection, error, sizeof error)
        );
        free(inspection);
        av_obj_set(actions, av_obj_get(last, "details"), "artifact_sha256", av_str(actions, "wrong"));
        inspection = av_json_write(last);
        REQUIRE(inspection);
        REQUIRE(
            !workspace_runtime_model_replay(runtime, opened.session, inspection, error, sizeof error)
        );
        free(inspection);
        av_doc_free(actions);
        REQUIRE(!render_shader_find(model_name));
        previous_session = opened.session;
    }
    return true;
}
#endif
TEST_CASE(runtime_computed_property_metadata_matches_write_policy)
{
    static const type_field field = { .name = "value",
                                      .type = PIN_F64,
                                      .offset = 0,
                                      .size = sizeof(double) };
    static const type_desc layout = { .name = "ComputedFixture",
                                      .size = sizeof(double),
                                      .fields = &field,
                                      .field_count = 1 };
    ecs_component_desc source = { .id = 98010,
                                  .key = "fixture.source",
                                  .name = "SourceFixture",
                                  .element_size = sizeof(double),
                                  .type = &layout };
    ecs_component_desc computed = { .id = 98011,
                                    .key = "fixture.computed",
                                    .name = "ComputedFixture",
                                    .element_size = sizeof(double),
                                    .type = &layout,
                                    .derived_from = source.id };
    properties_world = ecs_world_new();
    ecs_component_id id;
    REQUIRE(ecs_world_register_component(properties_world, &source, &id) == ECS_OK);
    REQUIRE(ecs_world_register_component(properties_world, &computed, &id) == ECS_OK);
    ecs_entity entity = ecs_world_create_entity(properties_world);
    double value = 3;
    REQUIRE(ecs_world_record_add(properties_world, entity, source.id, &value, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(properties_world) == ECS_OK);
    pin_array *rows, *choices;
    int total;
    char error[256] = "";
    REQUIRE(
        runtime_properties_query(properties_world, entity, 0, 16, &native_storage, &rows, &choices, &total, error, sizeof error)
    );
    bool found = false;
    for (int i = 0; i < rows->count; i++) {
        const runtime_property *row = rows->items[i].ptr;
        if (!strcmp(row->component, "fixture.computed")) {
            REQUIRE(row->computed && !row->writable);
            found = true;
        }
    }
    REQUIRE(found);
    REQUIRE(
        !runtime_properties_write(properties_world, entity, "fixture.computed", "value", "7", error, sizeof error)
    );
    return true;
}
void test_workspace_runtime()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(runtime_computed_property_metadata_matches_write_policy);
    RUN_TEST_CASE(runtime_subject_properties_preserve_exact_values_and_generation);
    RUN_TEST_CASE(runtime_subject_properties_nested_offsets_and_named_enum_choices);
    RUN_TEST_CASE(runtime_optional_bindings_refuse_inactive_project_without_reading_inputs);
    RUN_TEST_CASE(runtime_reflection_scopes_restore_previous_project);
    RUN_TEST_CASE(detached_prepared_player_does_not_publish_target_project_types);
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
    RUN_TEST_CASE(runtime_private_sources_are_owned_exclusive_and_atomic);
    RUN_TEST_CASE(runtime_snapshot_owns_frozen_overlays_and_refuses_special_sources);
#endif
    pu_clear_setup();
}
