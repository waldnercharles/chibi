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

script_type script_analyzer_arithmetic_type(script_type left, script_type right)
{
    if (left.flags & SCRIPT_TYPE_UNTYPED) {
        if (!(right.flags & SCRIPT_TYPE_UNTYPED)) {
            return right;
        }
        return (script_type){ .storage = PIN_F64, .flags = SCRIPT_TYPE_UNTYPED };
    }
    if (right.flags & SCRIPT_TYPE_UNTYPED) {
        return left;
    }
    if (left.storage == PIN_F64 || right.storage == PIN_F64) {
        return (script_type){ .storage = PIN_F64 };
    }
    if (left.storage == PIN_F32 || right.storage == PIN_F32) {
        return (script_type){ .storage = PIN_F32 };
    }
    return left.storage >= right.storage ? left : right;
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
        if (!candidate->operator_name || strcmp(candidate->operator_name, name) ||
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

static int number_constant(analyzer *a, TSNode node, script_type expected, bool negate)
{
    script_expression result = { .kind = SCRIPT_EXPR_CONSTANT,
                                 .symbol = -1,
                                 .left = -1,
                                 .right = -1,
                                 .range = script_analyzer_range(node) };
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
        script_analyzer_diagnostic(a, node, "Numeric literal must be finite");
    }
    result.kind = SCRIPT_EXPR_CONSTANT;
    bool enumeration = expected.storage == PIN_ENUM &&
                       expected.container == PIN_CONTAINER_SINGLE;
    result.type = (script_type_numeric(expected) || enumeration) &&
                          !(expected.flags & SCRIPT_TYPE_UNTYPED)
                    ? expected
                    : (script_type){ .storage = PIN_F64, .flags = SCRIPT_TYPE_UNTYPED };
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
        return script_analyzer_expr_add(a, result);
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
    bool fits = isfinite(value);
    if (integer) {
        fits = fits && truncl(value) == value && value < limit &&
               value >= (unsigned_integer ? 0 : -limit);
    } else {
        fits = fits && fabsl(value) <=
                           (result.type.storage == PIN_F32 ? FLT_MAX : DBL_MAX);
    }
    bool exact_integer = integer && !strpbrk(text, ".eE");
    uint64_t exact = 0;
    int64_t exact_signed = 0;
    if (exact_integer && !unsigned_integer) {
        errno = 0;
        intmax_t parsed = strtoimax(text, &end, 10);
        fits = parsed >= INT64_MIN && parsed <= INT64_MAX;
        exact_signed = fits ? (int64_t)parsed : 0;
        fits = fits && !*end && errno != ERANGE &&
               (long double)exact_signed >= -limit &&
               (result.type.storage == PIN_I64 || (long double)exact_signed < limit);
    } else if (exact_integer) {
        errno = 0;
        uintmax_t parsed = strtoumax(text, &end, 10);
        exact = parsed <= UINT64_MAX ? (uint64_t)parsed : 0;
        uint64_t maximum = UINT64_MAX;
        switch (result.type.storage) {
            case PIN_I8: maximum = INT8_MAX; break;
            case PIN_U8: maximum = UINT8_MAX; break;
            case PIN_I16: maximum = INT16_MAX; break;
            case PIN_U16: maximum = UINT16_MAX; break;
            case PIN_I32: maximum = INT32_MAX; break;
            case PIN_U32: maximum = UINT32_MAX; break;
            case PIN_I64: maximum = INT64_MAX; break;
            default: break;
        }
        fits = parsed <= UINT64_MAX && text[0] != '-' && !*end &&
               errno != ERANGE && exact <= maximum;
    }
    if (!fits) {
        exact = 0;
        exact_signed = 0;
        script_analyzer_diagnostic(
            a,
            node,
            "Numeric literal is outside the range of %s",
            script_scalar_name(result.type.storage)
        );
        value = 0;
    }
    switch (result.type.storage) {
        case PIN_I8: result.constant.i8 = (int8_t)value; break;
        case PIN_U8: result.constant.u8 = (uint8_t)value; break;
        case PIN_I16: result.constant.i16 = (int16_t)value; break;
        case PIN_U16: result.constant.u16 = (uint16_t)value; break;
        case PIN_I32: result.constant.i = (int32_t)value; break;
        case PIN_ENUM:
            result.constant.e = exact_integer ? (int32_t)exact_signed : (int32_t)value;
            break;
        case PIN_U32: result.constant.u = (uint32_t)value; break;
        case PIN_I64:
            result.constant.i64 = exact_integer ? exact_signed : (int64_t)value;
            break;
        case PIN_U64:
            result.constant.u64 = exact_integer ? exact : (uint64_t)value;
            break;
        case PIN_F32: result.constant.f = (float)value; break;
        default: result.constant.d = value; break;
    }
    free(text);
    return script_analyzer_expr_add(a, result);
}

static void contextual_numeric(analyzer *a, int index, script_type type)
{
    script_expression *expression = &a->out->expressions[index];
    if (!(expression->type.flags & SCRIPT_TYPE_UNTYPED) || !script_type_numeric(type)) {
        return;
    }
    if (expression->kind == SCRIPT_EXPR_CONSTANT) {
        script_range range = expression->range;
        bool negative = signbit(expression->constant.d);
        TSNode node = ts_node_descendant_for_byte_range(
            ts_tree_root_node(a->syntax->tree),
            (uint32_t)range.start,
            (uint32_t)range.end
        );
        if (script_analyzer_is(node, "unary_expression")) {
            node = script_analyzer_field(node, "operand");
        }
        int converted = number_constant(a, node, type, negative);
        a->out->expressions[index] = a->out->expressions[converted];
        a->out->expressions[index].range = range;
        return;
    }
    if (expression->kind == SCRIPT_EXPR_UNARY || expression->kind == SCRIPT_EXPR_BINARY) {
        int left = expression->left, right = expression->right;
        if (left >= 0) {
            contextual_numeric(a, left, type);
        }
        if (right >= 0) {
            contextual_numeric(a, right, type);
        }
        a->out->expressions[index].type = type;
    }
}

/* Awaiting a task handle waits for that task and takes the results of the
 * function that started it, when the handle's type names one. */
static int join_task(analyzer *a, TSNode node, int value)
{
    script_type task = script_analyzer_expr_type(a, value);
    script_expression result = { .kind = SCRIPT_EXPR_JOIN,
                                 .type = { .storage = PIN_FLOW },
                                 .range = script_analyzer_range(node),
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
                                         .flags = SCRIPT_TYPE_RESULTS };
        }
        break;
    }
    asetlen(a->narrowed, 0);
    return script_analyzer_expr_add(a, result);
}
int script_analyzer_expression_inner(analyzer *a, TSNode node, script_type expected)
{
    script_expression result = { .kind = SCRIPT_EXPR_ERROR,
                                 .type = script_analyzer_bad_type(),
                                 .range = script_analyzer_range(node),
                                 .symbol = -1,
                                 .left = -1,
                                 .right = -1 };
    if (ts_node_is_null(node)) {
        script_analyzer_diagnostic(a, node, "Expected a value expression");
        return script_analyzer_expr_add(a, result);
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
        if (call->kind == SCRIPT_EXPR_STORE) {
            return value; /* A state write completes synchronously. */
        }
        if (call->type.flags & SCRIPT_TYPE_TASK) {
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
        call->awaited = true;
        /* Component accessors complete in place, awaited or not. */
        if (!capability || capability->component_access == SCRIPT_COMPONENT_ACCESS_NONE) {
            call->effects |= SCRIPT_EFFECT_SUSPEND;
        }
        asetlen(a->narrowed, 0);
        return value;
    }
    if (script_analyzer_is(node, "number")) {
        return number_constant(a, node, expected, false);
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
        result.constant = (pin_value){ .type = result.type.storage,
                                       .str = script_analyzer_name_copy(a, node) };
        return script_analyzer_expr_add(a, result);
    }
    if (script_analyzer_is(node, "nil")) {
        result.kind = SCRIPT_EXPR_NIL;
        result.type = expected;
        if (script_analyzer_text_is(a, node, "null") &&
            !(expected.flags & SCRIPT_TYPE_OPTIONAL) &&
            !(expected.flags & SCRIPT_TYPE_UNTYPED)) {
            result.kind = expected.container != PIN_CONTAINER_SINGLE ? SCRIPT_EXPR_ARRAY
                        : expected.storage == PIN_STRUCT ? SCRIPT_EXPR_STRUCT
                                                         : SCRIPT_EXPR_CONSTANT;
            result.constant = (pin_value){ .type = expected.storage,
                                           .container = expected.container };
        } else {
            result.type.flags |= SCRIPT_TYPE_OPTIONAL;
        }
        return script_analyzer_expr_add(a, result);
    }
    if (script_analyzer_is(node, "identifier") ||
        script_analyzer_is(node, "builtin_reference")) {
        int index = script_analyzer_lookup(a, node);
        if (index < 0) {
            char *name = script_analyzer_name_copy(a, node);
            script_analyzer_diagnostic(a, node, "Unknown value '%s'", name);
            free(name);
            return script_analyzer_expr_add(a, result);
        }
        script_analyzer_resolve_initializer(a, index);
        if (script_analyzer_is(node, "builtin_reference") &&
            script_analyzer_text_is(a, node, "entry")) {
            int function = a->out->symbols[index].function;
            if (function >= 0 && acount(a->out->functions[function].parameters) == 1) {
                index = a->out->functions[function].parameters[0];
            }
        }
        script_symbol symbol = a->out->symbols[index];
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
        script_analyzer_use(a, node, index, SCRIPT_READ, 0);
        return script_analyzer_expr_add(a, result);
    }
    if (script_analyzer_is(node, "member_expression")) {
        return script_analyzer_member_expression(a, node, expected);
    }
    if (script_analyzer_is(node, "call_expression")) {
        return script_analyzer_call_expression(a, node, expected);
    }
    if (script_analyzer_is(node, "cast_expression")) {
        result.kind = SCRIPT_EXPR_CAST;
        result.type = script_analyzer_type_node(a, script_analyzer_field(node, "type"));
        result.left = script_analyzer_expression(
            a,
            script_analyzer_field(node, "value"),
            result.type
        );
        script_type actual = script_analyzer_expr_type(a, result.left);
        bool numeric = script_type_numeric(actual) && script_type_numeric(result.type);
        if (!numeric &&
            script_catalog_type_assignable(a->catalog, result.type, actual)) {
            /* The cast only supplied the expected type, as for a polymorphic call. */
            return result.left;
        }
        if (!numeric) {
            script_analyzer_diagnostic(a, node, "Cast requires scalar numeric types or the value's own type");
        }
        result.effects = script_analyzer_expr_effects(a, result.left);
        return script_analyzer_expr_add(a, result);
    }
    if (script_analyzer_is(node, "unary_expression")) {
        result.kind = SCRIPT_EXPR_UNARY;
        TSNode op = script_analyzer_field(node, "operator");
        TSNode operand = script_analyzer_field(node, "operand");
        if (script_analyzer_text_is(a, op, "-") &&
            script_analyzer_is(operand, "number")) {
            int constant = number_constant(a, operand, expected, true);
            a->out->expressions[constant].range = script_analyzer_range(node);
            return constant;
        }
        result.op = script_analyzer_operator_node(a, op);
        result.left = script_analyzer_expression(a, script_analyzer_field(node, "operand"), expected);
        result.type = result.op == SCRIPT_OP_NOT
                        ? script_analyzer_presence(a, result.left)
                        : script_analyzer_expr_type(a, result.left);
        result.effects = script_analyzer_expr_effects(a, result.left);
        if (result.op == SCRIPT_OP_NOT) {
            if (!(result.type.flags & (SCRIPT_TYPE_OPTIONAL | SCRIPT_TYPE_TASK))) {
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
        result.op = script_analyzer_operator_node(a, op);
        result.left = script_analyzer_expression(a, left, (script_type){ .flags = SCRIPT_TYPE_UNTYPED });
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
            contextual_numeric(a, result.left, operands);
            contextual_numeric(a, result.right, operands);
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
        result.kind = SCRIPT_EXPR_INDEX;
        result.left = script_analyzer_expression(
            a,
            script_analyzer_field(node, "object"),
            (script_type){ .flags = SCRIPT_TYPE_UNTYPED }
        );
        result.right = script_analyzer_expression(
            a,
            script_analyzer_field(node, "index"),
            (script_type){ .storage = PIN_I32 }
        );
        result.type = script_analyzer_expr_type(a, result.left);
        if (result.type.container != PIN_CONTAINER_ARRAY &&
            result.type.container != PIN_CONTAINER_MAP) {
            script_analyzer_diagnostic(a, node, "Index requires an array or map");
            result.type = script_analyzer_bad_type();
        }
        result.type.container = PIN_CONTAINER_SINGLE;
        script_analyzer_require(
            a,
            script_analyzer_field(node, "index"),
            (script_type){ .storage = PIN_I32 },
            script_analyzer_expr_type(a, result.right)
        );
        result.effects = script_analyzer_expr_effects(a, result.left) |
                         script_analyzer_expr_effects(a, result.right);
        result.writable = a->out->expressions[result.left].writable;
        return script_analyzer_expr_add(a, result);
    }
    if (script_analyzer_is(node, "array_expression")) {
        result.kind = SCRIPT_EXPR_ARRAY;
        script_type element = expected;
        element.container = PIN_CONTAINER_SINGLE;
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
            }
            script_analyzer_require(a, child, element, script_analyzer_expr_type(a, value));
            apush(result.arguments, ((script_argument){ value, -1, 0, script_analyzer_range(child) }));
            result.effects |= script_analyzer_expr_effects(a, value);
        }
        if (!acount(result.arguments) && inferred) {
            script_analyzer_diagnostic(a, node, "Empty arrays need an explicit element type");
            element = script_analyzer_bad_type();
        }
        result.type = element;
        result.type.container = expected.container == PIN_CONTAINER_SINGLE
                                  ? PIN_CONTAINER_ARRAY
                                  : expected.container;
        return script_analyzer_expr_add(a, result);
    }
    if (script_analyzer_is(node, "struct_expression")) {
        result.kind = SCRIPT_EXPR_STRUCT;
        result.type = expected;
        const script_catalog_type
            *type = script_catalog_type_by_id(a->catalog, expected.nominal);
        if (!type || expected.storage != PIN_STRUCT) {
            script_analyzer_diagnostic(a, node, "Struct literal needs an explicit reflected type");
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
                                    script_analyzer_range(child) })
            );
            if (index >= 0) {
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
                                 .range = script_analyzer_range(node) }
        );
    }
    a->depth++;
    int result = script_analyzer_expression_inner(a, node, expected);
    a->depth--;
    return result;
}
