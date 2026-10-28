#include "unit_algebra_chibi.h"

#include "compiler_fixture.h"
#include "language/unit_algebra.h"
#include "package_core_source.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/array.h"
#include "types/text.h"
#include "types/union.h"
#include "unit_algebra_chibi_source.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

/* 48 fixed requests: 24 Parse, five Same, ten Product and nine Format. Both
 * backends give 96 authored calls, each capped at two million instructions.
 * Units/CoreStrings are prepared once per backend: four preparation parses.
 * At most eight terms, 240 input/name bytes and 192 output bytes coexist.
 * Algebra inputs are explicit raw terms, never C parser policy outputs.
 * No IO, services, processes, tasks, combinations or timing loops. */
typedef struct
{
    const char *name;
    int power;
} term;
typedef struct
{
    script_unit_kind kind;
    int count;
    term terms[8];
} unit;
typedef struct
{
    const char *text;
    unit expected;
} parse_case;
typedef struct
{
    unit left, right;
    bool expected;
} same_case;
typedef struct
{
    unit left, right;
    int sign;
    unit expected;
    bool overflow;
} product_case;
typedef struct
{
    unit value;
    int size;
    const char *expected;
} format_case;
#define UNKNOWN { .kind = SCRIPT_UNIT_UNKNOWN }
#define LITERAL { .kind = SCRIPT_UNIT_LITERAL }
#define INVALID { .kind = SCRIPT_UNIT_INVALID }
#define DIMENSIONLESS { .kind = SCRIPT_UNIT_KNOWN }
#define KNOWN(N, ...)                                                          \
    {                                                                          \
        SCRIPT_UNIT_KNOWN, (N),                                                \
        {                                                                      \
            __VA_ARGS__                                                        \
        }                                                                      \
    }
static const parse_case parses[] = {
    { NULL, UNKNOWN },
    { "", INVALID },
    { "1", DIMENSIONLESS },
    { "m", KNOWN(1, { "m", 1 }) },
    { "_m2", KNOWN(1, { "_m2", 1 }) },
    { "m/s·kg", KNOWN(3, { "m", 1 }, { "s", -1 }, { "kg", -1 }) },
    { "m²/s³", KNOWN(2, { "m", 2 }, { "s", -3 }) },
    { "m^+2", KNOWN(1, { "m", 2 }) },
    { "m^-2", KNOWN(1, { "m", -2 }) },
    { "m^0", INVALID },
    { "m^ 2", INVALID },
    { "m^2147483647", KNOWN(1, { "m", INT_MAX }) },
    { "m^-2147483647", KNOWN(1, { "m", -INT_MAX }) },
    { "m^2147483648", INVALID },
    { "m^-2147483648", INVALID },
    { "m^2147483647·m", INVALID },
    { "m^-2147483647/m", INVALID },
    { "a·b·c·d·e·f·g·h",
      KNOWN(8, { "a", 1 }, { "b", 1 }, { "c", 1 }, { "d", 1 }, { "e", 1 }, { "f", 1 }, { "g", 1 }, { "h", 1 }) },
    { "a·b·c·d·e·f·g·h·i/i", INVALID },
    { "m^2·m^-2·s", KNOWN(1, { "s", 1 }) },
    { "m/", INVALID },
    { "1m", INVALID },
    { "m^2x", INVALID },
    { "m·s/m·kg", KNOWN(2, { "s", 1 }, { "kg", -1 }) },
};
static const same_case equals[] = {
    { KNOWN(2, { "m", 1 }, { "s", -1 }), KNOWN(2, { "s", -1 }, { "m", 1 }), true },
    { KNOWN(1, { "m", 1 }), KNOWN(1, { "m", 2 }), false },
    { KNOWN(1, { "m", 1 }), KNOWN(2, { "m", 1 }, { "s", 1 }), false },
    { UNKNOWN, INVALID, true }, /* Equality itself intentionally ignores kind. */
    { KNOWN(1, { "m", 1 }), KNOWN(1, { "M", 1 }), false },
};
static const product_case products[] = {
    { KNOWN(1, { "m", 1 }), KNOWN(1, { "s", -1 }), 1, KNOWN(2, { "m", 1 }, { "s", -1 }), false },
    { KNOWN(1, { "m", 1 }), KNOWN(1, { "m", 1 }), -1, DIMENSIONLESS, false },
    { KNOWN(2, { "m", 1 }, { "s", 1 }),
      KNOWN(2, { "m", -1 }, { "kg", 1 }),
      1,
      KNOWN(2, { "s", 1 }, { "kg", 1 }),
      false },
    { KNOWN(8, { "a", 1 }, { "b", 1 }, { "c", 1 }, { "d", 1 }, { "e", 1 }, { "f", 1 }, { "g", 1 }, { "h", 1 }),
      KNOWN(1, { "i", 1 }),
      1,
      UNKNOWN,
      true },
    { KNOWN(8, { "a", 1 }, { "b", 1 }, { "c", 1 }, { "d", 1 }, { "e", 1 }, { "f", 1 }, { "g", 1 }, { "h", 1 }),
      KNOWN(1, { "a", -1 }),
      1,
      KNOWN(7, { "b", 1 }, { "c", 1 }, { "d", 1 }, { "e", 1 }, { "f", 1 }, { "g", 1 }, { "h", 1 }),
      false },
    { KNOWN(1, { "m", INT_MAX }), KNOWN(1, { "m", 1 }), 1, UNKNOWN, true },
    { KNOWN(1, { "m", -INT_MAX }), KNOWN(1, { "m", 1 }), -1, UNKNOWN, true },
    { LITERAL, LITERAL, 1, LITERAL, false },
    { KNOWN(1, { "m", 1 }), LITERAL, 1, UNKNOWN, false },
    { UNKNOWN, KNOWN(1, { "m", 1 }), -1, UNKNOWN, false },
};
static char long190[191], long189[190], full_squared[192];
static const format_case formats[] = {
    { UNKNOWN, 192, "?" },
    { DIMENSIONLESS, 192, "1" },
    { KNOWN(3, { "kg", 1 }, { "m", 1 }, { "s", -2 }), 192, "kg·m/s²" },
    { KNOWN(1, { "m", -4 }), 192, "1/m^4" },
    { KNOWN(1, { long190, 2 }), 192, long190 },
    { KNOWN(1, { long189, 2 }), 192, full_squared },
    { KNOWN(2, { long190, 1 }, { "x", 1 }), 192, long190 },
    { KNOWN(1, { "m", 2 }), 3, "m" },
    { UNKNOWN, 1, "" },
};
#undef UNKNOWN
#undef LITERAL
#undef INVALID
#undef DIMENSIONLESS
#undef KNOWN
_Static_assert(sizeof parses / sizeof *parses == 24, "parse workload");
_Static_assert(sizeof equals / sizeof *equals == 5, "same workload");
_Static_assert(sizeof products / sizeof *products == 10, "product workload");
_Static_assert(sizeof formats / sizeof *formats == 9, "format workload");

static const char PACKAGE[] = "project:unit_algebra";
static compiler_fixture fixture;
static arena input, detached, retained[2][2];
static pin_value kept[2][2];
static script_unit saved;
static char saved_text[192];
static int calls;

static void cleanup()
{
    arena_free(&input);
    arena_free(&detached);
}
static pin_value integer(int n)
{
    return (pin_value){ .type = PIN_I32, .i = n };
}
static pin_value boolean(bool n)
{
    return (pin_value){ .type = PIN_BOOL, .b = n };
}
static pin_value text(const char *s, int length)
{
    s = s ? s : "";
    pin_value result;
    const char *error = pin_value_text(&input, &result, s, (size_t)length);
    if (error) {
        abort();
    }
    return result;
}
static bool ready()
{
    REQUIRE(
        fixture.owners[0] && fixture.owners[1] &&
        authoring_packages_valid(fixture.owners[0]) &&
        authoring_packages_valid(fixture.owners[1])
    );
    return true;
}
static bool put(pin_value *row, const char *name, pin_value value)
{
    const type_field *field = pin_type_field_path(pin_value_type(row), name);
    REQUIRE(field && field->type == value.type);
    REQUIRE(!pin_write_field(&input, row->ptr, field, value));
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
static script_unit raw(unit value)
{
    script_unit out = { .kind = value.kind, .count = value.count };
    for (int i = 0; i < value.count; i++) {
        out.terms[i] = (script_unit_term){ value.terms[i].name,
                                           (int)strlen(value.terms[i].name),
                                           value.terms[i].power };
    }
    return out;
}
static bool same_unit(script_unit actual, script_unit expected)
{
    REQUIRE(actual.kind == expected.kind && actual.count == expected.count);
    for (int i = 0; i < expected.count; i++) {
        REQUIRE(
            actual.terms[i].length == expected.terms[i].length &&
            actual.terms[i].power == expected.terms[i].power &&
            !memcmp(
                actual.terms[i].name,
                expected.terms[i].name,
                (size_t)expected.terms[i].length
            )
        );
    }
    return true;
}
static bool pack(int b, script_unit value, pin_value *out)
{
    REQUIRE(value.count >= 0 && value.count <= 8);
    const pin_record *shape = compiler_fixture_record(&fixture, b, PACKAGE, "Term", &input);
    REQUIRE(shape);
    pin_array *array = pin_array_snapshot_allocate(
        &input,
        PIN_STRUCT,
        shape->type.name,
        value.count
    );
    REQUIRE(array);
    array->record = shape;
    for (int i = 0; i < value.count; i++) {
        const script_unit_term *t = &value.terms[i];
        REQUIRE(t->length >= 0 && t->length <= 240);
        pin_value *row = &array->items[i];
        *row = (pin_value){ .type = PIN_STRUCT,
                            .record = shape,
                            .type_name = shape->type.name,
                            .ptr = arena_alloc(&input, shape->type.size) };
        memset(row->ptr, 0, shape->type.size);
        REQUIRE(
            put(row, "name", text(t->name, t->length)) &&
            put(row, "power", integer(t->power))
        );
    }
    pin_value fields[] = { integer(value.kind),
                           { .type = PIN_STRUCT,
                             .container = PIN_CONTAINER_ARRAY,
                             .record = shape,
                             .type_name = shape->type.name,
                             .ptr = array } };
    const script_catalog_variant *tag = variant(b, "Unit", "Value");
    REQUIRE(tag && tag->field_count == 2);
    REQUIRE(!compiler_fixture_union(&input, tag->tag, fields, 2, out));
    return true;
}
static bool call(int b, const char *name, pin_value *args, int count, pin_value *out)
{
    REQUIRE(
        compiler_fixture_call(&fixture, b, PACKAGE, name, args, count, 2000000, &detached, out)
    );
    calls++;
    arena_free(&input);
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
        REQUIRE(arena_contains(storage, array) && array->count >= 0 && array->count <= 8);
        REQUIRE(!array->count || arena_contains(storage, array->items));
        for (int i = 0; i < array->count; i++) {
            REQUIRE(owned(storage, &array->items[i], depth + 1));
        }
        return true;
    }
    REQUIRE(value->container == PIN_CONTAINER_SINGLE);
    if (value->type == PIN_UNION) {
        const pin_union *data = value->ptr;
        REQUIRE(
            data && arena_contains(storage, data) && data->count == 2 &&
            arena_contains(storage, data->values)
        );
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
        REQUIRE(type && type->field_count == 2);
        for (int i = 0; i < type->field_count; i++) {
            pin_value field;
            REQUIRE(!pin_read_field(&input, value->ptr, &type->fields[i], &field));
            REQUIRE(owned(storage, &field, depth + 1));
        }
        return true;
    }
    if (value->type == PIN_STR) {
        REQUIRE(pin_text(*value).bytes && arena_contains(storage, value->text));
        return true;
    }
    REQUIRE(value->type == PIN_I32 || value->type == PIN_BOOL);
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
static bool unit_result(const pin_value *value, script_unit expected)
{
    REQUIRE(value->type == PIN_UNION && value->ptr);
    const pin_union *data = value->ptr;
    REQUIRE(
        data->count == 2 && data->values[0].type == PIN_I32 &&
        data->values[0].i == (int)expected.kind
    );
    REQUIRE(data->values[1].type == PIN_STRUCT && data->values[1].container == PIN_CONTAINER_ARRAY);
    const pin_array *terms = data->values[1].ptr;
    REQUIRE((terms ? terms->count : 0) == expected.count);
    for (int i = 0; i < expected.count; i++) {
        pin_value name, power;
        REQUIRE(
            read(&terms->items[i], "name", &name) &&
            read(&terms->items[i], "power", &power)
        );
        REQUIRE(
            name.type == PIN_STR && pin_text(name).bytes &&
            strlen(pin_text(name).bytes) == (size_t)expected.terms[i].length &&
            !memcmp(
                pin_text(name).bytes,
                expected.terms[i].name,
                (size_t)expected.terms[i].length
            )
        );
        REQUIRE(power.type == PIN_I32 && power.i == expected.terms[i].power);
    }
    return true;
}
static bool tag(int b, const pin_value *value, const char *name, const char *member)
{
    const script_catalog_variant *expected = variant(b, name, member);
    REQUIRE(
        expected && value->type == PIN_UNION && value->ptr &&
        ((const pin_union *)value->ptr)->tag == expected->tag
    );
    return true;
}
TEST_CASE(unit_algebra_parse_matches)
{
    source_package_input files[] = {
        { "unit_algebra/Units.chibi",
          UNIT_ALGEBRA_CHIBI_SOURCE,
          sizeof UNIT_ALGEBRA_CHIBI_SOURCE - 1 },
        { "/sdk/core/strings/Strings.chibi",
          CORE_STRINGS_SOURCE,
          sizeof CORE_STRINGS_SOURCE - 1 },
    };
    REQUIRE(files[0].size < 32768 && files[1].size < 32768);
    REQUIRE(compiler_fixture_prepare(&fixture, files, 2));
    calls = 0;
    for (int i = 0; i < 24; i++) {
        const parse_case *item = &parses[i];
        REQUIRE(!item->text || strlen(item->text) <= 240);
        script_unit oracle = script_unit_parse(item->text);
        REQUIRE(same_unit(oracle, raw(item->expected)));
        for (int b = 0; b < 2; b++) {
            cleanup();
            pin_value args[] = { boolean(item->text != NULL),
                                 text(
                                     item->text ? item->text : "",
                                     item->text ? (int)strlen(item->text) : 0
                                 ) },
                      result;
            REQUIRE(
                call(b, "Parse", args, 2, &result) && tag(b, &result, "Unit", "Value") &&
                owned(&detached, &result, 0) && unit_result(&result, oracle)
            );
            if (i == 23) {
                saved = oracle;
                REQUIRE(!compiler_fixture_snapshot(&retained[b][0], &kept[b][0], &result));
                REQUIRE(owned(&retained[b][0], &kept[b][0], 0));
            }
        }
    }
    return true;
}
TEST_CASE(unit_algebra_same_and_product_match)
{
    REQUIRE(ready());
    for (int i = 0; i < 5; i++) {
        const same_case *item = &equals[i];
        script_unit left = raw(item->left), right = raw(item->right);
        bool oracle = script_unit_same(left, right);
        REQUIRE(oracle == item->expected);
        for (int b = 0; b < 2; b++) {
            cleanup();
            pin_value args[2], result;
            REQUIRE(
                pack(b, left, &args[0]) && pack(b, right, &args[1]) &&
                call(b, "Same", args, 2, &result)
            );
            REQUIRE(result.type == PIN_BOOL && result.b == oracle);
        }
    }
    for (int i = 0; i < 10; i++) {
        const product_case *item = &products[i];
        script_unit left = raw(item->left), right = raw(item->right);
        bool overflow = false;
        script_unit oracle = script_unit_product(left, right, item->sign, &overflow);
        REQUIRE(overflow == item->overflow && same_unit(oracle, raw(item->expected)));
        for (int b = 0; b < 2; b++) {
            cleanup();
            pin_value args[3], result;
            REQUIRE(pack(b, left, &args[0]) && pack(b, right, &args[1]));
            args[2] = integer(item->sign);
            REQUIRE(
                call(b, "Product", args, 3, &result) &&
                tag(b, &result, "ProductResult", "Product") &&
                owned(&detached, &result, 0)
            );
            const pin_union *data = result.ptr;
            REQUIRE(
                data->count == 2 && data->values[1].type == PIN_BOOL &&
                data->values[1].b == overflow
            );
            REQUIRE(
                tag(b, &data->values[0], "Unit", "Value") &&
                unit_result(&data->values[0], oracle)
            );
        }
    }
    return true;
}
TEST_CASE(unit_algebra_format_matches)
{
    REQUIRE(ready());
    memset(long190, 'n', 190);
    long190[190] = 0;
    memset(long189, 'n', 189);
    long189[189] = 0;
    memcpy(full_squared, long189, 189);
    memcpy(full_squared + 189, "²", 3);
    for (int i = 0; i < 9; i++) {
        const format_case *item = &formats[i];
        REQUIRE(item->size > 0 && item->size <= 192);
        char oracle[192];
        script_unit value = raw(item->value);
        script_unit_format(value, oracle, (size_t)item->size);
        REQUIRE(!strcmp(oracle, item->expected));
        for (int b = 0; b < 2; b++) {
            cleanup();
            pin_value args[2], result;
            REQUIRE(pack(b, value, &args[0]));
            args[1] = integer(item->size);
            REQUIRE(
                call(b, "Format", args, 2, &result) && result.type == PIN_STR &&
                owned(&detached, &result, 0)
            );
            REQUIRE(
                !strcmp(pin_text(result).bytes, oracle) &&
                strlen(pin_text(result).bytes) < (size_t)item->size
            );
            if (i == 5) {
                memcpy(saved_text, oracle, strlen(oracle) + 1);
                REQUIRE(!compiler_fixture_snapshot(&retained[b][1], &kept[b][1], &result));
                REQUIRE(owned(&retained[b][1], &kept[b][1], 0));
            }
        }
    }
    return true;
}
TEST_CASE(unit_algebra_results_outlive_owners)
{
    compiler_fixture_free(&fixture);
    cleanup();
    REQUIRE(calls == 96);
    REQUIRE(saved.kind == SCRIPT_UNIT_KNOWN && saved.count == 2);
    REQUIRE(strlen(saved_text) == 191 && !memcmp(saved_text + 189, "²", 2));
    for (int b = 0; b < 2; b++) {
        REQUIRE(owned(&retained[b][0], &kept[b][0], 0) && unit_result(&kept[b][0], saved));
        REQUIRE(
            owned(&retained[b][1], &kept[b][1], 0) && kept[b][1].type == PIN_STR &&
            !strcmp(pin_text(kept[b][1]).bytes, saved_text)
        );
    }
    return true;
}
void test_language_unit_algebra_chibi()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(unit_algebra_parse_matches);
    RUN_TEST_CASE(unit_algebra_same_and_product_match);
    RUN_TEST_CASE(unit_algebra_format_matches);
    RUN_TEST_CASE(unit_algebra_results_outlive_owners);
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
