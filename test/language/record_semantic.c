#include "authoring/source_catalog_snapshot.h"
#include "language/semantic.h"
#include "support/array.h"
#include "third_party/pico_headers/pico_unit.h"

#include <stdio.h>
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
        "library Rows { OutlinerRow :: struct { key: string; label: string; "
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
            constructors++;
        }
        if (e->kind == SCRIPT_EXPR_CONSTANT && e->constant_snapshot &&
            e->constant.type == PIN_STR) {
            REQUIRE(!strcmp(e->constant.str, ""));
            owned_strings++;
        }
    }
    REQUIRE(constructors == 2 && owned_strings == 6);
    return true;
}
TEST_CASE(record_semantic_operators)
{
    parse(
        "library Defaults { Row :: struct { depth: int = Later + 1; wrap: int "
        "= 2147483647 + 1; shift: int = -1 >> 33; flag: bool = true or "
        "(int(1) / int(0) > 0); same: bool = \"x\" == \"x\"; "
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
    REQUIRE(!strcmp(row->fields[5].default_value.str, "saved"));
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
            "library Rows { Row :: struct { key: string; } Make :: () -> Row { "
            "return %s; } }",
            values[i]
        );
        REQUIRE(length > 0 && (size_t)length < sizeof source);
        parse(source, &empty);
        REQUIRE(!syntax->diagnostic_count);
        REQUIRE(!analysis->valid && diagnostic(messages[i]));
    }
    parse("library Rows { Row :: struct { key: string; } value: Row; }", &empty);
    REQUIRE(!syntax->diagnostic_count && !analysis->valid && diagnostic("requires a default"));
    return true;
}
TEST_CASE(record_semantic_default_rejections)
{
    static const char *const bodies[] = {
        "Row :: struct { value: int = Call(); } Call :: pure () -> int { "
        "return 1; }",
        "Row :: struct { value: int = mutable; } mutable: int = 1;",
        "Row :: struct { value: int = 1 / 0; }",
        "Row :: struct { value: int = -2147483648 / -1; }",
        "Row :: struct { value: int = -2147483648 % -1; }",
        "Row :: struct { value: bool = true or Call(); } Call :: pure () -> "
        "bool { return true; }",
        "Row :: struct { value: int = Later; } Later :: Earlier; Earlier :: "
        "Later;",
        "Row :: struct { value: string[]; }",
        "Row :: struct { value: bool = true or [false][0]; }"
    };
    static const char *const messages[] = { "Record default",
                                            "Record default",
                                            "invalid integer division",
                                            "invalid integer division",
                                            "invalid integer division",
                                            "Record default",
                                            "Cyclic value dependency",
                                            "Record fields require",
                                            "Record default" };
    _Static_assert(sizeof bodies / sizeof *bodies == 9, "fixed rejection inputs");
    _Static_assert(sizeof messages / sizeof *messages == 9, "matching diagnostics");
    char source[256];
    for (int i = 0; i < 9; i++) {
        int length = snprintf(source, sizeof source, "library Rows { %s }", bodies[i]);
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
        "library Existing { Copy :: (row: struct<\"SceneObjectRow\">) -> "
        "struct<\"SceneObjectRow\"> { return row; } }",
        "library Existing { struct :: 1; Value :: () -> int { return struct; } "
        "}",
        "behavior Wrong { Row :: struct { key: string; } }",
        "library Wrong { Make :: () { Row :: struct { key: string; } } }"
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
    parse("library Rows { Row :: struct { label: string = \"owned-first\"; } }", &empty);
    REQUIRE(analysis->valid);
    script_catalog source = { .types = analysis->types,
                              .type_count = analysis->type_count },
                   old, edited;
    REQUIRE(script_catalog_snapshot(&first, &source, &old));
    REQUIRE(source_catalog_default_key(&old, &first_key));
    uint64_t id = old.types[0].id, layout = old.types[0].layout;
    parse("library Rows { Row :: struct { label: string = \"owned-second\"; } }", &empty);
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
    parse("library Client { A :: () -> Rows.Row { return {}; } B :: () -> Rows.Row { return {}; } }", &old);
    REQUIRE(!syntax->diagnostic_count && analysis->valid);
    arena_free(&first);
    arena_free(&second);
    int strings = 0;
    for (int i = 0; i < analysis->expression_count; i++) {
        const script_expression *e = &analysis->expressions[i];
        if (e->kind == SCRIPT_EXPR_CONSTANT && e->constant.type == PIN_STR) {
            REQUIRE(e->constant_snapshot && !strcmp(e->constant.str, "owned-first"));
            strings++;
        }
    }
    REQUIRE(strings == 2);
    return true;
}
TEST_CASE(record_semantic_default_budget)
{
    char source[2048];
    int used = snprintf(source, sizeof source, "library Budget { Row :: struct { value: int = C17; } C0 :: 1; ");
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
    RUN_TEST_CASE(record_semantic_default_budget);
    pu_clear_setup();
}
