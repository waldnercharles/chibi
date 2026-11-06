#include "lexical_binding_chibi.h"

#include "compiler_fixture.h"
#include "language/lexical_binding.h"
#include "lexical_binding_chibi_source.h"
#include "package_core_source.h"
#include "syntax_chibi_source.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/array.h"
#include "types/text.h"
#include "types/union.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Forty requests (24 fixed tables, sixteen from four real analyses), eighty
 * authored calls, each capped at two million instructions. Three authored
 * sources per backend and four fact sources give ten preparation parses.
 * At most 32 symbols, eight scopes, four functions and two documents coexist;
 * names are at most 220 bytes, identity/keys at most 128, diagnostics at most
 * three. No IO, tasks, services, timing loops or generated combinations. */
enum
{
    MAX_SYMBOLS = 32,
    MAX_SCOPES = 8,
    MAX_FUNCTIONS = 4,
    MAX_DOCUMENTS = 2,
    RAW_REQUESTS = 24
};
typedef enum
{
    NAME,
    STORAGE,
    CALLABLE,
    DECLARE
} operation;
typedef struct
{
    operation operation;
    int scope;
    const char *name;
    script_symbol_kind kind;
    script_lexical_context context;
    script_range range;
    uint64_t expected_id;
    bool truncated;
} request;
static const char PACKAGE[] = "project:lexical_binding";
static compiler_fixture fixture;
static arena input, detached, retained[2][2];
static pin_value kept[2][2];
static script_lexical_declaration saved[2];
static script_syntax *syntax;
static script_analysis *analysis;
static script_scope scopes[MAX_SCOPES];
static script_symbol symbols[MAX_SYMBOLS];
static script_function functions[MAX_FUNCTIONS];
static script_source_document documents[MAX_DOCUMENTS];
static char long_name[221];

static void call_cleanup()
{
    arena_free(&input);
    arena_free(&detached);
}
static void cleanup()
{
    call_cleanup();
    script_analysis_free(analysis);
    script_syntax_free(syntax);
    analysis = NULL;
    syntax = NULL;
}
static pin_value integer(int value)
{
    return (pin_value){ .type = PIN_I32, .i = value };
}
static pin_value boolean(bool value)
{
    return (pin_value){ .type = PIN_BOOL, .b = value };
}
static pin_value text(const char *value)
{
    value = value ? value : "";
    pin_value result;
    const char *error = pin_value_text(&input, &result, value, strlen(value));
    if (error) {
        abort();
    }
    return result;
}
static bool put(pin_value *row, const char *name, pin_value value)
{
    const type_field *field = pin_type_field_path(pin_value_type(row), name);
    REQUIRE(field && field->type == value.type);
    REQUIRE(!pin_write_field(&input, row->ptr, field, value));
    return true;
}
static bool record(int b, const char *name, pin_value *out)
{
    const pin_record *shape = compiler_fixture_record(&fixture, b, PACKAGE, name, &input);
    REQUIRE(shape);
    *out = (pin_value){ .type = PIN_STRUCT,
                        .record = shape,
                        .type_name = shape->type.name,
                        .ptr = arena_alloc(&input, shape->type.size) };
    memset(out->ptr, 0, shape->type.size);
    return true;
}
static bool rows(int b, const char *name, int count, pin_value *out)
{
    const pin_record *shape = compiler_fixture_record(&fixture, b, PACKAGE, name, &input);
    REQUIRE(shape);
    pin_array
        *array = pin_array_snapshot_allocate(&input, PIN_STRUCT, shape->type.name, count);
    REQUIRE(array);
    array->record = shape;
    *out = (pin_value){ .type = PIN_STRUCT,
                        .container = PIN_CONTAINER_ARRAY,
                        .record = shape,
                        .type_name = shape->type.name,
                        .ptr = array };
    for (int i = 0; i < count; i++) {
        REQUIRE(record(b, name, &array->items[i]));
    }
    return true;
}
static bool range_fields(pin_value *row, script_range range)
{
    REQUIRE(
        put(row, "start", integer(range.start)) &&
        put(row, "end", integer(range.end)) && put(row, "line", integer(range.line)) &&
        put(row, "column", integer(range.column)) &&
        put(row, "document", integer(range.document))
    );
    return true;
}
static const script_catalog_variant *variant(int b, const char *type_name, const char *name)
{
    const authoring_package *package = compiler_fixture_package(&fixture, b, PACKAGE);
    if (!package || !package->exports || !package->exports->metadata) {
        return NULL;
    }
    const script_catalog_type *type = script_catalog_type_by_id(
        package->exports->metadata,
        compiler_fixture_export(&fixture, b, PACKAGE, type_name)
    );
    for (int i = 0; type && i < type->variant_count; i++) {
        if (!strcmp(type->variants[i].name, name)) {
            return &type->variants[i];
        }
    }
    return NULL;
}
static bool environment(
    int b,
    const script_lexical_facts *facts,
    int scope_count,
    int function_count,
    int document_count,
    pin_value *out
)
{
    REQUIRE(facts->symbol_count >= 0 && facts->symbol_count <= MAX_SYMBOLS);
    REQUIRE(scope_count > 0 && scope_count <= MAX_SCOPES);
    REQUIRE(function_count >= 0 && function_count <= MAX_FUNCTIONS);
    REQUIRE(document_count >= 0 && document_count <= MAX_DOCUMENTS);
    pin_value values[5];
    REQUIRE(rows(b, "Scope", scope_count, &values[0]));
    REQUIRE(rows(b, "Symbol", facts->symbol_count, &values[1]));
    REQUIRE(rows(b, "Function", function_count, &values[2]));
    pin_array *ids = pin_array_snapshot_allocate(&input, PIN_U64, NULL, facts->symbol_count);
    pin_array *keys = pin_array_snapshot_allocate(&input, PIN_STR, NULL, document_count);
    REQUIRE(ids && keys);
    values[3] = (pin_value){ .type = PIN_U64, .container = PIN_CONTAINER_ARRAY, .ptr = ids };
    values[4] = (pin_value){ .type = PIN_STR,
                             .container = PIN_CONTAINER_ARRAY,
                             .ptr = keys };
    pin_array *scope_rows = values[0].ptr, *symbol_rows = values[1].ptr,
              *function_rows = values[2].ptr;
    for (int i = 0; i < scope_count; i++) {
        REQUIRE(
            put(&scope_rows->items[i], "parent", integer(facts->scopes[i].parent)) &&
            put(&scope_rows->items[i], "function", integer(facts->scopes[i].function)) &&
            range_fields(&scope_rows->items[i], facts->scopes[i].range)
        );
    }
    for (int i = 0; i < facts->symbol_count; i++) {
        const script_symbol *symbol = &facts->symbols[i];
        REQUIRE(symbol->name && strlen(symbol->name) <= 220);
        REQUIRE(
            put(&symbol_rows->items[i], "name", text(symbol->name)) &&
            put(&symbol_rows->items[i], "kind", integer(symbol->kind)) &&
            put(&symbol_rows->items[i], "scope", integer(symbol->scope)) &&
            put(&symbol_rows->items[i], "id_slot", integer(i)) &&
            range_fields(&symbol_rows->items[i], symbol->declaration)
        );
        ids->items[i] = (pin_value){ .type = PIN_U64, .u64 = symbol->id };
    }
    for (int i = 0; i < function_count; i++) {
        REQUIRE(
            put(&function_rows->items[i], "symbol", integer(facts->functions[i].symbol)) &&
            put(&function_rows->items[i], "scope", integer(facts->functions[i].scope))
        );
    }
    for (int i = 0; i < document_count; i++) {
        REQUIRE(facts->documents[i].key && strlen(facts->documents[i].key) <= 128);
        keys->items[i] = text(facts->documents[i].key);
    }
    const script_catalog_variant *tables = variant(b, "Environment", "Tables");
    REQUIRE(tables && tables->field_count == 5);
    REQUIRE(!compiler_fixture_union(&input, tables->tag, values, 5, out));
    return true;
}
static bool read(const pin_value *row, const char *name, pin_value *out)
{
    REQUIRE(row->type == PIN_STRUCT && row->ptr);
    const type_field *field = pin_type_field_path(pin_value_type(row), name);
    REQUIRE(field);
    REQUIRE(!pin_read_field(&input, row->ptr, field, out));
    return true;
}
static bool same_int(const pin_value *row, const char *name, int value)
{
    pin_value actual;
    REQUIRE(read(row, name, &actual) && actual.type == PIN_I32 && actual.i == value);
    return true;
}
static bool diagnostics(const arena *storage, const pin_value *value, const script_lexical_declaration *expected)
{
    REQUIRE(value->type == PIN_STRUCT && value->container == PIN_CONTAINER_ARRAY);
    if (!value->ptr) {
        REQUIRE(expected->diagnostic_count == 0);
        return true;
    }
    const pin_array *array = value->ptr;
    REQUIRE(arena_contains(storage, array));
    REQUIRE(!array->count || arena_contains(storage, array->items));
    REQUIRE(array->count == expected->diagnostic_count);
    for (int i = 0; i < array->count; i++) {
        const pin_value *row = &array->items[i];
        const script_diagnostic *error = &expected->diagnostics[i];
        REQUIRE(
            row->ptr && row->record && arena_contains(storage, row->ptr) &&
            arena_contains(storage, row->record)
        );
        REQUIRE(
            same_int(row, "start", error->range.start) &&
            same_int(row, "end", error->range.end) &&
            same_int(row, "line", error->range.line) &&
            same_int(row, "column", error->range.column) &&
            same_int(row, "document", error->range.document)
        );
        pin_value message;
        REQUIRE(
            read(row, "message", &message) && message.type == PIN_STR &&
            pin_text(message).bytes
        );
        REQUIRE(
            arena_contains(storage, message.text) &&
            !strcmp(pin_text(message).bytes, error->message)
        );
    }
    return true;
}
static bool declaration(const arena *storage, const pin_value *value, const script_lexical_declaration *expected)
{
    REQUIRE(
        value->type == PIN_UNION && value->ptr && arena_contains(storage, value->ptr)
    );
    const pin_union *data = value->ptr;
    REQUIRE(data->count == 2 && arena_contains(storage, data->values));
    if (expected->existing >= 0) {
        REQUIRE(data->values[0].type == PIN_I32 && data->values[0].i == expected->existing);
    } else {
        REQUIRE(
            data->values[0].type == PIN_U64 && data->values[0].u64 == expected->id
        );
    }
    REQUIRE(diagnostics(storage, &data->values[1], expected));
    return true;
}
static bool compare(
    const script_lexical_facts *facts,
    int scope_count,
    int function_count,
    int document_count,
    request query,
    int retain
)
{
    REQUIRE(fixture.owners[0] && fixture.owners[1]);
    REQUIRE(
        authoring_packages_valid(fixture.owners[0]) &&
        authoring_packages_valid(fixture.owners[1])
    );
    REQUIRE(
        query.name && strlen(query.name) <= 220 &&
        (!facts->identity || strlen(facts->identity) <= 128)
    );
    const char *entry = query.operation == NAME     ? "FindName"
                      : query.operation == STORAGE  ? "FindStorage"
                      : query.operation == CALLABLE ? "FindCallable"
                                                    : "Declare";
    int expected = -1;
    script_lexical_declaration declared = { 0 };
    switch (query.operation) {
        case NAME:
            expected = script_lexical_name(facts, query.scope, query.name);
            break;
        case STORAGE:
            expected = script_lexical_storage(
                facts,
                query.scope,
                query.name,
                query.kind
            );
            break;
        case CALLABLE:
            expected = script_lexical_callable(facts, query.scope, query.name);
            break;
        case DECLARE:
            declared = script_lexical_declare(
                facts,
                query.context,
                query.range,
                query.name,
                query.kind
            );
            break;
    }
    if (query.expected_id) {
        REQUIRE(declared.existing == -1 && declared.id == query.expected_id);
    }
    if (query.truncated) {
        static const char prefix[] = "Duplicate declaration '";
        REQUIRE(declared.existing == 13 && declared.diagnostic_count == 1);
        const char *message = declared.diagnostics[0].message;
        REQUIRE(strlen(message) == 191 && !strncmp(message, prefix, sizeof prefix - 1));
        for (int i = sizeof prefix - 1; i < 191; i++) {
            REQUIRE(message[i] == 'n');
        }
    }
    if (retain >= 0) {
        saved[retain] = declared;
    }
    for (int b = 0; b < 2; b++) {
        call_cleanup();
        pin_value args[4], result;
        REQUIRE(environment(b, facts, scope_count, function_count, document_count, &args[0]));
        int count = query.operation == STORAGE ? 4 : 3;
        if (query.operation == DECLARE) {
            REQUIRE(record(b, "Context", &args[1]) && record(b, "Request", &args[2]));
            REQUIRE(
                put(&args[1], "identity", text(facts->identity)) &&
                put(&args[1], "has_identity", boolean(facts->identity != NULL)) &&
                put(&args[1], "has_documents", boolean(query.context.document_count != 0)) &&
                put(&args[1], "scope", integer(query.context.scope)) &&
                put(&args[1], "function", integer(query.context.function)) &&
                put(&args[1], "package_scope", integer(query.context.package_scope))
            );
            REQUIRE(
                put(&args[2], "name", text(query.name)) &&
                put(&args[2], "kind", integer(query.kind)) &&
                range_fields(&args[2], query.range)
            );
        } else {
            args[1] = integer(query.scope);
            args[2] = text(query.name);
            args[3] = integer(query.kind);
        }
        REQUIRE(
            compiler_fixture_call(&fixture, b, PACKAGE, entry, args, count, 2000000, &detached, &result)
        );
        arena_free(&input);
        if (query.operation != DECLARE) {
            REQUIRE(result.type == PIN_I32 && result.i == expected);
            continue;
        }
        const script_catalog_variant
            *tag = variant(b, "Declaration", declared.existing >= 0 ? "Existing" : "Appended");
        REQUIRE(
            tag && result.type == PIN_UNION && result.ptr &&
            ((const pin_union *)result.ptr)->tag == tag->tag
        );
        REQUIRE(declaration(&detached, &result, &declared));
        if (retain >= 0) {
            REQUIRE(!compiler_fixture_snapshot(&retained[b][retain], &kept[b][retain], &result));
            REQUIRE(declaration(&retained[b][retain], &kept[b][retain], &declared));
        }
    }
    return true;
}
static void raw_facts()
{
    memset(scopes, 0, sizeof scopes);
    memset(symbols, 0, sizeof symbols);
    memset(functions, 0, sizeof functions);
    memset(long_name, 'n', sizeof long_name - 1);
    long_name[sizeof long_name - 1] = 0;
    scopes[0] = (script_scope){ .parent = -1,
                                .function = -1,
                                .range = { 0, 200, 1, 1, 0 } };
    scopes[1] = (script_scope){ .parent = 0,
                                .function = 0,
                                .range = { 50, 150, 3, 2, 0 } };
    scopes[2] = (script_scope){ .parent = 1,
                                .function = 0,
                                .range = { 90, 120, 5, 4, 1 } };
    scopes[3] = (script_scope){ .parent = 0,
                                .function = -1,
                                .range = { 160, 190, 9, 2, 1 } };
    const char *names[] = { "x",         "fn",   "macro",     "shared", "x",
                            "local",     "call", "call",      "x",      "fn",
                            "Cancelled", "fn",   "Cancelled", long_name };
    const script_symbol_kind kinds[] = {
        SCRIPT_STATE,     SCRIPT_FUNCTION, SCRIPT_MACRO,    SCRIPT_SHARED_STATE,
        SCRIPT_PARAMETER, SCRIPT_LOCAL,    SCRIPT_FUNCTION, SCRIPT_ACTION,
        SCRIPT_LOCAL,     SCRIPT_LOCAL,    SCRIPT_LOCAL,    SCRIPT_MACRO,
        SCRIPT_PARAMETER, SCRIPT_LOCAL
    };
    const int owners[] = { 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 0, 0, 1, 2 };
    for (int i = 0; i < 14; i++) {
        symbols[i] = (script_symbol){
            .name = (char *)names[i],
            .kind = kinds[i],
            .scope = owners[i],
            .id = UINT64_MAX - (uint64_t)i,
            .declaration = { 10 + i * 2, 11 + i * 2, 2 + i, 3 + i, i % 2 }
        };
    }
    functions[0] = (script_function){ .symbol = 1, .scope = 1 };
    documents[0] = (script_source_document){ .key = "first/Main.chibi" };
    documents[1] = (script_source_document){ .key = "second/Other.chibi" };
}
static bool raw_request(int index)
{
    script_lexical_facts facts = { .scopes = scopes,
                                   .symbols = symbols,
                                   .functions = functions,
                                   .documents = documents,
                                   .symbol_count = 14,
                                   .identity = "project:binding" };
    request query = { .operation = NAME,
                      .scope = 2,
                      .name = "x",
                      .kind = SCRIPT_LOCAL,
                      .context = { .scope = 2, .function = -1, .package_scope = 0 },
                      .range = { 301, 304, 12, 7, 1 } };
    switch (index) {
        case 0: break;
        case 1: query.scope = 1; break;
        case 2:
            query.scope = 0;
            query.name = "fn";
            break;
        case 3: query.name = "missing"; break;
        case 4:
            query.operation = STORAGE;
            query.kind = SCRIPT_STATE;
            break;
        case 5:
            query.operation = STORAGE;
            query.name = "shared";
            query.kind = SCRIPT_STATE;
            break;
        case 6:
            query.operation = STORAGE;
            query.name = "shared";
            query.kind = SCRIPT_SHARED_STATE;
            break;
        case 7: query.operation = STORAGE; break;
        case 8:
            query.operation = CALLABLE;
            query.scope = 1;
            query.name = "call";
            break;
        case 9:
            query.operation = CALLABLE;
            query.name = "fn";
            break;
        case 10:
            query.operation = CALLABLE;
            query.name = "missing";
            break;
        case 11:
            query.operation = CALLABLE;
            query.name = "macro";
            break;
        case 12:
            query.operation = DECLARE;
            query.name = long_name;
            query.truncated = true;
            break;
        case 13:
            query.operation = DECLARE;
            query.context.scope = 1;
            break;
        case 14:
            query.operation = DECLARE;
            query.context.scope = 1;
            query.kind = SCRIPT_RESULT;
            break;
        case 15:
            query.operation = DECLARE;
            query.name = "fresh";
            break;
        case 16:
            query.operation = DECLARE;
            query.context.scope = 0;
            query.name = "fn";
            query.kind = SCRIPT_FUNCTION;
            break;
        case 17:
            query.operation = DECLARE;
            query.context.scope = 0;
            query.name = "fn";
            break;
        case 18:
        case 19:
            query.operation = DECLARE;
            query.context.scope = 0;
            query.name = "Cancelled";
            query.context.document_count = index == 19 ? 2 : 0;
            break;
        case 20:
            query.operation = DECLARE;
            query.context.scope = 0;
            query.context.document_count = 2;
            query.name = "fn";
            query.kind = SCRIPT_FUNCTION;
            break;
        case 21:
        case 22:
            query.operation = DECLARE;
            query.name = "fresh";
            query.context.scope = index == 21 ? 2 : 1;
            query.context.function = 0;
            query.context.document_count = 2;
            /* FNV-1a over package-local:18446744073709551614:<key>:<start>:8:fresh.
             * Scope2 uses second/Other.chibi:90, direct scope1 first/Main.chibi:-1. */
            query.expected_id = index == 21 ? UINT64_C(7753933517814665030)
                                            : UINT64_C(16492155535662703308);
            break;
        case 23:
            query.operation = DECLARE;
            query.context.scope = 3;
            query.name = "fresh";
            facts.identity = NULL;
            break;
    }
    return compare(
        &facts,
        4,
        1,
        2,
        query,
        index == 19   ? 0
        : index == 21 ? 1
                      : -1
    );
}
TEST_CASE(lexical_binding_raw_facts_match)
{
    source_package_input files[] = {
        { "lexical_binding/Binding.chibi",
          LEXICAL_BINDING_CHIBI_SOURCE,
          sizeof LEXICAL_BINDING_CHIBI_SOURCE - 1 },
        { "syntax_diagnostics/Diagnostics.chibi",
          SYNTAX_CHIBI_SOURCE,
          sizeof SYNTAX_CHIBI_SOURCE - 1 },
        { "/sdk/core/strings/Strings.chibi",
          CORE_STRINGS_SOURCE,
          sizeof CORE_STRINGS_SOURCE - 1 },
    };
    /* Core strings uses the production snapshot limits and full retention checks. */
    for (int i = 0; i < 2; i++) {
        REQUIRE(files[i].size < 16384);
    }
    REQUIRE(compiler_fixture_prepare(&fixture, files, 3));
    raw_facts();
    for (int i = 0; i < RAW_REQUESTS; i++) {
        REQUIRE(raw_request(i));
    }
    return true;
}
TEST_CASE(lexical_binding_analyzer_facts_match)
{
    const char *sources[] = {
        "Facts :: library { Run :: (value: int) -> int { local: int = "
        "value; return local; } }",
        "Facts :: library { Seed: int : 3; Run :: (value: int) -> "
        "int { local: int = value + Seed; return local; } }",
        "package facts\nRun :: (value: int) -> int { local: int = "
        "value; return local; }",
        "package facts\nRun :: (value: int) -> int { if value > 0 { "
        "local: int = value; return local; } return value; }",
    };
    const script_catalog native = { 0 };
    for (int c = 0; c < 4; c++) {
        script_analysis_free(analysis);
        script_syntax_free(syntax);
        analysis = NULL;
        syntax = script_parse(sources[c]);
        REQUIRE(syntax && !syntax->diagnostic_count);
        if (c < 2) {
            analysis = script_analyze_document(syntax, &native, "legacy/Facts.chibi");
        } else {
            const script_package_document document = {
                .path = "/facts/Main.chibi",
                .key = c == 2 ? "direct/Main.chibi" : "nested/Main.chibi",
                .syntax = syntax
            };
            const script_package_options options = {
                .documents = &document,
                .document_count = 1,
                .identity = "project:facts",
                .native = &native
            };
            analysis = script_analyze_package(&options);
        }
        REQUIRE(analysis && analysis->valid);
        int local = -1;
        for (int i = 0; i < analysis->symbol_count; i++) {
            if (!strcmp(analysis->symbols[i].name, "local")) {
                local = i;
                break;
            }
        }
        REQUIRE(local >= 0);
        int scope = analysis->symbols[local].scope;
        const script_lexical_facts facts = { .scopes = analysis->scopes,
                                             .symbols = analysis->symbols,
                                             .functions = analysis->functions,
                                             .documents = analysis->documents,
                                             .symbol_count = analysis->symbol_count,
                                             .identity = analysis->identity };
        request query = { .scope = scope,
                          .name = "local",
                          .context = { .scope = scope,
                                       .function = analysis->scopes[scope].function,
                                       .package_scope = analysis->package_scope,
                                       .document_count = analysis->document_count },
                          .range = { 401, 406, 16, 2, 0 } };
        REQUIRE(compare(
            &facts,
            analysis->scope_count,
            analysis->function_count,
            analysis->document_count,
            query,
            -1
        ));
        query.operation = STORAGE;
        query.name = "value";
        query.kind = SCRIPT_PARAMETER;
        REQUIRE(compare(
            &facts,
            analysis->scope_count,
            analysis->function_count,
            analysis->document_count,
            query,
            -1
        ));
        query.operation = CALLABLE;
        query.name = "Run";
        REQUIRE(compare(
            &facts,
            analysis->scope_count,
            analysis->function_count,
            analysis->document_count,
            query,
            -1
        ));
        query.operation = DECLARE;
        query.name = "fresh";
        query.kind = SCRIPT_LOCAL;
        REQUIRE(compare(
            &facts,
            analysis->scope_count,
            analysis->function_count,
            analysis->document_count,
            query,
            -1
        ));
    }
    return true;
}
TEST_CASE(lexical_binding_results_outlive_owners)
{
    compiler_fixture_free(&fixture);
    cleanup();
    REQUIRE(saved[0].diagnostic_count == 3 && saved[0].existing == 10);
    REQUIRE(saved[1].existing == -1 && saved[1].id != 0);
    for (int b = 0; b < 2; b++) {
        for (int i = 0; i < 2; i++) {
            REQUIRE(declaration(&retained[b][i], &kept[b][i], &saved[i]));
        }
    }
    return true;
}
void test_language_lexical_binding_chibi()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(lexical_binding_raw_facts_match);
    RUN_TEST_CASE(lexical_binding_analyzer_facts_match);
    RUN_TEST_CASE(lexical_binding_results_outlive_owners);
    pu_clear_setup();
    cleanup();
    compiler_fixture_free(&fixture);
    for (int b = 0; b < 2; b++) {
        for (int i = 0; i < 2; i++) {
            arena_free(&retained[b][i]);
            kept[b][i] = (pin_value){ 0 };
        }
    }
}
