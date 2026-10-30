#include "type_relations_chibi.h"

#include "compiler_fixture.h"
#include "lexical_binding_chibi_source.h"
#include "named_binding_chibi_source.h"
#include "package_core_source.h"
#include "syntax_chibi_source.h"
#include "third_party/pico_headers/pico_unit.h"
#include "toolchain/compiler_json.h"
#include "type_relations_chibi_source.h"
#include "types/array.h"
#include "types/text.h"
#include "types/union.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* 111 explicit requests: 18 equal, 15 numeric, 36 assignment, 33 catalog and
 * 9 canonical, each once on VM and SLJIT = 222 direct calls. The 50,000
 * budget argument is inherited/reset per activation, not a whole-call ceiling.
 * Five compiler inputs per backend give ten parses; no extra source or
 * combinations execute.
 * Caps: 5 definitions, 8 variants/types, 4 full-width IDs. Compiler files are
 * each below 32 KiB. The JSON primitive only admits imported named_binding;
 * these five entrypoints never call it. No IO, tasks, frames, native policy
 * callbacks, timing loops or new executable. C catalog functions stay oracle. */
enum
{
    MAX_DEFINITIONS = 5,
    MAX_VARIANTS = 8,
    MAX_IDS = 4,
    CALL_BUDGET = 50000,
    ERROR = SCRIPT_TYPE_ERROR,
    OPTIONAL = SCRIPT_TYPE_OPTIONAL,
    REFERENCE = SCRIPT_TYPE_REFERENCE,
    RESULTS = SCRIPT_TYPE_RESULTS,
    UNTYPED = SCRIPT_TYPE_UNTYPED,
    GENERIC = SCRIPT_TYPE_GENERIC,
    TASK = SCRIPT_TYPE_TASK,
    UNION = SCRIPT_TYPE_UNION,
    LITERAL = SCRIPT_TYPE_LITERAL,
    BOXED = SCRIPT_TYPE_BOXED_OPTIONAL
};
#define HIGH UINT64_C(0xfedcba9876543210)
#define NEXT UINT64_C(0xfedcba9876543211)
#define LAST UINT64_C(0x8000000000000001)
#define MISSING UINT64_C(0xffffffffffffffff)
#define TYPE(s, c, f, n, p)                                                    \
    { .storage = (s), .container = (c), .flags = (f), .nominal = (n), .parameter = (p) }
#define COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

typedef enum
{
    REL_EQUAL,
    REL_NUMERIC,
    REL_ASSIGNABLE,
    REL_CATALOG_ASSIGNABLE
} operation;
typedef enum
{
    EMPTY,
    ALIASES,
    MISSING_END,
    CYCLE,
    SELF_CYCLE,
    DUPLICATES,
    ZERO_ALIAS,
    UNIONS
} catalog_case;
typedef struct
{
    const char *name;
    operation op;
    catalog_case catalog;
    script_type expected, actual;
    bool answer;
} relation_case;
typedef struct
{
    const char *name;
    catalog_case catalog;
    script_type input, answer;
} canonical_case;

static const char PACKAGE[] = "project:type_relations",
                  NAMED[] = "project:named_binding";
static compiler_fixture fixture;
static arena input, detached, retained[2];
static pin_value kept[2];
static uint64_t kept_tags[2];
static script_type kept_types[2];
static uint64_t ids[MAX_IDS];
static int id_count, calls, parses;
static bool prepared;

static const script_catalog_type aliases[] = { { .id = HIGH, .underlying = NEXT },
                                               { .id = NEXT, .underlying = LAST },
                                               { .id = LAST } };
static const script_catalog_type missing_end[] = {
    { .id = HIGH, .underlying = NEXT },
    { .id = NEXT, .underlying = LAST },
    { .id = LAST, .underlying = MISSING }
};
static const script_catalog_type cycle[] = { { .id = HIGH, .underlying = NEXT },
                                             { .id = NEXT, .underlying = LAST },
                                             { .id = LAST, .underlying = NEXT } };
static const script_catalog_type self_cycle[] = { { .id = HIGH, .underlying = HIGH } };
static const script_catalog_type duplicates[] = { { .id = HIGH, .underlying = NEXT },
                                                  { .id = HIGH, .underlying = LAST },
                                                  { .id = NEXT } };
static const script_catalog_type zero_alias[] = { { .id = 0, .underlying = HIGH } };
static const script_catalog_variant target_variants[] = {
    { .tag = HIGH, .type = TYPE(PIN_I32, 0, 0, 0, 0) },
    { .tag = NEXT, .type = TYPE(PIN_STR, 0, LITERAL, 0, NEXT), .is_literal = true },
    { .tag = LAST, .type = TYPE(PIN_BOOL, 0, 0, 0, 0), .tagged = true },
    /* Metadata, rather than the type's LITERAL bit, adds this restriction. */
    { .tag = 0, .type = TYPE(PIN_NAME, 0, 0, 0, HIGH), .is_literal = true }
};
static const script_catalog_variant subset_variants[] = {
    /* Tag inclusion does not compare payload types, literal or tagged bits. */
    { .tag = NEXT, .type = TYPE(PIN_HANDLE, 1, OPTIONAL, LAST, 0), .tagged = true },
    { .tag = HIGH, .type = TYPE(PIN_F64, 0, 0, 0, 0) }
};
static const script_catalog_variant outside_variants[] = {
    { .tag = MISSING, .type = TYPE(PIN_I32, 0, 0, 0, 0) }
};
static const script_catalog_variant zero_variants[] = {
    { .tag = 0, .type = TYPE(PIN_BOOL, 0, 0, 0, 0) }
};
static const script_catalog_type unions[] = {
    { .id = HIGH, .variants = target_variants, .variant_count = COUNT(target_variants) },
    { .id = NEXT, .variants = subset_variants, .variant_count = COUNT(subset_variants) },
    { .id = LAST },
    { .id = MISSING, .variants = outside_variants, .variant_count = COUNT(outside_variants) },
    { .id = 0, .variants = zero_variants, .variant_count = COUNT(zero_variants) }
};
static script_catalog catalog_for(catalog_case which)
{
    switch (which) {
        case EMPTY: return (script_catalog){ 0 };
        case ALIASES:
            return (script_catalog){ .types = aliases, .type_count = COUNT(aliases) };
        case MISSING_END:
            return (script_catalog){ .types = missing_end,
                                     .type_count = COUNT(missing_end) };
        case CYCLE:
            return (script_catalog){ .types = cycle, .type_count = COUNT(cycle) };
        case SELF_CYCLE:
            return (script_catalog){ .types = self_cycle,
                                     .type_count = COUNT(self_cycle) };
        case DUPLICATES:
            return (script_catalog){ .types = duplicates,
                                     .type_count = COUNT(duplicates) };
        case ZERO_ALIAS:
            return (script_catalog){ .types = zero_alias,
                                     .type_count = COUNT(zero_alias) };
        case UNIONS:
            return (script_catalog){ .types = unions, .type_count = COUNT(unions) };
    }
    assert(false);
    return (script_catalog){ 0 };
}
static void cleanup()
{
    arena_free(&input);
    arena_free(&detached);
}
static pin_value integer(int n)
{
    return (pin_value){ .type = PIN_I32, .i = n };
}
static pin_value number(uint64_t n)
{
    return (pin_value){ .type = PIN_U64, .u64 = n };
}
static pin_value boolean(bool b)
{
    return (pin_value){ .type = PIN_BOOL, .b = b };
}
static bool put(pin_value *row, const char *name, pin_value value)
{
    const type_field *field = pin_type_field_path(pin_value_type(row), name);
    REQUIRE(field && field->type == value.type);
    REQUIRE(!pin_write_field(&input, row->ptr, field, value));
    return true;
}
static bool rows(int b, const char *package, const char *name, int count, pin_value *out)
{
    REQUIRE(count >= 0 && count <= MAX_VARIANTS);
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
        array->items[i] = (pin_value){ .type = PIN_STRUCT,
                                       .record = shape,
                                       .type_name = shape->type.name,
                                       .ptr = arena_alloc(&input, shape->type.size) };
        memset(array->items[i].ptr, 0, shape->type.size);
    }
    return true;
}
static pin_value *row(pin_value *array, int i)
{
    return &((pin_array *)array->ptr)->items[i];
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
static const script_catalog_variant *variant(int b, const char *package, const char *name, const char *tag)
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
        if (!strcmp(type->variants[i].name, tag)) {
            return &type->variants[i];
        }
    }
    return NULL;
}
static bool pack_value(int b, script_type type, pin_value *out)
{
    const script_catalog_variant *v = variant(b, NAMED, "Value", "Typed");
    REQUIRE(v);
    const pin_value values[] = { integer(type.storage),
                                 integer(type.container),
                                 integer((int)type.flags),
                                 number(type.nominal),
                                 number(type.parameter) };
    REQUIRE(!compiler_fixture_union(&input, v->tag, values, COUNT(values), out));
    return true;
}
static bool pack_catalog(int b, const script_catalog *catalog, pin_value *out)
{
    REQUIRE(catalog->type_count >= 0 && catalog->type_count <= MAX_DEFINITIONS);
    int variant_count = 0;
    for (int i = 0; i < catalog->type_count; i++) {
        REQUIRE(catalog->types[i].variant_count >= 0);
        variant_count += catalog->types[i].variant_count;
    }
    REQUIRE(variant_count <= MAX_VARIANTS);
    pin_value tables[4];
    REQUIRE(
        rows(b, NAMED, "Type", variant_count, &tables[0]) &&
        rows(b, PACKAGE, "Definition", catalog->type_count, &tables[2]) &&
        rows(b, PACKAGE, "Variant", variant_count, &tables[3])
    );
    id_count = 0;
    int offset = 0;
    for (int i = 0; i < catalog->type_count; i++) {
        const script_catalog_type *definition = &catalog->types[i];
        int id = slot(definition->id), underlying = slot(definition->underlying);
        REQUIRE(id >= -1 && underlying >= -1);
        pin_value *d = row(&tables[2], i);
        REQUIRE(
            put(d, "id_slot", integer(id)) &&
            put(d, "underlying_slot", integer(underlying)) &&
            put(d, "variant_start", integer(offset)) &&
            put(d, "variant_count", integer(definition->variant_count))
        );
        for (int j = 0; j < definition->variant_count; j++) {
            const script_catalog_variant *v = &definition->variants[j];
            int tag = slot(v->tag), nominal = slot(v->type.nominal),
                parameter = slot(v->type.parameter);
            REQUIRE(tag >= -1 && nominal >= -1 && parameter >= -1);
            pin_value *t = row(&tables[0], offset), *r = row(&tables[3], offset);
            REQUIRE(
                put(t, "storage", integer(v->type.storage)) &&
                put(t, "container", integer(v->type.container)) &&
                put(t, "flags", integer((int)v->type.flags)) &&
                put(t, "nominal_slot", integer(nominal)) &&
                put(t, "parameter_slot", integer(parameter)) &&
                put(r, "tag_slot", integer(tag)) && put(r, "type", integer(offset)) &&
                put(r, "tagged", boolean(v->tagged)) &&
                put(r, "is_literal", boolean(v->is_literal))
            );
            offset++;
        }
    }
    pin_array *numbers = pin_array_snapshot_allocate(&input, PIN_U64, NULL, id_count);
    REQUIRE(numbers);
    tables[1] = (pin_value){ .type = PIN_U64,
                             .container = PIN_CONTAINER_ARRAY,
                             .ptr = numbers };
    for (int i = 0; i < id_count; i++) {
        numbers->items[i] = number(ids[i]);
    }
    const script_catalog_variant *v = variant(b, PACKAGE, "Environment", "Tables");
    REQUIRE(v && !compiler_fixture_union(&input, v->tag, tables, COUNT(tables), out));
    return true;
}
static bool type_value(const pin_value *value, uint64_t tag, script_type expected)
{
    REQUIRE(value->type == PIN_UNION && value->ptr);
    const pin_union *u = value->ptr;
    REQUIRE(u->tag == tag && u->count == 5);
    const pin_value *v = u->values;
    REQUIRE(
        v[0].type == PIN_I32 && v[0].i == (int)expected.storage &&
        v[1].type == PIN_I32 && v[1].i == (int)expected.container &&
        v[2].type == PIN_I32 && v[2].i == (int)expected.flags &&
        v[3].type == PIN_U64 && v[3].u64 == expected.nominal &&
        v[4].type == PIN_U64 && v[4].u64 == expected.parameter
    );
    return true;
}
static bool run_relation(const relation_case *test)
{
    static const char *names[] = { "Equal", "Numeric", "Assignable", "CatalogAssignable" };
    script_catalog catalog = catalog_for(test->catalog);
    bool expected = test->answer;
    for (int b = 0; b < 2; b++) {
        cleanup();
        pin_value args[3], result;
        int count = 0;
        if (test->op == REL_CATALOG_ASSIGNABLE) {
            REQUIRE(pack_catalog(b, &catalog, &args[count++]));
        }
        REQUIRE(pack_value(b, test->expected, &args[count++]));
        if (test->op != REL_NUMERIC) {
            REQUIRE(pack_value(b, test->actual, &args[count++]));
        }
        REQUIRE(
            compiler_fixture_call(&fixture, b, PACKAGE, names[test->op], args, count, CALL_BUDGET, &detached, &result)
        );
        calls++;
        arena_free(&input);
        if (result.type != PIN_BOOL || result.b != expected) {
            fprintf(stderr, "relation backend %d: %s\n", b, test->name);
        }
        REQUIRE(result.type == PIN_BOOL && result.b == expected);
    }
    return true;
}

static const relation_case equal_cases[] = {
    { "same scalar", REL_EQUAL, EMPTY, TYPE(PIN_I32, 0, 0, 0, 0), TYPE(PIN_I32, 0, 0, 0, 0), true },
    { "different storage",
      REL_EQUAL,
      EMPTY,
      TYPE(PIN_I32, 0, 0, 0, 0),
      TYPE(PIN_F64, 0, 0, 0, 0),
      false },
    { "different container",
      REL_EQUAL,
      EMPTY,
      TYPE(PIN_I32, 1, 0, 0, 0),
      TYPE(PIN_I32, 0, 0, 0, 0),
      false },
    { "full width nominal",
      REL_EQUAL,
      EMPTY,
      TYPE(PIN_HANDLE, 0, 0, HIGH, 0),
      TYPE(PIN_HANDLE, 0, 0, NEXT, 0),
      false },
    { "ordinary parameters ignored",
      REL_EQUAL,
      EMPTY,
      TYPE(PIN_STR, 0, 0, 0, HIGH),
      TYPE(PIN_STR, 0, 0, 0, NEXT),
      true },
    { "one literal parameter ignored",
      REL_EQUAL,
      EMPTY,
      TYPE(PIN_STR, 0, LITERAL, 0, HIGH),
      TYPE(PIN_STR, 0, 0, 0, NEXT),
      true },
    { "same literal",
      REL_EQUAL,
      EMPTY,
      TYPE(PIN_STR, 0, LITERAL, 0, HIGH),
      TYPE(PIN_STR, 0, LITERAL, 0, HIGH),
      true },
    { "distinct full width literals",
      REL_EQUAL,
      EMPTY,
      TYPE(PIN_STR, 0, LITERAL, 0, HIGH),
      TYPE(PIN_STR, 0, LITERAL, 0, NEXT),
      false },
    { "optional differs",
      REL_EQUAL,
      EMPTY,
      TYPE(PIN_I32, 0, OPTIONAL, 0, 0),
      TYPE(PIN_I32, 0, 0, 0, 0),
      false },
    { "results differs",
      REL_EQUAL,
      EMPTY,
      TYPE(PIN_I32, 0, RESULTS, 0, 0),
      TYPE(PIN_I32, 0, 0, 0, 0),
      false },
    { "other flags ignored",
      REL_EQUAL,
      EMPTY,
      TYPE(PIN_I32, 0, ERROR | UNTYPED | GENERIC | REFERENCE | UNION | BOXED | SCRIPT_TYPE_NAMESPACE | SCRIPT_TYPE_SINGLETON, 0, 0),
      TYPE(PIN_I32, 0, 0, 0, 0),
      true },
    { "task ignores storage nominal results",
      REL_EQUAL,
      EMPTY,
      TYPE(PIN_TASK, 0, TASK | RESULTS, HIGH, HIGH),
      TYPE(PIN_HANDLE, 0, TASK, NEXT, HIGH),
      true },
    { "one task flag",
      REL_EQUAL,
      EMPTY,
      TYPE(PIN_TASK, 0, TASK, 0, 0),
      TYPE(PIN_TASK, 0, 0, 0, 0),
      false },
    { "task container differs",
      REL_EQUAL,
      EMPTY,
      TYPE(PIN_TASK, 1, TASK, 0, 0),
      TYPE(PIN_TASK, 0, TASK, 0, 0),
      false },
    { "task left wildcard",
      REL_EQUAL,
      EMPTY,
      TYPE(PIN_TASK, 0, TASK, 0, 0),
      TYPE(PIN_TASK, 0, TASK, 0, HIGH),
      true },
    { "task right wildcard",
      REL_EQUAL,
      EMPTY,
      TYPE(PIN_TASK, 0, TASK, 0, HIGH),
      TYPE(PIN_TASK, 0, TASK, 0, 0),
      true },
    { "task parameter differs",
      REL_EQUAL,
      EMPTY,
      TYPE(PIN_TASK, 0, TASK, 0, HIGH),
      TYPE(PIN_TASK, 0, TASK, 0, NEXT),
      false },
    { "task optional differs",
      REL_EQUAL,
      EMPTY,
      TYPE(PIN_TASK, 0, TASK | OPTIONAL, 0, 0),
      TYPE(PIN_TASK, 0, TASK, 0, 0),
      false }
};
static const relation_case numeric_cases[] = {
    { "numeric lower bound", REL_NUMERIC, EMPTY, TYPE(PIN_I8, 0, 0, 0, 0), { 0 }, true },
    { "numeric upper bound", REL_NUMERIC, EMPTY, TYPE(PIN_F64, 0, 0, 0, 0), { 0 }, true },
    { "below numeric range", REL_NUMERIC, EMPTY, TYPE(PIN_BOOL, 0, 0, 0, 0), { 0 }, false },
    { "above numeric range", REL_NUMERIC, EMPTY, TYPE(PIN_NAME, 0, 0, 0, 0), { 0 }, false },
    { "numeric array excluded", REL_NUMERIC, EMPTY, TYPE(PIN_I32, 1, 0, 0, 0), { 0 }, false },
    { "nominal numeric excluded", REL_NUMERIC, EMPTY, TYPE(PIN_I32, 0, 0, HIGH, 0), { 0 }, false },
    { "generic nominal numeric", REL_NUMERIC, EMPTY, TYPE(PIN_I32, 0, GENERIC, HIGH, 0), { 0 }, true },
    { "numeric error excluded", REL_NUMERIC, EMPTY, TYPE(PIN_I32, 0, ERROR, 0, 0), { 0 }, false },
    { "numeric optional excluded",
      REL_NUMERIC,
      EMPTY,
      TYPE(PIN_I32, 0, OPTIONAL | GENERIC, HIGH, 0),
      { 0 },
      false },
    { "numeric results excluded", REL_NUMERIC, EMPTY, TYPE(PIN_I32, 0, RESULTS, 0, 0), { 0 }, false },
    { "numeric task excluded", REL_NUMERIC, EMPTY, TYPE(PIN_I32, 0, TASK, 0, 0), { 0 }, false },
    { "numeric reference allowed", REL_NUMERIC, EMPTY, TYPE(PIN_I32, 0, REFERENCE, 0, 0), { 0 }, true },
    { "numeric untyped allowed", REL_NUMERIC, EMPTY, TYPE(PIN_I32, 0, UNTYPED, 0, 0), { 0 }, true },
    { "numeric boxed bit alone allowed",
      REL_NUMERIC,
      EMPTY,
      TYPE(PIN_I32, 0, BOXED, 0, 0),
      { 0 },
      true },
    { "numeric literal allowed", REL_NUMERIC, EMPTY, TYPE(PIN_I32, 0, LITERAL, 0, HIGH), { 0 }, true }
};
static const relation_case assignment_cases[] = {
    { "integer widening",
      REL_ASSIGNABLE,
      EMPTY,
      TYPE(PIN_I32, 0, 0, 0, 0),
      TYPE(PIN_U8, 0, 0, 0, 0),
      true },
    { "integer narrowing",
      REL_ASSIGNABLE,
      EMPTY,
      TYPE(PIN_I8, 0, 0, 0, 0),
      TYPE(PIN_I32, 0, 0, 0, 0),
      false },
    { "signed to unsigned",
      REL_ASSIGNABLE,
      EMPTY,
      TYPE(PIN_U64, 0, 0, 0, 0),
      TYPE(PIN_I32, 0, 0, 0, 0),
      false },
    { "unsigned to equal signed",
      REL_ASSIGNABLE,
      EMPTY,
      TYPE(PIN_I32, 0, 0, 0, 0),
      TYPE(PIN_U32, 0, 0, 0, 0),
      false },
    { "integer to f64",
      REL_ASSIGNABLE,
      EMPTY,
      TYPE(PIN_F64, 0, 0, 0, 0),
      TYPE(PIN_U64, 0, 0, 0, 0),
      true },
    { "f64 to integer",
      REL_ASSIGNABLE,
      EMPTY,
      TYPE(PIN_I16, 0, 0, 0, 0),
      TYPE(PIN_F64, 0, 0, 0, 0),
      false },
    { "integer to f64",
      REL_ASSIGNABLE,
      EMPTY,
      TYPE(PIN_F64, 0, 0, 0, 0),
      TYPE(PIN_I16, 0, 0, 0, 0),
      true },
    { "expected error before boxed",
      REL_ASSIGNABLE,
      EMPTY,
      TYPE(PIN_STR, 0, ERROR | BOXED, 0, 0),
      TYPE(PIN_I32, 1, 0, 0, 0),
      true },
    { "actual error",
      REL_ASSIGNABLE,
      EMPTY,
      TYPE(PIN_STR, 0, 0, 0, 0),
      TYPE(PIN_I32, 1, ERROR, 0, 0),
      true },
    { "boxed strips both optional flags",
      REL_ASSIGNABLE,
      EMPTY,
      TYPE(PIN_I32, 0, BOXED | OPTIONAL, 0, 0),
      TYPE(PIN_I32, 0, BOXED | OPTIONAL, 0, 0),
      true },
    { "boxed blocks numeric widening",
      REL_ASSIGNABLE,
      EMPTY,
      TYPE(PIN_I32, 0, BOXED | OPTIONAL, 0, 0),
      TYPE(PIN_F64, 0, 0, 0, 0),
      false },
    { "boxed precedes untyped",
      REL_ASSIGNABLE,
      EMPTY,
      TYPE(PIN_STR, 0, BOXED | UNTYPED, 0, 0),
      TYPE(PIN_I32, 0, UNTYPED, 0, 0),
      false },
    { "boxed blocks name string conversion",
      REL_ASSIGNABLE,
      EMPTY,
      TYPE(PIN_NAME, 0, BOXED | OPTIONAL, 0, 0),
      TYPE(PIN_STR, 0, 0, 0, 0),
      false },
    { "boxed task uses symmetric equality wildcard",
      REL_ASSIGNABLE,
      EMPTY,
      TYPE(PIN_TASK, 0, TASK | BOXED | OPTIONAL, 0, HIGH),
      TYPE(PIN_TASK, 0, TASK, 0, 0),
      true },
    { "expected untyped",
      REL_ASSIGNABLE,
      EMPTY,
      TYPE(PIN_STR, 0, UNTYPED, 0, 0),
      TYPE(PIN_I32, 1, OPTIONAL, 0, 0),
      true },
    { "actual untyped",
      REL_ASSIGNABLE,
      EMPTY,
      TYPE(PIN_STR, 0, 0, 0, 0),
      TYPE(PIN_I32, 1, UNTYPED, 0, 0),
      true },
    { "generic cleared before numeric",
      REL_ASSIGNABLE,
      EMPTY,
      TYPE(PIN_I32, 0, GENERIC, HIGH, 0),
      TYPE(PIN_F64, 0, GENERIC, HIGH, 0),
      false },
    { "numeric width conversion",
      REL_ASSIGNABLE,
      EMPTY,
      TYPE(PIN_I8, 0, 0, 0, 0),
      TYPE(PIN_F64, 0, 0, 0, 0),
      false },
    { "actual optional rejected",
      REL_ASSIGNABLE,
      EMPTY,
      TYPE(PIN_I32, 0, 0, 0, 0),
      TYPE(PIN_I32, 0, OPTIONAL, 0, 0),
      false },
    { "expected optional permits value",
      REL_ASSIGNABLE,
      EMPTY,
      TYPE(PIN_I32, 0, OPTIONAL, 0, 0),
      TYPE(PIN_I32, 0, 0, 0, 0),
      true },
    { "optional strips actual boxed before numeric",
      REL_ASSIGNABLE,
      EMPTY,
      TYPE(PIN_F64, 0, OPTIONAL, 0, 0),
      TYPE(PIN_I32, 0, OPTIONAL | BOXED, 0, 0),
      true },
    { "name accepts string",
      REL_ASSIGNABLE,
      EMPTY,
      TYPE(PIN_NAME, 0, 0, 0, 0),
      TYPE(PIN_STR, 0, 0, 0, 0),
      true },
    { "string accepts name",
      REL_ASSIGNABLE,
      EMPTY,
      TYPE(PIN_STR, 0, 0, 0, 0),
      TYPE(PIN_NAME, 0, 0, 0, 0),
      true },
    { "name string preserves nominal",
      REL_ASSIGNABLE,
      EMPTY,
      TYPE(PIN_NAME, 0, 0, HIGH, 0),
      TYPE(PIN_STR, 0, 0, NEXT, 0),
      false },
    { "reference ignored",
      REL_ASSIGNABLE,
      EMPTY,
      TYPE(PIN_HANDLE, 0, REFERENCE, HIGH, 0),
      TYPE(PIN_HANDLE, 0, 0, HIGH, 0),
      true },
    { "task expected wildcard",
      REL_ASSIGNABLE,
      EMPTY,
      TYPE(PIN_TASK, 0, TASK, HIGH, 0),
      TYPE(PIN_TASK, 0, TASK, NEXT, HIGH),
      true },
    { "task actual wildcard rejected",
      REL_ASSIGNABLE,
      EMPTY,
      TYPE(PIN_TASK, 0, TASK, HIGH, HIGH),
      TYPE(PIN_TASK, 0, TASK, NEXT, 0),
      false },
    { "task ignores storage and nominal",
      REL_ASSIGNABLE,
      EMPTY,
      TYPE(PIN_TASK, 0, TASK | RESULTS, HIGH, HIGH),
      TYPE(PIN_HANDLE, 0, TASK, NEXT, HIGH),
      true },
    { "task actual optional rejected",
      REL_ASSIGNABLE,
      EMPTY,
      TYPE(PIN_TASK, 0, TASK, 0, 0),
      TYPE(PIN_TASK, 0, TASK | OPTIONAL, 0, HIGH),
      false },
    { "task expected optional permits value",
      REL_ASSIGNABLE,
      EMPTY,
      TYPE(PIN_TASK, 0, TASK | OPTIONAL, 0, HIGH),
      TYPE(PIN_TASK, 0, TASK, 0, HIGH),
      true },
    { "task container differs",
      REL_ASSIGNABLE,
      EMPTY,
      TYPE(PIN_TASK, 1, TASK, 0, 0),
      TYPE(PIN_TASK, 0, TASK, 0, 0),
      false },
    { "task bit required on both",
      REL_ASSIGNABLE,
      EMPTY,
      TYPE(PIN_TASK, 0, TASK, 0, 0),
      TYPE(PIN_TASK, 0, 0, 0, 0),
      false },
    { "different string literals rejected",
      REL_ASSIGNABLE,
      EMPTY,
      TYPE(PIN_STR, 0, LITERAL, 0, HIGH),
      TYPE(PIN_STR, 0, LITERAL, 0, NEXT),
      false },
    { "numeric shortcut precedes literal equality",
      REL_ASSIGNABLE,
      EMPTY,
      TYPE(PIN_I32, 0, LITERAL, 0, HIGH),
      TYPE(PIN_I32, 0, LITERAL, 0, NEXT),
      true },
    { "plain scalar may satisfy raw literal",
      REL_ASSIGNABLE,
      EMPTY,
      TYPE(PIN_STR, 0, LITERAL, 0, HIGH),
      TYPE(PIN_STR, 0, 0, 0, 0),
      true },
    { "array has no numeric widening",
      REL_ASSIGNABLE,
      EMPTY,
      TYPE(PIN_I32, 1, 0, 0, 0),
      TYPE(PIN_F64, 1, 0, 0, 0),
      false },
    { "results prevent numeric widening",
      REL_ASSIGNABLE,
      EMPTY,
      TYPE(PIN_I32, 0, RESULTS, 0, 0),
      TYPE(PIN_F64, 0, RESULTS, 0, 0),
      false }
};

TEST_CASE(type_relations_scalar_tables_match_native_oracle)
{
    const source_package_input files[] = {
        { "type_relations/Relations.chibi",
          TYPE_RELATIONS_CHIBI_SOURCE,
          sizeof TYPE_RELATIONS_CHIBI_SOURCE - 1 },
        { "named_binding/Binding.chibi",
          NAMED_BINDING_CHIBI_SOURCE,
          sizeof NAMED_BINDING_CHIBI_SOURCE - 1 },
        { "lexical_binding/Binding.chibi",
          LEXICAL_BINDING_CHIBI_SOURCE,
          sizeof LEXICAL_BINDING_CHIBI_SOURCE - 1 },
        { "syntax_diagnostics/Diagnostics.chibi",
          SYNTAX_CHIBI_SOURCE,
          sizeof SYNTAX_CHIBI_SOURCE - 1 },
        { "/sdk/core/strings/Strings.chibi", CORE_STRINGS_SOURCE, sizeof CORE_STRINGS_SOURCE - 1 }
    };
    for (int i = 0; i < COUNT(files); i++) {
        REQUIRE(files[i].size < 32768);
    }
    REQUIRE(
        compiler_fixture_prepare_packs(&fixture, files, COUNT(files), &COMPILER_JSON_DEFINITION_PACK, 1)
    );
    parses = 2 * COUNT(files);
    prepared = true;
    for (int i = 0; i < COUNT(equal_cases); i++) {
        REQUIRE(run_relation(&equal_cases[i]));
    }
    for (int i = 0; i < COUNT(numeric_cases); i++) {
        REQUIRE(run_relation(&numeric_cases[i]));
    }
    for (int i = 0; i < COUNT(assignment_cases); i++) {
        REQUIRE(run_relation(&assignment_cases[i]));
    }
    REQUIRE(parses == 10 && calls == 140);
    return true;
}

static const relation_case catalog_cases[] = {
    { "catalog literal requires literal before error recovery",
      REL_CATALOG_ASSIGNABLE,
      EMPTY,
      TYPE(PIN_STR, 0, LITERAL, 0, HIGH),
      TYPE(PIN_STR, 0, ERROR, 0, HIGH),
      false },
    { "catalog literal compares only full width parameter",
      REL_CATALOG_ASSIGNABLE,
      EMPTY,
      TYPE(PIN_STR, 0, LITERAL | ERROR, HIGH, HIGH),
      TYPE(PIN_I32, 1, LITERAL | ERROR | OPTIONAL, NEXT, HIGH),
      true },
    { "catalog literal parameter differs despite errors",
      REL_CATALOG_ASSIGNABLE,
      EMPTY,
      TYPE(PIN_STR, 0, LITERAL | ERROR, 0, HIGH),
      TYPE(PIN_STR, 0, LITERAL | ERROR, 0, NEXT),
      false },
    { "same union bypasses missing metadata",
      REL_CATALOG_ASSIGNABLE,
      EMPTY,
      TYPE(PIN_UNION, 0, UNION, HIGH, 0),
      TYPE(PIN_UNION, 0, UNION, HIGH, 0),
      true },
    { "same union fast path ignores boxed payload",
      REL_CATALOG_ASSIGNABLE,
      EMPTY,
      TYPE(PIN_UNION, 0, UNION, HIGH, 0),
      TYPE(PIN_HANDLE, 0, UNION | BOXED, HIGH, NEXT),
      true },
    { "same optional union fast path",
      REL_CATALOG_ASSIGNABLE,
      EMPTY,
      TYPE(PIN_UNION, 0, UNION | OPTIONAL, HIGH, 0),
      TYPE(PIN_UNION, 0, UNION | OPTIONAL, HIGH, 0),
      true },
    { "optional source falls through to tag subset",
      REL_CATALOG_ASSIGNABLE,
      UNIONS,
      TYPE(PIN_UNION, 0, UNION, HIGH, 0),
      TYPE(PIN_UNION, 0, UNION | OPTIONAL, HIGH, 0),
      true },
    { "optional source without metadata fails",
      REL_CATALOG_ASSIGNABLE,
      EMPTY,
      TYPE(PIN_UNION, 0, UNION, HIGH, 0),
      TYPE(PIN_UNION, 0, UNION | OPTIONAL, HIGH, 0),
      false },
    { "source tags included despite unrelated payloads",
      REL_CATALOG_ASSIGNABLE,
      UNIONS,
      TYPE(PIN_UNION, 0, UNION, HIGH, 0),
      TYPE(PIN_UNION, 0, UNION, NEXT, 0),
      true },
    { "source tag absent",
      REL_CATALOG_ASSIGNABLE,
      UNIONS,
      TYPE(PIN_UNION, 0, UNION, HIGH, 0),
      TYPE(PIN_UNION, 0, UNION, MISSING, 0),
      false },
    { "empty source succeeds with missing target",
      REL_CATALOG_ASSIGNABLE,
      UNIONS,
      TYPE(PIN_UNION, 0, UNION, 123, 0),
      TYPE(PIN_UNION, 0, UNION | OPTIONAL, LAST, 0),
      true },
    { "nonempty source fails with missing target",
      REL_CATALOG_ASSIGNABLE,
      UNIONS,
      TYPE(PIN_UNION, 0, UNION, 123, 0),
      TYPE(PIN_UNION, 0, UNION, NEXT, 0),
      false },
    { "missing source uses plain alternative path",
      REL_CATALOG_ASSIGNABLE,
      UNIONS,
      TYPE(PIN_UNION, 0, UNION, HIGH, 0),
      TYPE(PIN_UNION, 0, UNION, 123, 0),
      false },
    { "boxed source excluded from subset",
      REL_CATALOG_ASSIGNABLE,
      UNIONS,
      TYPE(PIN_UNION, 0, UNION, HIGH, 0),
      TYPE(PIN_UNION, 0, UNION | BOXED, NEXT, 0),
      false },
    { "array source excluded from subset",
      REL_CATALOG_ASSIGNABLE,
      UNIONS,
      TYPE(PIN_UNION, 0, UNION, HIGH, 0),
      TYPE(PIN_UNION, 1, UNION, NEXT, 0),
      false },
    { "plain member exact match",
      REL_CATALOG_ASSIGNABLE,
      UNIONS,
      TYPE(PIN_UNION, 0, UNION, HIGH, 0),
      TYPE(PIN_I32, 0, 0, 0, 0),
      true },
    { "plain member has no numeric widening",
      REL_CATALOG_ASSIGNABLE,
      UNIONS,
      TYPE(PIN_UNION, 0, UNION, HIGH, 0),
      TYPE(PIN_F64, 0, 0, 0, 0),
      false },
    { "tagged member cannot match plain scalar",
      REL_CATALOG_ASSIGNABLE,
      UNIONS,
      TYPE(PIN_UNION, 0, UNION, HIGH, 0),
      TYPE(PIN_BOOL, 0, 0, 0, 0),
      false },
    { "plain member permits literal scalar",
      REL_CATALOG_ASSIGNABLE,
      UNIONS,
      TYPE(PIN_UNION, 0, UNION, HIGH, 0),
      TYPE(PIN_I32, 0, LITERAL, 0, HIGH),
      true },
    { "literal member matches exact literal",
      REL_CATALOG_ASSIGNABLE,
      UNIONS,
      TYPE(PIN_UNION, 0, UNION, HIGH, 0),
      TYPE(PIN_STR, 0, LITERAL, 0, NEXT),
      true },
    { "literal member rejects different literal",
      REL_CATALOG_ASSIGNABLE,
      UNIONS,
      TYPE(PIN_UNION, 0, UNION, HIGH, 0),
      TYPE(PIN_STR, 0, LITERAL, 0, HIGH),
      false },
    { "literal member rejects plain string",
      REL_CATALOG_ASSIGNABLE,
      UNIONS,
      TYPE(PIN_UNION, 0, UNION, HIGH, 0),
      TYPE(PIN_STR, 0, 0, 0, NEXT),
      false },
    { "literal metadata independently requires literal",
      REL_CATALOG_ASSIGNABLE,
      UNIONS,
      TYPE(PIN_UNION, 0, UNION, HIGH, 0),
      TYPE(PIN_NAME, 0, 0, 0, HIGH),
      false },
    { "literal metadata independently matches parameter",
      REL_CATALOG_ASSIGNABLE,
      UNIONS,
      TYPE(PIN_UNION, 0, UNION, HIGH, 0),
      TYPE(PIN_NAME, 0, LITERAL, 0, HIGH),
      true },
    { "literal metadata independently rejects parameter",
      REL_CATALOG_ASSIGNABLE,
      UNIONS,
      TYPE(PIN_UNION, 0, UNION, HIGH, 0),
      TYPE(PIN_NAME, 0, LITERAL, 0, NEXT),
      false },
    { "actual error bypasses union branch",
      REL_CATALOG_ASSIGNABLE,
      UNIONS,
      TYPE(PIN_UNION, 0, UNION, HIGH, 0),
      TYPE(PIN_BOOL, 0, ERROR, 0, 0),
      true },
    { "expected error does not bypass union branch",
      REL_CATALOG_ASSIGNABLE,
      UNIONS,
      TYPE(PIN_UNION, 0, UNION | ERROR, HIGH, 0),
      TYPE(PIN_F64, 0, 0, 0, 0),
      false },
    { "array expected uses ordinary relation",
      REL_CATALOG_ASSIGNABLE,
      EMPTY,
      TYPE(PIN_I32, 1, UNION, 0, 0),
      TYPE(PIN_I32, 1, 0, 0, 0),
      true },
    { "expected alias canonicalized",
      REL_CATALOG_ASSIGNABLE,
      ALIASES,
      TYPE(PIN_HANDLE, 0, 0, HIGH, 0),
      TYPE(PIN_HANDLE, 0, 0, LAST, 0),
      true },
    { "actual alias canonicalized",
      REL_CATALOG_ASSIGNABLE,
      ALIASES,
      TYPE(PIN_HANDLE, 0, 0, LAST, 0),
      TYPE(PIN_HANDLE, 0, 0, HIGH, 0),
      true },
    { "canonicalization preserves storage",
      REL_CATALOG_ASSIGNABLE,
      ALIASES,
      TYPE(PIN_HANDLE, 0, 0, HIGH, 0),
      TYPE(PIN_STR, 0, 0, LAST, 0),
      false },
    { "alias cycle error enables recovery",
      REL_CATALOG_ASSIGNABLE,
      CYCLE,
      TYPE(PIN_HANDLE, 0, 0, HIGH, 0),
      TYPE(PIN_BOOL, 1, 0, 0, 0),
      true },
    { "zero nominal still selects catalog union",
      REL_CATALOG_ASSIGNABLE,
      UNIONS,
      TYPE(PIN_UNION, 0, UNION, 0, 0),
      TYPE(PIN_BOOL, 0, 0, 0, 0),
      true }
};
TEST_CASE(type_relations_catalog_tables_match_native_oracle)
{
    REQUIRE(prepared && parses == 10);
    for (int i = 0; i < COUNT(catalog_cases); i++) {
        REQUIRE(run_relation(&catalog_cases[i]));
    }
    REQUIRE(calls == 206);
    return true;
}
static const canonical_case canonical_cases[] = {
    { "zero nominal returns before zero-id alias lookup",
      ZERO_ALIAS,
      TYPE(PIN_STR, 1, OPTIONAL, 0, HIGH),
      TYPE(PIN_STR, 1, OPTIONAL, 0, HIGH) },
    { "unknown nominal unchanged",
      EMPTY,
      TYPE(PIN_HANDLE, 0, REFERENCE, HIGH, NEXT),
      TYPE(PIN_HANDLE, 0, REFERENCE, HIGH, NEXT) },
    { "terminal definition unchanged",
      ALIASES,
      TYPE(PIN_HANDLE, 0, GENERIC, LAST, HIGH),
      TYPE(PIN_HANDLE, 0, GENERIC, LAST, HIGH) },
    { "alias chain preserves all other fields",
      ALIASES,
      TYPE(PIN_HANDLE, 1, GENERIC | OPTIONAL, HIGH, NEXT),
      TYPE(PIN_HANDLE, 1, GENERIC | OPTIONAL, LAST, NEXT) },
    { "missing destination at exact count is not cycle",
      MISSING_END,
      TYPE(PIN_HANDLE, 1, RESULTS, HIGH, NEXT),
      TYPE(PIN_HANDLE, 1, RESULTS, MISSING, NEXT) },
    { "cycle retains count-th nominal and adds error",
      CYCLE,
      TYPE(PIN_HANDLE, 0, REFERENCE, HIGH, MISSING),
      TYPE(PIN_HANDLE, 0, REFERENCE | ERROR, NEXT, MISSING) },
    { "self cycle",
      SELF_CYCLE,
      TYPE(PIN_HANDLE, 0, 0, HIGH, NEXT),
      TYPE(PIN_HANDLE, 0, ERROR, HIGH, NEXT) },
    { "first duplicate ID wins",
      DUPLICATES,
      TYPE(PIN_HANDLE, 0, 0, HIGH, LAST),
      TYPE(PIN_HANDLE, 0, 0, NEXT, LAST) },
    { "existing error bit survives successful chain",
      ALIASES,
      TYPE(PIN_HANDLE, 1, ERROR | RESULTS | BOXED, HIGH, MISSING),
      TYPE(PIN_HANDLE, 1, ERROR | RESULTS | BOXED, LAST, MISSING) }
};
TEST_CASE(type_relations_canonical_tables_match_recorded_results)
{
    REQUIRE(prepared);
    for (int i = 0; i < COUNT(canonical_cases); i++) {
        const canonical_case *test = &canonical_cases[i];
        script_catalog catalog = catalog_for(test->catalog);
        script_type expected = test->answer;
        for (int b = 0; b < 2; b++) {
            cleanup();
            pin_value args[2], result;
            const script_catalog_variant *v = variant(b, NAMED, "Value", "Typed");
            REQUIRE(v);
            uint64_t tag = v->tag;
            REQUIRE(
                pack_catalog(b, &catalog, &args[0]) &&
                pack_value(b, test->input, &args[1])
            );
            REQUIRE(
                compiler_fixture_call(&fixture, b, PACKAGE, "Canonical", args, 2, CALL_BUDGET, &detached, &result)
            );
            calls++;
            arena_free(&input);
            REQUIRE(type_value(&result, tag, expected));
            if (test->catalog == CYCLE) {
                REQUIRE(!compiler_fixture_snapshot(&retained[b], &kept[b], &result));
                kept_tags[b] = tag;
                kept_types[b] = expected;
            }
        }
    }
    REQUIRE(calls == 224 && parses == 10);
    return true;
}
TEST_CASE(type_relations_approved_floating_matrix)
{
    static const struct
    {
        pin_type expected, actual;
        bool answer;
    } cases[] = {
        { PIN_F64, PIN_F32, true },
        { PIN_F32, PIN_F64, false },
        { PIN_F32, PIN_F32, true },
        { PIN_F64, PIN_F64, true },
    };
    bool agreed = true;
    for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
        script_type expected = TYPE(cases[i].expected, 0, 0, 0, 0);
        script_type actual = TYPE(cases[i].actual, 0, 0, 0, 0);
        for (int b = 0; b < 2; b++) {
            cleanup();
            pin_value args[2], result;
            REQUIRE(pack_value(b, expected, &args[0]));
            REQUIRE(pack_value(b, actual, &args[1]));
            REQUIRE(
                compiler_fixture_call(&fixture, b, PACKAGE, "Assignable", args, 2, CALL_BUDGET, &detached, &result)
            );
            REQUIRE(result.type == PIN_BOOL);
            if (result.b != cases[i].answer) {
                fprintf(
                    stderr,
                    "authored floating relation backend %d case %zu: expected "
                    "%d, got %d\n",
                    b,
                    i,
                    cases[i].answer,
                    result.b
                );
                agreed = false;
            }
        }
    }
    REQUIRE(agreed);
    return true;
}

TEST_CASE(type_relations_canonical_results_outlive_all_producers)
{
    REQUIRE(prepared && calls == 224 && parses == 10);
    cleanup();
    compiler_fixture_free(&fixture);
    prepared = false;
    for (int b = 0; b < 2; b++) {
        REQUIRE(
            kept[b].type == PIN_UNION && kept[b].ptr &&
            arena_contains(&retained[b], kept[b].ptr)
        );
        REQUIRE(type_value(&kept[b], kept_tags[b], kept_types[b]));
    }
    return true;
}
void test_language_type_relations_chibi()
{
    calls = 0;
    parses = 0;
    prepared = false;
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(type_relations_scalar_tables_match_native_oracle);
    RUN_TEST_CASE(type_relations_catalog_tables_match_native_oracle);
    RUN_TEST_CASE(type_relations_canonical_tables_match_recorded_results);
    RUN_TEST_CASE(type_relations_approved_floating_matrix);
    RUN_TEST_CASE(type_relations_canonical_results_outlive_all_producers);
    pu_clear_setup();
    compiler_fixture_free(&fixture);
    for (int b = 0; b < 2; b++) {
        arena_free(&retained[b]);
        kept[b] = (pin_value){ 0 };
    }
}
