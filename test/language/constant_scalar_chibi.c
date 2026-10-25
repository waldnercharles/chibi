#include "constant_scalar_chibi.h"

#include "compiler_fixture.h"
#include "constant_scalar_chibi_source.h"
#include "language/constant_proof.h"
#include "language/scalar_constant.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/array.h"
#include "types/scalar.h"
#include "types/text.h"
#include "types/union.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/* Three cases, one 91-request table, 91 C proof/evaluation oracle calls and
 * 182 authored calls. At most seven expressions and one symbol per request;
 * every call has a two-million-instruction ceiling. Four authored sources are
 * prepared once per backend (eight source parses). The 65536-visit exhaustion
 * workload is deliberately absent. No IO, services, workers or timing loops. */
enum
{
    CASE_COUNT = 91,
    MAX_EXPRESSIONS = 7
};
typedef enum
{
    LITERAL,
    UNARY,
    BINARY,
    CAST,
    CAST_WRAP,
    CAST_CLAMP,
    SYMBOL,
    SHORT_CALL,
    SHORT_DIV,
    MIXED
} form;
typedef struct
{
    const char *name;
    form form;
    script_operator op;
    pin_value left, right;
    pin_type result, expected;
} scalar_case;
#define I32(N) { .type = PIN_I32, .i = (N) }
#define I64(N) { .type = PIN_I64, .i64 = (N) }
#define U32(N) { .type = PIN_U32, .u = (N) }
#define U64(N) { .type = PIN_U64, .u64 = (N) }
#define F32(N) { .type = PIN_F32, .f = (N) }
#define F64(N) { .type = PIN_F64, .d = (N) }
#define BOOL(N) { .type = PIN_BOOL, .b = (N) }
#define TEXT(N)                                                                \
    { .type = PIN_STR,                                                         \
      .text = (N) ? &(                                                         \
                        const pin_string                                       \
                    ){ .bytes = (N),                                           \
                       .length = (N) ? (uint32_t)strlen((N) ? (N) : "") : 0 }  \
                  : NULL }
#define NAME(N) { .type = PIN_NAME, .str = (N) }
static const scalar_case cases[] = {
    { "bool", LITERAL, 0, BOOL(true), BOOL(false), PIN_BOOL, PIN_BOOL },
    { "i8", LITERAL, 0, { .type = PIN_I8, .i8 = INT8_MIN }, I32(0), PIN_I8, PIN_I8 },
    { "u8", LITERAL, 0, { .type = PIN_U8, .u8 = UINT8_MAX }, I32(0), PIN_U8, PIN_U8 },
    { "i16", LITERAL, 0, { .type = PIN_I16, .i16 = INT16_MIN }, I32(0), PIN_I16, PIN_I16 },
    { "u16", LITERAL, 0, { .type = PIN_U16, .u16 = UINT16_MAX }, I32(0), PIN_U16, PIN_U16 },
    { "i32", LITERAL, 0, I32(INT32_MIN), I32(0), PIN_I32, PIN_I32 },
    { "u32", LITERAL, 0, U32(UINT32_MAX), I32(0), PIN_U32, PIN_U32 },
    { "i64", LITERAL, 0, I64(INT64_MIN), I32(0), PIN_I64, PIN_I64 },
    { "u64", LITERAL, 0, U64(UINT64_MAX), I32(0), PIN_U64, PIN_U64 },
    { "float-negative-zero", LITERAL, 0, F32(-0.0f), I32(0), PIN_F32, PIN_F32 },
    { "double-nan-payload", LITERAL, 0, F64(NAN), I32(0), PIN_F64, PIN_F64 },
    { "owned-name", LITERAL, 0, NAME("caf\xc3\xa9"), I32(0), PIN_NAME, PIN_NAME },
    { "null-text", LITERAL, 0, TEXT(NULL), I32(0), PIN_STR, PIN_STR },
    { "i32-add-overflow", BINARY, SCRIPT_OP_ADD, I32(INT32_MAX), I32(1), PIN_I32, PIN_I32 },
    { "i32-sub-overflow", BINARY, SCRIPT_OP_SUB, I32(INT32_MIN), I32(1), PIN_I32, PIN_I32 },
    { "i32-mul-overflow", BINARY, SCRIPT_OP_MUL, I32(INT32_MAX), I32(2), PIN_I32, PIN_I32 },
    { "i32-neg-min", UNARY, SCRIPT_OP_NEG, I32(INT32_MIN), I32(0), PIN_I32, PIN_I32 },
    { "i64-add-overflow", BINARY, SCRIPT_OP_ADD, I64(INT64_MAX), I64(1), PIN_I64, PIN_I64 },
    { "u32-add-overflow", BINARY, SCRIPT_OP_ADD, U32(UINT32_MAX), U32(1), PIN_U32, PIN_U32 },
    { "u64-add-overflow", BINARY, SCRIPT_OP_ADD, U64(UINT64_MAX), U64(1), PIN_U64, PIN_U64 },
    { "u64-mul-overflow", BINARY, SCRIPT_OP_MUL, U64(UINT64_MAX), U64(2), PIN_U64, PIN_U64 },
    { "u64-sub", BINARY, SCRIPT_OP_SUB, U64(2), U64(1), PIN_U64, PIN_U64 },
    { "i32-div-zero", BINARY, SCRIPT_OP_DIV, I32(1), I32(0), PIN_I32, PIN_I32 },
    { "i32-div-min", BINARY, SCRIPT_OP_DIV, I32(INT32_MIN), I32(-1), PIN_I32, PIN_I32 },
    { "i64-div-zero", BINARY, SCRIPT_OP_DIV, I64(1), I64(0), PIN_I64, PIN_I64 },
    { "i64-div-min", BINARY, SCRIPT_OP_DIV, I64(INT64_MIN), I64(-1), PIN_I64, PIN_I64 },
    { "i32-mod-zero", BINARY, SCRIPT_OP_MOD, I32(1), I32(0), PIN_I32, PIN_I32 },
    { "i32-mod-min", BINARY, SCRIPT_OP_MOD, I32(INT32_MIN), I32(-1), PIN_I32, PIN_I32 },
    { "i32-lt", BINARY, SCRIPT_OP_LT, I32(-1), I32(1), PIN_BOOL, PIN_BOOL },
    { "i32-le", BINARY, SCRIPT_OP_LE, I32(1), I32(1), PIN_BOOL, PIN_BOOL },
    { "i32-gt", BINARY, SCRIPT_OP_GT, I32(2), I32(1), PIN_BOOL, PIN_BOOL },
    { "i32-ge", BINARY, SCRIPT_OP_GE, I32(1), I32(1), PIN_BOOL, PIN_BOOL },
    { "name-text-equal", BINARY, SCRIPT_OP_EQ, NAME("same"), TEXT("same"), PIN_BOOL, PIN_BOOL },
    { "name-text-different", BINARY, SCRIPT_OP_NE, NAME("same"), TEXT("other"), PIN_BOOL, PIN_BOOL },
    { "null-empty-equal", BINARY, SCRIPT_OP_EQ, TEXT(NULL), TEXT(""), PIN_BOOL, PIN_BOOL },
    { "nan-not-equal", BINARY, SCRIPT_OP_EQ, F64(NAN), F64(NAN), PIN_BOOL, PIN_BOOL },
    { "shift-negative-count", BINARY, SCRIPT_OP_SHIFT_RIGHT, I32(-4), I32(-1), PIN_I32, PIN_I32 },
    { "shift-past-width-int", BINARY, SCRIPT_OP_SHIFT_LEFT, I32(1), I32(32), PIN_I32, PIN_I32 },
    { "shift-past-width-int8",
      BINARY,
      SCRIPT_OP_SHIFT_LEFT,
      { .type = PIN_I8, .i8 = 1 },
      { .type = PIN_I8, .i8 = 8 },
      PIN_I8,
      PIN_I8 },
    { "shift-past-width-byte",
      BINARY,
      SCRIPT_OP_SHIFT_RIGHT,
      { .type = PIN_U8, .u8 = 1 },
      { .type = PIN_U8, .u8 = 8 },
      PIN_U8,
      PIN_U8 },
    { "shift-past-width-int16",
      BINARY,
      SCRIPT_OP_SHIFT_RIGHT,
      { .type = PIN_I16, .i16 = -1 },
      { .type = PIN_I16, .i16 = 16 },
      PIN_I16,
      PIN_I16 },
    { "shift-past-width-uint16",
      BINARY,
      SCRIPT_OP_SHIFT_LEFT,
      { .type = PIN_U16, .u16 = 1 },
      { .type = PIN_U16, .u16 = 16 },
      PIN_U16,
      PIN_U16 },
    { "shift-past-width-uint", BINARY, SCRIPT_OP_SHIFT_RIGHT, U32(1), U32(32), PIN_U32, PIN_U32 },
    { "shift-past-width-int64", BINARY, SCRIPT_OP_SHIFT_LEFT, I64(1), I64(64), PIN_I64, PIN_I64 },
    { "shift-past-width-uint64", BINARY, SCRIPT_OP_SHIFT_RIGHT, U64(1), U64(64), PIN_U64, PIN_U64 },
    { "shift-count-too-large", BINARY, SCRIPT_OP_SHIFT_LEFT, U64(1), U64(UINT64_MAX), PIN_U64, PIN_U64 },
    { "shift-unfit-before-result-widening", BINARY, SCRIPT_OP_SHIFT_LEFT, I32(1), I32(32), PIN_I32, PIN_I64 },
    { "byte-shift-unfit-before-result-widening",
      BINARY,
      SCRIPT_OP_SHIFT_LEFT,
      { .type = PIN_U8, .u8 = 128 },
      { .type = PIN_U8, .u8 = 1 },
      PIN_U8,
      PIN_I64 },
    { "signed-right-shift-rounds-down", BINARY, SCRIPT_OP_SHIFT_RIGHT, I32(-3), I32(1), PIN_I32, PIN_I32 },
    { "left-shift-past-int64", BINARY, SCRIPT_OP_SHIFT_LEFT, I64(1), I64(63), PIN_I64, PIN_I64 },
    { "zero-shift-preserves-value", BINARY, SCRIPT_OP_SHIFT_LEFT, I32(-3), I32(0), PIN_I32, PIN_I32 },
    { "unsigned-right-shift", BINARY, SCRIPT_OP_SHIFT_RIGHT, U64(UINT64_MAX), U64(63), PIN_U64, PIN_U64 },
    { "byte-not", UNARY, SCRIPT_OP_BIT_NOT, { .type = PIN_U8, .u8 = UINT8_MAX }, I32(0), PIN_U8, PIN_U8 },
    { "int16-and",
      BINARY,
      SCRIPT_OP_BIT_AND,
      { .type = PIN_I16, .i16 = -128 },
      { .type = PIN_I16, .i16 = 15 },
      PIN_I16,
      PIN_I16 },
    { "uint64-xor",
      BINARY,
      SCRIPT_OP_BIT_XOR,
      U64(UINT64_C(0xffffffffffffff00)),
      U64(UINT64_C(0xffffffffffffff0f)),
      PIN_U64,
      PIN_U64 },
    { "int-to-uint64", CAST, 0, I32(-1), I32(0), PIN_U64, PIN_U64 },
    { "int-to-uint64-wrap", CAST_WRAP, 0, I32(-1), I32(0), PIN_U64, PIN_U64 },
    { "int-to-uint64-clamp", CAST_CLAMP, 0, I32(-1), I32(0), PIN_U64, PIN_U64 },
    { "int64-to-byte-wrap", CAST_WRAP, 0, I64(300), I32(0), PIN_U8, PIN_U8 },
    { "int64-to-byte-clamp", CAST_CLAMP, 0, I64(300), I32(0), PIN_U8, PIN_U8 },
    { "uint64-to-int-exact", CAST, 0, U64(42), I32(0), PIN_I32, PIN_I32 },
    { "int-to-int64", CAST, 0, I32(INT32_MIN), I32(0), PIN_I64, PIN_I64 },
    { "int64-double-rounding", CAST, 0, I64(INT64_C(9007199254740993)), I32(0), PIN_F64, PIN_F64 },
    { "uint64-double-rounding", CAST, 0, U64(UINT64_MAX), I32(0), PIN_F64, PIN_F64 },
    { "nan-int-zero", CAST, 0, F64(NAN), I32(0), PIN_I32, PIN_I32 },
    { "infinity-int-max", CAST, 0, F64(INFINITY), I32(0), PIN_I32, PIN_I32 },
    { "negative-infinity-int64-min", CAST, 0, F64(-INFINITY), I32(0), PIN_I64, PIN_I64 },
    { "double-byte-truncation", CAST, 0, F64(255.9), I32(0), PIN_U8, PIN_U8 },
    { "negative-double-byte-clamp", CAST, 0, F64(-1), I32(0), PIN_U8, PIN_U8 },
    { "float-double-exact", CAST, 0, F32(1.5f), I32(0), PIN_F64, PIN_F64 },
    { "double-float-tie", CAST, 0, F64(0x1.000001p0), I32(0), PIN_F32, PIN_F32 },
    { "double-int64-upper-clamp", CAST, 0, F64(0x1p63), I32(0), PIN_I64, PIN_I64 },
    { "float-add-rounding", BINARY, SCRIPT_OP_ADD, F32(16777216.0f), F32(1.0f), PIN_F32, PIN_F32 },
    { "double-neg-zero", UNARY, SCRIPT_OP_NEG, F64(0.0), I32(0), PIN_F64, PIN_F64 },
    { "double-div-zero", BINARY, SCRIPT_OP_DIV, F64(1.0), F64(0.0), PIN_F64, PIN_F64 },
    { "symbol-negative-zero", SYMBOL, 0, F64(-0.0), I32(0), PIN_F64, PIN_F64 },
    { "mixed-int-float", MIXED, SCRIPT_OP_ADD, I32(3), F32(0.5f), PIN_F32, PIN_F32 },
    { "byte-int8-add",
      BINARY,
      SCRIPT_OP_ADD,
      { .type = PIN_U8, .u8 = 200 },
      { .type = PIN_I8, .i8 = -100 },
      PIN_I32,
      PIN_I32 },
    { "int-uint-add", BINARY, SCRIPT_OP_ADD, I32(-1), U32(1), PIN_I64, PIN_I64 },
    { "literal-unfit-result", LITERAL, 0, I64(300), I32(0), PIN_I64, PIN_U8 },
    { "short-circuit-does-not-skip-proof",
      SHORT_CALL,
      SCRIPT_OP_AND,
      BOOL(false),
      BOOL(false),
      PIN_BOOL,
      PIN_BOOL },
    { "and-skips-bad-division", SHORT_DIV, SCRIPT_OP_AND, BOOL(false), BOOL(false), PIN_BOOL, PIN_BOOL },
    { "or-skips-bad-division", SHORT_DIV, SCRIPT_OP_OR, BOOL(true), BOOL(false), PIN_BOOL, PIN_BOOL },
    { "and-evaluates-bad-division", SHORT_DIV, SCRIPT_OP_AND, BOOL(true), BOOL(false), PIN_BOOL, PIN_BOOL },
    { "or-evaluates-bad-division", SHORT_DIV, SCRIPT_OP_OR, BOOL(false), BOOL(false), PIN_BOOL, PIN_BOOL },
    { "widened-add-result", BINARY, SCRIPT_OP_ADD, I32(INT32_MAX), I32(1), PIN_I64, PIN_I64 },
    { "widened-mul-result", BINARY, SCRIPT_OP_MUL, I32(INT32_MAX), I32(2), PIN_I64, PIN_I64 },
    { "widened-neg-result", UNARY, SCRIPT_OP_NEG, I32(INT32_MIN), I32(0), PIN_I64, PIN_I64 },
    { "widened-bit-not-result",
      UNARY,
      SCRIPT_OP_BIT_NOT,
      { .type = PIN_I8, .i8 = INT8_MIN },
      I32(0),
      PIN_I64,
      PIN_I64 },
    { "widened-float-result", BINARY, SCRIPT_OP_ADD, F32(16777216.0f), F32(1.0f), PIN_F64, PIN_F64 },
    { "narrowed-double-result", BINARY, SCRIPT_OP_ADD, F64(1.0), F64(0x1p-24), PIN_F32, PIN_F32 },
};
_Static_assert(sizeof cases / sizeof cases[0] == CASE_COUNT, "scalar request count");
#undef I32
#undef I64
#undef U32
#undef U64
#undef F32
#undef F64
#undef BOOL
#undef TEXT
#undef NAME

static const char IR[] = "project:constant_ir", SCALAR[] = "project:constant_scalar",
                  FOLD[] = "project:constant_folding";
static compiler_fixture fixture;
static arena input, detached, retained[2][2], scalar_expected;
static pin_value kept[2][2];
static script_expression expressions[MAX_EXPRESSIONS];
static script_symbol symbol;
static script_analysis analysis;

static void cleanup()
{
    arena_free(&input);
    arena_free(&detached);
}
static pin_value integer(int value)
{
    return (pin_value){ .type = PIN_I32, .i = value };
}
static pin_value boolean(bool value)
{
    return (pin_value){ .type = PIN_BOOL, .b = value };
}
static bool put(pin_value *row, const char *name, pin_value value)
{
    const type_field *field = pin_type_field_path(pin_value_type(row), name);
    REQUIRE(field && field->type == value.type);
    REQUIRE(!pin_write_field(&input, row->ptr, field, value));
    return true;
}
static bool rows(int backend, const char *name, int count, pin_value *out)
{
    const pin_record *shape = compiler_fixture_record(&fixture, backend, IR, name, &input);
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
static const script_catalog_type *exported_type(int backend, const char *identity, const char *name)
{
    const authoring_package *package = compiler_fixture_package(&fixture, backend, identity);
    return package && package->exports && package->exports->metadata
             ? script_catalog_type_by_id(
                   package->exports->metadata,
                   compiler_fixture_export(&fixture, backend, identity, name)
               )
             : NULL;
}
static const script_catalog_variant *named_variant(const script_catalog_type *type, const char *name)
{
    for (int i = 0; type && name && i < type->variant_count; i++) {
        if (!strcmp(type->variants[i].name, name)) {
            return &type->variants[i];
        }
    }
    return NULL;
}
static const char *scalar_name(pin_type type)
{
    switch (type) {
        case PIN_BOOL: return "Bool";
        case PIN_I8: return "I8";
        case PIN_U8: return "U8";
        case PIN_I16: return "I16";
        case PIN_U16: return "U16";
        case PIN_I32: return "I32";
        case PIN_U32: return "U32";
        case PIN_I64: return "I64";
        case PIN_U64: return "U64";
        case PIN_F32: return "F32";
        case PIN_F64: return "F64";
        case PIN_NAME: return "Name";
        case PIN_STR: return "Text";
        default: return NULL;
    }
}
static bool scalar_value(int backend, pin_value value, pin_value *out)
{
    const char *name = scalar_name(value.type);
    const script_catalog_variant
        *variant = named_variant(exported_type(backend, SCALAR, "Value"), name);
    bool text = value.type == PIN_NAME || value.type == PIN_STR;
    REQUIRE(variant && variant->field_count == (text ? 2 : 1));
    pin_value fields[2] = {
        value,
        boolean(text && (value.type == PIN_NAME ? value.str != NULL : value.text != NULL))
    };
    if (text) {
        pin_string bytes = pin_text(value);
        REQUIRE(!pin_value_text(&input, &fields[0], bytes.bytes, bytes.length));
    }
    REQUIRE(!compiler_fixture_union(&input, variant->tag, fields, variant->field_count, out));
    return true;
}
static void literal(int index, pin_value value)
{
    expressions[index] = (script_expression){ .kind = SCRIPT_EXPR_CONSTANT,
                                              .type = { .storage = value.type },
                                              .constant = value,
                                              .left = -1,
                                              .right = -1,
                                              .symbol = -1 };
}
static void build(const scalar_case *item)
{
    memset(expressions, 0, sizeof expressions);
    symbol = (script_symbol){ .kind = SCRIPT_CONSTANT, .initializer = 1 };
    analysis = (script_analysis){ .expressions = expressions,
                                  .expression_count = 3,
                                  .symbols = &symbol };
    literal(0, item->left);
    literal(1, item->left);
    literal(2, item->right);
    expressions[0].type.storage = item->result;
    expressions[0].op = item->op;
    expressions[0].left = 1;
    expressions[0].right = 2;
    switch (item->form) {
        case LITERAL: break;
        case UNARY: expressions[0].kind = SCRIPT_EXPR_UNARY; break;
        case BINARY: expressions[0].kind = SCRIPT_EXPR_BINARY; break;
        case CAST:
        case CAST_WRAP:
        case CAST_CLAMP:
            expressions[0].kind = SCRIPT_EXPR_CAST;
            expressions[0].conversion = item->form == CAST_WRAP ? BEHAVIOR_CAST_WRAP
                                      : item->form == CAST_CLAMP
                                          ? BEHAVIOR_CAST_CLAMP
                                          : BEHAVIOR_CAST_EXACT;
            break;
        case SYMBOL:
            expressions[0].kind = SCRIPT_EXPR_SYMBOL;
            expressions[0].symbol = 0;
            analysis.symbol_count = 1;
            break;
        case SHORT_CALL:
            expressions[0].kind = SCRIPT_EXPR_BINARY;
            expressions[2].kind = SCRIPT_EXPR_CALL;
            break;
        case MIXED:
            analysis.expression_count = 4;
            expressions[0].kind = SCRIPT_EXPR_BINARY;
            literal(3, item->left);
            literal(1, (pin_value){ .type = PIN_F32, .f = 0 });
            expressions[1].kind = SCRIPT_EXPR_CAST;
            expressions[1].left = 3;
            break;
        case SHORT_DIV:
            analysis.expression_count = 7;
            expressions[0].kind = SCRIPT_EXPR_BINARY;
            literal(2, boolean(false));
            expressions[2].kind = SCRIPT_EXPR_BINARY;
            expressions[2].op = SCRIPT_OP_EQ;
            expressions[2].left = 3;
            expressions[2].right = 6;
            literal(3, integer(0));
            expressions[3].kind = SCRIPT_EXPR_BINARY;
            expressions[3].op = SCRIPT_OP_DIV;
            expressions[3].left = 4;
            expressions[3].right = 5;
            literal(4, integer(1));
            literal(5, integer(0));
            literal(6, integer(0));
            break;
    }
    if (!strcmp(item->name, "double-nan-payload")) {
        uint64_t bits = UINT64_C(0x7ff8000012345678);
        memcpy(&expressions[0].constant.d, &bits, sizeof bits);
    }
}
static bool pack(int backend, pin_type expected, pin_value args[7])
{
    REQUIRE(rows(backend, "Expression", analysis.expression_count, &args[0]));
    REQUIRE(rows(backend, "Type", analysis.expression_count, &args[1]));
    REQUIRE(rows(backend, "Symbol", analysis.symbol_count, &args[2]));
    REQUIRE(rows(backend, "Argument", 0, &args[3]));
    pin_array *exprs = args[0].ptr, *types = args[1].ptr;
    pin_array *
        literals = pin_array_snapshot_allocate(&input, PIN_UNION, NULL, analysis.expression_count);
    REQUIRE(literals);
    args[4] = (pin_value){ .type = PIN_UNION,
                           .container = PIN_CONTAINER_ARRAY,
                           .ptr = literals };
    for (int i = 0; i < analysis.expression_count; i++) {
        const script_expression *e = &expressions[i];
        pin_value *row = &exprs->items[i], *type = &types->items[i];
        REQUIRE(
            put(row, "kind", integer(e->kind)) && put(row, "op", integer(e->op)) &&
            put(row, "literal", integer(i)) && put(row, "type", integer(i)) &&
            put(row, "symbol", integer(e->symbol)) &&
            put(row, "left", integer(e->left)) &&
            put(row, "right", integer(e->right)) &&
            put(row, "arg_start", integer(0)) && put(row, "arg_count", integer(0)) &&
            put(row, "effects", integer((int)e->effects)) &&
            put(row, "conversion", integer((int)e->conversion))
        );
        REQUIRE(
            put(type, "storage", integer(e->type.storage)) &&
            put(type, "container", integer(e->type.container)) &&
            put(type, "flags", integer((int)e->type.flags)) &&
            put(type, "has_nominal", boolean(e->type.nominal != 0))
        );
        REQUIRE(scalar_value(backend, e->constant, &literals->items[i]));
    }
    if (analysis.symbol_count) {
        pin_array *symbols = args[2].ptr;
        REQUIRE(
            put(&symbols->items[0], "kind", integer(symbol.kind)) &&
            put(&symbols->items[0], "initializer", integer(symbol.initializer))
        );
    }
    args[5] = integer(0);
    args[6] = integer(expected);
    return true;
}
static bool value_matches(int backend, const pin_value *value, pin_value expected)
{
    REQUIRE(value->type == PIN_UNION && value->ptr);
    const script_catalog_variant *variant = named_variant(
        exported_type(backend, SCALAR, "Value"),
        scalar_name(expected.type)
    );
    const pin_union *data = value->ptr;
    REQUIRE(variant && data->tag == variant->tag && data->count == variant->field_count);
    if (behavior_scalar_string(expected.type)) {
        REQUIRE(
            data->count == 2 && data->values[0].type == PIN_STR &&
            data->values[1].type == PIN_BOOL
        );
        REQUIRE(
            data->values[1].b ==
            (expected.type == PIN_NAME ? expected.str != NULL : expected.text != NULL)
        );
        REQUIRE(
            pin_text(data->values[0]).bytes &&
            !strcmp(pin_text(data->values[0]).bytes, pin_text(expected).bytes)
        );
    } else {
        REQUIRE(data->count == 1 && data->values[0].type == expected.type);
        if (expected.type == PIN_F32) {
            REQUIRE(!memcmp(&data->values[0].f, &expected.f, sizeof expected.f));
        } else if (expected.type == PIN_F64) {
            REQUIRE(!memcmp(&data->values[0].d, &expected.d, sizeof expected.d));
        } else {
            REQUIRE(behavior_scalar_bits(data->values[0]) == behavior_scalar_bits(expected));
        }
    }
    return true;
}
static bool owned(const arena *storage, const pin_value *value)
{
    REQUIRE(value->container == PIN_CONTAINER_SINGLE);
    if (value->type == PIN_UNION) {
        REQUIRE(value->ptr && arena_contains(storage, value->ptr));
        const pin_union *data = value->ptr;
        REQUIRE(data->count > 0 && arena_contains(storage, data->values));
        for (int i = 0; i < data->count; i++) {
            REQUIRE(owned(storage, &data->values[i]));
        }
    } else if (behavior_scalar_string(value->type)) {
        REQUIRE(pin_text(*value).bytes && arena_contains(storage, value->text));
    }
    return true;
}
static bool compare(int index)
{
    REQUIRE(fixture.owners[0] && fixture.owners[1]);
    REQUIRE(
        authoring_packages_valid(fixture.owners[0]) &&
        authoring_packages_valid(fixture.owners[1])
    );
    const scalar_case *item = &cases[index];
    arena_free(&scalar_expected);
    build(item);
    constant_proof proofs[MAX_EXPRESSIONS] = { 0 };
    int steps = SCRIPT_SCALAR_CONSTANT_STEPS;
    pin_value expected = { .type = PIN_I32, .i = 73 };
    const char *error = !script_constant_prove(&analysis, proofs, 1, 0, &steps, true)
                          ? "requires bounded compile-time values and operators"
                          : script_scalar_evaluate(&scalar_expected, &analysis, 0, item->expected, &expected);
    /* Integer operators compute exactly: only the result must fit. */
    static const char *const unfit[] = {
        "i32-add-overflow",
        "i32-neg-min",
        "i32-div-min",
        "u64-mul-overflow",
        "shift-unfit-before-result-widening",
        "byte-shift-unfit-before-result-widening",
        "left-shift-past-int64"
    };
    for (size_t i = 0; i < sizeof unfit / sizeof *unfit; i++) {
        if (!strcmp(item->name, unfit[i])) {
            REQUIRE(error && !strcmp(error, "integer constant does not fit its type"));
        }
    }
    if (!strcmp(item->name, "shift-negative-count")) {
        REQUIRE(error && !strcmp(error, "integer constant shift count must not be negative"));
    }
    if (!strcmp(item->name, "i32-div-zero")) {
        REQUIRE(
            error && !strcmp(error, "integer constant divides by zero, shifts by a negative count or negates an unsigned value")
        );
    }
    if (!strcmp(item->name, "shift-count-too-large")) {
        REQUIRE(error && !strcmp(error, "integer constant shift count is too large"));
    }
    if (!strncmp(item->name, "shift-past-width-", strlen("shift-past-width-"))) {
        bool negative = behavior_scalar_signed(item->left.type) &&
                        (int64_t)behavior_scalar_bits(item->left) < 0;
        REQUIRE(
            item->op == SCRIPT_OP_SHIFT_LEFT
                ? error && !strcmp(error, "integer constant does not fit its type")
                : !error && behavior_scalar_bits(expected) == (negative ? UINT64_MAX : 0)
        );
    }
    if (!strcmp(item->name, "i32-mod-min") || !strcmp(item->name, "u64-sub")) {
        REQUIRE(!error && behavior_scalar_bits(expected) == (item->op == SCRIPT_OP_SUB));
    }
    if (!strcmp(item->name, "int64-to-byte-clamp")) {
        REQUIRE(!error && expected.type == PIN_U8 && expected.u8 == UINT8_MAX);
    }
    if (!strcmp(item->name, "signed-right-shift-rounds-down") ||
        !strcmp(item->name, "zero-shift-preserves-value")) {
        REQUIRE(!error && expected.type == PIN_I32);
        REQUIRE(expected.i == (!strcmp(item->name, "zero-shift-preserves-value") ? -3 : -2));
    }
    if (!strcmp(item->name, "widened-add-result") ||
        !strcmp(item->name, "widened-neg-result")) {
        REQUIRE(!error && expected.type == PIN_I64);
        REQUIRE(expected.i64 == INT64_C(2147483648));
    }
    if (!strcmp(item->name, "widened-mul-result")) {
        REQUIRE(!error && expected.type == PIN_I64 && expected.i64 == INT64_C(4294967294));
    }
    if (!strcmp(item->name, "widened-bit-not-result")) {
        REQUIRE(!error && expected.type == PIN_I64 && expected.i64 == 127);
    }
    if (!strcmp(item->name, "widened-float-result")) {
        REQUIRE(!error && expected.type == PIN_F64 && expected.d == 16777216.0);
    }
    if (!strcmp(item->name, "narrowed-double-result")) {
        REQUIRE(!error && expected.type == PIN_F32 && expected.f == 1.0f);
    }
    for (int b = 0; b < 2; b++) {
        cleanup();
        pin_value args[7], result;
        REQUIRE(pack(b, item->expected, args));
        REQUIRE(
            compiler_fixture_call(&fixture, b, FOLD, "Fold", args, 7, 2000000, &detached, &result)
        );
        arena_free(&input);
        REQUIRE(result.type == PIN_UNION && result.ptr);
        REQUIRE(owned(&detached, &result));
        const script_catalog_variant *variant = named_variant(
            exported_type(b, SCALAR, "Result"),
            error ? "Failed" : "Folded"
        );
        const pin_union *data = result.ptr;
        if (!variant || data->tag != variant->tag) {
            fprintf(stderr, "scalar fold tag mismatch: %s backend%d\n", item->name, b);
        }
        REQUIRE(variant && data->tag == variant->tag && data->count == 1);
        if (error) {
            REQUIRE(
                data->values[0].type == PIN_STR && pin_text(data->values[0]).bytes &&
                !strcmp(pin_text(data->values[0]).bytes, error)
            );
        } else {
            REQUIRE(value_matches(b, &data->values[0], expected));
        }
        if (index == 11 || index == 12) {
            REQUIRE(
                !compiler_fixture_snapshot(&retained[b][index - 11], &kept[b][index - 11], &result)
            );
            REQUIRE(owned(&retained[b][index - 11], &kept[b][index - 11]));
        }
    }
    return true;
}
TEST_CASE(constant_scalar_literals_match)
{
    source_package_input files[] = {
        { "constant_ir/IR.chibi", SCALAR_IR_CHIBI_SOURCE, sizeof SCALAR_IR_CHIBI_SOURCE - 1 },
        { "constant_admissibility/Proof.chibi",
          SCALAR_PROOF_CHIBI_SOURCE,
          sizeof SCALAR_PROOF_CHIBI_SOURCE - 1 },
        { "constant_scalar/Scalar.chibi",
          CONSTANT_SCALAR_CHIBI_SOURCE,
          sizeof CONSTANT_SCALAR_CHIBI_SOURCE - 1 },
        { "constant_folding/Fold.chibi",
          CONSTANT_FOLDING_CHIBI_SOURCE,
          sizeof CONSTANT_FOLDING_CHIBI_SOURCE - 1 },
    };
    REQUIRE(sizeof cases / sizeof cases[0] == CASE_COUNT);
    for (int i = 0; i < 4; i++) {
        REQUIRE(files[i].size < 65536);
    }
    REQUIRE(compiler_fixture_prepare(&fixture, files, 4));
    for (int i = 0; i < 13; i++) {
        REQUIRE(compare(i));
    }
    return true;
}
TEST_CASE(constant_scalar_operators_and_failures_match)
{
    for (int i = 13; i < CASE_COUNT; i++) {
        REQUIRE(compare(i));
    }
    return true;
}
TEST_CASE(constant_scalar_text_results_outlive_owners)
{
    compiler_fixture_free(&fixture);
    cleanup();
    for (int b = 0; b < 2; b++) {
        for (int i = 0; i < 2; i++) {
            REQUIRE(kept[b][i].type == PIN_UNION && kept[b][i].ptr);
            REQUIRE(owned(&retained[b][i], &kept[b][i]));
            const pin_union *result = kept[b][i].ptr;
            REQUIRE(
                result->count == 1 && result->values[0].type == PIN_UNION &&
                result->values[0].ptr
            );
            const pin_union *value = result->values[0].ptr;
            REQUIRE(
                value->count == 2 && value->values[0].type == PIN_STR &&
                value->values[1].type == PIN_BOOL
            );
            REQUIRE(value->values[1].b == (i == 0));
            REQUIRE(
                pin_text(value->values[0]).bytes &&
                !strcmp(pin_text(value->values[0]).bytes, i ? "" : "caf\xc3\xa9")
            );
        }
    }
    return true;
}
void test_language_constant_scalar_chibi()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(constant_scalar_literals_match);
    RUN_TEST_CASE(constant_scalar_operators_and_failures_match);
    RUN_TEST_CASE(constant_scalar_text_results_outlive_owners);
    pu_clear_setup();
    cleanup();
    compiler_fixture_free(&fixture);
    arena_free(&scalar_expected);
    for (int b = 0; b < 2; b++) {
        for (int i = 0; i < 2; i++) {
            arena_free(&retained[b][i]);
            kept[b][i] = (pin_value){ 0 };
        }
    }
}
