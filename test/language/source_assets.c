#include "assets/project.h"
#include "authoring/prototype_prepare.h"
#include "authoring/source_asset.h"
#include "authoring/source_catalog.h"
#include "backend.h"
#include "language/lower.h"
#include "runtime/scenes/prepared.h"
#include "support/arena.h"
#include "support/array.h"
#include "support/asset_text.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/catalog.h"
#include "types/text.h"
#ifdef CHIBI_TEST_RECORD_SERVICES
#include "runtime/behaviors/entities.h"
#include "runtime/behaviors/entities_native.h"
#endif

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct
{
    int current, maximum;
} health;
static const type_field health_fields[] = {
    { .name = "current", .type = PIN_I32, .size = sizeof(int), .offset = offsetof(health, current) },
    { .name = "maximum", .type = PIN_I32, .size = sizeof(int), .offset = offsetof(health, maximum) },
};
static const type_desc health_type = { .name = "AssetHealth",
                                       .size = sizeof(health),
                                       .fields = health_fields,
                                       .field_count = 2 };
static const type_desc *const health_types[] = { &health_type };
static const type_catalog_pack health_pack = { .types = health_types, .type_count = 1 };
static bool health_catalog_installed;
static const ecs_component_desc health_component = { .id = 12001,
                                                     .key = "test.Health",
                                                     .name = "Health",
                                                     .element_size = sizeof(health),
                                                     .type = &health_type };
static av_doc *document;
static ecs_relationship_desc *relation;
static prototype *authored;
static scene *prepared, *decoded;
static scene_instance *instance;
static ecs_world *world;
static source_catalog *project;
static script_syntax *syntax;
static script_analysis *analysis;
static execution_module *module;
static execution_program *program;
static unsigned char *bytes;
static char *base_text, *child_text;
static char error[256];
static char catalog_root[256];
static behavior_backend query_backend = BEHAVIOR_BYTECODE;
#ifdef CHIBI_TEST_RECORD_SERVICES
static entities *service;
#endif
static arena text_reads;
static void cleanup()
{
    arena_free(&text_reads);
    execution_program_free(program);
    execution_module_free(module);
    script_analysis_free(analysis);
    script_syntax_free(syntax);
    source_catalog_free(project);
    scene_instance_free(instance);
#ifdef CHIBI_TEST_RECORD_SERVICES
    if (service) {
        entities_free(service);
    }
    service = NULL;
#endif
    ecs_world_free(world);
    scene_free(prepared);
    scene_free(decoded);
    prototype_free(authored);
    authoring_relationship_source_free(relation);
    av_doc_free(document);
    free(bytes);
    free(base_text);
    free(child_text);
    program = NULL;
    module = NULL;
    analysis = NULL;
    syntax = NULL;
    project = NULL;
    instance = NULL;
    world = NULL;
    prepared = decoded = NULL;
    authored = NULL;
    relation = NULL;
    document = NULL;
    bytes = NULL;
    base_text = child_text = NULL;
    if (catalog_root[0]) {
        char path[512];
        snprintf(path, sizeof path, "%s/libraries/Plain.chibi", catalog_root);
        unlink(path);
        snprintf(path, sizeof path, "%s/libraries", catalog_root);
        rmdir(path);
        rmdir(catalog_root);
        catalog_root[0] = 0;
    }
    if (health_catalog_installed) {
        type_catalog_set_project(NULL);
        health_catalog_installed = false;
    }
}
TEST_CASE(source_catalog_analyzes_libraries_without_specialization_requests)
{
    char directory[] = "/tmp/chibi-source-no-requests.XXXXXX";
    REQUIRE(mkdtemp(directory));
    snprintf(catalog_root, sizeof catalog_root, "%s", directory);
    char path[512];
    snprintf(path, sizeof path, "%s/libraries", catalog_root);
    REQUIRE(!mkdir(path, 0700));
    snprintf(path, sizeof path, "%s/libraries/Plain.chibi", catalog_root);
    FILE *file = fopen(path, "wb");
    REQUIRE(file);
    bool written = fputs("Plain :: library { Identity :: (value: int) -> int { return value } }\n", file) >=
                   0;
    int closed = fclose(file);
    REQUIRE(written && !closed);

    const script_catalog empty = { 0 };
    project = source_catalog_new(catalog_root, &empty);
    REQUIRE(project);
    REQUIRE(!source_catalog_diagnostic_count(project));
    REQUIRE(source_catalog_source_count(project) == 1);
    int requests = -1;
    REQUIRE(!source_catalog_requests(project, &requests));
    REQUIRE(requests == 0);
    const script_catalog *view = source_catalog_view(project);
    REQUIRE(view->capability_count == 1);
    REQUIRE(!strcmp(view->capabilities[0].qualified_name, "Plain.Identity"));
    REQUIRE(view->capabilities[0].result_count == 1);
    REQUIRE(view->capabilities[0].results[0].type.storage == PIN_I32);
    REQUIRE(view->capabilities[0].pure);
    REQUIRE(!view->capabilities[0].effects);
    source_catalog_free(project);
    project = NULL;
    return true;
}

TEST_CASE(source_authored_relationship_rows_retire_and_restart_on_membership_change)
{
    relation = authoring_relationship_source_read("Targets :: relationship {}", error, sizeof error);
    REQUIRE(relation);
    script_catalog_type type = { .id = 12002,
                                 .name = health_type.name,
                                 .type = { .storage = PIN_STRUCT, .nominal = 12002 },
                                 .component = true,
                                 .component_id = health_component.id,
                                 .size = sizeof(health),
                                 .layout = type_layout_hash(&health_type) };
    script_catalog_relationship relationship = { .id = relation->id,
                                                 .name = relation->name };
    const script_catalog catalog = { .types = &type,
                                     .type_count = 1,
                                     .relationships = &relationship,
                                     .relationship_count = 1 };
    type_catalog_set_project(&health_pack);
    health_catalog_installed = true;
    syntax = script_parse(
        "Rows :: system(h: ^AssetHealth, target in self.Targets) { visits: "
        "retain "
        "int = 0; visits += 1 "
        "}"
    );
    analysis = script_analyze(syntax, &catalog);
    for (int i = 0; i < analysis->diagnostic_count; i++) {
        fprintf(stderr, "asset query: %s\n", analysis->diagnostics[i].message);
    }
    REQUIRE(analysis->valid);
    script_lower_bindings bindings = { 0 };
    execution_module_plan *plan = script_lower(analysis, &catalog, &bindings);
    REQUIRE(plan && plan->valid);
    module = execution_module_finish(&plan, query_backend, error, sizeof error);
    REQUIRE(module);
    world = ecs_world_new();
    REQUIRE(!ecs_world_register_component(world, &health_component, NULL));
    REQUIRE(!ecs_world_register_relationship(world, relation, NULL));
    ecs_entity owner = ecs_world_create_entity(world),
               target = ecs_world_create_entity(world);
    REQUIRE(!ecs_world_record_add(world, owner, health_component.id, NULL, 0));
    REQUIRE(!ecs_world_record_add_pair(world, owner, relation->id, target, 0));
    REQUIRE(!ecs_world_apply_commands(world));
    program = execution_program_new(module, world, NULL);
    REQUIRE(program);
    REQUIRE(!execution_program_step(program, 0, .1, 100000));
    uint64_t target_id = ecs_entity_to_u64(target), state = 0;
    for (int i = 0; i < analysis->symbol_count; i++) {
        if (analysis->symbols[i].kind == SCRIPT_SHARED_STATE &&
            !strcmp(analysis->symbols[i].name, "visits")) {
            state = analysis->symbols[i].id;
        }
    }
    execution_instance *row = execution_program_row(program, ecs_entity_to_u64(owner), &target_id, 1);
    REQUIRE(row && state);
    REQUIRE(execution_instance_state(row, state)->i == 1);
    REQUIRE(!execution_program_step(program, .1, .1, 100000));
    REQUIRE(execution_instance_state(row, state)->i == 2);
    REQUIRE(!ecs_world_record_remove_pair(world, owner, relation->id, target, 0));
    REQUIRE(!ecs_world_apply_commands(world));
    REQUIRE(!execution_program_step(program, .2, .1, 100000));
    REQUIRE(!execution_program_row(program, ecs_entity_to_u64(owner), &target_id, 1));
    REQUIRE(!ecs_world_record_add_pair(world, owner, relation->id, target, 0));
    REQUIRE(!ecs_world_apply_commands(world));
    REQUIRE(!execution_program_step(program, .3, .1, 100000));
    row = execution_program_row(program, ecs_entity_to_u64(owner), &target_id, 1);
    /* Membership retires the row, not the registration-owned binding. */
    REQUIRE(row && execution_instance_state(row, state)->i == 3);
    REQUIRE(!ecs_world_destroy_entity(world, target));
    REQUIRE(!execution_program_step(program, .4, .1, 100000));
    REQUIRE(!execution_program_row(program, ecs_entity_to_u64(owner), &target_id, 1));
    return true;
}
#ifdef CHIBI_TEST_RECORD_SERVICES
TEST_CASE(source_relationship_native_commands_are_deferred)
{
    relation = authoring_relationship_source_read(
        "Targets :: relationship {exclusive:true}",
        error,
        sizeof error
    );
    REQUIRE(relation);
    world = ecs_world_new();
    REQUIRE(!ecs_world_register_relationship(world, relation, NULL));
    service = entities_new(world, NULL);
    ecs_entity owner = ecs_world_create_entity(world),
               target = ecs_world_create_entity(world);
    const pin_value args[] = {
        { .type = PIN_HANDLE, .type_name = "ecs.entity", .u64 = ecs_entity_to_u64(owner) },
        { .type = PIN_U64, .u64 = relation->id },
        { .type = PIN_HANDLE, .type_name = "ecs.entity", .u64 = ecs_entity_to_u64(target) },
    };
    REQUIRE(!ENTITY_ADD_PAIR_COMMAND.invoke(service, NULL, args, 3));
    pin_value result = { .type = PIN_BOOL };
    REQUIRE(!ENTITY_HAS_PAIR_CALL.invoke(service, args, &result));
    REQUIRE(!result.b);
    REQUIRE(!ecs_world_apply_commands(world));
    REQUIRE(!ENTITY_HAS_PAIR_CALL.invoke(service, args, &result));
    REQUIRE(result.b);
    REQUIRE(!ENTITY_REMOVE_PAIR_COMMAND.invoke(service, NULL, args, 3));
    REQUIRE(ecs_has_pair(world, owner, relation->id, target));
    REQUIRE(!ecs_world_apply_commands(world));
    REQUIRE(!ecs_has_pair(world, owner, relation->id, target));
    return true;
}
#endif
TEST_CASE(source_authored_relationship_compiled_rows_retire_and_restart)
{
    query_backend = TEST_COMPILED_BACKEND;
    bool valid = source_authored_relationship_rows_retire_and_restart_on_membership_change();
    query_backend = BEHAVIOR_BYTECODE;
    return valid;
}
TEST_CASE(source_scene_release_preserves_external_panic_and_cleans_internal_links)
{
    relation = authoring_relationship_source_read(
        "Targets :: relationship { on_delete_target: Panic }",
        error,
        sizeof error
    );
    REQUIRE(relation);
    const scene_object objects[] = { { .name = "owner" }, { .name = "target" } };
    const int parents[] = { -1, 0 };
    const scene_pair pair = { 0, 0, 1 };
    const scene_spec spec = { .name = "PanicRelease",
                              .physics = physics_defaults(),
                              .objects = objects,
                              .object_count = 2,
                              .parents = parents,
                              .relationships = relation,
                              .relationship_count = 1,
                              .pairs = &pair,
                              .pair_count = 1 };
    prepared = scene_prepare(&spec, error, sizeof error);
    REQUIRE(prepared);
    world = ecs_world_new();
    REQUIRE(!scene_register_components(prepared, world, error, sizeof error));
    instance = scene_instantiate_components(prepared, world, error, sizeof error);
    REQUIRE(instance);
    ecs_entity owner = scene_instance_entity(instance, 0),
               target = scene_instance_entity(instance, 1);
    ecs_entity external = ecs_world_create_entity(world);
    REQUIRE(!ecs_world_record_add_pair(world, external, relation->id, target, 0));
    REQUIRE(!ecs_world_apply_commands(world));
    REQUIRE(!scene_instance_release(instance, error, sizeof error));
    REQUIRE(strstr(error, "Panic"));
    REQUIRE(ecs_world_live_count(world) == 3);
    REQUIRE(ecs_has_pair(world, owner, relation->id, target));
    REQUIRE(ecs_has_pair(world, external, relation->id, target));
    REQUIRE(!ecs_world_record_remove_pair(world, external, relation->id, target, 0));
    REQUIRE(!ecs_world_apply_commands(world));
    REQUIRE(scene_instance_release(instance, error, sizeof error));
    instance = NULL;
    REQUIRE(!ecs_entity_is_alive(world, owner));
    REQUIRE(!ecs_entity_is_alive(world, target));
    REQUIRE(ecs_entity_is_alive(world, external));
    REQUIRE(ecs_world_live_count(world) == 1);
    return true;
}

void test_language_source_assets()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(source_catalog_analyzes_libraries_without_specialization_requests);
    RUN_TEST_CASE(source_authored_relationship_rows_retire_and_restart_on_membership_change);
    RUN_TEST_CASE(source_authored_relationship_compiled_rows_retire_and_restart);
    RUN_TEST_CASE(source_scene_release_preserves_external_panic_and_cleans_internal_links);
#ifdef CHIBI_TEST_RECORD_SERVICES
    RUN_TEST_CASE(source_relationship_native_commands_are_deferred);
#endif
    pu_clear_setup();
}
