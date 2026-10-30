#include "authoring/source_catalog_snapshot.h"
#include "authoring/source_literal.h"
#include "language/semantic.h"
#include "support/array.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/array.h"
#include "types/text.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Seven finite semantic-only cases. No lowering, finalized function, runtime,
 * filesystem, worker or configurable workload is exercised here. */
static script_syntax *syntax;
static script_analysis *analysis;
static arena first, second;
static unsigned char *first_key, *second_key;
static const script_catalog empty;
static void clear_analysis()
{
    script_analysis_free(analysis);
    script_syntax_free(syntax);
    analysis = NULL;
    syntax = NULL;
}
static void cleanup()
{
    clear_analysis();
    arena_free(&first);
    arena_free(&second);
    afree(first_key);
    afree(second_key);
}
static void parse(const char *source, const script_catalog *catalog)
{
    clear_analysis();
    syntax = script_parse(source);
    analysis = script_analyze_document(syntax, catalog, "record-semantic.chibi");
    for (int i = 0; i < analysis->diagnostic_count; i++) {
        fprintf(
            stderr,
            "%d:%d: %s\n",
            analysis->diagnostics[i].range.line,
            analysis->diagnostics[i].range.column,
            analysis->diagnostics[i].message
        );
    }
}
static bool diagnostic(const char *part)
{
    for (int i = 0; i < analysis->diagnostic_count; i++) {
        if (strstr(analysis->diagnostics[i].message, part)) {
            return true;
        }
    }
    return false;
}
TEST_CASE(record_semantic_row)
{
    parse(
        "Rows :: library { OutlinerRow :: struct { key: string; label: string; "
        "icon: string = \"\"; prototype: string = \"\"; drop: string = \"\"; "
        "depth: int = 1; behaviors: int = 0; active: bool = false; marked: "
        "bool = false; hidden: bool = false; locked: bool = false; renaming: "
        "bool = false; } Make :: () -> OutlinerRow { return {label: \"one\", "
        "key: \"a\"}; } Other :: () -> OutlinerRow { return {key: \"b\", "
        "label: \"two\"}; } }",
        &empty
    );
    REQUIRE(!syntax->diagnostic_count);
    REQUIRE(analysis->valid);
    REQUIRE(analysis->type_count == 1);
    const script_catalog_type *row = &analysis->types[0];
    REQUIRE(row->authored_record && row->field_count == 12 && row->size && row->layout);
    REQUIRE(!strcmp(row->name, "Rows.OutlinerRow"));
    REQUIRE(!strcmp(row->source_name, "OutlinerRow"));
    REQUIRE(!row->fields[0].has_default && !row->fields[1].has_default);
    REQUIRE(row->fields[5].has_default && row->fields[5].default_value.i == 1);
    int constructors = 0, owned_strings = 0;
    for (int i = 0; i < analysis->expression_count; i++) {
        const script_expression *e = &analysis->expressions[i];
        if (e->kind == SCRIPT_EXPR_STRUCT) {
            REQUIRE(e->argument_count == 12);
            if (!constructors) {
                REQUIRE(e->arguments[0].parameter == 1 && e->arguments[1].parameter == 0);
            }
            for (int j = 0; j < e->argument_count; j++) {
                const script_argument *argument = &e->arguments[j];
                REQUIRE(argument->parameter >= 0 && argument->parameter < row->field_count);
                REQUIRE(argument->value >= 0 && argument->value < analysis->expression_count);
                if (argument->parameter > 4) {
                    continue;
                }
                const script_expression *field = &analysis->expressions[argument->value];
                REQUIRE(
                    field->kind == SCRIPT_EXPR_CONSTANT &&
                    field->constant_snapshot && field->constant.type == PIN_STR
                );
                const char *expected = argument->parameter == 0
                                         ? (constructors ? "b" : "a")
                                     : argument->parameter == 1
                                         ? (constructors ? "two" : "one")
                                         : "";
                REQUIRE(!strcmp(pin_text(field->constant).bytes, expected));
                owned_strings += argument->parameter >= 2;
            }
            constructors++;
        }
    }
    REQUIRE(constructors == 2 && owned_strings == 6);
    return true;
}
TEST_CASE(record_semantic_operators)
{
    parse(
        "Defaults :: library { Row :: struct { depth: int = Later + 1; wrap: "
        "int = int(wrap: 2147483648); shift: int = -1 >> 33; flag: bool = true "
        "or "
        "(int(1) / int(1) > 1); same: bool = \"x\" == \"x\"; "
        "label: string = Text; } Later :: 3; Text :: \"saved\"; Make :: () -> "
        "Row { return {}; } }",
        &empty
    );
    REQUIRE(!syntax->diagnostic_count && analysis->valid);
    const script_catalog_type *row = &analysis->types[0];
    REQUIRE(row->field_count == 6);
    REQUIRE(row->fields[0].default_value.i == 4);
    REQUIRE(row->fields[1].default_value.i == INT32_MIN);
    REQUIRE(row->fields[2].default_value.i == -1);
    REQUIRE(row->fields[3].default_value.b && row->fields[4].default_value.b);
    REQUIRE(!strcmp(pin_text(row->fields[5].default_value).bytes, "saved"));
    return true;
}
TEST_CASE(record_semantic_required)
{
    static const char *const values[] = { "{}",
                                          "{key: \"a\", key: \"b\"}",
                                          "{other: \"a\"}",
                                          "{key: false}",
                                          "null" };
    static const char *const messages[] = { "Missing required",
                                            "Duplicate literal",
                                            "Unknown/ambiguous literal",
                                            "Incompatible types",
                                            "Missing required" };
    char source[256];
    for (int i = 0; i < 5; i++) {
        int length = snprintf(
            source,
            sizeof source,
            "Rows :: library { Row :: struct { key: string; } Make :: () -> "
            "Row { "
            "return %s; } }",
            values[i]
        );
        REQUIRE(length > 0 && (size_t)length < sizeof source);
        parse(source, &empty);
        REQUIRE(!syntax->diagnostic_count);
        REQUIRE(!analysis->valid && diagnostic(messages[i]));
    }
    parse("Rows :: library { Row :: struct { key: string; } value: Row; }", &empty);
    REQUIRE(!syntax->diagnostic_count && !analysis->valid && diagnostic("requires a default"));
    return true;
}
TEST_CASE(record_semantic_default_rejections)
{
    static const char *const bodies[] = {
        "Row :: struct { value: int = Call(); } Call :: () -> int { "
        "return 1; }",
        "Row :: struct { value: int = mutable; } mutable: int = 1;",
        "Row :: struct { value: int = 1 / 0; }",
        "Row :: struct { value: int = -2147483648 / -1; }",
        "Row :: struct { value: int = 1 % 0; }",
        "Row :: struct { value: bool = true or Call(); } Call :: () -> "
        "bool { return true; }",
        "Row :: struct { value: int = Later; } Later :: Earlier; Earlier :: "
        "Later;",
        "Row :: struct { value: string[] = []; }",
        "Row :: struct { value: bool = true or [false][0]; }"
    };
    static const char *const messages[] = {
        "Record default",
        "Record default",
        "integer constant divides by zero",
        "integer constant does not fit its type",
        "integer constant divides by zero",
        "Record default",
        "Cyclic value dependency",
        "Aggregate record fields require explicit initialization",
        "Record default"
    };
    _Static_assert(sizeof bodies / sizeof *bodies == 9, "fixed rejection inputs");
    _Static_assert(sizeof messages / sizeof *messages == 9, "matching diagnostics");
    char source[256];
    for (int i = 0; i < 9; i++) {
        int length = snprintf(source, sizeof source, "Rows :: library { %s }", bodies[i]);
        REQUIRE(length > 0 && (size_t)length < sizeof source);
        parse(source, &empty);
        REQUIRE(!syntax->diagnostic_count);
        REQUIRE(!analysis->valid && diagnostic(messages[i]));
    }
    return true;
}
TEST_CASE(record_semantic_scope_and_compatibility)
{
    static const char *const sources[] = {
        "Existing :: library { Copy :: (row: SceneObjectRow) -> "
        "SceneObjectRow { return row; } }",
        "Existing :: library { struct :: 1; Value :: () -> int { return "
        "struct; } }",
        "Wrong :: behavior { Row :: struct { key: string; } }",
        "Wrong :: library { Make :: () { Row :: struct { key: string; } } }"
    };
    for (int i = 0; i < 4; i++) {
        clear_analysis();
        syntax = script_parse(sources[i]);
        REQUIRE((syntax->diagnostic_count != 0) == (i >= 2));
    }
    return true;
}
TEST_CASE(record_semantic_catalog_lifetime)
{
    parse("Rows :: library { Row :: struct { label: string = \"owned-first\"; } }", &empty);
    REQUIRE(analysis->valid);
    script_catalog source = { .types = analysis->types,
                              .type_count = analysis->type_count },
                   old, edited;
    REQUIRE(script_catalog_snapshot(&first, &source, &old));
    REQUIRE(source_catalog_default_key(&old, &first_key));
    uint64_t id = old.types[0].id, layout = old.types[0].layout;
    parse("Rows :: library { Row :: struct { label: string = \"owned-second\"; } }", &empty);
    REQUIRE(analysis->valid);
    source = (script_catalog){ .types = analysis->types,
                               .type_count = analysis->type_count };
    REQUIRE(script_catalog_snapshot(&second, &source, &edited));
    REQUIRE(source_catalog_default_key(&edited, &second_key));
    REQUIRE(edited.types[0].id == id && edited.types[0].layout == layout);
    REQUIRE(source_catalog_metadata_equal(&old, &edited));
    REQUIRE(
        acount(first_key) != acount(second_key) ||
        memcmp(first_key, second_key, (size_t)acount(first_key))
    );
    parse("Client :: library { A :: () -> Rows.Row { return {}; } B :: () -> Rows.Row { return {}; } }", &old);
    REQUIRE(!syntax->diagnostic_count && analysis->valid);
    arena_free(&first);
    arena_free(&second);
    int strings = 0;
    for (int i = 0; i < analysis->expression_count; i++) {
        const script_expression *e = &analysis->expressions[i];
        if (e->kind == SCRIPT_EXPR_CONSTANT && e->constant.type == PIN_STR) {
            REQUIRE(e->constant_snapshot && !strcmp(pin_text(e->constant).bytes, "owned-first"));
            strings++;
        }
    }
    REQUIRE(strings == 2);
    return true;
}
TEST_CASE(record_semantic_collection_catalog_identity)
{
    script_type array = { .storage = PIN_I32,
                          .container = PIN_CONTAINER_ARRAY,
                          .collection = 901 };
    script_type map = { .storage = PIN_UNION,
                        .container = PIN_CONTAINER_MAP,
                        .collection = 902 };
    script_catalog_type types[] = {
        { .id = 901,
          .name = "NestedArray",
          .type = array,
          .container = PIN_CONTAINER_ARRAY,
          .element_type = { .storage = PIN_I32 } },
        { .id = 902,
          .name = "NestedMap",
          .type = map,
          .container = PIN_CONTAINER_MAP,
          .key_type = { .storage = PIN_STR },
          .element_type = array },
    };
    script_catalog_parameter argument = { .name = "value", .type = map };
    script_catalog_capability capability = {
        .id = 903,
        .name = "Consume",
        .parameters = &argument,
        .parameter_count = 1,
    };
    script_catalog source = { .types = types,
                              .type_count = 2,
                              .capabilities = &capability,
                              .capability_count = 1 },
                   saved;
    REQUIRE(script_catalog_snapshot(&first, &source, &saved));
    REQUIRE(source_catalog_metadata_equal(&source, &saved));
    REQUIRE(script_catalog_key_type(&saved, map).storage == PIN_STR);
    REQUIRE(script_type_equal(script_catalog_element_type(&saved, map), array));
    uint64_t abi = script_catalog_capability_abi(&saved, &saved.capabilities[0]);
    types[1].key_type.storage = PIN_I32;
    REQUIRE(!source_catalog_metadata_equal(&source, &saved));
    REQUIRE(script_catalog_capability_abi(&source, &capability) != abi);
    REQUIRE(script_catalog_key_type(&saved, map).storage == PIN_STR);
    types[1].key_type.storage = PIN_STR;
    types[0].element_type.storage = PIN_BOOL;
    REQUIRE(!source_catalog_metadata_equal(&source, &saved));
    REQUIRE(script_catalog_capability_abi(&source, &capability) != abi);
    REQUIRE(script_catalog_element_type(&saved, array).storage == PIN_I32);
    types[0].element_type.storage = PIN_I32;
    types[0].container = PIN_CONTAINER_SET;
    REQUIRE(!source_catalog_metadata_equal(&source, &saved));
    REQUIRE(script_catalog_capability_abi(&source, &capability) != abi);
    script_type other = map;
    other.collection++;
    REQUIRE(!script_type_equal(map, other));
    return true;
}

TEST_CASE(record_semantic_reference_literals_keep_symbol_identity)
{
    script_catalog_capability prototype = {
        .id = 1001,
        .name = "Enemy Drone",
        .kind = SCRIPT_CAPABILITY_PROTOTYPE,
    };
    script_catalog_type component = {
        .id = 1002,
        .name = "Health",
        .source_name = "Health",
        .key = "health_key",
        .component = true,
    };
    script_catalog_relationship relationship = { .id = 1003, .name = "Target" };
    script_catalog catalog = {
        .types = &component,
        .type_count = 1,
        .capabilities = &prototype,
        .capability_count = 1,
        .relationships = &relationship,
        .relationship_count = 1,
    };
    const script_reference_kind kinds[] = {
        SCRIPT_REFERENCE_PROTOTYPE,
        SCRIPT_REFERENCE_COMPONENT,
        SCRIPT_REFERENCE_RELATIONSHIP,
    };
    const pin_value values[] = {
        { .type = PIN_NAME, .str = "Enemy Drone" },
        { .type = PIN_NAME, .str = "health_key" },
        { .type = PIN_U64, .u64 = 1003 },
    };
    const char *spellings[] = { "Enemy_Drone", "Health", "Target" };
    char error[192];
    for (int i = 0; i < 3; i++) {
        char *source = authoring_source_literal(
            &catalog,
            script_reference_type(kinds[i]),
            &values[i],
            error,
            sizeof error
        );
        REQUIRE(source);
        bool same = !strcmp(source, spellings[i]);
        free(source);
        REQUIRE(same);
    }
    REQUIRE(!authoring_source_literal(
        &catalog,
        script_reference_type(SCRIPT_REFERENCE_COMPONENT),
        &values[0],
        error,
        sizeof error
    ));
    REQUIRE(strstr(error, "declared kind"));
    return true;
}

TEST_CASE(record_semantic_generic_catalog_snapshot_owns_bindings_and_parameters)
{
    char name[] = "Box", owner[] = "Library", parameter_name[] = "value",
         binder[] = "T";
    char binding_name[] = "label", label[] = "retained";
    script_template_parameter parameter = {
        .name = parameter_name,
        .binder = binder,
        .type = { .storage = PIN_I32 },
        .binds_type = true,
    };
    script_catalog_template definition = {
        .id = 71,
        .name = name,
        .owner = owner,
        .kind = SCRIPT_TEMPLATE_RECORD,
        .parameters = &parameter,
        .parameter_count = 1,
        .start = 4,
        .end = 20,
        .generic = true,
        .source_generation = 41,
    };
    pin_value item = { .type = PIN_I32, .i = 7 };
    pin_array array = { .element_type = PIN_I32, .count = 1, .capacity = 1, .items = &item };
    script_compile_binding bindings[] = {
        { .name = binding_name,
          .value = { .value = { .type = PIN_STR,
                                .text = &(
                                    const pin_string
                                ){ .bytes = label, .length = (uint32_t)strlen(label) } } } },
        { .name = "numbers",
          .value = { .value = { .type = PIN_I32,
                                .container = PIN_CONTAINER_ARRAY,
                                .ptr = &array } } },
    };
    script_catalog_specialization specialization = {
        .id = 72,
        .template_id = 71,
        .bindings = bindings,
        .binding_count = 2,
        .result = { .is_type = true, .type = { .storage = PIN_STRUCT, .nominal = 73 } },
    };
    script_catalog source = { .templates = &definition,
                              .template_count = 1,
                              .specializations = &specialization,
                              .specialization_count = 1 },
                   saved;
    REQUIRE(script_catalog_snapshot(&first, &source, &saved));
    REQUIRE(source_catalog_metadata_equal(&source, &saved));
    REQUIRE(source_catalog_default_key(&source, &first_key));
    definition.source_generation++;
    REQUIRE(!source_catalog_metadata_equal(&source, &saved));
    definition.source_generation--;
    parameter.comptime = true;
    REQUIRE(!source_catalog_metadata_equal(&source, &saved));
    parameter.comptime = false;
    item.i = 9;
    REQUIRE(!source_catalog_metadata_equal(&source, &saved));
    REQUIRE(source_catalog_default_key(&source, &second_key));
    REQUIRE(
        acount(first_key) == acount(second_key) &&
        memcmp(first_key, second_key, (size_t)acount(first_key))
    );
    name[0] = owner[0] = parameter_name[0] = binder[0] = binding_name[0] = label[0] = 'X';
    REQUIRE(!strcmp(saved.templates[0].name, "Box"));
    REQUIRE(!strcmp(saved.templates[0].owner, "Library"));
    REQUIRE(!strcmp(saved.templates[0].parameters[0].name, "value"));
    REQUIRE(!strcmp(saved.templates[0].parameters[0].binder, "T"));
    REQUIRE(!strcmp(saved.specializations[0].bindings[0].name, "label"));
    REQUIRE(!strcmp(pin_text(saved.specializations[0].bindings[0].value.value).bytes, "retained"));
    REQUIRE(
        pin_array_get(saved.specializations[0].bindings[1].value.value.ptr, 0).i == 7
    );
    REQUIRE(
        saved.specializations[0].result.is_type &&
        saved.specializations[0].result.type.nominal == 73
    );
    return true;
}

TEST_CASE(record_semantic_default_budget)
{
    char source[2048];
    int used = snprintf(source, sizeof source, "Budget :: library { Row :: struct { value: int = C17; } C0 :: 1; ");
    for (int i = 1; i <= 17; i++) {
        int count = snprintf(
            source + used,
            sizeof source - (size_t)used,
            "C%d :: C%d + C%d; ",
            i,
            i - 1,
            i - 1
        );
        REQUIRE(count > 0 && (size_t)count < sizeof source - (size_t)used);
        used += count;
    }
    REQUIRE((size_t)used + 2 < sizeof source);
    strcpy(source + used, "}");
    parse(source, &empty);
    REQUIRE(!syntax->diagnostic_count && !analysis->valid);
    REQUIRE(diagnostic("bounded budget"));
    return true;
}
void test_language_record_semantic()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(record_semantic_row);
    RUN_TEST_CASE(record_semantic_operators);
    RUN_TEST_CASE(record_semantic_required);
    RUN_TEST_CASE(record_semantic_default_rejections);
    RUN_TEST_CASE(record_semantic_scope_and_compatibility);
    RUN_TEST_CASE(record_semantic_catalog_lifetime);
    RUN_TEST_CASE(record_semantic_collection_catalog_identity);
    RUN_TEST_CASE(record_semantic_reference_literals_keep_symbol_identity);
    RUN_TEST_CASE(record_semantic_generic_catalog_snapshot_owns_bindings_and_parameters);
    RUN_TEST_CASE(record_semantic_default_budget);
    pu_clear_setup();
}
