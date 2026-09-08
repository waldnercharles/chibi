#include "graph/nodes/math_expression.h"

#include "graph/compiler/compile.h"
#include "graph/model/registry.h"
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
    EXPRESSION_MAX_DEPTH = 128
};

typedef struct
{
    const node_desc *desc;
    int args[3];
    int count;
    int symbol;
    double number;
    char field[64];
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
    math_expression_error *error;
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
        if (c == '*' && *p->cursor == '*') {
            p->cursor++;
            p->token = '^';
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
        if (p->token == '.') {
            next(p);
            if (p->token != TOKEN_NAME) {
                return fail(p, "Expected a field name after '.'");
            }
            if (p->length >= (int)sizeof value.field) {
                return fail(p, "Field name is too long");
            }
            memcpy(value.field, p->start, (size_t)p->length);
            value.field[p->length] = 0;
            next(p);
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
    if (p->token == '+' || p->token == '-') {
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

static pin_spec generic_pin(const char *name, int group)
{
    return (pin_spec){ .name = name,
                       .type = PIN_F64,
                       .type_name = "double",
                       .kind = PIN_DATA,
                       .flags = PIN_FLAG_WILDCARD,
                       .group = group,
                       .default_value = { .type = PIN_F64 } };
}

static const pin_spec *previous_input(const node *previous, const char *name)
{
    if (!previous || !name) {
        return NULL;
    }
    for (int i = 0; i < previous->input_count; i++) {
        if (!strcmp(previous->input_pins[i].name, name)) {
            return &previous->input_pins[i];
        }
    }
    return NULL;
}

static bool generic_expression_value(const pin_spec *pin)
{
    return pin->type == PIN_F32 || pin->type == PIN_F64 || pin->type == PIN_I32 ||
           pin->type == PIN_I64 || pin->type == PIN_STRUCT;
}

typedef struct
{
    node *node;
    int pin;
} value_source;

static behavior_asset *generate(
    behavior_asset *asset,
    const node *previous,
    const char *source,
    math_expression_error *error
)
{
    parser p = { .source = source, .cursor = source, .error = error };
    next(&p);
    int result = expression(&p, 0);
    if (result >= 0 && p.token) {
        result = fail(&p, "Unexpected token after expression");
    }
    if (result < 0) {
        parser_free(&p);
        return NULL;
    }
    behavior_asset *staged = behavior_asset_new("Expression staging");
    staged->graph->id_counter = asset->graph->id_counter;
    char name[64];
    snprintf(
        name,
        sizeof name,
        "Math Expression %llu",
        (unsigned long long)asset->graph->id_counter
    );
    graph *target = behavior_asset_add_function(staged, name, 1);
    assert(target);
    target->math_expression = graph_own_str(target, source, strlen(source));
    node *entry = graph_node_by_id(target, target->entry_id);
    node *ret = graph_node_by_id(target, target->return_id);
    int *symbol_pins = xmalloc((size_t)acount(p.symbols) * sizeof *symbol_pins);
    for (int i = 0; i < acount(p.symbols); i++) {
        symbol_pins[i] = -1;
    }
    /* Keep surviving input order, then append new names in source order. */
    for (int i = 0; previous && i < previous->input_count; i++) {
        for (int j = 0; j < acount(p.symbols); j++) {
            if (!strcmp(previous->input_pins[i].name, p.symbols[j])) {
                pin_spec pin = generic_pin(p.symbols[j], j + 1);
                symbol_pins[j] = graph_add_pin(target, entry, 1, &pin);
                assert(symbol_pins[j] >= 0);
                const pin_spec *old = previous_input(previous, p.symbols[j]);
                if (old && old->type != PIN_FLOW) {
                    assert(
                        graph_set_pin_type(target, entry, 1, symbol_pins[j], old->type, old->type_name) == 0
                    );
                }
            }
        }
    }
    for (int i = 0; i < acount(p.symbols); i++) {
        if (symbol_pins[i] < 0) {
            pin_spec pin = generic_pin(p.symbols[i], i + 1);
            symbol_pins[i] = graph_add_pin(target, entry, 1, &pin);
            assert(symbol_pins[i] >= 0);
        }
    }
    for (int i = 0; i < acount(p.terms); i++) {
        const term *term = &p.terms[i];
        if (!term->field[0]) {
            continue;
        }
        pin_spec *input = &entry->output_pins[symbol_pins[term->symbol]];
        uint64_t source = input->id;
        if (input->type != PIN_STRUCT) {
            continue;
        }
        if (graph_expose_output_fields(target, entry, symbol_pins[term->symbol]) != 0) {
            result = fail(&p, "Could not expose fields for this input");
            break;
        }
        bool found = false;
        for (int j = 0; j < entry->output_count; j++) {
            const pin_spec *field = &entry->output_pins[j];
            if (field->field_source == source && field->field_name &&
                !strcmp(field->field_name, term->field)) {
                found = true;
                break;
            }
        }
        if (!found) {
            result = fail(&p, "Unknown field on expression input");
            break;
        }
    }
    if (result < 0) {
        free(symbol_pins);
        parser_free(&p);
        behavior_asset_free(staged);
        return NULL;
    }
    pin_spec output = generic_pin("Result", acount(p.symbols) + 1);
    assert(graph_add_pin(target, ret, 0, &output) == 0);
    value_source *values = xmalloc((size_t)acount(p.terms) * sizeof *values);
    int *depths = xcalloc((size_t)acount(p.terms), sizeof *depths);
    int max_depth = 0;
    for (int i = 0; i < acount(p.terms); i++) {
        const term *term = &p.terms[i];
        if (term->symbol >= 0) {
            int pin = symbol_pins[term->symbol];
            if (term->field[0]) {
                pin = -1;
                const uint64_t
                    source = entry->output_pins[symbol_pins[term->symbol]].id;
                for (int j = 0; j < entry->output_count; j++) {
                    const pin_spec *field = &entry->output_pins[j];
                    if (field->field_source == source && field->field_name &&
                        !strcmp(field->field_name, term->field)) {
                        pin = j;
                        break;
                    }
                }
                if (pin < 0) {
                    pin = symbol_pins[term->symbol];
                }
            }
            values[i] = (value_source){ entry, pin };
            continue;
        }
        node *n = graph_add_node(target, term->desc ? term->desc : registry_node_by_name("Real"));
        assert(n);
        for (int j = 0; j < n->input_count; j++) {
            assert(graph_set_pin_type(target, n, 0, j, PIN_F64, "double") == 0);
        }
        assert(graph_set_pin_type(target, n, 1, 0, PIN_F64, "double") == 0);
        if (!term->desc) {
            pin_value value = { .type = PIN_F64, .d = term->number };
            assert(graph_set_pin_default(target, n, 0, 0, &value) == 0);
        }

        depths[i] = 1;
        for (int j = 0; j < term->count; j++) {
            value_source input = values[term->args[j]];
            assert(graph_connect_data(target, input.node, input.pin, n, j) == 0);
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
    assert(graph_connect_data(target, values[result].node, values[result].pin, ret, 0) == 0);
    ret->x = (float)(max_depth + 1) * 220;
    ret->y = values[result].node->y;
    free(depths);
    free(values);
    free(symbol_pins);
    parser_free(&p);
    return staged;
}

static graph *expression_graph(const behavior_asset *asset, const node *call)
{
    if (!asset || !call || call->desc != &NODE_CallPureFunction) {
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
    int output = 0;
    for (int i = 0; i < ret->input_count && output < call->output_count; i++) {
        const pin_spec *ret_pin = &ret->input_pins[i];
        if (ret_pin->kind == PIN_DATA && !ret_pin->field_source) {
            call->output_pins[output++].group = ret_pin->group;
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
            assert(graph_set_pin_type(caller, call, 0, i, output->type, output->type_name) == 0);
        }
    }
}

const char *math_expression_source(const behavior_asset *asset, const node *call)
{
    graph *g = expression_graph(asset, call);
    return g ? g->math_expression : NULL;
}

static void synchronize_call(graph *caller, node *call, const node *entry, const node *ret)
{
    for (int i = call->input_count - 1; i >= 0; i--) {
        bool keep = false;
        for (int j = 0; j < entry->output_count; j++) {
            keep |= entry->output_pins[j].kind == PIN_DATA &&
                    !entry->output_pins[j].field_source &&
                    !strcmp(call->input_pins[i].name, entry->output_pins[j].name);
        }
        if (!keep) {
            assert(graph_remove_pin(caller, call, 0, i) == 0);
        }
    }
    int parameter = 0;
    for (int i = 0; i < entry->output_count; i++) {
        if (entry->output_pins[i].kind != PIN_DATA || entry->output_pins[i].field_source) {
            continue;
        }
        int found = 0;
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
            assert(graph_move_pin(caller, call, 0, found, parameter) == 0);
        }
        if (call->input_pins[parameter].container != PIN_CONTAINER_SINGLE) {
            assert(graph_set_pin_container(caller, call, 0, parameter, PIN_CONTAINER_SINGLE) == 0);
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
            assert(
                graph_set_pin_type(
                    caller,
                    call,
                    0,
                    parameter,
                    entry_pin->flags & PIN_FLAG_WILDCARD ? PIN_F64 : entry_pin->type,
                    entry_pin->flags & PIN_FLAG_WILDCARD ? "double" : entry_pin->type_name
                ) == 0
            );
        }
        parameter++;
    }
    int output = 0;
    for (int i = 0; i < ret->input_count; i++) {
        const pin_spec *source = &ret->input_pins[i];
        if (source->kind != PIN_DATA || source->field_source) {
            continue;
        }
        if (output >= call->output_count) {
            break;
        }
        call->output_pins[output].group = source->group;
        if (call->output_pins[output].type != source->type ||
            (source->type == PIN_STRUCT &&
             strcmp(
                 call->output_pins[output].type_name ? call->output_pins[output].type_name : "",
                 source->type_name ? source->type_name : ""
             ) != 0)) {
            assert(
                graph_set_pin_type(caller, call, 1, output, source->type, source->type_name) == 0
            );
        }
        output++;
    }
}

int math_expression_repair(behavior_asset *asset, node *call)
{
    if (!asset || !behavior_asset_mutable(asset) || !expression_graph(asset, call)) {
        return -1;
    }
    graph *generated = expression_graph(asset, call);
    graph *caller = caller_graph(asset, call);
    assert(caller);
    synchronize_connected_inputs(asset, call);
    synchronize_call(
        caller,
        call,
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
    assert(behavior_asset_rename_graph(asset, generated->graph_id, source) == 0);
}

static bool prepare_error(math_expression_error *error, const behavior_asset *asset, const char *source)
{
    *error = (math_expression_error){ 0 };
    if (!asset || !behavior_asset_mutable(asset) || !source || strlen(source) > INT_MAX) {
        snprintf(error->message, sizeof error->message, "Expression requires a mutable asset and valid source text");
        return false;
    }
    return true;
}

node *math_expression_add(behavior_asset *asset, graph *caller, const char *source, math_expression_error *error)
{
    math_expression_error local;
    error = error ? error : &local;
    if (!prepare_error(error, asset, source)) {
        return NULL;
    }
    if (!caller || caller->owner != asset) {
        snprintf(error->message, sizeof error->message, "Expression caller must belong to this asset");
        return NULL;
    }
    behavior_asset *staged = generate(asset, NULL, source, error);
    if (!staged) {
        return NULL;
    }
    graph *generated = adopt(asset, staged, NULL);
    sync_expression_title(asset, generated, source);
    node *call = behavior_asset_add_function_call_node_to_graph(asset, caller, generated);
    assert(call);
    synchronize_call(
        caller,
        call,
        graph_node_by_id(generated, generated->entry_id),
        graph_node_by_id(generated, generated->return_id)
    );
    return call;
}

int math_expression_set(behavior_asset *asset, node *call, const char *source, math_expression_error *error)
{
    math_expression_error local;
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
    synchronize_connected_inputs(asset, call);
    if (!strcmp(previous->math_expression, source)) {
        return 0;
    }
    bool follows_source = !strcmp(previous->name, previous->math_expression);
    behavior_asset *staged = generate(asset, call, source, error);
    if (!staged) {
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
            if (n->desc == &NODE_CallPureFunction &&
                n->reference_id == generated->graph_id) {
                synchronize_call(
                    caller,
                    n,
                    entry,
                    graph_node_by_id(generated, generated->return_id)
                );
            }
        }
    }
    return 0;
}

int math_expression_refresh(behavior_asset *asset, node *call, math_expression_error *error)
{
    math_expression_error local;
    error = error ? error : &local;
    const char *source = math_expression_source(asset, call);
    if (!prepare_error(error, asset, source)) {
        return -1;
    }
    graph *previous = expression_graph(asset, call);
    if (!previous) {
        snprintf(error->message, sizeof error->message, "Node is not an expression call in this asset");
        return -1;
    }
    synchronize_connected_inputs(asset, call);
    behavior_asset *staged = generate(asset, call, source, error);
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
            if (n->desc == &NODE_CallPureFunction &&
                n->reference_id == generated->graph_id) {
                synchronize_call(caller, n, entry, ret);
            }
        }
    }
    return 0;
}
