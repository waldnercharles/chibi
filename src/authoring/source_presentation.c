#include "authoring/source_presentation_details.h"
#include "authoring/source_presentation_internal.h"
#include "graph/authoring/graph_edit.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t generation_mix(uint64_t hash, uint64_t word)
{
    return (hash ^ word) * UINT64_C(1099511628211);
}
static uint64_t generation_type(uint64_t hash, script_type type)
{
    hash = generation_mix(hash, type.storage);
    hash = generation_mix(hash, type.container);
    hash = generation_mix(hash, type.nominal);
    hash = generation_mix(hash, type.parameter);
    hash = generation_mix(hash, type.collection);
    return generation_mix(hash, type.flags);
}
static uint64_t generation_name(uint64_t hash, const char *name)
{
    return generation_mix(hash, name ? script_catalog_id("source-generation-name", name) : 0);
}
static uint64_t semantic_generation(const script_analysis *a, const script_catalog *catalog)
{
    uint64_t hash = script_catalog_id("source-generation-text", a->text);
    hash = generation_name(hash, a->identity);
#define MIX(value) hash = generation_mix(hash, (uint64_t)(value))
#define TYPE(value) hash = generation_type(hash, (value))
#define NAME(value) hash = generation_name(hash, (value))
    MIX(a->kind);
    MIX(a->valid);
    MIX(a->symbol_count);
    MIX(a->expression_count);
    MIX(a->statement_count);
    MIX(a->function_count);
    MIX(a->use_count);
    for (int i = 0; i < a->symbol_count; i++) {
        const script_symbol *s = &a->symbols[i];
        MIX(s->id);
        NAME(s->name);
        NAME(s->unit);
        MIX(s->capability);
        MIX(s->kind);
        TYPE(s->type);
        MIX(s->scope);
        MIX(s->initializer);
        MIX(s->function);
        MIX(s->writable);
    }
    for (int i = 0; i < a->expression_count; i++) {
        const script_expression *e = &a->expressions[i];
        MIX(e->kind);
        MIX(e->op);
        TYPE(e->type);
        MIX(e->range.start);
        MIX(e->range.end);
        MIX(e->capability);
        MIX(e->member);
        MIX(e->read);
        MIX(e->write);
        MIX(e->instance);
        MIX(e->symbol);
        MIX(e->left);
        MIX(e->right);
        MIX(e->entry);
        MIX(e->effects);
        MIX(e->offset);
        MIX(e->writable);
        MIX(e->awaited);
        MIX(e->partial);
        MIX(e->argument_count);
        MIX(e->result_count);
        if (e->kind == SCRIPT_EXPR_MEMBER && e->left >= 0) {
            const script_catalog_type *type = script_catalog_type_by_id(
                catalog,
                a->expressions[e->left].type.nominal
            );
            for (int j = 0; type && j < type->field_count; j++) {
                if (type->fields[j].id == e->member) {
                    NAME(type->fields[j].read_key);
                    break;
                }
            }
        }
        for (int j = 0; j < e->argument_count; j++) {
            MIX(e->arguments[j].value);
            MIX(e->arguments[j].parameter);
            MIX(e->arguments[j].member);
        }
        for (int j = 0; j < e->result_count; j++) {
            TYPE(e->results[j]);
        }
    }
    for (int i = 0; i < a->statement_count; i++) {
        const script_statement *s = &a->statements[i];
        MIX(s->kind);
        MIX(s->range.start);
        MIX(s->range.end);
        MIX(s->scope);
        MIX(s->symbol);
        if (s->kind == SCRIPT_STMT_FOREACH) {
            MIX(s->key_symbol);
        }
        MIX(s->expression);
        MIX(s->second);
        MIX(s->third);
        MIX(s->body);
        MIX(s->alternative);
        MIX(s->op);
        MIX(s->reduction);
        MIX(s->effects);
        MIX(s->child_count);
        for (int j = 0; j < s->child_count; j++) {
            MIX(s->children[j]);
        }
    }
    for (int i = 0; i < a->function_count; i++) {
        const script_function *f = &a->functions[i];
        MIX(f->kind);
        MIX(f->symbol);
        MIX(f->scope);
        MIX(f->body);
        MIX(f->receiver);
        MIX(f->parent);
        MIX(f->event);
        MIX(f->action);
        MIX(f->effects);
        NAME(f->entry);
        MIX(f->parameter_count);
        MIX(f->result_count);
        for (int j = 0; j < f->parameter_count; j++) {
            MIX(f->parameters[j]);
        }
        for (int j = 0; j < f->result_count; j++) {
            MIX(f->results[j]);
        }
    }
    for (int i = 0; i < a->use_count; i++) {
        const script_use *u = &a->uses[i];
        MIX(u->range.start);
        MIX(u->range.end);
        MIX(u->symbol);
        MIX(u->member);
        MIX(u->kind);
    }
    MIX(catalog->type_count);
    for (int i = 0; i < catalog->type_count; i++) {
        const script_catalog_type *type = &catalog->types[i];
        if (type->container) {
            MIX(type->id);
            MIX(type->container);
            TYPE(type->key_type);
            TYPE(type->element_type);
        }
    }
    MIX(catalog->capability_count);
    for (int i = 0; i < catalog->capability_count; i++) {
        const script_catalog_capability *c = &catalog->capabilities[i];
        MIX(c->id);
        NAME(c->name);
        MIX(script_catalog_capability_abi(catalog, c));
        /* Port names are source syntax, excluded from the execution ABI. */
        for (int j = 0; j < c->parameter_count; j++) {
            NAME(c->parameters[j].name);
            NAME(c->parameters[j].unit);
        }
        for (int j = 0; j < c->result_count; j++) {
            NAME(c->results[j].name);
            NAME(c->results[j].unit);
        }
    }
    MIX(catalog->template_count);
    for (int i = 0; i < catalog->template_count; i++) {
        const script_catalog_template *definition = &catalog->templates[i];
        MIX(definition->id);
        MIX(definition->source_generation);
        NAME(definition->name);
        NAME(definition->owner);
        MIX(definition->kind);
        MIX(definition->start);
        MIX(definition->end);
        MIX(definition->generic);
        MIX(definition->parameter_count);
        for (int j = 0; j < definition->parameter_count; j++) {
            const script_template_parameter *parameter = &definition->parameters[j];
            NAME(parameter->name);
            NAME(parameter->binder);
            TYPE(parameter->type);
            MIX(parameter->comptime);
            MIX(parameter->type_value);
            MIX(parameter->binds_type);
        }
    }
    MIX(catalog->specialization_count);
    for (int i = 0; i < catalog->specialization_count; i++) {
        const script_catalog_specialization *specialization = &catalog->specializations[i];
        MIX(specialization->id);
        MIX(specialization->template_id);
        MIX(specialization->executable);
        MIX(specialization->result.is_type);
        if (specialization->result.is_type) {
            TYPE(specialization->result.type);
        }
        MIX(specialization->binding_count);
        for (int j = 0; j < specialization->binding_count; j++) {
            const script_compile_binding *binding = &specialization->bindings[j];
            NAME(binding->name);
            MIX(binding->value.is_type);
            if (binding->value.is_type) {
                TYPE(binding->value.type);
            }
        }
    }
    unsigned char *constants = NULL;
    bool complete = script_catalog_values_key(catalog, &constants);
    for (int i = 0; i < acount(constants); i++) {
        MIX(constants[i]);
    }
    afree(constants);
#undef MIX
#undef TYPE
#undef NAME
    return complete ? hash : 0;
}
bool authoring_source_presentation_matches(
    const authoring_source_presentation *p,
    const script_analysis *a,
    const script_catalog *catalog
)
{
    return p->semantic_generation && !strcmp(p->text, a->text) &&
           !strcmp(p->identity, a->identity) &&
           p->semantic_generation == semantic_generation(a, catalog);
}
uint64_t authoring_source_presentation_call_graph(
    const authoring_source_presentation *p,
    uint64_t graph_id,
    uint64_t node_id
)
{
    int callee = -1;
    for (int i = 0; i < acount(p->origins); i++) {
        const source_presentation_origin *o = &p->origins[i];
        if (o->graph == graph_id && o->node == node_id && !o->pin &&
            o->kind == SOURCE_PRESENTATION_EXPRESSION &&
            o->operation == SCRIPT_EXPR_CALL) {
            callee = o->called_function;
            break;
        }
    }
    if (callee < 0) {
        return 0;
    }
    for (int i = 0; i < acount(p->origins); i++) {
        const source_presentation_origin *o = &p->origins[i];
        if (o->kind != SOURCE_PRESENTATION_FUNCTION || o->record != callee || o->pin) {
            continue;
        }
        const graph *g = behavior_asset_find_graph(p->view, o->graph);
        if (g && g->entry_id == o->node) {
            return g->graph_id;
        }
    }
    return 0;
}
authoring_source_presentation *authoring_source_presentation_new(
    const script_analysis *analysis,
    const script_catalog *catalog,
    const authoring_source_presentation *previous
)
{
    assert(analysis && catalog);
    if (!analysis->text || !analysis->name || !analysis->name[0]) {
        return NULL;
    }
    authoring_source_presentation *out = xcalloc(1, sizeof *out);
    out->references = 1;
    out->semantic_generation = semantic_generation(analysis, catalog);
    out->text = xstrdup(analysis->text);
    out->identity = xstrdup(analysis->identity);
    out->view = behavior_asset_new(analysis->name);
    out->view->domain = analysis->kind == SCRIPT_PROGRAM_LIBRARY
                          ? BEHAVIOR_DOMAIN_LIBRARY
                          : BEHAVIOR_DOMAIN_EVENT;
    source_projection p = {
        .out = out,
        .previous = previous,
        .analysis = analysis,
        .occurrences = script_occurrences_new(analysis),
        .catalog = catalog,
        .function = -1,
        .expressions = xcalloc((size_t)analysis->expression_count + 1, sizeof(node *)),
        .statements = xcalloc((size_t)analysis->statement_count + 1, sizeof(node *))
    };
    for (int i = 0; i < analysis->function_count; i++) {
        source_view_function_graph(&p, i);
    }
    p.graph = out->view->graph;
    p.function = -1;
    memset(p.expressions, 0, (size_t)analysis->expression_count * sizeof *p.expressions);
    memset(p.statements, 0, (size_t)analysis->statement_count * sizeof *p.statements);
    for (int i = 0; i < analysis->symbol_count; i++) {
        const script_symbol *s = &analysis->symbols[i];
        if (s->declaration.start < 0 || s->function >= 0 ||
            analysis->scopes[s->scope].function >= 0) {
            continue;
        }
        if (s->kind == SCRIPT_CONSTANT || s->kind == SCRIPT_STATE ||
            s->kind == SCRIPT_SHARED_STATE || s->kind == SCRIPT_LOCAL ||
            s->kind == SCRIPT_ACTION || s->kind == SCRIPT_EVENT ||
            s->kind == SCRIPT_COMPONENT) {
            source_view_declaration(&p, i);
        }
        if (s->kind == SCRIPT_COMPONENT) {
            const script_catalog_type *type = source_view_type(&p, s->type.nominal);
            if (type && s->type.storage == PIN_POINTER) {
                type = source_view_type(&p, type->element_type.nominal);
            }
            if (type && type->key) {
                behavior_asset_add_component_parameter(
                    out->view,
                    s->name,
                    type->key,
                    (s->type.flags & SCRIPT_TYPE_OPTIONAL) != 0
                );
            }
        }
    }
    if (analysis->query >= 0) {
        source_view_query(&p, analysis->query);
    }
    for (int i = 0; i < analysis->binding_count; i++) {
        const script_binding *b = &analysis->bindings[i];
        pin_spec *inputs = NULL, *outputs = NULL;
        apush(
            inputs,
            source_view_data_pin(&p, "Target", analysis->expressions[b->target].type)
        );
        apush(
            inputs,
            source_view_data_pin(&p, "Value", analysis->expressions[b->value].type)
        );
        node *n = source_view_box(&p, "Bind", NODE_DISPLAY_FLOW, inputs, outputs);
        source_view_origin(&p, SOURCE_PRESENTATION_BINDING, i, b->range, n, 0, 0);
        source_view_wire(&p, source_view_expression(&p, b->target), 0, n, 0, GRAPH_EDGE_DATA, 0);
        source_view_wire(&p, source_view_expression(&p, b->value), 0, n, 1, GRAPH_EDGE_DATA, 0);
    }
    graph_reindex(p.graph);
    free(p.expressions);
    free(p.statements);
    script_occurrences_free(p.occurrences);
    if (previous && !source_presentation_details_carry(previous, out, NULL, 0)) {
        authoring_source_presentation_free(out);
        return NULL;
    }
    return out;
}
static pin_spec clone_pin(graph *g, const pin_spec *pin)
{
    pin_spec copy = *pin;
    if (pin->name) {
        copy.name = source_view_own(g, pin->name);
    }
    if (pin->type_name) {
        copy.type_name = source_view_own(g, pin->type_name);
    }
    if (pin->display_name) {
        copy.display_name = source_view_own(g, pin->display_name);
    }
    if (pin->split_type_name) {
        copy.split_type_name = source_view_own(g, pin->split_type_name);
    }
    if (pin->field_name) {
        copy.field_name = source_view_own(g, pin->field_name);
    }
    /* Source views have no native writable graph binding descriptors. */
    assert(!pin->binding);
    return copy;
}
static node *clone_node(graph *g, const node *source)
{
    node_desc *desc = graph_own_block(g, sizeof *desc);
    *desc = *source->desc;
    desc->name = source_view_own(g, source->desc->name);
    if (source->desc->category) {
        desc->category = source_view_own(g, source->desc->category);
    }
    if (source->desc->description) {
        desc->description = source_view_own(g, source->desc->description);
    }
    desc->input_count = source->input_count;
    desc->output_count = source->output_count;
    desc->inputs = graph_own_block(
        g,
        (size_t)(source->input_count + 1) * sizeof *desc->inputs
    );
    desc->outputs = graph_own_block(
        g,
        (size_t)(source->output_count + 1) * sizeof *desc->outputs
    );
    for (int i = 0; i < source->input_count; i++) {
        desc->inputs[i] = clone_pin(g, &source->input_pins[i]);
    }
    for (int i = 0; i < source->output_count; i++) {
        desc->outputs[i] = clone_pin(g, &source->output_pins[i]);
    }
    node *n = graph_add_node(g, desc);
    assert(n);
    n->id = source->id;
    n->reference_id = source->reference_id;
    if (source->reference_name) {
        n->reference_name = source_view_own(g, source->reference_name);
    }
    if (source->reference_asset) {
        n->reference_asset = source_view_own(g, source->reference_asset);
    }
    n->x = source->x;
    n->y = source->y;
    n->placed = source->placed;
    n->disabled = source->disabled;
    n->presentation = source->presentation;
    n->breakpoint = source->breakpoint;
    n->breakpoint_enabled = source->breakpoint_enabled;
    n->show_advanced = source->show_advanced;
    if (source->comment) {
        graph_set_node_comment(g, n, source->comment, source->comment_hidden);
    }
    for (int output = 0; output < 2; output++) {
        int count = output ? source->output_count : source->input_count;
        const pin_spec *pins = output ? source->output_pins : source->input_pins;
        pin_spec *copies = output ? n->output_pins : n->input_pins;
        pin_spec *templates = output ? desc->outputs : desc->inputs;
        for (int i = 0; i < count; i++) {
            if (pins[i].kind == PIN_DATA) {
                int status = graph_set_pin_default(g, n, output, i, &pins[i].default_value);
                assert(status == 0);
                (void)status;
            }
            copies[i].id = pins[i].id;
            templates[i].default_value = copies[i].default_value;
        }
    }
    return n;
}
authoring_source_presentation *authoring_source_presentation_clone(
    const authoring_source_presentation *source
)
{
    assert(source);
    authoring_source_presentation *out = xcalloc(1, sizeof *out);
    out->references = 1;
    out->text = xstrdup(source->text);
    out->identity = xstrdup(source->identity);
    out->semantic_generation = source->semantic_generation;
    out->view = behavior_asset_new(source->view->name);
    out->view->domain = source->view->domain;
    behavior_asset_set_description(out->view, source->view->description);
    for (int i = 0; i < acount(source->view->graphs); i++) {
        const graph *from = source->view->graphs[i];
        graph *to = i ? behavior_asset_add_graph(out->view, from->kind, from->name)
                      : out->view->graph;
        assert(to);
        to->graph_id = from->graph_id;
        to->kind = from->kind;
        if (from->name) {
            graph_set_metadata(to, from->name, from->description ? from->description : "");
        }
        to->entry_id = from->entry_id;
        to->return_id = from->return_id;
        to->call_depth_limit = from->call_depth_limit;
        to->breakpoints_enabled = from->breakpoints_enabled;
        for (int j = 0; j < acount(from->nodes); j++) {
            clone_node(to, from->nodes[j]);
        }
        for (int j = 0; j < acount(from->edges); j++) {
            apush(to->edges, from->edges[j]);
        }
        for (int j = 0; j < acount(from->comment_boxes); j++) {
            graph_comment_box copy = from->comment_boxes[j];
            if (copy.title) {
                copy.title = source_view_own(to, copy.title);
            }
            apush(to->comment_boxes, copy);
        }
        graph_reindex(to);
    }
    for (int i = 0; i < acount(source->view->component_parameters); i++) {
        const behavior_component_parameter *parameter = &source->view->component_parameters[i];
        behavior_asset_add_component_parameter(
            out->view,
            parameter->name,
            parameter->key,
            parameter->optional
        );
    }
    out->view->graph->id_counter = source->view->graph->id_counter;
    for (int i = 0; i < acount(source->origins); i++) {
        apush(out->origins, source->origins[i]);
    }
    return out;
}
authoring_source_presentation *authoring_source_presentation_retain(authoring_source_presentation *p)
{
    assert(p && p->references > 0);
    p->references++;
    return p;
}
void authoring_source_presentation_free(authoring_source_presentation *p)
{
    if (!p) {
        return;
    }
    assert(p->references > 0);
    if (--p->references) {
        return;
    }
    behavior_asset_free(p->view);
    afree(p->origins);
    free(p->text);
    free(p->identity);
    free(p);
}
behavior_asset *authoring_source_presentation_view(const authoring_source_presentation *p)
{
    return p->view;
}
behavior_asset *authoring_source_presentation_take_view(authoring_source_presentation *p)
{
    assert(p && p->references == 1);
    behavior_asset *view = p->view;
    p->view = NULL;
    authoring_source_presentation_free(p);
    return view;
}
const char *authoring_source_presentation_text(const authoring_source_presentation *p)
{
    return p->text;
}
const char *authoring_source_presentation_identity(const authoring_source_presentation *p)
{
    return p->identity;
}
int authoring_source_presentation_origin_count(const authoring_source_presentation *p)
{
    return acount(p->origins);
}
const source_presentation_origin *authoring_source_presentation_origin_at(
    const authoring_source_presentation *p,
    int i
)
{
    assert(i >= 0 && i < acount(p->origins));
    return &p->origins[i];
}
const source_presentation_origin *authoring_source_presentation_origin(
    const authoring_source_presentation *p,
    uint64_t graph,
    uint64_t node,
    uint64_t pin
)
{
    for (int i = 0; i < acount(p->origins); i++) {
        const source_presentation_origin *o = &p->origins[i];
        if (o->graph == graph && o->node == node && o->pin == pin) {
            return o;
        }
    }
    return NULL;
}
char *authoring_source_presentation_replace(
    const authoring_source_presentation *p,
    const source_presentation_origin *origin,
    const char *text
)
{
    bool found = false;
    for (int i = 0; i < acount(p->origins); i++) {
        found |= origin == &p->origins[i];
    }
    assert(found && text);
    (void)found;
    int start = origin->range.start, end = origin->range.end;
    size_t size = strlen(p->text), added = strlen(text);
    if (start < 0 || end < start || (size_t)end > size) {
        return NULL;
    }
    char *result = xmalloc(size - (size_t)(end - start) + added + 1);
    memcpy(result, p->text, (size_t)start);
    memcpy(result + start, text, added);
    memcpy(result + start + added, p->text + end, size - (size_t)end + 1);
    return result;
}
