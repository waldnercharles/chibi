#include "authoring/source_collection.h"
#include "backend.h"
#include "capabilities/catalog_snapshot.h"
#include "execution/prepared_internal.h"
#include "runtime/behaviors/source.h"
#include "support/array.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/catalog.h"
#include "types/record.h"
#include "types/text.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const pin_string stale_text = { .bytes = "stale",
                                       .length = sizeof("stale") - 1 };
static const pin_string changed_text = { .bytes = "changed",
                                         .length = sizeof("changed") - 1 };

/* Seven fixed cases. VM/SLJIT preparation and publication, no authored execution.
 * The repro selector runs only the VM half of the real publication case. */
#define FIELDS                                                                 \
    "label: string = \"saved\"; count: int = 7; enabled: bool = true;"
static const char
    record_source[] = "CatalogRows :: library { Flags :: struct { " FIELDS
                      " } }";
static const char
    library_source[] = "CatalogRows :: library { Flags :: struct { " FIELDS
                       " } Choice :: union { Off | On }; Make :: () -> Flags { "
                       "return {}; } }";
static const char
    consumer_source[] = "CatalogConsumer :: behavior { "
                        "on Pulse :: () { flags := CatalogRows.Make(); "
                        "flags.count = 8; } }";
static const script_catalog_capability pulse = { .id = 10,
                                                 .name = "Pulse",
                                                 .kind = SCRIPT_CAPABILITY_EVENT,
                                                 .completion_outcome = -1 };
static const script_catalog native = { .capabilities = &pulse, .capability_count = 1 };
static const script_catalog empty;
static const script_lower_bindings bindings;
static char root[512];
static bool reproduce;
static bool project_types;
static source_catalog *snapshot;
static authoring_sources *sources;
static ecs_world *world;
static source_registry *registry;
static source_staging *stage;
static script_syntax *syntax;
static script_analysis *analysis;
static execution_module_plan *plan, *decoded;
static arena catalog_storage;
static script_catalog copied;
static prepared_context writer, reader;

static void clear_codec()
{
    free(reader.records);
    free(writer.records);
    afree(writer.wire.data);
    reader = writer = (prepared_context){ 0 };
    execution_module_plan_free(decoded);
    decoded = NULL;
}
static void clear_analysis()
{
    script_analysis_free(analysis);
    script_syntax_free(syntax);
    analysis = NULL;
    syntax = NULL;
    arena_free(&catalog_storage);
    copied = (script_catalog){ 0 };
}
static void clear()
{
    if (stage) {
        source_discard(stage);
        stage = NULL;
    }
    if (registry) {
        source_registry_free(registry);
        registry = NULL;
    }
    ecs_world_free(world);
    world = NULL;
    authoring_sources_free(sources);
    sources = NULL;
    source_catalog_free(snapshot);
    snapshot = NULL;
    clear_codec();
    execution_module_plan_free(plan);
    plan = NULL;
    clear_analysis();
}
static void cleanup()
{
    clear();
    if (project_types) {
        type_catalog_set_project(NULL);
        project_types = false;
    }
    if (root[0]) {
        assert(!rmdir(root));
        root[0] = 0;
    }
}
static bool parse_record()
{
    syntax = script_parse(record_source);
    REQUIRE(!syntax->diagnostic_count);
    analysis = script_analyze_document(syntax, &empty, "libraries/CatalogRows.chibi");
    REQUIRE(analysis->valid && analysis->type_count == 1);
    const script_catalog input = { .types = analysis->types, .type_count = 1 };
    REQUIRE(script_catalog_snapshot(&catalog_storage, &input, &copied));
    return true;
}
static const authoring_source *find_source(const char *name)
{
    for (int i = 0; i < authoring_sources_count(sources); i++) {
        const authoring_source *source = authoring_sources_at(sources, i);
        if (!strcmp(source->analysis->name, name)) {
            return source;
        }
    }
    return NULL;
}
static bool valid_sources()
{
    for (int i = 0; i < authoring_sources_catalog_diagnostic_count(sources); i++) {
        fprintf(
            stderr,
            "record catalog: %s\n",
            authoring_sources_catalog_diagnostic_at(sources, i)->message
        );
    }
    for (int i = 0; i < authoring_sources_count(sources); i++) {
        const authoring_source *source = authoring_sources_at(sources, i);
        for (int j = 0; j < source->diagnostic_count; j++) {
            fprintf(stderr, "record source: %s\n", source->diagnostics[j].message);
        }
    }
    return authoring_sources_valid(sources);
}
TEST_CASE(record_project_catalog_publishes_declaring_library_and_importer)
{
    const char *temporary = getenv("TMPDIR");
    int size = snprintf(root, sizeof root, "%s/chibi-record-publication-XXXXXX", temporary ? temporary : "/tmp");
    REQUIRE(size > 0 && (size_t)size < sizeof root);
    REQUIRE(mkdtemp(root));
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int backend = 0; backend < (reproduce ? 1 : 2); backend++) {
        char paths[2][640];
        snprintf(paths[0], sizeof paths[0], "%s/libraries/CatalogRows.chibi", root);
        snprintf(paths[1], sizeof paths[1], "%s/behaviors/CatalogConsumer.chibi", root);
        const source_catalog_edit inputs[] = {
            { .path = paths[0], .text = library_source },
            { .path = paths[1], .text = consumer_source }
        };
        snapshot = source_catalog_replaced(root, &native, inputs, 2);
        REQUIRE(source_catalog_source_count(snapshot) == 2);
        REQUIRE(!source_catalog_diagnostic_count(snapshot));
        sources = authoring_sources_prepare_catalog(snapshot, &bindings, backends[backend]);
        snapshot = NULL;
        REQUIRE(valid_sources() && authoring_sources_count(sources) == 2);
        const authoring_source *library = find_source("CatalogRows"),
                               *consumer = find_source("CatalogConsumer");
        REQUIRE(library && consumer);
        REQUIRE(execution_module_backend(library->module) == backends[backend]);
        REQUIRE(execution_module_backend(consumer->module) == backends[backend]);
        REQUIRE(execution_module_import_count(consumer->module) == 1);
        REQUIRE(execution_module_import_at(consumer->module, 0) == library->module);
        const source_publication entries[] = {
            { .name = "CatalogRows", .module = library->module },
            { .name = "CatalogConsumer", .module = consumer->module }
        };
        world = ecs_world_new();
        registry = source_registry_new(world);
        char error[256] = { 0 };
        stage = source_stage(registry, entries, 2, NULL, error, sizeof error);
        if (!stage) {
            fprintf(stderr, "record publication stage: %s\n", error);
        }
        /* On the fixture-only baseline this must fail specifically here with
         * duplicate prepared type requirement, before any commit. */
        REQUIRE(stage && !error[0]);
        const execution_module *modules[] = { library->module, consumer->module };
        for (int i = 0; i < 2; i++) {
            REQUIRE(modules[i]->plan.type_count == 1);
            const execution_type_requirement *type = &modules[i]->plan.types[0];
            REQUIRE(type->record && !strcmp(type->name, "CatalogRows.Flags"));
            REQUIRE(type->record->type.field_count == 3);
        }
        source_commit(stage);
        stage = NULL;
        authoring_sources_free(sources);
        sources = NULL;
        REQUIRE(source_registered(registry, "CatalogRows"));
        REQUIRE(source_registered(registry, "CatalogConsumer"));
        clear();
    }
    return true;
}
static void write_types()
{
    writer = (prepared_context){ .plan = plan };
    writer.wire.storage = &plan->storage;
    prepared_types(&writer);
}
static void read_types()
{
    decoded = execution_module_plan_new();
    reader = (prepared_context){ .plan = decoded };
    reader.wire = (execution_wire){ .reading = true,
                                    .data = writer.wire.data,
                                    .size = writer.wire.size,
                                    .storage = &decoded->storage };
    prepared_types(&reader);
}
TEST_CASE(record_equal_catalog_snapshots_merge_and_own_their_shape)
{
    REQUIRE(parse_record());
    REQUIRE(copied.types[0].name != analysis->types[0].name);
    REQUIRE(copied.types[0].fields != analysis->types[0].fields);
    REQUIRE(
        pin_text(copied.types[0].fields[0].default_value).bytes !=
        pin_text(analysis->types[0].fields[0].default_value).bytes
    );
    plan = script_lower(analysis, &copied, &bindings);
    REQUIRE(plan->valid && plan->type_count == 1);
    uint64_t id = plan->types[0].id, layout = plan->types[0].layout;
    size_t size = plan->types[0].size;
    clear_analysis();
    write_types();
    REQUIRE(!writer.wire.error);
    read_types();
    REQUIRE(!reader.wire.error && reader.wire.offset == reader.wire.size);
    REQUIRE(decoded->type_count == 1);
    const execution_type_requirement *type = &decoded->types[0];
    REQUIRE(type->id == id && type->layout == layout && type->size == size);
    REQUIRE(type->record && pin_record_valid(type->record));
    REQUIRE(!strcmp(type->name, "CatalogRows.Flags"));
    static const char *const names[] = { "label", "count", "enabled" };
    static const pin_type kinds[] = { PIN_STR, PIN_I32, PIN_BOOL };
    REQUIRE(type->record->type.field_count == 3);
    for (int i = 0; i < 3; i++) {
        REQUIRE(!strcmp(type->record->type.fields[i].name, names[i]));
        REQUIRE(type->record->type.fields[i].type == kinds[i]);
    }
    return true;
}
TEST_CASE(record_catalog_conflicts_reject_before_function_creation)
{
    REQUIRE(parse_record());
    for (int change = 0; change < 31; change++) {
        script_catalog_type types[2] = { copied.types[0], copied.types[0] };
        script_catalog_field fields[3];
        memcpy(fields, copied.types[0].fields, sizeof fields);
        types[0].fields = fields;
        script_catalog input = { .types = types, .type_count = 1 };
        script_catalog_type own[2] = { analysis->types[0], analysis->types[0] };
        script_analysis checked = *analysis;
        switch (change) {
            case 0: types[0].id ^= UINT64_C(1); break;
            case 1: types[0].name = "Elsewhere.Flags"; break;
            case 2: types[0].authored_record = false; break;
            case 3: types[0].type.storage = PIN_HANDLE; break;
            case 4: types[0].type.container = PIN_CONTAINER_ARRAY; break;
            case 5: types[0].type.nominal ^= UINT64_C(1); break;
            case 6: types[0].type.flags |= SCRIPT_TYPE_REFERENCE; break;
            case 7: types[0].type.parameter = 1; break;
            case 8: types[0].layout ^= UINT64_C(1); break;
            case 9: types[0].size++; break;
            case 10:
                fields[0] = copied.types[0].fields[1];
                fields[1] = copied.types[0].fields[0];
                break;
            case 11: fields[0].id ^= UINT64_C(1); break;
            case 12: fields[0].name = "renamed"; break;
            case 13: fields[0].type.storage = PIN_I32; break;
            case 14: fields[0].offset++; break;
            case 15: fields[0].has_default = false; break;
            case 16: fields[0].default_value.text = &stale_text; break;
            case 17: fields[1].default_value.i = 8; break;
            case 18: fields[2].default_value.b = false; break;
            case 19: types[0].layout = 0; break;
            case 20: types[0].size = 0; break;
            case 21: types[0].fields = NULL; break;
            case 22: types[0].field_count = 0; break;
            case 23: input.type_count = 2; break;
            case 24: types[0].underlying = 1; break;
            case 25: fields[0].type.flags |= SCRIPT_TYPE_REFERENCE; break;
            case 26: fields[0].type.nominal = 1; break;
            case 27:
                fields[0].default_value.container = PIN_CONTAINER_ARRAY;
                break;
            case 28:
                checked.types = own;
                checked.type_count = 2;
                break;
            case 29: types[0].type.collection = 1; break;
            case 30: fields[0].type.collection = 1; break;
        }
        plan = script_lower(&checked, &input, &bindings);
        REQUIRE(!plan->valid && plan->diagnostic_count == 1);
        REQUIRE(strstr(plan->diagnostics[0].message, "Conflicting authored record declaration"));
        REQUIRE(!plan->type_count && !plan->function_count);
        execution_module_plan_free(plan);
        plan = NULL;
    }
    return true;
}
TEST_CASE(record_scalar_defaults_merge_and_reject_changed_values)
{
    static const char
        source[] = "Defaults :: library { Row :: struct { "
                   "flag: bool = true; i8: i8 = -1; u8: u8 = 2; "
                   "i16: i16 = -3; u16: u16 = 4; i32: int = -5; u32: u32 = 6; "
                   "i64: i64 = -7; u64: u64 = 18446744073709551615; "
                   "f32: number = -0.0; f64: f64 = -0.0; "
                   "key: name = \"saved\"; label: string = \"saved\"; } }";
    syntax = script_parse(source);
    REQUIRE(!syntax->diagnostic_count);
    analysis = script_analyze_document(syntax, &empty, "libraries/Defaults.chibi");
    REQUIRE(analysis->valid && analysis->type_count == 1);
    REQUIRE(analysis->types[0].field_count == 13);
    const script_catalog input = { .types = analysis->types, .type_count = 1 };
    REQUIRE(script_catalog_snapshot(&catalog_storage, &input, &copied));
    plan = script_lower(analysis, &copied, &bindings);
    REQUIRE(plan->valid && plan->type_count == 1);
    execution_module_plan_free(plan);
    plan = NULL;
    for (int changed = 0; changed < 13; changed++) {
        script_catalog_field fields[13];
        memcpy(fields, copied.types[0].fields, sizeof fields);
        fields[changed].default_value = (pin_value){
            .type = fields[changed].type.storage
        };
        if (fields[changed].type.storage == PIN_NAME ||
            fields[changed].type.storage == PIN_STR) {
            fields[changed].default_value.text = &changed_text;
        }
        script_catalog_type type = copied.types[0];
        type.fields = fields;
        const script_catalog stale = { .types = &type, .type_count = 1 };
        plan = script_lower(analysis, &stale, &bindings);
        REQUIRE(!plan->valid && plan->diagnostic_count == 1);
        REQUIRE(strstr(plan->diagnostics[0].message, "Conflicting authored record declaration"));
        REQUIRE(!plan->type_count && !plan->function_count);
        execution_module_plan_free(plan);
        plan = NULL;
    }
    return true;
}
TEST_CASE(record_qualified_foreign_identity_stays_distinct)
{
    REQUIRE(parse_record());
    script_catalog_type foreign = copied.types[0];
    foreign.id ^= UINT64_C(1);
    foreign.type.nominal = foreign.id;
    foreign.name = "Foreign.Flags";
    const type_field shape[] = { { .name = "label", .type = PIN_STR },
                                 { .name = "count", .type = PIN_I32 },
                                 { .name = "enabled", .type = PIN_BOOL } };
    const pin_record
        *record = pin_record_create(&catalog_storage, foreign.name, foreign.id, shape, 3);
    REQUIRE(record);
    foreign.layout = record->layout;
    foreign.size = record->type.size;
    const script_catalog input = { .types = &foreign, .type_count = 1 };
    plan = script_lower(analysis, &input, &bindings);
    REQUIRE(plan->valid && plan->type_count == 2);
    REQUIRE(
        plan->types[0].id == foreign.id &&
        plan->types[1].id == analysis->types[0].id
    );
    REQUIRE(!strcmp(plan->types[0].name, "Foreign.Flags"));
    REQUIRE(!strcmp(plan->types[1].name, "CatalogRows.Flags"));
    return true;
}
TEST_CASE(record_native_nested_field_keeps_its_nominal_layout)
{
    static const type_field field = { .name = "value",
                                      .type = PIN_F32,
                                      .size = sizeof(float) };
    static const type_desc nested = { .name = "PublicationNative",
                                      .size = sizeof(float),
                                      .fields = &field,
                                      .field_count = 1 };
    static const type_desc *const types[] = { &nested };
    static const type_catalog_pack pack = { .types = types, .type_count = 1 };
    static const script_catalog_type native_type = {
        .id = 51,
        .name = "PublicationNative",
        .type = { .storage = PIN_STRUCT, .nominal = 51 },
        .size = sizeof(float)
    };
    const script_catalog catalog = { .types = &native_type, .type_count = 1 };
    type_catalog_set_project(&pack);
    project_types = true;
    syntax = script_parse("Nested :: library { Row :: struct { value: PublicationNative; } }");
    REQUIRE(!syntax->diagnostic_count);
    analysis = script_analyze_document(syntax, &catalog, "libraries/Nested.chibi");
    REQUIRE(analysis->valid && analysis->type_count == 1);
    plan = script_lower(analysis, &catalog, &bindings);
    REQUIRE(plan->valid && plan->type_count == 2);
    const pin_record *record = plan->types[1].record;
    REQUIRE(record && pin_record_valid(record));
    REQUIRE(record->layout == analysis->types[0].layout);
    REQUIRE(record->type.size == analysis->types[0].size);
    clear_analysis();
    REQUIRE(record->type.field_count == 1);
    REQUIRE(record->type.fields[0].type == PIN_STRUCT);
    REQUIRE(record->type.fields[0].type_name);
    REQUIRE(!strcmp(record->type.fields[0].type_name, nested.name));
    REQUIRE(record->type.fields[0].type_name != nested.name);
    return true;
}
TEST_CASE(record_prepared_duplicate_identity_and_name_remain_rejected)
{
    const type_field field = { .name = "enabled", .type = PIN_BOOL };
    for (int mode = 0; mode < 3; mode++) {
        plan = execution_module_plan_new();
        const pin_record *record = pin_record_create(&plan->storage, "Wire.Flags", 91, &field, 1);
        REQUIRE(record);
        execution_type_requirement type = { .id = record->id,
                                            .layout = record->layout,
                                            .storage = PIN_STRUCT,
                                            .record = record,
                                            .name = record->type.name,
                                            .size = record->type.size };
        apush(plan->types, type);
        if (mode == 1) {
            type.name = "Wire.Other";
        }
        if (mode == 2) {
            type.id = 92;
        }
        apush(plan->types, type);
        plan->type_count = 2;
        write_types();
        REQUIRE(
            writer.wire.error &&
            !strcmp(writer.wire.error, "duplicate prepared type requirement")
        );
        /* The rejected writer emitted the complete first row and second
         * identity header. The reader must reject before any second payload. */
        read_types();
        REQUIRE(
            reader.wire.error &&
            !strcmp(reader.wire.error, "duplicate prepared type requirement")
        );
        clear_codec();
        execution_module_plan_free(plan);
        plan = NULL;
    }
    return true;
}
void test_language_record_publication(bool repro)
{
    reproduce = repro;
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(record_project_catalog_publishes_declaring_library_and_importer);
    if (!reproduce) {
        RUN_TEST_CASE(record_equal_catalog_snapshots_merge_and_own_their_shape);
        RUN_TEST_CASE(record_catalog_conflicts_reject_before_function_creation);
        RUN_TEST_CASE(record_scalar_defaults_merge_and_reject_changed_values);
        RUN_TEST_CASE(record_qualified_foreign_identity_stays_distinct);
        RUN_TEST_CASE(record_native_nested_field_keeps_its_nominal_layout);
        RUN_TEST_CASE(record_prepared_duplicate_identity_and_name_remain_rejected);
    }
    pu_clear_setup();
}
