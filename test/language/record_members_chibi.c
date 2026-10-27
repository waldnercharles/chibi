#include "record_members_chibi.h"

#include "compiler_fixture.h"
#include "language/record_members.h"
#include "lexical_binding_chibi_source.h"
#include "named_binding_chibi_source.h"
#include "package_core_source.h"
#include "record_members_chibi_source.h"
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
#include <string.h>

/* Frozen proposal: 8 selection + 13 member + 3 refreshed-fact boundary +
 * 2 publication-continuation requests = 26, once on each backend = 52 calls,
 * each supplies budget 500,000, inherited/reset per activation; no total
 * instruction ceiling is implied. Eight compiler inputs/backend =16 parses;
 * 16 fixed fact-source parses give32 total. Sources <=256 bytes/128 nodes,
 * depth32; one scope/symbol, <=3 definitions, two variants, three prior names,
 * <=4 diagnostics/1 use/1 publication per member. Four runtime JSON calls
 * decode the two quoted names on both backends. No default is analyzed,
 * evaluated or represented by a host answer. No runtime IO/processes/GPU. */
enum
{
    SOURCE_CAP = 256,
    NODE_CAP = 128,
    CALL_BUDGET = 500000
};
#define COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))
#define ROW_ID UINT64_C(0xfedcba9876543210)
#define PEER_ID UINT64_C(0xfedcba9876543211)
#define AFTER_ID UINT64_C(0x8000000000000001)
static const char PACKAGE[] = "project:signature_reader",
                  NAMED[] = "project:named_binding",
                  CONSTRUCTORS[] = "project:type_constructors",
                  MEMBERS[] = "project:record_members";
static compiler_fixture fixture;
static arena input, detached, oracle, before_storage, retained[2], chains[2];
static pin_value kept[2], chain_environment[2];
static script_syntax *syntax;
static compiler_fixture_node nodes[NODE_CAP];
static char source[SOURCE_CAP];
static int node_count, calls, parses;
static bool prepared, has_kept[2];
static script_scope scope;
static script_symbol symbol;
static script_catalog_type definitions[3];
static script_catalog catalog;
static script_signature_facts facts;

static void cleanup()
{
    arena_free(&input);
    arena_free(&detached);
    arena_free(&oracle);
    arena_free(&before_storage);
    script_syntax_free(syntax);
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
    pin_value v;
    REQUIRE(read(row, name, &v) && v.type == PIN_I32 && v.i == expected);
    return true;
}
static bool same_bool(const pin_value *row, const char *name, bool expected)
{
    pin_value v;
    REQUIRE(read(row, name, &v) && v.type == PIN_BOOL && v.b == expected);
    return true;
}
static bool same_text(const pin_value *row, const char *name, const char *expected)
{
    pin_value v;
    REQUIRE(
        read(row, name, &v) && v.type == PIN_STR && pin_text(v).bytes &&
        !strcmp(pin_text(v).bytes, expected ? expected : "")
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
static bool unpack(const pin_value *value, int count, const pin_value **out)
{
    REQUIRE(value->type == PIN_UNION && value->ptr);
    const pin_union *data = value->ptr;
    REQUIRE(data->count == count);
    *out = data->values;
    return true;
}
static bool tagged(
    int b,
    const pin_value *value,
    const char *package,
    const char *name,
    const char *member,
    int count,
    const pin_value **out
)
{
    const script_catalog_variant *form = variant(b, package, name, member);
    REQUIRE(form && unpack(value, count, out));
    REQUIRE(((const pin_union *)value->ptr)->tag == form->tag);
    return true;
}
static bool array(const pin_value *value, pin_type type, int count, const pin_array **out)
{
    REQUIRE(value->type == type && value->container == PIN_CONTAINER_ARRAY);
    *out = value->ptr;
    REQUIRE((*out ? (*out)->count : 0) == count);
    return true;
}
static bool bounded_array(const pin_value *value, pin_type type, int cap, const pin_array **out)
{
    REQUIRE(value->type == type && value->container == PIN_CONTAINER_ARRAY);
    *out = value->ptr;
    REQUIRE(!*out || ((*out)->count >= 0 && (*out)->count <= cap));
    return true;
}
static bool id(const pin_array *numbers, int slot, uint64_t *out)
{
    if (slot < 0) {
        REQUIRE(slot == -1);
        *out = 0;
        return true;
    }
    REQUIRE(numbers && slot < numbers->count && numbers->items[slot].type == PIN_U64);
    *out = numbers->items[slot].u64;
    return true;
}
static bool row_id(const pin_value *row, const char *name, const pin_array *numbers, uint64_t expected)
{
    pin_value slot;
    uint64_t actual;
    REQUIRE(
        read(row, name, &slot) && slot.type == PIN_I32 &&
        id(numbers, slot.i, &actual) && actual == expected
    );
    return true;
}
static bool type_row(const pin_array *types, const pin_array *numbers, int index, script_type expected)
{
    REQUIRE(types && index >= 0 && index < types->count);
    const pin_value *row = &types->items[index];
    REQUIRE(
        same_int(row, "storage", expected.storage) &&
        same_int(row, "container", expected.container) &&
        same_int(row, "flags", (int)expected.flags) &&
        row_id(row, "nominal_slot", numbers, expected.nominal) &&
        row_id(row, "parameter_slot", numbers, expected.parameter)
    );
    return true;
}
static bool value_type(int b, const pin_value *value, script_type expected)
{
    const pin_value *v;
    REQUIRE(tagged(b, value, NAMED, "Value", "Typed", 5, &v));
    REQUIRE(
        v[0].type == PIN_I32 && v[0].i == (int)expected.storage &&
        v[1].type == PIN_I32 && v[1].i == (int)expected.container &&
        v[2].type == PIN_I32 && v[2].i == (int)expected.flags &&
        v[3].type == PIN_U64 && v[3].u64 == expected.nominal &&
        v[4].type == PIN_U64 && v[4].u64 == expected.parameter
    );
    return true;
}
static bool diagnostics(const pin_value *value, const script_diagnostic *expected, int count)
{
    const pin_array *rows;
    REQUIRE(count <= 4 && array(value, PIN_STRUCT, count, &rows));
    for (int i = 0; i < count; i++) {
        const script_diagnostic *d = &expected[i];
        const pin_value *row = &rows->items[i];
        REQUIRE(
            same_int(row, "start", d->range.start) &&
            same_int(row, "end", d->range.end) && same_int(row, "line", d->range.line) &&
            same_int(row, "column", d->range.column) &&
            same_int(row, "document", d->range.document) &&
            same_text(row, "message", d->message)
        );
    }
    return true;
}
static bool use(const pin_value *row, const pin_array *ids, const script_use *expected)
{
    REQUIRE(
        same_int(row, "kind", expected->kind) &&
        same_int(row, "symbol", expected->symbol) &&
        row_id(row, "member_slot", ids, expected->member) &&
        same_int(row, "start", expected->range.start) &&
        same_int(row, "end", expected->range.end) &&
        same_int(row, "line", expected->range.line) &&
        same_int(row, "column", expected->range.column) &&
        same_int(row, "document", expected->range.document)
    );
    return true;
}
static bool plan(int b, const pin_value *value, const script_constructor_plan *expected)
{
    const pin_value *v;
    REQUIRE(tagged(b, value, CONSTRUCTORS, "Plan", "Constructed", 13, &v));
    REQUIRE(
        value_type(b, &v[0], expected->type) && v[1].type == PIN_BOOL &&
        v[1].b == expected->publish && v[2].type == PIN_U64 &&
        v[2].u64 == expected->id && v[3].type == PIN_STR && pin_text(v[3]).bytes &&
        !strcmp(pin_text(v[3]).bytes, expected->name ? expected->name : "")
    );
    REQUIRE(
        value_type(b, &v[4], expected->definition_type) &&
        value_type(b, &v[11], expected->element_type) && v[12].type == PIN_U64 &&
        v[12].u64 == expected->element_type.collection &&
        v[5].type == PIN_I32 && v[5].i == (int)expected->size &&
        v[6].type == PIN_BOOL && v[6].b == expected->internal
    );
    const pin_array *variants, *fields, *ids;
    REQUIRE(
        array(&v[7], PIN_STRUCT, expected->variant_count, &variants) &&
        array(&v[8], PIN_STRUCT, expected->field_count, &fields) &&
        bounded_array(&v[9], PIN_U64, 12, &ids)
    );
    for (int i = 0; i < expected->variant_count; i++) {
        const script_constructor_variant *s = &expected->variants[i];
        const pin_value *row = &variants->items[i];
        pin_value collection;
        REQUIRE(
            read(row, "collection", &collection) &&
            collection.type == PIN_U64 && collection.u64 == s->type.collection
        );
        REQUIRE(
            same_int(row, "catalog_type", s->catalog_type) &&
            same_int(row, "variant", s->variant) && same_int(row, "member", s->member) &&
            same_int(row, "storage", s->type.storage) &&
            same_int(row, "container", s->type.container) &&
            same_int(row, "flags", (int)s->type.flags) &&
            row_id(row, "nominal_slot", ids, s->type.nominal) &&
            row_id(row, "parameter_slot", ids, s->type.parameter) &&
            row_id(row, "tag_slot", ids, s->tag)
        );
    }
    for (int i = 0; i < expected->field_count; i++) {
        const script_constructor_field *s = &expected->fields[i];
        REQUIRE(
            same_int(&fields->items[i], "capability", s->capability) &&
            same_int(&fields->items[i], "parameter", s->parameter) &&
            same_int(&fields->items[i], "symbol", s->symbol)
        );
    }
    REQUIRE(diagnostics(&v[10], expected->diagnostics, expected->diagnostic_count));
    return true;
}
static bool prefix(const pin_value *value, const pin_value *before, int added, pin_type type, int cap)
{
    const pin_array *out, *old;
    REQUIRE(bounded_array(value, type, cap, &out) && bounded_array(before, type, cap, &old));
    int old_count = old ? old->count : 0;
    REQUIRE((out ? out->count : 0) == old_count + added);
    for (int i = 0; i < old_count; i++) {
        REQUIRE(pin_value_equal(&out->items[i], &old->items[i]));
    }
    return true;
}
static const script_catalog_type *definition(
    const script_signature_facts *facts,
    const script_signature_reading *expected,
    int index
)
{
    return index < facts->named.catalog->type_count
             ? &facts->named.catalog->types[index]
             : &expected->types[index - facts->named.catalog->type_count];
}
static bool entry(const pin_value *row, const pin_array *types, const pin_array *ids, const script_catalog_type *expected)
{
    REQUIRE(
        same_text(row, "name", expected->name) &&
        same_text(row, "source_name", expected->source_name) &&
        same_text(row, "key", expected->key) &&
        same_bool(row, "has_source_name", expected->source_name != NULL) &&
        same_bool(row, "has_key", expected->key != NULL) &&
        same_bool(row, "internal", expected->internal) &&
        same_bool(row, "component", expected->component) &&
        row_id(row, "id_slot", ids, expected->id)
    );
    pin_value index;
    REQUIRE(
        read(row, "type", &index) && index.type == PIN_I32 &&
        type_row(types, ids, index.i, expected->type)
    );
    return true;
}
static bool environment(
    int b,
    const pin_value *value,
    const pin_value *before,
    const script_signature_facts *facts,
    const script_signature_reading *expected,
    const pin_array **name_ids
)
{
    const pin_value *out, *old, *names, *old_names, *constructors, *old_constructors;
    REQUIRE(
        tagged(b, value, PACKAGE, "Environment", "Tables", 3, &out) &&
        unpack(before, 3, &old)
    );
    REQUIRE(
        tagged(b, &out[0], NAMED, "Environment", "Tables", 15, &names) &&
        unpack(&old[0], 15, &old_names)
    );
    REQUIRE(
        tagged(b, &out[1], CONSTRUCTORS, "Environment", "Tables", 8, &constructors) &&
        unpack(&old[1], 8, &old_constructors)
    );
    REQUIRE(out[2].type == PIN_I32 && out[2].i == (int)facts->pointer_size);
    int unchanged[] = { 0, 5, 6, 7, 8, 10, 11, 12, 13, 14 };
    for (int i = 0; i < 10; i++) {
        REQUIRE(pin_value_equal(&names[unchanged[i]], &old_names[unchanged[i]]));
    }
    for (int i = 4; i < 8; i++) {
        REQUIRE(pin_value_equal(&constructors[i], &old_constructors[i]));
    }
    REQUIRE(
        prefix(&names[1], &old_names[1], expected->type_count, PIN_STRUCT, 64) &&
        prefix(&names[2], &old_names[2], 3 * expected->type_count + expected->use_count, PIN_U64, 128)
    );
    const pin_array *types, *ids, *merged, *own, *uses;
    REQUIRE(
        bounded_array(&names[1], PIN_STRUCT, 64, &types) &&
        bounded_array(&names[2], PIN_U64, 128, &ids)
    );
    REQUIRE(
        array(&names[3], PIN_STRUCT, facts->named.catalog->type_count + expected->type_count, &merged) &&
        array(&names[4], PIN_STRUCT, facts->named.own_type_count + expected->type_count, &own) &&
        array(&names[9], PIN_STRUCT, facts->named.use_count + expected->use_count, &uses)
    );
    for (int i = 0; i < merged->count; i++) {
        REQUIRE(entry(&merged->items[i], types, ids, definition(facts, expected, i)));
    }
    for (int i = 0; i < (own ? own->count : 0); i++) {
        const script_catalog_type *type = i < facts->named.own_type_count
                                            ? &facts->named.own_types[i]
                                            : &expected->types[i - facts->named.own_type_count];
        REQUIRE(entry(&own->items[i], types, ids, type));
    }
    for (int i = 0; i < (uses ? uses->count : 0); i++) {
        const script_use *want = i < facts->named.use_count
                                   ? &facts->named.uses[i]
                                   : &expected->uses[i - facts->named.use_count];
        REQUIRE(use(&uses->items[i], ids, want));
    }
    int added_variants = 0;
    for (int i = 0; i < expected->type_count; i++) {
        added_variants += expected->types[i].variant_count;
    }
    REQUIRE(
        prefix(&constructors[0], &old_constructors[0], added_variants, PIN_STRUCT, 64) &&
        prefix(
            &constructors[1],
            &old_constructors[1],
            3 * added_variants + expected->type_count,
            PIN_U64,
            128
        )
    );
    const pin_array *ctypes, *cids, *definitions, *variants;
    REQUIRE(
        bounded_array(&constructors[0], PIN_STRUCT, 64, &ctypes) &&
        bounded_array(&constructors[1], PIN_U64, 128, &cids) &&
        array(
            &constructors[2],
            PIN_STRUCT,
            facts->named.catalog->type_count + expected->type_count,
            &definitions
        ) &&
        bounded_array(&constructors[3], PIN_STRUCT, 16, &variants)
    );
    int at = 0;
    for (int i = 0; i < definitions->count; i++) {
        const script_catalog_type *want = definition(facts, expected, i);
        const pin_value *row = &definitions->items[i];
        REQUIRE(
            row_id(row, "id_slot", cids, want->id) &&
            same_int(row, "variant_start", at) &&
            same_int(row, "variant_count", want->variant_count)
        );
        for (int j = 0; j < want->variant_count; j++) {
            REQUIRE(variants && at < variants->count);
            const pin_value *v = &variants->items[at++];
            pin_value index;
            REQUIRE(
                row_id(v, "tag_slot", cids, want->variants[j].tag) &&
                read(v, "type", &index) && index.type == PIN_I32 &&
                type_row(ctypes, cids, index.i, want->variants[j].type)
            );
        }
    }
    REQUIRE((variants ? variants->count : 0) == at);
    *name_ids = ids;
    return true;
}
static bool owned(arena *owner, const pin_value *value, int depth)
{
    REQUIRE(depth <= 12);
    if (value->container == PIN_CONTAINER_ARRAY) {
        if (!value->ptr) {
            return true;
        }
        const pin_array *a = value->ptr;
        REQUIRE(
            arena_contains(owner, a) && a->count >= 0 && a->count <= 128 &&
            (!a->count || (a->items && arena_contains(owner, a->items)))
        );
        for (int i = 0; i < a->count; i++) {
            REQUIRE(owned(owner, &a->items[i], depth + 1));
        }
        return true;
    }
    if (value->type == PIN_UNION) {
        const pin_union *u = value->ptr;
        REQUIRE(u && arena_contains(owner, u) && u->count >= 0 && u->count <= 15);
        for (int i = 0; i < u->count; i++) {
            REQUIRE(owned(owner, &u->values[i], depth + 1));
        }
        return true;
    }
    if (value->type == PIN_STRUCT) {
        REQUIRE(
            value->ptr && value->record && arena_contains(owner, value->ptr) &&
            arena_contains(owner, value->record)
        );
        const type_desc *type = pin_value_type(value);
        REQUIRE(
            type && type->name && arena_contains(owner, type->name) &&
            type->field_count >= 0 && type->field_count <= 11 &&
            (!type->field_count || (type->fields && arena_contains(owner, type->fields)))
        );
        for (int i = 0; i < type->field_count; i++) {
            REQUIRE(
                type->fields[i].name && arena_contains(owner, type->fields[i].name)
            );
            pin_value field;
            REQUIRE(!pin_read_field(owner, value->ptr, &type->fields[i], &field));
            REQUIRE(owned(owner, &field, depth + 1));
        }
        return true;
    }
    if (value->type == PIN_STR) {
        REQUIRE(pin_text(*value).bytes && arena_contains(owner, pin_text(*value).bytes));
        return true;
    }
    REQUIRE(value->type == PIN_I32 || value->type == PIN_U64 || value->type == PIN_BOOL);
    return true;
}
static bool reading_value(
    int b,
    const pin_value *value,
    const pin_value *before,
    const script_signature_facts *facts,
    const script_signature_reading *expected,
    const pin_value **environment_out
)
{
    const pin_value *v;
    REQUIRE(
        tagged(b, value, PACKAGE, "Reading", "Read", 8, &v) &&
        value_type(b, &v[0], expected->type)
    );
    REQUIRE(v[7].type == PIN_U64 && v[7].u64 == expected->type.collection);
    REQUIRE(
        v[1].type == PIN_BOOL && v[1].b == (expected->unit != NULL) &&
        v[2].type == PIN_STR && pin_text(v[2]).bytes &&
        !strcmp(pin_text(v[2]).bytes, expected->unit ? expected->unit : "")
    );
    REQUIRE(
        expected->diagnostic_count <= 4 && expected->use_count <= 4 &&
        expected->publication_count <= 4 && expected->type_count == expected->publication_count
    );
    const pin_array *ids;
    REQUIRE(
        environment(b, &v[3], before, facts, expected, &ids) &&
        diagnostics(&v[4], expected->diagnostics, expected->diagnostic_count)
    );
    const pin_array *uses, *plans;
    REQUIRE(
        array(&v[5], PIN_STRUCT, expected->use_count, &uses) &&
        array(&v[6], PIN_UNION, expected->publication_count, &plans)
    );
    for (int i = 0; i < expected->use_count; i++) {
        REQUIRE(use(&uses->items[i], ids, &expected->uses[i]));
    }
    for (int i = 0; i < expected->publication_count; i++) {
        REQUIRE(
            expected->publications[i].publish &&
            plan(b, &plans->items[i], &expected->publications[i])
        );
    }
    *environment_out = &v[3];
    return true;
}

static int node_index(TSNode node)
{
    if (ts_node_is_null(node)) {
        return -1;
    }
    for (int i = 0; i < node_count; i++) {
        if (ts_node_eq(nodes[i].node, node)) {
            return i;
        }
    }
    return -2;
}
static int kind_index(const char *kind, int ordinal)
{
    for (int i = 0; i < node_count; i++) {
        if (!strcmp(ts_node_type(nodes[i].node), kind) && !ordinal--) {
            return i;
        }
    }
    return -1;
}
static bool parse_source(const char *text)
{
    cleanup();
    REQUIRE(strlen(text) < sizeof source);
    strcpy(source, text);
    syntax = script_parse(source);
    parses++;
    REQUIRE(syntax && !syntax->diagnostic_count);
    REQUIRE(
        compiler_fixture_nodes(ts_tree_root_node(syntax->tree), nodes, NODE_CAP, 32, &node_count)
    );
    return true;
}
static void reset_facts()
{
    scope = (script_scope){ .parent = -1,
                            .function = -1,
                            .range = script_node_range(nodes[0].node) };
    int record = kind_index("record_declaration", 0);
    TSNode token = ts_node_child_by_field_name(nodes[record].node, "name", 4);
    symbol = (script_symbol){ .id = ROW_ID,
                              .name = "Row",
                              .kind = SCRIPT_TYPE,
                              .type = { .storage = PIN_STRUCT, .nominal = ROW_ID },
                              .scope = 0,
                              .initializer = -1,
                              .function = -1,
                              .declaration = script_node_range(token) };
    definitions[0] = (script_catalog_type){ .id = ROW_ID,
                                            .name = "Facts.Row",
                                            .source_name = "Row",
                                            .type = symbol.type,
                                            .authored_record = true,
                                            .size = sizeof(void *) };
    definitions[1] = (script_catalog_type){ .id = PEER_ID,
                                            .name = "Facts.Peer",
                                            .source_name = "Peer",
                                            .type = { .storage = PIN_STRUCT,
                                                      .nominal = PEER_ID },
                                            .authored_record = true,
                                            .size = sizeof(void *) };
    definitions[2] = (script_catalog_type){ 0 };
    catalog = (script_catalog){ .types = definitions, .type_count = 2 };
    facts = (script_signature_facts){
        .named = { .lexical = { .scopes = &scope,
                                .symbols = &symbol,
                                .symbol_count = 1,
                                .identity = "legacy:record-members" },
                   .catalog = &catalog,
                   .own_types = definitions,
                   .own_type_count = 2,
                   .package_scope = -1 },
        .pointer_size = sizeof(void *)
    };
}
static pin_value previous_names(const char *const *names, int count)
{
    pin_array *values = pin_array_snapshot_allocate(&input, PIN_STR, NULL, count);
    for (int i = 0; i < count; i++) {
        size_t bytes = strlen(names[i]) + 1;
        char *copy = arena_alloc(&input, bytes);
        memcpy(copy, names[i], bytes);
        const char *error = pin_value_text(&input, &values->items[i], copy, bytes - 1);
        if (error) {
            abort();
        }
    }
    return (pin_value){ .type = PIN_STR, .container = PIN_CONTAINER_ARRAY, .ptr = values };
}
static bool member_output(
    int backend,
    const pin_value *out,
    const pin_value *before,
    const script_record_member *expected,
    const pin_value **updated
)
{
    const pin_value *v;
    REQUIRE(tagged(backend, out, MEMBERS, "Member", "Discovered", 7, &v));
    REQUIRE(
        v[0].type == PIN_STR && !strcmp(pin_text(v[0]).bytes, expected->field.name)
    );
    REQUIRE(v[1].type == PIN_U64 && v[1].u64 == expected->field.id);
    REQUIRE(v[3].type == PIN_BOOL && v[3].b == expected->field.has_default);
    REQUIRE(v[4].type == PIN_I32 && v[4].i == node_index(expected->default_node));
    REQUIRE(v[5].type == PIN_BOOL && v[5].b == expected->admissible);
    REQUIRE(diagnostics(&v[6], expected->diagnostics, expected->diagnostic_count));
    REQUIRE(reading_value(backend, &v[2], before, &facts, &expected->annotation, updated));
    REQUIRE(expected->field.writable && !expected->field.offset);
    REQUIRE(expected->field.default_value.type == PIN_FLOW);
    return true;
}
static bool run_member(
    int member,
    const char *const *previous,
    int count,
    const script_record_member *expected,
    bool use_chain,
    bool save_chain,
    bool retain
)
{
    REQUIRE(member >= 0);
    for (int b = 0; b < 2; b++) {
        arena_free(&input);
        arena_free(&detached);
        arena_free(&before_storage);
        pin_value args[6], out, before;
        if (use_chain) {
            REQUIRE(!compiler_fixture_snapshot(&input, &args[0], &chain_environment[b]));
        } else {
            REQUIRE(signature_fixture_environment(&fixture, b, &facts, 1, &input, &args[0]));
        }
        REQUIRE(
            signature_fixture_tree(&fixture, b, nodes, node_count, source, &input, &args[1])
        );
        args[2] = integer(member);
        args[3] = previous_names(previous, count);
        args[4] = integer(0);
        args[5] = integer(0);
        REQUIRE(!compiler_fixture_snapshot(&before_storage, &before, &args[0]));
        REQUIRE(
            compiler_fixture_call(&fixture, b, MEMBERS, "Read", args, 6, CALL_BUDGET, &detached, &out)
        );
        calls++;
        arena_free(&input);
        const pin_value *updated;
        REQUIRE(owned(&detached, &out, 0) && member_output(b, &out, &before, expected, &updated));
        if (save_chain) {
            arena_free(&chains[b]);
            REQUIRE(!compiler_fixture_snapshot(&chains[b], &chain_environment[b], updated));
            REQUIRE(owned(&chains[b], &chain_environment[b], 0));
        }
        if (retain) {
            arena_free(&retained[b]);
            REQUIRE(!compiler_fixture_snapshot(&retained[b], &kept[b], &out));
            REQUIRE(owned(&retained[b], &kept[b], 0));
            has_kept[b] = true;
        }
    }
    return true;
}
static bool prepare()
{
    const source_package_input files[] = {
        { "record_members/Members.chibi",
          RECORD_MEMBERS_CHIBI_SOURCE,
          sizeof RECORD_MEMBERS_CHIBI_SOURCE - 1 },
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
        { "/sdk/core/strings/Strings.chibi", CORE_STRINGS_SOURCE, sizeof CORE_STRINGS_SOURCE - 1 }
    };
    for (int i = 0; i < COUNT(files); i++) {
        REQUIRE(files[i].size < 48 * 1024);
    }
    REQUIRE(
        compiler_fixture_prepare_packs(&fixture, files, COUNT(files), &COMPILER_JSON_DEFINITION_PACK, 1)
    );
    parses += 2 * COUNT(files);
    prepared = true;
    return true;
}
TEST_CASE(record_members_selection_keeps_original_guards)
{
    REQUIRE(prepare());
    REQUIRE(parse_source("Facts :: library { Row::struct{value:int;} }"));
    int record = kind_index("record_declaration", 0);
    REQUIRE(record >= 0);
    const bool accepted[] = { true,  false, false, false,
                              false, true,  false, false };
    for (int i = 0; i < COUNT(accepted); i++) {
        reset_facts();
        if (i == 1) {
            facts.named.lexical.symbol_count = 0;
        }
        if (i == 2) {
            definitions[0].authored_record = false;
        }
        if (i == 3) {
            symbol.declaration.start--;
        }
        if (i == 4) {
            symbol.declaration.document = 1;
        }
        if (i == 5) {
            symbol.kind = SCRIPT_LOCAL;
            symbol.type.storage = PIN_BOOL;
        }
        if (i == 6) {
            definitions[1] = definitions[0];
            definitions[0].authored_record = false;
        }
        if (i == 7) {
            catalog.type_count = 0;
            facts.named.own_type_count = 0;
        }
        script_record_selection result = script_record_members_select(
            &oracle,
            &facts,
            nodes[record].node,
            source,
            0,
            0
        );
        REQUIRE(result.symbol == (accepted[i] ? 0 : -1));
        REQUIRE(result.definition == (accepted[i] ? 0 : -1) && !result.diagnostic_count);
        for (int b = 0; b < 2; b++) {
            arena_free(&input);
            arena_free(&detached);
            pin_value args[6], out;
            REQUIRE(signature_fixture_environment(&fixture, b, &facts, 1, &input, &args[0]));
            pin_array *bits = pin_array_snapshot_allocate(
                &input,
                PIN_BOOL,
                NULL,
                catalog.type_count
            );
            for (int j = 0; j < catalog.type_count; j++) {
                bits->items[j] = boolean(catalog.types[j].authored_record);
            }
            args[1] = (pin_value){ .type = PIN_BOOL,
                                   .container = PIN_CONTAINER_ARRAY,
                                   .ptr = bits };
            REQUIRE(
                signature_fixture_tree(&fixture, b, nodes, node_count, source, &input, &args[2])
            );
            args[3] = integer(record);
            args[4] = integer(0);
            args[5] = integer(0);
            REQUIRE(
                compiler_fixture_call(&fixture, b, MEMBERS, "Select", args, 6, CALL_BUDGET, &detached, &out)
            );
            calls++;
            arena_free(&input);
            const pin_value *v;
            REQUIRE(
                owned(&detached, &out, 0) &&
                tagged(b, &out, MEMBERS, "Selection", "Selected", 3, &v)
            );
            REQUIRE(v[0].type == PIN_I32 && v[0].i == result.symbol);
            REQUIRE(v[1].type == PIN_I32 && v[1].i == result.definition);
            REQUIRE(diagnostics(&v[2], result.diagnostics, result.diagnostic_count));
        }
    }
    REQUIRE(calls == 16);
    return true;
}
static const char *const fields[] = {
    "value:int;",  "value:string;",   "value:bool=Unknown();", "value:number;",
    "value:int?;", "value:string[];", "value:ref int;",        "value:Missing;",
    "value:int;",  "label:string;",   "value:int|string;",     "value:Peer;"
};
TEST_CASE(record_members_annotations_and_diagnostics)
{
    REQUIRE(prepared && COUNT(fields) == 12);
    const bool accepted[] = { true,  true,  true,  false, false, false,
                              false, false, false, false, false, false };
    const int errors[] = { 0, 0, 0, 1, 1, 1, 1, 2, 2, 1, 1, 1 };
    for (int i = 0; i < COUNT(fields); i++) {
        char text[SOURCE_CAP];
        int length = snprintf(text, sizeof text, "Facts :: library { Row::struct{%s} }", fields[i]);
        REQUIRE(length > 0 && length < (int)sizeof text && parse_source(text));
        reset_facts();
        int member = kind_index("record_field", 0);
        REQUIRE(member >= 0);
        const char *const duplicates[] = { "value", "other", "value" };
        const char *const label[] = { "label" };
        const char *const *previous = i == 8 ? duplicates
                                    : i == 9 ? label
                                             : NULL;
        int count = i == 8 ? COUNT(duplicates) : i == 9 ? 1 : 0;
        script_record_member result = script_record_members_read(
            &oracle,
            &facts,
            nodes[member].node,
            source,
            0,
            0,
            previous,
            count
        );
        REQUIRE(result.admissible == accepted[i] && result.diagnostic_count == errors[i]);
        REQUIRE(result.field.has_default == (i == 2));
        REQUIRE(result.annotation.publication_count == (i == 10 ? 1 : 0));
        REQUIRE(result.annotation.use_count == (i == 11 ? 1 : 0));
        if (i == 2) {
            REQUIRE(result.field.type.storage == PIN_BOOL && !result.diagnostic_count);
            REQUIRE(!strcmp(ts_node_type(result.default_node), "call_expression"));
        }
        if (i == 11) {
            REQUIRE(result.field.type.nominal == PEER_ID);
        }
        REQUIRE(run_member(member, previous, count, &result, false, false, i == 9));
    }
    REQUIRE(calls == 40);
    return true;
}
TEST_CASE(record_members_default_boundary_requires_refreshed_facts)
{
    REQUIRE(
        prepared && parse_source("Facts :: library { Row::struct{first:int=Unknown();second:After;} }")
    );
    reset_facts();
    int first = kind_index("record_field", 0), second = kind_index("record_field", 1);
    REQUIRE(first >= 0 && second >= 0 && first < second);
    script_record_member
        head = script_record_members_read(&oracle, &facts, nodes[first].node, source, 0, 0, NULL, 0);
    REQUIRE(head.admissible && head.field.has_default && !head.diagnostic_count);
    REQUIRE(run_member(first, NULL, 0, &head, false, false, false));
    const char *const previous[] = { "first" };
    script_record_member stale = script_record_members_read(
        &oracle,
        &facts,
        nodes[second].node,
        source,
        0,
        0,
        previous,
        1
    );
    REQUIRE(
        !stale.admissible && stale.annotation.diagnostic_count == 1 &&
        stale.diagnostic_count == 2
    );
    REQUIRE(run_member(second, previous, 1, &stale, false, false, false));
    /* This is a fixed raw catalog snapshot supplied by the caller after its
     * boundary, NOT an answer to or execution of Unknown(). Only the next
     * authored annotation read decides what these facts mean. */
    definitions[2] = (script_catalog_type){ .id = AFTER_ID,
                                            .name = "After",
                                            .source_name = "After",
                                            .type = { .storage = PIN_BOOL } };
    catalog.type_count = 3;
    facts.named.own_type_count = 3;
    script_record_member fresh = script_record_members_read(
        &oracle,
        &facts,
        nodes[second].node,
        source,
        0,
        0,
        previous,
        1
    );
    REQUIRE(!fresh.admissible && !fresh.annotation.diagnostic_count && fresh.diagnostic_count == 1);
    REQUIRE(fresh.field.type.storage == PIN_BOOL && fresh.field.type.nominal == AFTER_ID);
    REQUIRE(run_member(second, previous, 1, &fresh, false, false, false));
    REQUIRE(calls == 46);
    return true;
}
TEST_CASE(record_members_publications_continue_before_next_member)
{
    REQUIRE(
        prepared && parse_source("Facts :: library { Row::struct{left:int|string=Unknown();right:int|string;} }")
    );
    reset_facts();
    int first = kind_index("record_field", 0), second = kind_index("record_field", 1);
    REQUIRE(first >= 0 && second >= 0 && first < second);
    script_record_member
        head = script_record_members_read(&oracle, &facts, nodes[first].node, source, 0, 0, NULL, 0);
    REQUIRE(head.field.has_default && !head.admissible && head.annotation.type_count == 1);
    REQUIRE(head.annotation.publication_count == 1 && !head.annotation.use_count);
    REQUIRE(run_member(first, NULL, 0, &head, false, true, false));
    /* Native side applies the returned publication. Authored side feeds its
     * own returned environment forward; it is never given a C selection. */
    definitions[2] = head.annotation.types[0];
    catalog.type_count = 3;
    facts.named.own_type_count = 3;
    const char *const previous[] = { "left" };
    script_record_member tail = script_record_members_read(
        &oracle,
        &facts,
        nodes[second].node,
        source,
        0,
        0,
        previous,
        1
    );
    REQUIRE(!tail.field.has_default && !tail.admissible);
    REQUIRE(
        !tail.annotation.publication_count &&
        tail.field.type.nominal == head.field.type.nominal
    );
    REQUIRE(run_member(second, previous, 1, &tail, true, false, false));
    REQUIRE(calls == 50 && parses == 31);
    return true;
}
TEST_CASE(record_members_outputs_outlive_all_producers)
{
    REQUIRE(prepared && calls == 50 && parses == 31);
    cleanup();
    compiler_fixture_free(&fixture);
    prepared = false;
    for (int b = 0; b < 2; b++) {
        arena_free(&chains[b]);
        REQUIRE(has_kept[b] && owned(&retained[b], &kept[b], 0));
        const pin_value *v, *a;
        REQUIRE(unpack(&kept[b], 7, &v));
        REQUIRE(v[0].type == PIN_STR && !strcmp(pin_text(v[0]).bytes, "label"));
        REQUIRE(v[1].type == PIN_U64 && v[1].u64 == script_catalog_id("record-field", "label"));
        REQUIRE(v[3].type == PIN_BOOL && !v[3].b && v[4].type == PIN_I32 && v[4].i == -1);
        REQUIRE(v[5].type == PIN_BOOL && !v[5].b && unpack(&v[2], 8, &a));
        REQUIRE(a[7].type == PIN_U64 && a[7].u64 == 0);
        const pin_value *type;
        REQUIRE(unpack(&a[0], 5, &type) && type[0].type == PIN_I32 && type[0].i == PIN_STR);
        const pin_array *errors;
        REQUIRE(array(&v[6], PIN_STRUCT, 1, &errors));
        REQUIRE(same_text(&errors->items[0], "message", "Duplicate record field 'label'"));
        arena_free(&retained[b]);
        has_kept[b] = false;
    }
    return true;
}
void test_language_record_members_chibi()
{
    calls = parses = 0;
    prepared = false;
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(record_members_selection_keeps_original_guards);
    RUN_TEST_CASE(record_members_annotations_and_diagnostics);
    RUN_TEST_CASE(record_members_default_boundary_requires_refreshed_facts);
    RUN_TEST_CASE(record_members_publications_continue_before_next_member);
    RUN_TEST_CASE(record_members_outputs_outlive_all_producers);
    pu_clear_setup();
    cleanup();
    compiler_fixture_free(&fixture);
    for (int b = 0; b < 2; b++) {
        arena_free(&retained[b]);
        arena_free(&chains[b]);
        has_kept[b] = false;
    }
    prepared = false;
}
