#include "callable_headers_chibi.h"

#include "callable_headers_chibi_source.h"
#include "compiler_fixture.h"
#include "language/callable_headers.h"
#include "lexical_binding_chibi_source.h"
#include "named_binding_chibi_source.h"
#include "package_core_source.h"
#include "signature_fixture.h"
#include "signature_reader_chibi_source.h"
#include "syntax_chibi_source.h"
#include "third_party/pico_headers/pico_unit.h"
#include "toolchain/compiler_json.h"
#include "type_constructors_chibi_source.h"
#include "types/array.h"
#include "types/text.h"
#include "types/union.h"
#include "unit_algebra_chibi_source.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Fifteen raw-tree requests, thirty Register calls, two million instructions
 * each. Eight dependency sources/backend plus fifteen fact parses:31 parses.
 * Header source<64KiB; each dependency<32KiB.
 * Sources<=512bytes, trees<=256nodes/depth32, one input scope/symbol and no
 * input functions/catalogs/uses. Output<=10symbols, two total scopes,
 * three functions, two initializers, one body, four uses/diagnostics, no
 * publications; pools<=64types/128IDs. Four quoted-name codec calls. No bodies
 * or defaults execute; no IO, tasks, workers or timing loops. */
enum
{
    CASES = 15,
    NODES = 256,
    SYMBOLS = 10,
    FUNCTIONS = 3
};
static const char PACKAGE[] = "project:callable_headers";
static const char *const sources[CASES] = {
    "Facts :: library { Work :: (value:int,scale:f64)->f64 { return "
    "scale; } }",
    "Facts :: library { Work :: (value:U)->int { return 1; } }",
    "Facts :: library { Work :: (T:T,value:T)->int { return 1; } }",
    "Facts :: library { Work :: (value:int)->int { return value; } }",
    "package facts\nWork :: (value:int)->int { return value; }",
    "package facts\nWork :: (value:int)->int { return value; }",
    "Facts :: library { Work :: ()->int { return 1; } }",
    "Facts :: library { Work :: (value:int)->int { return 1; } }",
    "Facts :: library { Work :: () { return 1; } }",
    "Facts :: library { Work :: () { Inner :: () { return 1; } } }",
    "Facts :: library { macro Work(value:int) { entry Begin {} entry Other {} "
    "exit Done; exit Fail; return 1; } }",
    "Facts :: library { Work :: (value:int=3)->(value:int=4) { return; } }",
    "Facts :: library { Work :: (value) {} }",
    "package facts\nWork :: (value:int)->int { return value; }",
    "Facts :: library { Work :: (speed:f64 u/s)->f64 u/s { return speed; "
    "} }",
};
static compiler_fixture fixture;
static arena input, detached, oracle, retained[2];
static pin_value kept[2];
static script_syntax *syntax;
static compiler_fixture_node nodes[NODES];
static int node_count, calls, retained_parameter;
static uint64_t retained_id;

static void cleanup()
{
    arena_free(&input);
    arena_free(&detached);
    arena_free(&oracle);
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
static int bits(unsigned n)
{
    int value;
    _Static_assert(sizeof value == sizeof n, "mask width");
    memcpy(&value, &n, sizeof value);
    return value;
}
static pin_value text(const char *s)
{
    s = s ? s : "";
    pin_value result;
    const char *error = pin_value_text(&input, &result, s, strlen(s));
    if (error) {
        abort();
    }
    return result;
}
static bool read(const pin_value *row, const char *name, pin_value *out)
{
    REQUIRE(row->type == PIN_STRUCT && row->ptr);
    const type_field *field = pin_type_field_path(pin_value_type(row), name);
    REQUIRE(field);
    REQUIRE(!pin_read_field(&input, row->ptr, field, out));
    return true;
}
static bool put(pin_value *row, const char *name, pin_value value)
{
    const type_field *field = pin_type_field_path(pin_value_type(row), name);
    REQUIRE(field && field->type == value.type);
    REQUIRE(!pin_write_field(&input, row->ptr, field, value));
    return true;
}
static bool int_field(const pin_value *row, const char *name, int expected)
{
    pin_value value;
    REQUIRE(read(row, name, &value) && value.type == PIN_I32 && value.i == expected);
    return true;
}
static bool bool_field(const pin_value *row, const char *name, bool expected)
{
    pin_value value;
    REQUIRE(read(row, name, &value) && value.type == PIN_BOOL && value.b == expected);
    return true;
}
static bool string_field(const pin_value *row, const char *name, const char *expected)
{
    pin_value value;
    REQUIRE(
        read(row, name, &value) && value.type == PIN_STR && pin_text(value).bytes &&
        !strcmp(pin_text(value).bytes, expected ? expected : "")
    );
    return true;
}
static bool range_fields(const pin_value *row, script_range r)
{
    REQUIRE(
        int_field(row, "start", r.start) && int_field(row, "end", r.end) &&
        int_field(row, "line", r.line) && int_field(row, "column", r.column) &&
        int_field(row, "document", r.document)
    );
    return true;
}
static bool set_range(pin_value *row, script_range r)
{
    REQUIRE(
        put(row, "start", integer(r.start)) && put(row, "end", integer(r.end)) &&
        put(row, "line", integer(r.line)) && put(row, "column", integer(r.column)) &&
        put(row, "document", integer(r.document))
    );
    return true;
}
static bool union_data(const pin_value *value, int count, const pin_union **out)
{
    REQUIRE(value->type == PIN_UNION && value->ptr);
    *out = value->ptr;
    REQUIRE((*out)->count == count);
    return true;
}
static bool list(const pin_value *value, pin_type type, int count, const pin_array **out)
{
    REQUIRE(value->type == type && value->container == PIN_CONTAINER_ARRAY);
    *out = value->ptr;
    int actual = *out ? (*out)->count : 0;
    REQUIRE(actual >= 0 && actual <= 128 && (count < 0 || actual == count));
    return true;
}
static bool field_number(const pin_value *row, const char *name, int *out)
{
    pin_value value;
    REQUIRE(read(row, name, &value) && value.type == PIN_I32);
    *out = value.i;
    return true;
}
static bool id_at(const pin_value *pool, int index, uint64_t *out)
{
    REQUIRE(index >= -1);
    if (index < 0) {
        *out = 0;
        return true;
    }
    const pin_array *array;
    REQUIRE(
        list(pool, PIN_U64, -1, &array) && array && index < array->count &&
        array->items[index].type == PIN_U64
    );
    *out = array->items[index].u64;
    return true;
}
static bool id_field(const pin_value *row, const char *name, const pin_value *ids, uint64_t expected)
{
    int index;
    uint64_t actual;
    REQUIRE(field_number(row, name, &index) && id_at(ids, index, &actual) && actual == expected);
    return true;
}
static bool type_at(const pin_value *types, const pin_value *ids, int index, script_type expected)
{
    const pin_array *array;
    REQUIRE(
        list(types, PIN_STRUCT, -1, &array) && array && index >= 0 &&
        index < array->count
    );
    const pin_value *row = &array->items[index];
    REQUIRE(
        int_field(row, "storage", (int)expected.storage) &&
        int_field(row, "container", (int)expected.container) &&
        int_field(row, "flags", bits(expected.flags)) &&
        id_field(row, "nominal_slot", ids, expected.nominal) &&
        id_field(row, "parameter_slot", ids, expected.parameter)
    );
    return true;
}
static bool rows(int b, const char *name, int count, pin_value *out)
{
    const pin_record *shape = compiler_fixture_record(&fixture, b, PACKAGE, name, &input);
    REQUIRE(shape && count >= 0 && count <= SYMBOLS);
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
        array->items[i] = (pin_value){ .type = PIN_STRUCT,
                                       .record = shape,
                                       .type_name = shape->type.name,
                                       .ptr = arena_alloc(&input, shape->type.size) };
        memset(array->items[i].ptr, 0, shape->type.size);
    }
    return true;
}
static const script_catalog_variant *variant(int b, const char *name, const char *member)
{
    const authoring_package *package = compiler_fixture_package(&fixture, b, PACKAGE);
    if (!package || !package->exports || !package->exports->metadata) {
        return NULL;
    }
    const script_catalog_type *type = script_catalog_type_by_id(
        package->exports->metadata,
        compiler_fixture_export(&fixture, b, PACKAGE, name)
    );
    for (int i = 0; type && i < type->variant_count; i++) {
        if (!strcmp(type->variants[i].name, member)) {
            return &type->variants[i];
        }
    }
    return NULL;
}
static bool tagged(int b, const char *name, const char *member, pin_value *fields, int count, pin_value *out)
{
    const script_catalog_variant *tag = variant(b, name, member);
    REQUIRE(tag && tag->field_count == count);
    REQUIRE(!compiler_fixture_union(&input, tag->tag, fields, count, out));
    return true;
}
static pin_value empty()
{
    return (pin_value){ .type = PIN_I32,
                        .container = PIN_CONTAINER_ARRAY,
                        .ptr = pin_array_snapshot_allocate(&input, PIN_I32, NULL, 0) };
}
static bool parts(const pin_value *reader, const pin_union **names, const pin_union **lexical)
{
    const pin_union *r;
    REQUIRE(
        union_data(reader, 3, &r) && union_data(&r->values[0], 15, names) &&
        union_data(&(*names)->values[0], 5, lexical)
    );
    return true;
}
static bool input_environment(int b, const script_callable_facts *facts, pin_value *out)
{
    pin_value fields[6];
    REQUIRE(
        signature_fixture_environment(&fixture, b, &facts->signature, facts->scope_count, &input, &fields[0])
    );
    const pin_union *names, *lexical;
    REQUIRE(parts(&fields[0], &names, &lexical));
    int count = facts->signature.named.lexical.symbol_count;
    REQUIRE(rows(b, "Symbol", count, &fields[1]) && rows(b, "Function", 0, &fields[2]));
    pin_array *symbols = fields[1].ptr;
    for (int i = 0; i < count; i++) {
        const script_symbol *s = &facts->signature.named.lexical.symbols[i];
        pin_value *row = &symbols->items[i];
        REQUIRE(type_at(&names->values[1], &names->values[2], 0, s->type));
        REQUIRE(
            put(row, "name", text(s->name)) && put(row, "unit", text(s->unit)) &&
            put(row, "has_unit", boolean(s->unit != NULL)) &&
            put(row, "kind", integer(s->kind)) &&
            put(row, "scope", integer(s->scope)) &&
            put(row, "initializer", integer(s->initializer)) &&
            put(row, "function", integer(s->function)) &&
            put(row, "writable", boolean(s->writable)) &&
            put(row, "outcome", boolean(s->outcome)) &&
            put(row, "type_index", integer(0)) && put(row, "id_slot", integer(i)) &&
            put(row, "capability_slot", integer(-1)) && set_range(row, s->declaration)
        );
    }
    fields[3] = empty();
    fields[4] = empty();
    fields[5] = empty();
    REQUIRE(tagged(b, "Environment", "Tables", fields, 6, out));
    return true;
}
static bool owned(const arena *storage, const pin_value *value, int depth)
{
    REQUIRE(depth <= 10);
    if (value->container == PIN_CONTAINER_ARRAY) {
        if (!value->ptr) {
            return true;
        }
        const pin_array *a = value->ptr;
        REQUIRE(arena_contains(storage, a) && a->count >= 0 && a->count <= 128);
        REQUIRE(!a->count || arena_contains(storage, a->items));
        for (int i = 0; i < a->count; i++) {
            REQUIRE(owned(storage, &a->items[i], depth + 1));
        }
        return true;
    }
    REQUIRE(value->container == PIN_CONTAINER_SINGLE);
    if (value->type == PIN_UNION) {
        const pin_union *u = value->ptr;
        REQUIRE(u && arena_contains(storage, u) && u->count >= 0 && u->count <= 15);
        REQUIRE(!u->count || arena_contains(storage, u->values));
        for (int i = 0; i < u->count; i++) {
            REQUIRE(owned(storage, &u->values[i], depth + 1));
        }
        return true;
    }
    if (value->type == PIN_STRUCT) {
        REQUIRE(
            value->ptr && value->record && arena_contains(storage, value->ptr) &&
            arena_contains(storage, value->record)
        );
        const type_desc *t = pin_value_type(value);
        REQUIRE(t && t->field_count <= 22);
        for (int i = 0; i < t->field_count; i++) {
            pin_value field;
            REQUIRE(!pin_read_field(&input, value->ptr, &t->fields[i], &field));
            REQUIRE(owned(storage, &field, depth + 1));
        }
        return true;
    }
    if (value->type == PIN_STR) {
        REQUIRE(pin_text(*value).bytes && arena_contains(storage, value->text));
        return true;
    }
    REQUIRE(value->type == PIN_I32 || value->type == PIN_BOOL || value->type == PIN_U64);
    return true;
}
static const script_symbol *symbol_at(
    const script_callable_facts *facts,
    const script_callable_header *header,
    int index
)
{
    for (int i = 0; i < header->symbol_count; i++) {
        if (header->symbols[i].index == index) {
            return &header->symbols[i].symbol;
        }
    }
    return index < facts->signature.named.lexical.symbol_count
             ? &facts->signature.named.lexical.symbols[index]
             : NULL;
}
static bool symbol_row(
    const pin_value *row,
    const pin_union *names,
    const pin_union *lexical,
    const script_symbol *s
)
{
    REQUIRE(s);
    int type;
    REQUIRE(
        string_field(row, "name", s->name) && string_field(row, "unit", s->unit) &&
        bool_field(row, "has_unit", s->unit != NULL) &&
        int_field(row, "kind", (int)s->kind) && int_field(row, "scope", s->scope) &&
        int_field(row, "initializer", s->initializer) &&
        int_field(row, "function", s->function) &&
        bool_field(row, "writable", s->writable) &&
        bool_field(row, "outcome", s->outcome) && range_fields(row, s->declaration)
    );
    REQUIRE(
        field_number(row, "type_index", &type) &&
        type_at(&names->values[1], &names->values[2], type, s->type) &&
        id_field(row, "id_slot", &lexical->values[3], s->id) &&
        id_field(row, "capability_slot", &lexical->values[3], s->capability)
    );
    return true;
}
static bool members(
    const pin_value *row,
    const char *prefix,
    const pin_value *values,
    const int *expected,
    int count
)
{
    char start[32], number[32];
    snprintf(start, sizeof start, "%s_start", prefix);
    snprintf(number, sizeof number, "%s_count", prefix);
    int at;
    const pin_array *array;
    REQUIRE(
        field_number(row, start, &at) && int_field(row, number, count) &&
        list(values, PIN_I32, -1, &array) && at >= 0 &&
        at + count <= (array ? array->count : 0)
    );
    for (int i = 0; i < count; i++) {
        REQUIRE(array->items[at + i].type == PIN_I32 && array->items[at + i].i == expected[i]);
    }
    return true;
}
static bool function_row(const pin_value *row, const pin_union *environment, const script_function *f)
{
    REQUIRE(
        int_field(row, "kind", (int)f->kind) && int_field(row, "symbol", f->symbol) &&
        int_field(row, "scope", f->scope) && int_field(row, "body", f->body) &&
        int_field(row, "receiver", f->receiver) && int_field(row, "parent", f->parent) &&
        int_field(row, "depth_limit", f->depth_limit) &&
        bool_field(row, "pure", f->pure) && bool_field(row, "inferred", f->inferred) &&
        string_field(row, "entry_name", f->entry) &&
        bool_field(row, "has_entry", f->entry != NULL) && range_fields(row, f->range)
    );
    REQUIRE(
        members(row, "parameter", &environment->values[3], f->parameters, f->parameter_count) &&
        members(row, "result", &environment->values[4], f->results, f->result_count) &&
        members(row, "generic", &environment->values[5], NULL, 0)
    );
    return true;
}
static bool uses(const pin_value *value, const pin_value *ids, const script_callable_header *header)
{
    const pin_array *array;
    REQUIRE(list(value, PIN_STRUCT, header->use_count, &array));
    for (int i = 0; i < header->use_count; i++) {
        const script_use *use = &header->uses[i];
        const pin_value *row = &array->items[i];
        REQUIRE(
            int_field(row, "kind", (int)use->kind) &&
            int_field(row, "symbol", use->symbol) &&
            id_field(row, "member_slot", ids, use->member) &&
            range_fields(row, use->range)
        );
    }
    return true;
}
static int node_index(TSNode node)
{
    for (int i = 0; i < node_count; i++) {
        if (ts_node_eq(nodes[i].node, node)) {
            return i;
        }
    }
    return -1;
}
static bool references(const pin_value *value, const script_callable_header *header, bool initializer)
{
    int count = initializer ? header->initializer_count : header->body_count;
    const pin_array *array;
    REQUIRE(list(value, PIN_STRUCT, count, &array));
    for (int i = 0; i < count; i++) {
        int index = initializer ? header->initializers[i].symbol
                                : header->bodies[i].function,
            document = initializer ? header->initializers[i].document
                                   : header->bodies[i].document;
        TSNode node = initializer ? header->initializers[i].node
                                  : header->bodies[i].node;
        int expected = node_index(node);
        REQUIRE(
            expected >= 0 && int_field(&array->items[i], "index", index) &&
            int_field(&array->items[i], "document", document) &&
            int_field(&array->items[i], "node", expected)
        );
    }
    return true;
}
static bool compare(const pin_value *value, const script_callable_facts *facts, const script_callable_header *header)
{
    const pin_union *result, *environment, *names, *lexical, *reader, *constructors;
    REQUIRE(union_data(value, 11, &result));
    REQUIRE(
        result->values[0].type == PIN_I32 && result->values[0].i == header->symbol &&
        result->values[1].type == PIN_I32 && result->values[1].i == header->function
    );
    REQUIRE(
        union_data(&result->values[2], 6, &environment) &&
        parts(&environment->values[0], &names, &lexical) &&
        union_data(&environment->values[0], 3, &reader) &&
        union_data(&reader->values[1], 8, &constructors)
    );
    int symbol_count = facts->signature.named.lexical.symbol_count,
        function_count = 0;
    for (int i = 0; i < header->symbol_count; i++) {
        symbol_count += (header->symbols[i].fields & SCRIPT_CALLABLE_APPEND) != 0;
    }
    for (int i = 0; i < header->function_count; i++) {
        function_count += header->functions[i].append;
    }
    REQUIRE(
        symbol_count <= SYMBOLS && function_count <= FUNCTIONS &&
        header->scope_count <= 1 && header->initializer_count <= 2 &&
        header->body_count <= 1 && header->diagnostic_count <= 4 &&
        header->use_count <= 4 && !header->type_count
    );
    const pin_array *full, *light, *types;
    REQUIRE(
        list(&environment->values[1], PIN_STRUCT, symbol_count, &full) &&
        list(&lexical->values[1], PIN_STRUCT, symbol_count, &light) &&
        list(&names->values[6], PIN_I32, symbol_count, &types)
    );
    for (int i = 0; i < symbol_count; i++) {
        const script_symbol *s = symbol_at(facts, header, i);
        REQUIRE(s && symbol_row(&full->items[i], names, lexical, s));
        const pin_value *r = &light->items[i];
        REQUIRE(
            string_field(r, "name", s->name) && int_field(r, "kind", (int)s->kind) &&
            int_field(r, "scope", s->scope) && range_fields(r, s->declaration) &&
            id_field(r, "id_slot", &lexical->values[3], s->id)
        );
        REQUIRE(
            types->items[i].type == PIN_I32 &&
            type_at(&names->values[1], &names->values[2], types->items[i].i, s->type)
        );
    }
    const pin_array *functions, *light_functions;
    REQUIRE(
        list(&environment->values[2], PIN_STRUCT, function_count, &functions) &&
        list(&lexical->values[2], PIN_STRUCT, function_count, &light_functions)
    );
    int parameters = 0, results = 0, generics = 0;
    for (int i = 0; i < header->function_count; i++) {
        const script_callable_function_change *change = &header->functions[i];
        REQUIRE(change->append && change->index == i);
        const script_function *f = &change->function;
        REQUIRE(
            function_row(&functions->items[i], environment, f) &&
            int_field(&light_functions->items[i], "symbol", f->symbol) &&
            int_field(&light_functions->items[i], "scope", f->scope)
        );
        parameters += f->parameter_count;
        results += f->result_count;
    }
    const pin_array *ignored;
    const int totals[] = { parameters, results, generics };
    for (int i = 0; i < 3; i++) {
        REQUIRE(list(&environment->values[i + 3], PIN_I32, -1, &ignored));
        /* Banks may retain earlier immutable spans. Current function spans
         * above, rather than unused historical cells, carry membership. */
        REQUIRE((ignored ? ignored->count : 0) >= totals[i]);
        REQUIRE((ignored ? ignored->count : 0) <= 16);
    }
    const pin_array *scope_rows;
    REQUIRE(
        list(&lexical->values[0], PIN_STRUCT, facts->scope_count + header->scope_count, &scope_rows)
    );
    for (int i = 0; i < facts->scope_count + header->scope_count; i++) {
        const script_scope *s = i < facts->scope_count
                                  ? &facts->signature.named.lexical.scopes[i]
                                  : &header->scopes[i - facts->scope_count];
        REQUIRE(
            int_field(&scope_rows->items[i], "parent", s->parent) &&
            int_field(&scope_rows->items[i], "function", s->function) &&
            range_fields(&scope_rows->items[i], s->range)
        );
    }
    const pin_array *changes, *function_changes, *diagnostics;
    REQUIRE(
        list(&result->values[3], PIN_STRUCT, header->symbol_count, &changes) &&
        list(&result->values[4], PIN_STRUCT, header->function_count, &function_changes)
    );
    for (int i = 0; i < header->symbol_count; i++) {
        REQUIRE(
            int_field(&changes->items[i], "index", header->symbols[i].index) &&
            int_field(&changes->items[i], "fields", bits(header->symbols[i].fields))
        );
    }
    for (int i = 0; i < header->function_count; i++) {
        REQUIRE(
            int_field(&function_changes->items[i], "index", header->functions[i].index) &&
            bool_field(
                &function_changes->items[i],
                "append",
                header->functions[i].append
            )
        );
    }
    REQUIRE(
        result->values[5].type == PIN_I32 && result->values[5].i == facts->scope_count &&
        references(&result->values[6], header, true) &&
        references(&result->values[7], header, false) &&
        list(&result->values[8], PIN_STRUCT, header->diagnostic_count, &diagnostics)
    );
    for (int i = 0; i < header->diagnostic_count; i++) {
        REQUIRE(
            range_fields(&diagnostics->items[i], header->diagnostics[i].range) &&
            string_field(
                &diagnostics->items[i],
                "message",
                header->diagnostics[i].message
            )
        );
    }
    REQUIRE(
        uses(&result->values[9], &names->values[2], header) &&
        uses(&names->values[9], &names->values[2], header) &&
        list(&result->values[10], PIN_UNION, 0, &ignored)
    );
    for (int i = 3; i <= 5; i++) {
        REQUIRE(list(&names->values[i], PIN_STRUCT, 0, &ignored));
    }
    REQUIRE(
        list(&constructors->values[2], PIN_STRUCT, 0, &ignored) &&
        list(&constructors->values[3], PIN_STRUCT, 0, &ignored) &&
        list(&constructors->values[4], PIN_STRUCT, 0, &ignored)
    );
    const pin_array *ctor_symbols, *ctor_functions, *ctor_results;
    REQUIRE(
        list(&constructors->values[6], PIN_STRUCT, symbol_count, &ctor_symbols) &&
        list(&constructors->values[5], PIN_STRUCT, function_count, &ctor_functions) &&
        list(&constructors->values[7], PIN_I32, -1, &ctor_results)
    );
    for (int i = 0; i < symbol_count; i++) {
        REQUIRE(id_field(
            &ctor_symbols->items[i],
            "id_slot",
            &constructors->values[1],
            symbol_at(facts, header, i)->id
        ));
    }
    REQUIRE((ctor_results ? ctor_results->count : 0) <= 16);
    for (int i = 0; i < function_count; i++) {
        const script_function *f = &header->functions[i].function;
        const pin_value *row = &ctor_functions->items[i];
        REQUIRE(
            int_field(row, "symbol", f->symbol) &&
            members(row, "result", &constructors->values[7], f->results, f->result_count)
        );
    }
    return true;
}
static bool witness(int index, const script_callable_facts *facts, const script_callable_header *header)
{
    if (index == 4 || index == 13) {
        REQUIRE(
            header->function == -1 && !header->scope_count &&
            !header->function_count && header->diagnostic_count == 2
        );
        return true;
    }
    REQUIRE(header->function == 0 && header->scope_count == 1 && header->function_count >= 1);
    const script_function *f = &header->functions[0].function;
    if (index == 0) {
        REQUIRE(f->parameter_count == 2 && f->result_count == 1 && !header->diagnostic_count);
    }
    if (index == 1) {
        REQUIRE(
            header->diagnostic_count == 1 &&
            !strcmp(header->diagnostics[0].message, "Unknown type 'U'")
        );
    }
    if (index == 2) {
        REQUIRE(
            f->parameter_count == 2 && header->diagnostic_count == 1 &&
            !strcmp(header->diagnostics[0].message, "Unknown type 'T'")
        );
    }
    if (index == 3) {
        REQUIRE(
            header->symbol == 0 && symbol_at(facts, header, 0)->id == UINT64_MAX - 5 &&
            header->diagnostic_count == 1
        );
    }
    if (index == 5) {
        REQUIRE(header->symbol == 0 && header->diagnostic_count == 2);
    }
    if (index == 6) {
        const script_symbol *s = symbol_at(facts, header, f->results[0]);
        REQUIRE(
            !f->pure && f->result_count == 1 && s && !strcmp(s->name, "Result") &&
            s->declaration.start == -1 && s->declaration.end == -1
        );
    }
    if (index == 7) {
        int entries = 0;
        for (int i = 0; i < header->symbol_count; i++) {
            entries += !strcmp(header->symbols[i].symbol.name, "entry");
        }
        REQUIRE(entries == 0);
    }
    if (index == 8) {
        REQUIRE(f->inferred);
    }
    if (index == 9) {
        REQUIRE(!f->inferred && header->function_count == 1);
    }
    if (index == 10) {
        REQUIRE(!f->inferred && header->function_count == 3 && header->body_count == 1);
        int outcomes = 0;
        for (int i = 0; i < header->symbol_count; i++) {
            outcomes += header->symbols[i].symbol.outcome;
        }
        REQUIRE(outcomes == 2);
    }
    if (index == 11) {
        REQUIRE(
            header->initializer_count == 2 && f->parameter_count == 1 &&
            f->result_count == 1 && f->parameters[0] != f->results[0]
        );
    }
    if (index == 12) {
        REQUIRE(
            header->diagnostic_count == 1 &&
            !strcmp(header->diagnostics[0].message, "Callable parameters and results require a type")
        );
    }
    if (index == 14) {
        REQUIRE(f->parameter_count == 1 && f->result_count == 1);
        const script_symbol *p = symbol_at(facts, header, f->parameters[0]),
                            *r = symbol_at(facts, header, f->results[0]);
        REQUIRE(
            p && r && p->unit && r->unit && !strcmp(p->unit, "u/s") &&
            !strcmp(r->unit, "u/s")
        );
    }
    return true;
}
static bool run_case(int index)
{
    REQUIRE(
        fixture.owners[0] && fixture.owners[1] &&
        authoring_packages_valid(fixture.owners[0]) &&
        authoring_packages_valid(fixture.owners[1])
    );
    cleanup();
    REQUIRE(strlen(sources[index]) <= 512);
    syntax = script_parse(sources[index]);
    REQUIRE(
        syntax && !syntax->diagnostic_count &&
        compiler_fixture_nodes(ts_tree_root_node(syntax->tree), nodes, NODES, 32, &node_count)
    );
    int declaration = -1;
    for (int i = 0; i < node_count; i++) {
        const char *kind = ts_node_type(nodes[i].node);
        if (!strcmp(kind, "function_declaration") ||
            !strcmp(kind, "macro_declaration")) {
            declaration = i;
            break;
        }
    }
    REQUIRE(declaration >= 0);
    TSNode name = ts_node_child_by_field_name(nodes[declaration].node, "name", 4);
    REQUIRE(!ts_node_is_null(name));
    script_scope scope = { .parent = -1,
                           .function = -1,
                           .range = script_node_range(ts_tree_root_node(syntax->tree)) };
    script_source_document document = { .path = "/headers/Work.chibi",
                                        .key = "Work.chibi" };
    bool package = index == 4 || index == 5 || index == 13;
    script_symbol prior = { .id = UINT64_MAX - 5,
                            .name = index == 2 ? "T" : "Work",
                            .kind = index == 2  ? SCRIPT_TYPE_PARAMETER
                                  : index == 13 ? SCRIPT_TYPE
                                                : SCRIPT_FUNCTION,
                            .type = { .storage = index == 2 ? PIN_I32 : PIN_FLOW,
                                      .flags = index == 2 ? SCRIPT_TYPE_GENERIC : 0,
                                      .parameter = index == 2 ? UINT64_MAX - 5 : 0 },
                            .scope = 0,
                            .initializer = -1,
                            .function = -1,
                            .declaration = script_node_range(name) };
    if (index != 5) {
        prior.declaration.start--;
        prior.declaration.end--;
    }
    bool existing = index == 2 || index == 3 || index == 4 || index == 5 ||
                    index == 13;
    const script_catalog catalog = { 0 };
    script_callable_facts
        facts = { .signature = { .named = { .lexical = { .scopes = &scope,
                                                         .symbols = &prior,
                                                         .symbol_count = existing ? 1 : 0,
                                                         .documents = &document,
                                                         .identity = package ? "project:headers"
                                                                             : "legacy:headers" },
                                            .catalog = &catalog,
                                            .native = package ? &catalog : NULL,
                                            .document_count = package ? 1 : 0,
                                            .package_scope = package ? 0 : -1 },
                                 .pointer_size = sizeof(void *) },
                  .scope_count = 1 };
    script_callable_context context = { .scope = 0, .function = -1, .document = 0 };
    script_callable_header header = script_callable_register(
        &oracle,
        &facts,
        nodes[declaration].node,
        sources[index],
        context
    );
    REQUIRE(witness(index, &facts, &header));
    for (int b = 0; b < 2; b++) {
        arena_free(&input);
        arena_free(&detached);
        pin_value args[4], result;
        REQUIRE(
            input_environment(b, &facts, &args[0]) &&
            signature_fixture_tree(&fixture, b, nodes, node_count, sources[index], &input, &args[1])
        );
        args[2] = integer(declaration);
        pin_value context_rows;
        REQUIRE(rows(b, "Context", 1, &context_rows));
        args[3] = ((pin_array *)context_rows.ptr)->items[0];
        REQUIRE(
            put(&args[3], "scope", integer(0)) &&
            put(&args[3], "function", integer(-1)) &&
            put(&args[3], "document", integer(0))
        );
        REQUIRE(
            compiler_fixture_call(&fixture, b, PACKAGE, "Register", args, 4, 2000000, &detached, &result)
        );
        calls++;
        arena_free(&input);
        const script_catalog_variant *tag = variant(b, "Header", "Registered");
        REQUIRE(
            tag && result.type == PIN_UNION && result.ptr &&
            ((const pin_union *)result.ptr)->tag == tag->tag &&
            owned(&detached, &result, 0) && compare(&result, &facts, &header)
        );
        if (index == 14) {
            retained_parameter = header.functions[0].function.parameters[0];
            retained_id = symbol_at(&facts, &header, header.symbol)->id;
            REQUIRE(
                !compiler_fixture_snapshot(&retained[b], &kept[b], &result) &&
                owned(&retained[b], &kept[b], 0)
            );
        }
    }
    return true;
}
TEST_CASE(callable_headers_register_raw_signatures)
{
    const source_package_input files[] = {
        { "callable_headers/Headers.chibi",
          CALLABLE_HEADERS_CHIBI_SOURCE,
          sizeof CALLABLE_HEADERS_CHIBI_SOURCE - 1 },
        { "signature_reader/Reader.chibi",
          SIGNATURE_READER_CHIBI_SOURCE,
          sizeof SIGNATURE_READER_CHIBI_SOURCE - 1 },
        { "named_binding/Binding.chibi",
          NAMED_BINDING_CHIBI_SOURCE,
          sizeof NAMED_BINDING_CHIBI_SOURCE - 1 },
        { "type_constructors/Constructors.chibi",
          TYPE_CONSTRUCTORS_CHIBI_SOURCE,
          sizeof TYPE_CONSTRUCTORS_CHIBI_SOURCE - 1 },
        { "lexical_binding/Binding.chibi",
          LEXICAL_BINDING_CHIBI_SOURCE,
          sizeof LEXICAL_BINDING_CHIBI_SOURCE - 1 },
        { "unit_algebra/Units.chibi",
          UNIT_ALGEBRA_CHIBI_SOURCE,
          sizeof UNIT_ALGEBRA_CHIBI_SOURCE - 1 },
        { "syntax_diagnostics/Diagnostics.chibi",
          SYNTAX_CHIBI_SOURCE,
          sizeof SYNTAX_CHIBI_SOURCE - 1 },
        { "/sdk/core/strings/Strings.chibi",
          CORE_STRINGS_SOURCE,
          sizeof CORE_STRINGS_SOURCE - 1 },
    };
    REQUIRE(files[0].size < 65536);
    for (int i = 1; i < 8; i++) {
        REQUIRE(files[i].size < 48 * 1024);
    }
    REQUIRE(compiler_fixture_prepare_packs(&fixture, files, 8, &COMPILER_JSON_DEFINITION_PACK, 1));
    calls = 0;
    for (int i = 0; i < CASES; i++) {
        REQUIRE(run_case(i));
    }
    return true;
}
TEST_CASE(callable_headers_outputs_outlive_sources_and_modules)
{
    cleanup();
    compiler_fixture_free(&fixture);
    REQUIRE(calls == 30 && retained_id != 0);
    for (int b = 0; b < 2; b++) {
        REQUIRE(owned(&retained[b], &kept[b], 0));
        const pin_union *header, *environment, *names, *lexical;
        REQUIRE(
            union_data(&kept[b], 11, &header) &&
            union_data(&header->values[2], 6, &environment) &&
            parts(&environment->values[0], &names, &lexical)
        );
        const pin_array *symbols;
        REQUIRE(
            list(&environment->values[1], PIN_STRUCT, -1, &symbols) && symbols &&
            retained_parameter >= 0 && retained_parameter < symbols->count
        );
        REQUIRE(
            string_field(&symbols->items[retained_parameter], "name", "speed") &&
            string_field(&symbols->items[retained_parameter], "unit", "u/s") &&
            bool_field(&symbols->items[retained_parameter], "has_unit", true) &&
            id_field(&symbols->items[0], "id_slot", &lexical->values[3], retained_id)
        );
    }
    return true;
}
void test_language_callable_headers_chibi()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(callable_headers_register_raw_signatures);
    RUN_TEST_CASE(callable_headers_outputs_outlive_sources_and_modules);
    pu_clear_setup();
    cleanup();
    compiler_fixture_free(&fixture);
    for (int b = 0; b < 2; b++) {
        arena_free(&retained[b]);
        kept[b] = (pin_value){ 0 };
    }
}
