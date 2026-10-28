#include "union_members_chibi.h"

#include "compiler_fixture.h"
#include "language/union_members.h"
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
#include "union_members_chibi_source.h"
#include "unit_algebra_chibi_source.h"

#include <stdio.h>
#include <string.h>

/* Sixteen Build requests and eight raw Validate requests, both backends:
 * 48 calls, 16 module-preparation parses plus 16 raw-source parses. Source
 * calls have 2m instructions, raw validation 16m. Raw sources <=512 bytes,
 * trees <=256 nodes/depth32; initial source tables <=2 symbols/definitions,
 * one scope, no functions/imports/capabilities. Raw validation <=257
 * definitions/fields, two variants, four roots, 520 types/1024 IDs. The longest
 * walk performs <=257*257 nominal comparisons, <=514 state-array writes,
 * and <=258 traversal calls. No body/default execution or IO. */
enum
{
    SOURCE_CASES = 16,
    RAW_CASES = 8,
    NODES = 256,
    POOL = 1024
};
static const char PACKAGE[] = "project:union_members";
static const char NAMED[] = "project:named_binding";
static const char *const sources[SOURCE_CASES] = {
    "Facts :: library { Choice::union{Ready} }",
    "Facts :: library { Choice::union{\"alpha\"|\"beta\"} }",
    "Facts :: library { Choice::union{Payload(value:int,name:string)} }",
    "Facts :: library { Choice::union{\"same\"|\"same\"|\"same\"} }",
    "Facts :: library { Choice::union{A(value:int)|A(value:string)} }",
    "Facts :: library { Choice::union{Payload(value:^int)} }",
    "Facts :: library { Choice::union{Payload(value)} }",
    "Facts :: library { Choice::union{Payload(value:int=Unknown())} }",
    "Facts :: library { "
    "Choice::union{Payload(first:int|string,second:int|string)} }",
    "Facts :: library { A::union{Next(value:B)} B::union{Next(value:A)} }",
    "Facts :: library { Choice::union{Next(values:Choice[])} }",
    "Facts :: library { Choice::union{Next(value:task(Choice))} }",
    "Facts :: library { Choice::union{Ready} }",
    "Facts :: library { Choice::union{Ready} }",
    "Facts :: library { Choice::union{\"\\u0000\"} }",
    "Facts :: library { A::union{Ready|Payload(value:int)} "
    "B::union{Box(first:A|bool,second:A|bool)} }",
};
static compiler_fixture fixture;
static arena input, detached, oracle, retained[2][2];
static pin_value kept[2][2];
static script_syntax *syntax;
static compiler_fixture_node nodes[NODES];
static int node_count, calls;
static void cleanup(void)
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
static bool collection_field(const pin_value *row, uint64_t expected)
{
    pin_value value;
    REQUIRE(read(row, "collection", &value) && value.type == PIN_U64 && value.u64 == expected);
    return true;
}
static bool bool_field(const pin_value *row, const char *name, bool expected)
{
    pin_value value;
    REQUIRE(read(row, name, &value) && value.type == PIN_BOOL && value.b == expected);
    return true;
}
static bool text_field(const pin_value *row, const char *name, pin_string expected)
{
    pin_value value;
    REQUIRE(read(row, name, &value) && value.type == PIN_STR);
    pin_string actual = pin_text(value);
    REQUIRE(actual.length == expected.length);
    REQUIRE(!actual.length || !memcmp(actual.bytes, expected.bytes, actual.length));
    return true;
}
static bool string_field(const pin_value *row, const char *name, const char *expected)
{
    expected = expected ? expected : "";
    return text_field(row, name, (pin_string){ .bytes = expected, .length = (uint32_t)strlen(expected) });
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
    REQUIRE(actual >= 0 && actual <= 1024 && (count < 0 || actual == count));
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
        id_field(row, "parameter_slot", ids, expected.parameter) &&
        collection_field(row, expected.collection)
    );
    return true;
}
static bool rows_in(int b, const char *package, const char *name, int count, pin_value *out)
{
    const pin_record *shape = compiler_fixture_record(&fixture, b, package, name, &input);
    REQUIRE(shape && count >= 0 && count <= 1024);
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
static bool owned(const arena *storage, const pin_value *value, int depth)
{
    REQUIRE(depth <= 20);
    if (value->container == PIN_CONTAINER_ARRAY) {
        if (!value->ptr) {
            return true;
        }
        const pin_array *a = value->ptr;
        REQUIRE(arena_contains(storage, a) && a->count >= 0 && a->count <= 1024);
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
static bool rows(int b, const char *name, int count, pin_value *out)
{
    return rows_in(b, PACKAGE, name, count, out);
}
static pin_value integers(const int *values, int count)
{
    pin_array *a = pin_array_snapshot_allocate(&input, PIN_I32, NULL, count);
    for (int i = 0; i < count; i++) {
        a->items[i] = integer(values ? values[i] : 0);
    }
    return (pin_value){ .type = PIN_I32, .container = PIN_CONTAINER_ARRAY, .ptr = a };
}
typedef struct
{
    script_type types[520];
    uint64_t ids[POOL];
    int nt, ni;
} pools;
static int pool_id(pools *p, uint64_t id)
{
    if (!id) {
        return -1;
    }
    for (int i = 0; i < p->ni; i++) {
        if (p->ids[i] == id) {
            return i;
        }
    }
    if (p->ni == POOL) {
        return -2;
    }
    p->ids[p->ni] = id;
    return p->ni++;
}
static bool same_type(script_type a, script_type b)
{
    return a.storage == b.storage && a.container == b.container &&
           a.flags == b.flags && a.nominal == b.nominal &&
           a.parameter == b.parameter && a.collection == b.collection;
}
static int pool_type(pools *p, script_type type)
{
    for (int i = 0; i < p->nt; i++) {
        if (same_type(p->types[i], type)) {
            return i;
        }
    }
    if (p->nt == 520) {
        return -1;
    }
    p->types[p->nt] = type;
    return p->nt++;
}
static bool pack_pools(int b, pools *p, pin_value *types, pin_value *ids)
{
    REQUIRE(rows_in(b, NAMED, "Type", p->nt, types));
    pin_array *a = types->ptr;
    for (int i = 0; i < p->nt; i++) {
        script_type t = p->types[i];
        int n = pool_id(p, t.nominal), r = pool_id(p, t.parameter);
        REQUIRE(n >= -1 && r >= -1);
        REQUIRE(
            put(&a->items[i], "storage", integer(t.storage)) &&
            put(&a->items[i], "container", integer(t.container)) &&
            put(&a->items[i], "flags", integer(bits(t.flags))) &&
            put(&a->items[i], "nominal_slot", integer(n)) &&
            put(&a->items[i], "parameter_slot", integer(r)) &&
            put(&a->items[i], "collection", (pin_value){ .type = PIN_U64, .u64 = t.collection })
        );
    }
    pin_array *id = pin_array_snapshot_allocate(&input, PIN_U64, NULL, p->ni);
    for (int i = 0; i < p->ni; i++) {
        id->items[i] = (pin_value){ .type = PIN_U64, .u64 = p->ids[i] };
    }
    *ids = (pin_value){ .type = PIN_U64, .container = PIN_CONTAINER_ARRAY, .ptr = id };
    return true;
}
/* Mechanical graph expansion: every original definition, field and variant
 * stays in catalog order. There is no ownership-edge selection here. */
static bool graph(int b, const script_catalog *catalog, pools *p, pin_value fields[5])
{
    int nf = 0, nv = 0;
    for (int i = 0; i < catalog->type_count; i++) {
        const script_catalog_type *d = &catalog->types[i];
        nf += d->field_count;
        nv += d->variant_count;
        for (int j = 0; j < d->variant_count; j++) {
            nf += d->variants[j].field_count;
        }
    }
    REQUIRE(catalog->type_count <= 257 && nf <= 257 && nv <= 16);
    REQUIRE(
        rows(b, "Definition", catalog->type_count, &fields[2]) &&
        rows(b, "Variant", nv, &fields[3]) && rows(b, "Field", nf, &fields[4])
    );
    pin_array *defs = fields[2].ptr, *variants = fields[3].ptr,
              *payload = fields[4].ptr;
    int fi = 0, vi = 0;
    for (int i = 0; i < catalog->type_count; i++) {
        const script_catalog_type *d = &catalog->types[i];
        pin_value *row = &defs->items[i];
        int id = pool_id(p, d->id);
        REQUIRE(id >= -1);
        REQUIRE(
            put(row, "id_slot", integer(id)) && put(row, "field_start", integer(fi)) &&
            put(row, "field_count", integer(d->field_count)) &&
            put(row, "variant_start", integer(vi)) &&
            put(row, "variant_count", integer(d->variant_count))
        );
        for (int j = 0; j < d->field_count; j++) {
            int t = pool_type(p, d->fields[j].type);
            REQUIRE(t >= 0 && put(&payload->items[fi++], "type", integer(t)));
        }
        for (int j = 0; j < d->variant_count; j++) {
            const script_catalog_variant *v = &d->variants[j];
            row = &variants->items[vi++];
            int t = pool_type(p, v->type);
            REQUIRE(t >= 0);
            REQUIRE(
                put(row, "type", integer(t)) &&
                put(row, "tagged", boolean(v->tagged)) &&
                put(row, "field_start", integer(fi)) &&
                put(row, "field_count", integer(v->field_count))
            );
            for (int k = 0; k < v->field_count; k++) {
                t = pool_type(p, v->fields[k].type);
                REQUIRE(t >= 0 && put(&payload->items[fi++], "type", integer(t)));
            }
        }
    }
    REQUIRE(pack_pools(b, p, &fields[0], &fields[1]));
    return true;
}
static bool input_environment(int b, const script_signature_facts *facts, pin_value *out)
{
    pin_value f[8], g[5];
    pools p = { 0 };
    REQUIRE(
        signature_fixture_environment(&fixture, b, facts, 1, &input, &f[0]) &&
        graph(b, facts->named.catalog, &p, g)
    );
    for (int i = 0; i < 5; i++) {
        f[i + 1] = g[i];
    }
    REQUIRE(
        rows(b, "Capability", 0, &f[6]) && rows(b, "Field", 0, &f[7]) &&
        tagged(b, "Environment", "Tables", f, 8, out)
    );
    return true;
}
static bool typed_value(const pin_value *value, script_type expected)
{
    const pin_union *u;
    REQUIRE(union_data(value, 5, &u));
    REQUIRE(
        u->values[0].type == PIN_I32 && u->values[0].i == (int)expected.storage &&
        u->values[1].type == PIN_I32 && u->values[1].i == (int)expected.container &&
        u->values[2].type == PIN_I32 && u->values[2].i == bits(expected.flags) &&
        u->values[3].type == PIN_U64 && u->values[3].u64 == expected.nominal &&
        u->values[4].type == PIN_U64 && u->values[4].u64 == expected.parameter
    );
    return true;
}
static bool typed_field(
    const pin_value *row,
    const char *field,
    const pin_value *types,
    const pin_value *ids,
    script_type expected
)
{
    int i;
    REQUIRE(field_number(row, field, &i) && type_at(types, ids, i, expected));
    return true;
}
static bool diagnostics(const pin_value *value, const script_diagnostic *expected, int count)
{
    const pin_array *a;
    REQUIRE(list(value, PIN_STRUCT, count, &a));
    for (int i = 0; i < count; i++) {
        REQUIRE(
            range_fields(&a->items[i], expected[i].range) &&
            string_field(&a->items[i], "message", expected[i].message)
        );
    }
    return true;
}
static bool compare_fields(
    const pin_value *value,
    int start,
    const script_catalog_field *fields,
    int count,
    const pin_value *types,
    const pin_value *ids,
    bool declared
)
{
    const pin_array *a;
    REQUIRE(
        list(value, PIN_STRUCT, -1, &a) && start >= 0 &&
        (!count || (a && start + count <= a->count))
    );
    for (int i = 0; i < count; i++) {
        const pin_value *row = &a->items[start + i];
        const script_catalog_field *f = &fields[i];
        REQUIRE(typed_field(row, "type", types, ids, f->type));
        if (declared) {
            REQUIRE(
                string_field(row, "name", f->name) &&
                id_field(row, "id_slot", ids, f->id) &&
                int_field(row, "offset", (int)f->offset) &&
                bool_field(row, "writable", f->writable) &&
                bool_field(row, "has_default", f->has_default)
            );
        }
    }
    return true;
}
static bool compare_graph(const pin_union *env, const script_catalog *catalog)
{
    const pin_array *defs, *variants, *empty;
    REQUIRE(
        list(&env->values[6], PIN_STRUCT, 0, &empty) &&
        list(&env->values[7], PIN_STRUCT, 0, &empty)
    );
    REQUIRE(
        list(&env->values[3], PIN_STRUCT, catalog->type_count, &defs) &&
        list(&env->values[4], PIN_STRUCT, -1, &variants)
    );
    for (int i = 0; i < catalog->type_count; i++) {
        const script_catalog_type *d = &catalog->types[i];
        const pin_value *row = &defs->items[i];
        int fs, vs;
        REQUIRE(
            id_field(row, "id_slot", &env->values[2], d->id) &&
            int_field(row, "field_count", d->field_count) &&
            int_field(row, "variant_count", d->variant_count) &&
            field_number(row, "field_start", &fs) &&
            field_number(row, "variant_start", &vs) &&
            compare_fields(&env->values[5], fs, d->fields, d->field_count, &env->values[1], &env->values[2], false)
        );
        REQUIRE(
            !d->variant_count ||
            (variants && vs >= 0 && vs + d->variant_count <= variants->count)
        );
        for (int j = 0; j < d->variant_count; j++) {
            const script_catalog_variant *v = &d->variants[j];
            row = &variants->items[vs + j];
            REQUIRE(
                typed_field(row, "type", &env->values[1], &env->values[2], v->type) &&
                bool_field(row, "tagged", v->tagged) &&
                int_field(row, "field_count", v->field_count) &&
                field_number(row, "field_start", &fs) &&
                compare_fields(
                    &env->values[5],
                    fs,
                    v->fields,
                    v->field_count,
                    &env->values[1],
                    &env->values[2],
                    false
                )
            );
        }
    }
    return true;
}
static bool compare_replaced(const pin_union *u, const script_catalog_type *d)
{
    const pin_array *variants;
    REQUIRE(
        u->count == 5 && u->values[0].type == PIN_U64 && u->values[0].u64 == d->id &&
        list(&u->values[1], PIN_STRUCT, d->variant_count, &variants)
    );
    for (int i = 0; i < d->variant_count; i++) {
        const script_catalog_variant *v = &d->variants[i];
        const pin_value *r = &variants->items[i];
        int fs;
        REQUIRE(
            string_field(r, "name", v->name) &&
            id_field(r, "tag_slot", &u->values[4], v->tag) &&
            typed_field(r, "type", &u->values[3], &u->values[4], v->type) &&
            bool_field(r, "tagged", v->tagged) &&
            bool_field(r, "is_literal", v->is_literal) &&
            text_field(
                r,
                "literal",
                v->is_literal ? pin_text(v->literal)
                              : (pin_string){ .bytes = "", .length = 0 }
            ) &&
            int_field(r, "field_count", v->field_count) &&
            field_number(r, "field_start", &fs) &&
            compare_fields(&u->values[2], fs, v->fields, v->field_count, &u->values[3], &u->values[4], true)
        );
    }
    return true;
}
/* Added plans preserve both their published semantic definition and raw
 * selection provenance. Constructor metadata is materialized by the C oracle. */
static bool compare_added(const pin_union *update, const script_catalog_type *d, const script_catalog *before)
{
    const pin_union *p;
    const pin_array *a;
    REQUIRE(update->count == 1 && union_data(&update->values[0], 16, &p));
    REQUIRE(
        p->values[1].type == PIN_BOOL && p->values[1].b && p->values[2].type == PIN_U64 &&
        p->values[2].u64 == d->id && p->values[3].type == PIN_STR &&
        !strcmp(pin_text(p->values[3]).bytes, d->name) &&
        typed_value(&p->values[4], d->type) &&
        typed_value(&p->values[11], d->element_type) && p->values[12].type == PIN_U64 &&
        p->values[12].u64 == d->element_type.collection &&
        p->values[5].type == PIN_I32 && p->values[5].i == (int)d->size &&
        p->values[6].type == PIN_BOOL && p->values[6].b == d->internal &&
        list(&p->values[7], PIN_STRUCT, d->variant_count, &a)
    );
    if (d->variant_count) {
        REQUIRE(typed_value(&p->values[0], d->type));
    } else {
        script_type task = { .storage = PIN_TASK,
                             .flags = SCRIPT_TYPE_TASK,
                             .parameter = d->id };
        REQUIRE(typed_value(&p->values[0], task));
    }
    for (int i = 0; i < d->variant_count; i++) {
        const pin_value *r = &a->items[i];
        const script_catalog_variant *v = &d->variants[i];
        int ci, vi, mi;
        REQUIRE(
            int_field(r, "storage", v->type.storage) &&
            int_field(r, "container", v->type.container) &&
            int_field(r, "flags", bits(v->type.flags)) &&
            collection_field(r, v->type.collection) &&
            id_field(r, "nominal_slot", &p->values[9], v->type.nominal) &&
            id_field(r, "parameter_slot", &p->values[9], v->type.parameter) &&
            id_field(r, "tag_slot", &p->values[9], v->tag) &&
            field_number(r, "catalog_type", &ci) &&
            field_number(r, "variant", &vi) && field_number(r, "member", &mi)
        );
        if (v->tagged) {
            REQUIRE(
                ci == 0 && vi >= 0 && vi < before->types[0].variant_count &&
                mi == -1 && before->types[0].variants[vi].tag == v->tag
            );
        } else {
            REQUIRE(ci == -1 && vi == -1 && mi == (v->type.storage == PIN_I32 ? 0 : 1));
        }
    }
    REQUIRE(
        list(&p->values[8], PIN_STRUCT, d->field_count, &a) &&
        diagnostics(&p->values[10], NULL, 0)
    );
    return true;
}
static bool compare_reading(const pin_value *value, const script_catalog *catalog)
{
    const pin_union *r, *names, *ctor;
    const pin_array *entries, *defs, *variants;
    REQUIRE(
        union_data(value, 3, &r) && union_data(&r->values[0], 15, &names) &&
        union_data(&r->values[1], 8, &ctor) &&
        list(&names->values[3], PIN_STRUCT, catalog->type_count, &entries) &&
        list(&ctor->values[2], PIN_STRUCT, catalog->type_count, &defs) &&
        list(&ctor->values[3], PIN_STRUCT, -1, &variants)
    );
    for (int i = 0; i < catalog->type_count; i++) {
        const script_catalog_type *d = &catalog->types[i];
        const pin_value *e = &entries->items[i], *row = &defs->items[i];
        int start;
        REQUIRE(
            string_field(e, "name", d->name) &&
            id_field(e, "id_slot", &names->values[2], d->id) &&
            typed_field(e, "type", &names->values[1], &names->values[2], d->type) &&
            bool_field(e, "internal", d->internal) &&
            id_field(row, "id_slot", &ctor->values[1], d->id) &&
            int_field(row, "variant_count", d->variant_count) &&
            field_number(row, "variant_start", &start)
        );
        REQUIRE(
            !d->variant_count ||
            (variants && start >= 0 && start + d->variant_count <= variants->count)
        );
        for (int j = 0; j < d->variant_count; j++) {
            REQUIRE(
                id_field(
                    &variants->items[start + j],
                    "tag_slot",
                    &ctor->values[1],
                    d->variants[j].tag
                ) &&
                typed_field(
                    &variants->items[start + j],
                    "type",
                    &ctor->values[0],
                    &ctor->values[1],
                    d->variants[j].type
                )
            );
        }
    }
    return true;
}
static bool compare_members(
    int b,
    const pin_value *value,
    const script_union_member_result *expected,
    const script_catalog *initial
)
{
    const pin_union *u, *env, *reading, *names;
    const pin_array *updates, *uses;
    const script_catalog_variant *built = variant(b, "Members", "Built");
    REQUIRE(
        built && union_data(value, 4, &u) && u->tag == built->tag &&
        union_data(&u->values[0], 8, &env) &&
        list(&u->values[1], PIN_UNION, expected->update_count, &updates) &&
        diagnostics(&u->values[2], expected->diagnostics, expected->diagnostic_count) &&
        list(&u->values[3], PIN_STRUCT, expected->use_count, &uses) &&
        union_data(&env->values[0], 3, &reading) &&
        union_data(&reading->values[0], 15, &names)
    );
    script_catalog_type final[8];
    int count = initial->type_count;
    REQUIRE(count <= 2);
    memcpy(final, initial->types, (size_t)count * sizeof *final);
    for (int i = 0; i < expected->update_count; i++) {
        const script_union_member_update *e = &expected->updates[i];
        const pin_union *update = updates->items[i].ptr;
        REQUIRE(updates->items[i].type == PIN_UNION && update);
        const script_catalog_variant *tag = variant(b, "Update", e->append ? "Added" : "Replaced");
        REQUIRE(tag && update->tag == tag->tag);
        script_catalog before = { .types = final, .type_count = count };
        if (e->append) {
            REQUIRE(compare_added(update, &e->definition, &before) && count < 8);
            final[count++] = e->definition;
        } else {
            REQUIRE(compare_replaced(update, &e->definition));
            bool found = false;
            for (int j = 0; j < count; j++) {
                if (final[j].id == e->definition.id) {
                    final[j] = e->definition;
                    found = true;
                }
            }
            REQUIRE(found);
        }
    }
    for (int i = 0; i < expected->use_count; i++) {
        const script_use *e = &expected->uses[i];
        REQUIRE(
            int_field(&uses->items[i], "kind", e->kind) &&
            int_field(&uses->items[i], "symbol", e->symbol) &&
            id_field(&uses->items[i], "member_slot", &names->values[2], e->member) &&
            range_fields(&uses->items[i], e->range)
        );
    }
    script_catalog catalog = { .types = final, .type_count = count };
    REQUIRE(compare_graph(env, &catalog) && compare_reading(&env->values[0], &catalog));
    return true;
}
static bool witness(int index, const script_union_member_result *r)
{
    static const int counts[SOURCE_CASES] = { 1, 1, 1, 1, 1, 1, 1, 1,
                                              2, 2, 1, 2, 0, 0, 1, 3 };
    static const int diagnostics_count[SOURCE_CASES] = { 0, 0, 0, 3, 1, 1, 1, 0,
                                                         0, 1, 1, 0, 0, 0, 0, 0 };
    REQUIRE(r->update_count == counts[index] && r->diagnostic_count == diagnostics_count[index]);
    if (index == 0) {
        REQUIRE(
            r->updates[0].definition.field_count == 1 &&
            r->updates[0].definition.fields[0].id == UINT64_MAX - 200 &&
            r->updates[0].definition.fields[0].offset == 7 &&
            !strcmp(r->updates[0].definition.fields[0].name, "retained_header_field")
        );
    }
    if (index == 3) {
        REQUIRE(
            r->updates[0].definition.variant_count == 3 &&
            !strcmp(pin_text(r->updates[0].definition.variants[2].literal).bytes, "same")
        );
    }
    if (index == 2 || index == 7) {
        const script_catalog_variant *v = &r->updates[0].definition.variants[0];
        REQUIRE(
            v->field_count == (index == 2 ? 2 : 1) && !v->fields[0].has_default &&
            v->fields[0].offset == 0 && !strcmp(v->fields[0].name, "value")
        );
        if (index == 2) {
            REQUIRE(v->fields[1].offset == 1 && !strcmp(v->fields[1].name, "name"));
        }
    }
    if (index == 8) {
        REQUIRE(
            r->updates[0].append && !r->updates[1].append &&
            r->updates[0].definition.variant_count == 2
        );
    }
    if (index == 9 || index == 10) {
        REQUIRE(strstr(r->diagnostics[0].message, "Recursive owned"));
    }
    if (index == 14) {
        REQUIRE(r->diagnostic_count == 0 && r->updates[0].definition.variant_count == 1);
        const script_catalog_variant *literal = &r->updates[0].definition.variants[0];
        REQUIRE(literal->is_literal && literal->literal.type == PIN_STR);
        pin_string text = pin_text(literal->literal);
        REQUIRE(text.length == 1 && text.bytes[0] == 0);
        REQUIRE(literal->tag != script_catalog_id("union-literal", ""));
        const char label[] = { '"', '\\', 'u', '0', '0', '0', '0', '"', 0 };
        REQUIRE(!strcmp(literal->name, label));
    }
    if (index == 15) {
        REQUIRE(
            !r->updates[0].append && r->updates[1].append &&
            !r->updates[2].append && r->updates[1].definition.variant_count == 3 &&
            r->updates[2].definition.variants[0].field_count == 2
        );
        const script_catalog_field *f = r->updates[2].definition.variants[0].fields;
        REQUIRE(
            same_type(f[0].type, f[1].type) &&
            f[0].type.nominal == r->updates[1].definition.id
        );
        int tagged_count = 0, payload_count = 0;
        for (int j = 0; j < 3; j++) {
            tagged_count += r->updates[1].definition.variants[j].tagged;
            payload_count += r->updates[1].definition.variants[j].field_count;
        }
        REQUIRE(tagged_count == 2 && payload_count == 1);
    }
    return true;
}
static bool ready(void)
{
    REQUIRE(
        fixture.owners[0] && fixture.owners[1] &&
        authoring_packages_valid(fixture.owners[0]) &&
        authoring_packages_valid(fixture.owners[1])
    );
    return true;
}
static bool source_case(int index)
{
    REQUIRE(ready());
    cleanup();
    REQUIRE(strlen(sources[index]) <= 512);
    syntax = script_parse(sources[index]);
    REQUIRE(
        syntax && !syntax->diagnostic_count &&
        compiler_fixture_nodes(ts_tree_root_node(syntax->tree), nodes, NODES, 32, &node_count)
    );
    int body = -1, count = 0;
    script_symbol symbols[2] = { 0 };
    script_catalog_type definitions[2] = { 0 };
    for (int i = 0; i < node_count; i++) {
        if (!strcmp(ts_node_type(nodes[i].node), "declaration_body")) {
            body = i;
        }
        if (strcmp(ts_node_type(nodes[i].node), "union_declaration")) {
            continue;
        }
        TSNode name = ts_node_child_by_field_name(nodes[i].node, "name", 4);
        REQUIRE(count < 2 && !ts_node_is_null(name));
        size_t start = ts_node_start_byte(name), size = ts_node_end_byte(name) - start;
        char *copy = arena_alloc(&oracle, size + 1);
        memcpy(copy, sources[index] + start, size);
        copy[size] = 0;
        uint64_t id = UINT64_MAX - 100 - (uint64_t)count;
        script_type t = { .storage = PIN_UNION, .flags = SCRIPT_TYPE_UNION, .nominal = id };
        symbols[count] = (script_symbol){ .id = id,
                                          .name = copy,
                                          .kind = SCRIPT_TYPE,
                                          .type = t,
                                          .scope = 0,
                                          .initializer = -1,
                                          .function = -1,
                                          .declaration = script_node_range(name) };
        definitions[count] = (script_catalog_type){ .id = id,
                                                    .name = copy,
                                                    .source_name = copy,
                                                    .type = t,
                                                    .size = sizeof(pin_union) };
        count++;
    }
    REQUIRE(body >= 0 && count >= 1);
    script_catalog_field existing_field = {
        .id = UINT64_MAX - 200,
        .name = "retained_header_field",
        .type = { .storage = PIN_I32 },
        .offset = 7,
    };
    if (index == 0) {
        definitions[0].fields = &existing_field;
        definitions[0].field_count = 1;
    }
    if (index == 12) {
        symbols[0].declaration.start--;
    }
    if (index == 13) {
        symbols[0].kind = SCRIPT_LOCAL;
    }
    script_scope scope = { .parent = -1,
                           .function = -1,
                           .range = script_node_range(nodes[0].node) };
    script_catalog catalog = { .types = definitions, .type_count = count };
    script_signature_facts facts = {
        .named = { .lexical = { .scopes = &scope,
                                .symbols = symbols,
                                .symbol_count = count,
                                .identity = "legacy:members" },
                   .catalog = &catalog,
                   .own_types = definitions,
                   .own_type_count = count,
                   .package_scope = -1 },
        .pointer_size = sizeof(void *)
    };
    script_union_member_result
        result = script_union_members(&oracle, &facts, nodes[body].node, sources[index], 0, 0);
    REQUIRE(witness(index, &result));
    for (int b = 0; b < 2; b++) {
        arena_free(&input);
        arena_free(&detached);
        pin_value args[5], out;
        REQUIRE(
            input_environment(b, &facts, &args[0]) &&
            signature_fixture_tree(&fixture, b, nodes, node_count, sources[index], &input, &args[1])
        );
        args[2] = integer(body);
        args[3] = integer(0);
        args[4] = integer(0);
        REQUIRE(
            compiler_fixture_call(&fixture, b, PACKAGE, "Build", args, 5, 2000000, &detached, &out)
        );
        calls++;
        arena_free(&input);
        REQUIRE(owned(&detached, &out, 0) && compare_members(b, &out, &result, &catalog));
        if (index == 15) {
            REQUIRE(
                !compiler_fixture_snapshot(&retained[0][b], &kept[0][b], &out) &&
                owned(&retained[0][b], &kept[0][b], 0)
            );
        }
    }
    return true;
}
static bool raw_case(int index)
{
    REQUIRE(ready());
    cleanup();
    script_catalog_type definitions[257] = { 0 };
    script_catalog_field fields[257] = { 0 };
    script_catalog_variant variants[2] = { 0 };
    script_type roots[4] = { 0 };
    unsigned char states[257] = { 0 };
    int count = index == 0 ? 0
              : index == 1 ? 2
              : index == 2 ? 1
              : index == 3 ? 2
              : index == 4 ? 3
              : index == 6 ? 257
                           : 256;
    for (int i = 0; i < count; i++) {
        definitions[i] = (script_catalog_type){
            .id = UINT64_MAX - 1000 - (uint64_t)i,
            .type = { .storage = PIN_UNION,
                      .flags = SCRIPT_TYPE_UNION,
                      .nominal = UINT64_MAX - 1000 - (uint64_t)i }
        };
    }
    int nr = 1;
    roots[0] = count ? definitions[0].type
                     : (script_type){ .storage = PIN_STRUCT, .nominal = 999 };
    if (index == 0) {
        nr = 4;
        roots[1] = (script_type){ .storage = PIN_F64 };
        roots[2] = (script_type){ .storage = PIN_HANDLE, .nominal = 999 };
        roots[3] = (script_type){ .storage = PIN_TASK, .parameter = 999 };
    }
    if (index == 1) {
        definitions[1].type.storage = PIN_STRUCT;
        definitions[1].type.flags = 0;
        fields[0].type = definitions[1].type;
        fields[1].type = (script_type){ .storage = PIN_HANDLE,
                                        .nominal = definitions[0].id };
        fields[2].type = (script_type){ .storage = PIN_TASK,
                                        .parameter = definitions[0].id };
        variants[0] = (script_catalog_variant){ .type = definitions[0].type,
                                                .tagged = true,
                                                .fields = &fields[0],
                                                .field_count = 1 };
        variants[1] = (script_catalog_variant){ .type = definitions[1].type };
        definitions[0].variants = variants;
        definitions[0].variant_count = 2;
        definitions[1].fields = &fields[1];
        definitions[1].field_count = 2;
        nr = 2;
        roots[1] = definitions[1].type;
    }
    if (index == 2) {
        fields[0].type = definitions[0].type;
        fields[0].type.container = PIN_CONTAINER_ARRAY;
        definitions[0].fields = fields;
        definitions[0].field_count = 1;
    }
    if (index == 3) {
        definitions[1].type.storage = PIN_STRUCT;
        definitions[1].type.flags = 0;
        variants[0].type = definitions[1].type;
        definitions[0].variants = variants;
        definitions[0].variant_count = 1;
        fields[0].type = definitions[0].type;
        definitions[1].fields = fields;
        definitions[1].field_count = 1;
    }
    if (index == 4) {
        for (int i = 1; i < 3; i++) {
            fields[i].type = definitions[i].type;
            definitions[i].fields = &fields[i];
            definitions[i].field_count = 1;
        }
        nr = 3;
        roots[1] = definitions[1].type;
        roots[2] = definitions[2].type;
    }
    if (index >= 5) {
        for (int i = 0; i < count; i++) {
            fields[i].type = i + 1 < count ? definitions[i + 1].type
                           : index == 7 ? definitions[0].type
                                        : (script_type){ .storage = PIN_I32 };
            definitions[i].fields = &fields[i];
            definitions[i].field_count = 1;
        }
    }
    script_catalog catalog = { .types = definitions, .type_count = count };
    script_union_member_validation
        expected = script_union_members_validate(&catalog, roots, nr, states);
    int expected_root = index == 2 || index == 3 || index == 6 || index == 7 ? 0
                      : index == 4 ? 1
                                   : -1;
    int expected_error = index == 6 ? 2 : expected_root >= 0 ? 1 : 0;
    REQUIRE(expected.root == expected_root && expected.error == expected_error);
    for (int i = 0; i < count; i++) {
        int state = index == 4     ? (i == 0   ? 2
                                      : i == 1 ? 1
                                               : 0)
                  : index == 6     ? (i == 256 ? 0 : 1)
                  : expected_error ? 1
                                   : 2;
        REQUIRE(states[i] == state);
    }
    for (int b = 0; b < 2; b++) {
        arena_free(&input);
        arena_free(&detached);
        pools p = { 0 };
        int root_indices[4];
        for (int i = 0; i < nr; i++) {
            root_indices[i] = pool_type(&p, roots[i]);
            REQUIRE(root_indices[i] >= 0);
        }
        pin_value args[7], out;
        REQUIRE(graph(b, &catalog, &p, args));
        args[5] = integers(root_indices, nr);
        args[6] = integers(NULL, count);
        REQUIRE(
            compiler_fixture_call(&fixture, b, PACKAGE, "Validate", args, 7, 16000000, &detached, &out)
        );
        calls++;
        arena_free(&input);
        const pin_union *u;
        const pin_array *a;
        const script_catalog_variant *tag = variant(b, "Validation", "Checked");
        REQUIRE(
            tag && union_data(&out, 3, &u) && u->tag == tag->tag &&
            owned(&detached, &out, 0) && u->values[0].type == PIN_I32 &&
            u->values[0].i == expected.root && u->values[1].type == PIN_I32 &&
            u->values[1].i == expected.error &&
            list(&u->values[2], PIN_I32, count, &a)
        );
        for (int i = 0; i < count; i++) {
            REQUIRE(a->items[i].type == PIN_I32 && a->items[i].i == states[i]);
        }
        if (index == 6) {
            REQUIRE(
                !compiler_fixture_snapshot(&retained[1][b], &kept[1][b], &out) &&
                owned(&retained[1][b], &kept[1][b], 0)
            );
        }
    }
    return true;
}
TEST_CASE(union_members_population_and_publication)
{
    const source_package_input files[] = {
        { "union_members/Members.chibi",
          UNION_MEMBERS_CHIBI_SOURCE,
          sizeof UNION_MEMBERS_CHIBI_SOURCE - 1 },
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
    for (int i = 0; i < 8; i++) {
        REQUIRE(files[i].size < 48 * 1024);
    }
    REQUIRE(compiler_fixture_prepare_packs(&fixture, files, 8, &COMPILER_JSON_DEFINITION_PACK, 1));
    calls = 0;
    for (int i = 0; i < SOURCE_CASES; i++) {
        REQUIRE(source_case(i));
    }
    return true;
}
TEST_CASE(union_members_raw_owned_cycle_graphs)
{
    REQUIRE(calls == 32 && ready());
    for (int i = 0; i < RAW_CASES; i++) {
        REQUIRE(raw_case(i));
    }
    return true;
}
TEST_CASE(union_members_results_outlive_inputs_and_modules)
{
    cleanup();
    compiler_fixture_free(&fixture);
    REQUIRE(calls == 48);
    for (int b = 0; b < 2; b++) {
        REQUIRE(
            owned(&retained[0][b], &kept[0][b], 0) &&
            owned(&retained[1][b], &kept[1][b], 0)
        );
        const pin_union *members, *replacement, *validation;
        const pin_array *updates, *variants, *fields, *states;
        REQUIRE(
            union_data(&kept[0][b], 4, &members) &&
            list(&members->values[1], PIN_UNION, 3, &updates) &&
            union_data(&updates->items[2], 5, &replacement) &&
            replacement->values[0].type == PIN_U64 &&
            replacement->values[0].u64 == UINT64_MAX - 101 &&
            list(&replacement->values[1], PIN_STRUCT, 1, &variants) &&
            string_field(&variants->items[0], "name", "Box") &&
            list(&replacement->values[2], PIN_STRUCT, 2, &fields) &&
            string_field(&fields->items[0], "name", "first") &&
            string_field(&fields->items[1], "name", "second")
        );
        REQUIRE(
            union_data(&kept[1][b], 3, &validation) &&
            validation->values[0].type == PIN_I32 && validation->values[0].i == 0 &&
            validation->values[1].type == PIN_I32 && validation->values[1].i == 2 &&
            list(&validation->values[2], PIN_I32, 257, &states)
        );
        for (int i = 0; i < 257; i++) {
            REQUIRE(
                states->items[i].type == PIN_I32 &&
                states->items[i].i == (i == 256 ? 0 : 1)
            );
        }
    }
    return true;
}
void test_language_union_members_chibi(void)
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(union_members_population_and_publication);
    RUN_TEST_CASE(union_members_raw_owned_cycle_graphs);
    RUN_TEST_CASE(union_members_results_outlive_inputs_and_modules);
    pu_clear_setup();
    cleanup();
    compiler_fixture_free(&fixture);
    for (int i = 0; i < 2; i++) {
        for (int b = 0; b < 2; b++) {
            arena_free(&retained[i][b]);
            kept[i][b] = (pin_value){ 0 };
        }
    }
}
