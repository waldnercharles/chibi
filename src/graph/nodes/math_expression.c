#include "ecs/ecs.h"
#include "graph/authoring/asset_edit.h"
#include "graph/authoring/graph_edit.h"
#include "graph/authoring/local_edit.h"
#include "graph/model/local.h"
#include "graph/model/pin_conv.h"
#include "graph/model/registry.h"
#include "graph/nodes/behavior_expression.h"
#include "graph/nodes/compare_nodes.h"
#include "graph/nodes/conv_nodes.h"
#include "graph/nodes/core_nodes.h"
#include "support/xalloc.h"

#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
    TOKEN_NUMBER = 256,
    TOKEN_NAME,
    TOKEN_EQ,
    TOKEN_NE,
    TOKEN_LE,
    TOKEN_GE,
    TOKEN_AND,
    TOKEN_OR,
    TOKEN_ASSIGN,
    TOKEN_ADD_ASSIGN,
    TOKEN_SUB_ASSIGN,
    TOKEN_MUL_ASSIGN,
    TOKEN_DIV_ASSIGN,
    EXPRESSION_MAX_DEPTH = 128
};

typedef struct
{
    const node_desc *desc;
    const signed char *order; /* node input per argument, or NULL for source order */
    int args[8];
    int count;
    int symbol;
    double number;
    bool boolean;
    bool is_boolean_literal;
    bool member; /* field reads term `base`, a computed value */
    int base;
    char field[256];
    int indices[4];
    int index_count;
} term;

/* One expression language serves every graph. Behavior graphs evaluate
   doubles and reflected structs; shader graphs lower to GLSL-backed nodes over
   float and ShaderVec2..4; field graphs to scalar field nodes. */
typedef enum
{
    DIALECT_BEHAVIOR,
    DIALECT_SHADER,
    DIALECT_FIELD,
    DIALECT_COUNT,
} expression_dialect;

typedef struct
{
    const char *source;
    const char *cursor;
    expression_dialect dialect;
    const char *start;
    int length;
    int token;
    double number;
    int depth;
    term *terms;
    char **symbols;
    behavior_expression_error *error;
    const node *previous;
    int assignment_operator;
    int assignment_left;
    int assignment_right;
} parser;

/* Each function names the node that implements it in behavior, shader and
   field graphs, or NULL where that graph kind lacks it; one spelling, arity
   and argument order hold everywhere. Arguments follow GLSL; `order` maps
   them to node inputs where a node orders its inputs differently. A name may
   repeat when a built-in takes different arguments in another graph kind. */
typedef struct
{
    const char *name;
    int arity;
    const char *nodes[DIALECT_COUNT];
    signed char order[4];
} math_function;

static const math_function functions[] = {
    { .name = "sin", .arity = 1, .nodes = { "Sine", "Shader Sin", "Field Sin" } },
    { .name = "cos", .arity = 1, .nodes = { "Cosine", "Shader Cos", "Field Cos" } },
    { .name = "tan", .arity = 1, .nodes = { "Tangent" } },
    { .name = "asin", .arity = 1, .nodes = { "Arc Sine" } },
    { .name = "acos", .arity = 1, .nodes = { "Arc Cosine" } },
    { .name = "atan", .arity = 1, .nodes = { "Arc Tangent" } },
    { .name = "atan2", .arity = 2, .nodes = { "Atan2" } },
    { .name = "sqrt", .arity = 1, .nodes = { "Square Root", "Shader Sqrt" } },
    { .name = "pow", .arity = 2, .nodes = { "Power" } },
    { .name = "exp", .arity = 1, .nodes = { "Exponential" } },
    { .name = "log", .arity = 1, .nodes = { "Logarithm" } },
    { .name = "ln", .arity = 1, .nodes = { "Logarithm" } },
    { .name = "log10", .arity = 1, .nodes = { "Logarithm 10" } },
    { .name = "floor", .arity = 1, .nodes = { "Floor", "Shader Floor", "Field Floor" } },
    { .name = "ceil", .arity = 1, .nodes = { "Ceiling" } },
    { .name = "round", .arity = 1, .nodes = { "Round" } },
    { .name = "fract", .arity = 1, .nodes = { "Fract", "Shader Fract" } },
    { .name = "sign", .arity = 1, .nodes = { "Sign", "Shader Sign" } },
    { .name = "abs", .arity = 1, .nodes = { "Abs", "Shader Abs", "Field Abs" } },
    { .name = "negate", .arity = 1, .nodes = { "Negate", NULL, "Field Negate" } },
    { .name = "min", .arity = 2, .nodes = { "Min", "Shader Min", "Field Min" } },
    { .name = "max", .arity = 2, .nodes = { "Max", "Shader Max", "Field Max" } },
    { .name = "clamp", .arity = 3, .nodes = { "Clamp", "Shader Clamp", "Field Clamp" } },
    { .name = "saturate", .arity = 1, .nodes = { NULL, "Shader Saturate" } },
    { .name = "mix", .arity = 3, .nodes = { NULL, "Shader Mix" } },
    { .name = "step", .arity = 2, .nodes = { NULL, "Shader Step" }, .order = { 1, 0 } },
    { .name = "smoothstep",
      .arity = 3,
      .nodes = { NULL, "Shader Smoothstep" },
      .order = { 1, 2, 0 } },
    { .name = "remap", .arity = 5, .nodes = { NULL, NULL, "Field Remap" } },
    { .name = "select", .arity = 3, .nodes = { NULL, NULL, "Field Select" } },
    { .name = "length", .arity = 1, .nodes = { "Vector Length", "Shader Length" } },
    { .name = "dot", .arity = 2, .nodes = { "Vector Dot", "Shader Dot" } },
    { .name = "distance", .arity = 2, .nodes = { "Vector Distance" } },
    { .name = "direction", .arity = 1, .nodes = { "Direction From Angle" } },
    { .name = "direction", .arity = 2, .nodes = { "Direction To" } },
    { .name = "normalize", .arity = 1, .nodes = { "Normalize Vector 2", "Shader Normalize" } },
    { .name = "rand", .arity = 0, .nodes = { "Random Real" } },
    { .name = "vec2", .arity = 2, .nodes = { "Make_vec2", "Shader Vector2" } },
    { .name = "vec3", .arity = 3, .nodes = { "Make_vec3", "Shader Vector3" } },
    { .name = "vec4", .arity = 4, .nodes = { NULL, "Shader Vector4" } },
    { .name = "noise", .arity = 2, .nodes = { NULL, "Shader Noise" } },
    { .name = "noise", .arity = 3, .nodes = { NULL, NULL, "Field Noise" } },
    { .name = "hash", .arity = 3, .nodes = { NULL, NULL, "Field Hash" } },
    { .name = "fractal", .arity = 6, .nodes = { NULL, NULL, "Field Fractal" } },
    { .name = "circle", .arity = 3, .nodes = { NULL, NULL, "Field Circle" } },
    { .name = "box", .arity = 4, .nodes = { NULL, NULL, "Field Box" } },
    { .name = "capsule", .arity = 7, .nodes = { NULL, NULL, "Field Capsule" } },
    { .name = "rotate_x", .arity = 3, .nodes = { NULL, NULL, "Field Rotate X" } },
    { .name = "rotate_y", .arity = 3, .nodes = { NULL, NULL, "Field Rotate Y" } },
    { .name = "smooth_union", .arity = 3, .nodes = { NULL, NULL, "Field Smooth Union" } },
    { .name = "smooth_subtract", .arity = 3, .nodes = { NULL, NULL, "Field Smooth Subtract" } },
};

/* Infix operators by binding power; higher binds tighter. */
typedef struct
{
    int token;
    const char *symbol;
    int binding;
    const char *nodes[DIALECT_COUNT];
} math_operator;

static const math_operator operators[] = {
    { TOKEN_OR, "||", 1, { "Or" } },
    { TOKEN_AND, "&&", 2, { "And" } },
    { TOKEN_EQ, "==", 3, { "Equal" } },
    { TOKEN_NE, "!=", 3, { "Not Equal" } },
    { '<', "<", 4, { "Less" } },
    { TOKEN_LE, "<=", 4, { "Less Or Equal" } },
    { '>', ">", 4, { "Greater" } },
    { TOKEN_GE, ">=", 4, { "Greater Or Equal" } },
    { '+', "+", 10, { "Add", "Shader Add", "Field Add" } },
    { '-', "-", 10, { "Subtract", "Shader Subtract", "Field Subtract" } },
    { '*', "*", 20, { "Multiply", "Shader Multiply", "Field Multiply" } },
    { '/', "/", 20, { "Divide", "Shader Divide", "Field Divide" } },
    { '%', "%", 20, { "Modulo" } },
    { '^', "^", 30, { "Power" } },
};

static bool implements(const char *const nodes[DIALECT_COUNT], const char *name)
{
    for (int d = 0; d < DIALECT_COUNT; d++) {
        if (nodes[d] && !strcmp(nodes[d], name)) {
            return true;
        }
    }
    return false;
}

static const math_function *function_of(const node_desc *desc)
{
    for (size_t i = 0; i < sizeof functions / sizeof functions[0]; i++) {
        if (implements(functions[i].nodes, desc->name)) {
            return &functions[i];
        }
    }
    return NULL;
}

/* A library holds functions of any domain, so its graph speaks the dialect
   of the nodes it holds. */
static expression_dialect dialect_of(const behavior_asset *asset, const graph *caller)
{
    switch (asset->domain) {
        case BEHAVIOR_DOMAIN_MATERIAL:
        case BEHAVIOR_DOMAIN_SHAPE:
        case BEHAVIOR_DOMAIN_MODEL:
        case BEHAVIOR_DOMAIN_SHADER_LIBRARY: return DIALECT_SHADER;
        case BEHAVIOR_DOMAIN_FIELD: return DIALECT_FIELD;
        default: break;
    }
    for (int i = 0;
         asset->domain == BEHAVIOR_DOMAIN_LIBRARY && i < acount(caller->nodes);
         i++) {
        const char *name = caller->nodes[i]->desc->name;
        if (!strncmp(name, "Field ", 6)) {
            return DIALECT_FIELD;
        }
        if (!strncmp(name, "Shader ", 7)) {
            return DIALECT_SHADER;
        }
    }
    return DIALECT_BEHAVIOR;
}

static const char *dialect_name(expression_dialect dialect)
{
    static const char *const NAMES[DIALECT_COUNT] = { "behavior", "shader", "field" };
    return NAMES[dialect];
}

const char *behavior_expression_spelling(const node_desc *desc, int *binding)
{
    const char *name = desc->name;
    if (!strncmp(name, "Integer ", 8)) {
        name += 8;
    }
    *binding = BEHAVIOR_EXPRESSION_PREFIX_BINDING;
    if (!strcmp(name, "Not")) {
        return "!";
    }
    if (!strcmp(name, "Negate") || !strcmp(name, "Field Negate")) {
        return "-";
    }
    for (size_t i = 0; i < sizeof operators / sizeof operators[0]; i++) {
        if (implements(operators[i].nodes, name)) {
            *binding = operators[i].binding;
            return operators[i].symbol;
        }
    }
    *binding = 0;
    const math_function *function = function_of(desc);
    return function ? function->name : NULL;
}

int behavior_expression_arity(const node_desc *desc)
{
    const math_function *function = function_of(desc);
    return function ? function->arity : -1;
}

const char *behavior_expression_alias(const node_desc *desc, int index)
{
    for (size_t i = 0; i < sizeof functions / sizeof functions[0]; i++) {
        if (implements(functions[i].nodes, desc->name) && !index--) {
            return functions[i].name;
        }
    }
    return NULL;
}

int behavior_expression_argument_input(const node_desc *desc, int argument)
{
    const math_function *function = function_of(desc);
    bool ordered = function && (function->order[0] || function->order[1]);
    return ordered ? function->order[argument] : argument;
}

static int fail(parser *p, const char *message)
{
    if (!p->error->message[0]) {
        p->error->offset = (int)(p->start - p->source);
        p->error->length = p->length ? p->length : 1;
        snprintf(p->error->message, sizeof p->error->message, "%s", message);
    }
    return -1;
}

static void next(parser *p)
{
    while (isspace((unsigned char)*p->cursor)) {
        p->cursor++;
    }
    p->start = p->cursor;
    unsigned char c = (unsigned char)*p->cursor;
    p->token = c;
    if (isdigit(c) || (c == '.' && isdigit((unsigned char)p->cursor[1]))) {
        char *end;
        p->number = strtod(p->cursor, &end);
        p->cursor = end;
        p->token = TOKEN_NUMBER;
    } else if (isalpha(c) || c == '_') {
        do {
            p->cursor++;
        } while (isalnum((unsigned char)*p->cursor) || *p->cursor == '_');
        p->token = TOKEN_NAME;
    } else if (c) {
        p->cursor++;
        switch (c) {
            case '*':
                if (*p->cursor == '*') {
                    p->cursor++;
                    p->token = '^';
                } else if (*p->cursor == '=') {
                    p->cursor++;
                    p->token = TOKEN_MUL_ASSIGN;
                }
                break;
            case '+':
                if (*p->cursor == '=') {
                    p->cursor++;
                    p->token = TOKEN_ADD_ASSIGN;
                }
                break;
            case '-':
                if (*p->cursor == '=') {
                    p->cursor++;
                    p->token = TOKEN_SUB_ASSIGN;
                }
                break;
            case '/':
                if (*p->cursor == '=') {
                    p->cursor++;
                    p->token = TOKEN_DIV_ASSIGN;
                }
                break;
            case '=':
                if (*p->cursor == '=') {
                    p->cursor++;
                    p->token = TOKEN_EQ;
                } else {
                    p->token = TOKEN_ASSIGN;
                }
                break;
            case '!':
                if (*p->cursor == '=') {
                    p->cursor++;
                    p->token = TOKEN_NE;
                }
                break;
            case '<':
                if (*p->cursor == '=') {
                    p->cursor++;
                    p->token = TOKEN_LE;
                }
                break;
            case '>':
                if (*p->cursor == '=') {
                    p->cursor++;
                    p->token = TOKEN_GE;
                }
                break;
            case '&':
                if (*p->cursor == '&') {
                    p->cursor++;
                    p->token = TOKEN_AND;
                }
                break;
            case '|':
                if (*p->cursor == '|') {
                    p->cursor++;
                    p->token = TOKEN_OR;
                }
                break;
            default: break;
        }
    }
    p->length = (int)(p->cursor - p->start);
}

static bool named(const char *name, const char *text, int length)
{
    return strlen(name) == (size_t)length && !memcmp(name, text, (size_t)length);
}

static int append(parser *p, term value)
{
    apush(p->terms, value);
    return acount(p->terms) - 1;
}

static int expression(parser *p, int precedence);

/* Member reads of a computed value: normalize(v).x, (a + b).xy. */
static int members(parser *p, int value)
{
    while (value >= 0 && p->token == '.') {
        next(p);
        if (p->token != TOKEN_NAME) {
            return fail(p, "Expected a field name after '.'");
        }
        term read = { .symbol = -1, .member = true, .base = value };
        snprintf(read.field, sizeof read.field, "%.*s", p->length, p->start);
        next(p);
        value = append(p, read);
    }
    return value;
}

static int identifier(parser *p)
{
    const char *name = p->start;
    int length = p->length;
    next(p);
    if (p->token != '(') {
        if (named("pi", name, length) || named("e", name, length)) {
            return append(p, (term){ .symbol = -1, .number = *name == 'p' ? acos(-1) : exp(1) });
        }
        if (named("true", name, length) || named("false", name, length)) {
            return append(
                p,
                (term){ .symbol = -1,
                        .boolean = named("true", name, length),
                        .is_boolean_literal = true }
            );
        }
        int symbol = 0;
        for (; symbol < acount(p->symbols); symbol++) {
            if (named(p->symbols[symbol], name, length)) {
                break;
            }
        }
        if (symbol == acount(p->symbols)) {
            char *copy = xmalloc((size_t)length + 1);
            memcpy(copy, name, (size_t)length);
            copy[length] = 0;
            apush(p->symbols, copy);
        }
        term value = { .symbol = symbol };
        while (p->token == '.' || p->token == '[') {
            if (p->token == '.') {
                next(p);
                if (p->token != TOKEN_NAME) {
                    return fail(p, "Expected a field name after '.'");
                }
                size_t used = strlen(value.field);
                if (used && used + 1 >= sizeof value.field) {
                    return fail(p, "Field path is too long");
                }
                if (used) {
                    value.field[used++] = '.';
                }
                if (used + (size_t)p->length >= sizeof value.field) {
                    return fail(p, "Field path is too long");
                }
                memcpy(value.field + used, p->start, (size_t)p->length);
                value.field[used + (size_t)p->length] = 0;
                next(p);
                continue;
            }
            if (value.index_count ==
                (int)(sizeof value.indices / sizeof value.indices[0])) {
                return fail(p, "Too many array indices");
            }
            next(p);
            int index = expression(p, 0);
            if (index < 0) {
                return -1;
            }
            if (p->token != ']') {
                return fail(p, "Expected ']' after array index");
            }
            next(p);
            value.indices[value.index_count++] = index;
        }
        return append(p, value);
    }
    const math_function *function = NULL;
    bool known = false;
    for (size_t i = 0; i < sizeof functions / sizeof functions[0] && !function; i++) {
        known |= named(functions[i].name, name, length);
        function = named(functions[i].name, name, length) &&
                           functions[i].nodes[p->dialect]
                     ? &functions[i]
                     : NULL;
    }
    if (!function) {
        char message[96];
        snprintf(
            message,
            sizeof message,
            "%.*s() is not available in %s graphs",
            length,
            name,
            dialect_name(p->dialect)
        );
        p->start = name;
        p->length = length;
        return fail(p, known ? message : "Unknown math function");
    }
    term value = { .desc = registry_node_by_name(function->nodes[p->dialect]),
                   .order = function->order[0] || function->order[1] ? function->order
                                                                     : NULL,
                   .symbol = -1 };
    assert(value.desc);
    /* Shader vectors pack like GLSL's: vec3(uv, 0) fills three components
       from a vec2 and a scalar. */
    bool packs = p->dialect == DIALECT_SHADER && !strncmp(function->name, "vec", 3);
    char arity[96];
    snprintf(
        arity,
        sizeof arity,
        "%s() takes %d arguments",
        function->name,
        function->arity
    );
    next(p);
    while (p->token != ')') {
        if (value.count == function->arity) {
            return fail(p, arity);
        }
        int argument = expression(p, 0);
        if (argument < 0) {
            return -1;
        }
        value.args[value.count++] = argument;
        if (p->token != ',') {
            break;
        }
        next(p);
        if (p->token == ')') {
            return fail(p, "Expected an expression after ','");
        }
    }
    if (value.count != function->arity && !(packs && value.count)) {
        return fail(p, arity);
    }
    if (p->token != ')') {
        return fail(p, "Expected ')' after function arguments");
    }
    next(p);
    return members(p, append(p, value));
}

/* Shader and field graphs fold a negated literal; otherwise fields negate and
   shaders scale by -1. */
static int negate_dataflow(parser *p, int argument)
{
    term *value = &p->terms[argument];
    if (!value->desc && value->symbol < 0 && !value->is_boolean_literal &&
        !value->member) {
        value->number = -value->number;
        return argument;
    }
    if (p->dialect == DIALECT_FIELD) {
        return append(
            p,
            (term){ .desc = registry_node_by_name("Field Negate"),
                    .args = { argument },
                    .count = 1,
                    .symbol = -1 }
        );
    }
    int minus_one = append(p, (term){ .symbol = -1, .number = -1 });
    return append(
        p,
        (term){ .desc = registry_node_by_name("Shader Multiply"),
                .args = { argument, minus_one },
                .count = 2,
                .symbol = -1 }
    );
}

static int primary(parser *p)
{
    if (p->token == TOKEN_NUMBER) {
        double number = p->number;
        if (!isfinite(number)) {
            return fail(p, "Numeric literal is outside the finite real range");
        }
        next(p);
        return append(p, (term){ .symbol = -1, .number = number });
    }
    if (p->token == TOKEN_NAME) {
        return identifier(p);
    }
    if (p->token == '(') {
        next(p);
        int result = expression(p, 0);
        if (result < 0) {
            return -1;
        }
        if (p->token != ')') {
            return fail(p, "Expected ')' to close expression");
        }
        next(p);
        return members(p, result);
    }
    if (p->token == '+' || p->token == '-' || p->token == '!') {
        if (p->token == '!' && p->dialect != DIALECT_BEHAVIOR) {
            return fail(p, "Logical operators need a behavior graph");
        }
        if (p->token == '!') {
            next(p);
            int argument = expression(p, BEHAVIOR_EXPRESSION_PREFIX_BINDING);
            if (argument < 0) {
                return -1;
            }
            return append(
                p,
                (term){ .desc = registry_node_by_name("Not"),
                        .args = { argument },
                        .count = 1,
                        .symbol = -1 }
            );
        }
        bool negative = p->token == '-';
        next(p);
        int argument = expression(p, BEHAVIOR_EXPRESSION_PREFIX_BINDING);
        if (argument < 0 || !negative) {
            return argument;
        }
        if (p->dialect != DIALECT_BEHAVIOR) {
            return negate_dataflow(p, argument);
        }
        return append(
            p,
            (term){ .desc = registry_node_by_name("Negate"),
                    .args = { argument },
                    .count = 1,
                    .symbol = -1 }
        );
    }
    return fail(p, "Expected a number, input name, function, or '(' ");
}

static int expression(parser *p, int precedence)
{
    if (++p->depth > EXPRESSION_MAX_DEPTH) {
        p->depth--;
        return fail(p, "Expression nesting is too deep");
    }
    int left = primary(p);
    while (left >= 0) {
        const math_operator *operation = NULL;
        for (size_t i = 0; i < sizeof operators / sizeof operators[0]; i++) {
            if (operators[i].token == p->token) {
                operation = &operators[i];
            }
        }
        int binding = operation ? operation->binding : -1;
        if (binding < precedence) {
            break;
        }
        const char *node_name = operation->nodes[p->dialect];
        if (!node_name) {
            char message[96];
            snprintf(
                message,
                sizeof message,
                "'%s' needs a behavior graph; %s expressions use + - * /",
                operation->symbol,
                dialect_name(p->dialect)
            );
            left = fail(p, message);
            break;
        }
        bool right_associative = p->token == '^';
        next(p);
        int right = expression(p, binding + !right_associative);
        if (right < 0) {
            left = -1;
            break;
        }
        left = append(
            p,
            (term){ .desc = registry_node_by_name(node_name),
                    .args = { left, right },
                    .count = 2,
                    .symbol = -1 }
        );
    }
    p->depth--;
    return left;
}

static void parser_free(parser *p)
{
    for (int i = 0; i < acount(p->symbols); i++) {
        free(p->symbols[i]);
    }
    afree(p->symbols);
    afree(p->terms);
}

static const char *expression_pin_name(const char *name)
{
    return name;
}

static pin_spec generic_pin(const char *name, int group)
{
    return (pin_spec){ .name = expression_pin_name(name),
                       .type = PIN_F64,
                       .type_name = "f64",
                       .kind = PIN_DATA,
                       .flags = PIN_FLAG_WILDCARD,
                       .group = group,
                       .default_value = { .type = PIN_F64 } };
}

typedef struct
{
    const char *from;
    const char *to;
} expression_rename;

static void free_symbols(char **symbols)
{
    for (int i = 0; i < acount(symbols); i++) {
        free(symbols[i]);
    }
    afree(symbols);
}

static bool assignment_token(int token)
{
    return token == TOKEN_ASSIGN || token == TOKEN_ADD_ASSIGN ||
           token == TOKEN_SUB_ASSIGN || token == TOKEN_MUL_ASSIGN ||
           token == TOKEN_DIV_ASSIGN;
}

static int parse_source(parser *p)
{
    next(p);
    int result = expression(p, 0);
    if (result < 0) {
        return -1;
    }
    if (assignment_token(p->token)) {
        term *left = &p->terms[result];
        bool valid = left->symbol >= 0 && left->symbol < acount(p->symbols);
        if (!valid) {
            return fail(p, "Assignment left side must start with an input name");
        }
        p->assignment_operator = p->token;
        p->assignment_left = result;
        next(p);
        p->assignment_right = expression(p, 0);
        if (p->assignment_right < 0) {
            return -1;
        }
        result = p->assignment_right;
        if (assignment_token(p->token)) {
            return fail(p, "Only one top-level assignment is allowed");
        }
    }
    if (p->token) {
        return fail(p, "Unexpected token after expression");
    }
    return result;
}

static int parse_symbols(
    const behavior_asset *asset,
    const graph *caller,
    const char *source,
    char ***symbols,
    behavior_expression_error *error
)
{
    parser p = { .source = source,
                 .cursor = source,
                 .dialect = dialect_of(asset, caller),
                 .error = error };
    int result = parse_source(&p);
    if (result < 0) {
        parser_free(&p);
        return -1;
    }
    *symbols = p.symbols;
    p.symbols = NULL;
    afree(p.terms);
    return 0;
}

static bool renamed_to(const char *from, const char *to, const expression_rename *renames, int rename_count)
{
    for (int i = 0; i < rename_count; i++) {
        if (!strcmp(renames[i].from, from) && !strcmp(renames[i].to, to)) {
            return true;
        }
    }
    return false;
}

static bool generic_expression_value(const pin_spec *pin)
{
    /* Keep concrete numeric, opaque and aggregate captures. Bool demand and
       wired Bool coercion are handled separately by synchronize_call. */
    return (pin->type >= PIN_I8 && pin->type <= PIN_ENUM) ||
           pin->container >= PIN_CONTAINER_ARRAY;
}

static bool expression_comparison(const node_desc *desc)
{
    return desc == &NODE_Less || desc == &NODE_Less_Or_Equal ||
           desc == &NODE_Greater || desc == &NODE_Greater_Or_Equal ||
           desc == &NODE_Equal || desc == &NODE_Not_Equal;
}

static bool expression_logical(const node_desc *desc)
{
    return desc == &NODE_And || desc == &NODE_Or || desc == &NODE_Not;
}

static bool expression_previous_bool(const node *previous, const char *name);

static bool expression_boolean_term(const parser *p, int index)
{
    const term *value = &p->terms[index];
    return value->is_boolean_literal || expression_logical(value->desc) ||
           expression_comparison(value->desc) ||
           (value->symbol >= 0 && !value->field[0] && !value->index_count &&
            expression_previous_bool(p->previous, p->symbols[value->symbol]));
}

static void expression_require_symbol(
    const parser *p,
    int index,
    pin_type expected,
    pin_type *types,
    bool *known,
    bool *arrays,
    bool *structs,
    bool *invalid
)
{
    const term *value = &p->terms[index];
    if (value->symbol < 0) {
        return;
    }
    if (value->field[0]) {
        structs[value->symbol] = true;
    }
    if (value->index_count) {
        arrays[value->symbol] = true;
    }
    if (value->field[0]) {
        return;
    }
    if (expected != PIN_BOOL && expected != PIN_F64 && expected != PIN_I32) {
        return;
    }
    if (expected == PIN_I32) {
        expected = PIN_F64;
    }
    if (known[value->symbol] && types[value->symbol] != expected) {
        *invalid = true;
        return;
    }
    known[value->symbol] = true;
    types[value->symbol] = expected;
}

static void expression_require_term(
    const parser *p,
    int index,
    pin_type expected,
    pin_type *types,
    bool *known,
    bool *arrays,
    bool *structs,
    bool *invalid
)
{
    const term *value = &p->terms[index];
    if (value->member) {
        expression_require_term(p, value->base, PIN_FLOW, types, known, arrays, structs, invalid);
        return;
    }
    if (value->symbol >= 0) {
        expression_require_symbol(p, index, expected, types, known, arrays, structs, invalid);
        for (int i = 0; i < value->index_count; i++) {
            expression_require_term(p, value->indices[i], PIN_I32, types, known, arrays, structs, invalid);
        }
        return;
    }
    if (value->is_boolean_literal) {
        if (expected == PIN_F64 || expected == PIN_I32) {
            *invalid = true;
        }
        return;
    }
    if (!value->desc) {
        if (expected == PIN_BOOL) {
            *invalid = true;
        }
        return;
    }
    if (expression_comparison(value->desc)) {
        if (expected != PIN_FLOW && expected != PIN_BOOL) {
            *invalid = true;
        }
        bool boolean_equality = (value->desc == &NODE_Equal ||
                                 value->desc == &NODE_Not_Equal) &&
                                (expression_boolean_term(p, value->args[0]) ||
                                 expression_boolean_term(p, value->args[1]));
        for (int i = 0; i < value->count; i++) {
            expression_require_term(
                p,
                value->args[i],
                boolean_equality ? PIN_BOOL : PIN_F64,
                types,
                known,
                arrays,
                structs,
                invalid
            );
        }
        return;
    }
    if (expression_logical(value->desc)) {
        if (expected != PIN_FLOW && expected != PIN_BOOL) {
            *invalid = true;
        }
        for (int i = 0; i < value->count; i++) {
            expression_require_term(p, value->args[i], PIN_BOOL, types, known, arrays, structs, invalid);
        }
        return;
    }
    if (expected == PIN_BOOL) {
        *invalid = true;
    }
    for (int i = 0; i < value->count; i++) {
        expression_require_term(p, value->args[i], PIN_FLOW, types, known, arrays, structs, invalid);
    }
}

static int expression_assignment_symbol(const parser *p)
{
    if (!p->assignment_operator || p->assignment_left < 0 ||
        p->assignment_left >= acount(p->terms)) {
        return -1;
    }
    int symbol = p->terms[p->assignment_left].symbol;
    return symbol >= 0 && symbol < acount(p->symbols) ? symbol : -1;
}

static const ecs_component_desc *expression_component(const char *key)
{
    for (int i = 0; i < registry_node_count(); i++) {
        const node_desc *desc = registry_node(i);
        if (desc->component && desc->component->key && key &&
            !strcmp(desc->component->key, key)) {
            return desc->component;
        }
    }
    return NULL;
}

static const node_desc *expression_component_get(const char *key)
{
    const ecs_component_desc *component = expression_component(key);
    if (!component || !component->name) {
        return NULL;
    }
    char name[128];
    if (snprintf(name, sizeof name, "Get_%s", component->name) >= (int)sizeof name) {
        return NULL;
    }
    return registry_node_by_name(name);
}

static int expression_binding_type(
    const behavior_asset *asset,
    const pin_binding *binding,
    const term *left,
    pin_type *type,
    const char **type_name,
    pin_container *container
)
{
    if (!binding || !type || !type_name || !container) {
        return -1;
    }
    if (binding->kind == PIN_BINDING_VARIABLE) {
        const behavior_variable
            *variable = behavior_asset_find_variable_by_id(asset, binding->variable_id);
        if (!variable) {
            return -1;
        }
        *type = variable->type;
        *type_name = variable->type_name;
        *container = variable->container;
        return 0;
    }
    if (binding->kind != PIN_BINDING_COMPONENT &&
        binding->kind != PIN_BINDING_COMPONENT_FIELD) {
        return -1;
    }
    const ecs_component_desc *component = expression_component(binding->component_key);
    if (!component || !component->type) {
        return -1;
    }
    bool root = binding->kind == PIN_BINDING_COMPONENT || (left && left->field[0]);
    if (root) {
        *type = PIN_STRUCT;
        *type_name = component->type->name;
        *container = PIN_CONTAINER_SINGLE;
        return 0;
    }
    const type_field *field = pin_type_field_path(component->type, binding->field_name);
    if (!field) {
        return -1;
    }
    *type = field->type;
    *type_name = field->type_name;
    *container = PIN_CONTAINER_SINGLE;
    return 0;
}

static bool expression_branch_context(const graph *caller)
{
    if (!caller || caller->dataflow) {
        return false;
    }
    if (caller->kind == GRAPH_EVENT) {
        return true;
    }
    const node *entry = graph_node_by_id(caller, caller->entry_id);
    for (int i = 0; entry && i < entry->output_count; i++) {
        if (entry->output_pins[i].kind == PIN_EXEC) {
            return true;
        }
    }
    return false;
}

/* A condition read only through its Result, such as a While's, is a value
   rather than a branch. */
static bool expression_read_as_value(const graph *caller, const node *previous)
{
    bool read = false;
    for (int i = 0; previous && i < acount(caller->edges); i++) {
        const edge *e = &caller->edges[i];
        bool touches = e->from_id == previous->id || e->to_id == previous->id;
        if (touches && e->kind != GRAPH_EDGE_DATA) {
            return false;
        }
        read |= e->kind == GRAPH_EDGE_DATA && e->from_id == previous->id;
    }
    return read;
}

static bool expression_previous_bool(const node *previous, const char *name)
{
    if (!previous || !name) {
        return false;
    }
    for (int i = 0; i < previous->input_count; i++) {
        const pin_spec *input = &previous->input_pins[i];
        if (input->kind == PIN_DATA && !input->field_source && input->name &&
            !strcmp(input->name, name)) {
            return input->type == PIN_BOOL && input->container == PIN_CONTAINER_SINGLE;
        }
    }
    return false;
}

typedef struct
{
    node *node;
    int pin;
} value_source;

static int find_named_output(const node *n, const char *name)
{
    for (int i = 0; i < n->output_count; i++) {
        if (!n->output_pins[i].field_source && n->output_pins[i].name &&
            !strcmp(n->output_pins[i].name, name)) {
            return i;
        }
    }
    return -1;
}

static int find_named_input(const node *n, const char *name)
{
    for (int i = 0; i < n->input_count; i++) {
        if (n->input_pins[i].name && !strcmp(n->input_pins[i].name, name)) {
            return i;
        }
    }
    return -1;
}

static int expression_assignment_reference(
    graph *caller,
    const node *previous,
    const char *name,
    const expression_rename *renames,
    int rename_count,
    pin_binding *binding
)
{
    if (!previous || !name || !binding) {
        return 0;
    }
    int input = find_named_input(previous, name);
    if (input < 0) {
        for (int i = 0; i < rename_count; i++) {
            if (!strcmp(renames[i].to, name)) {
                input = find_named_input(previous, renames[i].from);
                break;
            }
        }
    }
    if (input < 0) {
        return 0;
    }
    node *source = NULL;
    int source_pin = -1;
    bool has_source = caller &&
                      graph_input_source(caller, previous, input, &source, &source_pin);
    if (previous->input_pins[input].binding && has_source) {
        return -1;
    }
    if (previous->input_pins[input].binding) {
        *binding = *previous->input_pins[input].binding;
        return 1;
    }
    if (!has_source) {
        return 0;
    }
    return graph_resolve_reference(caller, source, source_pin, binding) == 0 ? 1 : -1;
}

static int expression_assignment_input(const node *call, const char *source)
{
    if (!call || !source) {
        return -1;
    }
    behavior_expression_error error = { 0 };
    parser p = { .source = source, .cursor = source, .error = &error };
    int result = parse_source(&p);
    int input = -1;
    if (result >= 0) {
        int symbol = expression_assignment_symbol(&p);
        if (symbol >= 0) {
            input = find_named_input(call, p.symbols[symbol]);
        }
    }
    parser_free(&p);
    return input;
}

static const node_desc *expression_component_set(const char *key, const char *field)
{
    for (int i = 0; i < registry_node_count(); i++) {
        const node_desc *desc = registry_node(i);
        if (!desc->component || !desc->component->key || !key ||
            strcmp(desc->component->key, key) != 0) {
            continue;
        }
        if (field && desc->component_kind == NODE_COMPONENT_SET_FIELD &&
            desc->component_field >= 0 && desc->component->type &&
            desc->component_field < desc->component->type->field_count &&
            desc->component->type->fields[desc->component_field].name &&
            !strcmp(desc->component->type->fields[desc->component_field].name, field)) {
            return desc;
        }
        if (!field && desc->component_kind == NODE_COMPONENT_SET_SELF) {
            return desc;
        }
    }
    return NULL;
}

static int connect_number(graph *g, value_source from, node *to, int pin);

static int expression_replace_struct_field(
    graph *target,
    value_source base,
    const char *path,
    value_source replacement,
    value_source *out,
    behavior_expression_error *error
)
{
    const type_desc *type = graph_output_struct(base.node, base.pin);
    if (!type || !type->name) {
        snprintf(error->message, sizeof error->message, "Assignment target is not a struct");
        return -1;
    }
    const char *end = strchr(path, '.');
    size_t length = end ? (size_t)(end - path) : strlen(path);
    if (!length || length >= 64) {
        snprintf(error->message, sizeof error->message, "Invalid member assignment");
        return -1;
    }
    char segment[64];
    memcpy(segment, path, length);
    segment[length] = 0;
    const node_desc *break_desc = NULL;
    const node_desc *make_desc = NULL;
    char break_name[128];
    char make_name[128];
    if (snprintf(break_name, sizeof break_name, "Break_%s", type->name) >=
            (int)sizeof break_name ||
        snprintf(make_name, sizeof make_name, "Make_%s", type->name) >=
            (int)sizeof make_name) {
        snprintf(error->message, sizeof error->message, "Member assignment type name is too long");
        return -1;
    }
    break_desc = registry_node_by_name(break_name);
    make_desc = registry_node_by_name(make_name);
    if (!break_desc || !make_desc) {
        snprintf(error->message, sizeof error->message, "Member assignment lacks struct nodes");
        return -1;
    }
    node *split = graph_add_node(target, break_desc);
    node *make = graph_add_node(target, make_desc);
    if (!split || !make ||
        graph_connect_data(target, base.node, base.pin, split, 0) != 0) {
        snprintf(error->message, sizeof error->message, "Could not lower member assignment");
        return -1;
    }
    int selected = -1;
    for (int i = 0; i < make->input_count; i++) {
        if (make->input_pins[i].name && !strcmp(make->input_pins[i].name, segment)) {
            selected = i;
            break;
        }
    }
    if (selected < 0) {
        snprintf(error->message, sizeof error->message, "Unknown member '%s'", segment);
        return -1;
    }
    for (int i = 0; i < make->input_count; i++) {
        int split_pin = find_named_output(split, make->input_pins[i].name);
        if (split_pin < 0) {
            snprintf(error->message, sizeof error->message, "Could not lower member assignment");
            return -1;
        }
        value_source value = { split, split_pin };
        if (i == selected) {
            if (end) {
                if (expression_replace_struct_field(target, value, end + 1, replacement, &value, error) !=
                    0) {
                    return -1;
                }
            } else {
                value = replacement;
            }
        }
        if (connect_number(target, value, make, i) != 0) {
            snprintf(error->message, sizeof error->message, "Could not connect member assignment");
            return -1;
        }
    }
    *out = (value_source){ make, 0 };
    return 0;
}

static int expression_field_source(
    graph *target,
    node *entry,
    value_source source,
    const char *path,
    bool input_known,
    value_source *out,
    behavior_expression_error *error
)
{
    char segment[64];
    const char *at = path;
    while (at && *at) {
        const char *end = strchr(at, '.');
        size_t length = end ? (size_t)(end - at) : strlen(at);
        if (!length || length >= sizeof segment) {
            snprintf(error->message, sizeof error->message, "Invalid member path");
            return -1;
        }
        memcpy(segment, at, length);
        segment[length] = 0;
        int field = -1;
        if (source.node == entry) {
            if (entry->output_pins[source.pin].type != PIN_STRUCT) {
                if (input_known) {
                    snprintf(error->message, sizeof error->message, "Member access requires a struct input");
                    return -1;
                }
                *out = source;
                return 0;
            }
            if (graph_expose_output_fields(target, entry, source.pin) != 0) {
                snprintf(error->message, sizeof error->message, "Could not expose fields for this input");
                return -1;
            }
            uint64_t parent = entry->output_pins[source.pin].id;
            for (int i = 0; i < entry->output_count; i++) {
                const pin_spec *pin = &entry->output_pins[i];
                if (pin->field_source == parent && pin->field_name &&
                    !strcmp(pin->field_name, segment)) {
                    field = i;
                    break;
                }
            }
        } else {
            const type_desc *type = graph_output_struct(source.node, source.pin);
            char break_name[128];
            if (!type || !type->name ||
                snprintf(break_name, sizeof break_name, "Break_%s", type->name) >=
                    (int)sizeof break_name) {
                snprintf(error->message, sizeof error->message, "Member is not a struct");
                return -1;
            }
            const node_desc *desc = registry_node_by_name(break_name);
            if (!desc) {
                snprintf(error->message, sizeof error->message, "No member access node for '%s'", type->name);
                return -1;
            }
            node *split = graph_add_node(target, desc);
            if (!split ||
                graph_connect_data(target, source.node, source.pin, split, 0) != 0) {
                snprintf(error->message, sizeof error->message, "Could not lower member access");
                return -1;
            }
            field = find_named_output(split, segment);
            if (field < 0) {
                snprintf(error->message, sizeof error->message, "Unknown member '%s'", segment);
                return -1;
            }
            source = (value_source){ split, field };
        }
        if (field < 0) {
            snprintf(error->message, sizeof error->message, "Unknown field '%s'", segment);
            return -1;
        }
        source = (value_source){ source.node, field };
        at = end ? end + 1 : NULL;
    }
    *out = source;
    return 0;
}

static int expression_integer_index(
    graph *target,
    value_source source,
    value_source *out,
    behavior_expression_error *error
)
{
    const pin_spec *pin = &source.node->output_pins[source.pin];
    if (pin->type == PIN_I32 && pin->container == PIN_CONTAINER_SINGLE) {
        *out = source;
        return 0;
    }
    if ((pin->type != PIN_F32 && pin->type != PIN_F64) ||
        pin->container != PIN_CONTAINER_SINGLE) {
        snprintf(error->message, sizeof error->message, "Array index must be numeric");
        return -1;
    }
    node *convert = graph_add_node(target, registry_node_by_name("To Integer"));
    if (!convert ||
        graph_connect_data(target, source.node, source.pin, convert, 0) != 0) {
        snprintf(error->message, sizeof error->message, "Could not lower array index");
        return -1;
    }
    *out = (value_source){ convert, 0 };
    return 0;
}

/* ---------- Shader and field expressions ---------- */

/* Components in a shader value: 1 for scalars, 2 to 4 for ShaderVec2..4. */
static int dataflow_width(const pin_spec *pin)
{
    if (pin->type == PIN_F32 || pin->type == PIN_F64 || pin->type == PIN_I32) {
        return 1;
    }
    if (pin->type != PIN_STRUCT || !pin->type_name) {
        return 0;
    }
    if (!strcmp(pin->type_name, "ShaderVec2") || !strcmp(pin->type_name, "vec2")) {
        return 2;
    }
    return !strcmp(pin->type_name, "ShaderVec3") ? 3
         : !strcmp(pin->type_name, "ShaderVec4") ? 4
                                                 : 0;
}

/* Types a pin as float or ShaderVec2..4. Generated nodes drop their wildcard
   group so a scalar operand can broadcast against a vector one. */
static void set_width(graph *g, node *n, int output, int index, int width, bool wildcard)
{
    /* Shader and field floats are unnamed, as their nodes declare them. */
    static const char *const TYPES[] = { NULL, NULL, "ShaderVec2", "ShaderVec3", "ShaderVec4" };
    assert(width >= 1 && width <= 4);
    pin_spec *pin = output ? &n->output_pins[index] : &n->input_pins[index];
    if (!wildcard) {
        pin->flags &= ~PIN_FLAG_WILDCARD;
        pin->group = 0;
    }
    int rc = graph_set_pin_type(g, n, output, index, width == 1 ? PIN_F32 : PIN_STRUCT, TYPES[width]);
    assert(rc == 0);
    (void)rc;
}

static int component(char c)
{
    static const char XYZW[] = "xyzw", RGBA[] = "rgba";
    for (int i = 0; i < 4; i++) {
        if (c == XYZW[i] || c == RGBA[i]) {
            return i;
        }
    }
    return -1;
}

/* v.x reads one component; v.zyx builds a vector of several. */
static int swizzle(parser *p, graph *target, value_source *value, int *width, const char *field)
{
    static const char *const AXES[] = { "Shader X", "Shader Y", "Shader Z", "Shader W" };
    static const char *const VECTORS[] = { NULL,
                                           NULL,
                                           "Shader Vector2",
                                           "Shader Vector3",
                                           "Shader Vector4" };
    int count = (int)strlen(field);
    if (count > 4) {
        return fail(p, "Swizzles have at most four components");
    }
    value_source parts[4];
    for (int i = 0; i < count; i++) {
        int axis = component(field[i]);
        if (axis < 0 || axis >= *width) {
            return fail(p, "Unknown vector component");
        }
        node *n = graph_add_node(target, registry_node_by_name(AXES[axis]));
        assert(n);
        set_width(target, n, 0, 0, *width, false);
        if (graph_connect_data(target, value->node, value->pin, n, 0) != 0) {
            return fail(p, "Could not read a vector component");
        }
        parts[i] = (value_source){ n, 0 };
    }
    if (count == 1) {
        *value = parts[0];
        *width = 1;
        return 0;
    }
    node *vector = graph_add_node(target, registry_node_by_name(VECTORS[count]));
    assert(vector);
    for (int i = 0; i < count; i++) {
        if (graph_connect_data(target, parts[i].node, parts[i].pin, vector, i) != 0) {
            return fail(p, "Could not build a swizzle");
        }
    }
    *value = (value_source){ vector, 0 };
    *width = count;
    return 0;
}

/* vecN(...) from scalars and vectors whose components add up to N. */
static int pack_vector(
    parser *p,
    graph *target,
    const term *t,
    value_source *values,
    int *widths,
    int *depths,
    int i,
    bool provisional
)
{
    static const char *const AXES[] = { "Shader X", "Shader Y", "Shader Z", "Shader W" };
    node *vector = graph_add_node(target, t->desc);
    assert(vector);
    int filled = 0;
    depths[i] = 1;
    for (int j = 0; j < t->count; j++) {
        value_source part = values[t->args[j]];
        int width = widths[t->args[j]];
        depths[i] = depths[t->args[j]] + 1 > depths[i] ? depths[t->args[j]] + 1
                                                       : depths[i];
        for (int axis = 0; axis < width; axis++) {
            if (filled == vector->input_count) {
                break;
            }
            value_source component = part;
            if (width > 1) {
                node *read = graph_add_node(target, registry_node_by_name(AXES[axis]));
                assert(read);
                set_width(target, read, 0, 0, width, false);
                if (graph_connect_data(target, part.node, part.pin, read, 0) != 0) {
                    return fail(p, "Could not read a vector component");
                }
                component = (value_source){ read, 0 };
            }
            if (graph_connect_data(target, component.node, component.pin, vector, filled++) !=
                0) {
                return fail(p, "Could not build a vector");
            }
        }
    }
    char message[64];
    snprintf(
        message,
        sizeof message,
        "vec%d() needs %d components",
        vector->input_count,
        vector->input_count
    );
    if (filled != vector->input_count && !provisional) {
        return fail(p, message);
    }
    values[i] = (value_source){ vector, 0 };
    widths[i] = dataflow_width(&vector->output_pins[0]);
    return 0;
}

/* A shader input keeps the width of what feeds its call; field inputs are floats. */
static void type_dataflow_inputs(const parser *p, graph *target, node *entry, const int *symbol_pins)
{
    for (int i = 0; i < acount(p->symbols); i++) {
        int width = p->dialect == DIALECT_SHADER
                      ? dataflow_width(&entry->output_pins[symbol_pins[i]])
                      : 1;
        set_width(target, entry, 1, symbol_pins[i], width ? width : 1, p->dialect == DIALECT_SHADER);
    }
}

/* Lowers every term to the dialect's nodes. An operation takes the widest
   operand's width unless its output has a fixed type (Length, Dot, Vector3). */
static int lower_dataflow(
    parser *p,
    graph *caller,
    const node *previous,
    graph *target,
    node *entry,
    const int *symbol_pins,
    value_source *values,
    int *depths
)
{
    bool shader = p->dialect == DIALECT_SHADER;
    int *widths = xcalloc((size_t)acount(p->terms), sizeof *widths);
    int result = 0;
    /* Before every input is wired, widths are guesses: a short vector waits
       for the connection that regenerates it. */
    bool provisional = !previous;
    for (int s = 0; previous && s < acount(p->symbols); s++) {
        int input = find_named_input(previous, p->symbols[s]);
        node *source = NULL;
        int source_pin = -1;
        provisional |= input < 0 ||
                       (!previous->input_pins[input].binding &&
                        !graph_input_source(caller, previous, input, &source, &source_pin));
    }
    for (int i = 0; i < acount(p->terms) && result >= 0; i++) {
        const term *t = &p->terms[i];
        if (t->symbol >= 0) {
            value_source value = { entry, symbol_pins[t->symbol] };
            int width = dataflow_width(&entry->output_pins[value.pin]);
            if (t->index_count) {
                result = fail(p, "Array access needs a behavior graph");
            } else if (t->field[0] && !shader) {
                result = fail(p, "Field expressions read scalars; members need a shader graph");
            } else if (t->field[0]) {
                /* Until its input is connected, a member read passes the value
                   through; connecting regenerates with the real width. */
                int input = previous
                              ? find_named_input(previous, p->symbols[t->symbol])
                              : -1;
                node *source = NULL;
                int source_pin = -1;
                if (input >= 0 &&
                    graph_input_source(caller, previous, input, &source, &source_pin)) {
                    result = swizzle(p, target, &value, &width, t->field);
                }
            }
            values[i] = value;
            widths[i] = width;
            continue;
        }
        if (t->member) {
            value_source value = values[t->base];
            int width = widths[t->base];
            result = shader
                       ? swizzle(p, target, &value, &width, t->field)
                       : fail(p, "Field expressions read scalars; members need a shader graph");
            values[i] = value;
            widths[i] = width;
            depths[i] = depths[t->base] + 1;
            continue;
        }
        if (t->is_boolean_literal || expression_logical(t->desc) ||
            expression_comparison(t->desc)) {
            result = fail(p, "Boolean logic needs a behavior graph");
            break;
        }
        if (shader && t->desc && t->desc->output_count == 1 &&
            dataflow_width(&t->desc->outputs[0]) > 1 && t->desc->input_count > 1) {
            result = pack_vector(p, target, t, values, widths, depths, i, provisional);
            continue;
        }
        if (!t->desc) {
            node *n = graph_add_node(target, registry_node_by_name(shader ? "Shader Constant" : "Field Constant"));
            assert(n);
            pin_value value = { .type = PIN_F32, .f = (float)t->number };
            if (shader) {
                set_width(target, n, 1, 0, 1, false);
            }
            int rc = graph_set_pin_default(target, n, shader, 0, &value);
            assert(rc == 0);
            (void)rc;
            values[i] = (value_source){ n, 0 };
            widths[i] = 1;
            depths[i] = 1;
            continue;
        }
        node *n = graph_add_node(target, t->desc);
        assert(n);
        int width = 1;
        depths[i] = 1;
        for (int j = 0; j < t->count; j++) {
            width = widths[t->args[j]] > width ? widths[t->args[j]] : width;
            depths[i] = depths[t->args[j]] + 1 > depths[i] ? depths[t->args[j]] + 1
                                                           : depths[i];
        }
        bool elementwise = n->output_pins[0].flags & PIN_FLAG_WILDCARD;
        for (int j = 0; j < t->count && elementwise; j++) {
            int w = widths[t->args[j]];
            if (w != 1 && w != width &&
                (n->input_pins[t->order ? t->order[j] : j].flags & PIN_FLAG_WILDCARD)) {
                char message[128];
                snprintf(
                    message,
                    sizeof message,
                    "%s mixes %d- and %d-component values",
                    t->desc->name,
                    w,
                    width
                );
                result = fail(p, message);
            }
        }
        for (int j = 0; j < t->count && result >= 0; j++) {
            int pin = t->order ? t->order[j] : j;
            value_source from = values[t->args[j]];
            if (n->input_pins[pin].flags & PIN_FLAG_WILDCARD) {
                set_width(target, n, 0, pin, widths[t->args[j]], false);
            }
            if (graph_connect_data(target, from.node, from.pin, n, pin) != 0) {
                char message[160];
                snprintf(
                    message,
                    sizeof message,
                    "%s takes %s for %s",
                    t->desc->name,
                    n->input_pins[pin].type_name ? n->input_pins[pin].type_name : "a scalar",
                    n->input_pins[pin].name
                );
                result = fail(p, message);
            }
        }
        if (n->output_pins[0].flags & PIN_FLAG_WILDCARD) {
            set_width(target, n, 1, 0, width, false);
        }
        n->x = (float)depths[i] * 220;
        n->y = (float)i * 90;
        values[i] = (value_source){ n, 0 };
        widths[i] = dataflow_width(&n->output_pins[0]);
    }
    free(widths);
    return result;
}

/* Generation allocates the function's IDs in one run, starting at first_id. */
static bool scalar_number(const pin_spec *pin)
{
    return pin->container == PIN_CONTAINER_SINGLE && pin->type >= PIN_I8 &&
           pin->type <= PIN_F64;
}

static bool expression_numeric_operand(const pin_spec *pin)
{
    if (pin->container != PIN_CONTAINER_SINGLE) {
        return false;
    }
    if (scalar_number(pin) || pin->type == PIN_BOOL || pin_vector_width(pin)) {
        return true;
    }
    if (pin->type != PIN_STRUCT) {
        return false;
    }
    const type_desc *type = type_catalog_find(pin->type_name);
    if (!type || type->field_count != 1) {
        return false;
    }
    const type_field *field = &type->fields[0];
    pin_spec value = { .type = field->type, .type_name = field->type_name, .kind = PIN_DATA };
    return (scalar_number(&value) || pin_vector_width(&value)) &&
           conversion_for_pins(pin, &value);
}

/* Behavior arithmetic runs in double. A number of another width converts on
   its way in or out, so it neither retypes an operation away from its
   literals nor reaches an int as raw bits. A bool read as a number is 1 or 0. */
static int connect_number(graph *g, value_source from, node *to, int pin)
{
    const pin_spec *out = &from.node->output_pins[from.pin];
    const pin_spec *in = &to->input_pins[pin];
    bool flag = out->type == PIN_BOOL && conversion_for(PIN_BOOL, in->type);
    bool numbers = (scalar_number(out) || flag) && scalar_number(in) &&
                   out->type != in->type;
    bool widened = numbers && in->type == PIN_F64 && (in->flags & PIN_FLAG_WILDCARD);
    const node_desc *conversion = numbers ? conversion_for(out->type, in->type) : NULL;
    unsigned flags = from.node->output_pins[from.pin].flags;
    if (numbers && !conversion && pin_compatible(out, in)) {
        from.node->output_pins[from.pin].flags &= ~PIN_FLAG_WILDCARD;
    }
    int rc = graph_connect_data_conversion(g, from.node, from.pin, to, pin, conversion);
    from.node->output_pins[from.pin].flags = flags;
    /* A float shares the Real category, so it connects as is; the operation
       stays double. */
    return rc || !widened || conversion
             ? rc
             : graph_set_pin_type(g, to, 0, pin, PIN_F64, "f64");
}

/* A double result reaching an integer or float input, such as an int field
   it assigns, converts there instead of passing raw bits. */
static void convert_numbers(graph *g)
{
    for (int i = 0; i < acount(g->edges); i++) {
        const edge e = g->edges[i];
        node *from = graph_node_by_id(g, e.from_id);
        node *to = graph_node_by_id(g, e.to_id);
        int from_pin = -1, to_pin = -1;
        const pin_spec *out = e.kind == GRAPH_EDGE_DATA && !e.conversion
                                ? graph_pin_by_id(from, e.from_pin_id, NULL, &from_pin)
                                : NULL;
        const pin_spec *in = out ? graph_pin_by_id(to, e.to_pin_id, NULL, &to_pin)
                                 : NULL;
        if (in && scalar_number(out) && scalar_number(in) && out->type != in->type) {
            int rc = connect_number(g, (value_source){ from, from_pin }, to, to_pin);
            assert(rc == 0);
            (void)rc;
        }
    }
}

static behavior_asset *generate(
    behavior_asset *asset,
    graph *caller,
    const node *previous,
    const char *source,
    behavior_expression_error *error,
    const expression_rename *renames,
    int rename_count,
    uint64_t first_id,
    graph_expression_mode requested_mode
)
{
    parser p = { .source = source,
                 .cursor = source,
                 .dialect = dialect_of(asset, caller),
                 .error = error,
                 .previous = previous };
    int result = parse_source(&p);
    if (result >= 0 && p.assignment_operator && p.dialect != DIALECT_BEHAVIOR) {
        result = fail(&p, "Assignments need a behavior graph");
    }
    if (result < 0) {
        parser_free(&p);
        return NULL;
    }
    int symbol_count = acount(p.symbols);
    pin_type *symbol_types = xcalloc((size_t)symbol_count, sizeof *symbol_types);
    bool *symbol_known = xcalloc((size_t)symbol_count, sizeof *symbol_known);
    bool *symbol_arrays = xcalloc((size_t)symbol_count, sizeof *symbol_arrays);
    bool *symbol_structs = xcalloc((size_t)symbol_count, sizeof *symbol_structs);
    bool invalid_types = false;
    expression_require_term(&p, result, PIN_FLOW, symbol_types, symbol_known, symbol_arrays, symbol_structs, &invalid_types);
    if (p.assignment_operator) {
        const term *left = &p.terms[p.assignment_left];
        expression_require_term(
            &p,
            p.assignment_right,
            PIN_FLOW,
            symbol_types,
            symbol_known,
            symbol_arrays,
            symbol_structs,
            &invalid_types
        );
        if (left->field[0]) {
            symbol_structs[left->symbol] = true;
        }
        if (left->index_count) {
            symbol_arrays[left->symbol] = true;
        }
    }
    if (invalid_types) {
        fail(&p, "Expression operands have incompatible types");
        free(symbol_structs);
        free(symbol_arrays);
        free(symbol_known);
        free(symbol_types);
        parser_free(&p);
        return NULL;
    }
    const term *result_term = &p.terms[result];
    if (result_term->symbol >= 0 && !result_term->field[0] && !result_term->index_count &&
        expression_previous_bool(previous, p.symbols[result_term->symbol])) {
        symbol_known[result_term->symbol] = true;
        symbol_types[result_term->symbol] = PIN_BOOL;
    }
    bool boolean_result = result_term->is_boolean_literal ||
                          expression_logical(result_term->desc) ||
                          expression_comparison(result_term->desc) ||
                          (result_term->symbol >= 0 && result_term->symbol < symbol_count &&
                           ((symbol_known[result_term->symbol] &&
                             symbol_types[result_term->symbol] == PIN_BOOL) ||
                            expression_previous_bool(
                                previous,
                                p.symbols[result_term->symbol]
                            )));
    graph_expression_mode
        mode = p.assignment_operator
                 ? GRAPH_EXPRESSION_ACTION
                 : (requested_mode == GRAPH_EXPRESSION_VALUE
                        ? GRAPH_EXPRESSION_VALUE
                        : (boolean_result &&
                                   (requested_mode == GRAPH_EXPRESSION_BRANCH ||
                                    (expression_branch_context(caller) &&
                                     !expression_read_as_value(caller, previous)))
                               ? GRAPH_EXPRESSION_BRANCH
                               : GRAPH_EXPRESSION_VALUE));
    behavior_asset *staged = behavior_asset_new("Expression staging");
    staged->graph->id_counter = first_id;
    char name[64];
    snprintf(name, sizeof name, "Behavior Expression %llu", (unsigned long long)first_id);
    graph *target = behavior_asset_add_function(staged, name, mode != GRAPH_EXPRESSION_ACTION);
    assert(target);
    target->math_expression = graph_own_str(target, source, strlen(source));
    target->expression_mode = mode;
    node *entry = graph_node_by_id(target, target->entry_id);
    node *ret = graph_node_by_id(target, target->return_id);
    int *symbol_pins = xmalloc((size_t)symbol_count * sizeof *symbol_pins);
    for (int i = 0; i < symbol_count; i++) {
        symbol_pins[i] = -1;
    }
    /* Keep surviving input order, then append new names in source order. */
    for (int i = 0; previous && i < previous->input_count; i++) {
        const pin_spec *old = &previous->input_pins[i];
        if (old->kind != PIN_DATA || old->field_source) {
            continue;
        }
        for (int j = 0; j < symbol_count; j++) {
            if (!strcmp(old->name, expression_pin_name(p.symbols[j])) ||
                renamed_to(old->name, p.symbols[j], renames, rename_count)) {
                pin_spec pin = generic_pin(p.symbols[j], j + 1);
                symbol_pins[j] = graph_add_pin(target, entry, 1, &pin);
                assert(symbol_pins[j] >= 0);
                if (old->type != PIN_FLOW) {
                    int rc = graph_set_pin_type(target, entry, 1, symbol_pins[j], old->type, old->type_name);
                    assert(rc == 0);
                    (void)rc;
                }
                if (old->container != PIN_CONTAINER_SINGLE) {
                    int rc = graph_set_pin_container(target, entry, 1, symbol_pins[j], old->container);
                    assert(rc == 0);
                    (void)rc;
                }
            }
        }
    }
    for (int i = 0; i < symbol_count; i++) {
        if (symbol_pins[i] < 0) {
            pin_spec pin = generic_pin(p.symbols[i], i + 1);
            symbol_pins[i] = graph_add_pin(target, entry, 1, &pin);
            assert(symbol_pins[i] >= 0);
        }
    }
    int rc = 0;
    int assignment_symbol = expression_assignment_symbol(&p);
    const char *assignment_name = assignment_symbol >= 0
                                    ? p.symbols[assignment_symbol]
                                    : NULL;
    pin_binding assignment_binding_storage = { 0 };
    int assignment_reference = expression_assignment_reference(
        caller,
        previous,
        assignment_name,
        renames,
        rename_count,
        &assignment_binding_storage
    );
    const pin_binding *assignment_binding = assignment_reference > 0
                                              ? &assignment_binding_storage
                                              : NULL;
    if (p.assignment_operator && assignment_reference < 0) {
        fail(&p, "Assignment target must resolve to a writable reference");
        result = -1;
    }
    if (p.assignment_operator && assignment_binding) {
        pin_type type;
        const char *type_name;
        pin_container container;
        if (assignment_symbol < 0 || expression_binding_type(
                                         asset,
                                         assignment_binding,
                                         &p.terms[p.assignment_left],
                                         &type,
                                         &type_name,
                                         &container
                                     ) != 0) {
            fail(&p, "Assignment target binding is invalid");
            result = -1;
        } else {
            rc = graph_set_pin_type(target, entry, 1, symbol_pins[assignment_symbol], type, type_name);
            assert(rc == 0);
            rc = graph_set_pin_container(target, entry, 1, symbol_pins[assignment_symbol], container);
            assert(rc == 0);
            entry->output_pins[symbol_pins[assignment_symbol]].flags &= ~PIN_FLAG_WILDCARD;
        }
    }
    if (result < 0) {
        free(symbol_structs);
        free(symbol_arrays);
        free(symbol_known);
        free(symbol_types);
        free(symbol_pins);
        parser_free(&p);
        behavior_asset_free(staged);
        return NULL;
    }
    for (int i = 0; i < symbol_count && p.dialect == DIALECT_BEHAVIOR; i++) {
        if (symbol_known[i] && !symbol_structs[i]) {
            /* Comparison operands use double arithmetic internally. Keep a
               wired numeric parameter's storage type so refreshing the call
               does not remove its driver; connect_number widens in the body.
               An already boolean capture also keeps its reflected spelling. */
            const pin_spec *input = &entry->output_pins[symbol_pins[i]];
            bool preserve_type = previous && ((symbol_types[i] == PIN_F64 &&
                                               scalar_number(input)) ||
                                              (symbol_types[i] == PIN_BOOL &&
                                               input->type == PIN_BOOL));
            if (!preserve_type) {
                rc = graph_set_pin_type(
                    target,
                    entry,
                    1,
                    symbol_pins[i],
                    symbol_types[i],
                    symbol_types[i] == PIN_BOOL ? "bool" : "f64"
                );
                assert(rc == 0);
            }
            entry->output_pins[symbol_pins[i]].flags &= ~PIN_FLAG_WILDCARD;
        }
        if (symbol_arrays[i]) {
            rc = graph_set_pin_container(target, entry, 1, symbol_pins[i], PIN_CONTAINER_ARRAY);
            assert(rc == 0);
        }
    }
    pin_spec output = generic_pin("Result", symbol_count + 1);
    rc = graph_add_pin(target, ret, 0, &output);
    if (rc < 0) {
        free(symbol_structs);
        free(symbol_arrays);
        free(symbol_known);
        free(symbol_types);
        free(symbol_pins);
        parser_free(&p);
        behavior_asset_free(staged);
        return NULL;
    }
    assert(rc >= 0);
    (void)rc;
    value_source *values = xmalloc((size_t)acount(p.terms) * sizeof *values);
    int *depths = xcalloc((size_t)acount(p.terms), sizeof *depths);
    int max_depth = 0;
    if (p.dialect != DIALECT_BEHAVIOR) {
        type_dataflow_inputs(&p, target, entry, symbol_pins);
        result = lower_dataflow(&p, caller, previous, target, entry, symbol_pins, values, depths) < 0
                   ? -1
                   : result;
        for (int i = 0; i < acount(p.terms); i++) {
            max_depth = depths[i] > max_depth ? depths[i] : max_depth;
        }
    }
    for (int i = 0; i < acount(p.terms) && p.dialect == DIALECT_BEHAVIOR; i++) {
        const term *term = &p.terms[i];
        if (term->symbol >= 0) {
            value_source value = { entry, symbol_pins[term->symbol] };
            if (term->field[0]) {
                bool input_known = false;
                if (previous) {
                    int previous_pin = find_named_input(previous, p.symbols[term->symbol]);
                    node *driver = NULL;
                    int driver_pin = -1;
                    input_known = previous_pin >= 0 &&
                                  (graph_input_source(caller, previous, previous_pin, &driver, &driver_pin) ||
                                   previous->input_pins[previous_pin].binding ||
                                   previous->input_pins[previous_pin]
                                           .default_value.type == PIN_STRUCT);
                }
                if (expression_field_source(target, entry, value, term->field, input_known, &value, error) !=
                    0) {
                    result = -1;
                    break;
                }
            }
            for (int index = 0; index < term->index_count; index++) {
                node *get = graph_add_node(target, registry_node_by_name("Get"));
                if (!get) {
                    result = fail(&p, "Could not lower array access");
                    break;
                }
                const pin_spec *array_pin = &value.node->output_pins[value.pin];
                rc = graph_set_pin_type(
                    target,
                    get,
                    0,
                    0,
                    array_pin->type,
                    array_pin->type_name
                );
                assert(rc == 0);
                (void)rc;
                rc = graph_set_pin_container(target, get, 0, 0, PIN_CONTAINER_ARRAY);
                assert(rc == 0);
                (void)rc;
                rc = graph_connect_data(target, value.node, value.pin, get, 0);
                assert(rc == 0);
                (void)rc;
                value_source index_value = values[term->indices[index]];
                if (expression_integer_index(target, index_value, &index_value, error) !=
                    0) {
                    result = -1;
                    break;
                }
                rc = graph_connect_data(target, index_value.node, index_value.pin, get, 1);
                assert(rc == 0);
                (void)rc;
                const pin_spec *element = &get->output_pins[0];
                if (array_pin->type_name) {
                    rc = graph_set_pin_type(
                        target,
                        get,
                        1,
                        0,
                        array_pin->type,
                        array_pin->type_name
                    );
                    assert(rc == 0);
                    (void)rc;
                }
                value = (value_source){ get, 0 };
                depths[i] = depths[term->indices[index]] + 1;
                (void)element;
            }
            if (result < 0) {
                break;
            }
            values[i] = value;
            continue;
        }
        if (term->member) {
            value_source value = values[term->base];
            if (expression_field_source(target, entry, value, term->field, true, &value, error) !=
                0) {
                result = -1;
                break;
            }
            values[i] = value;
            depths[i] = depths[term->base] + 1;
            continue;
        }
        const node_desc *term_desc = term->is_boolean_literal
                                       ? registry_node_by_name("Bool")
                                       : (term->desc ? term->desc
                                                     : registry_node_by_name("Real"));
        node *n = graph_add_node(target, term_desc);
        assert(n);
        bool logical = term->is_boolean_literal || expression_logical(term->desc);
        bool comparison = expression_comparison(term->desc);
        bool boolean_equality = term->count == 2 &&
                                (term->desc == &NODE_Equal ||
                                 term->desc == &NODE_Not_Equal) &&
                                values[term->args[0]]
                                        .node
                                        ->output_pins[values[term->args[0]].pin]
                                        .type == PIN_BOOL &&
                                values[term->args[1]]
                                        .node
                                        ->output_pins[values[term->args[1]].pin]
                                        .type == PIN_BOOL;
        bool aggregate = term->desc && term->desc->pure_kind == NODE_PURE_MAKE;
        if (!aggregate) {
            /* Numbers become doubles; a vector pin such as Vector Length's
               keeps its declared type. */
            for (int j = 0; j < n->input_count; j++) {
                if (n->input_pins[j].type == PIN_STRUCT &&
                    !(n->input_pins[j].flags & PIN_FLAG_WILDCARD)) {
                    continue;
                }
                rc = graph_set_pin_type(
                    target,
                    n,
                    0,
                    j,
                    logical || boolean_equality ? PIN_BOOL : PIN_F64,
                    logical || boolean_equality ? "bool" : "f64"
                );
                assert(rc == 0);
                (void)rc;
            }
            if (n->output_pins[0].type != PIN_STRUCT ||
                (n->output_pins[0].flags & PIN_FLAG_WILDCARD)) {
                rc = graph_set_pin_type(
                    target,
                    n,
                    1,
                    0,
                    logical || comparison ? PIN_BOOL : PIN_F64,
                    logical || comparison ? "bool" : "f64"
                );
                assert(rc == 0);
                (void)rc;
            }
        }
        if (!term->desc) {
            pin_value value = { .type = PIN_F64, .d = term->number };
            rc = graph_set_pin_default(target, n, 0, 0, &value);
            assert(rc == 0);
            (void)rc;
        } else if (term->is_boolean_literal) {
            pin_value value = { .type = PIN_BOOL, .b = term->boolean };
            rc = graph_set_pin_default(target, n, 0, 0, &value);
            assert(rc == 0);
            (void)rc;
        }

        depths[i] = 1;
        for (int j = 0; j < term->count; j++) {
            value_source input = values[term->args[j]];
            if (!aggregate &&
                !expression_numeric_operand(&input.node->output_pins[input.pin])) {
                char message[160];
                snprintf(
                    message,
                    sizeof message,
                    "%s requires a numeric operand for %s",
                    n->desc->name,
                    n->input_pins[term->order ? term->order[j] : j].name
                );
                result = fail(&p, message);
                break;
            }
            rc = connect_number(target, input, n, term->order ? term->order[j] : j);
            if (rc) {
                result = fail(&p, "Could not connect expression operand");
                break;
            }
            (void)rc;
            int depth = depths[term->args[j]] + 1;
            if (depth > depths[i]) {
                depths[i] = depth;
            }
        }
        if (result < 0) {
            break;
        }
        if (depths[i] > max_depth) {
            max_depth = depths[i];
        }
        n->x = (float)depths[i] * 220;
        n->y = (float)i * 90;
        values[i] = (value_source){ n, 0 };
    }
    if (result < 0) {
        free(depths);
        free(values);
        free(symbol_structs);
        free(symbol_arrays);
        free(symbol_known);
        free(symbol_types);
        free(symbol_pins);
        parser_free(&p);
        behavior_asset_free(staged);
        return NULL;
    }
    value_source final = values[result];
    if (p.assignment_operator != 0 && p.assignment_operator != TOKEN_ASSIGN) {
        const char *operation = p.assignment_operator == TOKEN_ADD_ASSIGN
                                  ? "Add"
                              : p.assignment_operator == TOKEN_SUB_ASSIGN
                                  ? "Subtract"
                              : p.assignment_operator == TOKEN_MUL_ASSIGN
                                  ? "Multiply"
                                  : "Divide";
        const pin_spec *left = &values[p.assignment_left]
                                    .node->output_pins[values[p.assignment_left].pin];
        const pin_spec *right = &values[p.assignment_right]
                                     .node
                                     ->output_pins[values[p.assignment_right].pin];
        node *compound = NULL;
        if (!expression_numeric_operand(left) || !expression_numeric_operand(right)) {
            result = fail(&p, "Compound assignment requires numeric operands");
        } else {
            compound = graph_add_node(target, registry_node_by_name(operation));
        }
        if (result >= 0 && !compound) {
            result = fail(&p, "Could not lower assignment operator");
        }
        if (compound) {
            /* count += 1 on an int adds in double like the rest. */
            pin_type type = scalar_number(left) ? PIN_F64 : left->type;
            const char *type_name = scalar_number(left) ? "f64" : left->type_name;
            for (int pin = 0; pin < compound->input_count; pin++) {
                rc = graph_set_pin_type(target, compound, 0, pin, type, type_name);
                assert(rc == 0);
                (void)rc;
            }
            rc = graph_set_pin_type(target, compound, 1, 0, type, type_name);
            assert(rc == 0);
            (void)rc;
            rc = connect_number(target, values[p.assignment_left], compound, 0);
            assert(rc == 0);
            (void)rc;
            rc = connect_number(target, values[p.assignment_right], compound, 1);
            assert(rc == 0);
            (void)rc;
            final = (value_source){ compound, 0 };
        }
    }
    if (result < 0) {
        free(depths);
        free(values);
        free(symbol_structs);
        free(symbol_arrays);
        free(symbol_known);
        free(symbol_types);
        free(symbol_pins);
        parser_free(&p);
        behavior_asset_free(staged);
        return NULL;
    }

    if (mode == GRAPH_EXPRESSION_ACTION) {
        node *setter = NULL;
        value_source returned = final;
        const term *left = &p.terms[p.assignment_left];
        pin_binding binding_storage = { 0 };
        int reference = expression_assignment_reference(
            caller,
            previous,
            p.symbols[left->symbol],
            renames,
            rename_count,
            &binding_storage
        );
        const pin_binding *binding = reference > 0 ? &binding_storage : NULL;
        if (reference < 0) {
            result = fail(&p, "Assignment target must resolve to a writable reference");
        }
        bool member_write = left->field[0] || left->index_count;
        if (binding && binding->kind == PIN_BINDING_VARIABLE) {
            behavior_variable
                *variable = behavior_asset_find_variable_by_id(asset, binding->variable_id);
            if (!variable) {
                result = fail(&p, "Assignment target variable is missing");
            } else {
                value_source write_value = final;
                if (left->index_count > 1) {
                    result = fail(&p, "Nested array assignment is not supported");
                } else if (left->index_count == 1) {
                    node *set_array = graph_add_node(target, registry_node_by_name("Set Array Elem"));
                    value_source index = values[left->indices[0]];
                    if (!set_array ||
                        expression_integer_index(target, index, &index, error) != 0) {
                        result = fail(&p, "Could not lower array assignment");
                    } else {
                        const pin_spec *root = &entry->output_pins[symbol_pins[left->symbol]];
                        rc = graph_set_pin_type(target, set_array, 0, 0, root->type, root->type_name);
                        assert(rc == 0);
                        rc = graph_set_pin_container(target, set_array, 0, 0, root->container);
                        assert(rc == 0);
                        rc = graph_set_pin_type(
                            target,
                            set_array,
                            0,
                            2,
                            final.node->output_pins[final.pin].type,
                            final.node->output_pins[final.pin].type_name
                        );
                        assert(rc == 0);
                        rc = graph_set_pin_type(target, set_array, 1, 0, root->type, root->type_name);
                        assert(rc == 0);
                        rc = graph_set_pin_container(target, set_array, 1, 0, root->container);
                        assert(rc == 0);
                        if (graph_connect_data(target, entry, symbol_pins[left->symbol], set_array, 0) !=
                                0 ||
                            graph_connect_data(target, index.node, index.pin, set_array, 1) !=
                                0 ||
                            graph_connect_data(target, final.node, final.pin, set_array, 2) !=
                                0) {
                            result = fail(&p, "Could not connect array assignment");
                        } else {
                            write_value = (value_source){ set_array, 0 };
                        }
                    }
                } else if (left->field[0]) {
                    value_source root = { entry, symbol_pins[left->symbol] };
                    if (expression_replace_struct_field(target, root, left->field, final, &write_value, error) !=
                        0) {
                        result = -1;
                    }
                }
                setter = graph_add_node(target, &NODE_SetVariable);
                if (!setter ||
                    graph_set_node_reference(
                        target,
                        setter,
                        variable->id,
                        variable->name
                    ) != 0 ||
                    graph_set_pin_type(
                        target,
                        setter,
                        0,
                        1,
                        variable->type,
                        variable->type_name
                    ) != 0 ||
                    graph_set_pin_container(target, setter, 0, 1, variable->container) != 0 ||
                    graph_set_pin_type(
                        target,
                        setter,
                        1,
                        1,
                        variable->type,
                        variable->type_name
                    ) != 0 ||
                    graph_set_pin_container(target, setter, 1, 1, variable->container) !=
                        0) {
                    result = fail(&p, "Could not lower variable assignment");
                } else if (
                    result >= 0 &&
                    (graph_connect_exec(target, entry, 0, setter, 0, 0) != 0 ||
                     connect_number(target, write_value, setter, 1) != 0)
                ) {
                    result = fail(&p, "Could not connect variable assignment");
                } else if (!member_write && left->index_count == 0 && setter->output_count > 1) {
                    returned = (value_source){ setter, 1 };
                }
            }
        } else if (binding && (binding->kind == PIN_BINDING_COMPONENT || binding->kind == PIN_BINDING_COMPONENT_FIELD)) {
            value_source write_value = final;
            const char *path = left->field[0] ? left->field
                             : binding->kind == PIN_BINDING_COMPONENT_FIELD
                                 ? binding->field_name
                                 : NULL;
            value_source root = { entry, symbol_pins[left->symbol] };
            if (!path && left->index_count) {
                result = fail(&p, "Indexed component assignment is not supported");
            } else if (left->index_count > 0) {
                result = fail(&p, "Indexed component assignment is not supported");
            } else if (binding->kind == PIN_BINDING_COMPONENT_FIELD && !left->field[0]) {
                const node_desc *get_desc = expression_component_get(binding->component_key);
                node *get = get_desc ? graph_add_node(target, get_desc) : NULL;
                if (!get) {
                    result = fail(&p, "Assignment target component is missing");
                } else {
                    root = (value_source){ get, 0 };
                    if (expression_replace_struct_field(target, root, path, final, &write_value, error) !=
                        0) {
                        result = -1;
                    }
                }
            } else if (path && expression_replace_struct_field(target, root, path, final, &write_value, error) != 0) {
                result = -1;
            }
            const node_desc *desc = expression_component_set(binding->component_key, NULL);
            if (!desc) {
                result = fail(&p, "Assignment target component is missing");
            } else {
                setter = graph_add_node(target, desc);
                if (!setter) {
                    result = fail(&p, "Could not lower component assignment");
                } else if (
                    result >= 0 &&
                    (graph_connect_exec(target, entry, 0, setter, 0, 0) != 0 ||
                     connect_number(target, write_value, setter, 1) != 0)
                ) {
                    result = fail(&p, "Could not connect component assignment");
                } else if (
                    !member_write && !left->field[0] && !left->index_count &&
                    binding->kind == PIN_BINDING_COMPONENT && setter->output_count > 1
                ) {
                    returned = (value_source){ setter, 1 };
                }
            }
        }
        if (setter) {
            if (result >= 0 && graph_connect_exec(target, setter, 0, ret, 0, 0) != 0) {
                result = fail(&p, "Could not lower assignment write");
            }
        } else if (result >= 0 && graph_connect_exec(target, entry, 0, ret, 0, 0) != 0) {
            result = fail(&p, "Could not lower assignment flow");
        }
        final = returned;
    }
    int result_pin = find_named_input(ret, "Result");
    if (result < 0 || result_pin < 0) {
        result = fail(&p, "Generated expression has no Result output");
    }
    unsigned result_flags = result_pin >= 0 ? ret->input_pins[result_pin].flags : 0;
    int previous_result = previous ? find_named_output(previous, "Result") : -1;
    bool saved_number = result >= 0 && previous_result >= 0 &&
                        scalar_number(&previous->output_pins[previous_result]) &&
                        scalar_number(&final.node->output_pins[final.pin]);
    pin_type resolved_member_type = PIN_FLOW;
    if (saved_number) {
        const graph *old = behavior_asset_find_graph(asset, previous->reference_id);
        const node *old_entry = old ? graph_node_by_id(old, old->entry_id) : NULL;
        const node *old_return = old ? graph_node_by_id(old, old->return_id) : NULL;
        int old_result = old_return ? find_named_input(old_return, "Result") : -1;
        /* An unresolved member temporarily evaluates its numeric parent.
           Typing that parent as a struct resolves the actual field storage;
           its provisional result must not become a numeric ABI constraint.
           A caller Result override differing from the old Return stays fixed. */
        bool inferred_result = old_result >= 0 &&
                               previous->output_pins[previous_result].type ==
                                   old_return->input_pins[old_result].type;
        for (int i = 0; inferred_result && old_entry && i < acount(p.terms); i++) {
            const term *term = &p.terms[i];
            if (term->symbol < 0 || !term->field[0]) {
                continue;
            }
            int old_input = find_named_output(old_entry, p.symbols[term->symbol]);
            if (old_input >= 0 && old_entry->output_pins[old_input].type != PIN_STRUCT &&
                entry->output_pins[symbol_pins[term->symbol]].type == PIN_STRUCT) {
                if (scalar_number(&values[i].node->output_pins[values[i].pin])) {
                    resolved_member_type = values[i]
                                               .node->output_pins[values[i].pin]
                                               .type;
                }
                saved_number = false;
                break;
            }
        }
    }
    for (int i = 0; resolved_member_type != PIN_FLOW && i < acount(p.terms); i++) {
        const pin_spec *operand = &values[i].node->output_pins[values[i].pin];
        if (p.terms[i].symbol >= 0 && scalar_number(operand) &&
            operand->type != resolved_member_type) {
            resolved_member_type = PIN_FLOW;
            saved_number = true;
        }
    }
    if (saved_number || resolved_member_type != PIN_FLOW) {
        const pin_spec *saved = &previous->output_pins[previous_result];
        pin_type type = resolved_member_type != PIN_FLOW ? resolved_member_type
                                                         : saved->type;
        rc = graph_set_pin_type(
            target,
            ret,
            0,
            result_pin,
            type,
            resolved_member_type != PIN_FLOW ? NULL : saved->type_name
        );
        saved_number = true;
        assert(rc == 0);
        ret->input_pins[result_pin].flags &= ~PIN_FLAG_WILDCARD;
    }
    if (result >= 0 &&
        (saved_number &&
                 !pin_compatible(&final.node->output_pins[final.pin], &ret->input_pins[result_pin])
             ? connect_number(target, final, ret, result_pin)
             : graph_connect_data(target, final.node, final.pin, ret, result_pin)) != 0) {
        result = fail(&p, "Could not connect generated Result");
    }
    if (result < 0) {
        free(depths);
        free(values);
        free(symbol_structs);
        free(symbol_arrays);
        free(symbol_known);
        free(symbol_types);
        free(symbol_pins);
        parser_free(&p);
        behavior_asset_free(staged);
        return NULL;
    }
    convert_numbers(target);
    ret->input_pins[result_pin].flags = result_flags;
    ret->x = (float)(max_depth + 1) * 220;
    ret->y = final.node->y;
    free(depths);
    free(values);
    free(symbol_structs);
    free(symbol_arrays);
    free(symbol_known);
    free(symbol_types);
    free(symbol_pins);
    parser_free(&p);
    return staged;
}

static graph *expression_graph(const behavior_asset *asset, const node *call)
{
    if (!asset || !call ||
        (call->desc != &NODE_CallPureFunction && call->desc != &NODE_CallFunction &&
         call->desc != &NODE_BehaviorExpression)) {
        return NULL;
    }
    graph *g = behavior_asset_find_graph(asset, call->reference_id);
    return g && g->math_expression ? g : NULL;
}

static graph *caller_graph(const behavior_asset *asset, const node *call)
{
    for (int i = 0; i < acount(asset->graphs); i++) {
        graph *g = asset->graphs[i];
        if (graph_node_by_id(g, call->id) == call) {
            return g;
        }
    }
    return NULL;
}

static void synchronize_call_groups(node *call, const node *entry, const node *ret)
{
    for (int i = 0; i < entry->output_count; i++) {
        const pin_spec *entry_pin = &entry->output_pins[i];
        if (entry_pin->kind != PIN_DATA || entry_pin->field_source) {
            continue;
        }
        for (int j = 0; j < call->input_count; j++) {
            if (!strcmp(call->input_pins[j].name, entry_pin->name)) {
                call->input_pins[j].group = entry_pin->group;
                break;
            }
        }
    }
    for (int i = 0; i < ret->input_count; i++) {
        const pin_spec *ret_pin = &ret->input_pins[i];
        if (ret_pin->kind == PIN_DATA && !ret_pin->field_source) {
            int output = find_named_output(call, ret_pin->name);
            if (output >= 0) {
                call->output_pins[output].group = ret_pin->group;
            }
        }
    }
}

static void synchronize_connected_inputs(behavior_asset *asset, node *call)
{
    graph *caller = caller_graph(asset, call);
    assert(caller);
    graph *generated = expression_graph(asset, call);
    assert(generated);
    synchronize_call_groups(
        call,
        graph_node_by_id(generated, generated->entry_id),
        graph_node_by_id(generated, generated->return_id)
    );
    for (int i = 0; i < call->input_count; i++) {
        const pin_spec *input = &call->input_pins[i];
        if (input->kind != PIN_DATA || graph_input_conversion(caller, call, i)) {
            continue;
        }
        node *source;
        int source_pin;
        if (!graph_input_source(caller, call, i, &source, &source_pin)) {
            continue;
        }
        const pin_spec *output = &source->output_pins[source_pin];
        if ((output->flags & PIN_FLAG_WILDCARD) && output->type == PIN_FLOW) {
            continue;
        }
        /* A concrete real parameter owns its storage width. Compatible
           incoming real values coerce at that boundary instead of changing
           the expression's declared parameter type. */
        if ((input->type == PIN_F32 || input->type == PIN_F64) &&
            (output->type == PIN_F32 || output->type == PIN_F64) &&
            pin_compatible(input, output)) {
            continue;
        }
        bool type_name_changed = input->type == PIN_STRUCT &&
                                 strcmp(
                                     input->type_name ? input->type_name : "",
                                     output->type_name ? output->type_name : ""
                                 ) != 0;
        if (input->type != output->type || type_name_changed) {
            int rc = graph_set_pin_type(caller, call, 0, i, output->type, output->type_name);
            assert(rc == 0);
            (void)rc;
        }
    }
}

static void rename_expression_call_inputs(
    graph *caller,
    node *call,
    const expression_rename *renames,
    int rename_count
)
{
    for (int i = 0; i < rename_count; i++) {
        for (int j = 0; j < call->input_count; j++) {
            pin_spec *input = &call->input_pins[j];
            if (input->kind == PIN_DATA && !input->field_source &&
                !strcmp(input->name, renames[i].from)) {
                int rc = graph_set_pin_name(caller, call, 0, j, renames[i].to);
                assert(rc == 0);
                (void)rc;
                break;
            }
        }
    }
}

const char *behavior_expression_source(const behavior_asset *asset, const node *call)
{
    graph *g = expression_graph(asset, call);
    return g ? g->math_expression : NULL;
}

/* Retypes a call's result and keeps every consumer that can take the new
   type, converting where needed: a double result still feeds an int field. */
static void retype_result(graph *caller, node *call, int output, const pin_spec *source)
{
    uint64_t *consumers = NULL; /* node and pin ID pairs */
    for (int i = 0; i < acount(caller->edges); i++) {
        const edge *e = &caller->edges[i];
        if (e->kind == GRAPH_EDGE_DATA && e->from_id == call->id &&
            e->from_pin_id == call->output_pins[output].id) {
            apush(consumers, e->to_id);
            apush(consumers, e->to_pin_id);
        }
    }
    int rc = graph_set_pin_type(caller, call, 1, output, source->type, source->type_name);
    assert(rc == 0);
    rc = graph_set_pin_container(caller, call, 1, output, source->container);
    assert(rc == 0);
    /* The body fixes the result's type: rewiring its consumers must not
       resolve the generic result to theirs again, or the call would read a
       value of one type as another. */
    unsigned flags = call->output_pins[output].flags;
    call->output_pins[output].flags &= ~PIN_FLAG_WILDCARD;
    for (int i = 0; i < acount(consumers); i += 2) {
        node *to = graph_node_by_id(caller, consumers[i]);
        int pin = -1;
        const pin_spec *in = to ? graph_pin_by_id(to, consumers[i + 1], NULL, &pin)
                                : NULL;
        const pin_spec *out = &call->output_pins[output];
        const node_desc *conversion = in && !pin_compatible(out, in)
                                        ? conversion_for_pins(out, in)
                                        : NULL;
        if (in && (conversion || pin_compatible(out, in))) {
            rc = graph_connect_data_conversion(caller, call, output, to, pin, conversion);
            assert(rc == 0);
        }
    }
    call->output_pins[output].flags = flags;
    (void)rc;
    afree(consumers);
}

static bool bool_wire(const graph *caller, const node *call, int pin)
{
    node *source = NULL;
    int source_pin = -1;
    return call->input_pins[pin].type == PIN_BOOL &&
           graph_input_source(caller, call, pin, &source, &source_pin);
}

static void synchronize_call(graph *caller, node *call, const node *entry, const node *ret)
{
    bool entry_exec = false;
    for (int i = 0; i < entry->output_count; i++) {
        entry_exec |= entry->output_pins[i].kind == PIN_EXEC;
    }
    bool expression_flow = call->desc == &NODE_BehaviorExpression &&
                           (find_named_input(call, "Exec") >= 0 ||
                            find_named_output(call, "True") >= 0 ||
                            find_named_output(call, "False") >= 0);
    bool want_exec = entry_exec || expression_flow;
    for (int i = call->input_count - 1; i >= 0; i--) {
        bool keep = call->input_pins[i].kind == PIN_EXEC && want_exec &&
                    !strcmp(call->input_pins[i].name, "Exec");
        for (int j = 0; j < entry->output_count; j++) {
            keep |= entry->output_pins[j].kind == PIN_DATA &&
                    !entry->output_pins[j].field_source &&
                    !strcmp(call->input_pins[i].name, entry->output_pins[j].name);
        }
        if (!keep) {
            int rc = graph_remove_pin(caller, call, 0, i);
            assert(rc == 0);
            (void)rc;
        }
    }
    int parameter = 0;
    if (want_exec) {
        int found = find_named_input(call, "Exec");
        if (found < 0) {
            pin_spec pin = { .name = "Exec", .type = PIN_FLOW, .kind = PIN_EXEC };
            found = graph_add_pin(caller, call, 0, &pin);
            assert(found >= 0);
        }
        if (found != parameter) {
            int rc = graph_move_pin(caller, call, 0, found, parameter);
            assert(rc >= 0);
            (void)rc;
        }
        parameter++;
    }
    for (int i = 0; i < entry->output_count; i++) {
        if (entry->output_pins[i].kind != PIN_DATA || entry->output_pins[i].field_source) {
            continue;
        }
        int found = parameter;
        while (found < call->input_count &&
               strcmp(call->input_pins[found].name, entry->output_pins[i].name)) {
            found++;
        }
        if (found == call->input_count) {
            pin_spec pin = entry->output_pins[i];
            pin.id = 0;
            if (pin.type_name) {
                pin.type_name = graph_own_str(caller, pin.type_name, strlen(pin.type_name));
            }
            found = graph_add_pin(caller, call, 0, &pin);
            assert(found >= 0);
        }
        if (found != parameter) {
            int rc = graph_move_pin(caller, call, 0, found, parameter);
            assert(rc >= 0);
            (void)rc;
        }
        if (call->input_pins[parameter].container != PIN_CONTAINER_SINGLE) {
            if (entry->output_pins[i].container == PIN_CONTAINER_SINGLE) {
                int rc = graph_set_pin_container(caller, call, 0, parameter, PIN_CONTAINER_SINGLE);
                assert(rc == 0);
                (void)rc;
            }
        }
        const pin_spec *entry_pin = &entry->output_pins[i];
        call->input_pins[parameter].group = entry_pin->group;
        call->input_pins[parameter].declaration_id = entry_pin->id;
        bool retype = !(entry_pin->flags & PIN_FLAG_WILDCARD) &&
                      (call->input_pins[parameter].type != entry_pin->type ||
                       ((call->input_pins[parameter].type == PIN_STRUCT ||
                         call->input_pins[parameter].type == PIN_BOOL) &&
                        strcmp(
                            call->input_pins[parameter].type_name
                                ? call->input_pins[parameter].type_name
                                : "",
                            entry_pin->type_name ? entry_pin->type_name : ""
                        ) != 0));
        /* A bool wired into a generic input stays a bool; the body reads it
           as a number through a conversion. */
        if ((entry_pin->flags & PIN_FLAG_WILDCARD) &&
            !generic_expression_value(&call->input_pins[parameter]) &&
            !bool_wire(caller, call, parameter)) {
            retype = true;
        }
        if (retype) {
            int rc = graph_set_pin_type(
                caller,
                call,
                0,
                parameter,
                entry_pin->flags & PIN_FLAG_WILDCARD ? PIN_F64 : entry_pin->type,
                entry_pin->flags & PIN_FLAG_WILDCARD ? "f64" : entry_pin->type_name
            );
            assert(rc == 0);
            (void)rc;
        }
        parameter++;
    }
    if (entry_exec) {
        int found = -1;
        for (int i = 0; i < call->output_count; i++) {
            if (call->output_pins[i].kind == PIN_EXEC &&
                !strcmp(call->output_pins[i].name, "Then")) {
                found = i;
                break;
            }
        }
        if (found < 0) {
            pin_spec pin = { .name = "Then", .type = PIN_FLOW, .kind = PIN_EXEC };
            found = graph_add_pin(caller, call, 1, &pin);
            assert(found >= 0);
        }
        if (found != 0) {
            int rc = graph_move_pin(caller, call, 1, found, 0);
            assert(rc >= 0);
            (void)rc;
        }
    }
    bool expression_branch = call->desc == &NODE_BehaviorExpression &&
                             find_named_output(call, "True") >= 0 &&
                             find_named_output(call, "False") >= 0;
    int output = entry_exec ? 1 : expression_branch ? 2 : 0;
    for (int i = 0; i < ret->input_count; i++) {
        const pin_spec *source = &ret->input_pins[i];
        if (source->kind != PIN_DATA || source->field_source) {
            continue;
        }
        int found = -1;
        for (int j = output; j < call->output_count; j++) {
            if (call->output_pins[j].kind == PIN_DATA &&
                !strcmp(call->output_pins[j].name, source->name)) {
                found = j;
                break;
            }
        }
        if (found < 0) {
            pin_spec pin = *source;
            pin.id = 0;
            pin.binding = NULL;
            if (pin.type_name) {
                pin.type_name = graph_own_str(caller, pin.type_name, strlen(pin.type_name));
            }
            found = graph_add_pin(caller, call, 1, &pin);
            assert(found >= 0);
        }
        if (found != output) {
            int rc = graph_move_pin(caller, call, 1, found, output);
            assert(rc >= 0);
            (void)rc;
        }
        call->output_pins[output].group = source->group;
        call->output_pins[output].declaration_id = source->id;
        if (call->output_pins[output].type != source->type ||
            call->output_pins[output].container != source->container ||
            (source->type == PIN_STRUCT &&
             strcmp(
                 call->output_pins[output].type_name ? call->output_pins[output].type_name : "",
                 source->type_name ? source->type_name : ""
             ) != 0)) {
            retype_result(caller, call, output, source);
        }
        output++;
    }
}

static int expression_ensure_pin(graph *caller, node *call, int output, const char *name, int at)
{
    int found = output ? find_named_output(call, name) : find_named_input(call, name);
    if (found < 0) {
        pin_spec pin = { .name = name, .type = PIN_FLOW, .kind = PIN_EXEC };
        found = graph_add_pin(caller, call, output, &pin);
        if (found < 0) {
            return -1;
        }
    }
    if (found != at && graph_move_pin(caller, call, output, found, at) < 0) {
        return -1;
    }
    return at;
}

static void expression_remove_flow_pin(graph *caller, node *call, int output, const char *name)
{
    int index = output ? find_named_output(call, name) : find_named_input(call, name);
    if (index >= 0) {
        int rc = graph_remove_pin(caller, call, output, index);
        assert(rc == 0);
        (void)rc;
    }
}

static void synchronize_expression_interface(
    graph *caller,
    node *call,
    graph_expression_mode mode,
    const char *source,
    const node *entry,
    const node *ret
)
{
    call->desc = &NODE_BehaviorExpression;
    if (mode == GRAPH_EXPRESSION_BRANCH) {
        expression_ensure_pin(caller, call, 0, "Exec", 0);
        expression_ensure_pin(caller, call, 1, "True", 0);
        expression_ensure_pin(caller, call, 1, "False", 1);
    }
    synchronize_call(caller, call, entry, ret);
    for (int i = 0; i < call->input_count; i++) {
        if (call->input_pins[i].kind == PIN_DATA) {
            call->input_pins[i].flags &= ~(PIN_FLAG_SETTING | PIN_FLAG_REFERENCE);
        }
    }
    if (mode == GRAPH_EXPRESSION_ACTION) {
        int assignment = expression_assignment_input(call, source);
        if (assignment >= 0) {
            call->input_pins[assignment].flags |= PIN_FLAG_REFERENCE;
        }
    }
    if (mode == GRAPH_EXPRESSION_BRANCH) {
        expression_ensure_pin(caller, call, 1, "True", 0);
        expression_ensure_pin(caller, call, 1, "False", 1);
    } else if (mode == GRAPH_EXPRESSION_ACTION) {
        expression_remove_flow_pin(caller, call, 1, "True");
        expression_remove_flow_pin(caller, call, 1, "False");
    } else {
        expression_remove_flow_pin(caller, call, 0, "Exec");
        expression_remove_flow_pin(caller, call, 1, "Then");
        expression_remove_flow_pin(caller, call, 1, "True");
        expression_remove_flow_pin(caller, call, 1, "False");
    }
}

int behavior_expression_repair(behavior_asset *asset, node *call)
{
    if (!asset || !behavior_asset_mutable(asset) || !expression_graph(asset, call)) {
        return -1;
    }
    graph *generated = expression_graph(asset, call);
    graph *caller = caller_graph(asset, call);
    assert(caller);
    synchronize_connected_inputs(asset, call);
    synchronize_expression_interface(
        caller,
        call,
        generated->expression_mode,
        generated->math_expression,
        graph_node_by_id(generated, generated->entry_id),
        graph_node_by_id(generated, generated->return_id)
    );
    return 0;
}

static node *first_call(const behavior_asset *asset, const graph *generated)
{
    for (int i = 0; i < acount(asset->graphs); i++) {
        graph *caller = asset->graphs[i];
        for (int j = 0; j < acount(caller->nodes); j++) {
            node *n = caller->nodes[j];
            if ((n->desc == &NODE_CallPureFunction || n->desc == &NODE_CallFunction ||
                 n->desc == &NODE_BehaviorExpression) &&
                n->reference_id == generated->graph_id && !n->reference_asset) {
                return n;
            }
        }
    }
    return NULL;
}

static graph *rebuild(const behavior_asset *asset, const graph *generated, uint64_t first_id)
{
    const node *call = first_call(asset, generated);
    if (!call || !generated->math_expression || generated->source_asset) {
        return NULL;
    }
    behavior_expression_error error;
    behavior_asset *staged = generate(
        (behavior_asset *)asset,
        caller_graph(asset, call),
        call,
        generated->math_expression,
        &error,
        NULL,
        0,
        first_id,
        generated->expression_mode
    );
    if (!staged) {
        return NULL;
    }
    graph *body = staged->graphs[1];
    asetlen(staged->graphs, 1);
    behavior_asset_free(staged);
    body->owner = NULL;
    body->shared_id_counter = NULL;
    for (int i = 0; i < acount(body->nodes); i++) {
        body->nodes[i]->placed = false;
    }
    return body;
}

graph *behavior_expression_rebuild(const behavior_asset *asset, const graph *generated)
{
    return generated->entry_id ? rebuild(asset, generated, generated->entry_id - 1)
                               : NULL;
}

static bool id_used(const behavior_asset *asset, const graph *except, uint64_t id)
{
    for (int i = 0; i < acount(asset->graphs); i++) {
        const graph *g = asset->graphs[i];
        if (g == except) {
            continue;
        }
        for (int j = 0; j < acount(g->nodes); j++) {
            const node *n = g->nodes[j];
            int output = 0, index = -1;
            if (n->id == id || graph_pin_by_id(n, id, &output, &index)) {
                return true;
            }
        }
    }
    return false;
}

/* Whether body's IDs are free in asset, beside those of the graph it replaces. */
static bool body_ids_free(const behavior_asset *asset, const graph *body, const graph *replaced)
{
    for (int i = 0; i < acount(body->nodes); i++) {
        const node *n = body->nodes[i];
        if (id_used(asset, replaced, n->id)) {
            return false;
        }
        for (int output = 0; output < 2; output++) {
            const pin_spec *pins = output ? n->output_pins : n->input_pins;
            for (int p = 0; p < (output ? n->output_count : n->input_count); p++) {
                if (id_used(asset, replaced, pins[p].id)) {
                    return false;
                }
            }
        }
    }
    return true;
}

int behavior_expression_restore(behavior_asset *asset, graph *generated)
{
    assert(!acount(generated->nodes));
    graph *body = behavior_expression_rebuild(asset, generated);
    if (body && !body_ids_free(asset, body, generated)) {
        /* A changed generator can need more IDs than the document reserved. */
        graph_free(body);
        free(body);
        body = rebuild(asset, generated, asset->graph->id_counter);
    }
    if (!body) {
        return -1;
    }
    uint64_t id = generated->graph_id;
    char *name = xstrdup(generated->name);
    char *description = xstrdup(generated->description ? generated->description : "");
    graph_free(generated);
    *generated = *body;
    free(body);
    generated->owner = asset;
    generated->shared_id_counter = &asset->graph->id_counter;
    generated->graph_id = id;
    generated->name = graph_own_str(generated, name, strlen(name));
    generated->description = graph_own_str(generated, description, strlen(description));
    free(name);
    free(description);
    return 0;
}

static graph *adopt(behavior_asset *asset, behavior_asset *staged, graph *previous)
{
    graph *generated = staged->graphs[1];
    if (staged->graph->id_counter > asset->graph->id_counter) {
        asset->graph->id_counter = staged->graph->id_counter;
    }
    asetlen(staged->graphs, 1);
    behavior_asset_free(staged);
    generated->owner = asset;
    generated->shared_id_counter = &asset->graph->id_counter;
    if (!previous) {
        apush(asset->graphs, generated);
        return generated;
    }
    for (int i = 0; i < acount(generated->nodes); i++) {
        node *next = generated->nodes[i];
        const node *old = graph_node_by_id(previous, next->id);
        if (!old || old->desc != next->desc) {
            continue;
        }
        next->x = old->x;
        next->y = old->y;
        next->placed = old->placed;
        next->presentation = old->presentation;
        next->breakpoint = old->breakpoint;
        next->breakpoint_enabled = old->breakpoint_enabled;
        next->show_advanced = old->show_advanced;
        next->comment = old->comment
                          ? graph_own_str(generated, old->comment, strlen(old->comment))
                          : NULL;
        next->comment_hidden = old->comment_hidden;
    }
    generated->breakpoints_enabled = previous->breakpoints_enabled;
    generated->description = previous->description ? graph_own_str(
                                                         generated,
                                                         previous->description,
                                                         strlen(previous->description)
                                                     )
                                                   : NULL;
    uint64_t id = previous->graph_id;
    char *name = xstrdup(previous->name);
    unsigned long long revision = previous->topology_revision;
    graph_free(previous);
    *previous = *generated;
    free(generated);
    previous->graph_id = id;
    previous->name = graph_own_str(previous, name, strlen(name));
    previous->topology_revision = revision + 1;
    free(name);
    return previous;
}

/* Calls reach an expression by ID, so two with the same source share it as
   their name. */
static void sync_expression_title(behavior_asset *asset, graph *generated, const char *source)
{
    int rc = graph_set_metadata(
        generated,
        source,
        generated->description ? generated->description : ""
    );
    assert(rc == 0);
    for (int i = 0; i < acount(asset->graphs); i++) {
        graph *caller = asset->graphs[i];
        for (int j = 0; j < acount(caller->nodes); j++) {
            node *n = caller->nodes[j];
            if (n->desc == &NODE_BehaviorExpression &&
                n->reference_id == generated->graph_id) {
                rc = graph_set_node_reference(caller, n, generated->graph_id, source);
                assert(rc == 0);
            }
        }
    }
    (void)rc;
}

static bool prepare_error(behavior_expression_error *error, const behavior_asset *asset, const char *source)
{
    *error = (behavior_expression_error){ 0 };
    if (!asset || !behavior_asset_mutable(asset) || !source || strlen(source) > INT_MAX) {
        snprintf(error->message, sizeof error->message, "Expression requires a mutable asset and valid source text");
        return false;
    }
    return true;
}

static void read_named_values(behavior_asset *asset, node *call, graph_expression_mode mode);

node *behavior_expression_add_mode(
    behavior_asset *asset,
    graph *caller,
    const char *source,
    graph_expression_mode mode,
    behavior_expression_error *error
)
{
    assert(
        mode == GRAPH_EXPRESSION_NONE || mode == GRAPH_EXPRESSION_VALUE || mode == GRAPH_EXPRESSION_BRANCH
    );
    behavior_expression_error local;
    error = error ? error : &local;
    if (!prepare_error(error, asset, source)) {
        return NULL;
    }
    if (!caller || caller->owner != asset) {
        snprintf(error->message, sizeof error->message, "Expression caller must belong to this asset");
        return NULL;
    }
    behavior_asset *staged = generate(
        asset,
        caller,
        NULL,
        source,
        error,
        NULL,
        0,
        asset->graph->id_counter,
        mode
    );
    if (!staged) {
        return NULL;
    }
    graph *generated = adopt(asset, staged, NULL);
    sync_expression_title(asset, generated, source);
    node *call = behavior_asset_add_function_call_node_to_graph(asset, caller, generated);
    assert(call);
    synchronize_expression_interface(
        caller,
        call,
        generated->expression_mode,
        generated->math_expression,
        graph_node_by_id(generated, generated->entry_id),
        graph_node_by_id(generated, generated->return_id)
    );
    read_named_values(asset, call, mode);
    return call;
}

/* An input named like one of the graph's named values reads it, as code reads
   a variable in scope. */
node *behavior_expression_add(behavior_asset *asset, graph *caller, const char *source, behavior_expression_error *error)
{
    return behavior_expression_add_mode(asset, caller, source, GRAPH_EXPRESSION_NONE, error);
}

static void read_named_values(behavior_asset *asset, node *call, graph_expression_mode mode)
{
    graph *caller = caller_graph(asset, call);
    bool bound = false;
    for (int p = 0; p < call->input_count; p++) {
        const pin_spec *pin = &call->input_pins[p];
        node *source = NULL;
        int source_pin = -1;
        if (pin->kind != PIN_DATA || pin->binding || (pin->flags & PIN_FLAG_REFERENCE) ||
            graph_input_source(caller, call, p, &source, &source_pin)) {
            continue;
        }
        for (int l = 0; l < acount(caller->locals); l++) {
            if (!strcmp(caller->locals[l].name, pin->name) &&
                graph_use_local(caller, caller->locals[l].id, call, p) == 0) {
                bound = true;
                break;
            }
        }
    }
    if (bound) {
        int rc = mode == GRAPH_EXPRESSION_NONE
                   ? behavior_expression_refresh(asset, call, NULL)
                   : behavior_expression_refresh_mode(asset, call, mode, NULL);
        assert(rc == 0);
        (void)rc;
    }
}

static int set_source(
    behavior_asset *asset,
    node *call,
    const char *source,
    const expression_rename *renames,
    int rename_count,
    behavior_expression_error *error
);

/* Source edits match inputs by name, as Unreal's Math Expression does: a name
   that stays keeps its wire, and a new one starts unfed unless a named value
   carries it. behavior_expression_rename_input renames an input with its wire. */
int behavior_expression_set(behavior_asset *asset, node *call, const char *source, behavior_expression_error *error)
{
    return set_source(asset, call, source, NULL, 0, error);
}

static bool fed(const graph *g, const node *n, int pin)
{
    node *source = NULL;
    int source_pin = -1;
    return n->input_pins[pin].kind == PIN_DATA &&
           (n->input_pins[pin].binding ||
            graph_input_source(g, n, pin, &source, &source_pin));
}

char **behavior_expression_fed_inputs(const graph *g, const node *call)
{
    char **names = NULL;
    for (int i = 0; i < call->input_count; i++) {
        if (fed(g, call, i)) {
            apush(names, xstrdup(call->input_pins[i].name));
        }
    }
    return names;
}

void behavior_expression_keep_unfed(const graph *g, const node *call, char ***names)
{
    for (int i = acount(*names) - 1; i >= 0; i--) {
        bool still = false;
        for (int p = 0; p < call->input_count; p++) {
            still |= !strcmp(call->input_pins[p].name, (*names)[i]) &&
                     fed(g, call, p);
        }
        if (still) {
            free((*names)[i]);
            adel(*names, i);
        }
    }
}

void behavior_expression_free_names(char **names)
{
    for (int i = 0; i < acount(names); i++) {
        free(names[i]);
    }
    afree(names);
}

static bool identifier_char(char c)
{
    return isalnum((unsigned char)c) || c == '_';
}

/* The source with every read of one input renamed; members (v.x) and
   function names (sin) keep their spelling. */
static char *rename_identifier(const char *source, const char *from, const char *to)
{
    size_t length = strlen(source), from_length = strlen(from),
           to_length = strlen(to);
    char *out = xmalloc(length * (to_length + 1) + 1);
    size_t used = 0;
    for (size_t i = 0; i < length;) {
        if (!identifier_char(source[i]) || (i && identifier_char(source[i - 1]))) {
            out[used++] = source[i++];
            continue;
        }
        size_t end = i;
        while (end < length && identifier_char(source[end])) {
            end++;
        }
        size_t before = i, after = end;
        while (before && source[before - 1] == ' ') {
            before--;
        }
        while (after < length && source[after] == ' ') {
            after++;
        }
        bool member = before && source[before - 1] == '.';
        bool function = after < length && source[after] == '(';
        bool match = end - i == from_length && !strncmp(source + i, from, from_length);
        const char *word = match && !member && !function ? to : source + i;
        size_t word_length = match && !member && !function ? to_length : end - i;
        memcpy(out + used, word, word_length);
        used += word_length;
        i = end;
    }
    out[used] = 0;
    return out;
}

int behavior_expression_rename_input(
    behavior_asset *asset,
    node *call,
    const char *from,
    const char *to,
    behavior_expression_error *error
)
{
    behavior_expression_error local;
    error = error ? error : &local;
    *error = (behavior_expression_error){ 0 };
    const char *source = behavior_expression_source(asset, call);
    bool valid = to && to[0] && !isdigit((unsigned char)to[0]);
    for (int i = 0; valid && to[i]; i++) {
        valid = identifier_char(to[i]);
    }
    if (!source || !from || !valid) {
        snprintf(error->message, sizeof error->message, "An input name is a letter or _ followed by letters, digits or _");
        return -1;
    }
    char *renamed = rename_identifier(source, from, to);
    /* Setting the source frees the graph that owns the old text, and may
       rename the pin a caller took `from` from. */
    char *old = xstrdup(from), *new = xstrdup(to);
    expression_rename rename = { .from = old, .to = new };
    bool unchanged = !strcmp(renamed, source);
    if (unchanged) {
        snprintf(error->message, sizeof error->message, "The expression reads no input named %s", old);
    }
    int result = unchanged ? -1 : set_source(asset, call, renamed, &rename, 1, error);
    free(renamed);
    free(old);
    free(new);
    return result;
}

static int set_source(
    behavior_asset *asset,
    node *call,
    const char *source,
    const expression_rename *renames,
    int rename_count,
    behavior_expression_error *error
)
{
    behavior_expression_error local;
    error = error ? error : &local;
    if (!prepare_error(error, asset, source)) {
        return -1;
    }
    graph *previous = expression_graph(asset, call);
    bool owned = false;
    for (int i = 0; i < acount(asset->graphs); i++) {
        owned |= graph_node_by_id(asset->graphs[i], call ? call->id : 0) == call && call;
    }
    if (!previous || !owned) {
        snprintf(error->message, sizeof error->message, "Node is not an expression call in this asset");
        return -1;
    }
    /* The same source regenerates against the current wiring. */
    if (!strcmp(previous->math_expression, source)) {
        return behavior_expression_refresh(asset, call, error);
    }
    bool follows_source = !strcmp(previous->name, previous->math_expression);
    char **symbols = NULL;
    if (parse_symbols(asset, caller_graph(asset, call), source, &symbols, error) != 0) {
        return -1;
    }
    synchronize_connected_inputs(asset, call);
    behavior_asset *staged = generate(
        asset,
        caller_graph(asset, call),
        call,
        source,
        error,
        renames,
        rename_count,
        asset->graph->id_counter,
        previous->expression_mode == GRAPH_EXPRESSION_VALUE ? GRAPH_EXPRESSION_VALUE
                                                            : GRAPH_EXPRESSION_NONE
    );
    if (!staged) {
        free_symbols(symbols);
        return -1;
    }
    graph *generated = adopt(asset, staged, previous);
    if (follows_source) {
        sync_expression_title(asset, generated, source);
    }
    const node *entry = graph_node_by_id(generated, generated->entry_id);
    for (int i = 0; i < acount(asset->graphs); i++) {
        graph *caller = asset->graphs[i];
        for (int j = 0; j < acount(caller->nodes); j++) {
            node *n = caller->nodes[j];
            if ((n->desc == &NODE_CallPureFunction || n->desc == &NODE_CallFunction ||
                 n->desc == &NODE_BehaviorExpression) &&
                n->reference_id == generated->graph_id) {
                rename_expression_call_inputs(caller, n, renames, rename_count);
                synchronize_expression_interface(
                    caller,
                    n,
                    generated->expression_mode,
                    generated->math_expression,
                    entry,
                    graph_node_by_id(generated, generated->return_id)
                );
            }
        }
    }
    free_symbols(symbols);
    read_named_values(asset, call, GRAPH_EXPRESSION_NONE);
    return 0;
}

/* A body regenerates from the IDs it holds, as a loaded one does, so
   refreshing a current expression leaves its document unchanged. One that
   needs more IDs than it held takes new ones. */
static behavior_asset *regenerate(
    behavior_asset *asset,
    node *call,
    const char *source,
    const graph *previous,
    graph_expression_mode mode,
    behavior_expression_error *error
)
{
    assert(previous->entry_id);
    graph *caller = caller_graph(asset, call);
    behavior_asset *staged = generate(asset, caller, call, source, error, NULL, 0, previous->entry_id - 1, mode);
    if (!staged || body_ids_free(asset, staged->graphs[1], previous)) {
        return staged;
    }
    behavior_asset_free(staged);
    return generate(asset, caller, call, source, error, NULL, 0, asset->graph->id_counter, mode);
}

int behavior_expression_infer_assignment_result(behavior_asset *asset, node *call, behavior_expression_error *error)
{
    behavior_expression_error local = { 0 };
    error = error ? error : &local;
    const char *source = behavior_expression_source(asset, call);
    if (!prepare_error(error, asset, source)) {
        return -1;
    }
    parser p = { .source = source, .cursor = source, .error = error };
    int result = parse_source(&p);
    int symbol = result >= 0 ? expression_assignment_symbol(&p) : -1;
    if (symbol >= 0) {
        pin_binding binding;
        graph *caller = caller_graph(asset, call);
        int reference = expression_assignment_reference(caller, call, p.symbols[symbol], NULL, 0, &binding);
        pin_type type;
        const char *name;
        pin_container container;
        const term *left = &p.terms[p.assignment_left];
        if (reference < 0 ||
            (reference > 0 &&
             expression_binding_type(asset, &binding, left, &type, &name, &container))) {
            snprintf(error->message, sizeof error->message, "Assignment target binding is invalid");
            result = -1;
        } else if (reference > 0) {
            if (left->index_count) {
                container = PIN_CONTAINER_SINGLE;
            }
            if (left->field[0]) {
                const type_desc *root = type_catalog_find(name);
                const type_field *field = root ? pin_type_field_path(root, left->field)
                                               : NULL;
                if (!field) {
                    snprintf(error->message, sizeof error->message, "Assignment member is invalid");
                    result = -1;
                } else {
                    type = field->type;
                    name = field->type_name;
                }
            }
            int output = find_named_output(call, "Result");
            if (result >= 0 && output >= 0 && type >= PIN_I8 &&
                type <= PIN_F64 && container == PIN_CONTAINER_SINGLE &&
                graph_set_pin_type(caller, call, true, output, type, name)) {
                snprintf(error->message, sizeof error->message, "Assignment result type could not be inferred");
                result = -1;
            }
        }
    }
    parser_free(&p);
    return result < 0 ? -1 : 0;
}

int behavior_expression_refresh_mode(
    behavior_asset *asset,
    node *call,
    graph_expression_mode mode,
    behavior_expression_error *error
)
{
    assert(
        mode == GRAPH_EXPRESSION_NONE || mode == GRAPH_EXPRESSION_VALUE || mode == GRAPH_EXPRESSION_BRANCH
    );
    behavior_expression_error local;
    error = error ? error : &local;
    const char *source = behavior_expression_source(asset, call);
    if (!prepare_error(error, asset, source)) {
        return -1;
    }
    graph *previous = expression_graph(asset, call);
    if (!previous) {
        snprintf(error->message, sizeof error->message, "Node is not an expression call in this asset");
        return -1;
    }
    synchronize_connected_inputs(asset, call);
    behavior_asset *staged = regenerate(asset, call, source, previous, mode, error);
    if (!staged) {
        return -1;
    }
    graph *generated = adopt(asset, staged, previous);
    const node *entry = graph_node_by_id(generated, generated->entry_id);
    const node *ret = graph_node_by_id(generated, generated->return_id);
    for (int i = 0; i < acount(asset->graphs); i++) {
        graph *caller = asset->graphs[i];
        for (int j = 0; j < acount(caller->nodes); j++) {
            node *n = caller->nodes[j];
            if ((n->desc == &NODE_CallPureFunction || n->desc == &NODE_CallFunction ||
                 n->desc == &NODE_BehaviorExpression) &&
                n->reference_id == generated->graph_id) {
                synchronize_expression_interface(
                    caller,
                    n,
                    generated->expression_mode,
                    generated->math_expression,
                    entry,
                    ret
                );
            }
        }
    }
    return 0;
}

int behavior_expression_refresh(behavior_asset *asset, node *call, behavior_expression_error *error)
{
    const graph *generated = expression_graph(asset, call);
    graph_expression_mode mode = generated && generated->expression_mode == GRAPH_EXPRESSION_VALUE
                                   ? GRAPH_EXPRESSION_VALUE
                                   : GRAPH_EXPRESSION_NONE;
    return behavior_expression_refresh_mode(asset, call, mode, error);
}
