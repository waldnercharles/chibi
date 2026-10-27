#include "authoring/native_bindings.h"
#include "authoring/source_collection.h"
#include "backend.h"
#include "capabilities/definition.h"
#include "execution/status.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/array.h"
#include "types/catalog.h"
#include "types/text.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char root[128], paths[4][256];
static source_catalog_edit edits[4];
static script_catalog_capability tick;
static script_catalog catalog;
static script_lower_bindings bindings;
static authoring_sources *generations[4];
static native_capabilities *tables[2];
static execution_program *program;
static bool project_types;

static void setup()
{
    strcpy(root, "/tmp/chibi-incremental-XXXXXX");
    assert(mkdtemp(root));
    static const char *const names[] = { "libraries/Leaf.chibi",
                                         "libraries/Middle.chibi",
                                         "behaviors/Caller.chibi",
                                         "behaviors/Independent.chibi" };
    static const char *const texts[] = {
        "Leaf :: library { Value :: () -> number { return 2; } }",
        "Middle :: library { Value :: () -> number { return Leaf.Value() + 1; "
        "} }",
        "Caller :: behavior { value: retain number = 0; on Tick :: () { value "
        "= "
        "Middle.Value(); } }",
        "Independent :: behavior { value: retain number = 0; on Tick :: () { "
        "value "
        "= 7; } }"
    };
    for (int i = 0; i < 4; i++) {
        snprintf(paths[i], sizeof paths[i], "%s/%s", root, names[i]);
        edits[i] = (source_catalog_edit){ .path = paths[i], .text = texts[i] };
    }
    tick = (script_catalog_capability){ .id = 10,
                                        .name = "Tick",
                                        .kind = SCRIPT_CAPABILITY_EVENT,
                                        .completion_outcome = -1 };
    catalog = (script_catalog){ .capabilities = &tick,
                                .capability_count = 1,
                                .tick_event = 10 };
    bindings = (script_lower_bindings){ .generation = 1 };
}
static void cleanup()
{
    execution_program_free(program);
    program = NULL;
    for (int i = 0; i < 4; i++) {
        authoring_sources_free(generations[i]);
        generations[i] = NULL;
    }
    for (int i = 0; i < 2; i++) {
        native_capabilities_free(tables[i]);
        tables[i] = NULL;
    }
    if (project_types) {
        type_catalog_set_project(NULL);
        project_types = false;
    }
    rmdir(root);
}
static authoring_sources *prepare(int previous, behavior_backend backend)
{
    source_catalog *snapshot = source_catalog_replaced(root, &catalog, edits, 4);
    return authoring_sources_prepare_incremental(
        snapshot,
        &bindings,
        backend,
        previous >= 0 ? generations[previous] : NULL
    );
}
static const authoring_source *source(int generation, const char *name)
{
    for (int i = 0; i < authoring_sources_count(generations[generation]); i++) {
        const authoring_source *s = authoring_sources_at(generations[generation], i);
        if (s->analysis->name && !strcmp(s->analysis->name, name)) {
            return s;
        }
    }
    return NULL;
}
static bool valid(int generation)
{
    if (authoring_sources_valid(generations[generation])) {
        return true;
    }
    for (int i = 0;
         i < authoring_sources_catalog_diagnostic_count(generations[generation]);
         i++) {
        fprintf(
            stderr,
            "%s\n",
            authoring_sources_catalog_diagnostic_at(generations[generation], i)->message
        );
    }
    for (int i = 0; i < authoring_sources_count(generations[generation]); i++) {
        const authoring_source *s = authoring_sources_at(generations[generation], i);
        for (int j = 0; j < s->diagnostic_count; j++) {
            fprintf(stderr, "%s: %s\n", s->path, s->diagnostics[j].message);
        }
    }
    return false;
}
static bool value_is(int generation, const char *name, double expected)
{
    const authoring_source *s = source(generation, name);
    if (!s || !s->valid) {
        return false;
    }
    uint64_t state = 0;
    for (int i = 0; i < s->analysis->symbol_count; i++) {
        const script_symbol *symbol = &s->analysis->symbols[i];
        if (symbol->kind == SCRIPT_STATE && !strcmp(symbol->name, "value")) {
            state = symbol->id;
        }
    }
    program = execution_program_new(s->module, NULL, NULL);
    if (!program) {
        return false;
    }
    execution_instance *instance = execution_program_attach(program, 1);
    bool ok = instance &&
              execution_instance_dispatch(instance, 10, NULL, 0, 0, 0, 1000) == FLOW_OK;
    const pin_value *value = ok ? execution_instance_state(instance, state) : NULL;
    ok = value && value->type == PIN_F32 && value->f == (float)expected;
    execution_program_free(program);
    program = NULL;
    return ok;
}
static void discard(int generation)
{
    authoring_sources_free(generations[generation]);
    generations[generation] = NULL;
}

TEST_CASE(unchanged_sources_reuse_analysis_and_executables_after_old_collection_dies)
{
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        generations[0] = prepare(-1, backends[b]);
        REQUIRE(valid(0));
        generations[1] = prepare(0, backends[b]);
        REQUIRE(valid(1));
        REQUIRE(authoring_sources_reused(generations[1]) == 4);
        REQUIRE(source(0, "Caller") == source(1, "Caller"));
        REQUIRE(source(0, "Leaf")->module == source(1, "Leaf")->module);
        discard(0);
        REQUIRE(source(1, "Caller")->references == 1);
        REQUIRE(value_is(1, "Caller", 3));
        discard(1);
    }
    return true;
}

TEST_CASE(display_edits_reuse_sources_with_independently_owned_catalogs)
{
    static const char *const text[] = { "Lifecycle", "tick update", "Run one update." };
    char buffers[3][32];
    const char **fields[] = { &tick.category, &tick.keywords, &tick.description };
    generations[0] = prepare(-1, BEHAVIOR_BYTECODE);
    REQUIRE(valid(0));
    for (int change = 0; change < 3; change++) {
        strcpy(buffers[change], text[change]);
        *fields[change] = buffers[change];
        generations[1] = prepare(0, BEHAVIOR_BYTECODE);
        REQUIRE(valid(1) && authoring_sources_reused(generations[1]) == 4);
        REQUIRE(source(0, "Caller") == source(1, "Caller"));
        const script_catalog_capability *old = script_catalog_capability_by_id(
            authoring_sources_catalog(generations[0]),
            tick.id
        );
        const script_catalog_capability *fresh = script_catalog_capability_by_id(
            authoring_sources_catalog(generations[1]),
            tick.id
        );
        REQUIRE(old && fresh);
        const char *old_text[] = { old->category, old->keywords, old->description };
        const char *fresh_text[] = { fresh->category, fresh->keywords, fresh->description };
        REQUIRE(!old_text[change]);
        buffers[change][0] = '!';
        for (int field = 0; field <= change; field++) {
            REQUIRE(!strcmp(fresh_text[field], text[field]));
            if (field < change) {
                REQUIRE(!strcmp(old_text[field], text[field]));
            }
        }
        REQUIRE(value_is(0, "Caller", 3) && value_is(1, "Caller", 3));
        discard(0);
        REQUIRE(!strcmp(fresh_text[change], text[change]));
        REQUIRE(value_is(1, "Caller", 3));
        buffers[change][0] = text[change][0];
        generations[0] = generations[1];
        generations[1] = NULL;
    }
    return true;
}

TEST_CASE(body_edits_rebuild_transitive_callers_and_preserve_unrelated_sources)
{
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    const char *original = edits[0].text;
    for (int b = 0; b < 2; b++) {
        edits[0].text = original;
        generations[0] = prepare(-1, backends[b]);
        REQUIRE(valid(0));
        edits[0]
            .text = "Leaf :: library { Value :: () -> number { return 9; } }";
        generations[1] = prepare(0, backends[b]);
        REQUIRE(valid(1));
        REQUIRE(authoring_sources_reused(generations[1]) == 1);
        REQUIRE(source(0, "Independent") == source(1, "Independent"));
        REQUIRE(source(0, "Leaf")->module != source(1, "Leaf")->module);
        REQUIRE(source(0, "Middle")->module != source(1, "Middle")->module);
        REQUIRE(source(0, "Caller")->module != source(1, "Caller")->module);
        REQUIRE(value_is(0, "Caller", 3));
        REQUIRE(value_is(1, "Caller", 10));
        discard(0);
        REQUIRE(value_is(1, "Caller", 10));
        /* Current generations keep no chain of old collections. */
        for (int edit = 0; edit < 12; edit++) {
            char text[160];
            snprintf(text, sizeof text, "Independent :: behavior { value: retain number = 0; on Tick :: () { value = %d; } }", edit);
            edits[3].text = text;
            generations[2] = prepare(1, backends[b]);
            REQUIRE(valid(2) && authoring_sources_reused(generations[2]) == 3);
            discard(1);
            generations[1] = generations[2];
            generations[2] = NULL;
            REQUIRE(source(1, "Caller")->references == 1);
            REQUIRE(value_is(1, "Independent", edit));
            edits[3].text = "Independent :: behavior { value: retain number = "
                            "0; on Tick :: () { value = 7; } }";
        }
        discard(1);
    }
    return true;
}

TEST_CASE(binding_generations_and_backends_are_required_cache_inputs)
{
    generations[0] = prepare(-1, BEHAVIOR_BYTECODE);
    REQUIRE(valid(0));
    bindings.generation++;
    generations[1] = prepare(0, BEHAVIOR_BYTECODE);
    REQUIRE(valid(1) && !authoring_sources_reused(generations[1]));
    bindings.generation = 0;
    generations[2] = prepare(1, BEHAVIOR_BYTECODE);
    generations[3] = prepare(2, BEHAVIOR_BYTECODE);
    REQUIRE(valid(2) && valid(3));
    REQUIRE(
        !authoring_sources_reused(generations[2]) &&
        !authoring_sources_reused(generations[3])
    );
    discard(1);
    discard(2);
    discard(3);
    bindings.generation = 1;
    generations[1] = prepare(0, TEST_COMPILED_BACKEND);
    REQUIRE(valid(1) && !authoring_sources_reused(generations[1]));
    REQUIRE(execution_module_backend(source(1, "Caller")->module) == TEST_COMPILED_BACKEND);
    REQUIRE(value_is(1, "Caller", 3));
    return true;
}

TEST_CASE(catalog_defaults_effects_units_layouts_and_relationships_invalidate_reuse)
{
    script_catalog_parameter parameter = { .name = "unused",
                                           .type = { .storage = PIN_F64 },
                                           .has_default = true,
                                           .default_value = { .type = PIN_F64,
                                                              .d = 0.0 } };
    script_catalog_parameter result = { .name = "Result",
                                        .type = { .storage = PIN_F64 } };
    script_catalog_capability capabilities[2] = { tick,
                                                  { .id = 50,
                                                    .name = "Other",
                                                    .kind = SCRIPT_CAPABILITY_FUNCTION,
                                                    .parameters = &parameter,
                                                    .parameter_count = 1,
                                                    .results = &result,
                                                    .result_count = 1 } };
    script_catalog_global global = { .id = 51,
                                     .name = "distance",
                                     .type = { .storage = PIN_F64 },
                                     .unit = "u" };
    script_catalog_field field = { .id = 52,
                                   .name = "amount",
                                   .type = { .storage = PIN_F64 },
                                   .writable = true };
    script_catalog_type type = { .id = 53,
                                 .name = "Amount",
                                 .type = { .storage = PIN_STRUCT, .nominal = 53 },
                                 .fields = &field,
                                 .field_count = 1,
                                 .size = sizeof(double),
                                 .layout = 1 };
    script_catalog_relationship relationship = { .id = 54,
                                                 .name = "Targets",
                                                 .exclusive = true };
    catalog.capabilities = capabilities;
    catalog.capability_count = 2;
    catalog.globals = &global;
    catalog.global_count = 1;
    catalog.types = &type;
    catalog.type_count = 1;
    catalog.relationships = &relationship;
    catalog.relationship_count = 1;
    generations[0] = prepare(-1, BEHAVIOR_BYTECODE);
    REQUIRE(valid(0));
    generations[1] = prepare(0, BEHAVIOR_BYTECODE);
    REQUIRE(valid(1) && authoring_sources_reused(generations[1]) == 4);
    discard(1);
    for (int change = 0; change < 11; change++) {
        switch (change) {
            case 0: parameter.default_value.d = -0.0; break;
            case 1: capabilities[1].effects = SCRIPT_EFFECT_WRITE; break;
            case 2: global.unit = "s"; break;
            case 3:
                field.range_contract = true;
                field.min = 0;
                field.max = 10;
                break;
            case 4: type.layout++; break;
            case 5: relationship.exclusive = false; break;
            case 6: field.type.flags = SCRIPT_TYPE_REFERENCE; break;
            case 7: parameter.unit = "charge"; break;
            case 8: capabilities[1].name = "Renamed"; break;
            case 9: parameter.name = "renamed_input"; break;
            case 10: result.name = "RenamedResult"; break;
        }
        generations[1] = prepare(0, BEHAVIOR_BYTECODE);
        REQUIRE(valid(1));
        REQUIRE(!authoring_sources_reused(generations[1]));
        discard(0);
        generations[0] = generations[1];
        generations[1] = NULL;
    }
    return true;
}

TEST_CASE(signature_units_cross_libraries_and_invalidate_same_layout_callers)
{
    /* The caller sorts before its callee: declarations carry units before bodies. */
    snprintf(paths[0], sizeof paths[0], "%s/libraries/ZLeaf.chibi", root);
    edits[0].text = "Leaf :: library { Value :: (distance: number u = 2) -> "
                    "number u { return distance; } }";
    edits[1].text = "Middle :: library { Value :: () -> u { return "
                    "Leaf.Value(); } }";
    generations[0] = prepare(-1, BEHAVIOR_BYTECODE);
    REQUIRE(valid(0));
    generations[1] = prepare(0, BEHAVIOR_BYTECODE);
    REQUIRE(valid(1) && authoring_sources_reused(generations[1]) == 4);
    discard(1);
    edits[0].text = "Leaf :: library { Value :: (distance: number s = 2) -> "
                    "number s { return distance; } }";
    generations[1] = prepare(0, BEHAVIOR_BYTECODE);
    REQUIRE(!authoring_sources_valid(generations[1]));
    REQUIRE(!authoring_sources_reused(generations[1]));
    const authoring_source *caller = source(1, "Middle");
    REQUIRE(caller && caller->diagnostic_count == 1);
    REQUIRE(!strcmp(caller->diagnostics[0].message, "Units differ: u = s"));
    REQUIRE(value_is(0, "Caller", 2));
    discard(1);
    edits[1].text = "Middle :: library { Value :: (distance: u = 2) -> s { "
                    "return Leaf.Value(distance); } }";
    generations[1] = prepare(0, BEHAVIOR_BYTECODE);
    caller = source(1, "Middle");
    REQUIRE(caller && !caller->valid && caller->diagnostic_count == 1);
    REQUIRE(!strcmp(caller->diagnostics[0].message, "Units differ: s argument u"));
    return true;
}

TEST_CASE(native_signature_unit_snapshots_own_metadata)
{
    char parameter_unit[] = "u", result_unit[] = "s";
    script_catalog_parameter parameter = { .name = "distance",
                                           .unit = parameter_unit,
                                           .type = { .storage = PIN_F64 } };
    script_catalog_parameter result = { .name = "Elapsed",
                                        .unit = result_unit,
                                        .type = { .storage = PIN_F64 } };
    const script_catalog_capability capabilities[] = { tick,
                                                       { .id = 81,
                                                         .name = "Measured",
                                                         .parameters = &parameter,
                                                         .parameter_count = 1,
                                                         .results = &result,
                                                         .result_count = 1 } };
    catalog.capabilities = capabilities;
    catalog.capability_count = 2;
    native_capability_pack pack = { .catalog = &catalog };
    char error[192];
    tables[0] = native_capabilities_new(&pack, 1, error, sizeof error);
    REQUIRE(tables[0]);
    const script_catalog_capability *owned = script_catalog_capability_by_id(
        native_capabilities_catalog(tables[0]),
        81
    );
    REQUIRE(
        owned && !strcmp(owned->parameters[0].unit, "u") &&
        !strcmp(owned->results[0].unit, "s")
    );
    generations[0] = prepare(-1, BEHAVIOR_BYTECODE);
    REQUIRE(valid(0));
    parameter_unit[0] = 's';
    REQUIRE(!strcmp(owned->parameters[0].unit, "u") && !strcmp(owned->results[0].unit, "s"));
    generations[1] = prepare(0, BEHAVIOR_BYTECODE);
    REQUIRE(valid(1) && !authoring_sources_reused(generations[1]));
    discard(0);
    generations[0] = generations[1];
    generations[1] = NULL;
    result_unit[0] = 'u';
    REQUIRE(!strcmp(owned->results[0].unit, "s"));
    generations[1] = prepare(0, BEHAVIOR_BYTECODE);
    REQUIRE(valid(1) && !authoring_sources_reused(generations[1]));
    return true;
}

TEST_CASE(native_definitions_preserve_signature_units)
{
    char parameter_unit[] = "u", result_unit[] = "s";
    native_parameter_definition input = { .name = "Distance",
                                          .storage = PIN_F64,
                                          .unit = parameter_unit };
    native_parameter_definition output = { .name = "Elapsed",
                                           .storage = PIN_F64,
                                           .unit = result_unit };
    const native_capability_definition definition = {
        .canonical_name = "Measured",
        .source_name = "Measured",
        .inputs = &input,
        .input_count = 1,
        .outputs = &output,
        .output_count = 1,
        .completion_output = -1
    };
    const script_catalog base = { 0 };
    char error[192];
    native_definition_catalog *
        converted = native_definition_catalog_new(&base, &definition, 1, NULL, 0, error, sizeof error);
    REQUIRE(converted);
    native_capability_pack pack = { .catalog = native_definition_catalog_view(converted) };
    tables[0] = native_capabilities_new(&pack, 1, error, sizeof error);
    native_definition_catalog_free(converted);
    REQUIRE(tables[0]);
    parameter_unit[0] = 's';
    result_unit[0] = 'u';
    const script_catalog_capability *owned = script_catalog_capability_by_id(
        native_capabilities_catalog(tables[0]),
        native_capability_definition_id(&definition)
    );
    REQUIRE(
        owned && !strcmp(owned->parameters[0].unit, "u") &&
        !strcmp(owned->results[0].unit, "s")
    );
    input.storage = PIN_STR;
    converted = native_definition_catalog_new(&base, &definition, 1, NULL, 0, error, sizeof error);
    REQUIRE(!converted);
    REQUIRE(strstr(error, "A measured port requires a numeric type"));
    input.storage = PIN_F64;
    input.unit = "";
    converted = native_definition_catalog_new(&base, &definition, 1, NULL, 0, error, sizeof error);
    REQUIRE(!converted);
    REQUIRE(strstr(error, "nonempty unit"));
    return true;
}

TEST_CASE(invalid_edits_and_document_moves_preserve_previous_generations)
{
    generations[0] = prepare(-1, BEHAVIOR_BYTECODE);
    REQUIRE(valid(0));
    edits[0]
        .text = "Leaf :: library { Value :: () -> number { return missing; } }";
    generations[1] = prepare(0, BEHAVIOR_BYTECODE);
    REQUIRE(!authoring_sources_valid(generations[1]));
    REQUIRE(source(1, "Leaf")->diagnostic_count > 0);
    REQUIRE(source(1, "Caller")->diagnostic_count > 0);
    REQUIRE(authoring_sources_reused(generations[1]) == 1);
    REQUIRE(value_is(0, "Caller", 3));
    edits[0].text = "Leaf :: library { Value :: () -> number { return 4; } }";
    generations[2] = prepare(1, BEHAVIOR_BYTECODE);
    REQUIRE(valid(2) && authoring_sources_reused(generations[2]) == 1);
    REQUIRE(value_is(2, "Caller", 5));
    /* A move retains the stable key explicitly, but source locations change. */
    edits[3].identity = "behaviors/Independent.chibi";
    snprintf(paths[3], sizeof paths[3], "%s/behaviors/Moved.chibi", root);
    generations[3] = prepare(2, BEHAVIOR_BYTECODE);
    REQUIRE(valid(3) && authoring_sources_reused(generations[3]) == 3);
    REQUIRE(source(2, "Independent") != source(3, "Independent"));
    return true;
}

TEST_CASE(native_binding_tables_supply_nonrepeating_generation_keys)
{
    native_capability_pack pack = { .catalog = &catalog };
    char error[192];
    for (int i = 0; i < 2; i++) {
        tables[i] = native_capabilities_new(&pack, 1, error, sizeof error);
        REQUIRE(tables[i]);
    }
    REQUIRE(native_capabilities_generation(tables[0]));
    REQUIRE(
        native_capabilities_generation(tables[0]) !=
        native_capabilities_generation(tables[1])
    );
    bindings = authoring_native_bindings(tables[0]);
    generations[0] = prepare(-1, BEHAVIOR_BYTECODE);
    generations[1] = prepare(0, BEHAVIOR_BYTECODE);
    REQUIRE(valid(0) && valid(1) && authoring_sources_reused(generations[1]) == 4);
    bindings = authoring_native_bindings(tables[1]);
    generations[2] = prepare(1, BEHAVIOR_BYTECODE);
    REQUIRE(valid(2) && !authoring_sources_reused(generations[2]));
    return true;
}

TEST_CASE(struct_defaults_use_frozen_values_and_actual_snapshot_layout)
{
    static const type_field reflected_field = { .name = "amount",
                                                .type = PIN_F64,
                                                .size = sizeof(double),
                                                .type_name = "f64" };
    static const type_desc reflected = { .name = "IncrementalDefault",
                                         .size = sizeof(double),
                                         .fields = &reflected_field,
                                         .field_count = 1 };
    static const type_desc *const types[] = { &reflected };
    const type_catalog_pack pack = { .types = types, .type_count = 1 };
    type_catalog_set_project(&pack);
    project_types = true;
    double payload = 5;
    const script_catalog_field field = { .id = 70,
                                         .name = "amount",
                                         .type = { .storage = PIN_F64 } };
    script_catalog_type type = { .id = 71,
                                 .name = "IncrementalDefault",
                                 .type = { .storage = PIN_STRUCT, .nominal = 71 },
                                 .fields = &field,
                                 .field_count = 1,
                                 .size = sizeof(double) };
    script_catalog_parameter parameter = { .name = "value",
                                           .type = type.type,
                                           .has_default = true,
                                           .default_value = {
                                               .type = PIN_STRUCT,
                                               .ptr = &payload,
                                               .type_name = reflected.name } };
    script_catalog_capability capabilities[] = { tick,
                                                 { .id = 72,
                                                   .name = "Unused",
                                                   .kind = SCRIPT_CAPABILITY_EVENT,
                                                   .parameters = &parameter,
                                                   .parameter_count = 1 } };
    catalog.capabilities = capabilities;
    catalog.capability_count = 2;
    catalog.types = &type;
    catalog.type_count = 1;
    for (int mismatch = 0; mismatch < 2; mismatch++) {
        /* Snapshot allocation uses the reflected eight-byte descriptor, even
         * when an embedding declaration reports sixteen bytes. Reuse must not
         * read the default using that independent catalog size. */
        type.size = (size_t)(mismatch + 1) * sizeof(double);
        generations[0] = prepare(-1, BEHAVIOR_BYTECODE);
        generations[1] = prepare(0, BEHAVIOR_BYTECODE);
        REQUIRE(valid(0) && valid(1));
        REQUIRE(authoring_sources_reused(generations[1]) == 4);
        REQUIRE(value_is(1, "Caller", 3));
        payload++;
        generations[2] = prepare(1, BEHAVIOR_BYTECODE);
        REQUIRE(valid(2) && !authoring_sources_reused(generations[2]));
        /* Both older catalogs keep their original frozen value facts. */
        REQUIRE(value_is(0, "Caller", 3));
        discard(0);
        discard(1);
        discard(2);
    }
    /* Native tables own the same non-null reflected defaults; ordinary engine
     * bindings can reuse them without asking authors to change their values. */
    type.size = reflected.size;
    type.layout = type_layout_hash(&reflected);
    const native_capability_pack native = { .catalog = &catalog,
                                            .layouts = types,
                                            .layout_count = 1 };
    char error[192];
    tables[0] = native_capabilities_new(&native, 1, error, sizeof error);
    REQUIRE(tables[0]);
    catalog = *native_capabilities_catalog(tables[0]);
    bindings = authoring_native_bindings(tables[0]);
    generations[0] = prepare(-1, BEHAVIOR_BYTECODE);
    generations[1] = prepare(0, BEHAVIOR_BYTECODE);
    REQUIRE(valid(0) && valid(1) && authoring_sources_reused(generations[1]) == 4);
    script_catalog_type changed_type = catalog.types[0];
    changed_type.size *= 2;
    catalog.types = &changed_type;
    generations[2] = prepare(1, BEHAVIOR_BYTECODE);
    REQUIRE(valid(2) && !authoring_sources_reused(generations[2]));
    type_catalog_set_project(NULL);
    project_types = false;
    return true;
}

TEST_CASE(default_comparison_storage_is_bounded)
{
    size_t bytes = 1024 * 1024;
    char *large = malloc(bytes + 1);
    REQUIRE(large);
    memset(large, 'x', bytes);
    large[bytes] = 0;
    script_catalog_parameter parameter = {
        .name = "unused",
        .type = { .storage = PIN_STR },
        .has_default = true,
        .default_value = { .type = PIN_STR,
                           .text = &(const pin_string){ .bytes = large,
                                                        .length = (uint32_t)strlen(large) } }
    };
    script_catalog_capability capabilities[] = { tick,
                                                 { .id = 75,
                                                   .name = "LargeDefault",
                                                   .kind = SCRIPT_CAPABILITY_EVENT,
                                                   .parameters = &parameter,
                                                   .parameter_count = 1 } };
    catalog.capabilities = capabilities;
    catalog.capability_count = 2;
    generations[0] = prepare(-1, BEHAVIOR_BYTECODE);
    generations[1] = prepare(0, BEHAVIOR_BYTECODE);
    free(large);
    REQUIRE(valid(0) && valid(1));
    REQUIRE(!authoring_sources_reused(generations[1]));
    return true;
}

TEST_CASE(struct_default_keys_keep_opaque_bytes_and_do_not_read_malformed_fields)
{
    static type_field reflected_field;
    static type_desc reflected;
    static const type_desc *types[] = { &reflected };
    const type_catalog_pack pack = { .types = types, .type_count = 1 };
    double payload[2] = { 3, 5 };
    script_catalog_type type = { .id = 81,
                                 .name = "IncrementalOpaque",
                                 .type = { .storage = PIN_STRUCT, .nominal = 81 },
                                 .size = sizeof payload };
    script_catalog_parameter parameter = {
        .name = "unused",
        .type = type.type,
        .has_default = true,
        .default_value = { .type = PIN_STRUCT, .type_name = "IncrementalOpaque", .ptr = payload }
    };
    script_catalog_capability capabilities[] = { tick,
                                                 { .id = 82,
                                                   .name = "OpaqueDefault",
                                                   .kind = SCRIPT_CAPABILITY_EVENT,
                                                   .parameters = &parameter,
                                                   .parameter_count = 1 } };
    catalog.types = &type;
    catalog.type_count = 1;
    catalog.capabilities = capabilities;
    catalog.capability_count = 2;
    for (int shape = 0; shape < 4; shape++) {
        reflected_field = (type_field){ .name = "amount",
                                        .type = PIN_F64,
                                        .size = sizeof(double),
                                        .type_name = "f64" };
        reflected = (type_desc){ .name = "IncrementalOpaque",
                                 .size = sizeof payload,
                                 .fields = &reflected_field,
                                 .field_count = shape ? 1 : 0 };
        if (shape == 2) {
            /* A typed scalar read here would read eight bytes at offset
             * fifteen. Defaults keep a frozen layout, so a reflected layout
             * that cannot freeze is rejected before any field is read. */
            reflected_field.offset = sizeof payload - 1;
            reflected_field.size = 1;
        }
        if (shape == 3) {
            reflected_field.offset = 1; /* Packed scalar: copy bytes, never cast. */
        }
        type_catalog_set_project(&pack);
        project_types = true;
        generations[0] = prepare(-1, BEHAVIOR_BYTECODE);
        if (shape == 2) {
            REQUIRE(!authoring_sources_valid(generations[0]));
            REQUIRE(authoring_sources_catalog_diagnostic_count(generations[0]) == 1);
            REQUIRE(!strcmp(
                authoring_sources_catalog_diagnostic_at(generations[0], 0)->message,
                "Cannot retain native declaration defaults"
            ));
            discard(0);
            type_catalog_set_project(NULL);
            project_types = false;
            continue;
        }
        generations[1] = prepare(0, BEHAVIOR_BYTECODE);
        REQUIRE(valid(0) && valid(1));
        REQUIRE(authoring_sources_reused(generations[1]) == 4);
        payload[1]++;
        generations[2] = prepare(1, BEHAVIOR_BYTECODE);
        REQUIRE(valid(2) && !authoring_sources_reused(generations[2]));
        for (int i = 0; i < 3; i++) {
            discard(i);
        }
        type_catalog_set_project(NULL);
        project_types = false;
    }
    return true;
}

void test_language_incremental()
{
    pu_setup(setup, cleanup);
    RUN_TEST_CASE(unchanged_sources_reuse_analysis_and_executables_after_old_collection_dies);
    RUN_TEST_CASE(display_edits_reuse_sources_with_independently_owned_catalogs);
    RUN_TEST_CASE(body_edits_rebuild_transitive_callers_and_preserve_unrelated_sources);
    RUN_TEST_CASE(binding_generations_and_backends_are_required_cache_inputs);
    RUN_TEST_CASE(catalog_defaults_effects_units_layouts_and_relationships_invalidate_reuse);
    RUN_TEST_CASE(signature_units_cross_libraries_and_invalidate_same_layout_callers);
    RUN_TEST_CASE(native_signature_unit_snapshots_own_metadata);
    RUN_TEST_CASE(native_definitions_preserve_signature_units);
    RUN_TEST_CASE(invalid_edits_and_document_moves_preserve_previous_generations);
    RUN_TEST_CASE(native_binding_tables_supply_nonrepeating_generation_keys);
    RUN_TEST_CASE(struct_defaults_use_frozen_values_and_actual_snapshot_layout);
    RUN_TEST_CASE(default_comparison_storage_is_bounded);
    RUN_TEST_CASE(struct_default_keys_keep_opaque_bytes_and_do_not_read_malformed_fields);
}
