#include "runtime/comparisons.h"
#include "runtime/conversions.h"
#include "support/arena.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/catalog.h"
#include "types/value_format.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#ifdef CHIBI_TEST_NATIVE_RUNTIME
void test_native_catalog_enable();
#endif

static arena input, output;
static native_capabilities *capabilities;

static void cleanup()
{
    native_capabilities_free(capabilities);
    capabilities = NULL;
    arena_free(&input);
    arena_free(&output);
}
static char *own(const char *text)
{
    size_t size = strlen(text) + 1;
    char *value = arena_alloc(&input, size);
    memcpy(value, text, size);
    return value;
}
static const char *call(const char *name, const pin_value *arguments, int count, pin_value *result)
{
    const script_catalog *catalog = native_capabilities_catalog(capabilities);
    int index = script_catalog_find_capability(catalog, name, SCRIPT_CAPABILITY_FUNCTION);
    if (index < 0) {
        return "Missing canonical capability";
    }
    const script_catalog_capability *declaration = &catalog->capabilities[index];
    uint64_t abi = script_catalog_capability_abi(catalog, declaration);
    const native_capability_binding
        *binding = native_capabilities_find(capabilities, declaration->id, abi);
    if (!abi || !binding || !binding->call || !binding->call->value) {
        return "Missing native value callback";
    }
    return binding->call->value(&output, arguments, count, result);
}

TEST_CASE(native_values_preserve_numeric_contracts_and_owned_results)
{
#ifdef CHIBI_TEST_NATIVE_RUNTIME
    test_native_catalog_enable();
#endif
    native_definition_pack packs[] = {
        NATIVE_COMPARISON_DEFINITION_PACK,
        NATIVE_CONVERSION_DEFINITION_PACK,
    };
#ifdef CHIBI_TEST_NATIVE_RUNTIME
    const type_desc *vector_type = type_catalog_find("CF_V2");
    REQUIRE(vector_type != NULL && vector_type->size == 2 * sizeof(float));
    REQUIRE(vector_type->field_count == 2);
    script_catalog_field fields[2] = { 0 };
    for (int i = 0; i < 2; i++) {
        const type_field *field = &vector_type->fields[i];
        REQUIRE(field->type == PIN_F32);
        char name[64];
        int size = snprintf(name, sizeof name, "%s.%s", vector_type->name, field->name);
        REQUIRE(size > 0 && (size_t)size < sizeof name);
        fields[i] = (script_catalog_field){ .id = script_catalog_id("field", name),
                                            .name = field->name,
                                            .type = { .storage = field->type },
                                            .offset = field->offset };
    }
    uint64_t id = script_catalog_id("type", vector_type->name);
    script_catalog_type nominal = { .id = id,
                                    .name = vector_type->name,
                                    .source_name = vector_type->name,
                                    .type = { .storage = PIN_STRUCT, .nominal = id },
                                    .fields = fields,
                                    .field_count = 2,
                                    .size = vector_type->size,
                                    .layout = type_layout_hash(vector_type) };
    packs[1].base = (script_catalog){ .types = &nominal, .type_count = 1 };
    packs[1].layouts = &vector_type;
    packs[1].layout_count = 1;
#endif
    char error[256] = { 0 };
    capabilities = native_capabilities_from_definition_packs(NULL, 0, packs, 2, error, sizeof error);
    REQUIRE(capabilities != NULL);
    pin_value result;
    pin_value reals[] = { { .type = PIN_F32, .f = 1.5f },
                          { .type = PIN_F64, .d = 1.5 } };
    REQUIRE(call("Equal", reals, 2, &result) == NULL);
    REQUIRE(result.type == PIN_BOOL && result.b);
    reals[0].f = NAN;
    REQUIRE(call("Equal", reals, 2, &result) == NULL);
    REQUIRE(!result.b);
    REQUIRE(call("Not Equal", reals, 2, &result) == NULL);
    REQUIRE(result.b);
    pin_value integers[] = { { .type = PIN_I32, .i = INT32_MAX },
                             { .type = PIN_I32, .i = INT32_MAX - 1 } };
    REQUIRE(call("Integer Greater", integers, 2, &result) == NULL);
    REQUIRE(result.b);
    pin_value strings[] = { { .type = PIN_STR, .str = own("equal") },
                            { .type = PIN_STR, .str = own("equal") } };
    REQUIRE(strings[0].str != strings[1].str);
    REQUIRE(native_compare_Equal(strings, 2, &output).b);
    pin_value enums[] = { { .type = PIN_ENUM, .type_name = "test.mode", .e = 7 },
                          { .type = PIN_ENUM, .type_name = "test.mode", .e = 7 } };
    REQUIRE(type_catalog_enum_find("test.mode") != NULL);
    REQUIRE(native_compare_Equal(enums, 2, &output).b);
    REQUIRE(!strcmp(pin_value_to_string(&enums[0]), "Moving"));
    enums[1].e = 3;
    REQUIRE(!native_compare_Equal(enums, 2, &output).b);
    pin_value number = { .type = PIN_F64, .d = NAN };
    REQUIRE(call("To Integer", &number, 1, &result) == NULL);
    REQUIRE(result.type == PIN_I32 && result.i == 0);
    number.d = INFINITY;
    REQUIRE(call("To Integer64", &number, 1, &result) == NULL);
    REQUIRE(result.type == PIN_I64 && result.i64 == INT64_MAX);
    number.d = -INFINITY;
    REQUIRE(call("To Integer64", &number, 1, &result) == NULL);
    REQUIRE(result.i64 == INT64_MIN);
    number.d = -12.875;
    REQUIRE(call("To Integer", &number, 1, &result) == NULL);
    REQUIRE(result.i == -12);
    REQUIRE(call("To Byte", &number, 1, &result) == NULL);
    REQUIRE(result.type == PIN_U8 && result.u8 == 0);
    number.d = 512;
    REQUIRE(call("To Byte", &number, 1, &result) == NULL);
    REQUIRE(result.u8 == UINT8_MAX);
    pin_value text = { .type = PIN_STR, .str = own("retained") };
    pin_value string, name;
    REQUIRE(call("To String", &text, 1, &string) == NULL);
    REQUIRE(call("To Name", &text, 1, &name) == NULL);
    REQUIRE(string.str != text.str && name.str != text.str);
    const char *long_text = "a name longer than the sixty-four byte display "
                            "buffer keeps every byte";
    pin_value long_name = { .type = PIN_NAME, .str = own(long_text) }, long_string;
    REQUIRE(strlen(long_text) >= 64);
    REQUIRE(call("To String", &long_name, 1, &long_string) == NULL);
#ifdef CHIBI_TEST_NATIVE_RUNTIME
    number.d = 4.5;
    pin_value vector;
    REQUIRE(call("Scalar to CF_V2", &number, 1, &vector) == NULL);
    REQUIRE(vector.type == PIN_STRUCT && !strcmp(vector.type_name, "CF_V2"));
    REQUIRE(vector.ptr != NULL && arena_contains(&output, vector.ptr));
#endif
    arena_free(&input);
    native_capabilities_free(capabilities);
    capabilities = NULL;
    REQUIRE(string.type == PIN_STR && !strcmp(string.str, "retained"));
    REQUIRE(name.type == PIN_NAME && !strcmp(name.str, "retained"));
    REQUIRE(long_string.type == PIN_STR && !strcmp(long_string.str, long_text));
#ifdef CHIBI_TEST_NATIVE_RUNTIME
    const float *coordinates = vector.ptr;
    REQUIRE(coordinates[0] == 4.5f && coordinates[1] == 4.5f);
    REQUIRE(!strcmp(vector.type_name, "CF_V2"));
#endif
    return true;
}

void test_tool_native_values()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(native_values_preserve_numeric_contracts_and_owned_results);
    pu_clear_setup();
}
