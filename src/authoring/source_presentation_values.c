#include "authoring/source_presentation_internal.h"
#include "graph/authoring/graph_edit.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *operator_name(script_operator op)
{
    static const char *names[] = { "Value", "+",  "-",  "*",  "/",  "%",
                                   "-",     "!",  "&&", "||", "==", "!=",
                                   "<",     "<=", ">",  ">=", "&",  "|",
                                   "^",     "<<", ">>", "~",  "??" };
    assert(op >= SCRIPT_OP_NONE && op <= SCRIPT_OP_COALESCE);
    return names[op];
}
static const char *expression_name(source_projection *p, const script_expression *e)
{
    if (e->symbol >= 0) {
        return p->analysis->symbols[e->symbol].name;
    }
    const script_catalog_capability *capability = source_view_capability(p, e->capability);
    if (capability) {
        return capability->name;
    }
    switch (e->kind) {
        case SCRIPT_EXPR_ERROR: return "Unresolved";
        case SCRIPT_EXPR_CONSTANT: return "Literal";
        case SCRIPT_EXPR_SYMBOL: return "Value";
        case SCRIPT_EXPR_UNARY:
        case SCRIPT_EXPR_BINARY: return operator_name(e->op);
        case SCRIPT_EXPR_CAST: return "Cast";
        case SCRIPT_EXPR_ADDRESS: return "Address";
        case SCRIPT_EXPR_DEREFERENCE: return "Dereference";
        case SCRIPT_EXPR_CALL: return "Call";
        case SCRIPT_EXPR_ARRAY_MUTATE: return "Mutate Array";
        case SCRIPT_EXPR_SEQUENCE:
            switch (e->member) {
                case SCRIPT_SEQUENCE_SLICE: return "Copy Slice";
                case SCRIPT_SEQUENCE_VIEW: return "Sequence View";
                case SCRIPT_SEQUENCE_CLONE: return "Clone Sequence";
                case SCRIPT_SEQUENCE_LENGTH: return "Sequence Length";
            }
            assert(false);
            return "Sequence";
        case SCRIPT_EXPR_MEMBER: return "Field";
        case SCRIPT_EXPR_COMPONENT: return "Component";
        case SCRIPT_EXPR_RELATION: return "Relationship targets";
        case SCRIPT_EXPR_QUERY: return "Query";
        case SCRIPT_EXPR_QUERY_FIELD: return "Query field";
        case SCRIPT_EXPR_UNION: return "Union";
        case SCRIPT_EXPR_IS: return e->negated ? "is not" : "is";
        case SCRIPT_EXPR_AS: return "as";
        case SCRIPT_EXPR_UNWRAP: return "Matched value";
        case SCRIPT_EXPR_INDEX: return "Index";
        case SCRIPT_EXPR_ARRAY: return "Array";
        case SCRIPT_EXPR_MAP: return "Map";
        case SCRIPT_EXPR_MAP_HAS: return "Map Has";
        case SCRIPT_EXPR_MAP_REMOVE: return "Map Remove";
        case SCRIPT_EXPR_STRUCT: return "Struct";
        case SCRIPT_EXPR_NIL: return "nil";
        case SCRIPT_EXPR_FALLBACK: return "Fallback";
        case SCRIPT_EXPR_YIELD: return "Yield";
        case SCRIPT_EXPR_JOIN: return "Await Task";
        case SCRIPT_EXPR_CANCEL: return "Cancel Task";
    }
    assert(false);
    return "";
}
const script_expression *source_view_tuple(source_projection *p, int index)
{
    for (int depth = 0; index >= 0 && depth < 256; depth++) {
        const script_expression *e = &p->analysis->expressions[index];
        if (e->result_count > 1) {
            return e;
        }
        if (e->kind != SCRIPT_EXPR_SYMBOL || e->symbol < 0) {
            return NULL;
        }
        index = p->analysis->symbols[e->symbol].initializer;
    }
    return NULL;
}
const char *source_view_result_name(source_projection *p, const script_expression *e, int field)
{
    if (e->kind == SCRIPT_EXPR_ARRAY_MUTATE) {
        return "Success";
    }
    const script_catalog_capability *capability = source_view_capability(p, e->capability);
    if (capability && field < capability->result_count) {
        return capability->results[field].name;
    }
    if (e->kind == SCRIPT_EXPR_CALL && e->right >= 0 &&
        field < p->analysis->functions[e->right].result_count) {
        return p->analysis
            ->symbols[p->analysis->functions[e->right].results[field]]
            .name;
    }
    return "Value";
}
static const char *member_name(source_projection *p, uint64_t id)
{
    for (int i = 0; i < p->analysis->symbol_count; i++) {
        if (p->analysis->symbols[i].id == id) {
            return p->analysis->symbols[i].name;
        }
    }
    for (int i = 0; i < p->catalog->type_count; i++) {
        const script_catalog_type *t = &p->catalog->types[i];
        if (t->id == id) {
            return t->source_name ? t->source_name : t->name;
        }
        for (int j = 0; j < t->field_count; j++) {
            if (t->fields[j].id == id) {
                return t->fields[j].name;
            }
        }
    }
    for (int i = 0; i < p->analysis->type_count; i++) {
        const script_catalog_type *type = &p->analysis->types[i];
        for (int j = 0; j < type->field_count; j++) {
            if (type->fields[j].id == id) {
                return type->fields[j].name;
            }
        }
    }
    for (int i = 0; i < p->catalog->capability_count; i++) {
        const script_catalog_capability *c = &p->catalog->capabilities[i];
        for (int j = 0; j < c->parameter_count; j++) {
            if (script_catalog_id("port", c->parameters[j].name) == id) {
                return c->parameters[j].name;
            }
        }
        for (int j = 0; j < c->result_count; j++) {
            if (script_catalog_id("port", c->results[j].name) == id) {
                return c->results[j].name;
            }
        }
    }
    return "Field";
}
static const char *prefixed_name(source_projection *p, const char *prefix, const char *name)
{
    size_t size = strlen(prefix) + strlen(name) + 2;
    char *label = xmalloc(size);
    snprintf(label, size, "%s %s", prefix, name);
    const char *owned = source_view_own(p->graph, label);
    free(label);
    return owned;
}
bool source_view_expression_action(source_projection *p, int index)
{
    const script_expression *e = &p->analysis->expressions[index];
    if (e->kind == SCRIPT_EXPR_ARRAY_MUTATE || e->kind == SCRIPT_EXPR_JOIN ||
        e->kind == SCRIPT_EXPR_CANCEL || e->kind == SCRIPT_EXPR_YIELD) {
        return true;
    }
    if (e->kind != SCRIPT_EXPR_CALL) {
        return false;
    }
    const script_catalog_capability *capability = source_view_capability(p, e->capability);
    return e->awaited ||
           (e->effects & ~(SCRIPT_EFFECT_READ | SCRIPT_EFFECT_INVOCATION_READ)) ||
           (capability && capability->kind == SCRIPT_CAPABILITY_ACTION);
}
static void formula_inputs(source_projection *p, int index, int **children, int **arguments)
{
    if (index < 0) {
        return;
    }
    const script_expression *e = &p->analysis->expressions[index];
    if (e->kind == SCRIPT_EXPR_CALL || e->kind == SCRIPT_EXPR_SEQUENCE ||
        e->kind == SCRIPT_EXPR_ARRAY_MUTATE || e->kind == SCRIPT_EXPR_MEMBER ||
        e->kind == SCRIPT_EXPR_COMPONENT) {
        apush(*children, index);
        apush(*arguments, -1);
        return;
    }
    formula_inputs(p, e->left, children, arguments);
    formula_inputs(p, e->right, children, arguments);
    for (int i = 0; i < e->argument_count; i++) {
        formula_inputs(p, e->arguments[i].value, children, arguments);
    }
}
node *source_view_expression(source_projection *p, int index)
{
    if (index < 0) {
        return NULL;
    }
    assert(index < p->analysis->expression_count);
    if (p->expressions[index]) {
        return p->expressions[index];
    }
    const script_expression *e = &p->analysis->expressions[index];
    /* Lazy effects remain one source operation; exposing both operands would
     * imply unconditional execution of the short-circuited side. */
    bool lazy = (e->effects & ~(SCRIPT_EFFECT_READ | SCRIPT_EFFECT_INVOCATION_READ)) &&
                (e->kind == SCRIPT_EXPR_FALLBACK ||
                 (e->kind == SCRIPT_EXPR_BINARY &&
                  (e->op == SCRIPT_OP_AND || e->op == SCRIPT_OP_OR ||
                   e->op == SCRIPT_OP_COALESCE)));
    /* Keep read-only formulas as one editable source expression. Calls and
     * members retain their ports; operations with effects stay explicit. */
    bool formula = lazy ||
                   ((e->kind == SCRIPT_EXPR_UNARY || e->kind == SCRIPT_EXPR_BINARY ||
                     e->kind == SCRIPT_EXPR_CAST) &&
                    !(e->effects &
                      ~(SCRIPT_EFFECT_READ | SCRIPT_EFFECT_INVOCATION_READ)));
    pin_spec *inputs = NULL, *outputs = NULL;
    int *children = NULL, *arguments = NULL;
    if (formula && !lazy) {
        formula_inputs(p, e->left, &children, &arguments);
        formula_inputs(p, e->right, &children, &arguments);
    }
    /* Call right identifies an authored function, not an expression record. */
    bool receiver_argument = false;
    for (int i = 0;
         (e->kind == SCRIPT_EXPR_CALL || e->kind == SCRIPT_EXPR_ARRAY_MUTATE) &&
         i < e->argument_count;
         i++) {
        receiver_argument |= e->arguments[i].value == e->left;
    }
    bool namespace_receiver = e->kind == SCRIPT_EXPR_MEMBER && e->left >= 0 &&
                              (p->analysis->expressions[e->left].type.flags &
                               SCRIPT_TYPE_NAMESPACE);
    if (!formula && e->left >= 0 && !receiver_argument && !namespace_receiver) {
        apush(children, e->left);
        apush(arguments, -1);
    }
    if (!formula && e->right >= 0 && e->kind != SCRIPT_EXPR_CALL &&
        e->kind != SCRIPT_EXPR_ARRAY_MUTATE) {
        apush(children, e->right);
        apush(arguments, -1);
    }
    const script_catalog_capability *capability = source_view_capability(p, e->capability);
    const script_expression *tuple = source_view_tuple(p, index);
    const script_expression *schema = tuple ? tuple : e;
    for (int i = 0; !lazy && i < e->argument_count; i++) {
        if (e->arguments[i].value >= 0) {
            apush(children, e->arguments[i].value);
            apush(arguments, i);
        }
    }
    for (int i = 0; i < acount(children); i++) {
        const script_expression *child = &p->analysis->expressions[children[i]];
        const char *name = i == 0 ? "Value" : "Operand";
        script_type type = child->type;
        int arg = arguments[i];
        if (e->kind == SCRIPT_EXPR_SEQUENCE) {
            name = children[i] == e->left  ? "Sequence"
                 : children[i] == e->right ? "Start"
                                           : "End";
        } else if (
            arg >= 0 && capability && e->arguments[arg].parameter >= 0 &&
            e->arguments[arg].parameter < capability->parameter_count
        ) {
            name = capability->parameters[e->arguments[arg].parameter].name;
            type = capability->parameters[e->arguments[arg].parameter].type;
        } else if (
            arg >= 0 && e->kind == SCRIPT_EXPR_CALL && e->right >= 0 &&
            e->arguments[arg].parameter >= 0 &&
            e->arguments[arg].parameter < p->analysis->functions[e->right].parameter_count
        ) {
            const script_function *function = &p->analysis->functions[e->right];
            const script_symbol
                *parameter = &p->analysis->symbols
                                  [function->parameters[e->arguments[arg].parameter]];
            name = parameter->name;
            type = parameter->type;
        } else if (arg >= 0 && e->arguments[arg].member) {
            name = member_name(p, e->arguments[arg].member);
        } else if (e->kind == SCRIPT_EXPR_MEMBER) {
            name = "Target";
        } else if (e->kind == SCRIPT_EXPR_COMPONENT) {
            name = "Entity";
        }
        if ((type.flags & (SCRIPT_TYPE_GENERIC | SCRIPT_TYPE_UNTYPED | SCRIPT_TYPE_ERROR)) ||
            (type.storage == PIN_STRUCT && !type.nominal)) {
            unsigned reference = type.flags & SCRIPT_TYPE_REFERENCE;
            type = child->type;
            type.flags |= reference;
        }
        apush(inputs, source_view_data_pin(p, name, type));
    }
    int payload = -1;
    const script_catalog_capability *payload_capability = NULL;
    if (e->type.flags & SCRIPT_TYPE_RESULTS) {
        if (e->symbol >= 0 && p->analysis->symbols[e->symbol].kind == SCRIPT_PARAMETER) {
            payload = p->analysis->symbols[e->symbol].function;
        }
        payload_capability = source_view_capability(p, e->type.nominal);
        if (payload_capability && payload_capability->kind != SCRIPT_CAPABILITY_EVENT) {
            payload_capability = NULL;
        }
    }
    int count = schema->result_count ? schema->result_count
              : payload >= 0 ? p->analysis->functions[payload].parameter_count
              : payload_capability ? payload_capability->parameter_count
              : e->type.storage == PIN_FLOW ? 0
                                            : 1;
    for (int i = 0; i < count; i++) {
        script_type type = schema->result_count ? schema->results[i] : e->type;
        const char *name = source_view_result_name(p, schema, i);
        if (!schema->result_count && e->kind == SCRIPT_EXPR_SYMBOL && e->symbol >= 0) {
            name = p->analysis->symbols[e->symbol].name;
        } else if (!schema->result_count && (e->kind == SCRIPT_EXPR_MEMBER || e->kind == SCRIPT_EXPR_COMPONENT)) {
            name = member_name(p, e->member);
        }
        if (payload >= 0) {
            const script_symbol *
                field = &p->analysis->symbols[p->analysis->functions[payload].parameters[i]];
            type = field->type;
            name = field->name;
        } else if (payload_capability) {
            type = payload_capability->parameters[i].type;
            name = payload_capability->parameters[i].name;
        }
        apush(outputs, source_view_data_pin(p, name, type));
    }
    bool action = source_view_expression_action(p, index);
    if (action) {
        const char *entry = capability && e->entry >= 0 && e->entry < capability->entry_count
                              ? capability->entries[e->entry]
                              : "Exec";
        apush(inputs, source_view_flow_pin(p, entry));
        if (capability && capability->outcome_count) {
            for (int i = 0; i < capability->outcome_count; i++) {
                apush(outputs, source_view_flow_pin(p, capability->outcomes[i]));
            }
        } else {
            apush(outputs, source_view_flow_pin(p, e->awaited || e->kind == SCRIPT_EXPR_JOIN ? "Completed" : "Then"));
        }
    }
    node *n = source_view_box(
        p,
        formula ? "Expression"
        : e->kind == SCRIPT_EXPR_MEMBER || e->kind == SCRIPT_EXPR_COMPONENT
            ? prefixed_name(p, "Get", member_name(p, e->member))
        : e->kind == SCRIPT_EXPR_SYMBOL
            ? prefixed_name(p, "Get", expression_name(p, e))
        : e->kind == SCRIPT_EXPR_CALL && e->awaited
            ? prefixed_name(p, "Await", expression_name(p, e))
            : expression_name(p, e),
        action || e->kind == SCRIPT_EXPR_CALL || e->kind == SCRIPT_EXPR_SEQUENCE
            ? NODE_DISPLAY_FUNCTION
        : e->kind == SCRIPT_EXPR_CONSTANT ? NODE_DISPLAY_LITERAL
        : e->kind == SCRIPT_EXPR_SYMBOL   ? NODE_DISPLAY_VARIABLE
                                          : NODE_DISPLAY_PURE,
        inputs,
        outputs
    );
    p->expressions[index] = n;
    if (e->kind == SCRIPT_EXPR_SYMBOL && e->symbol >= 0) {
        source_view_describe_variable(p, n, &p->analysis->symbols[e->symbol], false);
    } else if (namespace_receiver) {
        char description[512];
        snprintf(
            description,
            sizeof description,
            "Reads the scene entity bound to '%s' in globals.",
            member_name(p, e->member)
        );
        source_view_describe(p, n, description);
    }
    n->reference_id = e->instance ? e->instance : e->capability;
    if (e->symbol >= 0) {
        n->disabled = p->analysis->symbols[e->symbol].disabled;
    }
    source_view_origin(p, SOURCE_PRESENTATION_EXPRESSION, index, e->range, n, 0, e->capability);
    for (int i = 0; i < acount(children); i++) {
        node *child = source_view_expression(p, children[i]);
        int field = 0;
        if (i == 0 && e->kind == SCRIPT_EXPR_MEMBER &&
            p->analysis->expressions[children[i]].type.flags & SCRIPT_TYPE_RESULTS) {
            field = (int)e->offset;
        }
        /* An unresolved draft may not have a resolved tuple field yet. The
         * operand's source origin remains editable without a fabricated edge. */
        if (child && field >= 0 && field < child->output_count) {
            source_view_wire(p, child, field, n, i, GRAPH_EDGE_DATA, 0);
        }
        source_view_origin(
            p,
            SOURCE_PRESENTATION_EXPRESSION,
            children[i],
            p->analysis->expressions[children[i]].range,
            n,
            n->input_pins[i].id,
            p->analysis->expressions[children[i]].capability
        );
        if (arguments[i] >= 0) {
            const script_argument *argument = &e->arguments[arguments[i]];
            source_presentation_origin *o = &p->out->origins[acount(p->out->origins) - 1];
            o->parameter = argument->parameter;
            o->member = argument->member;
        }
    }
    if (e->kind == SCRIPT_EXPR_CONSTANT) {
        int status = graph_set_pin_default(p->graph, n, 1, 0, &e->constant);
        assert(status == 0);
        (void)status;
    }
    for (int i = 0; i < n->output_count; i++) {
        source_view_origin(
            p,
            SOURCE_PRESENTATION_EXPRESSION,
            index,
            e->range,
            n,
            n->output_pins[i].id,
            e->capability
        );
        source_presentation_origin *o = &p->out->origins[acount(p->out->origins) - 1];
        if (i < count) {
            o->type = schema->result_count ? schema->results[i] : e->type;
            if (payload >= 0) {
                o->type = p->analysis
                              ->symbols[p->analysis->functions[payload].parameters[i]]
                              .type;
            } else if (payload_capability) {
                o->type = payload_capability->parameters[i].type;
            }
        }
    }
    afree(children);
    afree(arguments);
    return n;
}
