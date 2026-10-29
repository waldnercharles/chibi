#include "language/scalar_constant.h"
#include "language/semantic_internal.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "types/array.h"

#include <errno.h>
#include <float.h>
#include <inttypes.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Beside another integer, a char takes part as int, or as uint beside a
 * 32- or 64-bit unsigned operand, since every Unicode scalar fits both; the
 * result is then the other operand's type, as int's would be. */
script_type script_analyzer_char_operand(script_type operand, script_type other)
{
    if (!script_type_char(operand) || script_type_char(other) ||
        (other.flags & SCRIPT_TYPE_UNTYPED)) {
        return operand;
    }
    bool wide_unsigned = other.storage == PIN_U32 || other.storage == PIN_U64;
    return (script_type){ .storage = wide_unsigned ? PIN_U32 : PIN_I32,
                          .flags = operand.flags };
}
/* Two untyped constants combine as a char, then a floating number, then the
 * wider integer, as Go's untyped constant kinds do. */
static script_type untyped_common(script_type left, script_type right)
{
    if (script_type_char(left) || script_type_char(right)) {
        return script_char_type();
    }
    if (left.storage > PIN_U64 || right.storage > PIN_U64) {
        return (script_type){ .storage = PIN_F32 };
    }
    pin_type common = behavior_scalar_common_integer(left.storage, right.storage);
    return (script_type){ .storage = common == PIN_FLOW ? PIN_U64 : common };
}
script_type script_analyzer_arithmetic_type(script_type left, script_type right)
{
    if (left.flags & SCRIPT_TYPE_UNTYPED) {
        if (!(right.flags & SCRIPT_TYPE_UNTYPED)) {
            return right;
        }
        script_type untyped = untyped_common(left, right);
        untyped.flags = SCRIPT_TYPE_UNTYPED;
        return untyped;
    }
    if (right.flags & SCRIPT_TYPE_UNTYPED) {
        return left;
    }
    left = script_analyzer_char_operand(left, right);
    right = script_analyzer_char_operand(right, left);
    if (left.storage == PIN_F64 || right.storage == PIN_F64) {
        return (script_type){ .storage = PIN_F64 };
    }
    if (left.storage == PIN_F32 || right.storage == PIN_F32) {
        return (script_type){ .storage = PIN_F32 };
    }
    bool integers = left.storage >= PIN_I8 && left.storage <= PIN_U64 &&
                    right.storage >= PIN_I8 && right.storage <= PIN_U64;
    if (!integers) {
        return left.storage >= right.storage ? left : right;
    }
    pin_type common = behavior_scalar_common_integer(left.storage, right.storage);
    if (common == PIN_FLOW) {
        return script_analyzer_bad_type();
    }
    return common == left.storage  ? left
         : common == right.storage ? right
                                   : (script_type){ .storage = common };
}
bool script_analyzer_numeric_aggregate(const script_catalog *catalog, script_type type, int depth)
{
    if (script_type_numeric(type)) {
        return true;
    }
    if (depth >= SCRIPT_MAX_DEPTH || type.storage != PIN_STRUCT ||
        type.container != PIN_CONTAINER_SINGLE) {
        return false;
    }
    const script_catalog_type *record = script_catalog_type_by_id(catalog, type.nominal);
    if (!record || !record->field_count) {
        return false;
    }
    for (int i = 0; i < record->field_count; i++) {
        if (!script_analyzer_numeric_aggregate(catalog, record->fields[i].type, depth + 1)) {
            return false;
        }
    }
    return true;
}
static bool compatible_aggregate(const script_catalog *catalog, script_type x, script_type y, int depth)
{
    if (script_type_numeric(x) || script_type_numeric(y)) {
        return script_analyzer_numeric_aggregate(catalog, x, depth) &&
               script_analyzer_numeric_aggregate(catalog, y, depth);
    }
    if (depth >= SCRIPT_MAX_DEPTH) {
        return false;
    }
    const script_catalog_type *left = script_catalog_type_by_id(catalog, x.nominal);
    const script_catalog_type *right = script_catalog_type_by_id(catalog, y.nominal);
    if (!left || !right || left->field_count != right->field_count) {
        return false;
    }
    for (int i = 0; i < left->field_count; i++) {
        int index = script_catalog_find_field(right, left->fields[i].name);
        if (index < 0 || !compatible_aggregate(
                             catalog,
                             left->fields[i].type,
                             right->fields[index].type,
                             depth + 1
                         )) {
            return false;
        }
    }
    return true;
}
bool script_analyzer_resolve_operator(analyzer *a, TSNode node, TSNode operator_node, script_expression *expression)
{
    char *name = script_analyzer_text_copy(a, operator_node);
    size_t length = strlen(name);
    if (length > 1 && name[length - 1] == '=' && name[0] != '=' &&
        name[0] != '!' && name[0] != '<' && name[0] != '>') {
        name[length - 1] = 0;
    }
    const script_catalog_capability *best = NULL;
    int best_score = -1;
    bool ambiguous = false;
    int arity = expression->right >= 0 ? 2 : 1;
    for (int i = 0; i < a->catalog->capability_count; i++) {
        const script_catalog_capability *candidate = &a->catalog->capabilities[i];
        if (!script_analyzer_native_capability(a, candidate->id) ||
            !candidate->operator_name || strcmp(candidate->operator_name, name) ||
            candidate->parameter_count != arity || candidate->result_count != 1) {
            continue;
        }
        int operands[] = { expression->left, expression->right };
        int score = 0;
        bool matches = true;
        for (int j = 0; j < arity; j++) {
            script_type actual = script_analyzer_expr_type(a, operands[j]);
            matches &= candidate->numeric_aggregate
                         ? script_analyzer_numeric_aggregate(a->catalog, actual, 0)
                         : script_catalog_type_assignable(
                               a->catalog,
                               candidate->parameters[j].type,
                               actual
                           );
            score += script_type_equal(candidate->parameters[j].type, actual);
        }
        if (candidate->numeric_aggregate && arity == 2) {
            matches &= compatible_aggregate(
                a->catalog,
                script_analyzer_expr_type(a, operands[0]),
                script_analyzer_expr_type(a, operands[1]),
                0
            );
        }
        if (!matches || score < best_score) {
            continue;
        }
        if (score == best_score) {
            ambiguous = true;
            continue;
        }
        best = candidate;
        best_score = score;
        ambiguous = false;
    }
    free(name);
    if (!best) {
        return false;
    }
    if (ambiguous) {
        script_analyzer_diagnostic(a, node, "Ambiguous operator overload");
        expression->type = script_analyzer_bad_type();
        return true;
    }
    expression->kind = SCRIPT_EXPR_CALL;
    expression->capability = best->id;
    expression->type = best->results[0].type;
    if (best->numeric_aggregate) {
        script_type first = script_analyzer_expr_type(a, expression->left);
        script_type second = expression->right >= 0
                               ? script_analyzer_expr_type(a, expression->right)
                               : first;
        expression->type = first.storage == PIN_STRUCT ? first : second;
    }
    expression->effects |= best->effects;
    apush(
        expression->arguments,
        ((
            script_argument
        ){ expression->left, 0, 0, a->out->expressions[expression->left].range })
    );
    if (arity == 2) {
        apush(
            expression->arguments,
            ((
                script_argument
            ){ expression->right, 1, 0, a->out->expressions[expression->right].range })
        );
    }
    expression->left = -1;
    expression->right = -1;
    return true;
}

/* Without a type from context a literal is typed by its spelling, as in C#:
 * an integer is int, or the first of i64 and u64 that holds it, and a
 * decimal point or exponent makes a number. Hex digits include e. */
static script_type untyped_number(const char *text, long double value)
{
    script_type type = { .storage = PIN_F32, .flags = SCRIPT_TYPE_UNTYPED };
    if (!strpbrk(text, "xX") && strpbrk(text, ".eE")) {
        return type;
    }
    type.storage = value >= -0x1p31L && value < 0x1p31L ? PIN_I32
                 : value >= -0x1p63L && value < 0x1p63L ? PIN_I64
                                                        : PIN_U64;
    return type;
}
/* A decimal spelling such as 2.0 or -1e3 read exactly at an integer type,
 * digit by digit with no floating rounding: whole with its magnitude, past
 * 64 bits, or else with a fraction. */
typedef enum
{
    DECIMAL_WHOLE,
    DECIMAL_LARGE,
    DECIMAL_FRACTION
} decimal_integral;
static decimal_integral decimal_integer(const char *text, uint64_t *magnitude)
{
    static const long exponent_limit = 1000000;
    const char *at = text + (*text == '-' || *text == '+');
    char *digits = xmalloc(strlen(at) + 1);
    /* Significant digits before the decimal point; leading zeros after it
     * make this negative. */
    long point = 0;
    int count = 0;
    bool dot = false;
    for (; (*at >= '0' && *at <= '9') || *at == '.'; at++) {
        if (*at == '.') {
            dot = true;
        } else if (*at == '0' && count == 0) {
            point -= dot;
        } else {
            digits[count++] = *at;
            point += !dot;
        }
    }
    if (*at == 'e' || *at == 'E') {
        long exponent = strtol(at + 1, NULL, 10);
        exponent = exponent > exponent_limit ? exponent_limit : exponent;
        point += exponent < -exponent_limit ? -exponent_limit : exponent;
    }
    while (count > 0 && digits[count - 1] == '0') {
        count--;
    }
    *magnitude = 0;
    if (count == 0) {
        free(digits);
        return DECIMAL_WHOLE;
    }
    decimal_integral read = point > 20 ? DECIMAL_LARGE : DECIMAL_WHOLE;
    for (long i = 0; read == DECIMAL_WHOLE && i < point; i++) {
        uint64_t digit = i < count ? (uint64_t)(digits[i] - '0') : 0;
        if (*magnitude > (UINT64_MAX - digit) / 10) {
            read = DECIMAL_LARGE;
            break;
        }
        *magnitude = *magnitude * 10 + digit;
    }
    free(digits);
    if (read == DECIMAL_WHOLE && count > point) {
        return DECIMAL_FRACTION;
    }
    return read;
}
/* The largest value of an integer type. */
static uint64_t integer_maximum(uint8_t storage)
{
    switch (storage) {
        case PIN_I8: return INT8_MAX;
        case PIN_U8: return UINT8_MAX;
        case PIN_I16: return INT16_MAX;
        case PIN_U16: return UINT16_MAX;
        case PIN_I32:
        case PIN_ENUM: return INT32_MAX;
        case PIN_U32: return UINT32_MAX;
        case PIN_I64: return INT64_MAX;
        default: return UINT64_MAX;
    }
}
/* A union such as `number | Cancelled` gives a literal its one numeric
 * alternative's type. */
static script_type numeric_alternative(const analyzer *a, script_type expected)
{
    const script_catalog_type
        *type = (expected.flags & SCRIPT_TYPE_UNION) && expected.container == PIN_CONTAINER_SINGLE
                  ? script_catalog_type_by_id(a->catalog, expected.nominal)
                  : NULL;
    script_type found = expected;
    int count = 0;
    for (int i = 0; type && i < type->variant_count; i++) {
        if (!type->variants[i].tagged && script_type_numeric(type->variants[i].type)) {
            found = type->variants[i].type;
            count++;
        }
    }
    return count == 1 ? found : expected;
}
script_expression script_analyzer_number_literal(analyzer *a, TSNode node, script_type expected, bool negate)
{
    expected = numeric_alternative(a, expected);
    expected.flags &= ~(SCRIPT_TYPE_OPTIONAL | SCRIPT_TYPE_BOXED_OPTIONAL);
    script_expression result = { .kind = SCRIPT_EXPR_CONSTANT,
                                 .symbol = -1,
                                 .left = -1,
                                 .right = -1,
                                 .range = script_analyzer_range(a, node) };
    /* A negated literal's range starts at its minus sign. */
    TSNode literal = negate ? ts_node_parent(node) : node;
    char *text = script_analyzer_text_copy(a, node);
    if (negate) {
        size_t size = strlen(text);
        text = xrealloc(text, size + 2);
        memmove(text + 1, text, size + 1);
        text[0] = '-';
    }
    char *end;
    errno = 0;
    long double value = strtold(text, &end);
    if (*end || !isfinite(value) || errno == ERANGE) {
        script_analyzer_diagnostic(a, literal, "Numeric literal must be finite");
    }
    result.kind = SCRIPT_EXPR_CONSTANT;
    bool enumeration = expected.storage == PIN_ENUM &&
                       expected.container == PIN_CONTAINER_SINGLE;
    result.type = (script_type_numeric(expected) || enumeration) &&
                          !(expected.flags & SCRIPT_TYPE_UNTYPED)
                    ? expected
                    : untyped_number(text, value);
    if (enumeration && expected.nominal) {
        const script_catalog_type
            *type = script_catalog_type_by_id(a->catalog, expected.nominal);
        if (!type || type->type.storage != PIN_ENUM) {
            script_analyzer_diagnostic(a, node, "Enum literal needs a declared enum type");
        } else {
            size_t size = strlen(type->name) + 1;
            char *name = arena_alloc(&a->out->constants, size);
            memcpy(name, type->name, size);
            result.constant.type_name = name;
        }
    }
    if (expected.storage == PIN_HANDLE &&
        !(expected.flags & SCRIPT_TYPE_UNTYPED) && value == 0) {
        result.type = expected;
        result.constant.type = PIN_HANDLE;
        const script_catalog_type
            *type = script_catalog_type_by_id(a->catalog, expected.nominal);
        if (type) {
            size_t size = strlen(type->name) + 1;
            char *name = arena_alloc(&a->out->constants, size);
            memcpy(name, type->name, size);
            result.constant.type_name = name;
        }
        free(text);
        return result;
    }
    result.constant.type = result.type.storage;
    long double limit = 0;
    bool integer = true, unsigned_integer = false;
    switch (result.type.storage) {
        case PIN_I8: limit = 0x1p7L; break;
        case PIN_U8:
            limit = 0x1p8L;
            unsigned_integer = true;
            break;
        case PIN_I16: limit = 0x1p15L; break;
        case PIN_U16:
            limit = 0x1p16L;
            unsigned_integer = true;
            break;
        case PIN_I32:
        case PIN_ENUM: limit = 0x1p31L; break;
        case PIN_U32:
            limit = 0x1p32L;
            unsigned_integer = true;
            break;
        case PIN_I64: limit = 0x1p63L; break;
        case PIN_U64:
            limit = 0x1p64L;
            unsigned_integer = true;
            break;
        default: integer = false; break;
    }
    bool fits = isfinite(value) &&
                fabsl(value) <= (result.type.storage == PIN_F32 ? FLT_MAX : DBL_MAX);
    /* Hex digits include e, so only decimal literals have an exponent. */
    int base = strpbrk(text, "xX") ? 16 : 10;
    bool exact_integer = integer && (base == 16 || !strpbrk(text, ".eE"));
    bool fraction = false;
    uint64_t exact = 0;
    int64_t exact_signed = 0;
    if (integer && !exact_integer) {
        /* A decimal such as 2.0 is the integer it spells, exactly. */
        decimal_integral read = decimal_integer(text, &exact);
        bool negative = text[0] == '-' && exact;
        uint64_t maximum = integer_maximum(result.type.storage);
        fraction = read == DECIMAL_FRACTION;
        fits = read == DECIMAL_WHOLE &&
               (negative ? !unsigned_integer && exact - 1 <= maximum : exact <= maximum);
        /* Only a signed value in range is built as one. */
        if (fits && !unsigned_integer) {
            exact_signed = negative ? -(int64_t)(exact - 1) - 1 : (int64_t)exact;
        }
        value = negative ? -(long double)exact : (long double)exact;
    } else if (exact_integer && !unsigned_integer) {
        errno = 0;
        intmax_t parsed = strtoimax(text, &end, base);
        fits = parsed >= INT64_MIN && parsed <= INT64_MAX;
        exact_signed = fits ? (int64_t)parsed : 0;
        fits = fits && !*end && errno != ERANGE &&
               (long double)exact_signed >= -limit &&
               (result.type.storage == PIN_I64 || (long double)exact_signed < limit);
    } else if (exact_integer) {
        errno = 0;
        uintmax_t parsed = strtoumax(text, &end, base);
        exact = parsed <= UINT64_MAX ? (uint64_t)parsed : 0;
        fits = parsed <= UINT64_MAX && text[0] != '-' && !*end &&
               errno != ERANGE && exact <= integer_maximum(result.type.storage);
    }
    if (script_type_char(result.type) && fits) {
        fits = (value >= 0 && value < 0xD800) || (value > 0xDFFF && value <= 0x10FFFF);
    }
    if (!fits) {
        exact = 0;
        exact_signed = 0;
        script_analyzer_diagnostic(
            a,
            literal,
            fraction ? "Numeric literal has a fraction, which %s cannot hold"
                     : "Numeric literal is outside the range of %s",
            script_numeric_name(result.type)
        );
        value = 0;
    }
    switch (result.type.storage) {
        case PIN_I8: result.constant.i8 = (int8_t)value; break;
        case PIN_U8: result.constant.u8 = (uint8_t)value; break;
        case PIN_I16: result.constant.i16 = (int16_t)value; break;
        case PIN_U16: result.constant.u16 = (uint16_t)value; break;
        case PIN_I32: result.constant.i = (int32_t)value; break;
        case PIN_ENUM: result.constant.e = (int32_t)exact_signed; break;
        case PIN_U32: result.constant.u = (uint32_t)value; break;
        case PIN_I64: result.constant.i64 = exact_signed; break;
        case PIN_U64: result.constant.u64 = exact; break;
        case PIN_F32: result.constant.f = (float)value; break;
        default: result.constant.d = value; break;
    }
    free(text);
    return result;
}
static int number_constant(analyzer *a, TSNode node, script_type expected, bool negate)
{
    return script_analyzer_expr_add(a, script_analyzer_number_literal(a, node, expected, negate));
}
int script_analyzer_dereference(analyzer *a, TSNode node, int value)
{
    script_expression source = a->out->expressions[value];
    script_type pointee = script_catalog_pointee_type(a->catalog, source.type);
    if (pointee.flags & SCRIPT_TYPE_ERROR) {
        script_analyzer_diagnostic(a, node, "Dereference requires a managed pointer");
    } else if (
        (source.type.flags & SCRIPT_TYPE_OPTIONAL) &&
        !script_analyzer_present(a, source.symbol)
    ) {
        script_analyzer_diagnostic(a, node, "Nullable pointer requires a presence guard before dereference");
    }
    const script_catalog_type
        *definition = script_catalog_type_by_id(a->catalog, pointee.nominal);
    bool writable = !definition || !definition->component || !definition->derived;
    return script_analyzer_expr_add(
        a,
        (script_expression){ .kind = SCRIPT_EXPR_DEREFERENCE,
                             .type = pointee,
                             .range = script_analyzer_range(a, node),
                             .symbol = -1,
                             .left = value,
                             .right = -1,
                             .writable = writable,
                             .effects = source.effects | SCRIPT_EFFECT_READ }
    );
}
static const char *managed_address_layout(analyzer *a, int expression)
{
    const script_expression *value = &a->out->expressions[expression];
    if (value->kind == SCRIPT_EXPR_DEREFERENCE) {
        return NULL; /* The pointer already owns and validates its native layout. */
    }
    if (value->kind == SCRIPT_EXPR_MEMBER) {
        return managed_address_layout(a, value->left);
    }
    script_type type = value->kind == SCRIPT_EXPR_SYMBOL && value->symbol >= 0
                         ? a->out->symbols[value->symbol].type
                         : value->type;
    if (type.storage != PIN_STRUCT || type.container != PIN_CONTAINER_SINGLE) {
        return NULL;
    }
    const script_catalog_type *record = script_catalog_type_by_id(a->catalog, type.nominal);
    if (!record || !record->authored_record) {
        return "Managed values require authored record layouts";
    }
    return NULL;
}
static bool addressable_local(analyzer *a, int expression)
{
    const script_expression *value = &a->out->expressions[expression];
    if (!value->writable) {
        return false;
    }
    if (value->kind == SCRIPT_EXPR_DEREFERENCE) {
        return !(value->type.flags & SCRIPT_TYPE_ERROR);
    }
    if (value->kind == SCRIPT_EXPR_MEMBER && !value->read && !value->write) {
        if (!addressable_local(a, value->left)) {
            return false;
        }
        (void)script_analyzer_pointer_type(a, value->type);
        return true;
    }
    if (value->kind != SCRIPT_EXPR_SYMBOL || value->symbol < 0) {
        return false;
    }
    script_symbol *symbol = &a->out->symbols[value->symbol];
    int owner = symbol->scope >= 0 ? a->out->scopes[symbol->scope].function : -1;
    int current = a->scope >= 0 ? a->out->scopes[a->scope].function : -1;
    if ((symbol->kind != SCRIPT_LOCAL && symbol->kind != SCRIPT_PARAMETER) ||
        owner != current || (symbol->type.flags & SCRIPT_TYPE_REFERENCE)) {
        return false;
    }
    symbol->addressed = true;
    script_analyzer_invalidate_pointers(a);
    (void)script_analyzer_pointer_type(a, symbol->type);
    return true;
}
static int character_constant(analyzer *a, TSNode node, script_type expected)
{
    expected.flags &= ~(SCRIPT_TYPE_OPTIONAL | SCRIPT_TYPE_BOXED_OPTIONAL);
    int value = 0;
    script_analyzer_character_value(a, node, &value);
    script_expression result = { .kind = SCRIPT_EXPR_CONSTANT,
                                 .type = script_char_type(),
                                 .constant = { .type = PIN_I32, .i = value },
                                 .range = script_analyzer_range(a, node),
                                 .symbol = -1,
                                 .left = -1,
                                 .right = -1 };
    bool typed = script_type_numeric(expected) &&
                 !(expected.flags & SCRIPT_TYPE_UNTYPED);
    if (!typed) {
        result.type.flags |= expected.flags & SCRIPT_TYPE_UNTYPED;
        return script_analyzer_expr_add(a, result);
    }
    if (expected.storage > PIN_U64) {
        script_analyzer_diagnostic(a, node, "A character literal needs an integer or char type");
        result.type = script_analyzer_bad_type();
        return script_analyzer_expr_add(a, result);
    }
    if (value < behavior_scalar_integer_low(expected.storage) ||
        (uint64_t)value > behavior_scalar_integer_high(expected.storage)) {
        script_analyzer_diagnostic(
            a,
            node,
            "Character literal is outside the range of %s",
            script_numeric_name(expected)
        );
    }
    result.type = expected;
    result.constant = behavior_scalar_from_bits(expected.storage, (uint64_t)value);
    return script_analyzer_expr_add(a, result);
}

/* Drops the range error a literal, with its minus sign, got when it was read
 * without a type, so the type its context gives decides alone. */
static void retract_range_error(analyzer *a, TSNode literal)
{
    script_range range = script_analyzer_range(a, literal);
    static const char prefix[] = "Numeric literal is outside the range of ";
    for (int i = acount(a->out->diagnostics) - 1; i >= 0; i--) {
        const script_diagnostic *d = &a->out->diagnostics[i];
        if (d->range.start == range.start && d->range.end == range.end &&
            d->range.document == range.document &&
            !strncmp(d->message, prefix, sizeof prefix - 1)) {
            script_diagnostic *all = a->out->diagnostics;
            memmove(all + i, all + i + 1, (size_t)(acount(all) - i - 1) * sizeof *all);
            asetlen(a->out->diagnostics, acount(all) - 1);
            return;
        }
    }
}
void script_analyzer_contextual_numeric(analyzer *a, int index, script_type type)
{
    script_expression *expression = &a->out->expressions[index];
    if (!(expression->type.flags & SCRIPT_TYPE_UNTYPED) || !script_type_numeric(type)) {
        return;
    }
    if (expression->kind == SCRIPT_EXPR_CONSTANT) {
        script_range range = expression->range;
        TSNode node = ts_node_descendant_for_byte_range(
            ts_tree_root_node(script_analyzer_document_syntax(a, range.document)->tree),
            (uint32_t)range.start,
            (uint32_t)range.end
        );
        /* The sign comes from the source: a value the untyped reading
         * rejected holds zero. Only a minus folds into a literal. */
        bool negative = script_analyzer_is(node, "unary_expression");
        TSNode literal = node;
        if (negative) {
            node = script_analyzer_field(node, "operand");
        }
        bool character = script_analyzer_is(node, "character");
        if (!character) {
            retract_range_error(a, literal);
        }
        int converted = character ? character_constant(a, node, type)
                                  : number_constant(a, node, type, negative);
        a->out->expressions[index] = a->out->expressions[converted];
        a->out->expressions[index].range = range;
        return;
    }
    if (expression->kind == SCRIPT_EXPR_UNARY || expression->kind == SCRIPT_EXPR_BINARY) {
        int left = expression->left, right = expression->right;
        if (left >= 0) {
            script_analyzer_contextual_numeric(a, left, type);
        }
        if (right >= 0) {
            script_analyzer_contextual_numeric(a, right, type);
        }
        a->out->expressions[index].type = type;
    }
}

static bool typed_integer(script_type type)
{
    return script_type_numeric(type) && !(type.flags & SCRIPT_TYPE_UNTYPED) &&
           type.storage >= PIN_I8 && type.storage <= PIN_U64;
}
/* Integer operands of a binary operation share one type; the author converts
 * one of them, as in Odin. A char holds every Unicode scalar in 32 bits, so it
 * takes part beside any 32- or 64-bit integer as that integer. */
bool script_analyzer_integers_match(script_type x, script_type y)
{
    if (script_type_char(x) != script_type_char(y)) {
        return behavior_scalar_size(script_type_char(x) ? y.storage : x.storage) >= 4;
    }
    return x.storage == y.storage;
}
void script_analyzer_mismatched_integers(analyzer *a, TSNode node, script_type x, script_type y)
{
    script_analyzer_diagnostic(
        a,
        node,
        "Mismatched integer types %s and %s; convert one operand",
        script_numeric_name(x),
        script_numeric_name(y)
    );
}
/* A shift has its left operand's type; the count keeps its own unsigned
 * type, and lowering converts it, so a count past the width stays past it.
 * An untyped left operand takes the expected type. An untyped integer beside
 * a typed count stays untyped, so the other operand of a comparison or an
 * arithmetic operation types it as it would type the literal alone; any other
 * untyped left operand is an int. */
static int shift_expression(analyzer *a, TSNode node, script_expression result, script_type expected)
{
    TSNode count = script_analyzer_field(node, "right");
    script_type x = script_analyzer_expr_type(a, result.left),
                y = script_analyzer_expr_type(a, result.right);
    const script_expression *right = &a->out->expressions[result.right];
    bool negative = right->kind == SCRIPT_EXPR_CONSTANT &&
                    behavior_scalar_integer(right->constant.type) &&
                    behavior_scalar_signed(right->constant.type) &&
                    (int64_t)behavior_scalar_bits(right->constant) < 0;
    /* A negative constant count is reported with the other constants. */
    if (!negative && typed_integer(y) && !script_type_char(y) &&
        behavior_scalar_signed(y.storage)) {
        script_analyzer_diagnostic(a, count, "Shift count must be unsigned; convert it with u32(value)");
    } else if (!negative && !(y.flags & SCRIPT_TYPE_UNTYPED) && (!typed_integer(y) || script_type_char(y))) {
        script_analyzer_diagnostic(a, count, "Shift count must be an unsigned integer");
    }
    script_type value = x;
    if (x.flags & SCRIPT_TYPE_UNTYPED) {
        bool both = y.flags & SCRIPT_TYPE_UNTYPED,
             typed = script_type_numeric(expected) &&
                     !(expected.flags & SCRIPT_TYPE_UNTYPED),
             open = !typed && !both && x.storage >= PIN_I8 && x.storage <= PIN_U64;
        value = typed        ? expected
              : both || open ? x
                             : (script_type){ .storage = PIN_I32 };
        if (!open && (!both || script_type_numeric(expected))) {
            value.flags &= ~SCRIPT_TYPE_UNTYPED;
        }
        script_analyzer_contextual_numeric(a, result.left, value);
        value = script_analyzer_expr_type(a, result.left);
    }
    if (!typed_integer(value) && !(value.flags & SCRIPT_TYPE_UNTYPED)) {
        script_analyzer_diagnostic(a, node, "Shift requires an integer value");
    }
    if ((y.flags & SCRIPT_TYPE_UNTYPED) && !negative) {
        /* Counts keep their exact expression and an independent type until
         * constant folding. Lowering converts runtime counts to the value's
         * type only after the constant checks have finished. */
        script_analyzer_contextual_numeric(a, result.right, script_analyzer_conversion_operand(a, count));
    }
    result.effects = script_analyzer_expr_effects(a, result.left) |
                     script_analyzer_expr_effects(a, result.right);
    result.type = value;
    return script_analyzer_expr_add(a, result);
}
/* Awaiting a task handle waits for that task and takes the results of the
 * function that started it, when the handle's type names one. */
static int join_task(analyzer *a, TSNode node, int value)
{
    script_type task = script_analyzer_expr_type(a, value);
    script_expression result = { .kind = SCRIPT_EXPR_JOIN,
                                 .type = { .storage = PIN_FLOW },
                                 .range = script_analyzer_range(a, node),
                                 .symbol = -1,
                                 .left = value,
                                 .right = -1,
                                 .entry = -1,
                                 .awaited = true,
                                 .effects = script_analyzer_expr_effects(a, value) |
                                            SCRIPT_EFFECT_SUSPEND };
    const script_catalog_capability
        *library = script_catalog_capability_by_id(a->catalog, task.nominal);
    if (library && library->result_count) {
        for (int i = 0; i < library->result_count; i++) {
            apush(result.results, library->results[i].type);
        }
        result.result_count = library->result_count;
        result.type = library->result_count == 1
                        ? library->results[0].type
                        : (script_type){ .storage = PIN_STRUCT,
                                         .nominal = library->id,
                                         .flags = SCRIPT_TYPE_RESULTS };
    }
    for (int i = 0; task.nominal && !library && i < acount(a->out->symbols); i++) {
        const script_symbol *symbol = &a->out->symbols[i];
        if (symbol->id != task.nominal || symbol->kind != SCRIPT_FUNCTION ||
            symbol->function < 0) {
            continue;
        }
        const script_function *function = &a->out->functions[symbol->function];
        int count = acount(function->results);
        for (int r = 0; r < count; r++) {
            apush(result.results, a->out->symbols[function->results[r]].type);
        }
        result.result_count = count;
        result.right = symbol->function;
        if (count == 1) {
            result.type = result.results[0];
        } else if (count > 1) {
            result.type = (script_type){ .storage = PIN_STRUCT,
                                         .nominal = symbol->id,
                                         .flags = SCRIPT_TYPE_RESULTS };
        }
        break;
    }
    if (task.parameter) {
        const script_catalog_type *success = script_catalog_type_by_id(a->catalog, task.parameter);
        if (success) {
            result.type = success->type;
            if (!result.result_count && result.type.storage != PIN_FLOW) {
                apush(result.results, result.type);
                result.result_count = 1;
            }
        }
    }
    result.tested = result.type;
    result.tag = (result.type.flags & SCRIPT_TYPE_UNION) &&
                         result.type.container == PIN_CONTAINER_SINGLE &&
                         !(result.type.flags & SCRIPT_TYPE_BOXED_OPTIONAL)
                   ? 0
                   : script_type_tag(result.type);
    result.type = script_analyzer_await_type(a, node, result.tested);
    asetlen(a->narrowed, 0);
    return script_analyzer_expr_add(a, result);
}
/* Add immutable defaults after explicit expressions, preserving their authored
 * evaluation order. Required members can never be supplied by zero storage. */
static void record_defaults(analyzer *a, TSNode node, script_expression *result)
{
    const script_catalog_type
        *type = script_catalog_type_by_id(a->catalog, result->type.nominal);
    if (!type || !type->authored_record ||
        result->type.container != PIN_CONTAINER_SINGLE) {
        return;
    }
    for (int i = 0; i < type->field_count; i++) {
        bool provided = false;
        for (int j = 0; j < acount(result->arguments); j++) {
            provided |= result->arguments[j].parameter == i;
        }
        if (provided) {
            continue;
        }
        const script_catalog_field *field = &type->fields[i];
        if (!field->has_default) {
            script_analyzer_diagnostic(
                a,
                node,
                "Missing required record field '%s'",
                field->name
            );
            continue;
        }
        pin_value owned = { 0 };
        if (pin_value_capture_to_arena(&a->out->constants, &owned, &field->default_value)) {
            script_analyzer_diagnostic(
                a,
                node,
                "Cannot retain record field default '%s'",
                field->name
            );
            continue;
        }
        int value = script_analyzer_expr_add(
            a,
            (script_expression){ .kind = SCRIPT_EXPR_CONSTANT,
                                 .type = field->type,
                                 .constant = owned,
                                 .constant_snapshot = true,
                                 .range = result->range,
                                 .symbol = -1,
                                 .left = -1,
                                 .right = -1,
                                 .entry = -1 }
        );
        apush(result->arguments, ((script_argument){ value, i, field->id, result->range }));
    }
}
/* Whether symbol is a payload name a handler without a parameter list binds,
 * spelled like a parameter of the function the handler runs in. A bare read
 * would take the payload, though the function's parameter is in sight. An
 * `after entry` payload is the macro's own parameters, so it is exempt. */
static bool hides_parameter(const analyzer *a, const script_symbol *symbol)
{
    if (symbol->kind != SCRIPT_PARAMETER || symbol->declaration.start >= 0 ||
        symbol->function < 0) {
        return false;
    }
    const script_function *handler = &a->out->functions[symbol->function];
    if ((handler->kind != SCRIPT_FUNCTION_CONTINUATION &&
         handler->kind != SCRIPT_FUNCTION_HANDLER) ||
        handler->parent < 0) {
        return false;
    }
    const script_function *owner = &a->out->functions[handler->parent];
    for (int i = 0; i < acount(owner->parameters); i++) {
        if (!strcmp(a->out->symbols[owner->parameters[i]].name, symbol->name)) {
            return true;
        }
    }
    return false;
}
/* The value `null` makes of a nonoptional type: an empty container, a record
 * of its field defaults, or zero. */
static int zero_value(analyzer *a, TSNode node, script_type type)
{
    if (type.container == PIN_CONTAINER_VIEW && !(type.flags & SCRIPT_TYPE_OPTIONAL)) {
        script_analyzer_diagnostic(a, node, "A nonoptional view requires an initializer");
    }
    if (type.storage == PIN_POINTER && type.container == PIN_CONTAINER_SINGLE &&
        !(type.flags & SCRIPT_TYPE_OPTIONAL)) {
        script_analyzer_diagnostic(a, node, "A non-null pointer requires an initializer");
    }
    script_expression result = {
        .kind = type.container == PIN_CONTAINER_MAP    ? SCRIPT_EXPR_MAP
              : type.container != PIN_CONTAINER_SINGLE ? SCRIPT_EXPR_ARRAY
              : type.storage == PIN_STRUCT             ? SCRIPT_EXPR_STRUCT
                                                       : SCRIPT_EXPR_CONSTANT,
        .type = type,
        .range = script_analyzer_range(a, node),
        .constant = { .type = type.storage, .container = type.container },
        .symbol = -1,
        .left = -1,
        .right = -1
    };
    if (result.kind == SCRIPT_EXPR_STRUCT) {
        record_defaults(a, node, &result);
    }
    return script_analyzer_expr_add(a, result);
}
/* What reading an absent key yields, as Odin does: the value type's zero, or
 * nil for a boxed optional. A union, task or reference value has no zero, nor
 * does a record with a required field; reading those keys stays checked. */
static int absent_value(analyzer *a, TSNode node, script_type type)
{
    if ((type.flags & SCRIPT_TYPE_BOXED_OPTIONAL) && !(type.flags & SCRIPT_TYPE_TASK)) {
        return script_analyzer_expr_add(
            a,
            (script_expression){ .kind = SCRIPT_EXPR_NIL,
                                 .type = type,
                                 .range = script_analyzer_range(a, node),
                                 .symbol = -1,
                                 .left = -1,
                                 .right = -1 }
        );
    }
    if (type.container == PIN_CONTAINER_VIEW ||
        (type.storage == PIN_POINTER && type.container == PIN_CONTAINER_SINGLE) ||
        type.flags & (SCRIPT_TYPE_OPTIONAL | SCRIPT_TYPE_UNION | SCRIPT_TYPE_TASK |
                      SCRIPT_TYPE_REFERENCE | SCRIPT_TYPE_ERROR | SCRIPT_TYPE_UNTYPED) ||
        (type.storage == PIN_UNION && type.container == PIN_CONTAINER_SINGLE)) {
        return -1;
    }
    const script_catalog_type *record = script_catalog_type_by_id(a->catalog, type.nominal);
    if (type.storage == PIN_STRUCT && type.container == PIN_CONTAINER_SINGLE) {
        if (!record || !record->authored_record) {
            return -1;
        }
        for (int i = 0; i < record->field_count; i++) {
            if (!record->fields[i].has_default) {
                return -1;
            }
        }
    }
    return zero_value(a, node, type);
}
static bool map_key_supported(script_type key)
{
    return key.container == PIN_CONTAINER_SINGLE &&
           !(key.flags & (SCRIPT_TYPE_OPTIONAL | SCRIPT_TYPE_TASK | SCRIPT_TYPE_UNION)) &&
           (key.storage == PIN_BOOL ||
            (key.storage >= PIN_I8 && key.storage <= PIN_F64) ||
            key.storage == PIN_STR || key.storage == PIN_NAME ||
            key.storage == PIN_ENUM || key.storage == PIN_HANDLE);
}
/* `["Ada": 3]` takes its key and value types from context, or else from its
 * first entry; `[:]` needs context. */
static int map_literal(analyzer *a, TSNode node, script_type expected)
{
    script_expression result = { .kind = SCRIPT_EXPR_MAP,
                                 .range = script_analyzer_range(a, node),
                                 .symbol = -1,
                                 .left = -1,
                                 .right = -1 };
    bool inferred = expected.container != PIN_CONTAINER_MAP || !expected.collection;
    if (!inferred && (expected.flags & SCRIPT_TYPE_OPTIONAL)) {
        expected.flags &= ~(SCRIPT_TYPE_OPTIONAL | SCRIPT_TYPE_BOXED_OPTIONAL);
    }
    script_type untyped = { .flags = SCRIPT_TYPE_UNTYPED };
    script_type key = inferred ? untyped
                               : script_catalog_key_type(a->catalog, expected);
    script_type value_type = inferred
                               ? untyped
                               : script_catalog_element_type(a->catalog, expected);
    for (uint32_t i = 0; i < script_analyzer_named_count(node); i++) {
        TSNode entry = ts_node_named_child(node, i);
        if (ts_node_is_extra(entry)) {
            continue;
        }
        int key_value = script_analyzer_expression(a, script_analyzer_field(entry, "key"), key);
        int value = script_analyzer_expression(a, script_analyzer_field(entry, "value"), value_type);
        if (inferred && !acount(result.arguments)) {
            key = script_analyzer_expr_type(a, key_value);
            value_type = script_analyzer_expr_type(a, value);
            key.flags &= ~(SCRIPT_TYPE_UNTYPED | SCRIPT_TYPE_LITERAL);
            value_type.flags &= ~(SCRIPT_TYPE_UNTYPED | SCRIPT_TYPE_LITERAL);
        }
        script_analyzer_require(a, entry, value_type, script_analyzer_expr_type(a, value));
        script_type actual_key = script_analyzer_expr_type(a, key_value);
        actual_key.flags &= ~(SCRIPT_TYPE_UNTYPED | SCRIPT_TYPE_LITERAL);
        if (!script_type_equal(key, actual_key)) {
            script_analyzer_diagnostic(a, entry, "Map key type does not match");
        }
        script_range range = script_analyzer_range(a, entry);
        apush(result.arguments, ((script_argument){ key_value, -1, 0, range }));
        apush(result.arguments, ((script_argument){ value, -1, 0, range }));
        result.effects |= script_analyzer_expr_effects(a, key_value) |
                          script_analyzer_expr_effects(a, value);
    }
    if (!acount(result.arguments) && inferred) {
        script_analyzer_diagnostic(a, node, "An empty map needs its key and value types from context");
        result.type = script_analyzer_bad_type();
        return script_analyzer_expr_add(a, result);
    }
    if (!map_key_supported(key)) {
        script_analyzer_diagnostic(a, node, "Unsupported map key type");
    }
    result.type = script_analyzer_collection_type(a, PIN_CONTAINER_MAP, key, value_type);
    return script_analyzer_expr_add(a, result);
}
/* `m.has(k)`, `k in m` and removal look key up in map, an analyzed value. */
int script_analyzer_map_operation(analyzer *a, TSNode node, script_expression_kind kind, int map, TSNode key_node)
{
    script_type type = script_analyzer_expr_type(a, map);
    script_type key = script_catalog_key_type(a->catalog, type);
    script_expression result = { .kind = kind,
                                 .type = kind == SCRIPT_EXPR_MAP_HAS
                                           ? script_analyzer_boolean_type()
                                           : type,
                                 .range = script_analyzer_range(a, node),
                                 .symbol = -1,
                                 .left = map,
                                 .right = -1 };
    if ((type.container != PIN_CONTAINER_MAP || !type.collection) &&
        !(type.flags & SCRIPT_TYPE_ERROR)) {
        script_analyzer_diagnostic(a, node, "Map operation requires a map");
    }
    result.right = script_analyzer_expression(a, key_node, key);
    if (!script_type_equal(key, script_analyzer_expr_type(a, result.right))) {
        script_analyzer_diagnostic(a, key_node, "Map key type does not match");
    }
    result.effects = script_analyzer_expr_effects(a, map) |
                     script_analyzer_expr_effects(a, result.right);
    return script_analyzer_expr_add(a, result);
}
/* Whether `function` calls method `name` on a value, such as `m.has`, rather
 * than a package or type member. */
bool script_analyzer_map_method(analyzer *a, TSNode function, const char *name)
{
    if (!script_analyzer_is(function, "member_expression") ||
        !script_analyzer_text_is(a, script_analyzer_field(function, "member"), name)) {
        return false;
    }
    TSNode root = script_analyzer_field(function, "object");
    while (script_analyzer_is(root, "member_expression") ||
           script_analyzer_is(root, "index_expression")) {
        root = script_analyzer_field(root, "object");
    }
    return !script_analyzer_is(root, "identifier") ||
           script_analyzer_lookup(a, root) >= 0;
}
/* `m.has(k)`; `m.delete(k)` is a statement. */
static int map_method(analyzer *a, TSNode node)
{
    TSNode function = script_analyzer_field(node, "function"),
           arguments = script_analyzer_field(node, "arguments");
    int map = script_analyzer_expression(
        a,
        script_analyzer_field(function, "object"),
        (script_type){ .flags = SCRIPT_TYPE_UNTYPED }
    );
    if (!script_analyzer_map_method(a, function, "has")) {
        script_analyzer_diagnostic(a, node, "delete changes the map; write it as a statement");
    }
    if (script_analyzer_named_count(arguments) != 1) {
        script_analyzer_diagnostic(a, node, "Map method requires one key");
    }
    return script_analyzer_map_operation(a, node, SCRIPT_EXPR_MAP_HAS, map, ts_node_named_child(arguments, 0));
}
/* `a ?? b` reads a's value, or b when a is nil, as Swift's `??` and Odin's
 * `or_else` do. b runs only then; an optional b keeps the result optional. */
static int coalesce(analyzer *a, TSNode node, script_expression result)
{
    TSNode left = script_analyzer_field(node, "left"),
           right = script_analyzer_field(node, "right");
    script_type optional = script_analyzer_expr_type(a, result.left), value = optional;
    value.flags &= ~(SCRIPT_TYPE_OPTIONAL | SCRIPT_TYPE_BOXED_OPTIONAL);
    if ((!(optional.flags & SCRIPT_TYPE_BOXED_OPTIONAL) ||
         (optional.flags & SCRIPT_TYPE_TASK)) &&
        !(optional.flags & SCRIPT_TYPE_ERROR)) {
        script_analyzer_diagnostic(a, left, "?? requires an optional value on its left");
        value = script_analyzer_bad_type();
    }
    result.right = script_analyzer_expression(a, right, value);
    script_type actual = script_analyzer_expr_type(a, result.right);
    result.type = (actual.flags & SCRIPT_TYPE_OPTIONAL) &&
                          !(value.flags & SCRIPT_TYPE_ERROR)
                    ? optional
                    : value;
    script_analyzer_require(a, right, result.type, actual);
    result.effects = script_analyzer_expr_effects(a, result.left) |
                     script_analyzer_expr_effects(a, result.right);
    return script_analyzer_expr_add(a, result);
}
int script_analyzer_expression_inner(analyzer *a, TSNode node, script_type expected)
{
    script_expression result = { .kind = SCRIPT_EXPR_ERROR,
                                 .type = script_analyzer_bad_type(),
                                 .range = script_analyzer_range(a, node),
                                 .symbol = -1,
                                 .left = -1,
                                 .right = -1 };
    if (ts_node_is_null(node)) {
        script_analyzer_diagnostic(
            a,
            node,
            expected.storage == PIN_POINTER && expected.container == PIN_CONTAINER_SINGLE &&
                    !(expected.flags & SCRIPT_TYPE_OPTIONAL)
                ? "A non-null pointer requires an initializer"
                : "Expected a value expression"
        );
        return script_analyzer_expr_add(a, result);
    }
    if (script_analyzer_is(node, "address_expression")) {
        result.kind = SCRIPT_EXPR_ADDRESS;
        result.left = script_analyzer_expression(
            a,
            script_analyzer_field(node, "operand"),
            (script_type){ .flags = SCRIPT_TYPE_UNTYPED }
        );
        const char *layout_error = managed_address_layout(a, result.left);
        if (layout_error) {
            script_analyzer_diagnostic(a, node, layout_error);
        } else if (!addressable_local(a, result.left)) {
            script_analyzer_diagnostic(a, node, "Managed pointer requires a writable local or record field");
        }
        script_expression addressed = a->out->expressions[result.left];
        script_type target = addressed.kind == SCRIPT_EXPR_SYMBOL &&
                                     addressed.symbol >= 0
                               ? a->out->symbols[addressed.symbol].type
                               : addressed.type;
        target.flags &= ~(SCRIPT_TYPE_REFERENCE | SCRIPT_TYPE_UNTYPED);
        result.type = script_analyzer_pointer_type(a, target);
        result.effects = script_analyzer_expr_effects(a, result.left);
        return script_analyzer_expr_add(a, result);
    }
    if (script_analyzer_is(node, "dereference_expression")) {
        TSNode operand = script_analyzer_field(node, "operand");
        int value = script_analyzer_expression(a, operand, (script_type){ .flags = SCRIPT_TYPE_UNTYPED });
        const script_expression *source = &a->out->expressions[value];
        if (script_analyzer_is(operand, "identifier") &&
            source->kind == SCRIPT_EXPR_SYMBOL && source->symbol >= 0 &&
            (a->out->symbols[source->symbol].type.flags & SCRIPT_TYPE_REFERENCE)) {
            return value; /* A scoped pointer parameter names its target. */
        }
        return script_analyzer_dereference(a, node, value);
    }
    if (script_analyzer_is(node, "parenthesized_expression")) {
        return script_analyzer_expression(a, script_analyzer_field(node, "expression"), expected);
    }
    if (script_analyzer_is(node, "await_expression")) {
        TSNode inner = script_analyzer_field(node, "value");
        while (script_analyzer_is(inner, "parenthesized_expression")) {
            inner = script_analyzer_field(inner, "expression");
        }
        TSNode outer = a->awaited;
        a->awaited = inner;
        int value = script_analyzer_expression(a, inner, expected);
        a->awaited = outer;
        script_expression *call = &a->out->expressions[value];
        if ((call->type.flags & SCRIPT_TYPE_TASK) &&
            !(call->type.flags & SCRIPT_TYPE_OPTIONAL) &&
            call->type.container == PIN_CONTAINER_SINGLE) {
            return join_task(a, node, value);
        }
        if (call->kind != SCRIPT_EXPR_CALL) {
            script_analyzer_diagnostic(a, node, "Await requires a capability call");
        }
        const script_catalog_capability
            *capability = script_catalog_capability_by_id(a->catalog, call->capability);
        if (capability && capability->completion_outcome < 0) {
            script_analyzer_diagnostic(a, node, "Await requires one declared completion outcome");
        }
        if (call->symbol >= 0 && a->out->symbols[call->symbol].kind == SCRIPT_ACTION &&
            call->type.storage != PIN_FLOW &&
            (expected.storage != PIN_FLOW || (expected.flags & SCRIPT_TYPE_UNTYPED))) {
            script_analyzer_diagnostic(a, node, "Cannot capture an awaited shared action result");
        }
        if (script_analyzer_synchronous_await(a, inner) && capability &&
            capability->component_access != SCRIPT_COMPONENT_ACCESS_NONE &&
            !(capability->effects & SCRIPT_EFFECT_SUSPEND)) {
            /* Component access completes in place; await adds no task or
             * completion ownership to the resolved call. */
            return value;
        }
        call->awaited = true;
        /* Component accessors complete in place, awaited or not. */
        if (!capability || capability->component_access == SCRIPT_COMPONENT_ACCESS_NONE) {
            call->effects |= SCRIPT_EFFECT_SUSPEND;
        }
        asetlen(a->narrowed, 0);
        return value;
    }
    if (script_analyzer_is(node, "query_expression")) {
        int description = script_analyzer_query_description(a, node, false);
        unsigned effects = SCRIPT_EFFECT_READ;
        const script_query_description *query = &a->out->descriptions[description];
        for (int i = 0; i < acount(query->captures); i++) {
            const script_symbol *capture = &a->out->symbols[query->captures[i]];
            int function = capture->scope >= 0 ? a->out->scopes[capture->scope].function
                                               : -1;
            if ((!capture->query_description &&
                 (capture->kind == SCRIPT_STATE || capture->kind == SCRIPT_SHARED_STATE ||
                  function < 0 || function != a->function)) ||
                (capture->query_description && a->function >= 0 &&
                 function != a->function)) {
                effects |= SCRIPT_EFFECT_INVOCATION_READ;
            }
        }
        script_type type = a->out->descriptions[description].row_type;
        type.flags = SCRIPT_TYPE_QUERY;
        return script_analyzer_expr_add(
            a,
            (script_expression){ .kind = SCRIPT_EXPR_QUERY,
                                 .type = type,
                                 .range = script_analyzer_range(a, node),
                                 .entry = description,
                                 .symbol = -1,
                                 .left = -1,
                                 .right = -1,
                                 .effects = effects }
        );
    }
    if (script_analyzer_is(node, "number")) {
        return number_constant(a, node, expected, false);
    }
    if (script_analyzer_is(node, "character")) {
        return character_constant(a, node, expected);
    }
    if (script_analyzer_is(node, "boolean")) {
        result.kind = SCRIPT_EXPR_CONSTANT;
        result.type = script_analyzer_boolean_type();
        result.constant = (pin_value){ .type = PIN_BOOL,
                                       .b = script_analyzer_text_is(a, node, "true") };
        return script_analyzer_expr_add(a, result);
    }
    if (script_analyzer_is(node, "string")) {
        result.kind = SCRIPT_EXPR_CONSTANT;
        result.type = (script_type){ .storage = expected.storage == PIN_NAME
                                                  ? PIN_NAME
                                                  : PIN_STR };
        result.constant = result.type.storage == PIN_NAME
                            ? (pin_value){ .type = PIN_NAME,
                                           .str = script_analyzer_name_copy(a, node) }
                            : script_analyzer_string_value(a, node);
        result.constant_snapshot = result.type.storage == PIN_STR;
        result.type.flags |= SCRIPT_TYPE_LITERAL;
        pin_string text = pin_text(result.constant);
        result.type.parameter = script_catalog_id_bytes("union-literal", text.bytes, text.length);
        return script_analyzer_expr_add(a, result);
    }
    if (script_analyzer_is(node, "nil")) {
        if (expected.storage == PIN_POINTER && expected.container == PIN_CONTAINER_SINGLE &&
            !(expected.flags & (SCRIPT_TYPE_OPTIONAL | SCRIPT_TYPE_UNTYPED))) {
            script_analyzer_diagnostic(a, node, "A non-null pointer cannot be nil");
        }
        if (script_reference_kind_for_type(expected) != SCRIPT_REFERENCE_NONE &&
            !(expected.flags & SCRIPT_TYPE_OPTIONAL)) {
            script_analyzer_diagnostic(a, node, "A nonoptional symbol reference must name a declared symbol");
            return script_analyzer_expr_add(a, result);
        }
        if ((expected.flags & SCRIPT_TYPE_TASK) &&
            !(expected.flags & SCRIPT_TYPE_OPTIONAL) &&
            expected.container == PIN_CONTAINER_SINGLE) {
            result.kind = SCRIPT_EXPR_CONSTANT;
            result.type = expected;
            result.constant = (pin_value){ .type = PIN_TASK };
            return script_analyzer_expr_add(a, result);
        }
        if (expected.storage == PIN_UNION && expected.container == PIN_CONTAINER_SINGLE &&
            !(expected.flags & SCRIPT_TYPE_OPTIONAL)) {
            script_analyzer_diagnostic(a, node, "A nonoptional union requires one of its alternatives");
        }
        if (script_analyzer_text_is(a, node, "null") &&
            !(expected.flags & SCRIPT_TYPE_OPTIONAL) &&
            !(expected.flags & SCRIPT_TYPE_UNTYPED)) {
            return zero_value(a, node, expected);
        }
        result.kind = SCRIPT_EXPR_NIL;
        result.type = expected;
        result.type.flags |= SCRIPT_TYPE_OPTIONAL;
        return script_analyzer_expr_add(a, result);
    }
    if (script_analyzer_is(node, "identifier") ||
        script_analyzer_is(node, "builtin_reference")) {
        if (script_analyzer_text_is(a, node, "Cancelled")) {
            result.kind = SCRIPT_EXPR_UNION;
            result.type = script_cancelled_type();
            result.tag = script_type_tag(result.type);
            return script_analyzer_expr_add(a, result);
        }
        int index = script_analyzer_lookup(a, node);
        if (index < 0) {
            char *name = script_analyzer_name_copy(a, node);
            int callable = script_analyzer_callable_symbol(a, name);
            free(name);
            if (callable >= 0 && a->out->symbols[callable].callable_binding) {
                index = callable;
            }
        }
        if (index >= 0 && a->out->symbols[index].callable_binding) {
            script_analyzer_diagnostic(a, node, "Callable arguments can only be called or passed to another callable parameter");
            return script_analyzer_expr_add(a, result);
        }
        if (index < 0) {
            int reference = script_analyzer_symbol_reference(a, node, expected);
            if (reference >= 0) {
                return reference;
            }
            char *name = script_analyzer_name_copy(a, node);
            script_analyzer_diagnostic(a, node, "Unknown value '%s'", name);
            free(name);
            return script_analyzer_expr_add(a, result);
        }
        script_analyzer_resolve_initializer(a, index);
        script_symbol symbol = a->out->symbols[index];
        if (symbol.kind == SCRIPT_PARAMETER && symbol.function >= 0 &&
            !a->query && symbol.function != a->function &&
            a->out->functions[symbol.function].kind == SCRIPT_FUNCTION_SYSTEM) {
            script_analyzer_diagnostic(a, node, "Read the step in the system's body, and pass it to what it calls");
        }
        if ((symbol.type.flags & SCRIPT_TYPE_RESULTS) &&
            symbol.kind == SCRIPT_PARAMETER && symbol.function >= 0 &&
            a->out->functions[symbol.function].kind == SCRIPT_FUNCTION_HANDLER) {
            script_analyzer_diagnostic(a, node, "This handler reads its payload by the field names it lists; list the field instead");
        }
        if (hides_parameter(a, &symbol)) {
            script_analyzer_diagnostic(
                a,
                node,
                "'%s' names both this handler's payload and a parameter of the "
                "function around it; "
                "list the payload's parameters to give them other names",
                symbol.name
            );
        }
        int owner = symbol.scope >= 0 ? a->out->scopes[symbol.scope].function : -1;
        if (a->out->kind == SCRIPT_PROGRAM_SYSTEM &&
            symbol.kind == SCRIPT_LOCAL && owner < 0 && a->function >= 0 &&
            a->out->functions[a->function].kind != SCRIPT_FUNCTION_SYSTEM &&
            a->out->functions[a->function].kind != SCRIPT_FUNCTION_CONTINUATION) {
            script_analyzer_diagnostic(a, node, "Pass system invocation locals as parameters across callable boundaries");
        }
        if (a->function >= 0 && owner >= 0 && owner != a->function &&
            a->out->functions[a->function].kind == SCRIPT_FUNCTION_AUTHORED &&
            (symbol.kind == SCRIPT_LOCAL || symbol.kind == SCRIPT_PARAMETER ||
             symbol.kind == SCRIPT_RESULT)) {
            script_analyzer_diagnostic(a, node, "Nested functions require enclosing locals to be passed as parameters");
        }
        result.kind = SCRIPT_EXPR_SYMBOL;
        result.type = symbol.type;
        result.symbol = index;
        result.capability = symbol.capability;
        result.writable = symbol.writable;
        if (script_analyzer_present(a, index)) {
            result.type.flags &= ~SCRIPT_TYPE_OPTIONAL;
        }
        result.effects = symbol.kind == SCRIPT_STATE ||
                                 symbol.kind == SCRIPT_SHARED_STATE ||
                                 symbol.kind == SCRIPT_COMPONENT
                           ? SCRIPT_EFFECT_READ
                           : 0;
        if (a->out->kind == SCRIPT_PROGRAM_LIBRARY && index == a->out->owner_symbol) {
            result.effects |= SCRIPT_EFFECT_INVOCATION_READ;
        }
        script_analyzer_use(a, node, index, SCRIPT_READ, 0);
        script_type narrowed = script_analyzer_narrowed_type(a, index, result.type);
        if ((symbol.type.flags & SCRIPT_TYPE_BOXED_OPTIONAL) &&
            script_analyzer_present(a, index)) {
            narrowed.flags &= ~(SCRIPT_TYPE_OPTIONAL | SCRIPT_TYPE_BOXED_OPTIONAL);
        }
        if (((symbol.type.flags & SCRIPT_TYPE_UNION) &&
             symbol.type.container == PIN_CONTAINER_SINGLE &&
             (!(narrowed.flags & SCRIPT_TYPE_UNION) || narrowed.container != PIN_CONTAINER_SINGLE ||
              ((narrowed.flags & SCRIPT_TYPE_BOXED_OPTIONAL) &&
               !(symbol.type.flags & SCRIPT_TYPE_BOXED_OPTIONAL)))) ||
            ((symbol.type.flags & SCRIPT_TYPE_BOXED_OPTIONAL) &&
             !(narrowed.flags & SCRIPT_TYPE_BOXED_OPTIONAL))) {
            result.type = symbol.type;
            int source = script_analyzer_expr_add(a, result);
            return script_analyzer_expr_add(
                a,
                (script_expression){ .kind = SCRIPT_EXPR_UNWRAP,
                                     .type = narrowed,
                                     .range = result.range,
                                     .left = source,
                                     .right = -1,
                                     .symbol = -1,
                                     .entry = -1,
                                     .tag = 0,
                                     .writable = result.writable,
                                     .effects = result.effects }
            );
        }
        result.type = narrowed;
        return script_analyzer_expr_add(a, result);
    }
    if (script_analyzer_is(node, "is_expression") ||
        script_analyzer_is(node, "as_expression")) {
        return script_analyzer_pattern_expression(a, node);
    }
    if (script_analyzer_is(node, "member_expression")) {
        int variant = script_analyzer_union_constructor(a, node);
        if (variant >= 0) {
            return variant;
        }
        return script_analyzer_member_expression(a, node, expected);
    }
    if (script_analyzer_is(node, "call_expression")) {
        TSNode function = script_analyzer_field(node, "function");
        if (script_analyzer_map_method(a, function, "has") ||
            script_analyzer_map_method(a, function, "delete")) {
            return map_method(a, node);
        }
        int variant = script_analyzer_union_constructor(a, node);
        if (variant >= 0) {
            return variant;
        }
        return script_analyzer_call_expression(a, node, expected);
    }
    if (script_analyzer_is(node, "emit_expression")) {
        return script_analyzer_call_expression(a, node, expected);
    }
    if (script_analyzer_is(node, "unary_expression")) {
        result.kind = SCRIPT_EXPR_UNARY;
        TSNode op = script_analyzer_field(node, "operator");
        TSNode operand = script_analyzer_field(node, "operand");
        if (script_analyzer_text_is(a, op, "-") &&
            script_analyzer_is(operand, "number")) {
            int constant = number_constant(a, operand, expected, true);
            a->out->expressions[constant].range = script_analyzer_range(a, node);
            return constant;
        }
        result.op = script_analyzer_operator_node(a, op);
        result.left = script_analyzer_expression(a, script_analyzer_field(node, "operand"), expected);
        result.type = result.op == SCRIPT_OP_NOT
                        ? script_analyzer_presence(a, result.left)
                        : script_analyzer_expr_type(a, result.left);
        result.effects = script_analyzer_expr_effects(a, result.left);
        if (result.op == SCRIPT_OP_NOT) {
            if (!(result.type.flags & SCRIPT_TYPE_OPTIONAL) &&
                !((result.type.flags & SCRIPT_TYPE_TASK) &&
                  result.type.container == PIN_CONTAINER_SINGLE)) {
                script_analyzer_require(
                    a,
                    node,
                    script_analyzer_boolean_type(),
                    result.type
                );
            }
            result.type = script_analyzer_boolean_type();
        } else if (script_analyzer_text_is(a, op, "-")) {
            result.op = SCRIPT_OP_NEG;
            if (!script_type_numeric(result.type) &&
                !(result.type.flags & SCRIPT_TYPE_ERROR) &&
                !script_analyzer_resolve_operator(a, node, op, &result)) {
                script_analyzer_diagnostic(a, node, "Negation requires a compatible operator");
            }
        } else if (script_analyzer_text_is(a, op, "+")) {
            result.op = SCRIPT_OP_NONE;
        } else if (script_analyzer_text_is(a, op, "~")) {
            if (!script_type_numeric(result.type) || result.type.storage > PIN_U64) {
                script_analyzer_diagnostic(a, node, "Bitwise inversion requires an integer value");
            }
        } else {
            script_analyzer_diagnostic(a, op, "Unsupported unary operation");
        }
        return script_analyzer_expr_add(a, result);
    }
    if (script_analyzer_is(node, "binary_expression")) {
        result.kind = SCRIPT_EXPR_BINARY;
        TSNode left = script_analyzer_field(node, "left"),
               right = script_analyzer_field(node, "right"),
               op = script_analyzer_field(node, "operator");
        bool not_in = script_analyzer_is(op, "not_in");
        if (not_in || script_analyzer_text_is(a, op, "in")) {
            int map = script_analyzer_expression(a, right, (script_type){ .flags = SCRIPT_TYPE_UNTYPED });
            int has = script_analyzer_map_operation(a, node, SCRIPT_EXPR_MAP_HAS, map, left);
            if (!not_in) {
                return has;
            }
            result.kind = SCRIPT_EXPR_UNARY;
            result.op = SCRIPT_OP_NOT;
            result.left = has;
            result.type = script_analyzer_boolean_type();
            result.effects = script_analyzer_expr_effects(a, has);
            return script_analyzer_expr_add(a, result);
        }
        result.op = script_analyzer_operator_node(a, op);
        result.left = script_analyzer_expression(a, left, (script_type){ .flags = SCRIPT_TYPE_UNTYPED });
        if (result.op == SCRIPT_OP_COALESCE) {
            return coalesce(a, node, result);
        }
        if (result.op == SCRIPT_OP_AND || result.op == SCRIPT_OP_OR) {
            narrowing *saved = NULL;
            for (int i = 0; i < acount(a->narrowed); i++) {
                apush(saved, a->narrowed[i]);
            }
            script_analyzer_condition_type(a, left, result.left);
            script_analyzer_narrow_condition(a, result.left, result.op == SCRIPT_OP_AND);
            result.right = script_analyzer_expression(a, right, script_analyzer_boolean_type());
            script_analyzer_condition_type(a, right, result.right);
            result.effects = script_analyzer_expr_effects(a, result.left) |
                             script_analyzer_expr_effects(a, result.right);
            asetlen(a->narrowed, 0);
            if (!(result.effects & SCRIPT_EFFECT_SUSPEND)) {
                for (int i = 0; i < acount(saved); i++) {
                    apush(a->narrowed, saved[i]);
                }
            }
            afree(saved);
            result.type = script_analyzer_boolean_type();
            return script_analyzer_expr_add(a, result);
        }
        result.right = script_analyzer_expression(a, right, (script_type){ .flags = SCRIPT_TYPE_UNTYPED });
        script_type x = script_analyzer_expr_type(a, result.left),
                    y = script_analyzer_expr_type(a, result.right);
        bool shift = result.op == SCRIPT_OP_SHIFT_LEFT ||
                     result.op == SCRIPT_OP_SHIFT_RIGHT;
        if (shift && script_type_numeric(x) && script_type_numeric(y)) {
            return shift_expression(a, node, result, expected);
        }
        if (typed_integer(x) && typed_integer(y) &&
            !script_analyzer_integers_match(x, y)) {
            script_analyzer_mismatched_integers(a, node, x, y);
        }
        if (script_type_numeric(x) && script_type_numeric(y)) {
            script_type operands = script_analyzer_arithmetic_type(x, y);
            if ((x.flags & y.flags & SCRIPT_TYPE_UNTYPED) &&
                script_type_numeric(expected)) {
                operands = expected;
            }
            if (!(x.flags & y.flags & SCRIPT_TYPE_UNTYPED) ||
                script_type_numeric(expected)) {
                operands.flags &= ~SCRIPT_TYPE_UNTYPED;
            }
            script_analyzer_contextual_numeric(a, result.left, operands);
            script_analyzer_contextual_numeric(a, result.right, operands);
            x = script_analyzer_expr_type(a, result.left);
            y = script_analyzer_expr_type(a, result.right);
        }
        result.effects = script_analyzer_expr_effects(a, result.left) |
                         script_analyzer_expr_effects(a, result.right);
        bool equality = result.op == SCRIPT_OP_EQ || result.op == SCRIPT_OP_NE;
        bool comparison = result.op >= SCRIPT_OP_EQ && result.op <= SCRIPT_OP_GE;
        bool logical = result.op == SCRIPT_OP_AND || result.op == SCRIPT_OP_OR;
        if (logical || (equality && (script_analyzer_is(left, "nil") ||
                                     script_analyzer_is(right, "nil")))) {
            x = script_analyzer_presence(a, result.left);
            y = script_analyzer_presence(a, result.right);
        }
        if (equality &&
            (script_analyzer_is(left, "nil") || script_analyzer_is(right, "nil"))) {
            script_type nonnil = script_analyzer_is(left, "nil") ? y : x;
            if (!(nonnil.flags & SCRIPT_TYPE_OPTIONAL)) {
                script_analyzer_diagnostic(a, node, "Nil comparison requires an optional value");
            }
            result.type = script_analyzer_boolean_type();
        } else if (logical) {
            script_analyzer_require(a, left, script_analyzer_boolean_type(), x);
            script_analyzer_require(a, right, script_analyzer_boolean_type(), y);
            result.type = script_analyzer_boolean_type();
        } else if ((!script_type_numeric(x) || !script_type_numeric(y)) && script_analyzer_resolve_operator(a, node, op, &result)) {
            /* Catalog operands and result are already resolved. */
        } else if (equality && !script_type_numeric(x)) {
            if (behavior_scalar_string(x.storage) &&
                behavior_scalar_string(y.storage) && x.container == PIN_CONTAINER_SINGLE &&
                y.container == PIN_CONTAINER_SINGLE) {
                if (x.flags & SCRIPT_TYPE_LITERAL) {
                    x.flags &= ~SCRIPT_TYPE_LITERAL;
                    x.parameter = 0;
                }
                if (y.flags & SCRIPT_TYPE_LITERAL) {
                    y.flags &= ~SCRIPT_TYPE_LITERAL;
                    y.parameter = 0;
                }
            }
            script_analyzer_require(a, right, x, y);
            result.type = script_analyzer_boolean_type();
        } else if (script_type_numeric(x) && script_type_numeric(y)) {
            result.type = comparison ? script_analyzer_boolean_type()
                                     : script_analyzer_arithmetic_type(x, y);
        } else if ((x.flags | y.flags) & SCRIPT_TYPE_ERROR) {
            result.type = script_analyzer_bad_type();
        } else {
            script_analyzer_diagnostic(a, node, "Operation has no compatible typed operands");
        }
        return script_analyzer_expr_add(a, result);
    }
    if (script_analyzer_is(node, "index_expression")) {
        TSNode bounds = script_analyzer_field(node, "index");
        if (script_analyzer_is(bounds, "range_expression")) {
            return script_analyzer_sequence(a, node, script_analyzer_field(node, "object"), bounds, SCRIPT_SEQUENCE_SLICE);
        }
        result.kind = SCRIPT_EXPR_INDEX;
        result.left = script_analyzer_expression(
            a,
            script_analyzer_field(node, "object"),
            (script_type){ .flags = SCRIPT_TYPE_UNTYPED }
        );
        script_type collection_type = script_analyzer_expr_type(a, result.left);
        if (collection_type.flags & SCRIPT_TYPE_OPTIONAL) {
            script_analyzer_diagnostic(a, node, "Narrow an optional sequence before indexing it");
        }
        script_type key_type = collection_type.container == PIN_CONTAINER_MAP
                                 ? script_catalog_key_type(a->catalog, collection_type)
                                 : (script_type){ .storage = PIN_I32 };
        result.right = script_analyzer_expression(a, script_analyzer_field(node, "index"), key_type);
        result.type = script_analyzer_expr_type(a, result.left);
        /* A string's index reads one of its bytes. */
        bool text = result.type.storage == PIN_STR &&
                    result.type.container == PIN_CONTAINER_SINGLE;
        if (!text && result.type.container != PIN_CONTAINER_ARRAY &&
            result.type.container != PIN_CONTAINER_VIEW &&
            result.type.container != PIN_CONTAINER_MAP) {
            script_analyzer_diagnostic(a, node, "Index requires an array, map or string");
            result.type = script_analyzer_bad_type();
        }
        result.type = text ? (script_type){ .storage = PIN_I32 }
                           : script_catalog_element_type(a->catalog, result.type);
        script_analyzer_require(
            a,
            script_analyzer_field(node, "index"),
            key_type,
            script_analyzer_expr_type(a, result.right)
        );
        if (collection_type.container == PIN_CONTAINER_MAP &&
            !script_type_equal(key_type, script_analyzer_expr_type(a, result.right))) {
            script_analyzer_diagnostic(a, node, "Map key type does not match");
        }
        int absent = collection_type.container == PIN_CONTAINER_MAP
                       ? absent_value(a, node, result.type)
                       : -1;
        if (absent >= 0) {
            apush(
                result.arguments,
                ((script_argument){ absent, -1, 0, script_analyzer_range(a, node) })
            );
        }
        result.effects = script_analyzer_expr_effects(a, result.left) |
                         script_analyzer_expr_effects(a, result.right);
        /* An element of a view is read through its pointer. */
        if (collection_type.container == PIN_CONTAINER_VIEW) {
            result.effects |= SCRIPT_EFFECT_READ;
        }
        result.writable = collection_type.container == PIN_CONTAINER_VIEW ||
                          a->out->expressions[result.left].writable;
        return script_analyzer_expr_add(a, result);
    }
    if (script_analyzer_is(node, "array_expression")) {
        result.kind = SCRIPT_EXPR_ARRAY;
        script_type element = script_catalog_element_type(a->catalog, expected);
        bool inferred = expected.flags & SCRIPT_TYPE_UNTYPED;
        for (uint32_t i = 0; i < script_analyzer_named_count(node); i++) {
            TSNode child = ts_node_named_child(node, i);
            if (ts_node_is_extra(child)) {
                continue;
            }
            int value = script_analyzer_expression(a, child, element);
            if (inferred && !acount(result.arguments)) {
                element = script_analyzer_expr_type(a, value);
                element.flags &= ~SCRIPT_TYPE_UNTYPED;
                /* A constant's value is not an inferred array constraint. */
                if (element.container == PIN_CONTAINER_SINGLE &&
                    (element.storage == PIN_STR || element.storage == PIN_NAME) &&
                    (element.flags & SCRIPT_TYPE_LITERAL)) {
                    element.flags &= ~SCRIPT_TYPE_LITERAL;
                    element.parameter = 0;
                }
            }
            script_type actual = script_analyzer_expr_type(a, value);
            if (inferred && element.container == PIN_CONTAINER_SINGLE &&
                (element.storage == PIN_STR || element.storage == PIN_NAME)) {
                /* Untyped numbers cannot inhabit string/name element storage. */
                actual.flags &= ~SCRIPT_TYPE_UNTYPED;
            }
            script_analyzer_require(a, child, element, actual);
            apush(
                result.arguments,
                ((script_argument){ value, -1, 0, script_analyzer_range(a, child) })
            );
            result.effects |= script_analyzer_expr_effects(a, value);
        }
        if (!acount(result.arguments) && inferred) {
            script_analyzer_diagnostic(a, node, "Empty arrays need an explicit element type");
            element = script_analyzer_bad_type();
        }
        result.type = element.collection ||
                              element.container != PIN_CONTAINER_SINGLE ||
                              (element.flags & SCRIPT_TYPE_OPTIONAL)
                        ? script_analyzer_collection_type(a, PIN_CONTAINER_ARRAY, (script_type){ 0 }, element)
                        : element;
        result.type.container = (expected.container == PIN_CONTAINER_SINGLE ||
                                 expected.container == PIN_CONTAINER_VIEW)
                                  ? PIN_CONTAINER_ARRAY
                                  : expected.container;
        return script_analyzer_expr_add(a, result);
    }
    if (script_analyzer_is(node, "map_expression")) {
        return map_literal(a, node, expected);
    }
    if (script_analyzer_is(node, "struct_expression")) {
        result.kind = SCRIPT_EXPR_STRUCT;
        result.type = expected;
        const script_catalog_type
            *type = script_catalog_type_by_id(a->catalog, expected.nominal);
        script_catalog_type definition = type ? *type : (script_catalog_type){ 0 };
        type = type ? &definition : NULL;
        if (!type || expected.storage != PIN_STRUCT) {
            script_analyzer_diagnostic(a, node, "Struct literal needs an explicit record or reflected type");
            result.type = script_analyzer_bad_type();
        }
        for (uint32_t i = 0; i < script_analyzer_named_count(node); i++) {
            TSNode child = ts_node_named_child(node, i);
            if (ts_node_is_extra(child)) {
                continue;
            }
            char *name = script_analyzer_name_copy(a, script_analyzer_field(child, "name"));
            int index = type ? script_catalog_find_field(type, name) : -1;
            if (index < 0) {
                script_analyzer_diagnostic(a, child, "Unknown/ambiguous literal member '%s'", name);
            }
            int value = script_analyzer_expression(
                a,
                script_analyzer_field(child, "value"),
                index >= 0 ? type->fields[index].type : script_analyzer_bad_type()
            );
            for (int j = 0; j < acount(result.arguments); j++) {
                if (result.arguments[j].parameter == index && index >= 0) {
                    script_analyzer_diagnostic(a, child, "Duplicate literal member '%s'", name);
                }
            }
            apush(
                result.arguments,
                ((script_argument){ value,
                                    index,
                                    index >= 0 ? type->fields[index].id : 0,
                                    script_analyzer_range(a, child) })
            );
            if (index >= 0) {
                if (type->fields[index].c_string) {
                    script_analyzer_c_string_argument(
                        a,
                        script_analyzer_field(child, "value"),
                        value,
                        type->fields[index].name
                    );
                }
                script_analyzer_require(
                    a,
                    child,
                    type->fields[index].type,
                    script_analyzer_expr_type(a, value)
                );
            }
            result.effects |= script_analyzer_expr_effects(a, value);
            free(name);
        }
        record_defaults(a, node, &result);
        return script_analyzer_expr_add(a, result);
    }
    script_analyzer_diagnostic(a, node, "Unsupported expression '%s'", ts_node_type(node));
    return script_analyzer_expr_add(a, result);
}
int script_analyzer_expression(analyzer *a, TSNode node, script_type expected)
{
    if (a->depth >= SCRIPT_MAX_DEPTH) {
        script_analyzer_diagnostic(a, node, "Expression nesting exceeds %d", SCRIPT_MAX_DEPTH);
        return script_analyzer_expr_add(
            a,
            (script_expression){ .kind = SCRIPT_EXPR_ERROR,
                                 .type = script_analyzer_bad_type(),
                                 .symbol = -1,
                                 .left = -1,
                                 .right = -1,
                                 .range = script_analyzer_range(a, node) }
        );
    }
    a->depth++;
    int result = script_analyzer_expression_inner(a, node, expected);
    const script_expression *resolved = &a->out->expressions[result];
    /* Header callables are proved pure after their bodies are resolved. */
    bool unknown_effects = !a->query && resolved->kind == SCRIPT_EXPR_CALL &&
                           resolved->right >= 0 &&
                           !script_analyzer_readonly_callable(a, resolved->right);
    if (unknown_effects || (script_analyzer_expr_effects(a, result) &
                            (SCRIPT_EFFECT_SUSPEND | SCRIPT_EFFECT_STRUCTURAL |
                             SCRIPT_EFFECT_WRITE | SCRIPT_EFFECT_HOST))) {
        script_analyzer_invalidate_narrowing(a, -1);
    }
    a->depth--;
    return result;
}
