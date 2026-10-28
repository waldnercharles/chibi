#include "authoring/source_presentation_internal.h"
#include "graph/authoring/graph_edit.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    node *node;
    int pin;
} source_flow_target;
typedef struct
{
    source_flow_target next, stop, repeat;
} source_flow;

static int exec_pin(const node *n, bool output, const char *name)
{
    const pin_spec *pins = output ? n->output_pins : n->input_pins;
    int count = output ? n->output_count : n->input_count;
    for (int i = 0; i < count; i++) {
        if (pins[i].kind == PIN_EXEC && (!name || !strcmp(pins[i].name, name))) {
            return i;
        }
    }
    return -1;
}
static void flow_wire(source_projection *p, node *from, int output, source_flow_target to)
{
    if (to.node) {
        source_view_wire(p, from, output, to.node, to.pin, GRAPH_EDGE_EXEC, 0);
    }
}
static const char *statement_name(source_projection *p, const script_statement *s)
{
    static const char *names[] = { "Block",  "Declare",  "Set",    "Evaluate",
                                   "Branch", "While",    "For",    "For Each",
                                   "Break",  "Continue", "Return", "Label",
                                   "Goto",   "Sequence", "Switch", "Case",
                                   "Reduce" };
    if (s->kind == SCRIPT_STMT_RETURN &&
        p->analysis->functions[p->function].kind == SCRIPT_FUNCTION_HANDLER) {
        return "End Event";
    }
    if (s->reverse) {
        return s->kind == SCRIPT_STMT_FOR ? "For Reverse" : "For Each Reverse";
    }
    if (s->kind != SCRIPT_STMT_ASSIGN && s->kind != SCRIPT_STMT_DECLARE &&
        s->kind != SCRIPT_STMT_REDUCE) {
        return names[s->kind];
    }
    const char *target = s->symbol >= 0 ? p->analysis->symbols[s->symbol].name : "Value";
    int length = (int)strlen(target);
    if ((s->kind == SCRIPT_STMT_ASSIGN || s->kind == SCRIPT_STMT_REDUCE) &&
        s->expression >= 0) {
        const script_expression *destination = &p->analysis->expressions[s->expression];
        if (destination->kind == SCRIPT_EXPR_SYMBOL && destination->symbol >= 0) {
            target = p->analysis->symbols[destination->symbol].name;
            length = (int)strlen(target);
        } else {
            target = p->analysis->text + destination->range.start;
            length = destination->range.end - destination->range.start;
        }
    }
    const char *operation = "Set";
    switch (s->op) {
        case SCRIPT_OP_ADD: operation = "Add to"; break;
        case SCRIPT_OP_SUB: operation = "Subtract from"; break;
        case SCRIPT_OP_MUL: operation = "Multiply"; break;
        case SCRIPT_OP_DIV: operation = "Divide"; break;
        case SCRIPT_OP_MOD: operation = "Modulo"; break;
        default: break;
    }
    if (s->kind == SCRIPT_STMT_REDUCE) {
        operation = s->reduction == SCRIPT_REDUCTION_MIN ? "Reduce min"
                  : s->reduction == SCRIPT_REDUCTION_MAX ? "Reduce max"
                  : s->reduction == SCRIPT_REDUCTION_ADD_UNORDERED
                      ? "Reduce unordered add"
                      : "Reduce add";
    }
    size_t size = strlen(operation) + (size_t)length + 2;
    char *name = xmalloc(size);
    snprintf(name, size, "%s %.*s", operation, length, target);
    const char *owned = source_view_own(p->graph, name);
    free(name);
    return owned;
}
static const script_symbol *snapshot_variable(source_projection *p, const script_statement *s)
{
    if (s->kind != SCRIPT_STMT_DECLARE || s->expression < 0) {
        return NULL;
    }
    const script_expression *value = &p->analysis->expressions[s->expression];
    if (value->kind != SCRIPT_EXPR_SYMBOL || value->symbol < 0) {
        return NULL;
    }
    const script_symbol *symbol = &p->analysis->symbols[value->symbol];
    return symbol->kind == SCRIPT_STATE || symbol->kind == SCRIPT_SHARED_STATE
             ? symbol
             : NULL;
}
static node *statement_call(source_projection *p, int index)
{
    const script_statement *s = &p->analysis->statements[index];
    const script_expression *e = &p->analysis->expressions[s->expression];
    node *n = source_view_expression(p, s->expression);
    int input = exec_pin(n, false, NULL);
    if (input < 0) {
        pin_spec pin = source_view_flow_pin(p, "Exec");
        input = graph_add_pin(p->graph, n, false, &pin);
        assert(input >= 0);
    }
    const char *continuation = e->awaited || e->kind == SCRIPT_EXPR_JOIN
                                 ? "Completed"
                                 : "Then";
    int output = exec_pin(n, true, continuation);
    if (output < 0 && (e->awaited || e->kind == SCRIPT_EXPR_JOIN)) {
        output = exec_pin(n, true, NULL);
    }
    if (output < 0) {
        pin_spec pin = source_view_flow_pin(p, continuation);
        output = graph_add_pin(p->graph, n, true, &pin);
        assert(output >= 0);
    }
    source_view_origin(p, SOURCE_PRESENTATION_STATEMENT, index, s->range, n, 0, e->capability);
    source_view_origin(
        p,
        SOURCE_PRESENTATION_STATEMENT,
        index,
        s->range,
        n,
        n->input_pins[input].id,
        e->capability
    );
    source_view_origin(
        p,
        SOURCE_PRESENTATION_STATEMENT,
        index,
        s->range,
        n,
        n->output_pins[output].id,
        e->capability
    );
    return n;
}
static node *statement_box(source_projection *p, int index)
{
    const script_statement *s = &p->analysis->statements[index];
    if (s->kind == SCRIPT_STMT_EXPRESSION && s->expression >= 0) {
        script_expression_kind kind = p->analysis->expressions[s->expression].kind;
        if (kind == SCRIPT_EXPR_CALL || kind == SCRIPT_EXPR_ARRAY_MUTATE ||
            kind == SCRIPT_EXPR_JOIN || kind == SCRIPT_EXPR_CANCEL ||
            kind == SCRIPT_EXPR_YIELD) {
            return statement_call(p, index);
        }
    }
    pin_spec *inputs = NULL, *outputs = NULL;
    int *values = NULL;
    const script_symbol *snapshot = snapshot_variable(p, s);
    apush(inputs, source_view_flow_pin(p, "Exec"));
    bool loop = s->kind == SCRIPT_STMT_WHILE || s->kind == SCRIPT_STMT_FOR ||
                s->kind == SCRIPT_STMT_FOREACH;
    if (loop) {
        apush(inputs, source_view_flow_pin(p, "Continue"));
    }
    int first_value = acount(inputs);
    int receivers = 0;
    bool entity_receiver = false;
    const script_expression *destination = (s->kind == SCRIPT_STMT_ASSIGN ||
                                            s->kind == SCRIPT_STMT_REDUCE) &&
                                                   s->expression >= 0
                                             ? &p->analysis->expressions[s->expression]
                                             : NULL;
    if (destination) {
        int receiver = destination->left;
        const script_expression *component = destination;
        while (component->kind == SCRIPT_EXPR_MEMBER && component->left >= 0) {
            component = &p->analysis->expressions[component->left];
        }
        if (component->kind == SCRIPT_EXPR_COMPONENT) {
            receiver = component->left;
            entity_receiver = true;
        }
        if (destination->kind != SCRIPT_EXPR_SYMBOL && receiver >= 0) {
            apush(values, receiver);
            receivers++;
        }
        if (destination->kind == SCRIPT_EXPR_INDEX && destination->right >= 0) {
            apush(values, destination->right);
            receivers++;
        }
    } else if (s->expression >= 0 && !snapshot) {
        apush(values, s->expression);
    }
    if (s->second >= 0) {
        apush(values, s->second);
    }
    if (s->third >= 0) {
        apush(values, s->third);
    }
    for (int i = 0; s->kind == SCRIPT_STMT_RETURN && i < s->child_count; i++) {
        const script_statement *result = &p->analysis->statements[s->children[i]];
        if (result->second >= 0) {
            apush(values, result->second);
        }
    }
    for (int i = 0; i < acount(values); i++) {
        const char *name = "Value";
        if (s->kind == SCRIPT_STMT_IF || s->kind == SCRIPT_STMT_WHILE) {
            name = "Condition";
        } else if (s->kind == SCRIPT_STMT_ASSIGN || s->kind == SCRIPT_STMT_REDUCE) {
            name = i >= receivers ? "Value"
                 : destination->kind == SCRIPT_EXPR_INDEX
                     ? (i ? "Index" : "Collection")
                 : entity_receiver ? "Entity"
                                   : "Object";
        } else if (s->kind == SCRIPT_STMT_FOR) {
            name = !i ? "First" : s->exclusive ? "Before" : "Last";
        } else if (s->kind == SCRIPT_STMT_FOREACH) {
            name = "Collection";
        }
        apush(
            inputs,
            source_view_data_pin(p, name, p->analysis->expressions[values[i]].type)
        );
    }
    if (s->kind == SCRIPT_STMT_IF) {
        apush(outputs, source_view_flow_pin(p, "True"));
        apush(outputs, source_view_flow_pin(p, "False"));
    } else if (loop) {
        apush(outputs, source_view_flow_pin(p, "Body"));
        apush(outputs, source_view_flow_pin(p, "Completed"));
    } else if (s->kind == SCRIPT_STMT_MATCH) {
        bool fallback = false;
        for (int i = 0; i < s->child_count; i++) {
            const script_statement *arm = &p->analysis->statements[s->children[i]];
            const char *name = "Default";
            if (arm->expression >= 0) {
                script_range range = p->analysis->expressions[arm->expression].range;
                name = graph_own_str(
                    p->graph,
                    p->analysis->text + range.start,
                    (size_t)(range.end - range.start)
                );
            } else {
                fallback = true;
            }
            apush(outputs, source_view_flow_pin(p, name));
        }
        if (!fallback) {
            apush(outputs, source_view_flow_pin(p, "Default"));
        }
    } else if (s->kind != SCRIPT_STMT_RETURN) {
        apush(outputs, source_view_flow_pin(p, "Then"));
    }
    if (s->kind == SCRIPT_STMT_DECLARE && s->symbol >= 0) {
        const script_symbol *symbol = &p->analysis->symbols[s->symbol];
        const script_expression *tuple = source_view_tuple(p, s->expression);
        int count = tuple ? tuple->result_count : symbol->type.storage != PIN_FLOW;
        for (int i = 0; i < count; i++) {
            apush(
                outputs,
                source_view_data_pin(
                    p,
                    tuple ? source_view_result_name(p, tuple, i) : symbol->name,
                    tuple ? tuple->results[i] : symbol->type
                )
            );
        }
    }
    const char *title = statement_name(p, s);
    if (snapshot) {
        size_t size = strlen(snapshot->name) + sizeof "Get ";
        char *name = xmalloc(size);
        snprintf(name, size, "Get %s", snapshot->name);
        title = source_view_own(p->graph, name);
        free(name);
    }
    node *n = source_view_box(p, title, NODE_DISPLAY_FLOW, inputs, outputs);
    int variable = destination && destination->kind == SCRIPT_EXPR_SYMBOL
                     ? destination->symbol
                 : s->kind == SCRIPT_STMT_DECLARE ? s->symbol
                                                  : -1;
    if (variable >= 0) {
        source_view_describe_variable(p, n, &p->analysis->symbols[variable], true);
    }
    if (snapshot) {
        source_view_describe_variable(p, n, snapshot, false);
        char description[512];
        snprintf(
            description,
            sizeof description,
            "%s Captures its value in local '%s' when this step runs.",
            n->desc->description,
            p->analysis->symbols[s->symbol].name
        );
        source_view_describe(p, n, description);
        p->expressions[s->expression] = n;
    }
    if (s->symbol >= 0) {
        n->reference_id = p->analysis->symbols[s->symbol].id;
    }
    source_view_origin(p, SOURCE_PRESENTATION_STATEMENT, index, s->range, n, 0, 0);
    if (snapshot) {
        source_view_origin(
            p,
            SOURCE_PRESENTATION_EXPRESSION,
            s->expression,
            p->analysis->expressions[s->expression].range,
            n,
            0,
            snapshot->capability
        );
    }
    if (destination) {
        source_view_origin(
            p,
            SOURCE_PRESENTATION_EXPRESSION,
            s->expression,
            destination->range,
            n,
            0,
            destination->capability
        );
    }
    for (int output = 0; output < 2; output++) {
        int count = output ? n->output_count : first_value;
        for (int i = 0; i < count; i++) {
            int record = index;
            if (output) {
                if (n->output_pins[i].kind != PIN_EXEC) {
                    continue;
                }
                if (s->kind == SCRIPT_STMT_BREAK || s->kind == SCRIPT_STMT_CONTINUE ||
                    s->kind == SCRIPT_STMT_GOTO) {
                    continue;
                }
                if (s->kind == SCRIPT_STMT_IF) {
                    record = i ? s->alternative : s->body;
                } else if (loop && !i) {
                    record = s->body;
                } else if (s->kind == SCRIPT_STMT_MATCH) {
                    record = i < s->child_count
                               ? p->analysis->statements[s->children[i]].body
                               : -1;
                }
            }
            if (record >= 0) {
                source_view_origin(
                    p,
                    SOURCE_PRESENTATION_STATEMENT,
                    record,
                    p->analysis->statements[record].range,
                    n,
                    output ? n->output_pins[i].id : n->input_pins[i].id,
                    0
                );
            }
        }
    }
    for (int i = 0; i < acount(values); i++) {
        node *value = source_view_expression(p, values[i]);
        if (value->output_count && value->output_pins[0].kind == PIN_DATA) {
            source_view_wire(p, value, 0, n, first_value + i, GRAPH_EDGE_DATA, 0);
        }
        source_view_origin(
            p,
            SOURCE_PRESENTATION_EXPRESSION,
            values[i],
            p->analysis->expressions[values[i]].range,
            n,
            n->input_pins[first_value + i].id,
            p->analysis->expressions[values[i]].capability
        );
    }
    afree(values);
    return n;
}
/* A lazy expression stays one source operation on the exec spine: expanding
 * both operands into a chain would incorrectly promise eager evaluation. */
static int receiver(const script_expression *e)
{
    for (int i = 0;
         (e->kind == SCRIPT_EXPR_CALL || e->kind == SCRIPT_EXPR_ARRAY_MUTATE) &&
         i < e->argument_count;
         i++) {
        if (e->arguments[i].value == e->left) {
            return -1;
        }
    }
    return e->left;
}
static source_flow_target expression_flow(source_projection *p, int index, source_flow_target next, int cached)
{
    if (index < 0 || index == cached) {
        return next;
    }
    const script_expression *e = &p->analysis->expressions[index];
    if (!(e->effects & ~(SCRIPT_EFFECT_READ | SCRIPT_EFFECT_INVOCATION_READ))) {
        return next;
    }
    bool lazy = e->kind == SCRIPT_EXPR_FALLBACK ||
                (e->kind == SCRIPT_EXPR_BINARY &&
                 (e->op == SCRIPT_OP_AND || e->op == SCRIPT_OP_OR ||
                  e->op == SCRIPT_OP_COALESCE));
    if (lazy || source_view_expression_action(p, index)) {
        node *n = source_view_expression(p, index);
        int input = exec_pin(n, false, NULL);
        if (input < 0) {
            pin_spec pin = source_view_flow_pin(p, "Exec");
            input = graph_add_pin(p->graph, n, false, &pin);
            assert(input >= 0);
        }
        const char *continuation = e->awaited || e->kind == SCRIPT_EXPR_JOIN
                                     ? "Completed"
                                     : "Then";
        int output = exec_pin(n, true, continuation);
        if (output < 0 && e->awaited) {
            output = exec_pin(n, true, NULL);
        }
        if (output < 0) {
            pin_spec pin = source_view_flow_pin(p, continuation);
            output = graph_add_pin(p->graph, n, true, &pin);
            assert(output >= 0);
        }
        flow_wire(p, n, output, next);
        next = (source_flow_target){ n, input };
        if (lazy) {
            node_desc *desc = (node_desc *)n->desc;
            desc->name = source_view_own(
                p->graph,
                e->kind == SCRIPT_EXPR_FALLBACK ? "Choose Value" : "Evaluate Condition"
            );
            return next;
        }
    }
    for (int i = e->argument_count - 1; i >= 0; i--) {
        next = expression_flow(p, e->arguments[i].value, next, cached);
    }
    if (e->kind != SCRIPT_EXPR_CALL && e->kind != SCRIPT_EXPR_ARRAY_MUTATE) {
        next = expression_flow(p, e->right, next, cached);
    }
    return expression_flow(p, receiver(e), next, cached);
}
static source_flow_target statement_values_flow(
    source_projection *p,
    const script_statement *s,
    source_flow_target next
)
{
    if (s->kind == SCRIPT_STMT_EXPRESSION && s->expression >= 0) {
        const script_expression *e = &p->analysis->expressions[s->expression];
        if (e->kind == SCRIPT_EXPR_CALL || e->kind == SCRIPT_EXPR_ARRAY_MUTATE ||
            e->kind == SCRIPT_EXPR_JOIN || e->kind == SCRIPT_EXPR_CANCEL ||
            e->kind == SCRIPT_EXPR_YIELD) {
            for (int i = e->argument_count - 1; i >= 0; i--) {
                next = expression_flow(p, e->arguments[i].value, next, -1);
            }
            return expression_flow(p, receiver(e), next, -1);
        }
    }
    for (int i = s->child_count - 1; s->kind == SCRIPT_STMT_RETURN && i >= 0; i--) {
        next = expression_flow(p, p->analysis->statements[s->children[i]].second, next, -1);
    }
    int cached = -1;
    if (s->kind == SCRIPT_STMT_ASSIGN && s->second >= 0) {
        const script_expression *rhs = &p->analysis->expressions[s->second];
        if (rhs->kind == SCRIPT_EXPR_CALL && rhs->argument_count &&
            rhs->arguments[0].value == s->expression) {
            cached = s->expression;
        }
    }
    next = expression_flow(p, s->third, next, -1);
    next = expression_flow(p, s->second, next, cached);
    return expression_flow(p, s->expression, next, -1);
}
/* These edges present source control flow. The graph remains metadata-only;
 * execution and statement edits continue to use the analyzed source records. */
static source_flow_target statement(source_projection *p, int index, source_flow flow)
{
    if (index < 0) {
        return flow.next;
    }
    const script_statement *s = &p->analysis->statements[index];
    if (s->kind == SCRIPT_STMT_BLOCK || s->kind == SCRIPT_STMT_SEQUENCE) {
        for (int i = s->child_count - 1; i >= 0; i--) {
            int child = s->children[i];
            if (s->kind == SCRIPT_STMT_SEQUENCE) {
                child = p->analysis->statements[child].body;
            }
            flow.next = statement(p, child, flow);
        }
        source_view_origin(p, SOURCE_PRESENTATION_STATEMENT, index, s->range, NULL, 0, 0);
        return flow.next;
    }
    if (s->kind == SCRIPT_STMT_CASE) {
        source_view_origin(p, SOURCE_PRESENTATION_STATEMENT, index, s->range, NULL, 0, 0);
        return statement(p, s->body, flow);
    }
    node *n = statement_box(p, index);
    p->statements[index] = n;
    source_flow_target entry = statement_values_flow(
        p,
        s,
        (source_flow_target){ n, exec_pin(n, false, NULL) }
    );
    if (s->kind == SCRIPT_STMT_IF) {
        flow_wire(p, n, 0, statement(p, s->body, flow));
        flow_wire(p, n, 1, statement(p, s->alternative, flow));
        return entry;
    }
    if (s->kind == SCRIPT_STMT_WHILE || s->kind == SCRIPT_STMT_FOR ||
        s->kind == SCRIPT_STMT_FOREACH) {
        source_flow_target repeat = { n, 1 };
        if (s->kind == SCRIPT_STMT_WHILE && s->expression >= 0 &&
            (p->analysis->expressions[s->expression].effects &
             ~(SCRIPT_EFFECT_READ | SCRIPT_EFFECT_INVOCATION_READ))) {
            repeat = entry;
        }
        source_flow body = { .next = repeat, .stop = flow.next, .repeat = repeat };
        flow_wire(p, n, 0, statement(p, s->body, body));
        flow_wire(p, n, 1, flow.next);
        return entry;
    }
    if (s->kind == SCRIPT_STMT_MATCH) {
        bool fallback = false;
        for (int i = 0; i < s->child_count; i++) {
            const script_statement *arm = &p->analysis->statements[s->children[i]];
            fallback |= arm->expression < 0;
            flow_wire(p, n, i, statement(p, s->children[i], flow));
        }
        if (!fallback) {
            flow_wire(p, n, s->child_count, flow.next);
        }
        return entry;
    }
    if (s->kind == SCRIPT_STMT_RETURN || s->kind == SCRIPT_STMT_GOTO) {
        return entry;
    }
    source_flow_target next = s->kind == SCRIPT_STMT_BREAK    ? flow.stop
                            : s->kind == SCRIPT_STMT_CONTINUE ? flow.repeat
                                                              : flow.next;
    const char *name = "Then";
    if (s->kind == SCRIPT_STMT_EXPRESSION && s->expression >= 0) {
        const script_expression *e = &p->analysis->expressions[s->expression];
        if (e->awaited || e->kind == SCRIPT_EXPR_JOIN) {
            name = "Completed";
        }
    }
    int output = exec_pin(n, true, name);
    if (output < 0) {
        output = exec_pin(n, true, NULL);
    }
    if (output >= 0) {
        flow_wire(p, n, output, next);
    }
    return entry;
}
/* The title of a `where` clause's node. */
static const char *query_title(script_query_kind kind)
{
    switch (kind) {
        case SCRIPT_QUERY_HAS: return "Has";
        case SCRIPT_QUERY_ALL: return "All";
        case SCRIPT_QUERY_ANY: return "Any";
        case SCRIPT_QUERY_NOT: return "Not";
        case SCRIPT_QUERY_RELATION: return "Related";
        case SCRIPT_QUERY_GENERATOR: return "Generate";
        case SCRIPT_QUERY_VALUE: return "If";
        case SCRIPT_QUERY_CHANGED: return "Changed";
    }
    assert(false);
    return NULL;
}
node *source_view_query(source_projection *p, int index)
{
    const script_query *q = &p->analysis->queries[index];
    pin_spec *inputs = NULL, *outputs = NULL;
    script_type boolean = { .storage = PIN_BOOL };
    for (int i = 0; i < q->child_count; i++) {
        apush(inputs, source_view_data_pin(p, "Predicate", boolean));
    }
    apush(outputs, source_view_data_pin(p, "Matches", boolean));
    const char *name = query_title(q->kind);
    char *label = NULL;
    for (int i = 0; q->component && i < p->catalog->type_count; i++) {
        const script_catalog_type *type = &p->catalog->types[i];
        if (type->component_id != q->component) {
            continue;
        }
        const char *component = type->source_name ? type->source_name : type->name;
        size_t size = strlen(name) + strlen(component) + 3;
        label = xmalloc(size);
        snprintf(label, size, "%s(%s)", name, component);
        break;
    }
    node *n = source_view_box(p, label ? label : name, NODE_DISPLAY_PURE, inputs, outputs);
    free(label);
    n->reference_id = q->component;
    source_view_origin(p, SOURCE_PRESENTATION_QUERY, index, q->range, n, 0, q->component);
    for (int i = 0; i < q->child_count; i++) {
        source_view_wire(p, source_view_query(p, q->children[i]), 0, n, i, GRAPH_EDGE_DATA, 0);
    }
    return n;
}
void source_view_declaration(source_projection *p, int index)
{
    const script_symbol *s = &p->analysis->symbols[index];
    for (int i = 0; p->function >= 0 && i < p->analysis->statement_count; i++) {
        const script_statement *statement = &p->analysis->statements[i];
        node *n = p->statements[i];
        if (!n || statement->kind != SCRIPT_STMT_DECLARE || statement->symbol != index) {
            continue;
        }
        source_view_origin(p, SOURCE_PRESENTATION_SYMBOL, index, s->declaration, n, 0, s->capability);
        const script_expression *tuple = source_view_tuple(p, s->initializer);
        int result = 0;
        for (int j = 0; j < n->output_count; j++) {
            if (n->output_pins[j].kind == PIN_DATA) {
                source_view_origin(
                    p,
                    SOURCE_PRESENTATION_SYMBOL,
                    index,
                    s->declaration,
                    n,
                    n->output_pins[j].id,
                    s->capability
                );
                source_presentation_origin
                    *origin = &p->out->origins[acount(p->out->origins) - 1];
                origin->type = tuple ? tuple->results[result] : s->type;
                origin->result_index = result++;
            }
        }
        return;
    }
    pin_spec *inputs = NULL, *outputs = NULL;
    const script_expression *initializer = s->initializer >= 0
                                             ? &p->analysis->expressions[s->initializer]
                                             : NULL;
    /* Implicit empty handles and collections reuse the declaration name's
     * range. They have no authored value token to display or edit. */
    bool authored_initial = initializer &&
                            (initializer->range.start != s->declaration.start ||
                             initializer->range.end != s->declaration.end);
    if (authored_initial) {
        apush(
            inputs,
            source_view_data_pin(
                p,
                "Initial",
                p->analysis->expressions[s->initializer].type
            )
        );
    }
    const script_expression *tuple = source_view_tuple(p, s->initializer);
    int count = tuple ? tuple->result_count : 1;
    for (int i = 0; i < count; i++) {
        apush(
            outputs,
            source_view_data_pin(
                p,
                tuple ? source_view_result_name(p, tuple, i) : s->name,
                tuple ? tuple->results[i] : s->type
            )
        );
    }
    node *n = source_view_box(p, s->name, NODE_DISPLAY_VARIABLE, inputs, outputs);
    n->reference_id = s->id;
    n->disabled = s->disabled;
    source_view_origin(p, SOURCE_PRESENTATION_SYMBOL, index, s->declaration, n, 0, s->capability);
    for (int i = 0; i < n->output_count; i++) {
        source_view_origin(
            p,
            SOURCE_PRESENTATION_SYMBOL,
            index,
            s->declaration,
            n,
            n->output_pins[i].id,
            s->capability
        );
        p->out->origins[acount(p->out->origins) - 1].type = tuple
                                                              ? tuple->results[i]
                                                              : s->type;
    }
    if (authored_initial) {
        source_view_origin(
            p,
            SOURCE_PRESENTATION_EXPRESSION,
            s->initializer,
            p->analysis->expressions[s->initializer].range,
            n,
            n->input_pins[0].id,
            s->capability
        );
        node *initial = source_view_expression(p, s->initializer);
        if (initial->output_count && initial->output_pins[0].kind == PIN_DATA) {
            source_view_wire(p, initial, 0, n, 0, GRAPH_EDGE_DATA, 0);
        }
    }
}
static char *function_name(source_projection *p, int index)
{
    const script_function *f = &p->analysis->functions[index];
    const char *label = f->symbol >= 0 ? p->analysis->symbols[f->symbol].name : NULL;
    const script_catalog_capability *event = source_view_capability(p, f->event);
    if (!label && event) {
        label = event->name;
    }
    if (!label && f->parent >= 0 && p->analysis->functions[f->parent].symbol >= 0) {
        label = p->analysis->symbols[p->analysis->functions[f->parent].symbol].name;
    }
    if (!label) {
        label = p->analysis->kind == SCRIPT_PROGRAM_SYSTEM ? "System" : "Handler";
    }
    size_t size = strlen(label) + (f->entry ? strlen(f->entry) : 0) + 32;
    char *name = xmalloc(size);
    snprintf(name, size, "%s%s%s", label, f->entry ? "." : "", f->entry ? f->entry : "");
    if (behavior_asset_find_graph_by_name(p->out->view, name)) {
        snprintf(name, size, "%s%s%s [%d]", label, f->entry ? "." : "", f->entry ? f->entry : "", index + 1);
    }
    return name;
}
void source_view_function_graph(source_projection *p, int index)
{
    const script_function *f = &p->analysis->functions[index];
    char *name = function_name(p, index);
    bool macro = f->kind == SCRIPT_FUNCTION_MACRO_ENTRY ||
                 (f->symbol >= 0 && p->analysis->symbols[f->symbol].kind == SCRIPT_MACRO);
    graph_kind kind = macro                               ? GRAPH_MACRO
                    : f->kind == SCRIPT_FUNCTION_AUTHORED ? GRAPH_FUNCTION
                                                          : GRAPH_EVENT;
    p->graph = behavior_asset_add_graph(p->out->view, kind, name);
    free(name);
    assert(p->graph);
    p->function = index;
    memset(p->expressions, 0, (size_t)p->analysis->expression_count * sizeof *p->expressions);
    memset(p->statements, 0, (size_t)p->analysis->statement_count * sizeof *p->statements);
    pin_spec *inputs = NULL, *outputs = NULL;
    apush(outputs, source_view_flow_pin(p, "Then"));
    const script_catalog_capability *event = f->kind == SCRIPT_FUNCTION_HANDLER
                                               ? source_view_capability(p, f->event)
                                               : NULL;
    for (int i = 0; i < f->parameter_count; i++) {
        const script_symbol *s = &p->analysis->symbols[f->parameters[i]];
        /* A payload field the handler leaves out shows the field's name. */
        const char *pin = !s->name[0] && event && i < event->parameter_count
                            ? event->parameters[i].name
                            : s->name;
        apush(outputs, source_view_data_pin(p, pin, s->type));
    }
    node *entry = source_view_box(p, f->entry ? f->entry : "Entry", NODE_DISPLAY_EVENT, inputs, outputs);
    if (event) {
        entry->reference_name = source_view_own(p->graph, event->name);
    }
    entry->disabled = f->disabled;
    p->graph->entry_id = entry->id;
    source_view_origin(
        p,
        SOURCE_PRESENTATION_FUNCTION,
        index,
        f->range,
        entry,
        0,
        f->event ? f->event : f->action
    );
    source_view_origin(
        p,
        SOURCE_PRESENTATION_FUNCTION,
        index,
        f->range,
        entry,
        entry->output_pins[0].id,
        f->event ? f->event : f->action
    );
    for (int i = 0; i < f->parameter_count; i++) {
        const script_symbol *s = &p->analysis->symbols[f->parameters[i]];
        source_view_origin(
            p,
            SOURCE_PRESENTATION_SYMBOL,
            f->parameters[i],
            s->declaration,
            entry,
            entry->output_pins[i + 1].id,
            s->capability
        );
    }
    node *returned = NULL;
    if (f->kind != SCRIPT_FUNCTION_HANDLER || f->result_count) {
        pin_spec *return_inputs = NULL, *return_outputs = NULL;
        apush(return_inputs, source_view_flow_pin(p, "Exec"));
        for (int i = 0; i < f->result_count; i++) {
            const script_symbol *s = &p->analysis->symbols[f->results[i]];
            apush(return_inputs, source_view_data_pin(p, s->name, s->type));
        }
        for (int i = 0; macro && i < p->analysis->symbol_count; i++) {
            const script_symbol *s = &p->analysis->symbols[i];
            if (s->kind == SCRIPT_LABEL && s->outcome && s->scope == f->scope) {
                apush(return_outputs, source_view_flow_pin(p, s->name));
            }
        }
        returned = source_view_box(p, "Return", NODE_DISPLAY_FLOW, return_inputs, return_outputs);
        p->graph->return_id = returned->id;
        source_view_origin(
            p,
            SOURCE_PRESENTATION_FUNCTION,
            index,
            f->range,
            returned,
            0,
            f->event ? f->event : f->action
        );
        source_view_origin(
            p,
            SOURCE_PRESENTATION_FUNCTION,
            index,
            f->range,
            returned,
            returned->input_pins[0].id,
            f->event ? f->event : f->action
        );
        for (int i = 0; i < f->result_count; i++) {
            const script_symbol *s = &p->analysis->symbols[f->results[i]];
            source_view_origin(
                p,
                SOURCE_PRESENTATION_SYMBOL,
                f->results[i],
                s->declaration,
                returned,
                returned->input_pins[i + 1].id,
                s->capability
            );
            if (s->initializer >= 0) {
                source_view_origin(
                    p,
                    SOURCE_PRESENTATION_EXPRESSION,
                    s->initializer,
                    p->analysis->expressions[s->initializer].range,
                    returned,
                    returned->input_pins[i + 1].id,
                    s->capability
                );
            }
        }
    }
    source_flow flow = { .next = { returned, 0 } };
    flow_wire(p, entry, 0, statement(p, f->body, flow));
    for (int i = 0; i < p->analysis->statement_count; i++) {
        const script_statement *jump = &p->analysis->statements[i];
        if (jump->kind != SCRIPT_STMT_GOTO || !p->statements[i] || jump->symbol < 0) {
            continue;
        }
        for (int j = 0; j < p->analysis->statement_count; j++) {
            const script_statement *label = &p->analysis->statements[j];
            if (label->kind == SCRIPT_STMT_LABEL &&
                label->symbol == jump->symbol && p->statements[j]) {
                flow_wire(p, p->statements[i], 0, (source_flow_target){ p->statements[j], 0 });
                break;
            }
        }
    }
    for (int i = 0; i < p->analysis->symbol_count; i++) {
        const script_symbol *s = &p->analysis->symbols[i];
        if (s->declaration.start < 0 || p->analysis->scopes[s->scope].function != index) {
            continue;
        }
        if (s->kind == SCRIPT_CONSTANT || s->kind == SCRIPT_LOCAL ||
            s->kind == SCRIPT_STATE || s->kind == SCRIPT_SHARED_STATE ||
            s->kind == SCRIPT_ACTION || s->kind == SCRIPT_RESULT ||
            s->kind == SCRIPT_LABEL) {
            source_view_declaration(p, i);
        }
    }
    graph_reindex(p->graph);
}
