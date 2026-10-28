#include "assets/project.h"
#include "authoring/prototype_prepare.h"
#include "authoring/source_asset.h"
#include "authoring/source_catalog.h"
#include "authoring/source_prototype_edit.h"
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
static const ecs_component_desc *const components[] = { &health_component };
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
static const char *include(const char *name, void *)
{
    return !strcmp(name, "Enemy") ? base_text
         : !strcmp(name, "Child") ? child_text
                                  : NULL;
}
TEST_CASE(source_prototype_composition_normalizes_fields_and_overrides_defaults)
{
    prototype_registry registry = { .components = components,
                                    .component_count = 1,
                                    .include = include };
    document = authoring_prototype_source_read(
        "Enemy :: prototype { Health { Current: 100, maximum: 150 } }",
        &registry,
        error,
        sizeof error
    );
    REQUIRE(document);
    base_text = av_doc_write(document);
    av_doc_free(document);
    document = authoring_prototype_source_read(
        "Elite :: prototype(Enemy) { Health { current: 200 } }",
        &registry,
        error,
        sizeof error
    );
    REQUIRE(document);
    authored = prototype_load_value(av_doc_root(document), &registry, error, sizeof error);
    REQUIRE(authored);
    pin_value v;
    REQUIRE(!prototype_read_field(&text_reads, authored, "Health", "current", &v));
    REQUIRE(v.i == 200);
    REQUIRE(!prototype_read_field(&text_reads, authored, "Health", "maximum", &v));
    REQUIRE(v.i == 150);
    REQUIRE(!authoring_prototype_source_read(
        "Bad :: prototype { Health { current: 1, Current: 2 } }",
        &registry,
        error,
        sizeof error
    ));
    REQUIRE(!authoring_prototype_source_read(
        "Bad :: prototype { Health { missing: 1 } }",
        &registry,
        error,
        sizeof error
    ));
    REQUIRE(!authoring_prototype_source_read(
        "Bad :: prototype { Health { current: random() } }",
        &registry,
        error,
        sizeof error
    ));
    return true;
}
TEST_CASE(source_relationships_survive_preparation_encoding_and_target_cleanup)
{
    relation = authoring_relationship_source_read(
        "Targets :: relationship { exclusive: true, acyclic: true, "
        "on_delete_target: Remove }",
        error,
        sizeof error
    );
    REQUIRE(relation);
    const ecs_relationship_desc *relations[] = { relation };
    prototype_registry registry = { .components = components,
                                    .component_count = 1,
                                    .relationships = relations,
                                    .relationship_count = 1,
                                    .include = include };
    document = authoring_prototype_source_read(
        "Child :: prototype { Health { current: 5 } }",
        &registry,
        error,
        sizeof error
    );
    REQUIRE(document);
    child_text = av_doc_write(document);
    av_doc_free(document);
    document = authoring_prototype_source_read(
        "Squad :: prototype { Health { current: 100 } "
        "children: [{name: \"enemy\", prototype: Child}], "
        "links: {Targets: child.enemy} }",
        &registry,
        error,
        sizeof error
    );
    REQUIRE(document);
    authored = prototype_load_value(av_doc_root(document), &registry, error, sizeof error);
    REQUIRE(authored);
    prepared = authoring_prototype_prepare(authored, error, sizeof error);
    REQUIRE(prepared);
    REQUIRE(scene_pair_count(prepared) == 1);
    type_catalog_set_project(&health_pack);
    health_catalog_installed = true;
    size_t length = 0;
    bool written = scene_prepared_write(prepared, &bytes, &length, error, sizeof error);
    if (!written) {
        fprintf(stderr, "source prepared serialization: %s\n", error);
    }
    REQUIRE(written);
    decoded = scene_prepared_read(bytes, length, (ecs_component_catalog){ components, 1 }, error, sizeof error);
    REQUIRE(decoded);
    /* Direct spawning must reject the same ID/name with a different stable
     * schema key, including native relationships without keys. */
    const char *keys[] = { "other.Targets", NULL };
    for (int i = 0; i < 2; i++) {
        world = ecs_world_new();
        ecs_relationship_desc conflicting = *relation;
        conflicting.key = keys[i];
        REQUIRE(!ecs_world_register_relationship(world, &conflicting, NULL));
        ecs_entity spawned = { 0 };
        REQUIRE(
            prototype_spawn_components(authored, world, &spawned, error, sizeof error) == ECS_ERR_DUPLICATE
        );
        REQUIRE(!spawned.slot && ecs_world_live_count(world) == 0);
        REQUIRE(strstr(error, "relationship schema"));
        ecs_world_free(world);
        world = NULL;
    }
    world = ecs_world_new();
    REQUIRE(!scene_register_components(decoded, world, error, sizeof error));
    instance = scene_instantiate_components(decoded, world, error, sizeof error);
    REQUIRE(instance);
    ecs_entity owner = scene_instance_entity(instance, 0),
               target = scene_instance_entity(instance, 1);
    REQUIRE(ecs_has_pair(world, owner, relation->id, target));
    ecs_entity next = ecs_world_create_entity(world);
    REQUIRE(!ecs_world_record_add_pair(world, owner, relation->id, next, 0));
    REQUIRE(!ecs_world_apply_commands(world));
    REQUIRE(!ecs_has_pair(world, owner, relation->id, target));
    REQUIRE(ecs_has_pair(world, owner, relation->id, next));
    REQUIRE(!ecs_world_destroy_entity(world, next));
    REQUIRE(!ecs_pair_target_count(world, owner, relation->id));
    return true;
}
TEST_CASE(source_relationship_declarations_and_initial_pairs_reject_invalid_contracts)
{
    const char *quoted[] = {
        "Bad :: prototype { behaviors: [\"Probe\"] }",
        "Bad :: prototype { behaviors: [Probe, \"Other\"] }",
        "Bad :: prototype { links: {Targets: \"child.enemy\"} }",
        "Bad :: prototype { links: {Targets: [child.enemy, \"child.other\"]} }",
        ("Bad :: prototype { children: [{name: \"enemy\", prototype: "
         "\"Child\"}] }"),
    };
    for (size_t i = 0; i < sizeof quoted / sizeof *quoted; i++) {
        REQUIRE(!authoring_prototype_source_read(quoted[i], NULL, error, sizeof error));
        REQUIRE(strstr(error, "not a string"));
    }
    REQUIRE(!authoring_relationship_source_read(
        "Bad :: relationship {on_delete_target: \"Remove\"}",
        error,
        sizeof error
    ));
    REQUIRE(strstr(error, "not a string"));
    /* Symbol-only references must not restrict actual component text or
     * the user-visible names of child entities. */
    const type_field label_field = { .name = "label",
                                     .type = PIN_STR,
                                     .size = sizeof(char *) };
    const type_desc caption_type = { .name = "Caption",
                                     .size = sizeof(char *),
                                     .fields = &label_field,
                                     .field_count = 1 };
    const ecs_component_desc caption = { .id = 12003,
                                         .key = "test.Caption",
                                         .name = "Caption",
                                         .type = &caption_type,
                                         .element_size = sizeof(char *) };
    const ecs_component_desc *caption_components[] = { &caption };
    const prototype_registry caption_registry = { .components = caption_components,
                                                  .component_count = 1 };
    document = authoring_prototype_source_read(
        "Good :: prototype { Caption {label: \"Probe\"} behaviors:[Probe]; "
        "children:[{name:\"enemy\",prototype:Child}], "
        "links:{Targets:child.enemy} }",
        &caption_registry,
        error,
        sizeof error
    );
    REQUIRE(document);
    const av_value *root = av_doc_root(document);
    REQUIRE(
        !strcmp(av_get_str(av_obj_get(av_obj_get(av_obj_get(root, "components"), "Caption"), "label")), "Probe")
    );
    REQUIRE(
        !strcmp(av_get_str(av_obj_get(av_arr_get(av_obj_get(root, "children"), 0), "name")), "enemy")
    );
    REQUIRE(!authoring_relationship_source_read(
        "First :: relationship {} Second :: relationship {}",
        error,
        sizeof error
    ));
    REQUIRE(strstr(error, "Invalid authored asset declaration"));
    REQUIRE(
        !authoring_prototype_source_read("First :: prototype {} Second :: prototype {}", NULL, error, sizeof error)
    );
    REQUIRE(strstr(error, "Invalid authored asset declaration"));
    REQUIRE(
        !authoring_relationship_source_read("Bad :: relationship { exclusive: 1 }", error, sizeof error)
    );
    REQUIRE(!authoring_relationship_source_read(
        "Bad :: relationship { on_delete_target: Unknown }",
        error,
        sizeof error
    ));
    REQUIRE(
        !authoring_relationship_source_read("Bad :: relationship { traversable: true }", error, sizeof error)
    );
    relation = authoring_relationship_source_read(
        "Owns :: relationship { exclusive:true, acyclic:true, "
        "on_delete_target:Delete }",
        error,
        sizeof error
    );
    REQUIRE(relation);
    const scene_object objects[] = { { .name = "a" }, { .name = "b" }, { .name = "c" } };
    const scene_pair cycle[] = { { 0, 0, 1 }, { 1, 0, 0 } };
    scene_spec spec = { .name = "Pairs",
                        .physics = physics_defaults(),
                        .objects = objects,
                        .object_count = 3,
                        .relationships = relation,
                        .relationship_count = 1,
                        .pairs = cycle,
                        .pair_count = 2 };
    REQUIRE(!scene_prepare(&spec, error, sizeof error));
    const scene_pair exclusive[] = { { 0, 0, 1 }, { 0, 0, 2 } };
    spec.pairs = exclusive;
    REQUIRE(!scene_prepare(&spec, error, sizeof error));
    spec.pairs = cycle;
    spec.pair_count = 1;
    prepared = scene_prepare(&spec, error, sizeof error);
    if (!prepared) {
        fprintf(stderr, "source initial pair: %s\n", error);
    }
    REQUIRE(prepared);
    world = ecs_world_new();
    REQUIRE(!scene_register_components(prepared, world, error, sizeof error));
    instance = scene_instantiate_components(prepared, world, error, sizeof error);
    REQUIRE(instance);
    ecs_entity owner = scene_instance_entity(instance, 0),
               target = scene_instance_entity(instance, 1);
    REQUIRE(!ecs_world_destroy_entity(world, target));
    REQUIRE(!ecs_entity_is_alive(world, owner));
    return true;
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
    for (int migration = 0; migration < 2; migration++) {
        project = migration ? source_catalog_new_for_migration(catalog_root, &empty)
                            : source_catalog_new(catalog_root, &empty);
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
    }
    return true;
}

TEST_CASE(source_asset_catalog_tracks_prototype_bases_and_relationship_symbols)
{
    const char *root = "/__chibi_source_asset_test__";
    const source_catalog_edit edits[] = {
        { .path = "/__chibi_source_asset_test__/prototypes/Enemy.chibi",
          .text = "Enemy :: prototype {}" },
        { .path = "/__chibi_source_asset_test__/prototypes/Elite.chibi",
          .text = "Elite :: prototype(Enemy) {}" },
        { .path = "/__chibi_source_asset_test__/relationships/Targets.chibi",
          .text = "Targets :: relationship { exclusive: true }" },
    };
    const script_catalog empty = { 0 };
    project = source_catalog_edited(root, &empty, edits, 3);
    REQUIRE(!source_catalog_diagnostic_count(project));
    REQUIRE(source_catalog_view(project)->relationship_count == 1);
    REQUIRE(source_catalog_reference_count(project) == 1);
    const source_catalog_reference *reference = source_catalog_references(project);
    REQUIRE(reference->kind == SOURCE_REFERENCE_PROTOTYPE);
    REQUIRE(reference->id == script_catalog_id("prototype", "Enemy"));
    REQUIRE(reference->text_kind == SOURCE_TEXT_CHIBI);
    REQUIRE(!strncmp(reference->text + reference->range.start, "Enemy", 5));
    syntax = script_parse("Elite :: prototype(Enemy) {}");
    authoring_completions completions = { 0 };
    int offset = (int)(strstr(syntax->text, "Enemy") - syntax->text) + 2;
    authoring_asset_source_complete(syntax, project, offset, &completions);
    REQUIRE(completions.count == 1);
    REQUIRE(!strcmp(completions.items[0].label, "Enemy"));
    REQUIRE(completions.items[0].target.id == reference->id);
    authoring_completions_free(&completions);
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
static int refuse_panic_graph(ecs_world *spawn_world, ecs_entity entity, const char *, void *user)
{
    bool *saw_pair = user;
    *saw_pair = ecs_world_live_count(spawn_world) == 2 &&
                ecs_pair_target_count(spawn_world, entity, relation->id) == 1;
    return -1;
}
TEST_CASE(source_prototype_failed_attachment_rolls_back_internal_panic_links)
{
    relation = authoring_relationship_source_read(
        "Targets :: relationship { on_delete_target: Panic }",
        error,
        sizeof error
    );
    REQUIRE(relation);
    const ecs_relationship_desc *relations[] = { relation };
    prototype_registry registry = { .relationships = relations,
                                    .relationship_count = 1,
                                    .include = include };
    document = authoring_prototype_source_read("Child :: prototype {}", &registry, error, sizeof error);
    REQUIRE(document);
    child_text = av_doc_write(document);
    av_doc_free(document);
    document = authoring_prototype_source_read(
        "Squad :: prototype { children: [{name: \"enemy\", prototype: Child}]; "
        "links: {Targets: child.enemy} }",
        &registry,
        error,
        sizeof error
    );
    REQUIRE(document);
    authored = prototype_load_value(av_doc_root(document), &registry, error, sizeof error);
    REQUIRE(authored);
    REQUIRE(!prototype_add_graph(authored, "Fail"));
    world = ecs_world_new();
    bool saw_pair = false;
    const prototype_spawn_hooks hooks = { .attach = refuse_panic_graph,
                                          .attach_user = &saw_pair };
    ecs_entity entity = { 0 };
    REQUIRE(prototype_spawn(authored, world, &hooks, &entity, error, sizeof error) == ECS_ERR_COMMAND);
    REQUIRE(saw_pair);
    REQUIRE(strstr(error, "could not attach"));
    REQUIRE(!entity.slot);
    REQUIRE(ecs_world_live_count(world) == 0);
    return true;
}
static int migration_refuse(const char *content, const char *start, int from, void *user, char *error, size_t size)
{
    (void)content;
    (void)start;
    *(int *)user = from;
    snprintf(error, size, "native finalization refused");
    return -1;
}

TEST_CASE(source_migration_callback_failure_keeps_old_manifest_retryable)
{
    char directory[] = "/tmp/chibi-source-finalize.XXXXXX";
    REQUIRE(mkdtemp(directory));
    char content[256], manifest[256];
    snprintf(content, sizeof content, "%s/content", directory);
    snprintf(manifest, sizeof manifest, "%s/project.kdl", directory);
    REQUIRE(!mkdir(content, 0700));
    FILE *file = fopen(manifest, "wb");
    REQUIRE(file);
    REQUIRE(
        fputs("version 25\nname \"Migration\"\ncontent \"content\"\nstart_scene \"Start.kdl\"\n", file) >= 0
    );
    REQUIRE(!fclose(file));
    chibi_project opened = { 0 };
    char error[256];
    int called = 0;
    REQUIRE(
        chibi_project_open_migrating(&opened, directory, migration_refuse, &called, error, sizeof error) ==
        -1
    );
    REQUIRE(called == 25);
    REQUIRE(strstr(error, "native finalization refused"));
    file = fopen(manifest, "rb");
    REQUIRE(file);
    char bytes[256] = { 0 };
    REQUIRE(fread(bytes, 1, sizeof bytes - 1, file) > 0);
    fclose(file);
    REQUIRE(strstr(bytes, "version 25"));
    REQUIRE(!chibi_project_inspect(&opened, directory, error, sizeof error));
    chibi_project_free(&opened);
    REQUIRE(chibi_project_migrate(directory, error, sizeof error) == CHIBI_PROJECT_VERSION - 25);
    REQUIRE(!chibi_project_read(&opened, directory, error, sizeof error));
    chibi_project_free(&opened);
    unlink(manifest);
    rmdir(content);
    rmdir(directory);
    return true;
}

void test_language_source_assets()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(source_migration_callback_failure_keeps_old_manifest_retryable);
    RUN_TEST_CASE(source_prototype_composition_normalizes_fields_and_overrides_defaults);
    RUN_TEST_CASE(source_relationships_survive_preparation_encoding_and_target_cleanup);
    RUN_TEST_CASE(source_relationship_declarations_and_initial_pairs_reject_invalid_contracts);
    RUN_TEST_CASE(source_catalog_analyzes_libraries_without_specialization_requests);
    RUN_TEST_CASE(source_asset_catalog_tracks_prototype_bases_and_relationship_symbols);
    RUN_TEST_CASE(source_authored_relationship_rows_retire_and_restart_on_membership_change);
    RUN_TEST_CASE(source_authored_relationship_compiled_rows_retire_and_restart);
    RUN_TEST_CASE(source_scene_release_preserves_external_panic_and_cleans_internal_links);
    RUN_TEST_CASE(source_prototype_failed_attachment_rolls_back_internal_panic_links);
#ifdef CHIBI_TEST_RECORD_SERVICES
    RUN_TEST_CASE(source_relationship_native_commands_are_deferred);
#endif
    pu_clear_setup();
}

/* Bounded source-edit bridge cases. No worlds, hosts, rendering or files. */
typedef struct
{
    float x, y;
} bridge_vector;
typedef struct
{
    bridge_vector position;
    const char *label;
    uint64_t target;
    int mode;
    int64_t amount;
    const char *prefab;
} bridge_sample;
static const type_field bridge_vector_fields[] = {
    { .name = "x", .type = PIN_F32, .offset = offsetof(bridge_vector, x), .size = sizeof(float) },
    { .name = "y", .type = PIN_F32, .offset = offsetof(bridge_vector, y), .size = sizeof(float) },
};
static const type_desc bridge_vector_type = { .name = "BridgeVector",
                                              .size = sizeof(bridge_vector),
                                              .fields = bridge_vector_fields,
                                              .field_count = 2 };
static const type_field bridge_sample_fields[] = {
    { .name = "position",
      .type = PIN_STRUCT,
      .type_name = "BridgeVector",
      .offset = offsetof(bridge_sample, position),
      .size = sizeof(bridge_vector) },
    { .name = "label",
      .type = PIN_STR,
      .offset = offsetof(bridge_sample, label),
      .size = sizeof(char *) },
    { .name = "target",
      .type = PIN_HANDLE,
      .type_name = "ecs.entity",
      .offset = offsetof(bridge_sample, target),
      .size = sizeof(uint64_t) },
    { .name = "mode",
      .type = PIN_ENUM,
      .type_name = "BridgeMode",
      .offset = offsetof(bridge_sample, mode),
      .size = sizeof(int) },
    { .name = "amount",
      .type = PIN_I64,
      .offset = offsetof(bridge_sample, amount),
      .size = sizeof(int64_t) },
    { .name = "prefab",
      .type = PIN_NAME,
      .offset = offsetof(bridge_sample, prefab),
      .size = sizeof(char *) },
};
static const type_desc bridge_sample_type = { .name = "BridgeSample",
                                              .size = sizeof(bridge_sample),
                                              .fields = bridge_sample_fields,
                                              .field_count = 6 };
static const enum_value_desc bridge_modes[] = { { 0, "Idle", "Idle" },
                                                { 1, "Moving", "Moving" } };
static const enum_desc bridge_mode = { "BridgeMode", bridge_modes, 2 };
static const enum_desc *const bridge_enums[] = { &bridge_mode };
static const type_desc *const bridge_types[] = { &health_type,
                                                 &bridge_vector_type,
                                                 &bridge_sample_type };
static const type_catalog_pack bridge_pack = { bridge_types, 3, bridge_enums, 1 };
static const ecs_component_desc bridge_sample_component = {
    .id = 12003,
    .key = "test.Sample",
    .name = "Sample",
    .element_size = sizeof(bridge_sample),
    .type = &bridge_sample_type
};
static const ecs_component_desc *const bridge_components[] = { &health_component,
                                                               &bridge_sample_component };
static const script_catalog_field bridge_health_catalog_fields[] = {
    { .name = "current", .type = { .storage = PIN_I32 }, .offset = offsetof(health, current) },
    { .name = "maximum", .type = { .storage = PIN_I32 }, .offset = offsetof(health, maximum) },
};
static const script_catalog_field bridge_vector_catalog_fields[] = {
    { .name = "x", .type = { .storage = PIN_F32 }, .offset = offsetof(bridge_vector, x) },
    { .name = "y", .type = { .storage = PIN_F32 }, .offset = offsetof(bridge_vector, y) },
};
static script_catalog_field bridge_sample_catalog_fields[] = {
    { .name = "position",
      .type = { .storage = PIN_STRUCT, .nominal = 12004 },
      .offset = offsetof(bridge_sample, position) },
    { .name = "label",
      .type = { .storage = PIN_STR },
      .offset = offsetof(bridge_sample, label),
      .c_string = true },
    { .name = "target",
      .type = { .storage = PIN_HANDLE },
      .offset = offsetof(bridge_sample, target) },
    { .name = "mode",
      .type = { .storage = PIN_ENUM, .nominal = 12005 },
      .offset = offsetof(bridge_sample, mode) },
    { .name = "amount",
      .type = { .storage = PIN_I64 },
      .offset = offsetof(bridge_sample, amount) },
    { .name = "prefab", .offset = offsetof(bridge_sample, prefab) },
};
static const script_catalog_type bridge_catalog_types[] = {
    { .id = 12002,
      .name = "AssetHealth",
      .type = { .storage = PIN_STRUCT, .nominal = 12002 },
      .fields = bridge_health_catalog_fields,
      .field_count = 2,
      .size = sizeof(health),
      .component = true,
      .component_id = 12001 },
    { .id = 12003,
      .name = "BridgeSample",
      .type = { .storage = PIN_STRUCT, .nominal = 12003 },
      .fields = bridge_sample_catalog_fields,
      .field_count = 6,
      .size = sizeof(bridge_sample),
      .component = true,
      .component_id = 12003 },
    { .id = 12004,
      .name = "BridgeVector",
      .type = { .storage = PIN_STRUCT, .nominal = 12004 },
      .fields = bridge_vector_catalog_fields,
      .field_count = 2,
      .size = sizeof(bridge_vector) },
    { .id = 12005,
      .name = "BridgeMode",
      .type = { .storage = PIN_ENUM, .nominal = 12005 },
      .size = sizeof(int) },
};
static const script_catalog_capability bridge_capabilities[] = {
    { .id = 12006, .name = "Enemy", .kind = SCRIPT_CAPABILITY_PROTOTYPE },
};
static const script_catalog bridge_catalog = { .types = bridge_catalog_types,
                                               .type_count = 4,
                                               .capabilities = bridge_capabilities,
                                               .capability_count = 1 };
static prototype_registry bridge_registry()
{
    bridge_sample_catalog_fields[5].type = script_reference_type(SCRIPT_REFERENCE_PROTOTYPE);
    type_catalog_set_project(&bridge_pack);
    health_catalog_installed = true;
    return (prototype_registry){ .components = bridge_components,
                                 .component_count = 2,
                                 .include = include,
                                 .decode = authoring_prototype_document_read };
}
static authoring_prototype_edit_result bridge_result;
static void bridge_cleanup()
{
    authoring_prototype_edit_result_free(&bridge_result);
    cleanup();
}
static bool bridge_edit(
    const char *source,
    const prototype_registry *registry,
    const authoring_prototype_edit *ops,
    int count
)
{
    authoring_prototype_edit_result_free(&bridge_result);
    bool ok = authoring_prototype_source_edit(
        source,
        registry,
        &bridge_catalog,
        ops,
        count,
        &bridge_result,
        error,
        sizeof error
    );
    if (!ok) {
        fprintf(stderr, "prototype source edit: %s\n", error);
    }
    return ok;
}
TEST_CASE(source_edit_replaces_only_the_normalized_value_span)
{
    prototype_registry registry = bridge_registry();
    const char *source = "// keep header\nElite :: prototype { HeAlTh { "
                         "Cu_rRent: 0x64, /* keep */ maximum: 150 } }\n";
    authoring_prototype_edit op = { .kind = AUTHORING_PROTOTYPE_SET_FIELD,
                                    .component = "Health",
                                    .field = "current",
                                    .value = { .type = PIN_I32, .i = 200 } };
    REQUIRE(bridge_edit(source, &registry, &op, 1));
    REQUIRE(bridge_result.changed && !bridge_result.materialized);
    REQUIRE(
        !strcmp(bridge_result.source, "// keep header\nElite :: prototype { HeAlTh { Cu_rRent: 200, /* keep */ maximum: 150 } }\n")
    );
    pin_value v;
    REQUIRE(
        !prototype_read_field(&text_reads, bridge_result.value, "Health", "current", &v) &&
        v.i == 200
    );
    REQUIRE(
        !strcmp(source, "// keep header\nElite :: prototype { HeAlTh { Cu_rRent: 0x64, /* keep */ maximum: 150 } }\n")
    );
    op.value.i = 100;
    REQUIRE(bridge_edit(source, &registry, &op, 1));
    REQUIRE(!bridge_result.changed && !strcmp(bridge_result.source, source));
    authoring_prototype_edit_result_free(&bridge_result);
    REQUIRE(
        authoring_prototype_source_edit(source, &registry, NULL, NULL, 0, &bridge_result, error, sizeof error)
    );
    REQUIRE(
        !bridge_result.changed && bridge_result.value &&
        !strcmp(bridge_result.source, source)
    );
    return true;
}
TEST_CASE(source_edit_sparse_component_keeps_other_base_fields_live)
{
    prototype_registry registry = bridge_registry();
    base_text = strdup("Enemy :: prototype { Health { current: 100, maximum: 150 } }");
    authoring_prototype_edit op = { .kind = AUTHORING_PROTOTYPE_SET_FIELD,
                                    .component = "Health",
                                    .field = "current",
                                    .value = { .type = PIN_I32, .i = 200 } };
    REQUIRE(bridge_edit("Elite :: prototype(Enemy) { /* local note */ }", &registry, &op, 1));
    REQUIRE(!bridge_result.materialized && strstr(bridge_result.source, "prototype(Enemy)"));
    REQUIRE(
        strstr(bridge_result.source, "/* local note */") &&
        !strstr(bridge_result.source, "maximum")
    );
    free(base_text);
    base_text = strdup("Enemy :: prototype { Health { current: 110, maximum: 175 } }");
    document = authoring_prototype_source_read(bridge_result.source, &registry, error, sizeof error);
    REQUIRE(document);
    authored = prototype_load_value(av_doc_root(document), &registry, error, sizeof error);
    pin_value v;
    REQUIRE(
        authored &&
        !prototype_read_field(&text_reads, authored, "Health", "current", &v) &&
        v.i == 200
    );
    REQUIRE(!prototype_read_field(&text_reads, authored, "Health", "maximum", &v) && v.i == 175);
    return true;
}
TEST_CASE(source_edit_nested_axis_obeys_existing_whole_record_semantics)
{
    prototype_registry registry = bridge_registry();
    base_text = strdup("Enemy :: prototype { Sample { position: {x: 1, y: 2} } }");
    authoring_prototype_edit op = { .kind = AUTHORING_PROTOTYPE_SET_FIELD,
                                    .component = "Sample",
                                    .field = "position.x",
                                    .value = { .type = PIN_F32, .f = 9 } };
    REQUIRE(bridge_edit("Elite :: prototype(Enemy) {}", &registry, &op, 1));
    pin_value v;
    REQUIRE(!prototype_read_field(&text_reads, bridge_result.value, "Sample", "position", &v));
    REQUIRE(((bridge_vector *)v.ptr)->x == 9 && ((bridge_vector *)v.ptr)->y == 2);
    REQUIRE(strstr(bridge_result.source, "prototype(Enemy)") && !bridge_result.materialized);
    const char *explicit_source = "Elite :: prototype { Sample { position: { "
                                  "x: 1, /* axis */ y: 2 } } }";
    REQUIRE(bridge_edit(explicit_source, &registry, &op, 1));
    REQUIRE(
        !strcmp(bridge_result.source, "Elite :: prototype { Sample { position: { x: 9, /* axis */ y: 2 } } }")
    );
    return true;
}
TEST_CASE(source_edit_add_uses_explicit_creation_payload_and_local_remove)
{
    prototype_registry registry = bridge_registry();
    bridge_sample defaults = { .position = { 4, 8 },
                               .label = "a\"b\\c\n",
                               .mode = 1,
                               .amount = INT64_MIN,
                               .prefab = "Enemy" };
    bridge_sample original;
    memcpy(&original, &defaults, sizeof original);
    authoring_prototype_edit add = {
        .kind = AUTHORING_PROTOTYPE_ADD_COMPONENT,
        .component = "Sample",
        .value = { .type = PIN_STRUCT, .type_name = "BridgeSample", .ptr = &defaults }
    };
    REQUIRE(
        bridge_edit("Elite :: prototype { /* other */ Health {current:1} }", &registry, &add, 1)
    );
    pin_value v;
    REQUIRE(!memcmp(&defaults, &original, sizeof defaults));
    REQUIRE(strstr(bridge_result.source, "prefab: Enemy"));
    REQUIRE(
        !prototype_read_field(&text_reads, bridge_result.value, "Sample", "label", &v) &&
        !strcmp(pin_text(v).bytes, defaults.label)
    );
    REQUIRE(
        !prototype_read_field(&text_reads, bridge_result.value, "Sample", "amount", &v) &&
        v.i64 == INT64_MIN
    );
    char *added = strdup(bridge_result.source);
    authoring_prototype_edit remove = { .kind = AUTHORING_PROTOTYPE_REMOVE_COMPONENT,
                                        .component = "Sample" };
    REQUIRE(bridge_edit(added, &registry, &remove, 1));
    free(added);
    REQUIRE(
        !bridge_result.materialized &&
        strstr(bridge_result.source, "/* other */ Health {current:1}")
    );
    REQUIRE(prototype_read_field(&text_reads, bridge_result.value, "Sample", "position", &v));
    defaults.label = NULL;
    REQUIRE(bridge_edit("Elite :: prototype {}", &registry, &add, 1));
    REQUIRE(
        !prototype_read_field(&text_reads, bridge_result.value, "Sample", "label", &v) &&
        pin_text(v).length == 0
    );
    return true;
}
TEST_CASE(source_edit_inherited_remove_preserves_children_links_and_behavior_order)
{
    prototype_registry registry = bridge_registry();
    relation = authoring_relationship_source_read("Targets :: relationship {}", error, sizeof error);
    REQUIRE(relation);
    const ecs_relationship_desc *relationships[] = { relation };
    registry.relationships = relationships;
    registry.relationship_count = 1;
    child_text = strdup("Child :: prototype { Health {current:5} }");
    base_text = strdup(
        "Enemy :: prototype { Health {current:100} Sample {position:{x:3,y:4}, "
        "target: child.enemy} "
        "children:[{name:\"enemy\",prototype:Child,retained_components:{Health:"
        "{maximum:9}}}]; "
        "links:{Targets:child.enemy}; behaviors:[Zulu, Alpha]; }"
    );
    const char
        *source = "Elite :: prototype(Enemy) { /* keep */ Sample { label: "
                  "\"local\" } identity:\"stable-id\"; behaviors:[Local]; }";
    authoring_prototype_edit remove = { .kind = AUTHORING_PROTOTYPE_REMOVE_COMPONENT,
                                        .component = "Health" };
    REQUIRE(bridge_edit(source, &registry, &remove, 1));
    REQUIRE(bridge_result.materialized && !strstr(bridge_result.source, "prototype(Enemy)"));
    REQUIRE(strstr(bridge_result.source, "/* keep */ Sample { label: \"local\""));
    REQUIRE(!strcmp(prototype_identity(bridge_result.value), "stable-id"));
    REQUIRE(
        acount(bridge_result.value->children->entities) == 1 &&
        acount(bridge_result.value->child_prototypes) == 1
    );
    av_value *retained = bridge_result.value->children->entities[0].retained;
    REQUIRE(
        av_obj_size(retained) == 1 &&
        av_get_int(av_obj_get(av_obj_value_at(retained, 0), "maximum")) == 9
    );
    REQUIRE(
        acount(bridge_result.value->links) == 1 &&
        !strcmp(bridge_result.value->links[0].path, "child.enemy")
    );
    REQUIRE(
        acount(bridge_result.value->pairs) == 1 &&
        !strcmp(bridge_result.value->pairs[0].path, "enemy")
    );
    REQUIRE(
        acount(bridge_result.value->graphs) == 3 &&
        !strcmp(bridge_result.value->graphs[0], "Zulu") &&
        !strcmp(bridge_result.value->graphs[1], "Alpha") &&
        !strcmp(bridge_result.value->graphs[2], "Local")
    );
    pin_value v;
    REQUIRE(!prototype_read_field(&text_reads, bridge_result.value, "Sample", "position", &v));
    REQUIRE(((bridge_vector *)v.ptr)->x == 3 && ((bridge_vector *)v.ptr)->y == 4);
    REQUIRE(bridge_edit(source, &registry, NULL, 0));
    REQUIRE(
        !strcmp(bridge_result.source, source) &&
        !prototype_read_field(&text_reads, bridge_result.value, "Health", "current", &v)
    );
    return true;
}
TEST_CASE(source_edit_behavior_splices_and_inherited_detach)
{
    prototype_registry registry = bridge_registry();
    authoring_prototype_edit detach = { .kind = AUTHORING_PROTOTYPE_DETACH_BEHAVIOR,
                                        .behavior = "First" };
    REQUIRE(bridge_edit(
        "Elite :: prototype { behaviors:[First, /* retain */ Second, First, /* "
        "adjacent */ First, Third, First /* tail */]; }",
        &registry,
        &detach,
        1
    ));
    REQUIRE(!prototype_has_graph(bridge_result.value, "First"));
    REQUIRE(
        !strcmp(bridge_result.source, "Elite :: prototype { behaviors:[ /* retain */ Second,  /* adjacent */  Third,  /* tail */]; }")
    );
    REQUIRE(bridge_result.changed && !bridge_result.materialized);
    REQUIRE(
        acount(bridge_result.value->graphs) == 2 &&
        !strcmp(bridge_result.value->graphs[0], "Second") &&
        !strcmp(bridge_result.value->graphs[1], "Third")
    );
    REQUIRE(
        bridge_edit("Elite :: prototype { behaviors:[First, /* between */ First, First /* end */]; }", &registry, &detach, 1)
    );
    REQUIRE(
        !acount(bridge_result.value->graphs) &&
        !strcmp(bridge_result.source, "Elite :: prototype { behaviors:[ /* between */   /* end */]; }")
    );
    authoring_prototype_edit attach = { .kind = AUTHORING_PROTOTYPE_ATTACH_BEHAVIOR,
                                        .behavior = "Second" };
    REQUIRE(bridge_edit("Elite :: prototype {}", &registry, &attach, 1));
    REQUIRE(prototype_has_graph(bridge_result.value, "Second"));
    base_text = strdup("Enemy :: prototype { behaviors:[First,Second,First,Third,First]; }");
    REQUIRE(
        bridge_edit("Elite :: prototype(Enemy) { /* keep metadata */ behaviors:[First,Local,First]; }", &registry, &detach, 1)
    );
    REQUIRE(
        bridge_result.materialized &&
        !prototype_has_graph(bridge_result.value, "First") &&
        acount(bridge_result.value->graphs) == 3 &&
        !strcmp(bridge_result.value->graphs[0], "Second") &&
        !strcmp(bridge_result.value->graphs[1], "Third") &&
        !strcmp(bridge_result.value->graphs[2], "Local") &&
        strstr(bridge_result.source, "/* keep metadata */")
    );
    return true;
}
TEST_CASE(source_edit_typed_batch_and_null_symbolic_link)
{
    prototype_registry registry = bridge_registry();
    authoring_prototype_edit ops[] = {
        { .kind = AUTHORING_PROTOTYPE_SET_FIELD,
          .component = "Health",
          .field = "current",
          .value = { .type = PIN_I32, .i = 7 } },
        { .kind = AUTHORING_PROTOTYPE_SET_FIELD,
          .component = "Health",
          .field = "maximum",
          .value = { .type = PIN_I32, .i = INT32_MAX } },
        { .kind = AUTHORING_PROTOTYPE_SET_FIELD,
          .component = "Sample",
          .field = "amount",
          .value = { .type = PIN_I64, .i64 = INT64_MAX } },
        { .kind = AUTHORING_PROTOTYPE_SET_FIELD,
          .component = "Sample",
          .field = "mode",
          .value = { .type = PIN_ENUM, .type_name = "BridgeMode", .e = 1 } },
    };
    REQUIRE(
        bridge_edit("Elite :: prototype { Health {current:1 /*keep*/} Sample {} }", &registry, ops, 4)
    );
    pin_value value;
    REQUIRE(strstr(bridge_result.source, "/*keep*/"));
    REQUIRE(
        !prototype_read_field(&text_reads, bridge_result.value, "Health", "current", &value) &&
        value.i == 7
    );
    REQUIRE(
        !prototype_read_field(&text_reads, bridge_result.value, "Health", "maximum", &value) &&
        value.i == INT32_MAX
    );
    REQUIRE(
        !prototype_read_field(&text_reads, bridge_result.value, "Sample", "amount", &value) &&
        value.i64 == INT64_MAX
    );
    REQUIRE(
        !prototype_read_field(&text_reads, bridge_result.value, "Sample", "mode", &value) &&
        value.e == 1
    );
    child_text = strdup("Child :: prototype { Health {current:5} }");
    ops[0] = (authoring_prototype_edit){ .kind = AUTHORING_PROTOTYPE_SET_FIELD,
                                         .component = "Sample",
                                         .field = "target",
                                         .value = { .type = PIN_HANDLE, .u64 = 0 } };
    REQUIRE(
        bridge_edit("Elite :: prototype { Sample {target:child.enemy} children:[{name:\"enemy\",prototype:Child}]; }", &registry, ops, 1)
    );
    REQUIRE(bridge_result.changed && !acount(bridge_result.value->links));
    REQUIRE(strstr(bridge_result.source, "target:null"));
    return true;
}
TEST_CASE(source_edit_batch_rejections_have_no_partial_result)
{
    prototype_registry registry = bridge_registry();
    const char *source = "Elite :: prototype { Health {current:1} Sample "
                         "{position:{x:2,y:3}} }";
    authoring_prototype_edit ops[] = {
        { .kind = AUTHORING_PROTOTYPE_SET_FIELD,
          .component = "Health",
          .field = "current",
          .value = { .type = PIN_I32, .i = 9 } },
        { .kind = AUTHORING_PROTOTYPE_SET_FIELD,
          .component = "Sample",
          .field = "target",
          .value = { .type = PIN_HANDLE, .u64 = 42 } },
    };
    REQUIRE(!bridge_edit(source, &registry, ops, 2));
    REQUIRE(!bridge_result.source && !bridge_result.value && !bridge_result.changed);
    REQUIRE(!strcmp(source, "Elite :: prototype { Health {current:1} Sample {position:{x:2,y:3}} }"));
    ops[1] = ops[0];
    REQUIRE(!bridge_edit(source, &registry, ops, 2));
    REQUIRE(!bridge_result.source && !bridge_result.value);
    ops[0].component = "Sample";
    ops[0].field = "position.x";
    ops[0].value = (pin_value){ .type = PIN_F32, .f = NAN };
    REQUIRE(!bridge_edit(source, &registry, ops, 1));
    bridge_vector value = { 1, 2 };
    ops[0].kind = AUTHORING_PROTOTYPE_ADD_COMPONENT;
    ops[0].field = NULL;
    ops[0].value = (pin_value){ .type = PIN_STRUCT,
                                .type_name = "AssetHealth",
                                .ptr = &value };
    REQUIRE(!bridge_edit("Elite :: prototype { Health {current:1} }", &registry, ops, 1));
    ops[0].kind = AUTHORING_PROTOTYPE_SET_FIELD;
    ops[0].field = "prefab";
    ops[0].value = (pin_value){ .type = PIN_NAME, .str = "Missing" };
    REQUIRE(!bridge_edit(source, &registry, ops, 1));
    REQUIRE(!bridge_edit("Elite :: prototype { Health {current: } }", &registry, NULL, 0));
    REQUIRE(!bridge_result.source && !bridge_result.value);
    REQUIRE(
        !authoring_prototype_source_edit(source, &registry, NULL, ops, 1, &bridge_result, error, sizeof error)
    );
    REQUIRE(!authoring_prototype_source_edit(
        source,
        &registry,
        &bridge_catalog,
        ops,
        AUTHORING_PROTOTYPE_EDIT_MAX_OPERATIONS + 1,
        &bridge_result,
        error,
        sizeof error
    ));
    REQUIRE(
        !authoring_prototype_source_edit(source, &registry, &bridge_catalog, ops, -1, &bridge_result, error, sizeof error)
    );
    REQUIRE(!bridge_result.source && !bridge_result.value);
    return true;
}
void test_language_source_prototype_edits()
{
    pu_setup(NULL, bridge_cleanup);
    RUN_TEST_CASE(source_edit_replaces_only_the_normalized_value_span);
    RUN_TEST_CASE(source_edit_sparse_component_keeps_other_base_fields_live);
    RUN_TEST_CASE(source_edit_nested_axis_obeys_existing_whole_record_semantics);
    RUN_TEST_CASE(source_edit_add_uses_explicit_creation_payload_and_local_remove);
    RUN_TEST_CASE(source_edit_inherited_remove_preserves_children_links_and_behavior_order);
    RUN_TEST_CASE(source_edit_behavior_splices_and_inherited_detach);
    RUN_TEST_CASE(source_edit_typed_batch_and_null_symbolic_link);
    RUN_TEST_CASE(source_edit_batch_rejections_have_no_partial_result);
    pu_clear_setup();
}
