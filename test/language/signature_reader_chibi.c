#include "signature_reader_chibi.h"

#include "capabilities/catalog_snapshot.h"
#include "compiler_fixture.h"
#include "language/signature_reader.h"
#include "lexical_binding_chibi_source.h"
#include "named_binding_chibi_source.h"
#include "package_core_source.h"
#include "package_graph_chibi_source.h"
#include "package_paths_chibi_source.h"
#include "package_routes_chibi_source.h"
#include "package_snapshot_chibi_source.h"
#include "package_syntax_chibi_source.h"
#include "signature_fixture.h"
#include "signature_reader_chibi_source.h"
#include "support/asset_json.h"
#include "syntax_chibi_source.h"
#include "third_party/pico_headers/pico_unit.h"
#include "toolchain/compiler_json.h"
#include "toolchain/compiler_syntax.h"
#include "type_constructors_chibi_source.h"
#include "types/array.h"
#include "types/text.h"
#include "types/union.h"
#include "types/value_document.h"
#include "unit_algebra_chibi_source.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 29 fixed reads plus one65-parenthesis deep read,60 authored calls at500,000
 * instructions. Seven compiler
 * sources per backend plus30 external Tree-sitter parses give44 parses total.
 * The parser supplies complete raw nodes/fields/named/extra flags; no C reader
 * result is supplied as an authored input. Request3's owned environment feeds
 * request6 within the same58calls. Inputs stay below256bytes/256nodes/depth32;
 * the separate deep case stays below640bytes/512nodes/depth256.
 * Compiler sources stay below48KiB. Inputs have2scopes/2symbols/1function/2documents,
 * 3catalog definitions/2variants/1import/export; one chained definition adds1.
 * Outputs:10publications/10newtypes/4uses/4diagnostics,64types/128IDs.
 * Four retained readings serialize before/after producer retirement: eight
 * bounded128KiB projections. No runtimeIO/services/processes or tasks execute. */
enum
{
    SOURCE_CAP = 256,
    NODE_CAP = 256,
    DEPTH_CAP = 32,
    JSON_CAP = 128 * 1024
};
static const char PACKAGE[] = "project:signature_reader",
                  NAMED[] = "project:named_binding",
                  CONSTRUCTORS[] = "project:type_constructors";
extern const TSLanguage *tree_sitter_chibi();
static compiler_fixture fixture;
static arena input, detached, oracle, baseline_storage, chained[2], chained_c,
    retained[2][2];
static pin_value chain_env[2], kept[2][2];
static char *golden[2][2];
static TSParser *parser;
static TSTree *tree;
static compiler_fixture_node nodes[NODE_CAP];
static char source[SOURCE_CAP];
static int node_count, calls, parses, publication_count;
static bool prepared;
static script_catalog chain_catalog;
static script_signature_reading reading;
static script_scope scopes[2];
static script_symbol symbols[2];
static script_function functions[1];
static script_source_document documents[2];
static char document_keys[2][64]; /* Mutable copies of borrowed input paths. */
static script_catalog_type base_types[3];
static script_catalog_variant variants[2];
static script_catalog base_catalog;
static script_package_export exported;
static script_package_exports exports;
static script_package_import imported;

static void projection_cleanup();
static void cleanup()
{
    arena_free(&input);
    arena_free(&detached);
    arena_free(&oracle);
    arena_free(&baseline_storage);
    ts_tree_delete(tree);
    tree = NULL;
    projection_cleanup();
}
static pin_value integer(int n)
{
    return (pin_value){ .type = PIN_I32, .i = n };
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
    pin_value collection;
    REQUIRE(
        read(row, "collection", &collection) && collection.type == PIN_U64 &&
        collection.u64 == expected.collection
    );
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
        v[5].type == PIN_I32 && v[5].i == (int)expected->size &&
        v[6].type == PIN_BOOL && v[6].b == expected->internal
    );
    REQUIRE(value_type(b, &v[11], expected->element_type));
    REQUIRE(v[12].type == PIN_U64 && v[12].u64 == expected->element_type.collection);
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
    int added_variants = 0, added_elements = 0;
    for (int i = 0; i < expected->type_count; i++) {
        added_variants += expected->types[i].variant_count;
        added_elements += expected->types[i].type.storage == PIN_POINTER;
    }
    REQUIRE(
        prefix(&constructors[0], &old_constructors[0], added_variants + added_elements, PIN_STRUCT, 64) &&
        prefix(
            &constructors[1],
            &old_constructors[1],
            3 * added_variants + expected->type_count + 2 * added_elements,
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
        if (want->type.storage == PIN_POINTER) {
            pin_value element;
            REQUIRE(read(row, "element_type", &element) && element.type == PIN_I32);
            REQUIRE(type_row(ctypes, cids, element.i, want->element_type));
        }
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
static av_doc *projection;
static yyjson_mut_doc *encoded;
static char *rendered;
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
static void projection_cleanup()
{
    free(rendered);
    rendered = NULL;
    yyjson_mut_doc_free(encoded);
    encoded = NULL;
    av_doc_free(projection);
    projection = NULL;
}
static bool serialize(const pin_value *value, char **out)
{
    projection = av_doc_new();
    encoded = yyjson_mut_doc_new(NULL);
    REQUIRE(projection && encoded);
    av_value *root = pin_value_to_document(projection, value);
    REQUIRE(root);
    yyjson_mut_doc_set_root(encoded, av_to_json(encoded, root));
    size_t length;
    rendered = yyjson_mut_write(encoded, YYJSON_WRITE_ALLOW_INVALID_UNICODE, &length);
    REQUIRE(rendered && length < JSON_CAP);
    *out = rendered;
    rendered = NULL;
    projection_cleanup();
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
static void facts_reset(bool catalog_shadow, bool generic_shadow)
{
    scopes[0] = (script_scope){ .parent = -1,
                                .function = -1,
                                .range = { 0, 255, 1, 1, 0 } };
    scopes[1] = (script_scope){ .parent = 0, .function = 0, .range = { 0, 255, 1, 1, 0 } };
    functions[0] = (script_function){ .symbol = 0, .scope = 1 };
    symbols[0] = (script_symbol){ .id = 7001,
                                  .name = "Run",
                                  .kind = SCRIPT_FUNCTION,
                                  .scope = 0,
                                  .function = 0,
                                  .declaration = { 0, 3, 1, 1, 0 } };
    symbols[1] = (script_symbol){ .id = 7002,
                                  .name = generic_shadow ? "u" : "T",
                                  .kind = SCRIPT_TYPE_PARAMETER,
                                  .scope = 1,
                                  .function = -1,
                                  .type = { .storage = PIN_FLOW,
                                            .parameter = UINT64_MAX - 5,
                                            .flags = SCRIPT_TYPE_PARAMETER },
                                  .declaration = { 5, 6, 1, 6, 0 } };
    documents[0] = (script_source_document){ .path = "/facts/a.chibi",
                                             .key = "facts/a.chibi" };
    documents[1] = (script_source_document){ .path = "/facts/b.chibi",
                                             .key = "facts/b.chibi" };
    script_type choice = { .storage = PIN_UNION,
                           .nominal = UINT64_MAX - 17,
                           .flags = SCRIPT_TYPE_UNION };
    variants[0] = (script_catalog_variant){ .name = "First",
                                            .tag = UINT64_MAX - 19,
                                            .tagged = true,
                                            .type = choice };
    variants[0].type.parameter = variants[0].tag;
    variants[1] = (script_catalog_variant){ .name = "Second",
                                            .tag = 11,
                                            .tagged = true,
                                            .type = choice };
    variants[1].type.parameter = variants[1].tag;
    base_types[0] = (script_catalog_type){ .id = UINT64_MAX - 11,
                                           .name = "Native.Row",
                                           .source_name = "Row",
                                           .type = { .storage = PIN_STRUCT,
                                                     .nominal = UINT64_MAX - 11 } };
    base_types[1] = (script_catalog_type){ .id = choice.nominal,
                                           .name = "Choice",
                                           .source_name = "Choice",
                                           .type = choice,
                                           .variants = variants,
                                           .variant_count = 2 };
    base_types[2] = (script_catalog_type){ .id = UINT64_MAX - 23,
                                           .name = "u",
                                           .type = { .storage = PIN_STRUCT,
                                                     .nominal = UINT64_MAX - 23 } };
    base_catalog = (script_catalog){ .types = base_types,
                                     .type_count = catalog_shadow ? 3 : 2 };
    exported = (script_package_export){ .name = "Row",
                                        .kind = SCRIPT_PACKAGE_TYPE,
                                        .id = base_types[0].id };
    exports = (script_package_exports){ .members = &exported,
                                        .member_count = 1,
                                        .metadata = &base_catalog };
    imported = (script_package_import){ .alias = "lib",
                                        .identity = "vendor:rows",
                                        .document = 0,
                                        .exports = &exports };
}
static script_signature_facts facts(bool chained)
{
    script_named_facts named = {
        .lexical = { .scopes = scopes,
                     .symbols = symbols,
                     .functions = functions,
                     .documents = documents,
                     .symbol_count = 2,
                     .identity = "project:signature_facts" },
        .catalog = &base_catalog,
        .native = &base_catalog,
        .imports = &imported,
        .import_count = 1,
        .document_count = 2,
        .package_scope = 0
    };
    if (chained) {
        static script_catalog_type merged[4];
        static script_catalog catalog;
        for (int i = 0; i < base_catalog.type_count; i++) {
            merged[i] = base_catalog.types[i];
        }
        for (int i = 0; i < chain_catalog.type_count; i++) {
            merged[base_catalog.type_count + i] = chain_catalog.types[i];
        }
        catalog = (script_catalog){ .types = merged,
                                    .type_count = base_catalog.type_count +
                                                  chain_catalog.type_count };
        named.catalog = &catalog;
        named.own_types = chain_catalog.types;
        named.own_type_count = chain_catalog.type_count;
    }
    return (script_signature_facts){ .named = named,
                                     .functions = functions,
                                     .function_count = 1,
                                     .pointer_size = sizeof(void *) };
}
static const char *annotations[] = {
    NULL,
    "int",
    "Row?[]",
    "task(int)",
    "task",
    "task(int, number)",
    "task(int)[]",
    "task(int) | task(int)",
    "int[]",
    "Row[]?",
    "int | number",
    "Row?",
    "^Row",
    "array",
    "Row",
    "map",
    "Missing",
    "lib.Row",
    "u",
    "u /* gap */ / s",
    "number charge",
    "string int/s",
    "u",
    "u",
    "^int u",
    "^int?",
    "^int[]",
    "^(int[])",
    "^Row",
};
static int max_nodes, max_depth;
static bool execute(int index)
{
    REQUIRE(index >= 0 && index < 29 && prepared && parser);
    cleanup();
    facts_reset(index == 22, index == 23);
    if (index == 28) {
        base_types[0].type.container = PIN_CONTAINER_MAP;
        base_types[0].type.collection = UINT64_MAX - 31;
        base_types[0].type.parameter = UINT64_MAX - 37;
    }
    REQUIRE(index != 6 || (chain_catalog.types && chain_catalog.type_count == 1));
    script_signature_facts context = facts(index == 6);
    int document = index == 17 ? 0 : index % 2;
    int length = snprintf(
        source,
        sizeof source,
        "Facts :: library { Run :: (value%s%s) {} }",
        annotations[index] ? ": " : "",
        annotations[index] ? annotations[index] : ""
    );
    REQUIRE(length > 0 && length < SOURCE_CAP);
    tree = ts_parser_parse_string(parser, NULL, source, (uint32_t)length);
    REQUIRE(tree);
    parses++;
    TSNode root = ts_tree_root_node(tree);
    if (ts_node_has_error(root)) {
        fprintf(stderr, "signature raw source%d has parser error: %s\n", index, source);
    }
    REQUIRE(
        !ts_node_has_error(root) &&
        compiler_fixture_nodes(root, nodes, NODE_CAP, DEPTH_CAP, &node_count)
    );
    int depths[NODE_CAP];
    for (int i = 0; i < node_count; i++) {
        REQUIRE(nodes[i].parent < i);
        depths[i] = nodes[i].parent < 0 ? 0 : depths[nodes[i].parent] + 1;
        if (depths[i] > max_depth) {
            max_depth = depths[i];
        }
    }
    if (node_count > max_nodes) {
        max_nodes = node_count;
    }
    TSNode program = ts_node_named_child(root, 0),
           body = ts_node_child_by_field_name(program, "body", 4),
           function = ts_node_named_child(body, 0),
           parameters = ts_node_child_by_field_name(function, "parameters", 10),
           parameter = ts_node_named_child(parameters, 0),
           target = ts_node_child_by_field_name(parameter, "type", 4);
    REQUIRE(
        !ts_node_is_null(program) && !ts_node_is_null(body) &&
        !ts_node_is_null(function) && !ts_node_is_null(parameters) &&
        !ts_node_is_null(parameter)
    );
    int target_index = -1;
    for (int i = 0; i < node_count; i++) {
        if (ts_node_eq(target, nodes[i].node)) {
            target_index = i;
            break;
        }
    }
    REQUIRE(
        index == 0 ? ts_node_is_null(target) && target_index == -1
                   : !ts_node_is_null(target) && target_index >= 0
    );
    if (index == 19) {
        bool extra = false;
        for (int i = 0; i < node_count; i++) {
            extra |= nodes[i].parent == target_index &&
                     ts_node_is_extra(nodes[i].node);
        }
        REQUIRE(extra);
    }
    reading = script_signature_read(&oracle, &context, target, source, 1, document, index >= 16);
    REQUIRE(
        reading.type_count <= 4 && reading.publication_count <= 4 &&
        reading.diagnostic_count <= 4 && reading.use_count <= 4
    );
    publication_count += reading.publication_count;
    for (int b = 0; b < 2; b++) {
        arena_free(&input);
        arena_free(&detached);
        arena_free(&baseline_storage);
        pin_value values[5], before, result;
        REQUIRE(
            signature_fixture_environment(&fixture, b, &context, 2, &input, &values[0]) &&
            signature_fixture_tree(&fixture, b, nodes, node_count, source, &input, &values[1])
        );
        if (index == 6) {
            REQUIRE(
                chain_env[b].type == PIN_UNION && chain_env[b].ptr &&
                !compiler_fixture_snapshot(&input, &values[0], &chain_env[b])
            );
            arena_free(&chained[b]);
            chain_env[b] = (pin_value){ 0 };
        }
        REQUIRE(!compiler_fixture_snapshot(&baseline_storage, &before, &values[0]));
        values[2] = integer(target_index);
        values[3] = integer(1);
        values[4] = integer(document);
        REQUIRE(compiler_fixture_call(
            &fixture,
            b,
            PACKAGE,
            index >= 16 ? "Signature" : "Type",
            values,
            5,
            500000,
            &detached,
            &result
        ));
        calls++;
        arena_free(&input);
        const pin_value *updated;
        REQUIRE(
            owned(&detached, &result, 0) &&
            reading_value(b, &result, &before, &context, &reading, &updated)
        );
        if (index == 3) {
            REQUIRE(
                reading.type_count == 1 && reading.use_count == 0 &&
                !compiler_fixture_snapshot(&chained[b], &chain_env[b], updated)
            );
        }
        if (index == 7 || index == 21) {
            int slot = index == 7 ? 0 : 1;
            REQUIRE(
                !compiler_fixture_snapshot(&retained[b][slot], &kept[b][slot], &result) &&
                owned(&retained[b][slot], &kept[b][slot], 0) &&
                serialize(&kept[b][slot], &golden[b][slot])
            );
        }
    }
    if (index == 3) {
        script_catalog new_types = { .types = reading.types,
                                     .type_count = reading.type_count };
        REQUIRE(script_catalog_snapshot(&chained_c, &new_types, &chain_catalog));
    }
    if (index == 6) {
        REQUIRE(
            !reading.publication_count && reading.type.container == PIN_CONTAINER_ARRAY &&
            (reading.type.flags & SCRIPT_TYPE_TASK)
        );
        arena_free(&chained_c);
        chain_catalog = (script_catalog){ 0 };
    }
    if (index == 7) {
        REQUIRE(reading.publication_count == 2);
    }
    if (index == 10 || index == 12) {
        REQUIRE(reading.publication_count == 1);
    }
    if (index == 14) {
        REQUIRE(reading.type.nominal == UINT64_MAX - 11 && !reading.diagnostic_count);
    }
    if (index == 15) {
        REQUIRE(reading.diagnostic_count == 1);
    }
    if (index == 18) {
        REQUIRE(reading.unit && !strcmp(reading.unit, "u"));
    }
    if (index == 19) {
        REQUIRE(reading.unit && !strcmp(reading.unit, "u/s"));
    }
    if (index == 21) {
        REQUIRE(reading.diagnostic_count == 2);
    }
    if (index == 22 || index == 23) {
        REQUIRE(!reading.unit && !reading.diagnostic_count && reading.use_count == 1);
    }
    if (index >= 24) {
        REQUIRE(!reading.diagnostic_count && reading.type.storage == PIN_POINTER);
        REQUIRE(reading.type_count == 1 && reading.publication_count == 1);
        script_type expected_element = index == 28
                                         ? base_types[0].type
                                         : (script_type){
                                               .storage = PIN_I32,
                                               .container = index == 27 ? PIN_CONTAINER_ARRAY
                                                                        : PIN_CONTAINER_SINGLE
                                           };
        REQUIRE(script_type_equal(reading.types[0].element_type, expected_element));
        REQUIRE(
            (reading.type.flags & SCRIPT_TYPE_OPTIONAL) ==
            (index == 25 ? SCRIPT_TYPE_OPTIONAL : 0)
        );
        REQUIRE(!(reading.type.flags & SCRIPT_TYPE_BOXED_OPTIONAL));
        REQUIRE(reading.type.container == (index == 26 ? PIN_CONTAINER_ARRAY : PIN_CONTAINER_SINGLE));
    }
    return true;
}
TEST_CASE(signature_reader_type_syntax_and_publication_state_match)
{
    const source_package_input files[] = {
        { "signature_reader/Reader.chibi",
          SIGNATURE_READER_CHIBI_SOURCE,
          sizeof SIGNATURE_READER_CHIBI_SOURCE - 1 },
        { "named_binding/Binding.chibi",
          NAMED_BINDING_CHIBI_SOURCE,
          sizeof NAMED_BINDING_CHIBI_SOURCE - 1 },
        { "type_constructors/Constructors.chibi",
          TYPE_CONSTRUCTORS_CHIBI_SOURCE,
          sizeof TYPE_CONSTRUCTORS_CHIBI_SOURCE - 1 },
        { "unit_algebra/Units.chibi",
          UNIT_ALGEBRA_CHIBI_SOURCE,
          sizeof UNIT_ALGEBRA_CHIBI_SOURCE - 1 },
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
    for (int i = 0; i < 7; i++) {
        REQUIRE(files[i].size < 48 * 1024);
    }
    REQUIRE(compiler_fixture_prepare_packs(&fixture, files, 7, &COMPILER_JSON_DEFINITION_PACK, 1));
    prepared = true;
    parser = ts_parser_new();
    REQUIRE(parser && ts_parser_set_language(parser, tree_sitter_chibi()));
    for (int i = 0; i < 16; i++) {
        REQUIRE(execute(i));
    }
    REQUIRE(calls == 32 && parses == 16);
    return true;
}
TEST_CASE(signature_reader_units_names_diagnostics_and_uses_match)
{
    REQUIRE(prepared);
    for (int i = 16; i < 29; i++) {
        REQUIRE(execute(i));
    }
    REQUIRE(calls == 58 && parses == 29 && publication_count == 10);
    printf("Signature reader:29 raw trees,58 calls,10 publications, max%d nodes/depth%d\n", max_nodes, max_depth);
    return true;
}
TEST_CASE(signature_reader_deep_parentheses_do_not_consume_callable_depth)
{
    REQUIRE(prepared && parser && calls == 58 && parses == 29);
    cleanup();
    facts_reset(false, false);
    char deep_source[640];
    compiler_fixture_node deep_nodes[512];
    int length = snprintf(deep_source, sizeof deep_source, "Facts :: library { Run :: (value: ");
    REQUIRE(length > 0);
    /* Parenthesized types nest without naming a collection. */
    for (int i = 0; i < 65; i++) {
        deep_source[length++] = '(';
    }
    memcpy(deep_source + length, "int", 3);
    length += 3;
    for (int i = 0; i < 65; i++) {
        deep_source[length++] = ')';
    }
    REQUIRE(length + sizeof ") {} }" < sizeof deep_source);
    memcpy(deep_source + length, ") {} }", sizeof ") {} }");
    length += (int)sizeof ") {} }" - 1;
    scopes[0].range.end = scopes[1].range.end = length;
    script_signature_facts context = facts(false);
    tree = ts_parser_parse_string(parser, NULL, deep_source, (uint32_t)length);
    REQUIRE(tree && !ts_node_has_error(ts_tree_root_node(tree)));
    parses++;
    int count = 0;
    REQUIRE(compiler_fixture_nodes(ts_tree_root_node(tree), deep_nodes, 512, 256, &count));
    int depths[512], deepest = 0;
    for (int i = 0; i < count; i++) {
        REQUIRE(deep_nodes[i].parent < i);
        depths[i] = deep_nodes[i].parent < 0 ? 0 : depths[deep_nodes[i].parent] + 1;
        if (depths[i] > deepest) {
            deepest = depths[i];
        }
    }
    REQUIRE(deepest > 64);
    TSNode program = ts_node_named_child(ts_tree_root_node(tree), 0),
           body = ts_node_child_by_field_name(program, "body", 4),
           function = ts_node_named_child(body, 0),
           parameters = ts_node_child_by_field_name(function, "parameters", 10),
           parameter = ts_node_named_child(parameters, 0),
           target = ts_node_child_by_field_name(parameter, "type", 4);
    REQUIRE(!ts_node_is_null(target));
    int target_index = -1;
    for (int i = 0; i < count; i++) {
        if (ts_node_eq(target, deep_nodes[i].node)) {
            target_index = i;
            break;
        }
    }
    REQUIRE(target_index >= 0);
    reading = script_signature_read(&oracle, &context, target, deep_source, 1, 0, false);
    REQUIRE(
        reading.type.storage == PIN_I32 && reading.type.container == PIN_CONTAINER_SINGLE &&
        !reading.type.flags && !reading.type.nominal &&
        !reading.type.parameter && !reading.unit && !reading.diagnostic_count &&
        !reading.use_count && !reading.publication_count && !reading.type_count
    );
    for (int b = 0; b < 2; b++) {
        arena_free(&input);
        arena_free(&detached);
        arena_free(&baseline_storage);
        pin_value values[5], before, result;
        REQUIRE(
            signature_fixture_environment(&fixture, b, &context, 2, &input, &values[0]) &&
            signature_fixture_tree(&fixture, b, deep_nodes, count, deep_source, &input, &values[1]) &&
            !compiler_fixture_snapshot(&baseline_storage, &before, &values[0])
        );
        values[2] = integer(target_index);
        values[3] = integer(1);
        values[4] = integer(0);
        REQUIRE(
            compiler_fixture_call(&fixture, b, PACKAGE, "Type", values, 5, 500000, &detached, &result)
        );
        calls++;
        arena_free(&input);
        const pin_value *updated;
        REQUIRE(
            owned(&detached, &result, 0) &&
            reading_value(b, &result, &before, &context, &reading, &updated)
        );
    }
    REQUIRE(calls == 60 && parses == 30 && publication_count == 10);
    printf("Signature deep parentheses:65 wrappers,%d bytes,%d nodes/depth%d,2 calls\n", length, count, deepest);
    return true;
}
TEST_CASE(signature_reader_readings_survive_all_producer_owners)
{
    cleanup();
    compiler_fixture_free(&fixture);
    ts_parser_delete(parser);
    parser = NULL;
    for (int b = 0; b < 2; b++) {
        for (int i = 0; i < 2; i++) {
            REQUIRE(
                kept[b][i].type == PIN_UNION && kept[b][i].ptr &&
                golden[b][i] && owned(&retained[b][i], &kept[b][i], 0)
            );
            char *after = NULL;
            REQUIRE(serialize(&kept[b][i], &after));
            bool equal = !strcmp(after, golden[b][i]);
            free(after);
            REQUIRE(equal);
        }
    }
    return true;
}
void test_language_signature_reader_chibi()
{
    calls = parses = publication_count = max_nodes = max_depth = 0;
    prepared = false;
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(signature_reader_type_syntax_and_publication_state_match);
    RUN_TEST_CASE(signature_reader_units_names_diagnostics_and_uses_match);
    RUN_TEST_CASE(signature_reader_deep_parentheses_do_not_consume_callable_depth);
    RUN_TEST_CASE(signature_reader_readings_survive_all_producer_owners);
    pu_clear_setup();
    cleanup();
    compiler_fixture_free(&fixture);
    ts_parser_delete(parser);
    parser = NULL;
    for (int b = 0; b < 2; b++) {
        arena_free(&chained[b]);
        chain_env[b] = (pin_value){ 0 };
        for (int i = 0; i < 2; i++) {
            arena_free(&retained[b][i]);
            kept[b][i] = (pin_value){ 0 };
            free(golden[b][i]);
            golden[b][i] = NULL;
        }
    }
    arena_free(&chained_c);
    chain_catalog = (script_catalog){ 0 };
}

/* Separate proposed selector in this owner: three fixed snapshot requests,
 * two accepted three-file orders and one two-file rejected prefix. Fourteen
 * source files/backend, 28 preparation parses, seven C oracle parses and 14
 * private raw parses. There are 64 top-level authored calls: six Build, 14
 * maps, 20 tree views and 24 reader calls. See exact assertions below.
 * No existing selector's source cap, closure or instruction budget changes. */
enum
{
    SNAPSHOT_FILES = 3,
    SNAPSHOT_NODES = 128,
    SNAPSHOT_SOURCE_CAP = 256
};
static const char STATE[] = "project:analysis_state",
                  SNAPSHOT[] = "project:source_snapshot";
static arena snapshot_storage, map_storage[2][2],
    view_storage[2][SNAPSHOT_FILES], direct_storage;
static pin_value snapshot_value, mapped[2][2], views[2][SNAPSHOT_FILES];
static int snapshot_calls, map_calls, view_calls, reader_calls, snapshot_parses;
static int snapshot_max_nodes;
static bool snapshot_named[2], snapshot_extra[2];
static const char
    snapshot_a[] = "package app\nimport lib \"project:lib\"\nA :: (value: "
                   "task(int)) {}\n",
    snapshot_lib[] = "package lib\nLib :: (value: u /* gap */ / s) {}\n",
    snapshot_b[] = "package app\nB :: (value: Missing) {}\n";
static const source_package_input snapshot_sources[] = {
    { "app/A.chibi", snapshot_a, sizeof snapshot_a - 1 },
    { "lib/Lib.chibi", snapshot_lib, sizeof snapshot_lib - 1 },
    { "app/B.chibi", snapshot_b, sizeof snapshot_b - 1 },
};
static void snapshot_cleanup()
{
    cleanup();
    arena_free(&snapshot_storage);
    arena_free(&direct_storage);
    snapshot_value = (pin_value){ 0 };
    for (int b = 0; b < 2; b++) {
        for (int i = 0; i < 2; i++) {
            arena_free(&map_storage[b][i]);
            mapped[b][i] = (pin_value){ 0 };
        }
        for (int i = 0; i < SNAPSHOT_FILES; i++) {
            arena_free(&view_storage[b][i]);
            views[b][i] = (pin_value){ 0 };
        }
    }
}
static pin_value snapshot_text(const char *s)
{
    s = s ? s : "";
    pin_value result;
    const char *error = pin_value_text(&input, &result, s, strlen(s));
    if (error) {
        abort();
    }
    return result;
}
static bool snapshot_put(pin_value *row, const char *name, pin_value value)
{
    const type_field *field = pin_type_field_path(pin_value_type(row), name);
    REQUIRE(field && field->type == value.type);
    REQUIRE(!pin_write_field(&input, row->ptr, field, value));
    return true;
}
static bool snapshot_row(int b, const char *name, pin_value *out)
{
    const pin_record *record = compiler_fixture_record(&fixture, b, SNAPSHOT, name, &input);
    REQUIRE(record);
    *out = (pin_value){ .type = PIN_STRUCT,
                        .record = record,
                        .type_name = record->type.name,
                        .ptr = arena_alloc(&input, record->type.size) };
    memset(out->ptr, 0, record->type.size);
    return true;
}
static bool snapshot_pack(int b, const source_package_input *files, int count, pin_value *values)
{
    REQUIRE(count > 0 && count <= SNAPSHOT_FILES && snapshot_row(b, "Options", &values[0]));
    pin_value yes = { .type = PIN_BOOL, .b = true };
    REQUIRE(
        snapshot_put(&values[0], "project", snapshot_text("/facts")) &&
        snapshot_put(&values[0], "has_project", yes) &&
        snapshot_put(&values[0], "has_options", yes) &&
        snapshot_put(&values[0], "has_files", yes) &&
        snapshot_put(&values[0], "has_collections", yes) &&
        snapshot_put(&values[0], "file_count", integer(count)) &&
        snapshot_put(&values[0], "collection_count", integer(0))
    );
    const pin_record *collection = compiler_fixture_record(
        &fixture,
        b,
        "project:package_routes",
        "Collection",
        &input
    );
    const pin_record *record = compiler_fixture_record(&fixture, b, SNAPSHOT, "Input", &input);
    REQUIRE(collection && record);
    pin_array
        *items = pin_array_snapshot_allocate(&input, PIN_STRUCT, record->type.name, count);
    pin_array *sizes = pin_array_snapshot_allocate(&input, PIN_U64, NULL, count);
    REQUIRE(items && sizes);
    items->record = record;
    values[1] = (pin_value){ .type = PIN_STRUCT,
                             .container = PIN_CONTAINER_ARRAY,
                             .record = collection,
                             .type_name = collection->type.name };
    values[2] = (pin_value){ .type = PIN_STRUCT,
                             .container = PIN_CONTAINER_ARRAY,
                             .record = record,
                             .type_name = record->type.name,
                             .ptr = items };
    values[3] = (pin_value){ .type = PIN_U64,
                             .container = PIN_CONTAINER_ARRAY,
                             .ptr = sizes };
    for (int i = 0; i < count; i++) {
        REQUIRE(
            files[i].size == strlen(files[i].text) && files[i].size < SNAPSHOT_SOURCE_CAP &&
            snapshot_row(b, "Input", &items->items[i]) &&
            snapshot_put(&items->items[i], "path", snapshot_text(files[i].path)) &&
            snapshot_put(&items->items[i], "text", snapshot_text(files[i].text)) &&
            snapshot_put(&items->items[i], "has_path", yes) &&
            snapshot_put(&items->items[i], "has_text", yes)
        );
        sizes->items[i] = (pin_value){ .type = PIN_U64, .u64 = files[i].size };
    }
    return true;
}
static bool snapshot_build(int b, const source_package_input *files, int count, bool accepted)
{
    pin_value values[4];
    REQUIRE(
        snapshot_pack(b, files, count, values) &&
        compiler_fixture_call(&fixture, b, SNAPSHOT, "Build", values, 4, 3000000, &snapshot_storage, &snapshot_value)
    );
    snapshot_calls++;
    arena_free(&input);
    const pin_value *payload;
    REQUIRE(
        tagged(b, &snapshot_value, SNAPSHOT, "Result", accepted ? "Accepted" : "Rejected", accepted ? 5 : 7, &payload)
    );
    const pin_array *documents, *forest, *imports;
    REQUIRE(
        array(&payload[accepted ? 1 : 4], PIN_STRUCT, count, &documents) &&
        bounded_array(&payload[accepted ? 2 : 5], PIN_STRUCT, SNAPSHOT_FILES * SNAPSHOT_NODES, &forest) &&
        bounded_array(&payload[accepted ? 3 : 6], PIN_STRUCT, 1, &imports)
    );
    REQUIRE(forest && forest->count > 0);
    if (accepted) {
        const pin_value *graph;
        const pin_array *rows;
        REQUIRE(
            array(&payload[0], PIN_STRUCT, 1, &rows) &&
            tagged(b, &payload[4], "project:package_graph", "Graph", "Accepted", 6, &graph) &&
            array(&graph[0], PIN_STRUCT, 2, &rows) &&
            array(&graph[1], PIN_I32, count, &rows) &&
            array(&graph[2], PIN_I32, count, &rows) &&
            array(&graph[3], PIN_I32, 2, &rows) &&
            array(&graph[4], PIN_STRUCT, 1, &rows) &&
            array(&graph[5], PIN_STRUCT, 1, &rows)
        );
    } else {
        const pin_array *cycle;
        REQUIRE(array(&payload[3], PIN_I32, 256, &cycle));
    }
    for (int i = 0; i < count; i++) {
        pin_value start, size;
        REQUIRE(
            read(&documents->items[i], "node_start", &start) && start.type == PIN_I32 &&
            read(&documents->items[i], "node_count", &size) &&
            size.type == PIN_I32 && size.i >= 0 && size.i <= SNAPSHOT_NODES
        );
        if (accepted || i == 0) {
            REQUIRE(
                size.i > 0 && same_bool(&documents->items[i], "has_syntax", true) &&
                same_int(&documents->items[i], "syntax_root", start.i)
            );
            if (i > 0) {
                REQUIRE(start.i > 0);
            }
        } else {
            REQUIRE(
                size.i == 0 && same_bool(&documents->items[i], "has_syntax", false) &&
                same_int(&documents->items[i], "syntax_root", -1)
            );
        }
        REQUIRE(same_int(&documents->items[i], "root", 0));
    }
    return true;
}
static bool snapshot_map(int b, int package, const int *expected, int count, const int *inverse)
{
    pin_value values[] = { snapshot_value, integer(package) };
    REQUIRE(compiler_fixture_call(
        &fixture,
        b,
        STATE,
        "DocumentsForPackage",
        values,
        2,
        500000,
        &map_storage[b][package],
        &mapped[b][package]
    ));
    map_calls++;
    const pin_value *payload;
    const pin_array *local, *global;
    REQUIRE(
        owned(&map_storage[b][package], &mapped[b][package], 0) &&
        tagged(b, &mapped[b][package], STATE, "DocumentMap", "Mapped", 2, &payload) &&
        array(&payload[0], PIN_I32, count, &local) &&
        array(&payload[1], PIN_I32, SNAPSHOT_FILES, &global)
    );
    for (int i = 0; i < count; i++) {
        REQUIRE(local->items[i].type == PIN_I32 && local->items[i].i == expected[i]);
    }
    for (int i = 0; i < SNAPSHOT_FILES; i++) {
        REQUIRE(global->items[i].type == PIN_I32 && global->items[i].i == inverse[i]);
    }
    return true;
}
static bool snapshot_view(int b, int file, bool parsed)
{
    pin_value values[] = { snapshot_value, integer(file) };
    REQUIRE(compiler_fixture_call(
        &fixture,
        b,
        STATE,
        "TreeForDocument",
        values,
        2,
        500000,
        &view_storage[b][file],
        &views[b][file]
    ));
    view_calls++;
    const pin_value *payload;
    REQUIRE(
        owned(&view_storage[b][file], &views[b][file], 0) &&
        tagged(b, &views[b][file], STATE, "DocumentTree", parsed ? "Parsed" : "Missing", parsed ? 2 : 0, &payload)
    );
    return true;
}
static bool snapshot_missing(int b, bool map, int index)
{
    pin_value values[] = { snapshot_value, integer(index) }, result;
    REQUIRE(compiler_fixture_call(
        &fixture,
        b,
        STATE,
        map ? "DocumentsForPackage" : "TreeForDocument",
        values,
        2,
        500000,
        &detached,
        &result
    ));
    if (map) {
        map_calls++;
    } else {
        view_calls++;
    }
    const pin_value *payload;
    REQUIRE(
        owned(&detached, &result, 0) &&
        tagged(b, &result, STATE, map ? "DocumentMap" : "DocumentTree", "Missing", 0, &payload)
    );
    arena_free(&detached);
    return true;
}
static bool snapshot_parse(const source_package_input *file, TSNode *target, int *target_index)
{
    ts_tree_delete(tree);
    tree = ts_parser_parse_string(parser, NULL, file->text, (uint32_t)file->size);
    REQUIRE(tree && !ts_node_has_error(ts_tree_root_node(tree)));
    snapshot_parses++;
    REQUIRE(
        compiler_fixture_nodes(ts_tree_root_node(tree), nodes, SNAPSHOT_NODES, DEPTH_CAP, &node_count)
    );
    if (node_count > snapshot_max_nodes) {
        snapshot_max_nodes = node_count;
    }
    *target_index = -1;
    for (int i = 0; i < node_count; i++) {
        snapshot_named[ts_node_is_named(nodes[i].node)] = true;
        snapshot_extra[ts_node_is_extra(nodes[i].node)] = true;
        if (!strcmp(ts_node_type(nodes[i].node), "parameter")) {
            *target = ts_node_child_by_field_name(nodes[i].node, "type", 4);
        }
    }
    REQUIRE(!ts_node_is_null(*target));
    for (int i = 0; i < node_count; i++) {
        if (ts_node_eq(nodes[i].node, *target)) {
            *target_index = i;
        }
    }
    REQUIRE(*target_index >= 0);
    return true;
}
static bool snapshot_read(
    int b,
    int file,
    int document,
    const source_package_input *source,
    TSNode target,
    int target_index,
    int retain_slot
)
{
    /* The complete direct Tree.Raw and both readings must match. The adapter
     * cannot substitute filtered children, token spelling, or a C reading. */
    const pin_value *view;
    pin_value values[5], direct_tree, direct_result, adapted_result, before;
    REQUIRE(
        tagged(b, &views[b][file], STATE, "DocumentTree", "Parsed", 2, &view) &&
        view[1].type == PIN_I32 && view[1].i == 0 &&
        signature_fixture_tree(&fixture, b, nodes, node_count, source->text, &input, &direct_tree) &&
        pin_value_equal(&view[0], &direct_tree)
    );
    facts_reset(false, false);
    script_signature_facts context = facts(false);
    context.named.import_count = 0;
    context.named.document_count = document + 1;
    REQUIRE(document >= 0 && document < 2);
    for (int i = 0; i <= document; i++) {
        const char *key = i == document ? source->path : "app/sibling.chibi";
        REQUIRE(strlen(key) < sizeof document_keys[i]);
        strcpy(document_keys[i], key);
        documents[i].key = document_keys[i];
    }
    REQUIRE(target_index >= 0);
    reading = script_signature_read(&oracle, &context, target, source->text, 1, document, true);
    if (source->text == snapshot_a) {
        REQUIRE(
            reading.publication_count == 1 && reading.type_count == 1 &&
            (reading.type.flags & SCRIPT_TYPE_TASK) && !reading.diagnostic_count
        );
    }
    if (source->text == snapshot_lib) {
        REQUIRE(reading.unit && !strcmp(reading.unit, "u/s") && !reading.diagnostic_count);
    }
    if (source->text == snapshot_b) {
        REQUIRE(
            reading.diagnostic_count > 0 && reading.diagnostic_count <= 4 &&
            reading.diagnostics[0].range.document == document
        );
    }
    REQUIRE(
        signature_fixture_environment(&fixture, b, &context, 2, &input, &values[0]) &&
        !compiler_fixture_snapshot(&baseline_storage, &before, &values[0])
    );
    values[1] = direct_tree;
    values[2] = integer(target_index);
    values[3] = integer(1);
    values[4] = integer(document);
    REQUIRE(
        compiler_fixture_call(&fixture, b, PACKAGE, "Signature", values, 5, 500000, &direct_storage, &direct_result)
    );
    reader_calls++;
    values[1] = view[0];
    REQUIRE(
        compiler_fixture_call(&fixture, b, PACKAGE, "Signature", values, 5, 500000, &detached, &adapted_result)
    );
    reader_calls++;
    arena_free(&input);
    arena_free(&view_storage[b][file]);
    views[b][file] = (pin_value){ 0 };
    const pin_value *updated;
    REQUIRE(
        owned(&direct_storage, &direct_result, 0) &&
        owned(&detached, &adapted_result, 0) &&
        pin_value_equal(&direct_result, &adapted_result) &&
        reading_value(b, &adapted_result, &before, &context, &reading, &updated)
    );
    if (retain_slot >= 0) {
        REQUIRE(retain_slot < 2);
        REQUIRE(
            !compiler_fixture_snapshot(&retained[b][retain_slot], &kept[b][retain_slot], &adapted_result) &&
            serialize(&kept[b][retain_slot], &golden[b][retain_slot])
        );
    }
    arena_free(&oracle);
    arena_free(&baseline_storage);
    arena_free(&detached);
    arena_free(&direct_storage);
    return true;
}
static bool snapshot_request(int request)
{
    static const int order[2][SNAPSHOT_FILES] = { { 0, 1, 2 }, { 1, 2, 0 } };
    /* Graph members are sorted by path: app/A.chibi precedes app/B.chibi
     * even when A is the later input file. */
    static const int members[2][2][2] = { { { 0, 2 }, { 1, -1 } },
                                          { { 0, -1 }, { 2, 1 } } };
    static const int counts[2][2] = { { 2, 1 }, { 1, 2 } };
    static const int inverse[2][2][SNAPSHOT_FILES] = { { { 0, -1, 1 }, { -1, 0, -1 } },
                                                       { { 0, -1, -1 }, { -1, 1, 0 } } };
    static const int package_for_file[2][SNAPSHOT_FILES] = { { 0, 1, 0 }, { 0, 1, 1 } };
    source_package_input files[SNAPSHOT_FILES];
    for (int i = 0; i < SNAPSHOT_FILES; i++) {
        files[i] = snapshot_sources[order[request][i]];
    }
    snapshot_cleanup();
    for (int b = 0; b < 2; b++) {
        REQUIRE(snapshot_build(b, files, SNAPSHOT_FILES, true));
        const pin_value *value, *graph;
        const pin_array *dependency_order;
        REQUIRE(
            tagged(b, &snapshot_value, SNAPSHOT, "Result", "Accepted", 5, &value) &&
            tagged(b, &value[4], "project:package_graph", "Graph", "Accepted", 6, &graph) &&
            array(&graph[3], PIN_I32, 2, &dependency_order)
        );
        REQUIRE(
            dependency_order->items[0].i == (request == 0 ? 1 : 0) &&
            dependency_order->items[1].i == (request == 0 ? 0 : 1)
        );
        for (int p = 0; p < 2; p++) {
            REQUIRE(
                snapshot_map(b, p, members[request][p], counts[request][p], inverse[request][p])
            );
        }
        for (int i = 0; i < SNAPSHOT_FILES; i++) {
            REQUIRE(snapshot_view(b, i, true));
        }
        REQUIRE(snapshot_missing(b, true, -1) && snapshot_missing(b, false, SNAPSHOT_FILES));
        /* Views and maps must own their outputs after the input snapshot dies. */
        arena_free(&snapshot_storage);
        snapshot_value = (pin_value){ 0 };
    }
    /* Each oracle tree is parsed once, shared read-only across both backends. */
    for (int i = 0; i < SNAPSHOT_FILES; i++) {
        TSNode target = { 0 };
        int target_index;
        REQUIRE(snapshot_parse(&files[i], &target, &target_index));
        for (int b = 0; b < 2; b++) {
            const pin_value *mapping;
            REQUIRE(
                tagged(b, &mapped[b][package_for_file[request][i]], STATE, "DocumentMap", "Mapped", 2, &mapping)
            );
            const pin_array *local = mapping[1].ptr;
            REQUIRE(
                local && local->items[i].type == PIN_I32 &&
                local->items[i].i == inverse[request][package_for_file[request][i]][i]
            );
            REQUIRE(snapshot_read(
                b,
                i,
                local->items[i].i,
                &files[i],
                target,
                target_index,
                request == 0 && i == 2   ? 0
                : request == 1 && i == 0 ? 1
                                         : -1
            ));
        }
    }
    return true;
}
TEST_CASE(snapshot_reader_interleaved_packages_and_reordered_documents_match)
{
    const source_package_input sources[] = {
        { "analysis_state/State.chibi",
          ANALYSIS_STATE_CHIBI_SOURCE,
          sizeof ANALYSIS_STATE_CHIBI_SOURCE - 1 },
        { "signature_reader/Reader.chibi",
          SIGNATURE_READER_CHIBI_SOURCE,
          sizeof SIGNATURE_READER_CHIBI_SOURCE - 1 },
        { "named_binding/Binding.chibi",
          NAMED_BINDING_CHIBI_SOURCE,
          sizeof NAMED_BINDING_CHIBI_SOURCE - 1 },
        { "type_constructors/Constructors.chibi",
          TYPE_CONSTRUCTORS_CHIBI_SOURCE,
          sizeof TYPE_CONSTRUCTORS_CHIBI_SOURCE - 1 },
        { "unit_algebra/Units.chibi",
          UNIT_ALGEBRA_CHIBI_SOURCE,
          sizeof UNIT_ALGEBRA_CHIBI_SOURCE - 1 },
        { "lexical_binding/Binding.chibi",
          LEXICAL_BINDING_CHIBI_SOURCE,
          sizeof LEXICAL_BINDING_CHIBI_SOURCE - 1 },
        { "syntax_diagnostics/Diagnostics.chibi",
          SYNTAX_CHIBI_SOURCE,
          sizeof SYNTAX_CHIBI_SOURCE - 1 },
        { "/sdk/core/strings/Strings.chibi",
          CORE_STRINGS_SOURCE,
          sizeof CORE_STRINGS_SOURCE - 1 },
        { "source_snapshot/Snapshot.chibi",
          PACKAGE_SNAPSHOT_CHIBI_SOURCE,
          sizeof PACKAGE_SNAPSHOT_CHIBI_SOURCE - 1 },
        { "compiler_syntax/Syntax.chibi",
          COMPILER_SYNTAX_CHIBI_SOURCE,
          sizeof COMPILER_SYNTAX_CHIBI_SOURCE - 1 },
        { "package_routes/Routes.chibi",
          PACKAGE_ROUTES_CHIBI_SOURCE,
          sizeof PACKAGE_ROUTES_CHIBI_SOURCE - 1 },
        { "package_syntax/Extract.chibi",
          PACKAGE_SYNTAX_CHIBI_SOURCE,
          sizeof PACKAGE_SYNTAX_CHIBI_SOURCE - 1 },
        { "package_graph/Graph.chibi",
          PACKAGE_GRAPH_CHIBI_SOURCE,
          sizeof PACKAGE_GRAPH_CHIBI_SOURCE - 1 },
        { "package_paths/Paths.chibi",
          PACKAGE_PATHS_CHIBI_SOURCE,
          sizeof PACKAGE_PATHS_CHIBI_SOURCE - 1 },
    };
    size_t total = 0;
    for (int i = 0; i < 14; i++) {
        /* Keep the reader and snapshot fixture bounds. Core strings uses
         * production snapshot limits and full retention checks instead. */
        if (i != 7) {
            REQUIRE(sources[i].size < (i > 5 ? 16384 : 48 * 1024));
        }
        total += sources[i].size;
    }
    REQUIRE(total < 160 * 1024 && sizeof ANALYSIS_STATE_CHIBI_SOURCE < 8192);
    const native_definition_pack packs[] = { COMPILER_JSON_DEFINITION_PACK,
                                             COMPILER_SYNTAX_DEFINITION_PACK };
    REQUIRE(compiler_fixture_prepare_packs(&fixture, sources, 14, packs, 2));
    parser = ts_parser_new();
    REQUIRE(parser && ts_parser_set_language(parser, tree_sitter_chibi()));
    REQUIRE(snapshot_request(0) && snapshot_request(1));
    REQUIRE(
        snapshot_calls == 4 && map_calls == 12 && view_calls == 16 &&
        reader_calls == 24 && snapshot_parses == 6 && snapshot_named[0] &&
        snapshot_named[1] && snapshot_extra[0] && snapshot_extra[1]
    );
    return true;
}
TEST_CASE(snapshot_reader_rejected_prefix_keeps_only_parsed_document)
{
    REQUIRE(fixture.owners[0] && fixture.owners[1] && parser);
    snapshot_cleanup();
    source_package_input files[] = { snapshot_sources[0], snapshot_sources[1] };
    files[1].path = "/outside/Lib.chibi";
    for (int b = 0; b < 2; b++) {
        REQUIRE(
            snapshot_build(b, files, 2, false) && snapshot_view(b, 0, true) &&
            snapshot_view(b, 1, false) && snapshot_missing(b, true, 0)
        );
        arena_free(&snapshot_storage);
        snapshot_value = (pin_value){ 0 };
    }
    TSNode target = { 0 };
    int target_index;
    REQUIRE(snapshot_parse(&files[0], &target, &target_index));
    for (int b = 0; b < 2; b++) {
        pin_value direct;
        const pin_value *view;
        REQUIRE(
            tagged(b, &views[b][0], STATE, "DocumentTree", "Parsed", 2, &view) &&
            signature_fixture_tree(&fixture, b, nodes, node_count, files[0].text, &input, &direct) &&
            pin_value_equal(&view[0], &direct) && view[1].type == PIN_I32 &&
            view[1].i == 0
        );
        arena_free(&input);
    }
    REQUIRE(
        snapshot_calls == 6 && map_calls == 14 && view_calls == 20 &&
        reader_calls == 24 && snapshot_parses == 7
    );
    printf("Snapshot reader:3 requests,64 calls,7 C trees,14 raw parses,28 preparation parses; max %d nodes\n", snapshot_max_nodes);
    return true;
}
void test_language_snapshot_reader_chibi()
{
    snapshot_calls = map_calls = view_calls = reader_calls = snapshot_parses = snapshot_max_nodes = 0;
    memset(snapshot_named, 0, sizeof snapshot_named);
    memset(snapshot_extra, 0, sizeof snapshot_extra);
    pu_setup(NULL, snapshot_cleanup);
    RUN_TEST_CASE(snapshot_reader_interleaved_packages_and_reordered_documents_match);
    RUN_TEST_CASE(snapshot_reader_rejected_prefix_keeps_only_parsed_document);
    RUN_TEST_CASE(signature_reader_readings_survive_all_producer_owners);
    pu_clear_setup();
    snapshot_cleanup();
    compiler_fixture_free(&fixture);
    ts_parser_delete(parser);
    parser = NULL;
    for (int b = 0; b < 2; b++) {
        for (int i = 0; i < 2; i++) {
            arena_free(&retained[b][i]);
            kept[b][i] = (pin_value){ 0 };
            free(golden[b][i]);
            golden[b][i] = NULL;
        }
    }
}
