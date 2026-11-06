#include "nominal_binding_chibi.h"

#include "compiler_fixture.h"
#include "language/nominal_binding.h"
#include "lexical_binding_chibi_source.h"
#include "nominal_binding_chibi_source.h"
#include "package_core_source.h"
#include "syntax_chibi_source.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/array.h"
#include "types/text.h"
#include "types/union.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Twelve fixed fact requests plus four real one-header analyses, both backends:
 * 32 Declare calls at 100,000 instructions each. Four compiler sources per
 * owner plus four tiny analyzed sources give twelve preparation/parser calls.
 * Tables contain at most one symbol, one function, two scopes and two documents.
 * Actual headers are library record/union, package record and behavior union;
 * record placement rejection is covered by raw facts. No IO, services, lowering
 * of the analyzed sources or generated combinations. */
static const char PACKAGE[] = "project:nominal_binding";
static const char BINDING[] = "project:lexical_binding";
static compiler_fixture fixture;
static arena input, detached, oracle, retained[2];
static pin_value kept[2];
static script_nominal_declaration saved[2];
static char saved_name[2][128], saved_source[2][128];
static script_syntax *syntax;
static script_analysis *analysis;
static int calls;
static bool prepared;

static void cleanup()
{
    arena_free(&input);
    arena_free(&detached);
    arena_free(&oracle);
    script_analysis_free(analysis);
    analysis = NULL;
    script_syntax_free(syntax);
    syntax = NULL;
}
static pin_value integer(int n)
{
    return (pin_value){ .type = PIN_I32, .i = n };
}
static pin_value boolean(bool n)
{
    return (pin_value){ .type = PIN_BOOL, .b = n };
}
static pin_value number(uint64_t n)
{
    return (pin_value){ .type = PIN_U64, .u64 = n };
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
static bool record(int b, const char *package, const char *name, pin_value *out)
{
    const pin_record *shape = compiler_fixture_record(&fixture, b, package, name, &input);
    REQUIRE(shape);
    *out = (pin_value){ .type = PIN_STRUCT,
                        .record = shape,
                        .type_name = shape->type.name,
                        .ptr = arena_alloc(&input, shape->type.size) };
    memset(out->ptr, 0, shape->type.size);
    return true;
}
static bool rows(int b, const char *package, const char *name, int count, pin_value *out)
{
    REQUIRE(count >= 0 && count <= 2);
    const pin_record *shape = compiler_fixture_record(&fixture, b, package, name, &input);
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
        REQUIRE(record(b, package, name, &array->items[i]));
    }
    return true;
}
static pin_value array(pin_type type, int count)
{
    pin_array *value = pin_array_snapshot_allocate(&input, type, NULL, count);
    return (pin_value){ .type = type, .container = PIN_CONTAINER_ARRAY, .ptr = value };
}
static bool range(pin_value *value, script_range r, bool name)
{
    REQUIRE(
        put(value, name ? "name_start" : "start", integer(r.start)) &&
        put(value, name ? "name_end" : "end", integer(r.end)) &&
        put(value, name ? "name_line" : "line", integer(r.line)) &&
        put(value, name ? "name_column" : "column", integer(r.column)) &&
        put(value, name ? "name_document" : "document", integer(r.document))
    );
    return true;
}
static const script_catalog_variant *variant(
    int b,
    const char *package,
    const char *type_name,
    const char *name
)
{
    const authoring_package *owner = compiler_fixture_package(&fixture, b, package);
    if (!owner || !owner->exports || !owner->exports->metadata) {
        return NULL;
    }
    const script_catalog_type *type = script_catalog_type_by_id(
        owner->exports->metadata,
        compiler_fixture_export(&fixture, b, package, type_name)
    );
    for (int i = 0; type && i < type->variant_count; i++) {
        if (!strcmp(type->variants[i].name, name)) {
            return &type->variants[i];
        }
    }
    return NULL;
}
static bool pack(
    int b,
    const script_lexical_facts *facts,
    int scopes,
    int functions,
    int documents,
    script_nominal_context context,
    script_nominal_request request,
    pin_value values[6]
)
{
    REQUIRE(
        scopes > 0 && scopes <= 2 && functions >= 0 && functions <= 1 &&
        documents >= 0 && documents <= 2 && facts->symbol_count >= 0 &&
        facts->symbol_count <= 1
    );
    REQUIRE(
        request.name && strlen(request.name) < 128 && context.program_name &&
        strlen(context.program_name) < 128
    );
    REQUIRE(!facts->identity || strlen(facts->identity) < 128);
    pin_value tables[5];
    REQUIRE(
        rows(b, BINDING, "Scope", scopes, &tables[0]) &&
        rows(b, BINDING, "Symbol", facts->symbol_count, &tables[1]) &&
        rows(b, BINDING, "Function", functions, &tables[2])
    );
    tables[3] = array(PIN_U64, facts->symbol_count);
    tables[4] = array(PIN_STR, documents);
    REQUIRE(tables[3].ptr && tables[4].ptr);
    REQUIRE(rows(b, PACKAGE, "SymbolType", facts->symbol_count, &values[3]));
    values[4] = array(PIN_U64, facts->symbol_count);
    REQUIRE(values[4].ptr);
    pin_array *scope_rows = tables[0].ptr, *symbol_rows = tables[1].ptr,
              *function_rows = tables[2].ptr, *ids = tables[3].ptr,
              *keys = tables[4].ptr, *types = values[3].ptr,
              *type_ids = values[4].ptr;
    for (int i = 0; i < scopes; i++) {
        REQUIRE(
            put(&scope_rows->items[i], "parent", integer(facts->scopes[i].parent)) &&
            put(&scope_rows->items[i], "function", integer(facts->scopes[i].function)) &&
            range(&scope_rows->items[i], facts->scopes[i].range, false)
        );
    }
    for (int i = 0; i < facts->symbol_count; i++) {
        const script_symbol *s = &facts->symbols[i];
        REQUIRE(s->name && strlen(s->name) < 128);
        REQUIRE(
            put(&symbol_rows->items[i], "name", text(s->name)) &&
            put(&symbol_rows->items[i], "kind", integer(s->kind)) &&
            put(&symbol_rows->items[i], "scope", integer(s->scope)) &&
            put(&symbol_rows->items[i], "id_slot", integer(i)) &&
            range(&symbol_rows->items[i], s->declaration, false)
        );
        ids->items[i] = number(s->id);
        REQUIRE(
            put(&types->items[i], "storage", integer(s->type.storage)) &&
            put(&types->items[i], "container", integer(s->type.container)) &&
            put(&types->items[i], "flags", integer((int)s->type.flags)) &&
            put(&types->items[i], "parameter_slot", integer(i))
        );
        type_ids->items[i] = number(s->type.parameter);
    }
    for (int i = 0; i < functions; i++) {
        REQUIRE(
            put(&function_rows->items[i], "symbol", integer(facts->functions[i].symbol)) &&
            put(&function_rows->items[i], "scope", integer(facts->functions[i].scope))
        );
    }
    for (int i = 0; i < documents; i++) {
        REQUIRE(facts->documents[i].key && strlen(facts->documents[i].key) < 128);
        keys->items[i] = text(facts->documents[i].key);
    }
    const script_catalog_variant *table = variant(b, BINDING, "Environment", "Tables");
    REQUIRE(table && table->field_count == 5);
    REQUIRE(!compiler_fixture_union(&input, table->tag, tables, 5, &values[0]));
    REQUIRE(
        record(b, BINDING, "Context", &values[1]) &&
        record(b, PACKAGE, "Context", &values[2]) &&
        record(b, PACKAGE, "Request", &values[5])
    );
    REQUIRE(
        put(&values[1], "identity", text(facts->identity)) &&
        put(&values[1], "scope", integer(context.lexical.scope)) &&
        put(&values[1], "function", integer(context.lexical.function)) &&
        put(&values[1], "package_scope", integer(context.lexical.package_scope)) &&
        put(&values[1], "has_identity", boolean(facts->identity != NULL)) &&
        put(&values[1], "has_documents", boolean(context.lexical.document_count != 0))
    );
    REQUIRE(
        put(&values[2], "program_name", text(context.program_name)) &&
        put(&values[2], "program_kind", integer(context.program_kind)) &&
        put(&values[2], "pointer_size", integer((int)context.pointer_size))
    );
    REQUIRE(
        put(&values[5], "name", text(request.name)) &&
        put(&values[5], "kind", integer(request.kind)) &&
        range(&values[5], request.range, false) &&
        range(&values[5], request.name_range, true)
    );
    return true;
}
static bool read(const pin_value *value, const char *name, pin_value *out)
{
    REQUIRE(value->type == PIN_STRUCT && value->ptr);
    const type_field *field = pin_type_field_path(pin_value_type(value), name);
    REQUIRE(field);
    REQUIRE(!pin_read_field(&input, value->ptr, field, out));
    return true;
}
static bool diagnostics(const pin_value *value, const script_nominal_declaration *want)
{
    REQUIRE(value->container == PIN_CONTAINER_ARRAY && value->type == PIN_STRUCT);
    const pin_array *rows = value->ptr;
    REQUIRE((rows ? rows->count : 0) == want->diagnostic_count);
    for (int i = 0; i < want->diagnostic_count; i++) {
        const script_diagnostic *d = &want->diagnostics[i];
        const char *names[] = { "start", "end", "line", "column", "document" };
        int expected[] = { d->range.start,
                           d->range.end,
                           d->range.line,
                           d->range.column,
                           d->range.document };
        for (int j = 0; j < 5; j++) {
            pin_value field;
            REQUIRE(
                read(&rows->items[i], names[j], &field) &&
                field.type == PIN_I32 && field.i == expected[j]
            );
        }
        pin_value message;
        REQUIRE(
            read(&rows->items[i], "message", &message) && message.type == PIN_STR &&
            pin_text(message).bytes && !strcmp(pin_text(message).bytes, d->message)
        );
    }
    return true;
}
static bool compare_value(const pin_value *result, uint64_t tag, const script_nominal_declaration *want)
{
    REQUIRE(result->type == PIN_UNION && result->ptr);
    const pin_union *data = result->ptr;
    REQUIRE(data->tag == tag);
    if (!want->declare) {
        REQUIRE(data->count == 1 && diagnostics(&data->values[0], want));
        return true;
    }
    REQUIRE(data->count == 14);
    const pin_value *v = data->values;
    REQUIRE(
        v[0].type == PIN_I32 && v[0].i == want->existing &&
        v[1].type == PIN_U64 && v[1].u64 == want->id
    );
    REQUIRE(
        v[2].type == PIN_I32 && v[2].i == (int)want->declaration_type.storage &&
        v[3].type == PIN_I32 && v[3].i == (int)want->declaration_type.flags
    );
    REQUIRE(v[4].type == PIN_BOOL && v[4].b == want->publish);
    REQUIRE(
        v[5].type == PIN_I32 && v[5].i == (int)want->type.storage &&
        v[6].type == PIN_I32 && v[6].i == (int)want->type.container &&
        v[7].type == PIN_I32 && v[7].i == (int)want->type.flags &&
        v[8].type == PIN_U64 && v[8].u64 == want->type.parameter
    );
    REQUIRE(
        v[9].type == PIN_STR && pin_text(v[9]).bytes &&
        !strcmp(pin_text(v[9]).bytes, want->name ? want->name : "") &&
        v[10].type == PIN_STR && pin_text(v[10]).bytes &&
        !strcmp(pin_text(v[10]).bytes, want->source_name ? want->source_name : "")
    );
    REQUIRE(
        v[11].type == PIN_BOOL && v[11].b == want->authored_record &&
        v[12].type == PIN_I32 && v[12].i == (int)want->size && diagnostics(&v[13], want)
    );
    return true;
}
static bool owns(arena *owner, const pin_value *value)
{
    if (value->container == PIN_CONTAINER_ARRAY) {
        if (!value->ptr) {
            return true;
        }
        REQUIRE(arena_contains(owner, value->ptr));
        const pin_array *array = value->ptr;
        REQUIRE(array->count >= 0 && array->count <= 3);
        REQUIRE(!array->count || (array->items && arena_contains(owner, array->items)));
        for (int i = 0; i < array->count; i++) {
            REQUIRE(owns(owner, &array->items[i]));
        }
        return true;
    }
    if (value->type == PIN_UNION) {
        REQUIRE(value->ptr && arena_contains(owner, value->ptr));
        const pin_union *data = value->ptr;
        REQUIRE(data->count <= 14);
        for (int i = 0; i < data->count; i++) {
            REQUIRE(owns(owner, &data->values[i]));
        }
        return true;
    }
    if (value->type == PIN_STR) {
        REQUIRE(pin_text(*value).bytes && arena_contains(owner, value->text));
        return true;
    }
    if (value->type == PIN_STRUCT) {
        REQUIRE(
            value->ptr && value->record && arena_contains(owner, value->ptr) &&
            arena_contains(owner, value->record)
        );
        const type_desc *type = pin_value_type(value);
        REQUIRE(type && type->field_count <= 6);
        for (int i = 0; i < type->field_count; i++) {
            pin_value field;
            REQUIRE(!pin_read_field(owner, value->ptr, &type->fields[i], &field));
            REQUIRE(owns(owner, &field));
        }
        return true;
    }
    REQUIRE(value->type == PIN_I32 || value->type == PIN_U64 || value->type == PIN_BOOL);
    return true;
}
static uint64_t tags[2];
static bool compare(
    const script_lexical_facts *facts,
    int scopes,
    int functions,
    int documents,
    script_nominal_context context,
    script_nominal_request request,
    bool retain,
    script_nominal_declaration *expected
)
{
    arena_free(&oracle);
    script_nominal_declaration want = script_nominal_declare(&oracle, facts, context, request);
    REQUIRE(want.diagnostic_count >= 0 && want.diagnostic_count <= 3);
    if (expected) {
        *expected = want;
    }
    for (int b = 0; b < 2; b++) {
        arena_free(&input);
        arena_free(&detached);
        pin_value values[6], result;
        REQUIRE(pack(b, facts, scopes, functions, documents, context, request, values));
        REQUIRE(
            compiler_fixture_call(&fixture, b, PACKAGE, "Declare", values, 6, 100000, &detached, &result)
        );
        calls++;
        arena_free(&input);
        const script_catalog_variant
            *form = variant(b, PACKAGE, "Declaration", want.declare ? "Bound" : "Skipped");
        REQUIRE(form);
        REQUIRE(owns(&detached, &result) && compare_value(&result, form->tag, &want));
        if (retain) {
            REQUIRE(
                want.name && want.source_name &&
                strlen(want.name) < sizeof saved_name[b] &&
                strlen(want.source_name) < sizeof saved_source[b]
            );
            saved[b] = want;
            strcpy(saved_name[b], want.name);
            strcpy(saved_source[b], want.source_name);
            saved[b].name = saved_name[b];
            saved[b].source_name = saved_source[b];
            REQUIRE(!compiler_fixture_snapshot(&retained[b], &kept[b], &result));
            tags[b] = form->tag;
        }
    }
    return true;
}
TEST_CASE(nominal_raw_facts_match_header_admission)
{
    const source_package_input files[] = {
        { "nominal_binding/Nominal.chibi",
          NOMINAL_BINDING_CHIBI_SOURCE,
          sizeof NOMINAL_BINDING_CHIBI_SOURCE - 1 },
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
    for (int i = 0; i < 3; i++) {
        REQUIRE(files[i].size < 16384);
    }
    REQUIRE(compiler_fixture_prepare(&fixture, files, 4));
    prepared = true;
    for (int i = 0; i < 12; i++) {
        script_scope scope = { .parent = -1,
                               .function = -1,
                               .range = { 0, 100, 1, 1, 0 } };
        script_function function = { .symbol = -1, .scope = 0 };
        script_source_document documents[] = { { .key = "pkg/a.chibi" },
                                               { .key = "pkg/b.chibi" } };
        script_nominal_context context = { .lexical = { .scope = 0,
                                                        .function = -1,
                                                        .package_scope = 0,
                                                        .document_count = 2 },
                                           .program_kind = SCRIPT_PROGRAM_LIBRARY,
                                           .program_name = "Model",
                                           .pointer_size = sizeof(void *) };
        script_nominal_request request = { .kind = SCRIPT_NOMINAL_RECORD,
                                           .name = "Pair",
                                           .range = { 2, 50, 1, 3, 0 },
                                           .name_range = { 10, 14, 2, 3, 0 } };
        script_symbol symbol = { .id = UINT64_C(18446744073709551601),
                                 .name = "Pair",
                                 .kind = SCRIPT_TYPE,
                                 .scope = 0,
                                 .declaration = request.name_range,
                                 .type = { .storage = PIN_STRUCT } };
        script_lexical_facts facts = { .scopes = &scope,
                                       .symbols = &symbol,
                                       .functions = &function,
                                       .documents = documents,
                                       .identity = "project:model" };
        if (i == 1) {
            request.kind = SCRIPT_NOMINAL_UNION;
            request.name = "Choice";
            context.program_kind = SCRIPT_PROGRAM_BEHAVIOR;
            context.lexical.document_count = 0;
            context.lexical.package_scope = -1;
        }
        if (i == 2) {
            context.program_kind = SCRIPT_PROGRAM_BEHAVIOR;
        }
        if (i == 3) {
            context.lexical.function = 0;
            request.name = "Cancelled";
        }
        if (i >= 4 && i <= 9) {
            facts.symbol_count = 1;
        }
        if (i == 4) {
            symbol.type = (script_type){ .storage = PIN_I32,
                                         .container = PIN_CONTAINER_ARRAY,
                                         .flags = SCRIPT_TYPE_OPTIONAL,
                                         .parameter = UINT64_C(18446744073709551599) };
        }
        if (i == 5 || i == 6 || i == 9) {
            request.kind = SCRIPT_NOMINAL_UNION;
            symbol.type.storage = i == 6 ? PIN_STRUCT : PIN_UNION;
        }
        if (i == 7) {
            symbol.kind = SCRIPT_LOCAL;
        }
        if (i == 8) {
            symbol.declaration.document = 1;
        }
        if (i == 9) {
            symbol.declaration.start++;
        }
        if (i >= 10) {
            request.name = "Cancelled";
            symbol.name = "Cancelled";
        }
        if (i == 11) {
            facts.symbol_count = 1;
        }
        script_nominal_declaration want;
        REQUIRE(
            compare(&facts, 1, i == 3 ? 1 : 0, context.lexical.document_count, context, request, i == 11, &want)
        );
        if (i == 0) {
            REQUIRE(
                want.id == script_catalog_id("package-symbol", "project:model:13:Pair") &&
                want.publish && !want.diagnostic_count
            );
        }
        if (i == 2 || i == 3) {
            REQUIRE(
                !want.declare && want.diagnostic_count == 1 &&
                want.diagnostics[0].range.start == request.range.start
            );
        }
        if (i == 4) {
            REQUIRE(
                want.publish && want.type.storage == PIN_I32 &&
                want.type.parameter == symbol.type.parameter
            );
        }
        if (i >= 6 && i <= 9) {
            REQUIRE(!want.publish && want.existing == 0);
        }
        if (i == 11) {
            REQUIRE(
                want.publish && want.diagnostic_count == 3 && want.id == symbol.id
            );
        }
    }
    REQUIRE(calls == 24);
    return true;
}
TEST_CASE(nominal_analyzed_headers_match_metadata_and_origins)
{
    REQUIRE(prepared);
    static const char *sources[] = {
        "Model :: library { Pair :: struct { amount: int; } }",
        "Model :: library { Choice :: union { None | Value(amount: int) }; }",
        "package model\nPair :: struct { amount: int; }\n",
        "Model :: behavior { Choice :: union { None | Value(amount: int) }; }",
    };
    const script_catalog native = { 0 };
    uint64_t legacy_record = 0;
    for (int c = 0; c < 4; c++) {
        script_analysis_free(analysis);
        analysis = NULL;
        script_syntax_free(syntax);
        syntax = NULL;
        REQUIRE(strlen(sources[c]) < 256);
        syntax = script_parse(sources[c]);
        for (int i = 0; syntax && i < syntax->diagnostic_count; i++) {
            fprintf(
                stderr,
                "nominal analyzed source %d at %d:%d: %s\n",
                c,
                syntax->diagnostics[i].range.line,
                syntax->diagnostics[i].range.column,
                syntax->diagnostics[i].message
            );
        }
        REQUIRE(syntax && !syntax->diagnostic_count);
        TSNode body = ts_tree_root_node(syntax->tree);
        if (c == 2) {
            const script_package_document document = { "/model/Pair.chibi",
                                                       "model/Pair.chibi",
                                                       syntax };
            const script_package_options options = {
                .documents = &document,
                .document_count = 1,
                .identity = "project:model",
                .native = &native
            };
            analysis = script_analyze_package(&options);
        } else {
            analysis = script_analyze_document(syntax, &native, "legacy/Model.chibi");
            TSNode program = ts_node_named_child(body, 0);
            body = ts_node_child_by_field_name(program, "body", 4);
        }
        REQUIRE(analysis && !ts_node_is_null(body));
        TSNode declaration = { 0 };
        for (uint32_t i = 0; i < ts_node_named_child_count(body); i++) {
            TSNode child = ts_node_named_child(body, i);
            if (!strcmp(ts_node_type(child), c == 1 || c == 3 ? "union_declaration" : "record_declaration")) {
                declaration = child;
                break;
            }
        }
        REQUIRE(!ts_node_is_null(declaration));
        TSNode name = ts_node_child_by_field_name(declaration, "name", 4);
        REQUIRE(!ts_node_is_null(name));
        script_nominal_request request = {
            .kind = c == 1 || c == 3 ? SCRIPT_NOMINAL_UNION : SCRIPT_NOMINAL_RECORD,
            .name = c == 1 || c == 3 ? "Choice" : "Pair",
            .range = script_node_range(declaration),
            .name_range = script_node_range(name)
        };
        int found = -1;
        for (int i = 0; i < analysis->symbol_count; i++) {
            if (analysis->symbols[i].kind == SCRIPT_TYPE &&
                !strcmp(analysis->symbols[i].name, request.name)) {
                found = i;
                break;
            }
        }
        int scope = found >= 0 ? analysis->symbols[found].scope : 1;
        script_nominal_context context = {
            .lexical = { .scope = scope,
                         .function = -1,
                         .package_scope = analysis->package_scope,
                         .document_count = analysis->document_count },
            .program_kind = analysis->kind,
            .program_name = analysis->name,
            .pointer_size = sizeof(void *)
        };
        script_lexical_facts facts = script_lexical_view(
            analysis,
            found >= 0 ? found : analysis->symbol_count
        );
        script_nominal_declaration want;
        REQUIRE(compare(
            &facts,
            analysis->scope_count,
            analysis->function_count,
            analysis->document_count,
            context,
            request,
            false,
            &want
        ));
        REQUIRE(
            analysis->valid && found >= 0 && want.publish &&
            want.id == analysis->symbols[found].id
        );
        if (c == 0) {
            legacy_record = want.id;
        }
        if (c == 2) {
            REQUIRE(
                want.id == script_catalog_id("package-symbol", "project:model:13:Pair") &&
                want.id != legacy_record
            );
        }
        const script_catalog_type *type = NULL;
        for (int i = 0; i < analysis->type_count; i++) {
            if (analysis->types[i].id == want.id) {
                type = &analysis->types[i];
                break;
            }
        }
        REQUIRE(
            type && type->name && type->source_name && !strcmp(type->name, want.name) &&
            !strcmp(type->source_name, want.source_name) &&
            type->authored_record == want.authored_record &&
            type->type.nominal == want.id && type->type.storage == want.type.storage &&
            type->type.container == want.type.container &&
            type->type.flags == want.type.flags &&
            type->type.parameter == want.type.parameter
        );
    }
    REQUIRE(calls == 32);
    return true;
}
TEST_CASE(nominal_results_survive_input_analysis_and_owner_retirement)
{
    cleanup();
    compiler_fixture_free(&fixture);
    for (int b = 0; b < 2; b++) {
        REQUIRE(
            kept[b].type == PIN_UNION && kept[b].ptr && owns(&retained[b], &kept[b]) &&
            compare_value(&kept[b], tags[b], &saved[b])
        );
    }
    return true;
}
void test_language_nominal_binding_chibi()
{
    calls = 0;
    prepared = false;
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(nominal_raw_facts_match_header_admission);
    RUN_TEST_CASE(nominal_analyzed_headers_match_metadata_and_origins);
    RUN_TEST_CASE(nominal_results_survive_input_analysis_and_owner_retirement);
    pu_clear_setup();
    compiler_fixture_free(&fixture);
    for (int b = 0; b < 2; b++) {
        arena_free(&retained[b]);
        kept[b] = (pin_value){ 0 };
    }
}
