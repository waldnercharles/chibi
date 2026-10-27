#include "named_binding_chibi.h"

#include "compiler_fixture.h"
#include "language/named_binding.h"
#include "lexical_binding_chibi_source.h"
#include "named_binding_chibi_source.h"
#include "package_core_source.h"
#include "syntax_chibi_source.h"
#include "third_party/pico_headers/pico_unit.h"
#include "toolchain/compiler_json.h"
#include "types/array.h"
#include "types/text.h"
#include "types/union.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 28 fixed requests plus eight from four actual annotation analyses: 36 C
 * queries and 72 authored calls at two million instructions each. Four
 * authored files per backend plus four analyzed sources give twelve parses.
 * Caps: 32 symbols, eight scopes, four functions, two documents, eight catalog
 * entries, three imports/exports, four uses, eight tokens, 64 semantic types,
 * 128 exact uint64 IDs, 220 name bytes and 256 fallback text bytes. Three
 * quoted Text requests give six private JSON codec calls. Fact trees contain
 * at most 128 nodes at depth16. No IO,
 * workers, services, tasks, generated combinations or timing loops. */
enum
{
    MAX_SYMBOLS = 32,
    MAX_SCOPES = 8,
    MAX_FUNCTIONS = 4,
    MAX_ENTRIES = 8,
    MAX_TYPES = 64,
    MAX_IDS = 128,
    RAW_REQUESTS = 28
};
typedef enum
{
    PARSED,
    TEXT,
    FIND
} operation;
typedef struct
{
    operation operation;
    script_named_request request;
    const char *text;
    const char *tokens[8];
    int token_count;
    bool check_find;
    int expected_find;
} query;
static const char PACKAGE[] = "project:named_binding",
                  LEXICAL[] = "project:lexical_binding";
static compiler_fixture fixture;
static arena input, detached, retained[2][2];
static pin_value kept[2][2];
static script_named_resolution saved[2];
static script_syntax *syntax;
static script_analysis *analysis;
static script_type types[MAX_TYPES];
static uint64_t ids[MAX_IDS];
static int type_count, id_count;
static script_scope scopes[3];
static script_symbol symbols[4];
static script_function functions[1];
static script_source_document documents[2];
static script_catalog_type entries[MAX_ENTRIES], own[2], natives[6];
static script_catalog merged_catalog, native_catalog;
static script_package_import imports[3];
static script_package_export exports[1];
static script_package_exports export_view;
static script_use uses[2];
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
static pin_value integer(int n)
{
    return (pin_value){ .type = PIN_I32, .i = n };
}
static pin_value wide(uint64_t n)
{
    return (pin_value){ .type = PIN_U64, .u64 = n };
}
static pin_value boolean(bool n)
{
    return (pin_value){ .type = PIN_BOOL, .b = n };
}
static pin_value text(const char *source)
{
    source = source ? source : "";
    pin_value result;
    const char *error = pin_value_text(&input, &result, source, strlen(source));
    if (error) {
        abort();
    }
    return result;
}
static int bits(unsigned value)
{
    int result;
    _Static_assert(sizeof value == sizeof result, "flags width");
    memcpy(&result, &value, sizeof result);
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
    const pin_record *shape = compiler_fixture_record(&fixture, b, package, name, &input);
    REQUIRE(shape && count >= 0);
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
    return (pin_value){ .type = type,
                        .container = PIN_CONTAINER_ARRAY,
                        .ptr = pin_array_snapshot_allocate(&input, type, NULL, count) };
}
static bool range(pin_value *row, script_range r)
{
    REQUIRE(
        put(row, "start", integer(r.start)) && put(row, "end", integer(r.end)) &&
        put(row, "line", integer(r.line)) && put(row, "column", integer(r.column)) &&
        put(row, "document", integer(r.document))
    );
    return true;
}
static const script_catalog_variant *variant(int b, const char *package, const char *name, const char *member)
{
    const authoring_package *owner = compiler_fixture_package(&fixture, b, package);
    if (!owner || !owner->exports || !owner->exports->metadata) {
        return NULL;
    }
    const script_catalog_type *type = script_catalog_type_by_id(
        owner->exports->metadata,
        compiler_fixture_export(&fixture, b, package, name)
    );
    for (int i = 0; type && i < type->variant_count; i++) {
        if (!strcmp(type->variants[i].name, member)) {
            return &type->variants[i];
        }
    }
    return NULL;
}
static bool tagged(
    int b,
    const char *package,
    const char *name,
    const char *member,
    pin_value *fields,
    int count,
    pin_value *out
)
{
    const script_catalog_variant *tag = variant(b, package, name, member);
    REQUIRE(tag && tag->field_count == count);
    REQUIRE(!compiler_fixture_union(&input, tag->tag, fields, count, out));
    return true;
}
static int slot(uint64_t id)
{
    if (!id) {
        return -1;
    }
    for (int i = 0; i < id_count; i++) {
        if (ids[i] == id) {
            return i;
        }
    }
    if (id_count == MAX_IDS) {
        return -2;
    }
    ids[id_count] = id;
    return id_count++;
}
static int type_slot(script_type type)
{
    for (int i = 0; i < type_count; i++) {
        script_type t = types[i];
        if (t.storage == type.storage && t.container == type.container &&
            t.flags == type.flags && t.nominal == type.nominal &&
            t.parameter == type.parameter && t.collection == type.collection) {
            return i;
        }
    }
    if (type_count == MAX_TYPES) {
        return -1;
    }
    types[type_count] = type;
    return type_count++;
}
static bool lexical(int b, const script_named_facts *facts, int scope_count, int function_count, pin_value *out)
{
    REQUIRE(facts->lexical.symbol_count >= 0 && facts->lexical.symbol_count <= MAX_SYMBOLS);
    REQUIRE(
        scope_count > 0 && scope_count <= MAX_SCOPES && function_count >= 0 &&
        function_count <= MAX_FUNCTIONS
    );
    pin_value fields[5];
    REQUIRE(
        rows(b, LEXICAL, "Scope", scope_count, &fields[0]) &&
        rows(b, LEXICAL, "Symbol", facts->lexical.symbol_count, &fields[1]) &&
        rows(b, LEXICAL, "Function", function_count, &fields[2])
    );
    fields[3] = array(PIN_U64, facts->lexical.symbol_count);
    fields[4] = array(PIN_STR, facts->document_count);
    pin_array *scope_rows = fields[0].ptr, *symbol_rows = fields[1].ptr,
              *function_rows = fields[2].ptr, *identities = fields[3].ptr,
              *keys = fields[4].ptr;
    for (int i = 0; i < scope_count; i++) {
        REQUIRE(
            put(&scope_rows->items[i],
                "parent",
                integer(facts->lexical.scopes[i].parent)) &&
            put(&scope_rows->items[i],
                "function",
                integer(facts->lexical.scopes[i].function)) &&
            range(&scope_rows->items[i], facts->lexical.scopes[i].range)
        );
    }
    for (int i = 0; i < facts->lexical.symbol_count; i++) {
        const script_symbol *symbol = &facts->lexical.symbols[i];
        REQUIRE(symbol->name && strlen(symbol->name) <= 220);
        REQUIRE(
            put(&symbol_rows->items[i], "name", text(symbol->name)) &&
            put(&symbol_rows->items[i], "kind", integer(symbol->kind)) &&
            put(&symbol_rows->items[i], "scope", integer(symbol->scope)) &&
            put(&symbol_rows->items[i], "id_slot", integer(i)) &&
            range(&symbol_rows->items[i], symbol->declaration)
        );
        identities->items[i] = wide(symbol->id);
    }
    for (int i = 0; i < function_count; i++) {
        REQUIRE(
            put(&function_rows->items[i],
                "symbol",
                integer(facts->lexical.functions[i].symbol)) &&
            put(&function_rows->items[i],
                "scope",
                integer(facts->lexical.functions[i].scope))
        );
    }
    for (int i = 0; i < facts->document_count; i++) {
        REQUIRE(
            facts->lexical.documents[i].key &&
            strlen(facts->lexical.documents[i].key) <= 128
        );
        keys->items[i] = text(facts->lexical.documents[i].key);
    }
    REQUIRE(tagged(b, LEXICAL, "Environment", "Tables", fields, 5, out));
    return true;
}
static bool catalog_rows(int b, const script_catalog_type *entries, int count, pin_value *out)
{
    REQUIRE(count >= 0 && count <= MAX_ENTRIES && rows(b, PACKAGE, "Entry", count, out));
    pin_array *array = out->ptr;
    for (int i = 0; i < count; i++) {
        const script_catalog_type *entry = &entries[i];
        REQUIRE(entry->name && strlen(entry->name) <= 128);
        REQUIRE(!entry->source_name || strlen(entry->source_name) <= 128);
        REQUIRE(!entry->key || strlen(entry->key) <= 128);
        pin_value *row = &array->items[i];
        int id = slot(entry->id), type = type_slot(entry->type);
        REQUIRE(id >= -1 && type >= 0);
        REQUIRE(
            put(row, "name", text(entry->name)) &&
            put(row, "source_name", text(entry->source_name)) &&
            put(row, "key", text(entry->key)) &&
            put(row, "has_source_name", boolean(entry->source_name != NULL)) &&
            put(row, "has_key", boolean(entry->key != NULL)) &&
            put(row, "id_slot", integer(id)) && put(row, "type", integer(type)) &&
            put(row, "internal", boolean(entry->internal)) &&
            put(row, "component", boolean(entry->component))
        );
    }
    return true;
}
static bool environment(int b, const script_named_facts *facts, int scope_count, int function_count, pin_value *out)
{
    REQUIRE(
        facts->document_count >= 0 && facts->document_count <= 2 &&
        facts->import_count >= 0 && facts->import_count <= 3 &&
        facts->use_count >= 0 && facts->use_count <= 4
    );
    type_count = id_count = 0;
    pin_value fields[16];
    REQUIRE(lexical(b, facts, scope_count, function_count, &fields[0]));
    REQUIRE(
        catalog_rows(b, facts->catalog->types, facts->catalog->type_count, &fields[3]) &&
        catalog_rows(b, facts->own_types, facts->own_type_count, &fields[4]) &&
        catalog_rows(
            b,
            facts->native ? facts->native->types : NULL,
            facts->native ? facts->native->type_count : 0,
            &fields[5]
        )
    );
    fields[6] = array(PIN_I32, facts->lexical.symbol_count);
    pin_array *symbol_types = fields[6].ptr;
    for (int i = 0; i < facts->lexical.symbol_count; i++) {
        int type = type_slot(facts->lexical.symbols[i].type);
        REQUIRE(type >= 0);
        symbol_types->items[i] = integer(type);
    }
    int export_count = 0;
    for (int i = 0; i < facts->import_count; i++) {
        export_count += facts->imports[i].exports ? facts->imports[i].exports->member_count
                                                  : 0;
    }
    REQUIRE(export_count <= 3);
    REQUIRE(
        rows(b, PACKAGE, "Import", facts->import_count, &fields[7]) &&
        rows(b, PACKAGE, "Export", export_count, &fields[8]) &&
        rows(b, PACKAGE, "Use", facts->use_count, &fields[9])
    );
    pin_array *import_rows = fields[7].ptr, *export_rows = fields[8].ptr,
              *use_rows = fields[9].ptr;
    int at = 0;
    for (int i = 0; i < facts->import_count; i++) {
        const script_package_import *import = &facts->imports[i];
        REQUIRE(import->identity && strlen(import->identity) <= 128);
        REQUIRE(!import->alias || strlen(import->alias) <= 128);
        pin_value *row = &import_rows->items[i];
        int count = import->exports ? import->exports->member_count : 0;
        REQUIRE(
            put(row, "alias", text(import->alias)) &&
            put(row, "identity", text(import->identity)) &&
            put(row, "document", integer(import->document)) &&
            put(row, "export_start", integer(at)) &&
            put(row, "export_count", integer(count)) &&
            put(row, "has_alias", boolean(import->alias != NULL))
        );
        for (int j = 0; j < count; j++) {
            const script_package_export *export = &import->exports->members[j];
            int id = slot(export->id);
            REQUIRE(id >= -1);
            REQUIRE(
                put(&export_rows->items[at], "name", text(export->name)) &&
                put(&export_rows->items[at], "kind", integer(export->kind)) &&
                put(&export_rows->items[at], "id_slot", integer(id))
            );
            at++;
        }
    }
    for (int i = 0; i < facts->use_count; i++) {
        const script_use *use = &facts->uses[i];
        pin_value *row = &use_rows->items[i];
        int member = slot(use->member);
        REQUIRE(member >= -1);
        REQUIRE(
            put(row, "kind", integer(use->kind)) &&
            put(row, "symbol", integer(use->symbol)) &&
            put(row, "member_slot", integer(member)) && range(row, use->range)
        );
    }
    REQUIRE(rows(b, PACKAGE, "Type", type_count, &fields[1]));
    pin_array *type_rows = fields[1].ptr;
    for (int i = 0; i < type_count; i++) {
        pin_value *row = &type_rows->items[i];
        int nominal = slot(types[i].nominal), parameter = slot(types[i].parameter);
        REQUIRE(nominal >= -1 && parameter >= -1);
        REQUIRE(
            put(row, "storage", integer(types[i].storage)) &&
            put(row, "container", integer(types[i].container)) &&
            put(row, "flags", integer(bits(types[i].flags))) &&
            put(row, "nominal_slot", integer(nominal)) &&
            put(row, "parameter_slot", integer(parameter)) &&
            put(row, "collection", wide(types[i].collection))
        );
    }
    fields[2] = array(PIN_U64, id_count);
    pin_array *id_rows = fields[2].ptr;
    for (int i = 0; i < id_count; i++) {
        id_rows->items[i] = wide(ids[i]);
    }
    fields[10] = text(facts->lexical.identity);
    fields[11] = integer(facts->package_scope);
    fields[12] = boolean(facts->native != NULL);
    fields[13] = boolean(facts->document_count != 0);
    fields[14] = wide(facts->catalog->entity_type);
    REQUIRE(tagged(b, PACKAGE, "Environment", "Tables", fields, 15, out));
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
static bool same_int(const pin_value *row, const char *name, int expected)
{
    pin_value value;
    REQUIRE(read(row, name, &value) && value.type == PIN_I32 && value.i == expected);
    return true;
}
static bool owned(const arena *storage, const pin_value *value, int depth)
{
    REQUIRE(depth <= 5);
    if (value->container == PIN_CONTAINER_ARRAY) {
        if (!value->ptr) {
            return true;
        }
        const pin_array *array = value->ptr;
        REQUIRE(arena_contains(storage, array) && array->count >= 0 && array->count <= 1);
        REQUIRE(!array->count || arena_contains(storage, array->items));
        for (int i = 0; i < array->count; i++) {
            REQUIRE(owned(storage, &array->items[i], depth + 1));
        }
        return true;
    }
    REQUIRE(value->container == PIN_CONTAINER_SINGLE);
    if (value->type == PIN_UNION) {
        const pin_union *data = value->ptr;
        REQUIRE(data && arena_contains(storage, data) && data->count >= 0 && data->count <= 8);
        REQUIRE(!data->count || arena_contains(storage, data->values));
        for (int i = 0; i < data->count; i++) {
            REQUIRE(owned(storage, &data->values[i], depth + 1));
        }
        return true;
    }
    if (value->type == PIN_STRUCT) {
        REQUIRE(
            value->ptr && value->record && arena_contains(storage, value->ptr) &&
            arena_contains(storage, value->record)
        );
        const type_desc *type = pin_value_type(value);
        REQUIRE(type && type->field_count == 6);
        for (int i = 0; i < type->field_count; i++) {
            pin_value field;
            REQUIRE(!pin_read_field(&input, value->ptr, &type->fields[i], &field));
            REQUIRE(owned(storage, &field, depth + 1));
        }
        return true;
    }
    if (value->type == PIN_STR) {
        REQUIRE(
            pin_text(*value).bytes && arena_contains(storage, pin_text(*value).bytes)
        );
        return true;
    }
    REQUIRE(value->type == PIN_I32 || value->type == PIN_U64 || value->type == PIN_BOOL);
    return true;
}
static bool resolution(const pin_value *value, const script_named_resolution *expected)
{
    REQUIRE(value->type == PIN_UNION && value->ptr);
    const pin_union *data = value->ptr;
    REQUIRE(data->count == 8);
    REQUIRE(
        data->values[0].type == PIN_I32 &&
        data->values[0].i == (int)expected->type.storage &&
        data->values[1].type == PIN_I32 &&
        data->values[1].i == (int)expected->type.container &&
        data->values[2].type == PIN_I32 &&
        data->values[2].i == bits(expected->type.flags)
    );
    REQUIRE(
        data->values[3].type == PIN_U64 &&
        data->values[3].u64 == expected->type.nominal && data->values[4].type == PIN_U64 &&
        data->values[4].u64 == expected->type.parameter &&
        data->values[7].type == PIN_U64 && data->values[7].u64 == expected->type.collection
    );
    REQUIRE(data->values[5].type == PIN_UNION && data->values[5].ptr);
    const pin_union *use = data->values[5].ptr;
    REQUIRE(use->count == (expected->has_use ? 7 : 0));
    if (expected->has_use) {
        REQUIRE(
            use->values[0].type == PIN_I32 &&
            use->values[0].i == expected->use.symbol && use->values[1].type == PIN_U64 &&
            use->values[1].u64 == expected->use.member
        );
        const int ranges[] = { expected->use.range.start,
                               expected->use.range.end,
                               expected->use.range.line,
                               expected->use.range.column,
                               expected->use.range.document };
        for (int i = 0; i < 5; i++) {
            REQUIRE(use->values[i + 2].type == PIN_I32 && use->values[i + 2].i == ranges[i]);
        }
    }
    const pin_value *diagnostics = &data->values[6];
    REQUIRE(diagnostics->type == PIN_STRUCT && diagnostics->container == PIN_CONTAINER_ARRAY);
    const pin_array *array = diagnostics->ptr;
    REQUIRE((array ? array->count : 0) == (expected->has_diagnostic ? 1 : 0));
    if (expected->has_diagnostic) {
        const pin_value *row = &array->items[0];
        script_range r = expected->diagnostic.range;
        REQUIRE(
            same_int(row, "start", r.start) && same_int(row, "end", r.end) &&
            same_int(row, "line", r.line) && same_int(row, "column", r.column) &&
            same_int(row, "document", r.document)
        );
        pin_value message;
        REQUIRE(
            read(row, "message", &message) && message.type == PIN_STR &&
            pin_text(message).bytes &&
            !strcmp(pin_text(message).bytes, expected->diagnostic.message)
        );
    }
    return true;
}
static bool compare(const script_named_facts *facts, int scope_count, int function_count, query query, int retain)
{
    REQUIRE(
        fixture.owners[0] && fixture.owners[1] &&
        authoring_packages_valid(fixture.owners[0]) &&
        authoring_packages_valid(fixture.owners[1])
    );
    REQUIRE(
        query.text && strlen(query.text) <= 256 && query.token_count >= 0 &&
        query.token_count <= 8
    );
    REQUIRE(query.operation == TEXT || strlen(query.text) <= 220);
    REQUIRE(!facts->lexical.identity || strlen(facts->lexical.identity) <= 128);
    int found = -1;
    script_named_resolution expected = { 0 };
    if (query.operation == FIND) {
        found = script_named_find_type(
            facts,
            query.request.scope,
            query.request.range.document,
            query.text
        );
        if (query.check_find) {
            REQUIRE(found == query.expected_find);
        }
    } else if (query.operation == PARSED) {
        expected = script_named_parsed(
            facts,
            query.request,
            query.text,
            query.tokens,
            query.token_count
        );
    } else {
        expected = script_named_text(facts, query.request, query.text);
    }
    if (retain >= 0) {
        saved[retain] = expected;
    }
    for (int b = 0; b < 2; b++) {
        call_cleanup();
        pin_value args[4], result;
        REQUIRE(environment(b, facts, scope_count, function_count, &args[0]));
        int count = 2;
        const char *entry = "Text";
        if (query.operation == FIND) {
            entry = "FindType";
            count = 4;
            args[1] = integer(query.request.scope);
            args[2] = integer(query.request.range.document);
            args[3] = text(query.text);
        } else {
            REQUIRE(record(b, PACKAGE, "Request", &args[1]));
            REQUIRE(
                put(&args[1], "text", text(query.text)) &&
                put(&args[1], "scope", integer(query.request.scope)) &&
                range(&args[1], query.request.range)
            );
            if (query.operation == PARSED) {
                entry = "Parsed";
                count = 3;
                args[2] = array(PIN_STR, query.token_count);
                pin_array *tokens = args[2].ptr;
                for (int i = 0; i < query.token_count; i++) {
                    REQUIRE(query.tokens[i] && strlen(query.tokens[i]) <= 32);
                    tokens->items[i] = text(query.tokens[i]);
                }
            }
        }
        REQUIRE(
            compiler_fixture_call(&fixture, b, PACKAGE, entry, args, count, 2000000, &detached, &result)
        );
        arena_free(&input);
        if (query.operation == FIND) {
            REQUIRE(result.type == PIN_I32 && result.i == found);
            continue;
        }
        const script_catalog_variant *tag = variant(b, PACKAGE, "Resolution", "Resolved");
        REQUIRE(
            tag && result.type == PIN_UNION && result.ptr &&
            ((const pin_union *)result.ptr)->tag == tag->tag
        );
        REQUIRE(owned(&detached, &result, 0) && resolution(&result, &expected));
        const pin_union *resolved = result.ptr;
        const script_catalog_variant
            *use_tag = variant(b, PACKAGE, "TypeUse", expected.has_use ? "Reference" : "Absent");
        REQUIRE(
            use_tag &&
            ((const pin_union *)resolved->values[5].ptr)->tag == use_tag->tag
        );
        if (retain >= 0) {
            REQUIRE(!compiler_fixture_snapshot(&retained[b][retain], &kept[b][retain], &result));
            REQUIRE(
                owned(&retained[b][retain], &kept[b][retain], 0) &&
                resolution(&kept[b][retain], &expected)
            );
        }
    }
    return true;
}
static script_named_facts raw_facts()
{
    memset(long_name, 'n', 220);
    long_name[220] = 0;
    scopes[0] = (script_scope){ .parent = -1, .function = -1 };
    scopes[1] = (script_scope){ .parent = 0, .function = 0 };
    scopes[2] = (script_scope){ .parent = 1, .function = 0 };
    for (int i = 0; i < MAX_ENTRIES; i++) {
        entries[i] = (script_catalog_type){ .id = UINT64_MAX - (uint64_t)i - 10,
                                            .type = { .storage = PIN_STRUCT } };
    }
    entries[0].name = "Record";
    entries[1].name = "Collision";
    entries[2].name = "Comp";
    entries[2].component = true;
    entries[3].name = "Native.Foo_Bar";
    entries[3].source_name = "Frob";
    entries[3].key = "friendly-key";
    entries[4].name = "Other.FooBar";
    entries[5].name = "FooBar";
    entries[6].name = "Hidden";
    entries[6].internal = true;
    entries[7].name = "Collision";
    own[0] = entries[0];
    own[1] = entries[1];
    for (int i = 0; i < 6; i++) {
        natives[i] = entries[i + 2];
    }
    merged_catalog = (script_catalog){ .types = entries,
                                       .type_count = 8,
                                       .entity_type = UINT64_MAX - 22 };
    native_catalog = (script_catalog){ .types = natives, .type_count = 6 };
    symbols[0] = (script_symbol){ .name = "int",
                                  .kind = SCRIPT_TYPE,
                                  .scope = 1,
                                  .type = { .storage = PIN_I64 } };
    symbols[1] = (script_symbol){ .name = "T",
                                  .kind = SCRIPT_TYPE_PARAMETER,
                                  .scope = 0,
                                  .type = { .storage = PIN_FLOW,
                                            .flags = SCRIPT_TYPE_GENERIC,
                                            .nominal = UINT64_MAX - 18,
                                            .parameter = UINT64_MAX - 19 } };
    symbols[2] = (script_symbol){ .name = "Record",
                                  .kind = SCRIPT_TYPE,
                                  .scope = 0,
                                  .id = entries[0].id,
                                  .type = { .storage = PIN_STRUCT,
                                            .nominal = entries[0].id } };
    symbols[3] = (script_symbol){ .name = "dep",
                                  .kind = SCRIPT_LOCAL,
                                  .scope = 2,
                                  .type = { .storage = PIN_BOOL } };
    functions[0] = (script_function){ .scope = 1, .symbol = -1 };
    documents[0] = (script_source_document){ .key = "Main.chibi" };
    documents[1] = (script_source_document){ .key = "Other.chibi" };
    exports[0] = (script_package_export){ .name = "Remote",
                                          .kind = SCRIPT_PACKAGE_TYPE,
                                          .id = entries[3].id };
    export_view = (script_package_exports){ .identity = "vendor:remote",
                                            .members = exports,
                                            .member_count = 1 };
    imports[0] = (script_package_import){ .alias = "dep",
                                          .identity = "vendor:remote",
                                          .document = 0,
                                          .exports = &export_view };
    imports[1] = (script_package_import){ .alias = "self",
                                          .identity = "project:binding",
                                          .document = 0 };
    imports[2] = (script_package_import){ .alias = "_",
                                          .identity = "vendor:remote",
                                          .document = 0,
                                          .exports = &export_view };
    uses[0] = (script_use){ .range = { 10, 17, 2, 3, 0 },
                            .kind = SCRIPT_TYPE_USE,
                            .symbol = -1,
                            .member = entries[0].id };
    uses[1] = (script_use){ .range = { 10, 17, 2, 3, 0 },
                            .kind = SCRIPT_TYPE_USE,
                            .symbol = 1 };
    return (script_named_facts){ .lexical = { .scopes = scopes,
                                              .symbols = symbols,
                                              .functions = functions,
                                              .documents = documents,
                                              .symbol_count = 4,
                                              .identity = "project:binding" },
                                 .catalog = &merged_catalog,
                                 .native = &native_catalog,
                                 .own_types = own,
                                 .own_type_count = 2,
                                 .imports = imports,
                                 .import_count = 3,
                                 .document_count = 2,
                                 .package_scope = 0,
                                 .uses = uses };
}
static bool raw_query(int index)
{
    script_named_facts facts = raw_facts();
    query q = { .operation = PARSED,
                .text = "int",
                .request = { .scope = 0, .range = { 10, 17, 2, 3, 0 } } };
    switch (index) {
        case 0: break;
        case 1: q.operation = TEXT; break;
        case 2: q.request.scope = 1; break;
        case 3:
            q.operation = TEXT;
            q.request.scope = 1;
            break;
        case 4: q.text = "T"; break;
        case 5:
            q.operation = TEXT;
            q.text = "\"Record\\u0000Tail\"";
            break;
        case 6: q.text = "entity"; break;
        case 7: q.text = long_name; break;
        case 8:
            q.operation = FIND;
            q.text = "foo bar";
            q.check_find = true;
            q.expected_find = -2;
            break;
        case 9:
            q.operation = FIND;
            q.text = "FooBar";
            q.check_find = true;
            q.expected_find = 5;
            break;
        case 10:
            q.operation = FIND;
            q.text = "frob";
            q.check_find = true;
            q.expected_find = 3;
            break;
        case 11:
            q.operation = FIND;
            q.text = "friendly_key";
            q.check_find = true;
            q.expected_find = 3;
            break;
        case 12:
            q.operation = FIND;
            q.text = "Hidden";
            q.check_find = true;
            q.expected_find = -1;
            break;
        case 13:
            q.text = "Comp";
            q.tokens[0] = "?";
            q.token_count = 1;
            break;
        case 14:
            q.text = "Record";
            q.tokens[0] = "?";
            q.token_count = 1;
            break;
        case 15:
            q.tokens[0] = "ref";
            q.tokens[1] = "?";
            q.tokens[2] = "[]";
            q.token_count = 3;
            break;
        case 16:
            q.operation = TEXT;
            q.text = " ref Comp [] ? ";
            break;
        case 17:
            q.operation = TEXT;
            q.text = "\"Native.Foo_Bar\"";
            break;
        case 18:
            entries[3].type.container = PIN_CONTAINER_ARRAY;
            entries[3].type.collection = UINT64_MAX - 43;
            q.text = "dep.Remote";
            break;
        case 19: q.text = "_.Remote"; break;
        case 20:
            q.text = "dep.Remote";
            q.request.range.document = 1;
            break;
        case 21:
            q.text = "dep.Remote";
            q.request.scope = 2;
            break;
        case 22: q.text = "self.Record"; break;
        case 23: q.text = "Collision"; break;
        case 24:
            q.text = "Record";
            facts.use_count = 1;
            break;
        case 25:
            symbols[1].type.container = PIN_CONTAINER_ARRAY;
            symbols[1].type.collection = UINT64_MAX - 41;
            q.text = "T";
            facts.use_count = 2;
            break;
        case 26:
            q.operation = TEXT;
            q.text = "\"";
            break;
        case 27: q.text = "task"; break;
    }
    return compare(&facts, 3, 1, q, index == 7 ? 0 : index == 4 ? 1 : -1);
}
TEST_CASE(named_binding_raw_facts_match)
{
    source_package_input files[] = {
        { "named_binding/Binding.chibi",
          NAMED_BINDING_CHIBI_SOURCE,
          sizeof NAMED_BINDING_CHIBI_SOURCE - 1 },
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
    for (int i = 0; i < 4; i++) {
        REQUIRE(files[i].size < 32768);
    }
    const native_definition_pack packs[] = { COMPILER_JSON_DEFINITION_PACK };
    REQUIRE(compiler_fixture_prepare_packs(&fixture, files, 4, packs, 1));
    for (int i = 0; i < RAW_REQUESTS; i++) {
        REQUIRE(raw_query(i));
    }
    return true;
}
static TSNode annotation(TSNode node, int depth)
{
    if (depth > 16) {
        return (TSNode){ 0 };
    }
    if (!strcmp(ts_node_type(node), "parameter")) {
        return ts_node_child_by_field_name(node, "type", 4);
    }
    for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
        TSNode found = annotation(ts_node_child(node, i), depth + 1);
        if (!ts_node_is_null(found)) {
            return found;
        }
    }
    return (TSNode){ 0 };
}
static bool spelling(TSNode node, char *out, size_t capacity)
{
    REQUIRE(!ts_node_is_null(node));
    script_range r = script_node_range(node);
    REQUIRE(r.start >= 0 && r.end >= r.start && (size_t)(r.end - r.start) < capacity);
    memcpy(out, syntax->text + r.start, (size_t)(r.end - r.start));
    out[r.end - r.start] = 0;
    return true;
}
TEST_CASE(named_binding_analyzer_annotations_match)
{
    const char *sources[] = {
        "Facts :: library { Run :: (value: int) -> int { return value; } }",
        "Facts :: library { Run :: (value: int[]) -> int[] { return value; "
        "} }",
        "Facts :: library { Thing :: struct { value: int; }; Run :: "
        "(value: Thing?) -> Thing? { return value; } }",
        "Facts :: library { Run :: (value: ref int) -> int { return value; "
        "} }",
    };
    const script_catalog native = { 0 };
    for (int c = 0; c < 4; c++) {
        REQUIRE(strlen(sources[c]) < 256);
        script_analysis_free(analysis);
        script_syntax_free(syntax);
        analysis = NULL;
        syntax = script_parse(sources[c]);
        REQUIRE(syntax && !syntax->diagnostic_count);
        analysis = script_analyze(syntax, &native);
        REQUIRE(analysis && analysis->valid);
        TSNode root = ts_tree_root_node(syntax->tree);
        REQUIRE(ts_node_descendant_count(root) <= 128);
        TSNode type = annotation(root, 0);
        REQUIRE(!ts_node_is_null(type));
        TSNode name = ts_node_child_by_field_name(type, "name", 4);
        REQUIRE(!ts_node_is_null(name));
        int parameter = -1;
        for (int i = 0; i < analysis->symbol_count; i++) {
            if (analysis->symbols[i].kind == SCRIPT_PARAMETER &&
                !strcmp(analysis->symbols[i].name, "value")) {
                parameter = i;
                break;
            }
        }
        REQUIRE(parameter >= 0);
        script_catalog catalog = { .types = analysis->types,
                                   .type_count = analysis->type_count };
        script_named_facts facts = { .lexical = { .scopes = analysis->scopes,
                                                  .symbols = analysis->symbols,
                                                  .functions = analysis->functions,
                                                  .documents = analysis->documents,
                                                  .symbol_count = analysis->symbol_count,
                                                  .identity = analysis->identity },
                                     .catalog = &catalog,
                                     .own_types = analysis->types,
                                     .own_type_count = analysis->type_count,
                                     .package_scope = analysis->package_scope,
                                     .uses = analysis->uses,
                                     .use_count = analysis->use_count };
        query q = { .operation = PARSED,
                    .request = { .scope = analysis->symbols[parameter].scope,
                                 .range = script_node_range(name) } };
        char decoded[64], full[256], tokens[8][33];
        REQUIRE(spelling(name, decoded, sizeof decoded) && spelling(type, full, sizeof full));
        q.text = decoded;
        REQUIRE(ts_node_child_count(type) <= 8);
        q.token_count = (int)ts_node_child_count(type);
        for (int i = 0; i < q.token_count; i++) {
            REQUIRE(spelling(ts_node_child(type, (uint32_t)i), tokens[i], sizeof tokens[i]));
            q.tokens[i] = tokens[i];
        }
        REQUIRE(compare(&facts, analysis->scope_count, analysis->function_count, q, -1));
        q.operation = TEXT;
        q.text = full;
        q.request.range = script_node_range(type);
        q.token_count = 0;
        REQUIRE(compare(&facts, analysis->scope_count, analysis->function_count, q, -1));
    }
    return true;
}
TEST_CASE(named_binding_results_outlive_owners)
{
    compiler_fixture_free(&fixture);
    cleanup();
    REQUIRE(saved[0].has_diagnostic && strlen(saved[0].diagnostic.message) == 191);
    static const char prefix[] = "Unknown type '";
    REQUIRE(!strncmp(saved[0].diagnostic.message, prefix, sizeof prefix - 1));
    for (int i = sizeof prefix - 1; i < 191; i++) {
        REQUIRE(saved[0].diagnostic.message[i] == 'n');
    }
    REQUIRE(
        saved[1].type.nominal == UINT64_MAX - 18 &&
        saved[1].type.parameter == UINT64_MAX - 19 && saved[1].has_use &&
        saved[1].use.symbol == 1 && saved[1].use.member == 0
    );
    for (int b = 0; b < 2; b++) {
        for (int i = 0; i < 2; i++) {
            REQUIRE(
                owned(&retained[b][i], &kept[b][i], 0) &&
                resolution(&kept[b][i], &saved[i])
            );
        }
    }
    return true;
}
void test_language_named_binding_chibi()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(named_binding_raw_facts_match);
    RUN_TEST_CASE(named_binding_analyzer_annotations_match);
    RUN_TEST_CASE(named_binding_results_outlive_owners);
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
