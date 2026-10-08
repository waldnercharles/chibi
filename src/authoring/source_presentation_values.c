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
                                   "^",     "<<", ">>", "~" };
    assert(op >= SCRIPT_OP_NONE && op <= SCRIPT_OP_BIT_NOT);
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
        case SCRIPT_EXPR_CALL: return "Call";
        case SCRIPT_EXPR_MEMBER: return "Field";
        case SCRIPT_EXPR_COMPONENT: return "Component";
        case SCRIPT_EXPR_INDEX: return "Index";
        case SCRIPT_EXPR_ARRAY: return "Array";
        case SCRIPT_EXPR_STRUCT: return "Struct";
        case SCRIPT_EXPR_NIL: return "nil";
        case SCRIPT_EXPR_FALLBACK: return "Fallback";
        case SCRIPT_EXPR_STORE: return "Store";
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
    pin_spec *inputs = NULL, *outputs = NULL;
    int *children = NULL, *arguments = NULL;
    /* Call right identifies an authored function, not an expression record. */
    if (e->left >= 0) {
        apush(children, e->left);
        apush(arguments, -1);
    }
    if (e->right >= 0 && e->kind != SCRIPT_EXPR_CALL) {
        apush(children, e->right);
        apush(arguments, -1);
    }
    const script_catalog_capability *capability = source_view_capability(p, e->capability);
    const script_expression *tuple = source_view_tuple(p, index);
    const script_expression *schema = tuple ? tuple : e;
    for (int i = 0; i < e->argument_count; i++) {
        if (e->arguments[i].value >= 0) {
            apush(children, e->arguments[i].value);
            apush(arguments, i);
        }
    }
    for (int i = 0; i < acount(children); i++) {
        const script_expression *child = &p->analysis->expressions[children[i]];
        const char *name = i == 0 ? "Value" : "Operand";
        int arg = arguments[i];
        if (arg >= 0 && capability && e->arguments[arg].parameter >= 0 &&
            e->arguments[arg].parameter < capability->parameter_count) {
            name = capability->parameters[e->arguments[arg].parameter].name;
        } else if (arg >= 0 && e->arguments[arg].member) {
            name = member_name(p, e->arguments[arg].member);
        }
        apush(inputs, source_view_data_pin(p, name, child->type));
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
                                   : 1;
    for (int i = 0; i < count; i++) {
        script_type type = schema->result_count ? schema->results[i] : e->type;
        const char *name = source_view_result_name(p, schema, i);
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
    bool action = e->kind == SCRIPT_EXPR_CALL &&
                  ((capability && capability->kind == SCRIPT_CAPABILITY_ACTION) ||
                   e->awaited);
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
            apush(outputs, source_view_flow_pin(p, "Completed"));
        }
    }
    node *n = source_view_box(
        p,
        e->kind == SCRIPT_EXPR_MEMBER || e->kind == SCRIPT_EXPR_COMPONENT
            ? member_name(p, e->member)
            : expression_name(p, e),
        action                            ? NODE_DISPLAY_FUNCTION
        : e->kind == SCRIPT_EXPR_CONSTANT ? NODE_DISPLAY_LITERAL
                                          : NODE_DISPLAY_PURE,
        inputs,
        outputs
    );
    p->expressions[index] = n;
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
