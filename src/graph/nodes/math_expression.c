#include "ecs/ecs.h"
#include "graph/compiler/compile.h"
#include "graph/model/registry.h"
#include "graph/nodes/behavior_expression.h"
#include "graph/nodes/compare_nodes.h"
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
    int args[8];
    int count;
    int symbol;
    double number;
    bool boolean;
    bool is_boolean_literal;
    char field[256];
    int indices[4];
    int index_count;
} term;

typedef struct
{
    const char *source;
    const char *cursor;
    const char *start;
    int length;
    int token;
    double number;
    int depth;
    term *terms;
    char **symbols;
    behavior_expression_error *error;
    int assignment_operator;
    int assignment_left;
    int assignment_right;
} parser;

typedef struct
{
    const char *name;
    const char *node_name;
    int arity;
} math_function;

static const math_function functions[] = {
    { "sin", "Sine", 1 },
    { "cos", "Cosine", 1 },
    { "tan", "Tangent", 1 },
    { "asin", "Arc Sine", 1 },
    { "acos", "Arc Cosine", 1 },
    { "atan", "Arc Tangent", 1 },
    { "sqrt", "Square Root", 1 },
    { "exp", "Exponential", 1 },
    { "log", "Logarithm", 1 },
    { "ln", "Logarithm", 1 },
    { "log10", "Logarithm 10", 1 },
    { "floor", "Floor", 1 },
    { "ceil", "Ceiling", 1 },
    { "round", "Round", 1 },
    { "abs", "Abs", 1 },
    { "negate", "Negate", 1 },
    { "min", "Min", 2 },
    { "max", "Max", 2 },
    { "clamp", "Clamp", 3 },
    { "pow", "Power", 2 },
    { "atan2", "Arc Tangent 2", 2 },
    { "rand", "Random Real", 0 },
    { "vec2", "Make_CF_V2", 2 },
    { "vec3", "Make_CF_V3", 3 },
};

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
    for (size_t i = 0; i < sizeof functions / sizeof functions[0]; i++) {
        if (named(functions[i].name, name, length)) {
            function = &functions[i];
            break;
        }
    }
    if (!function) {
        p->start = name;
        p->length = length;
        return fail(p, "Unknown math function");
    }
    term value = { .desc = registry_node_by_name(function->node_name), .symbol = -1 };
    assert(value.desc);
    next(p);
    while (p->token != ')') {
        if (value.count == function->arity) {
            return fail(p, "Too many function arguments");
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
    if (value.count != function->arity) {
        return fail(p, "Wrong number of function arguments");
    }
    if (p->token != ')') {
        return fail(p, "Expected ')' after function arguments");
    }
    next(p);
    return append(p, value);
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
        return result;
    }
    if (p->token == '+' || p->token == '-' || p->token == '!') {
        if (p->token == '!') {
            next(p);
            int argument = expression(p, 25);
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
        int argument = expression(p, 25);
        if (argument < 0 || !negative) {
            return argument;
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
        int binding;
        const char *operation;
        switch (p->token) {
            case '+':
                binding = 10;
                operation = "Add";
                break;
            case '-':
                binding = 10;
                operation = "Subtract";
                break;
            case '*':
                binding = 20;
                operation = "Multiply";
                break;
            case '/':
                binding = 20;
                operation = "Divide";
                break;
            case '%':
                binding = 20;
                operation = "Modulo";
                break;
            case '^':
                binding = 30;
                operation = "Power";
                break;
            case TOKEN_OR:
                binding = 1;
                operation = "Or";
                break;
            case TOKEN_AND:
                binding = 2;
                operation = "And";
                break;
            case TOKEN_EQ:
                binding = 3;
                operation = "Equal";
                break;
            case TOKEN_NE:
                binding = 3;
                operation = "Not Equal";
                break;
            case '<':
                binding = 4;
                operation = "Less";
                break;
            case TOKEN_LE:
                binding = 4;
                operation = "Less Or Equal";
                break;
            case '>':
                binding = 4;
                operation = "Greater";
                break;
            case TOKEN_GE:
                binding = 4;
                operation = "Greater Or Equal";
                break;
            default:
                binding = -1;
                operation = NULL;
                break;
        }
        if (binding < precedence) {
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
            (term){ .desc = registry_node_by_name(operation),
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
                       .type_name = "double",
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

static int parse_symbols(const char *source, char ***symbols, behavior_expression_error *error)
{
    parser p = { .source = source, .cursor = source, .error = error };
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

static bool contains_symbol(char **symbols, int count, const char *name)
{
    for (int i = 0; i < count; i++) {
        if (!strcmp(symbols[i], name)) {
            return true;
        }
    }
    return false;
}

static expression_rename *expression_renames(const node *previous, char **symbols, int symbol_count)
{
    /* A source edit does not carry an explicit rename operation. Pairing only
     * equal-sized unmatched sets lets a name change retain its pin identity while
     * leaving add/remove edits to the normal signature synchronization. */
    const char **old_names = NULL;
    const char **new_names = NULL;
    for (int i = 0; previous && i < previous->input_count; i++) {
        const pin_spec *input = &previous->input_pins[i];
        if (input->kind != PIN_DATA || input->field_source || !input->name ||
            contains_symbol(symbols, symbol_count, input->name)) {
            continue;
        }
        apush(old_names, input->name);
    }
    for (int i = 0; i < symbol_count; i++) {
        bool found = false;
        for (int j = 0; previous && j < previous->input_count; j++) {
            const pin_spec *input = &previous->input_pins[j];
            if (input->kind == PIN_DATA && !input->field_source &&
                !strcmp(input->name, expression_pin_name(symbols[i]))) {
                found = true;
                break;
            }
        }
        if (!found) {
            apush(new_names, symbols[i]);
        }
    }
    if (acount(old_names) != acount(new_names)) {
        afree(old_names);
        afree(new_names);
        return NULL;
    }
    expression_rename *renames = NULL;
    for (int i = 0; i < acount(old_names); i++) {
        apush(renames, ((expression_rename){ .from = old_names[i], .to = new_names[i] }));
    }
    afree(old_names);
    afree(new_names);
    return renames;
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
    return pin->type == PIN_F32 || pin->type == PIN_F64 ||
           pin->type == PIN_I32 || pin->type == PIN_I64 ||
           pin->type == PIN_STRUCT || pin->container >= PIN_CONTAINER_ARRAY;
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
        for (int i = 0; i < value->count; i++) {
            expression_require_term(p, value->args[i], PIN_F64, types, known, arrays, structs, invalid);
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
        if (graph_connect_data(target, value.node, value.pin, make, i) != 0) {
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

static behavior_asset *generate(
    behavior_asset *asset,
    graph *caller,
    const node *previous,
    const char *source,
    behavior_expression_error *error,
    const expression_rename *renames,
    int rename_count
)
{
    parser p = { .source = source, .cursor = source, .error = error };
    int result = parse_source(&p);
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
    graph_expression_mode mode = p.assignment_operator
                                   ? GRAPH_EXPRESSION_ACTION
                                   : (expression_branch_context(caller) && boolean_result
                                          ? GRAPH_EXPRESSION_BRANCH
                                          : GRAPH_EXPRESSION_VALUE);
    behavior_asset *staged = behavior_asset_new("Expression staging");
    staged->graph->id_counter = asset->graph->id_counter;
    char name[64];
    snprintf(
        name,
        sizeof name,
        "Behavior Expression %llu",
        (unsigned long long)asset->graph->id_counter
    );
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
    for (int i = 0; i < symbol_count; i++) {
        if (symbol_known[i] && !symbol_structs[i]) {
            rc = graph_set_pin_type(
                target,
                entry,
                1,
                symbol_pins[i],
                symbol_types[i],
                symbol_types[i] == PIN_BOOL ? "bool" : "double"
            );
            assert(rc == 0);
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
    for (int i = 0; i < acount(p.terms); i++) {
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
        const node_desc *term_desc = term->is_boolean_literal
                                       ? registry_node_by_name("Bool")
                                       : (term->desc ? term->desc
                                                     : registry_node_by_name("Real"));
        node *n = graph_add_node(target, term_desc);
        assert(n);
        bool logical = term->is_boolean_literal || expression_logical(term->desc);
        bool comparison = expression_comparison(term->desc);
        bool aggregate = term->desc && term->desc->pure_kind == NODE_PURE_MAKE;
        if (!aggregate) {
            for (int j = 0; j < n->input_count; j++) {
                rc = graph_set_pin_type(target, n, 0, j, logical ? PIN_BOOL : PIN_F64, logical ? "bool" : "double");
                assert(rc == 0);
                (void)rc;
            }
            rc = graph_set_pin_type(
                target,
                n,
                1,
                0,
                logical || comparison ? PIN_BOOL : PIN_F64,
                logical || comparison ? "bool" : "double"
            );
            assert(rc == 0);
            (void)rc;
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
            rc = graph_connect_data(target, input.node, input.pin, n, j);
            assert(rc == 0);
            (void)rc;
            int depth = depths[term->args[j]] + 1;
            if (depth > depths[i]) {
                depths[i] = depth;
            }
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
        node *compound = graph_add_node(target, registry_node_by_name(operation));
        if (!compound) {
            result = fail(&p, "Could not lower assignment operator");
        } else {
            const pin_spec *left = &values[p.assignment_left]
                                        .node
                                        ->output_pins[values[p.assignment_left].pin];
            for (int pin = 0; pin < compound->input_count; pin++) {
                rc = graph_set_pin_type(target, compound, 0, pin, left->type, left->type_name);
                assert(rc == 0);
                (void)rc;
            }
            rc = graph_set_pin_type(target, compound, 1, 0, left->type, left->type_name);
            assert(rc == 0);
            (void)rc;
            rc = graph_connect_data(
                target,
                values[p.assignment_left].node,
                values[p.assignment_left].pin,
                compound,
                0
            );
            assert(rc == 0);
            (void)rc;
            rc = graph_connect_data(
                target,
                values[p.assignment_right].node,
                values[p.assignment_right].pin,
                compound,
                1
            );
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
                     graph_connect_data(target, write_value.node, write_value.pin, setter, 1) != 0)
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
                     graph_connect_data(target, write_value.node, write_value.pin, setter, 1) != 0)
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
    if (result >= 0 &&
        graph_connect_data(target, final.node, final.pin, ret, result_pin) != 0) {
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
        bool retype = !(entry_pin->flags & PIN_FLAG_WILDCARD) &&
                      (call->input_pins[parameter].type != entry_pin->type ||
                       (call->input_pins[parameter].type == PIN_STRUCT &&
                        strcmp(call->input_pins[parameter].type_name, entry_pin->type_name) !=
                            0));
        if ((entry_pin->flags & PIN_FLAG_WILDCARD) &&
            !generic_expression_value(&call->input_pins[parameter])) {
            retype = true;
        }
        if (retype) {
            int rc = graph_set_pin_type(
                caller,
                call,
                0,
                parameter,
                entry_pin->flags & PIN_FLAG_WILDCARD ? PIN_F64 : entry_pin->type,
                entry_pin->flags & PIN_FLAG_WILDCARD ? "double" : entry_pin->type_name
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
        if (call->output_pins[output].type != source->type ||
            (source->type == PIN_STRUCT &&
             strcmp(
                 call->output_pins[output].type_name ? call->output_pins[output].type_name : "",
                 source->type_name ? source->type_name : ""
             ) != 0)) {
            int rc = graph_set_pin_type(
                caller,
                call,
                1,
                output,
                source->type,
                source->type_name
            );
            assert(rc == 0);
            (void)rc;
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

static graph *adopt(behavior_asset *asset, behavior_asset *staged, graph *previous)
{
    graph *generated = staged->graphs[1];
    asset->graph->id_counter = staged->graph->id_counter;
    asetlen(staged->graphs, 1);
    behavior_asset_free(staged);
    generated->owner = asset;
    generated->shared_id_counter = &asset->graph->id_counter;
    if (!previous) {
        apush(asset->graphs, generated);
        return generated;
    }
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

static void sync_expression_title(behavior_asset *asset, graph *generated, const char *source)
{
    graph *same = behavior_asset_find_graph_by_name(asset, source);
    if (same && same != generated) {
        return;
    }
    int rc = behavior_asset_rename_graph(asset, generated->graph_id, source);
    assert(rc == 0);
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

node *behavior_expression_add(behavior_asset *asset, graph *caller, const char *source, behavior_expression_error *error)
{
    behavior_expression_error local;
    error = error ? error : &local;
    if (!prepare_error(error, asset, source)) {
        return NULL;
    }
    if (!caller || caller->owner != asset) {
        snprintf(error->message, sizeof error->message, "Expression caller must belong to this asset");
        return NULL;
    }
    behavior_asset *staged = generate(asset, caller, NULL, source, error, NULL, 0);
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
    return call;
}

int behavior_expression_set(behavior_asset *asset, node *call, const char *source, behavior_expression_error *error)
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
    if (!strcmp(previous->math_expression, source)) {
        synchronize_connected_inputs(asset, call);
        return 0;
    }
    bool follows_source = !strcmp(previous->name, previous->math_expression);
    char **symbols = NULL;
    if (parse_symbols(source, &symbols, error) != 0) {
        return -1;
    }
    expression_rename *renames = expression_renames(call, symbols, acount(symbols));
    synchronize_connected_inputs(asset, call);
    behavior_asset *staged = generate(
        asset,
        caller_graph(asset, call),
        call,
        source,
        error,
        renames,
        acount(renames)
    );
    if (!staged) {
        afree(renames);
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
                rename_expression_call_inputs(caller, n, renames, acount(renames));
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
    afree(renames);
    free_symbols(symbols);
    return 0;
}

int behavior_expression_refresh(behavior_asset *asset, node *call, behavior_expression_error *error)
{
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
    behavior_asset *staged = generate(asset, caller_graph(asset, call), call, source, error, NULL, 0);
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
