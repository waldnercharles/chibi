#include "language/scalar_constant.h"
#include "language/semantic_internal.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "types/scalar.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Keep branch-local scratch storage out of every active recursive frame. */
#if defined(_MSC_VER)
#define COMPILE_NOINLINE __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
#define COMPILE_NOINLINE __attribute__((noinline))
#else
#define COMPILE_NOINLINE
#endif

static int find(compile_binding *bindings, const char *name)
{
    for (int i = acount(bindings) - 1; i >= 0; i--) {
        if (!strcmp(bindings[i].name, name)) {
            return i;
        }
    }
    return -1;
}
static bool fail(analyzer *a, TSNode node, const char *message)
{
    if (!a->compile_failed) {
        script_analyzer_diagnostic(a, node, "Compile-time evaluation: %s", message);
        a->compile_failed = true;
    }
    return false;
}
static bool charge(analyzer *a, TSNode node, int *budget)
{
    return --*budget >= 0 || fail(a, node, "bounded execution budget exceeded");
}
bool script_analyzer_compile_as(
    analyzer *a,
    TSNode node,
    compile_binding *bindings,
    pin_type type,
    compile_value *out,
    int *budget
)
{
    pin_type previous = a->compile_numeric;
    a->compile_numeric = type;
    bool valid = script_analyzer_compile_expression(a, node, bindings, out, budget);
    a->compile_numeric = previous;
    return valid;
}
static bool scalar_cast(compile_value *value, pin_type type)
{
    if (value->is_type) {
        return false;
    }
    if (value->value.type == type) {
        return true;
    }
    if (value->value.type == PIN_F32 && type == PIN_F64) {
        value->value = (pin_value){ .type = PIN_F64, .d = value->value.f };
        return true;
    }
    if (value->value.type == PIN_F64 && type == PIN_F32) {
        value->value = (pin_value){ .type = PIN_F32, .f = (float)value->value.d };
        return true;
    }
    if (type == PIN_F32 && behavior_scalar_cast_supported(value->value.type, PIN_F64)) {
        pin_value wide;
        behavior_scalar_cast(value->value, PIN_F64, BEHAVIOR_CAST_WRAP, &wide);
        value->value = (pin_value){ .type = PIN_F32, .f = (float)wide.d };
        return true;
    }
    if (!behavior_scalar_cast_supported(value->value.type, type)) {
        return false;
    }
    behavior_scalar_cast(value->value, type, BEHAVIOR_CAST_WRAP, &value->value);
    return true;
}
static bool compile_constant(analyzer *a, TSNode node, int symbol, compile_value *out)
{
    script_analyzer_resolve_initializer(a, symbol);
    const script_symbol *constant = &a->out->symbols[symbol];
    pin_value value;
    const char *error = script_analyzer_scalar_constant(
        a,
        constant->initializer,
        constant->type.storage,
        &value
    );
    if (error) {
        return fail(a, node, error);
    }
    *out = (compile_value){ .value = value };
    return true;
}
static bool lookup(analyzer *a, TSNode node, compile_binding *bindings, const char *name, compile_value *out)
{
    int index = find(bindings, name);
    if (index >= 0) {
        *out = bindings[index].value;
        return true;
    }
    script_type builtin = script_builtin_type(name);
    if (!(builtin.flags & SCRIPT_TYPE_ERROR)) {
        *out = (compile_value){ .is_type = true, .type = builtin };
        return true;
    }
    int symbol = script_analyzer_lookup_name(a, name);
    if (symbol >= 0 && a->out->symbols[symbol].kind == SCRIPT_TYPE) {
        *out = (compile_value){ .is_type = true, .type = a->out->symbols[symbol].type };
        return true;
    }
    if (symbol >= 0 && a->out->symbols[symbol].kind == SCRIPT_CONSTANT) {
        return compile_constant(a, node, symbol, out);
    }
    int type = script_catalog_find_type(a->catalog, name);
    if (type >= 0) {
        *out = (compile_value){ .is_type = true, .type = a->catalog->types[type].type };
        out->type.nominal = a->catalog->types[type].id;
        return true;
    }
    return fail(a, node, "only constant values and types are available");
}
static bool truth(analyzer *a, TSNode node, compile_value value, bool *out)
{
    if (value.is_type || value.value.type != PIN_BOOL) {
        return fail(a, node, "condition requires a bool");
    }
    *out = value.value.b;
    return true;
}
typedef struct
{
    compile_value first, second;
    script_expression literal;
    script_type common;
    pin_value scalar;
} compile_expression_frame;
static bool expression(analyzer *, TSNode, compile_binding *, compile_value *, int *, compile_expression_frame *);
bool script_analyzer_compile_expression(
    analyzer *a,
    TSNode node,
    compile_binding *bindings,
    compile_value *out,
    int *budget
)
{
    if (!a->compile_expression_depth) {
        a->compile_failed = false;
    }
    if (a->compile_expression_depth >= SCRIPT_MAX_DEPTH) {
        return fail(a, node, "expression nesting exceeds the language depth limit");
    }
    a->compile_expression_depth++;
    compile_expression_frame *frame = xcalloc(1, sizeof *frame);
    bool valid = expression(a, node, bindings, out, budget, frame);
    free(frame);
    a->compile_expression_depth--;
    return valid;
}
static bool compile_parenthesized(
    analyzer *a,
    TSNode node,
    compile_binding *bindings,
    compile_value *out,
    int *budget,
    compile_expression_frame *frame
)
{
    (void)frame;

    return script_analyzer_compile_expression(
        a,
        script_analyzer_field(node, "expression"),
        bindings,
        out,
        budget
    );
}
static bool compile_named(
    analyzer *a,
    TSNode node,
    compile_binding *bindings,
    compile_value *out,
    int *budget,
    compile_expression_frame *frame
)
{
    (void)budget;

    char *name = script_analyzer_name_copy(a, node);
    char *dot = strchr(name, '.');
    bool valid;
    if (dot) {
        *dot = '\0';
        compile_value *type = &frame->first;
        valid = lookup(a, node, bindings, name, type) && type->is_type &&
                script_analyzer_generic_member(a, type->type, dot + 1, out);
        if (!valid) {
            fail(a, node, "unknown type specialization parameter");
        }
    } else {
        valid = lookup(a, node, bindings, name, out);
    }
    free(name);
    return valid;
}
static bool compile_member(
    analyzer *a,
    TSNode node,
    compile_binding *bindings,
    compile_value *out,
    int *budget,
    compile_expression_frame *frame
)
{
    TSNode object = script_analyzer_field(node, "object");
    char *name = script_analyzer_name_copy(a, object);
    bool bound = find(bindings, name) >= 0;
    free(name);
    int constant = bound ? -1 : script_analyzer_package_constant(a, node);
    if (constant >= 0) {
        return compile_constant(a, node, constant, out);
    }
    compile_value *type = &frame->first;
    if (!script_analyzer_compile_expression(a, object, bindings, type, budget)) {
        return false;
    }
    name = script_analyzer_name_copy(a, script_analyzer_field(node, "member"));
    bool valid = type->is_type &&
                 script_analyzer_generic_member(a, type->type, name, out);
    free(name);
    return valid || fail(a, node, "member requires a type specialization parameter");
}
static bool compile_number(
    analyzer *a,
    TSNode node,
    compile_binding *bindings,
    compile_value *out,
    int *budget,
    compile_expression_frame *frame
)
{
    (void)bindings;
    (void)budget;

    frame->literal = script_analyzer_number_literal(
        a,
        node,
        (script_type){ .storage = a->compile_numeric },
        false
    );
    *out = (compile_value){ .value = frame->literal.constant };
    return true;
}
/* A character literal evaluates to its scalar, an int as at run time. */
static bool compile_character(
    analyzer *a,
    TSNode node,
    compile_binding *bindings,
    compile_value *out,
    int *budget,
    compile_expression_frame *frame
)
{
    (void)bindings;
    (void)budget;
    (void)frame;

    int value = 0;
    if (!script_analyzer_character_value(a, node, &value)) {
        return false;
    }
    *out = (compile_value){ .value = { .type = PIN_I32, .i = value } };
    return true;
}
static bool compile_boolean(
    analyzer *a,
    TSNode node,
    compile_binding *bindings,
    compile_value *out,
    int *budget,
    compile_expression_frame *frame
)
{
    (void)bindings;
    (void)budget;
    (void)frame;

    *out = (compile_value){
        .value = { .type = PIN_BOOL, .b = script_analyzer_text_is(a, node, "true") }
    };
    return true;
}
static bool compile_string(
    analyzer *a,
    TSNode node,
    compile_binding *bindings,
    compile_value *out,
    int *budget,
    compile_expression_frame *frame
)
{
    (void)bindings;
    (void)budget;
    (void)frame;

    *out = (compile_value){ .value = script_analyzer_string_value(a, node) };
    return true;
}
/* The numeric type `T` in a call `T(value)`, as in a runtime conversion: a
 * bound comptime type or a builtin name no authored callable shadows. */
static bool conversion_type(analyzer *a, TSNode call, compile_binding *bindings, script_type *out)
{
    TSNode function = script_analyzer_field(call, "function");
    if (!script_analyzer_is(function, "identifier")) {
        return false;
    }
    char *name = script_analyzer_name_copy(a, function);
    int index = find(bindings, name);
    bool callable = index < 0 && script_analyzer_callable_symbol(a, name) >= 0;
    *out = index < 0                     ? script_builtin_type(name)
         : bindings[index].value.is_type ? bindings[index].value.type
                                         : script_analyzer_bad_type();
    free(name);
    return !callable && script_type_numeric(*out);
}
/* A call evaluated at compile time converts as at run time: integer
 * narrowing clamps unless the argument is named wrap. */
static bool compile_conversion(
    analyzer *a,
    TSNode call,
    compile_binding *bindings,
    pin_type type,
    compile_value *out,
    int *budget
)
{
    TSNode arguments = script_analyzer_field(call, "arguments"), value = { 0 };
    int count = 0;
    for (uint32_t i = 0; i < script_analyzer_named_count(arguments); i++) {
        TSNode child = ts_node_named_child(arguments, i);
        if (!ts_node_is_extra(child)) {
            value = child;
            count++;
        }
    }
    if (count != 1) {
        return fail(a, call, "conversion takes one value");
    }
    behavior_cast_mode mode = BEHAVIOR_CAST_EXACT;
    pin_type operand = type;
    if (script_analyzer_is(value, "named_argument")) {
        TSNode label = script_analyzer_field(value, "name");
        if (!script_analyzer_text_is(a, label, "wrap")) {
            return fail(a, label, "a conversion argument may only be named wrap");
        }
        mode = BEHAVIOR_CAST_WRAP;
        value = script_analyzer_field(value, "value");
        operand = script_analyzer_conversion_operand(a, value).storage;
    }
    if (!script_analyzer_compile_as(a, value, bindings, operand, out, budget)) {
        return false;
    }
    pin_type from = out->value.type;
    bool integers = !out->is_type && from >= PIN_I8 && from <= PIN_U64 &&
                    type >= PIN_I8 && type <= PIN_U64;
    if (!integers && mode != BEHAVIOR_CAST_EXACT) {
        return fail(a, call, "wrap converts between integer types");
    }
    if (!integers) {
        return scalar_cast(out, type) ||
               fail(a, call, "unsupported scalar conversion");
    }
    behavior_scalar_cast(out->value, type, mode, &out->value);
    return true;
}
/* A false assert while the compiler evaluates is a compile error, with the
 * message, or else the condition's text, that it fails a call with at run
 * time. Arguments bind by name or position, as calls bind them. */
static bool compile_assert(analyzer *a, TSNode node, compile_binding *bindings, compile_value *out, int *budget)
{
    TSNode arguments = script_analyzer_field(node, "arguments"), values[2] = { 0 };
    int position = 0;
    for (uint32_t i = 0; i < script_analyzer_named_count(arguments); i++) {
        TSNode child = ts_node_named_child(arguments, i);
        if (ts_node_is_extra(child)) {
            continue;
        }
        TSNode label = script_analyzer_field(child, "name");
        int index = position++;
        if (!ts_node_is_null(label)) {
            char *name = script_analyzer_name_copy(a, label);
            index = script_name_matches("Condition", name) ? 0
                  : script_name_matches("Message", name)   ? 1
                                                           : -1;
            free(name);
            child = script_analyzer_field(child, "value");
        }
        if (index < 0 || index > 1 || !ts_node_is_null(values[index])) {
            return fail(a, node, "assert takes a condition and a message");
        }
        values[index] = child;
    }
    bool test = false;
    if (ts_node_is_null(values[0]) ||
        !script_analyzer_compile_expression(a, values[0], bindings, out, budget) ||
        !truth(a, values[0], *out, &test)) {
        return false;
    }
    *out = (compile_value){ .value = { .type = PIN_FLOW } };
    if (test) {
        return true;
    }
    char *text = ts_node_is_null(values[1])
                   ? xstrdup("")
                   : script_analyzer_name_copy(a, values[1]);
    if (!*text) {
        free(text);
        text = script_analyzer_text_copy(a, values[0]);
    }
    size_t size = sizeof "Assertion failed: " + strlen(text);
    char *message = xmalloc(size);
    snprintf(message, size, "Assertion failed: %s", text);
    fail(a, node, message);
    free(message);
    free(text);
    return false;
}
static bool compile_call_expression(
    analyzer *a,
    TSNode node,
    compile_binding *bindings,
    compile_value *out,
    int *budget,
    compile_expression_frame *frame
)
{
    TSNode target = script_analyzer_field(node, "function");
    int symbol = script_analyzer_callable_symbol(a, "assert");
    if (script_analyzer_text_is(a, target, "assert") &&
        (symbol < 0 || a->out->symbols[symbol].function < 0)) {
        return compile_assert(a, node, bindings, out, budget);
    }
    if (conversion_type(a, node, bindings, &frame->common)) {
        return compile_conversion(a, node, bindings, frame->common.storage, out, budget);
    }
    return script_analyzer_compile_call(a, node, bindings, out, budget);
}
static bool compile_operation(
    analyzer *a,
    TSNode node,
    compile_binding *bindings,
    compile_value *out,
    int *budget,
    compile_expression_frame *frame
)
{
    bool unary = script_analyzer_is(node, "unary_expression");
    if (unary &&
        script_analyzer_text_is(a, script_analyzer_field(node, "operator"), "-") &&
        script_analyzer_is(script_analyzer_field(node, "operand"), "number")) {
        frame->literal = script_analyzer_number_literal(
            a,
            script_analyzer_field(node, "operand"),
            (script_type){ .storage = a->compile_numeric },
            true
        );
        *out = (compile_value){ .value = frame->literal.constant };
        return true;
    }
    if (unary || script_analyzer_is(node, "binary_expression")) {
        script_operator op = script_analyzer_operator_node(a, script_analyzer_field(node, "operator"));
        if (unary && op == SCRIPT_OP_SUB) {
            op = SCRIPT_OP_NEG;
        } else if (unary && op == SCRIPT_OP_ADD) {
            op = SCRIPT_OP_NONE;
        }
        compile_value *left = &frame->first, *right = &frame->second;
        TSNode left_node = script_analyzer_field(node, unary ? "operand" : "left");
        if (!script_analyzer_compile_expression(a, left_node, bindings, left, budget)) {
            return false;
        }
        if (op == SCRIPT_OP_AND || op == SCRIPT_OP_OR) {
            bool test;
            if (!truth(a, left_node, *left, &test)) {
                return false;
            }
            if ((op == SCRIPT_OP_AND && !test) || (op == SCRIPT_OP_OR && test)) {
                *out = *left;
                return true;
            }
            return script_analyzer_compile_expression(
                a,
                script_analyzer_field(node, "right"),
                bindings,
                out,
                budget
            );
        }
        if (!unary &&
            !script_analyzer_compile_expression(a, script_analyzer_field(node, "right"), bindings, right, budget)) {
            return false;
        }
        if (left->is_type || right->is_type) {
            if (!left->is_type || !right->is_type ||
                (op != SCRIPT_OP_EQ && op != SCRIPT_OP_NE)) {
                return fail(a, node, "type values support equality and inequality");
            }
            bool equal = script_type_equal(left->type, right->type);
            *out = (compile_value){
                .value = { .type = PIN_BOOL, .b = op == SCRIPT_OP_EQ ? equal : !equal }
            };
            return true;
        }
        pin_type result = left->value.type;
        if (!unary && left->value.type != right->value.type) {
            frame->common = script_analyzer_arithmetic_type(
                (script_type){ .storage = left->value.type },
                (script_type){ .storage = right->value.type }
            );
            if (!scalar_cast(left, frame->common.storage) ||
                !scalar_cast(right, frame->common.storage)) {
                return fail(a, node, "incompatible scalar operands");
            }
            result = frame->common.storage;
        }
        if (op == SCRIPT_OP_EQ || op == SCRIPT_OP_NE || op == SCRIPT_OP_LT ||
            op == SCRIPT_OP_LE || op == SCRIPT_OP_GT || op == SCRIPT_OP_GE ||
            op == SCRIPT_OP_NOT) {
            result = PIN_BOOL;
        }
        pin_value *value = &frame->scalar;
        const char *error = script_scalar_operation(op, left->value, right->value, result, value);
        if (error) {
            return fail(a, node, error);
        }
        *out = (compile_value){ .value = *value };
        return true;
    }
    return fail(a, node, "unsupported expression in a constant context");
}
typedef bool (*compile_expression_handler)(
    analyzer *a,
    TSNode node,
    compile_binding *bindings,
    compile_value *out,
    int *budget,
    compile_expression_frame *frame
);
static bool expression(
    analyzer *a,
    TSNode node,
    compile_binding *bindings,
    compile_value *out,
    int *budget,
    compile_expression_frame *frame
)
{
    if (ts_node_is_null(node) || !charge(a, node, budget)) {
        return false;
    }
    const char *kind = ts_node_type(node);
    int index = !strcmp(kind, "parenthesized_expression")                   ? 0
              : (!strcmp(kind, "identifier") || !strcmp(kind, "type_name")) ? 1
              : !strcmp(kind, "member_expression")                          ? 2
              : !strcmp(kind, "number")                                     ? 3
              : !strcmp(kind, "boolean")                                    ? 4
              : !strcmp(kind, "string")                                     ? 5
              : (!strcmp(kind, "call_expression") || !strcmp(kind, "type_application"))
                  ? 6
              : (!strcmp(kind, "unary_expression") || !strcmp(kind, "binary_expression"))
                  ? 7
              : !strcmp(kind, "character") ? 8
                                           : -1;
    if (index < 0) {
        return fail(a, node, "unsupported expression in a constant context");
    }
    /* Volatile dispatch preserves branch stack isolation through Wasm's
     * whole-program inliner, which does not retain C noinline attributes. */
    static compile_expression_handler const volatile handlers[] = {
        compile_parenthesized,   compile_named,     compile_member,
        compile_number,          compile_boolean,   compile_string,
        compile_call_expression, compile_operation, compile_character
    };
    return handlers[index](a, node, bindings, out, budget, frame);
}

typedef enum
{
    COMPILE_NEXT,
    COMPILE_RETURN,
    COMPILE_FAIL
} compile_control;
static compile_control statement(
    analyzer *a,
    TSNode node,
    compile_binding **bindings,
    compile_value *out,
    int *budget
);
static COMPILE_NOINLINE compile_control
compile_local(analyzer *a, TSNode node, compile_binding **bindings, compile_value *out, int *budget)
{
    (void)out;
    compile_value value;
    TSNode type = script_analyzer_field(node, "type");
    compile_value expected = { 0 };
    if (!ts_node_is_null(type) &&
        (!script_analyzer_compile_expression(a, script_analyzer_field(type, "name"), *bindings, &expected, budget) ||
         !expected.is_type)) {
        fail(a, node, "local requires a concrete type");
        return COMPILE_FAIL;
    }
    if (!script_analyzer_compile_as(
            a,
            script_analyzer_field(node, "value"),
            *bindings,
            expected.is_type ? expected.type.storage : PIN_FLOW,
            &value,
            budget
        )) {
        return COMPILE_FAIL;
    }
    if (!ts_node_is_null(type)) {
        if (!scalar_cast(&value, expected.type.storage)) {
            fail(a, node, "local initializer has incompatible type");
            return COMPILE_FAIL;
        }
    }
    char *name = script_analyzer_name_copy(a, script_analyzer_field(node, "name"));
    if (script_analyzer_is(node, "constant_declaration")) {
        apush(a->compile_readonly, acount(*bindings));
    }
    apush(*bindings, ((compile_binding){ name, value }));
    return COMPILE_NEXT;
}
static COMPILE_NOINLINE compile_control compile_assignment(
    analyzer *a,
    TSNode node,
    compile_binding **bindings,
    compile_value *out,
    int *budget
)
{
    (void)out;
    char *name = script_analyzer_name_copy(a, script_analyzer_field(node, "target"));
    int index = find(*bindings, name);
    free(name);
    if (index < 0) {
        fail(a, node, "assignment cannot change runtime or external storage");
        return COMPILE_FAIL;
    }
    for (int i = 0; i < acount(a->compile_readonly); i++) {
        if (a->compile_readonly[i] == index) {
            fail(a, node, "assignment cannot change a constant binding");
            return COMPILE_FAIL;
        }
    }
    compile_value value;
    if (!script_analyzer_compile_as(
            a,
            script_analyzer_field(node, "value"),
            *bindings,
            (*bindings)[index].value.value.type,
            &value,
            budget
        )) {
        return COMPILE_FAIL;
    }
    TSNode op_node = script_analyzer_field(node, "operator");
    if (!script_analyzer_text_is(a, op_node, "=")) {
        char *spelling = script_analyzer_text_copy(a, op_node);
        script_operator op = !strcmp(spelling, "+=") ? SCRIPT_OP_ADD
                           : !strcmp(spelling, "-=") ? SCRIPT_OP_SUB
                           : !strcmp(spelling, "*=") ? SCRIPT_OP_MUL
                           : !strcmp(spelling, "/=") ? SCRIPT_OP_DIV
                                                     : SCRIPT_OP_NONE;
        free(spelling);
        compile_value old = (*bindings)[index].value;
        const char *error = old.is_type || value.is_type
                              ? "cannot compound-assign a type"
                              : script_scalar_operation(
                                    op,
                                    old.value,
                                    value.value,
                                    old.value.type,
                                    &value.value
                                );
        if (error || op == SCRIPT_OP_NONE) {
            fail(a, node, error ? error : "unsupported compound assignment");
            return COMPILE_FAIL;
        }
    }
    if (!scalar_cast(&value, (*bindings)[index].value.value.type)) {
        fail(a, node, "assignment changes the binding type");
        return COMPILE_FAIL;
    }
    (*bindings)[index].value = value;
    return COMPILE_NEXT;
}
static COMPILE_NOINLINE compile_control compile_control_flow(
    analyzer *a,
    TSNode node,
    compile_binding **bindings,
    compile_value *out,
    int *budget
)
{
    bool loop = script_analyzer_is(node, "while_statement");
    do {
        compile_value condition;
        bool test;
        TSNode expression = script_analyzer_field(node, "condition");
        if (!script_analyzer_compile_expression(a, expression, *bindings, &condition, budget) ||
            !truth(a, expression, condition, &test)) {
            return COMPILE_FAIL;
        }
        TSNode body = script_analyzer_field(
            node,
            loop   ? "body"
            : test ? "consequence"
                   : "alternative"
        );
        if (loop && !test) {
            return COMPILE_NEXT;
        }
        if (ts_node_is_null(body)) {
            return COMPILE_NEXT;
        }
        compile_control result = statement(a, body, bindings, out, budget);
        if (result != COMPILE_NEXT) {
            return result;
        }
    } while (loop);
    return COMPILE_NEXT;
}
static COMPILE_NOINLINE compile_control compile_expression_statement(
    analyzer *a,
    TSNode node,
    compile_binding **bindings,
    compile_value *out,
    int *budget
)
{
    (void)out;
    TSNode expression = ts_node_named_child(node, 0);
    compile_value ignored;
    return script_analyzer_compile_expression(a, expression, *bindings, &ignored, budget)
             ? COMPILE_NEXT
             : COMPILE_FAIL;
}
static compile_control statement_inner(analyzer *, TSNode, compile_binding **, compile_value *, int *);
static compile_control statement(
    analyzer *a,
    TSNode node,
    compile_binding **bindings,
    compile_value *out,
    int *budget
)
{
    if (a->compile_statement_depth >= SCRIPT_MAX_DEPTH) {
        fail(a, node, "statement nesting exceeds the language depth limit");
        return COMPILE_FAIL;
    }
    a->compile_statement_depth++;
    compile_control result = statement_inner(a, node, bindings, out, budget);
    a->compile_statement_depth--;
    return result;
}
static compile_control compile_block(
    analyzer *a,
    TSNode node,
    compile_binding **bindings,
    compile_value *out,
    int *budget
)
{

    int start = acount(*bindings);
    compile_control result = COMPILE_NEXT;
    for (uint32_t i = 0; i < script_analyzer_named_count(node) && result == COMPILE_NEXT;
         i++) {
        TSNode child = ts_node_named_child(node, i);
        if (!ts_node_is_extra(child)) {
            result = statement(a, child, bindings, out, budget);
        }
    }
    for (int i = start; i < acount(*bindings); i++) {
        free((*bindings)[i].name);
    }
    asetlen(*bindings, start);
    while (acount(a->compile_readonly) && alast(a->compile_readonly) >= start) {
        asetlen(a->compile_readonly, acount(a->compile_readonly) - 1);
    }
    return result;
}
static compile_control compile_return(
    analyzer *a,
    TSNode node,
    compile_binding **bindings,
    compile_value *out,
    int *budget
)
{

    TSNode value = script_analyzer_field(node, "value");
    if (script_analyzer_is(value, "argument_list") &&
        script_analyzer_named_count(value) == 1) {
        value = ts_node_named_child(value, 0);
    }
    return script_analyzer_compile_as(a, value, *bindings, a->compile_result, out, budget)
             ? COMPILE_RETURN
             : COMPILE_FAIL;
}
typedef compile_control (*compile_statement_handler)(
    analyzer *a,
    TSNode node,
    compile_binding **bindings,
    compile_value *out,
    int *budget
);
static compile_control statement_inner(
    analyzer *a,
    TSNode node,
    compile_binding **bindings,
    compile_value *out,
    int *budget
)
{
    if (ts_node_is_null(node) || !charge(a, node, budget)) {
        return COMPILE_FAIL;
    }
    const char *kind = ts_node_type(node);
    int index = (!strcmp(kind, "block") || !strcmp(kind, "callable_body")) ? 0
              : !strcmp(kind, "return_statement")                          ? 1
              : (!strcmp(kind, "local_declaration") ||
                 !strcmp(kind, "constant_declaration"))
                  ? 2
              : !strcmp(kind, "assignment") ? 3
              : (!strcmp(kind, "if_statement") || !strcmp(kind, "while_statement"))
                  ? 4
              : !strcmp(kind, "expression_statement") ? 5
                                                      : -1;
    if (index < 0) {
        fail(a, node, "statement cannot execute in a constant context");
        return COMPILE_FAIL;
    }
    static compile_statement_handler const volatile handlers[] = {
        compile_block,      compile_return,       compile_local,
        compile_assignment, compile_control_flow, compile_expression_statement
    };
    return handlers[index](a, node, bindings, out, budget);
}

bool script_analyzer_compile_procedure(
    analyzer *a,
    TSNode declaration,
    int scope,
    compile_binding *arguments,
    compile_value *out,
    int *budget
)
{
    compile_binding *bindings = NULL;
    for (int i = 0; i < acount(arguments); i++) {
        apush(
            bindings,
            ((compile_binding){ xstrdup(arguments[i].name), arguments[i].value })
        );
    }
    int old_scope = a->scope;
    int *old_readonly = a->compile_readonly;
    a->compile_readonly = NULL;
    TSNode parameters = script_analyzer_field(declaration, "parameters");
    for (uint32_t i = 0; i < script_analyzer_named_count(parameters); i++) {
        TSNode parameter = ts_node_named_child(parameters, i);
        if (ts_node_is_extra(parameter) ||
            ts_node_is_null(script_analyzer_field(parameter, "comptime"))) {
            continue;
        }
        char *name = script_analyzer_name_copy(a, script_analyzer_field(parameter, "name"));
        int index = find(bindings, name);
        free(name);
        if (index >= 0) {
            apush(a->compile_readonly, index);
        }
    }
    a->scope = scope;
    pin_type old_result = a->compile_result;
    TSNode result_type = script_analyzer_field(declaration, "results");
    script_type expected_result = script_builtin_type(
        script_analyzer_text_is(a, result_type, "Type") ? "" : "number"
    );
    if (!ts_node_is_null(result_type) &&
        !script_analyzer_text_is(a, result_type, "Type")) {
        char *name = script_analyzer_text_copy(a, result_type);
        int argument = find(bindings, name);
        free(name);
        expected_result = argument >= 0 && bindings[argument].value.is_type
                            ? bindings[argument].value.type
                            : script_analyzer_type_node(a, result_type);
    }
    a->compile_result = expected_result.storage;
    bool valid = statement(a, script_analyzer_field(declaration, "body"), &bindings, out, budget) ==
                 COMPILE_RETURN;
    a->compile_result = old_result;
    if (valid && !ts_node_is_null(result_type)) {
        if (script_analyzer_text_is(a, result_type, "Type")) {
            valid = out->is_type;
        } else {
            valid = scalar_cast(out, expected_result.storage);
        }
    }
    a->scope = old_scope;
    afree(a->compile_readonly);
    a->compile_readonly = old_readonly;
    for (int i = 0; i < acount(bindings); i++) {
        free(bindings[i].name);
    }
    afree(bindings);
    return valid;
}

typedef struct
{
    TSNode declaration, parameters, arguments, argument, parameter, label,
        candidate, value, type;
    script_type expected;
    compile_value evaluated;
} compile_call_frame;
static bool compile_call(analyzer *, TSNode, compile_binding *, compile_value *, int *, compile_call_frame *);
bool script_analyzer_compile_call(
    analyzer *a,
    TSNode node,
    compile_binding *bindings,
    compile_value *out,
    int *budget
)
{
    compile_call_frame *frame = xcalloc(1, sizeof *frame);
    bool valid = compile_call(a, node, bindings, out, budget, frame);
    free(frame);
    return valid;
}
static COMPILE_NOINLINE bool compile_call(
    analyzer *a,
    TSNode call,
    compile_binding *outer,
    compile_value *out,
    int *budget,
    compile_call_frame *frame
)
{
    if (++a->compile_depth > 128) {
        a->compile_depth--;
        return fail(a, call, "call depth exceeded");
    }
    if (script_analyzer_generic_import_compile(a, call, outer, out, budget)) {
        a->compile_depth--;
        return true;
    }
    if (script_analyzer_generic_apply(a, call, outer, out, budget)) {
        a->compile_depth--;
        return true;
    }
    char *name = script_analyzer_qualified_name(a, script_analyzer_field(call, "function"));
    int symbol = script_analyzer_callable_symbol(a, name);
    frame->declaration = (TSNode){ 0 };
    int declaration_scope = a->scope;
    for (int i = 0; i < acount(a->templates); i++) {
        if (a->templates[i].symbol == symbol && !a->templates[i].record) {
            frame->declaration = a->templates[i].node;
            declaration_scope = a->templates[i].scope;
            break;
        }
    }
    if (ts_node_is_null(frame->declaration) && symbol >= 0) {
        int function = a->out->symbols[symbol].function;
        for (int i = 0; i < acount(a->bodies); i++) {
            if (a->bodies[i].function == function) {
                frame->declaration = ts_node_parent(a->bodies[i].node);
                declaration_scope = a->out->functions[function].scope;
                break;
            }
        }
    }
    free(name);
    bool valid = !ts_node_is_null(frame->declaration);
    compile_binding *bindings = NULL;
    frame->parameters = script_analyzer_field(frame->declaration, "parameters");
    frame->arguments = script_analyzer_field(call, "arguments");
    int ordinal = 0;
    for (uint32_t i = 0; valid && i < script_analyzer_named_count(frame->arguments);
         i++) {
        frame->argument = ts_node_named_child(frame->arguments, i);
        if (ts_node_is_extra(frame->argument)) {
            continue;
        }
        frame->parameter = (TSNode){ 0 };
        frame->label = script_analyzer_field(frame->argument, "name");
        int position = ordinal++;
        char *argument_name = ts_node_is_null(frame->label)
                                ? NULL
                                : script_analyzer_name_copy(a, frame->label);
        for (uint32_t j = 0; j < script_analyzer_named_count(frame->parameters); j++) {
            frame->candidate = ts_node_named_child(frame->parameters, j);
            if (ts_node_is_extra(frame->candidate)) {
                continue;
            }
            char *candidate_name = script_analyzer_name_copy(
                a,
                script_analyzer_field(frame->candidate, "name")
            );
            if (argument_name ? !strcmp(argument_name, candidate_name)
                              : position-- == 0) {
                frame->parameter = frame->candidate;
            }
            free(candidate_name);
        }
        free(argument_name);
        frame->value = ts_node_is_null(frame->label)
                         ? frame->argument
                         : script_analyzer_field(frame->argument, "value");
        if (ts_node_is_null(frame->value)) {
            frame->value = frame->argument;
        }

        frame->type = script_analyzer_field(frame->parameter, "type");
        frame->expected = script_analyzer_text_is(a, frame->type, "Type")
                            ? (script_type){ 0 }
                            : script_analyzer_type_node(a, frame->type);
        valid = !ts_node_is_null(frame->parameter) && script_analyzer_compile_as(
                                                          a,
                                                          frame->value,
                                                          outer,
                                                          frame->expected.storage,
                                                          &frame->evaluated,
                                                          budget
                                                      );
        if (valid) {
            if (script_analyzer_text_is(a, frame->type, "Type")) {
                valid = frame->evaluated.is_type;
            } else {
                frame->expected = script_analyzer_type_node(a, frame->type);
                valid = scalar_cast(&frame->evaluated, frame->expected.storage);
            }
            char *parameter_name = script_analyzer_name_copy(
                a,
                script_analyzer_field(frame->parameter, "name")
            );
            if (find(bindings, parameter_name) >= 0) {
                valid = false;
            }
            apush(bindings, ((compile_binding){ parameter_name, frame->evaluated }));
        }
    }
    int parameter_count = 0;
    for (uint32_t i = 0; i < script_analyzer_named_count(frame->parameters); i++) {
        parameter_count += !ts_node_is_extra(ts_node_named_child(frame->parameters, i));
    }
    if (valid && ordinal != parameter_count) {
        valid = false;
    }
    if (valid) {
        valid = script_analyzer_compile_procedure(a, frame->declaration, declaration_scope, bindings, out, budget);
    }
    for (int i = 0; i < acount(bindings); i++) {
        free(bindings[i].name);
    }
    afree(bindings);
    a->compile_depth--;
    return valid ||
           fail(a, call, "requires a terminating authored procedure and constant arguments");
}
