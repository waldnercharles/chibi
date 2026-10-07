#include "assets/code_components.h"
#include "assets/code_program_internal.h"
#include "assets/code_source_intelligence.h"
#include "graph/document/events.h"
#include "graph/model/pin_conv.h"
#include "graph/model/registry.h"
#include "graph/nodes/component_nodes.h"
#include "support/array.h"
#include "support/asset_json.h"
#include "support/xalloc.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    parser *parser;
    uint64_t next;
    uint64_t caller;
} emitter;

static code_semantic_type port_type(const pin_spec *port)
{
    return (code_semantic_type){ port->type, port->container, port->type_name };
}
/* Ordinary functions have implicit execution entry/continuation; only their
 * data pins are authored parameters/results. Macro tunnels remain explicit. */
static char *authored_signature(
    const char *name,
    const pin_spec *inputs,
    int input_count,
    const pin_spec *outputs,
    int output_count,
    bool macro
)
{
    if (macro) {
        return code_intelligence_callable_signature(name, inputs, input_count, outputs, output_count);
    }
    pin_spec *in = NULL, *out = NULL;
    for (int i = 0; i < input_count; i++) {
        if (inputs[i].kind == PIN_DATA) {
            apush(in, inputs[i]);
        }
    }
    for (int i = 0; i < output_count; i++) {
        if (outputs[i].kind == PIN_DATA) {
            apush(out, outputs[i]);
        }
    }
    char *signature = code_intelligence_callable_signature(name, in, acount(in), out, acount(out));
    afree(in);
    afree(out);
    return signature;
}

static char *node_signature(const node *native)
{
    if (native->desc == &NODE_CallFunction || native->desc == &NODE_CallPureFunction) {
        return authored_signature(
            native->reference_name ? native->reference_name : native->desc->name,
            native->input_pins,
            native->input_count,
            native->output_pins,
            native->output_count,
            false
        );
    }
    return code_intelligence_callable_signature(
        native->desc->name,
        native->input_pins,
        native->input_count,
        native->output_pins,
        native->output_count
    );
}
static uint64_t member_id(const code_source *source, uint64_t owner, const char *name)
{
    int count;
    const code_semantic_symbol *symbols = code_intelligence_symbols(source, &count);
    for (int i = 0; i < count; i++) {
        if (symbols[i].owner_id == owner &&
            (symbols[i].kind == CODE_SYMBOL_FIELD ||
             symbols[i].kind == CODE_SYMBOL_PARAMETER) &&
            !strcmp(symbols[i].name, name)) {
            return symbols[i].id;
        }
    }
    return 0;
}
static void reflected_fields(emitter *e, scope *scope, declaration *decl, const type_desc *type)
{
    for (int i = 0; type && i < type->field_count; i++) {
        const type_field *field = &type->fields[i];
        if (member_id(e->parser->source, decl->symbol, field->name)) {
            continue;
        }
        code_semantic_symbol symbol = {
            .id = e->next++,
            .owner_id = decl->symbol,
            .kind = CODE_SYMBOL_FIELD,
            .name = field->name,
            .description = field->description,
            .type = { field->type, PIN_CONTAINER_SINGLE, field->type_name },
            .scope_start = scope->start,
            .scope_end = scope->end,
            .graph_id = scope->native->graph_id,
            .node_id = decl->native ? decl->native->id : 0
        };
        code_intelligence_symbol(e->parser->source, &symbol);
    }
}
static void members(emitter *e, scope *scope, declaration *declaration)
{
    node *native = declaration->native;
    char error[256];
    const node_desc *descriptor = native ? native->desc
                                : declaration->call.name
                                    ? code_call_descriptor(declaration->call.name, error, sizeof error)
                                    : NULL;
    bool result = native && native->id == scope->native->return_id;
    const pin_spec *ports = native ? (result ? native->input_pins : native->output_pins)
                          : descriptor ? descriptor->outputs
                                       : NULL;
    int count = native ? (result ? native->input_count : native->output_count)
              : descriptor ? descriptor->output_count
                           : 0;
    for (int i = 0; i < count; i++) {
        const pin_spec *port = &ports[i];
        if (member_id(e->parser->source, declaration->symbol, port->name)) {
            continue;
        }
        code_semantic_symbol symbol = {
            .id = e->next++,
            .owner_id = declaration->symbol,
            .kind = CODE_SYMBOL_FIELD,
            .name = port->name,
            .type = port_type(port),
            .description = descriptor->description,
            .scope_end = (int)strlen(e->parser->source->text),
            .graph_id = scope->native->graph_id,
            .node_id = native ? native->id : 0,
            .pin_id = native ? port->id : 0
        };
        if (native && (native->id == scope->native->entry_id || result)) {
            symbol.kind = CODE_SYMBOL_PARAMETER;
            argument *arguments = result ? scope->outputs : scope->inputs;
            for (int j = 0; j < acount(arguments); j++) {
                if (strcmp(arguments[j].port.name, port->name)) {
                    continue;
                }
                symbol.start = arguments[j].name_start;
                symbol.end = arguments[j].name_end;
                symbol.renameable = symbol.end > symbol.start;
                break;
            }
        }
        symbol.scope_start = scope->start;
        symbol.scope_end = scope->end;
        code_intelligence_symbol(e->parser->source, &symbol);
    }
    if (descriptor && (component_node_kind_of(descriptor) == COMPONENT_NODE_GET ||
                       component_generated_kind(descriptor) == COMPONENT_NODE_GET)) {
        const ecs_component_desc *component = declaration->call.reference
                                                ? component_by_key(
                                                      declaration->call.reference
                                                  )
                                                : NULL;
        if (!component && native) {
            component = component_node_component(native);
        }
        reflected_fields(e, scope, declaration, component ? component->type : NULL);
    }
    if (descriptor == &NODE_Event && declaration->call.reference) {
        const graph_host_event_desc *host = NULL;
        for (int i = 0; i < GRAPH_HOST_EVENT_COUNT; i++) {
            if (graph_host_event_is(declaration->call.reference, i)) {
                host = &GRAPH_HOST_EVENTS[i];
            }
        }
        for (int i = 0; host && i < host->payload_count; i++) {
            const pin_spec *pin = &host->payload[i];
            if (member_id(e->parser->source, declaration->symbol, pin->name)) {
                continue;
            }
            code_semantic_symbol payload = { .id = e->next++,
                                             .owner_id = declaration->symbol,
                                             .kind = CODE_SYMBOL_FIELD,
                                             .name = pin->name,
                                             .type = port_type(pin),
                                             .description = host->description,
                                             .scope_start = scope->start,
                                             .scope_end = scope->end };
            code_intelligence_symbol(e->parser->source, &payload);
        }
        if (host && graph_host_event_is(host->name, GRAPH_HOST_EVENT_TICK) &&
            !member_id(e->parser->source, declaration->symbol, "dt")) {
            code_semantic_symbol delta = {
                .id = e->next++,
                .owner_id = declaration->symbol,
                .definition_id = member_id(e->parser->source, declaration->symbol, "DeltaSeconds"),
                .kind = CODE_SYMBOL_FIELD,
                .name = "dt",
                .type = { .type = PIN_F32 },
                .description = host->description,
                .scope_start = scope->start,
                .scope_end = scope->end
            };
            code_intelligence_symbol(e->parser->source, &delta);
        }
    }
}
static bool token_name(const code_lexer *lexer, code_token token, const char *name)
{
    if (token.kind == CODE_TOKEN_NAME) {
        return code_token_is(lexer, token, name);
    }
    if (token.kind != CODE_TOKEN_STRING) {
        return false;
    }
    size_t length = token.end - token.start;
    char *quoted = xmalloc(length + 1);
    memcpy(quoted, lexer->text + token.start, length);
    quoted[length] = 0;
    av_doc *document = av_json_read(quoted);
    const char *decoded = document ? av_get_str(av_doc_root(document)) : NULL;
    bool matches = decoded && !strcmp(decoded, name);
    av_doc_free(document);
    free(quoted);
    return matches;
}
static uint64_t ecs_fact_id(const code_source *source)
{
    int count;
    const code_semantic_symbol *symbols = code_intelligence_symbols(source, &count);
    uint64_t id = UINT64_C(1) << 63;
    for (int i = 0; i < count; i++) {
        if (symbols[i].id >= id) {
            id = symbols[i].id + 1;
        }
    }
    return id;
}
static uint64_t ecs_root(parser *p, scope *s, const code_lexer *lexer, code_token token, int offset)
{
    int count;
    const code_semantic_symbol *symbols = code_intelligence_symbols(p->source, &count);
    const code_semantic_symbol *best = NULL;
    for (int i = 0; i < count; i++) {
        const code_semantic_symbol *symbol = &symbols[i];
        if (symbol->kind == CODE_SYMBOL_FIELD || symbol->kind == CODE_SYMBOL_LABEL ||
            !token_name(lexer, token, symbol->name) ||
            offset < symbol->scope_start || offset > symbol->scope_end) {
            continue;
        }
        if (!best || symbol->scope_start > best->scope_start) {
            best = symbol;
        }
    }
    if (best) {
        return best->id;
    }
    if (!code_token_is(lexer, token, "self")) {
        return 0;
    }
    code_semantic_symbol self = {
        .id = ecs_fact_id(p->source),
        .kind = CODE_SYMBOL_COMPONENT,
        .name = "self",
        .type = { PIN_HANDLE, PIN_CONTAINER_SINGLE, "ecs.entity" },
        .scope_start = s->start,
        .scope_end = s->end,
        .description = "The entity owning this behavior"
    };
    code_intelligence_symbol(p->source, &self);
    return self.id;
}
static const char *ecs_prototype(parser *p, scope *s, const code_semantic_symbol *root)
{
    node *native = root->node_id ? graph_node_by_id(s->native, root->node_id) : NULL;
    for (int i = 0; !native && i < acount(s->locals); i++) {
        local_declaration *local = &s->locals[i];
        if ((uint64_t)local->start + 1 == root->id && local->has_resolved &&
            local->resolved.kind == CODE_CALL_OUTPUT) {
            native = local->resolved.source.node;
        }
    }
    if (!native || strcmp(native->desc->name, "Spawn Prototype")) {
        return NULL;
    }
    for (int i = 0; i < native->input_count; i++) {
        const pin_spec *pin = &native->input_pins[i];
        if (!strcmp(pin->name, "Prototype")) {
            return pin->default_value.str;
        }
    }
    return NULL;
}
static bool ecs_prototype_has(
    const code_source_context *context,
    const char *name,
    const ecs_component_desc *component
)
{
    for (int i = 0; name && i < context->prototype_count; i++) {
        const code_prototype *prototype = &context->prototypes[i];
        if (strcmp(prototype->name, name)) {
            continue;
        }
        for (int j = 0; j < prototype->component_count; j++) {
            bool ambiguous;
            if (code_component_named(prototype->components[j], &ambiguous) == component) {
                return true;
            }
        }
    }
    return false;
}
static uint64_t ecs_member(
    parser *p,
    scope *s,
    uint64_t owner,
    const char *name,
    code_semantic_type type,
    const char *description,
    int priority
)
{
    uint64_t id = member_id(p->source, owner, name);
    if (id) {
        return id;
    }
    const char *type_name = type.type_name ? type.type_name : pin_type_name(type.type);
    if (type.type == PIN_BOOL) {
        type_name = "bool";
    } else if (type.type >= PIN_I8 && type.type <= PIN_F64) {
        type_name = "number";
    } else if (type.type == PIN_HANDLE && type.type_name && !strcmp(type.type_name, "ecs.entity")) {
        type_name = "entity";
    }
    char signature[256];
    snprintf(signature, sizeof signature, "%s: %s", name, type_name ? type_name : "value");
    code_semantic_symbol symbol = { .id = ecs_fact_id(p->source),
                                    .owner_id = owner,
                                    .kind = CODE_SYMBOL_FIELD,
                                    .name = name,
                                    .signature = signature,
                                    .description = description,
                                    .type = type,
                                    .scope_start = s->start,
                                    .scope_end = s->end,
                                    .completion_priority = priority };
    code_intelligence_symbol(p->source, &symbol);
    return symbol.id;
}
static void ecs_members(parser *p, scope *s, uint64_t owner, const char *prototype, bool self)
{
    const code_semantic_symbol *root = code_intelligence_symbol_by_id(p->source, owner);
    code_semantic_type type = root->type;
    bool entity = type.type == PIN_HANDLE && type.type_name &&
                  !strcmp(type.type_name, "ecs.entity");
    if (entity) {
        for (int i = 0; i < registry_node_count(); i++) {
            const node_desc *desc = registry_node(i);
            if (desc->component_kind != NODE_COMPONENT_GET_SELF) {
                continue;
            }
            const ecs_component_desc *component = desc->component;
            const char *name = code_component_spelling(component);
            bool ambiguous;
            if (code_component_named(name, &ambiguous) != component || ambiguous) {
                continue;
            }
            int known = ecs_prototype_has(&p->source->context, prototype, component);
            int owners = self ? p->source->context.owner_prototype_count : 0;
            for (int j = 0; j < owners; j++) {
                known += ecs_prototype_has(
                    &p->source->context,
                    p->source->context.owner_prototypes[j],
                    component
                );
            }
            if (self && !p->source->context.owner_prototype_count) {
                for (int j = 0; j < p->source->context.prototype_count; j++) {
                    const code_prototype *candidate = &p->source->context.prototypes[j];
                    for (int k = 0; k < candidate->behavior_count; k++) {
                        if (!p->asset->name ||
                            strcmp(candidate->behaviors[k], p->asset->name)) {
                            continue;
                        }
                        owners++;
                        known += ecs_prototype_has(
                            &p->source->context,
                            candidate->name,
                            component
                        );
                        break;
                    }
                }
            }
            int priority = known ? 100 : 0;
            const char *detail = "Optional component on this entity";
            if (known && (!owners || known == owners)) {
                priority = 200;
                detail = "Component present on all known owning prototypes";
            } else if (known) {
                detail = "Component present on some owning prototypes";
            }
            ecs_member(
                p,
                s,
                owner,
                name,
                (code_semantic_type){ PIN_STRUCT,
                                      PIN_CONTAINER_SINGLE,
                                      component->type->name },
                detail,
                priority
            );
        }
        return;
    }
    const type_desc *reflected = type.type == PIN_STRUCT && type.type_name
                                   ? registry_type_by_name(type.type_name)
                                   : NULL;
    for (int i = 0; reflected && i < reflected->field_count; i++) {
        const type_field *field = &reflected->fields[i];
        char name[128];
        code_component_field_spelling(field, name, sizeof name);
        bool ambiguous;
        if (code_component_field(reflected, name, &ambiguous) != field || ambiguous) {
            continue;
        }
        ecs_member(
            p,
            s,
            owner,
            name,
            (code_semantic_type){ field->type, PIN_CONTAINER_SINGLE, field->type_name },
            field->description,
            0
        );
    }
}
static void ecs_events(parser *p, scope *s, uint64_t owner)
{
    const code_semantic_symbol *root = code_intelligence_symbol_by_id(p->source, owner);
    bool ambiguous;
    const ecs_component_desc *component = code_component_named("ActionBinding", &ambiguous);
    if (!component || ambiguous || !root->type.type_name ||
        strcmp(component->type->name, root->type.type_name)) {
        return;
    }
    uint64_t field = member_id(p->source, owner, "Pressed");
    int count;
    const code_semantic_symbol *symbols = code_intelligence_symbols(p->source, &count);
    for (int i = 0; i < count; i++) {
        if (symbols[i].owner_id == owner && symbols[i].kind == CODE_SYMBOL_EVENT &&
            !strcmp(symbols[i].name, "PressedChanged")) {
            return;
        }
    }
    code_semantic_symbol event = {
        .id = ecs_fact_id(p->source),
        .owner_id = owner,
        .definition_id = field,
        .kind = CODE_SYMBOL_EVENT,
        .name = "PressedChanged",
        .signature = "PressedChanged(pressed: bool)",
        .description = "Runs when the action's pressed state changes",
        .scope_start = s->start,
        .scope_end = s->end
    };
    code_intelligence_symbol(p->source, &event);
}
static void complete_ecs_path(parser *p, scope *s, const char *text, int offset, bool event, const code_call_port *expected)

{
    if (!text || !s) {
        return;
    }
    code_lexer lexer;
    code_lex_init(&lexer, text);
    code_token token;
    while ((token = code_lex_next(&lexer)).kind != CODE_TOKEN_END) {
        if (token.kind != CODE_TOKEN_NAME) {
            continue;
        }
        code_lexer tail = lexer;
        code_token separator = code_lex_next(&tail);
        if (separator.punctuation != '.') {
            continue;
        }
        uint64_t owner = ecs_root(p, s, &lexer, token, offset + (int)token.start);
        if (!owner) {
            continue;
        }
        const code_semantic_symbol *root = code_intelligence_symbol_by_id(p->source, owner);
        bool entity = root->type.type == PIN_HANDLE && root->type.type_name &&
                      !strcmp(root->type.type_name, "ecs.entity");
        bool reflected = root->type.type == PIN_STRUCT && root->type.type_name &&
                         registry_type_by_name(root->type.type_name);
        if (!entity && !reflected) {
            continue;
        }
        const char *prototype = ecs_prototype(p, s, root);
        bool self = !strcmp(root->name, "self");
        code_intelligence_use(
            p->source,
            (
                code_semantic_use
            ){ owner, offset + (int)token.start, offset + (int)token.end, CODE_USE_READ }
        );
        while (separator.punctuation == '.') {
            ecs_members(p, s, owner, prototype, self);
            code_token member = code_lex_next(&tail);
            if (event) {
                ecs_events(p, s, owner);
            }
            code_completion_context context = {
                .start = offset + (int)member.start,
                .end = offset + (int)member.end,
                .receiver_id = owner,
                .kinds = CODE_SYMBOL_MASK(CODE_SYMBOL_FIELD) |
                         (event ? CODE_SYMBOL_MASK(CODE_SYMBOL_EVENT) : 0),
                .ecs_member = true,
                .ecs_event = event,
                .typed = expected && expected->type != PIN_FLOW
            };
            if (expected) {
                context.expected = (code_semantic_type){ expected->type,
                                                         expected->container,
                                                         expected->type_name };
            }
            code_intelligence_context(p->source, &context);
            root = code_intelligence_symbol_by_id(p->source, owner);
            pin_spec receiver = { .type = root->type.type,
                                  .container = root->type.container,
                                  .type_name = root->type.type_name };
            pin_spec vector = { .type = PIN_STRUCT, .type_name = "CF_V2" };
            if (pin_compatible(&receiver, &vector)) {
                code_program_complete_vector_method(p, context.start, context.end);
            }
            int count;
            const code_semantic_symbol *symbols = code_intelligence_symbols(p->source, &count);
            uint64_t next = 0;
            size_t length = member.end - member.start;
            char *name = xmalloc(length + 1);
            memcpy(name, tail.text + member.start, length);
            name[length] = 0;
            root = code_intelligence_symbol_by_id(p->source, owner);
            bool entity = root->type.type == PIN_HANDLE;
            bool ambiguous;
            const ecs_component_desc *component = entity && member.kind == CODE_TOKEN_NAME
                                                    ? code_component_named(name, &ambiguous)
                                                    : NULL;
            const type_desc *type = !entity && root->type.type_name
                                      ? registry_type_by_name(root->type.type_name)
                                      : NULL;
            const type_field *field = type && member.kind == CODE_TOKEN_NAME
                                        ? code_component_field(type, name, &ambiguous)
                                        : NULL;
            char spelling[128] = { 0 };
            if (field) {
                code_component_field_spelling(field, spelling, sizeof spelling);
            }
            for (int i = 0; i < count; i++) {
                if (symbols[i].owner_id != owner ||
                    (symbols[i].kind != CODE_SYMBOL_FIELD &&
                     (!event || symbols[i].kind != CODE_SYMBOL_EVENT))) {
                    continue;
                }
                bool component_match = component && symbols[i].type.type_name &&
                                       !strcmp(
                                           symbols[i].type.type_name,
                                           component->type->name
                                       );
                if (token_name(&tail, member, symbols[i].name) || component_match ||
                    (field && !strcmp(spelling, symbols[i].name))) {
                    next = symbols[i].id;
                    break;
                }
            }
            free(name);
            if (!next) {
                break;
            }
            code_intelligence_use(
                p->source,
                (code_semantic_use){ next,
                                     offset + (int)member.start,
                                     offset + (int)member.end,
                                     CODE_USE_READ }
            );
            owner = next;
            prototype = NULL;
            self = false;
            separator = code_lex_next(&tail);
        }
        lexer = tail;
    }
}
void code_program_complete_ecs_path(parser *p, scope *s, const char *text, int offset)
{
    complete_ecs_path(p, s, text, offset, false, NULL);
}
void code_program_complete_ecs_value(parser *p, scope *s, const char *text, int offset, const code_call_port *expected)
{
    complete_ecs_path(p, s, text, offset, false, expected);
}
void code_program_complete_ecs_event(parser *p, scope *s, const char *text, int offset)
{
    complete_ecs_path(p, s, text, offset, true, NULL);
}
/* Names are already resolved by the AST/lowerer. Lexing here only finds byte
 * ranges within its expression; it never constructs or resolves expressions. */
static void expression_uses(emitter *e, scope *scope, const char *text, int start)
{
    if (!text) {
        return;
    }
    code_program_complete_ecs_path(e->parser, scope, text, start);
    e->next = ecs_fact_id(e->parser->source);
    code_lexer lexer;
    code_lex_init(&lexer, text);
    code_token token;
    while ((token = code_lex_next(&lexer)).kind != CODE_TOKEN_END) {
        if (token.kind == CODE_TOKEN_ERROR) {
            break;
        }
        if (token.kind != CODE_TOKEN_NAME && token.kind != CODE_TOKEN_STRING) {
            continue;
        }
        int count;
        const code_semantic_symbol
            *symbols = code_intelligence_symbols(e->parser->source, &count);
        for (int i = 0; i < count; i++) {
            pin_spec receiver = { .type = symbols[i].type.type,
                                  .container = symbols[i].type.container,
                                  .type_name = symbols[i].type.type_name };
            pin_spec vector = { .type = PIN_STRUCT, .type_name = "CF_V2" };
            if (!pin_compatible(&receiver, &vector) ||
                !token_name(&lexer, token, symbols[i].name)) {
                continue;
            }
            code_lexer tail = lexer;
            if (code_lex_next(&tail).punctuation == '.') {
                code_token member = code_lex_next(&tail);
                code_program_complete_vector_method(
                    e->parser,
                    start + (int)member.start,
                    start + (int)member.end
                );
            }
        }
        for (int i = 0; i < acount(scope->declarations); i++) {
            declaration *decl = scope->declarations[i];
            if (!decl->symbol || !token_name(&lexer, token, decl->name)) {
                continue;
            }
            code_lexer tail = lexer;
            code_token separator = code_lex_next(&tail);
            if (separator.punctuation != '.' && separator.punctuation != '[') {
                continue;
            }
            code_token member = code_lex_next(&tail);
            uint64_t resolved = 0;
            int symbol_count;
            const code_semantic_symbol
                *symbols = code_intelligence_symbols(e->parser->source, &symbol_count);
            for (int j = 0; j < symbol_count; j++) {
                if (symbols[j].owner_id == decl->symbol &&
                    (symbols[j].kind == CODE_SYMBOL_FIELD ||
                     symbols[j].kind == CODE_SYMBOL_PARAMETER) &&
                    token_name(&tail, member, symbols[j].name)) {
                    resolved = symbols[j].id;
                    break;
                }
            }
            code_intelligence_use(
                e->parser->source,
                (code_semantic_use){ decl->symbol,
                                     start + (int)token.start,
                                     start + (int)token.end,
                                     CODE_USE_READ }
            );
            if (resolved) {
                code_intelligence_use(
                    e->parser->source,
                    (code_semantic_use){ resolved,
                                         start + (int)member.start,
                                         start + (int)member.end,
                                         CODE_USE_READ }
                );
            }
            code_completion_context context = {
                .start = start + (int)member.start,
                .end = start + (int)member.end,
                .receiver_id = decl->symbol,
                .kinds = CODE_SYMBOL_MASK(CODE_SYMBOL_FIELD) |
                         CODE_SYMBOL_MASK(CODE_SYMBOL_PARAMETER)
            };
            code_intelligence_context(e->parser->source, &context);
        }
    }
}
static void argument_facts(emitter *e, scope *scope, argument *arguments)
{
    for (int i = 0; i < acount(arguments); i++) {
        argument *argument = &arguments[i];
        expression_uses(e, scope, argument->text, argument->start);
        if (argument->has_resolved) {
            code_program_resolved_use(
                e->parser,
                scope,
                argument->text,
                argument->start,
                &argument->resolved,
                CODE_USE_READ
            );
        }
    }
}
static uint64_t graph_symbol(emitter *e, const graph *native)
{
    scope *authored = NULL;
    for (int i = 0; i < acount(e->parser->program->scopes); i++) {
        if (e->parser->program->scopes[i]->native == native) {
            authored = e->parser->program->scopes[i];
            break;
        }
    }
    int count;
    const code_semantic_symbol *symbols = code_intelligence_symbols(e->parser->source, &count);
    for (int i = 0; i < count; i++) {
        if (!symbols[i].asset_name && symbols[i].graph_id == native->graph_id &&
            !symbols[i].node_id &&
            (symbols[i].kind == CODE_SYMBOL_FUNCTION ||
             symbols[i].kind == CODE_SYMBOL_MACRO)) {
            return symbols[i].id;
        }
    }
    node *entry = graph_node_by_id(native, native->entry_id);
    node *result = graph_node_by_id(native, native->return_id);
    const char *name = native->name ? native->name : e->parser->asset->name;
    char *detail = authored_signature(
        name,
        entry ? entry->output_pins : NULL,
        entry ? entry->output_count : 0,
        result ? result->input_pins : NULL,
        result ? result->input_count : 0,
        native->kind == GRAPH_MACRO
    );
    code_semantic_symbol symbol = {
        .id = authored && authored->symbol ? authored->symbol : e->next++,
        .kind = native->kind == GRAPH_MACRO ? CODE_SYMBOL_MACRO : CODE_SYMBOL_FUNCTION,
        .name = name,
        .signature = detail,
        .graph_id = native->graph_id,
        .start = authored ? authored->name_start : 0,
        .end = authored ? authored->name_end : 0,
        .renameable = authored && authored->name_end > authored->name_start,
        .scope_end = (int)strlen(e->parser->source->text)
    };
    code_intelligence_symbol(e->parser->source, &symbol);
    free(detail);
    return symbol.id;
}
static uint64_t native_target(emitter *e, node *native)
{
    if (!native) {
        return 0;
    }
    bool function = native->desc == &NODE_CallFunction ||
                    native->desc == &NODE_CallPureFunction ||
                    native->desc == &NODE_CallMacro;
    for (int i = 0; i < acount(e->parser->program->scopes); i++) {
        graph *target = e->parser->program->scopes[i]->native;
        if (function && !native->reference_asset &&
            native->reference_id == target->graph_id) {
            return graph_symbol(e, target);
        }
    }
    char *label;
    if (function && native->reference_name) {
        size_t size = strlen(native->reference_name) +
                      (native->reference_asset ? strlen(native->reference_asset) + 1
                                               : 0) +
                      1;
        label = xmalloc(size);
        snprintf(
            label,
            size,
            "%s%s%s",
            native->reference_asset ? native->reference_asset : "",
            native->reference_asset ? "." : "",
            native->reference_name
        );
    } else {
        label = xstrdup(native->desc->name);
    }
    const char *asset_name = function ? native->reference_asset : NULL;
    uint64_t graph_id = asset_name ? native->reference_id : 0;
    code_symbol_kind kind = native->desc == &NODE_CallMacro ? CODE_SYMBOL_MACRO
                                                            : CODE_SYMBOL_FUNCTION;
    int count;
    const code_semantic_symbol *symbols = code_intelligence_symbols(e->parser->source, &count);
    for (int i = 0; i < count; i++) {
        bool same_asset = asset_name && symbols[i].asset_name
                            ? !strcmp(asset_name, symbols[i].asset_name)
                            : !asset_name && !symbols[i].asset_name;
        if (symbols[i].kind == kind && same_asset && symbols[i].graph_id == graph_id &&
            !symbols[i].owner_id && !strcmp(symbols[i].name, label)) {
            free(label);
            return symbols[i].id;
        }
    }
    char *detail = function ? authored_signature(
                                  label,
                                  native->input_pins,
                                  native->input_count,
                                  native->output_pins,
                                  native->output_count,
                                  native->desc == &NODE_CallMacro
                              )
                            : code_intelligence_callable_signature(
                                  label,
                                  native->input_pins,
                                  native->input_count,
                                  native->output_pins,
                                  native->output_count
                              );
    code_semantic_symbol symbol = { .id = e->next++,
                                    .kind = kind,
                                    .name = label,
                                    .asset_name = asset_name,
                                    .graph_id = graph_id,
                                    .signature = detail,
                                    .description = native->desc->description,
                                    .scope_end = (int)strlen(e->parser->source->text) };
    code_intelligence_symbol(e->parser->source, &symbol);
    free(detail);
    free(label);
    return symbol.id;
}
void code_program_resolved_use(
    parser *p,
    scope *s,
    const char *atom,
    int offset,
    const code_call_argument *value,
    unsigned role
)
{
    if (!atom || !value) {
        return;
    }
    uint64_t target = 0;
    const char *name = NULL;
    if (value->kind == CODE_CALL_BINDING && value->binding.kind == PIN_BINDING_VARIABLE) {
        for (int i = 0; i < acount(p->program->states); i++) {
            state_declaration *state = &p->program->states[i];
            if (state->native_id == value->binding.variable_id) {
                target = state->symbol;
                name = state->name;
                break;
            }
        }
    } else if (value->kind == CODE_CALL_OUTPUT) {
        for (int i = 0; i < acount(s->declarations); i++) {
            declaration *decl = s->declarations[i];
            if (decl->native == value->source.node) {
                if (!decl->symbol) {
                    decl->symbol = (UINT64_C(1) << 63) | decl->native->id;
                    code_semantic_symbol implicit = { .id = decl->symbol,
                                                      .name = decl->name,
                                                      .kind = CODE_SYMBOL_LOCAL,
                                                      .scope_start = s->start,
                                                      .scope_end = s->end,
                                                      .graph_id = s->native->graph_id,
                                                      .node_id = decl->native->id };
                    code_intelligence_symbol(p->source, &implicit);
                }
                target = decl->symbol;
                name = decl->name;
                break;
            }
        }
    } else if (value->kind == CODE_CALL_BINDING && value->binding.kind == PIN_BINDING_EVENT_OUTPUT) {
        for (int i = 0; i < acount(s->declarations); i++) {
            declaration *decl = s->declarations[i];
            if (decl->native && decl->native->id == value->binding.event_node_id) {
                if (!decl->symbol) {
                    decl->symbol = (UINT64_C(1) << 63) | decl->native->id;
                    code_semantic_symbol implicit = { .id = decl->symbol,
                                                      .name = decl->name,
                                                      .kind = CODE_SYMBOL_LOCAL,
                                                      .scope_start = s->start,
                                                      .scope_end = s->end,
                                                      .graph_id = s->native->graph_id,
                                                      .node_id = decl->native->id };
                    code_intelligence_symbol(p->source, &implicit);
                }
                target = decl->symbol;
                name = decl->name;
                break;
            }
        }
    }
    for (int i = 0; i < acount(s->locals); i++) {
        local_declaration *local = &s->locals[i];
        bool matched = value->kind == CODE_CALL_BINDING &&
                       value->binding.kind == PIN_BINDING_LOCAL &&
                       value->binding.local_id == local->id;
        matched |= value->kind == CODE_CALL_OUTPUT && local->has_resolved &&
                   local->resolved.kind == CODE_CALL_OUTPUT &&
                   local->resolved.source.node == value->source.node &&
                   local->resolved.source.pin == value->source.pin;
        if (!matched) {
            continue;
        }
        code_lexer lexer;
        code_lex_init(&lexer, atom);
        code_token token;
        while ((token = code_lex_next(&lexer)).kind != CODE_TOKEN_END) {
            if (token.kind == CODE_TOKEN_ERROR) {
                break;
            }
            if (token_name(&lexer, token, local->name)) {
                code_intelligence_use(
                    p->source,
                    (code_semantic_use){ (uint64_t)local->start + 1,
                                         offset + (int)token.start,
                                         offset + (int)token.end,
                                         role }
                );
                break;
            }
        }
    }
    const pin_spec *port = NULL;
    node *native = NULL;
    if (value->kind == CODE_CALL_OUTPUT && value->source.node) {
        native = value->source.node;
        if (value->source.pin >= 0 && value->source.pin < native->output_count) {
            port = &native->output_pins[value->source.pin];
            if (port->field_source) {
                for (int i = 0; i < native->output_count; i++) {
                    if (native->output_pins[i].id == port->field_source) {
                        port = &native->output_pins[i];
                        break;
                    }
                }
            }
        }
    }
    if (native && port &&
        (component_node_kind_of(native->desc) == COMPONENT_NODE_GET ||
         component_generated_kind(native->desc) == COMPONENT_NODE_GET)) {
        const code_semantic_symbol *field = code_intelligence_symbol_at(
            p->source,
            offset + (int)strlen(atom) - 1
        );
        if (field && field->kind == CODE_SYMBOL_FIELD) {
            code_intelligence_location(
                p->source,
                field->id,
                s->native->graph_id,
                native->id,
                native->output_pins[value->source.pin].id
            );
        }
    }
    if (target && port) {
        uint64_t member = member_id(p->source, target, port->name);
        if (!member) {
            code_semantic_symbol symbol = { .id = (UINT64_C(1) << 63) | port->id,
                                            .owner_id = target,
                                            .kind = native->id == s->native->entry_id
                                                      ? CODE_SYMBOL_PARAMETER
                                                      : CODE_SYMBOL_FIELD,
                                            .name = port->name,
                                            .type = port_type(port),
                                            .scope_start = s->start,
                                            .scope_end = s->end,
                                            .graph_id = s->native->graph_id,
                                            .node_id = native->id,
                                            .pin_id = port->id };
            if (symbol.kind == CODE_SYMBOL_PARAMETER) {
                for (int i = 0; i < acount(s->inputs); i++) {
                    if (strcmp(s->inputs[i].port.name, port->name)) {
                        continue;
                    }
                    symbol.start = s->inputs[i].name_start;
                    symbol.end = s->inputs[i].name_end;
                    symbol.renameable = symbol.end > symbol.start;
                    break;
                }
            }
            code_intelligence_symbol(p->source, &symbol);
            member = symbol.id;
        }
        code_lexer lexer;
        code_lex_init(&lexer, atom);
        code_token token;
        while ((token = code_lex_next(&lexer)).kind != CODE_TOKEN_END) {
            if (token.kind == CODE_TOKEN_ERROR) {
                break;
            }
            if (token_name(&lexer, token, port->name)) {
                code_intelligence_use(
                    p->source,
                    (
                        code_semantic_use
                    ){ member, offset + (int)token.start, offset + (int)token.end, role }
                );
                break;
            }
        }
    }
    if (!target && value->kind == CODE_CALL_OUTPUT && value->source.node &&
        (value->source.node->desc == &NODE_CallFunction ||
         value->source.node->desc == &NODE_CallPureFunction ||
         value->source.node->desc == &NODE_CallMacro) &&
        value->source.node->reference_name) {
        emitter emitter = { .parser = p, .next = UINT64_C(1) << 63 };
        int count;
        const code_semantic_symbol *symbols = code_intelligence_symbols(p->source, &count);
        for (int i = 0; i < count; i++) {
            if (symbols[i].id >= emitter.next) {
                emitter.next = symbols[i].id + 1;
            }
        }
        target = native_target(&emitter, value->source.node);
        name = value->source.node->reference_name;
        code_intelligence_call(
            p->source,
            (code_semantic_call){ graph_symbol(&emitter, s->native),
                                  target,
                                  offset,
                                  offset + (int)strlen(atom) }
        );
    }
    if (!target || !name) {
        return;
    }
    code_lexer lexer;
    code_lex_init(&lexer, atom);
    code_token token;
    while ((token = code_lex_next(&lexer)).kind != CODE_TOKEN_END) {
        if (token.kind == CODE_TOKEN_ERROR) {
            break;
        }
        if (!token_name(&lexer, token, name)) {
            continue;
        }
        code_intelligence_use(
            p->source,
            (
                code_semantic_use
            ){ target, offset + (int)token.start, offset + (int)token.end, role }
        );
        break;
    }
}
static uint64_t label_symbol(emitter *e, const scope *scope, const char *name)
{
    uint64_t owner = graph_symbol(e, scope->native);
    int count;
    const code_semantic_symbol *symbols = code_intelligence_symbols(e->parser->source, &count);
    for (int i = 0; name && i < count; i++) {
        if (symbols[i].kind == CODE_SYMBOL_LABEL &&
            symbols[i].owner_id == owner && !strcmp(symbols[i].name, name)) {
            return symbols[i].id;
        }
    }
    return 0;
}
static void statements(emitter *e, scope *scope, statement *body)
{
    for (int i = 0; i < acount(body); i++) {
        statement *statement = &body[i];
        if (statement->kind == CODE_STMT_LABEL || statement->kind == CODE_STMT_GOTO) {
            uint64_t label = label_symbol(e, scope, statement->target);
            if (statement->kind == CODE_STMT_GOTO) {
                if (label && statement->target_end > statement->target_start) {
                    code_intelligence_use(
                        e->parser->source,
                        (code_semantic_use){ label,
                                             statement->target_start,
                                             statement->target_end,
                                             CODE_USE_READ }
                    );
                }
                code_completion_context context = {
                    .start = statement->target_start,
                    .end = statement->target_end,
                    .kinds = CODE_SYMBOL_MASK(CODE_SYMBOL_LABEL)
                };
                code_intelligence_context(e->parser->source, &context);
            }
            continue;
        }
        declaration *decl = statement->target
                              ? code_program_declaration(scope, statement->target)
                              : NULL;
        if (decl && decl->symbol && statement->target_start < statement->target_end) {
            code_intelligence_use(
                e->parser->source,
                (code_semantic_use){ decl->symbol,
                                     statement->target_start,
                                     statement->target_end,
                                     CODE_USE_CALL }
            );
        }
        uint64_t target = statement->kind == CODE_STMT_CALL
                            ? (decl ? decl->symbol : native_target(e, statement->native))
                            : 0;
        if (target && statement->target_start < statement->target_end) {
            if (!decl) {
                code_intelligence_use(
                    e->parser->source,
                    (code_semantic_use){ target,
                                         statement->target_start,
                                         statement->target_end,
                                         CODE_USE_CALL }
                );
            }
            code_intelligence_call(
                e->parser->source,
                (
                    code_semantic_call
                ){ e->caller, target, statement->target_start, statement->target_end }
            );
        }
        argument_facts(e, scope, statement->arguments);
        expression_uses(e, scope, statement->text, statement->value_start);
        expression_uses(e, scope, statement->second, statement->second_start);
        expression_uses(e, scope, statement->third, statement->third_start);
        statements(e, scope, statement->body);
        statements(e, scope, statement->alternate);
    }
}
static char *source_name(const char *name)
{
    if (name && (isalpha((unsigned char)*name) || *name == '_')) {
        const char *p = name + 1;
        while (isalnum((unsigned char)*p) || *p == '_') {
            p++;
        }
        if (!*p && !code_program_reserved(name)) {
            return xstrdup(name);
        }
    }
    av_doc *doc = av_doc_new();
    char *quoted = av_json_write(av_str(doc, name));
    av_doc_free(doc);
    return quoted;
}
static void named_offer(parser *p, int start, const char *name, const char *detail, code_completion_kind kind)
{
    char *insert = kind == CODE_COMPLETE_TYPE && code_program_reserved(name)
                     ? xstrdup(name)
                     : source_name(name);
    code_intelligence_offer(p->source, start, name, detail, insert, kind, NULL);
    free(insert);
}
/* Capture publishes this builtin's target slot before semantic lowering, so
 * a draft ending inside cast<T> still has an authoritative grammar context. */
void code_program_complete_cast(parser *p)
{
    if (!code_token_is(&p->lexer, p->token, "cast")) {
        return;
    }
    parser look = *p;
    code_program_next(&look);
    if (!code_program_take(&look, "<")) {
        return;
    }
    look.numeric_type = true;
    code_program_complete_type(
        &look,
        look.base + (int)look.token.start,
        look.base + (int)look.token.end
    );
}
void code_program_complete_type(parser *p, int start, int end)
{
    code_completion_context context = { .start = start,
                                        .end = end,
                                        .numeric_type = p->numeric_type };
    code_intelligence_context(p->source, &context);
    named_offer(p, start, "number", "Numeric value", CODE_COMPLETE_TYPE);
    const char *types[] = { "bool",   "int8",  "byte",   "int16",  "uint16",
                            "int",    "uint",  "int64",  "uint64", "float",
                            "double", "name",  "string", "entity", "any",
                            "exec",   "flow",  "set",    "map",    "struct",
                            "enum",   "handle" };
    int count = p->numeric_type ? PIN_F64 - PIN_BOOL + 1
                                : (int)(sizeof types / sizeof *types);
    for (int i = 0; i < count; i++) {
        named_offer(p, start, types[i], "Language type", CODE_COMPLETE_TYPE);
    }
    if (p->numeric_type) {
        return;
    }
    for (int i = 0; i < registry_type_count(); i++) {
        char *name = source_name(registry_type(i)->name);
        size_t size = strlen(name) + 9;
        char *insert = xmalloc(size);
        snprintf(insert, size, "struct<%s>", name);
        code_intelligence_offer(
            p->source,
            start,
            registry_type(i)->name,
            "Reflected struct type",
            insert,
            CODE_COMPLETE_TYPE,
            NULL
        );
        free(name);
        free(insert);
    }
    for (int i = 0; i < registry_enum_count(); i++) {
        char *name = source_name(registry_enum(i)->name);
        size_t size = strlen(name) + 7;
        char *insert = xmalloc(size);
        snprintf(insert, size, "enum<%s>", name);
        code_intelligence_offer(
            p->source,
            start,
            registry_enum(i)->name,
            "Reflected enum type",
            insert,
            CODE_COMPLETE_TYPE,
            NULL
        );
        free(name);
        free(insert);
    }
    for (int i = 0; i < acount(p->type_parameters); i++) {
        named_offer(p, start, p->type_parameters[i].name, "Function type parameter", CODE_COMPLETE_TYPE);
    }
}
void code_program_complete_event(parser *p, int start, int end)
{
    code_completion_context context = { .start = start,
                                        .end = end,
                                        .kinds = CODE_SYMBOL_MASK(CODE_SYMBOL_EVENT) };
    code_intelligence_context(p->source, &context);
    for (int i = 0; i < GRAPH_HOST_EVENT_COUNT; i++) {
        named_offer(p, start, GRAPH_HOST_EVENTS[i].name, "Host event", CODE_COMPLETE_FUNCTION);
    }
    for (int i = 0; i < p->source->context.event_count; i++) {
        named_offer(p, start, p->source->context.events[i]->name, "Project event", CODE_COMPLETE_FUNCTION);
    }
}
static void callable_proposals(parser *p, int start, const char *asset_name);
void code_program_complete_vector_method(parser *p, int start, int end)
{
    code_completion_context context = { .start = start, .end = end };
    code_intelligence_context(p->source, &context);
    code_semantic_type result = { .type = PIN_STRUCT, .type_name = "CF_V2" };
    code_intelligence_offer(p->source, start, "norm", "norm() -> vec2", "norm()", CODE_COMPLETE_FUNCTION, &result);
}

void code_program_complete_value(parser *p, scope *s, int start, int end, const code_call_port *expected)
{
    (void)s;
    if (expected && expected->reference_kind) {
        code_completion_context context = { .start = start,
                                            .end = end,
                                            .reference_value = true };
        code_intelligence_context(p->source, &context);
        if (expected->reference_kind == CODE_REFERENCE_COMPONENT) {
            for (int i = 0; i < registry_node_count(); i++) {
                const node_desc *desc = registry_node(i);
                if (desc->component_kind == NODE_COMPONENT_GET_SELF) {
                    named_offer(p, start, desc->component->name, "Component", CODE_COMPLETE_TYPE);
                }
            }
        }
        for (int i = 0; i < p->source->context.reference_count; i++) {
            const code_reference *ref = &p->source->context.references[i];
            if (ref->kind == expected->reference_kind) {
                named_offer(p, start, ref->name, "Prototype", CODE_COMPLETE_VALUE);
            }
        }
        return;
    }
    code_completion_context context = {
        .start = start,
        .end = end,
        .kinds = CODE_SYMBOL_MASK(CODE_SYMBOL_STATE) |
                 CODE_SYMBOL_MASK(CODE_SYMBOL_LOCAL) |
                 CODE_SYMBOL_MASK(CODE_SYMBOL_PARAMETER) |
                 CODE_SYMBOL_MASK(CODE_SYMBOL_COMPONENT) |
                 CODE_SYMBOL_MASK(CODE_SYMBOL_EVENT),
        .typed = expected && !(expected->flags & PIN_FLAG_WILDCARD) &&
                 expected->type != PIN_FLOW
    };
    if (expected) {
        context.expected = (code_semantic_type){ expected->type,
                                                 expected->container,
                                                 expected->type_name };
    }
    if (!context.typed) {
        context.kinds |= CODE_SYMBOL_MASK(CODE_SYMBOL_FUNCTION) |
                         CODE_SYMBOL_MASK(CODE_SYMBOL_MACRO);
        context.native_callables = true;
    }
    code_intelligence_context(p->source, &context);
    if (!context.typed) {
        callable_proposals(p, start, NULL);
    }
    if (expected && expected->container == PIN_CONTAINER_SINGLE &&
        expected->type >= PIN_BOOL && expected->type <= PIN_F64) {
        const char *type = pin_type_name(expected->type);
        char label[48], insert[49], signature[128];
        snprintf(label, sizeof label, "cast<%s>", type);
        snprintf(insert, sizeof insert, "cast<%s>(", type);
        snprintf(signature, sizeof signature, "cast<%s>(value: numeric) -> %s", type, type);
        code_intelligence_offer(
            p->source,
            start,
            label,
            signature,
            insert,
            CODE_COMPLETE_FUNCTION,
            &context.expected
        );
    } else if (!context.typed) {
        code_intelligence_offer(
            p->source,
            start,
            "cast<T>",
            "Scalar numeric conversion: cast<T>(value)",
            "cast<",
            CODE_COMPLETE_FUNCTION,
            NULL
        );
    }
    if (!expected || expected->type == PIN_BOOL) {
        code_semantic_type boolean = { .type = PIN_BOOL };
        code_intelligence_offer(p->source, start, "true", "Boolean literal", "true", CODE_COMPLETE_VALUE, &boolean);
        code_intelligence_offer(p->source, start, "false", "Boolean literal", "false", CODE_COMPLETE_VALUE, &boolean);
    }
    if (!expected || expected->type == PIN_HANDLE) {
        code_semantic_type entity = { .type = PIN_HANDLE,
                                      .type_name = expected ? expected->type_name
                                                            : "ecs.entity" };
        code_intelligence_offer(p->source, start, "self", "Current owner entity", "self", CODE_COMPLETE_VALUE, &entity);
    }
    if (expected && expected->type == PIN_ENUM && expected->type_name) {
        const enum_desc *enumeration = registry_enum_by_name(expected->type_name);
        for (int i = 0; enumeration && i < enumeration->value_count; i++) {
            const char *label = enumeration->values[i].label
                                  ? enumeration->values[i].label
                                  : enumeration->values[i].name;
            av_doc *doc = av_doc_new();
            char *quoted = av_json_write(av_str(doc, label));
            code_intelligence_offer(
                p->source,
                start,
                label,
                enumeration->name,
                quoted,
                CODE_COMPLETE_VALUE,
                &context.expected
            );
            free(quoted);
            av_doc_free(doc);
        }
    }
}
static const pin_spec *call_ports(parser *p, const code_call_spec *call, int *count)
{
    *count = 0;
    if (!call || !call->name) {
        return NULL;
    }
    char error[256];
    const node_desc *desc = code_call_descriptor(call->name, error, sizeof error);
    if (desc) {
        *count = desc->input_count;
        return desc->inputs;
    }
    const char *asset;
    const graph *target = code_source_context_graph(
        &p->source->context,
        p->asset,
        call->reference_asset,
        call->name,
        GRAPH_FUNCTION,
        &asset,
        error,
        sizeof error
    );
    if (!target) {
        target = code_source_context_graph(
            &p->source->context,
            p->asset,
            call->reference_asset,
            call->name,
            GRAPH_MACRO,
            &asset,
            error,
            sizeof error
        );
    }
    const node *entry = target ? graph_node_by_id(target, target->entry_id) : NULL;
    *count = entry ? entry->output_count : 0;
    return entry ? entry->output_pins : NULL;
}
static bool unique_port_name(const pin_spec *ports, int count, int index)
{
    for (int i = 0; i < count; i++) {
        if (i != index && ports[i].kind == PIN_DATA &&
            !strcmp(ports[i].name, ports[index].name)) {
            return false;
        }
    }
    return true;
}
bool code_program_argument_port(parser *p, const code_call_spec *call, const char *name, code_call_port *out)
{
    int count;
    const pin_spec *ports = call_ports(p, call, &count);
    for (int i = 0; i < count; i++) {
        if (strcmp(ports[i].name, name)) {
            continue;
        }
        if (!unique_port_name(ports, count, i)) {
            return false;
        }
        *out = (code_call_port){ .name = ports[i].name,
                                 .type = ports[i].type,
                                 .type_name = ports[i].type_name,
                                 .container = ports[i].container,
                                 .kind = ports[i].kind,
                                 .flags = ports[i].flags };
        char error[256];
        const node_desc *desc = call && !call->target
                                  ? code_call_descriptor(call->name, error, sizeof error)
                                  : NULL;
        out->reference_kind = code_call_reference_kind(desc, ports[i].name);
        return true;
    }
    return false;
}
bool code_program_positional_port(parser *p, const code_call_spec *call, int ordinal, code_call_port *out)
{
    int count;
    const pin_spec *ports = call_ports(p, call, &count);
    for (int i = 0; i < count; i++) {
        if (ports[i].kind != PIN_DATA || ports[i].field_source) {
            continue;
        }
        if (!ordinal--) {
            return code_program_argument_port(p, call, ports[i].name, out);
        }
    }
    return false;
}

void code_program_complete_arguments(parser *p, const code_call_spec *call, int start, int end)
{
    if (!call) {
        return;
    }
    code_completion_context context = { .start = start, .end = end };
    code_intelligence_context(p->source, &context);
    int count;
    const pin_spec *ports = call_ports(p, call, &count);
    for (int i = 0; i < count; i++) {
        if (ports[i].kind != PIN_DATA || !unique_port_name(ports, count, i)) {
            continue;
        }
        named_offer(
            p,
            start,
            ports[i].name,
            ports[i].type_name ? ports[i].type_name : pin_type_name(ports[i].type),
            CODE_COMPLETE_FIELD
        );
    }
}

static void callable_proposals(parser *p, int start, const char *asset_name)
{
    for (int i = 0; i < p->source->context.library_count; i++) {
        const behavior_asset *asset = p->source->context.libraries[i];
        if (asset_name && strcmp(asset_name, asset->name)) {
            continue;
        }
        for (int j = 0; j < acount(asset->graphs); j++) {
            const graph *g = asset->graphs[j];
            if (g->kind != GRAPH_FUNCTION && g->kind != GRAPH_MACRO) {
                continue;
            }
            const node *entry = graph_node_by_id(g, g->entry_id);
            const node *result = graph_node_by_id(g, g->return_id);
            char *a = source_name(asset->name), *b = source_name(g->name);
            size_t size = strlen(a) + strlen(b) + 2;
            char *insert = xmalloc(size);
            snprintf(insert, size, asset_name ? "%s%s" : "%s.%s", asset_name ? "" : a, b);
            size_t label_size = strlen(asset->name) + strlen(g->name) + 2;
            char *label = xmalloc(label_size);
            snprintf(
                label,
                label_size,
                asset_name ? "%s%s" : "%s.%s",
                asset_name ? "" : asset->name,
                g->name
            );
            char *detail = authored_signature(
                label,
                entry ? entry->output_pins : NULL,
                entry ? entry->output_count : 0,
                result ? result->input_pins : NULL,
                result ? result->input_count : 0,
                g->kind == GRAPH_MACRO
            );
            if (asset_name) {
                code_intelligence_offer_qualified(p->source, start, label, detail, insert);
            } else {
                code_intelligence_offer(p->source, start, label, detail, insert, CODE_COMPLETE_FUNCTION, NULL);
            }
            free(detail);
            free(label);
            free(insert);
            free(a);
            free(b);
        }
    }
}

/* Parser calls at an actual callable decision, including incomplete tokens. */
static void complete_callable(parser *p, int start, int end, const char *asset_name)
{
    code_completion_context context = {
        .start = start,
        .end = end,
        .native_callables = !asset_name,
        .qualified_callables = asset_name != NULL,
        .kinds = asset_name ? 0
                            : CODE_SYMBOL_MASK(CODE_SYMBOL_FUNCTION) |
                                  CODE_SYMBOL_MASK(CODE_SYMBOL_MACRO)
    };
    code_intelligence_context(p->source, &context);
    if (!asset_name) {
        code_intelligence_offer(
            p->source,
            start,
            "cast<T>",
            "Scalar numeric conversion: cast<T>(value)",
            "cast<",
            CODE_COMPLETE_FUNCTION,
            NULL
        );
    }
    callable_proposals(p, start, asset_name);
}

void code_program_complete_callable(parser *p, int start, int end)
{
    complete_callable(p, start, end, NULL);
}
void code_program_complete_qualified(parser *p, const char *asset, int start, int end)
{
    complete_callable(p, start, end, asset);
}

static scope *local_call_scope(emitter *e, const node *native)
{
    if (!native || native->reference_asset ||
        (native->desc != &NODE_CallFunction && native->desc != &NODE_CallPureFunction &&
         native->desc != &NODE_CallMacro)) {
        return NULL;
    }
    for (int i = 0; i < acount(e->parser->program->scopes); i++) {
        if (e->parser->program->scopes[i]->native->graph_id == native->reference_id) {
            return e->parser->program->scopes[i];
        }
    }
    return NULL;
}
static uint64_t parameter_id(emitter *e, scope *target, bool output, const char *name)
{
    uint64_t node = output ? target->native->return_id : target->native->entry_id;
    for (int i = 0; i < acount(target->declarations); i++) {
        declaration *decl = target->declarations[i];
        if (decl->native && decl->native->id == node) {
            return member_id(e->parser->source, decl->symbol, name);
        }
    }
    return 0;
}
static void call_parameters(emitter *e, scope *target, declaration *decl)
{
    for (int i = 0; i < acount(decl->arguments); i++) {
        argument *arg = &decl->arguments[i];
        uint64_t parameter = parameter_id(e, target, false, arg->port.name);
        if (parameter && arg->name_start < arg->name_end) {
            code_intelligence_use(
                e->parser->source,
                (code_semantic_use){ parameter, arg->name_start, arg->name_end, CODE_USE_CALL }
            );
        }
    }
    for (int i = 0; i < decl->native->output_count; i++) {
        const char *name = decl->native->output_pins[i].name;
        uint64_t parameter = parameter_id(e, target, true, name);
        uint64_t member = member_id(e->parser->source, decl->symbol, name);
        const code_semantic_symbol
            *existing = code_intelligence_symbol_by_id(e->parser->source, member);
        if (!parameter || !existing) {
            continue;
        }
        code_semantic_symbol symbol = *existing;
        symbol.definition_id = parameter;
        code_intelligence_symbol(e->parser->source, &symbol);
    }
}
void code_program_intelligence(parser *p)
{
    emitter e = { .parser = p, .next = UINT64_C(1) << 63 };
    int symbol_count;
    const code_semantic_symbol *symbols = code_intelligence_symbols(p->source, &symbol_count);
    for (int i = 0; i < symbol_count; i++) {
        if (symbols[i].id >= e.next) {
            e.next = symbols[i].id + 1;
        }
    }
    for (int i = 0; i < acount(p->program->scopes); i++) {
        scope *scope = p->program->scopes[i];
        for (int j = 0; j < acount(scope->declarations); j++) {
            if (scope->declarations[j]->symbol >= e.next) {
                e.next = scope->declarations[j]->symbol + 1;
            }
        }
    }
    for (int i = 0; i < acount(p->program->scopes); i++) {
        scope *scope = p->program->scopes[i];
        uint64_t owner = graph_symbol(&e, scope->native);
        for (int j = 0; j < acount(scope->labels); j++) {
            const code_program_label *label = &scope->labels[j];
            const statement *decl = label->statement;
            code_semantic_symbol symbol = { .id = e.next++,
                                            .owner_id = owner,
                                            .kind = CODE_SYMBOL_LABEL,
                                            .name = label->name,
                                            .start = decl->target_start,
                                            .end = decl->target_end,
                                            .scope_start = scope->start,
                                            .scope_end = scope->end,
                                            .graph_id = scope->native->graph_id,
                                            .renameable = true };
            code_intelligence_symbol(p->source, &symbol);
        }
        for (int j = 0; j < acount(scope->declarations); j++) {
            declaration *decl = scope->declarations[j];
            bool implicit = decl->start == 0 && decl->end == 0;
            if (!decl->symbol) {
                decl->symbol = e.next++;
            }
            const code_semantic_symbol
                *existing = code_intelligence_symbol_by_id(p->source, decl->symbol);
            code_semantic_symbol symbol = existing
                                            ? *existing
                                            : (code_semantic_symbol){
                                                  .id = decl->symbol,
                                                  .name = decl->name,
                                                  .kind = CODE_SYMBOL_LOCAL,
                                                  .start = decl->start,
                                                  .end = decl->start +
                                                         (int)strlen(decl->name),
                                                  .scope_end = (int)
                                                      strlen(p->source->text),
                                                  .renameable = true
                                              };
            char *detail = decl->native ? node_signature(decl->native) : NULL;
            if (implicit) {
                symbol.start = symbol.end = 0;
                symbol.renameable = false;
            }
            symbol.owner_id = owner;
            symbol.scope_start = scope->start;
            symbol.scope_end = scope->end;
            symbol.signature = detail;
            symbol.graph_id = scope->native->graph_id;
            symbol.node_id = decl->native ? decl->native->id : 0;
            if (decl->native) {
                symbol.description = decl->native->desc->description;
                int single = -1;
                for (int port = 0; port < decl->native->output_count; port++) {
                    if (decl->native->output_pins[port].kind != PIN_DATA) {
                        continue;
                    }
                    if (single >= 0) {
                        single = -2;
                        break;
                    }
                    single = port;
                }
                if (single >= 0) {
                    symbol.type = port_type(&decl->native->output_pins[single]);
                }
            }
            code_intelligence_symbol(p->source, &symbol);
            free(detail);
            members(&e, scope, decl);
        }
        for (int j = 0; j < acount(scope->continuations); j++) {
            continuation *continuation = &scope->continuations[j];
            for (int k = 0; k < acount(continuation->aliases); k++) {
                declaration *alias = continuation->aliases[k];
                declaration
                    *handled = continuation->source
                                 ? code_program_declaration(scope, continuation->source)
                                 : NULL;
                if (!strcmp(alias->name, "event") && handled &&
                    alias->symbol == handled->symbol) {
                    alias->symbol = e.next++;
                    code_semantic_symbol implicit = {
                        .id = alias->symbol,
                        .definition_id = handled->symbol,
                        .owner_id = owner,
                        .kind = CODE_SYMBOL_EVENT,
                        .name = alias->name,
                        .scope_start = continuation->start,
                        .scope_end = continuation->end,
                        .graph_id = scope->native->graph_id,
                        .node_id = alias->native ? alias->native->id : 0
                    };
                    code_intelligence_symbol(p->source, &implicit);
                }
                const code_semantic_symbol
                    *existing = code_intelligence_symbol_by_id(p->source, alias->symbol);
                if (existing) {
                    code_semantic_symbol symbol = *existing;
                    symbol.scope_start = continuation->start;
                    symbol.scope_end = continuation->end;
                    symbol.graph_id = scope->native->graph_id;
                    symbol.node_id = alias->native ? alias->native->id : 0;
                    code_intelligence_symbol(p->source, &symbol);
                }
                members(&e, scope, alias);
            }
        }
    }
    for (int i = 0; i < acount(p->program->scopes); i++) {
        scope *scope = p->program->scopes[i];
        for (int j = 0; j < acount(scope->declarations); j++) {
            declaration *decl = scope->declarations[j];
            struct scope *target = local_call_scope(&e, decl->native);
            if (target) {
                call_parameters(&e, target, decl);
            }
        }
        for (int j = 0; j < acount(scope->declarations); j++) {
            argument_facts(&e, scope, scope->declarations[j]->arguments);
        }
        uint64_t owner = graph_symbol(&e, scope->native);
        for (int j = 0; j < acount(scope->declarations); j++) {
            declaration *decl = scope->declarations[j];
            uint64_t target = native_target(&e, decl->native);
            if (target && decl->symbol) {
                int start = decl->reference_end > decl->reference_start
                              ? decl->reference_start
                              : decl->call_start;
                int end = decl->reference_end > decl->reference_start
                            ? decl->reference_end
                            : decl->call_end;
                if (end > start) {
                    code_intelligence_use(
                        p->source,
                        (code_semantic_use){ target, start, end, CODE_USE_CALL }
                    );
                }
                code_intelligence_call(
                    p->source,
                    (code_semantic_call){ decl->symbol, target, decl->start, decl->end }
                );
            }
        }
        for (int j = 0; j < acount(scope->returns); j++) {
            argument *result = &scope->returns[j];
            uint64_t parameter = parameter_id(&e, scope, true, result->port.name);
            if (parameter && result->name_start < result->name_end) {
                code_intelligence_use(
                    p->source,
                    (code_semantic_use){ parameter, result->name_start, result->name_end, CODE_USE_WRITE }
                );
            }
        }
        argument_facts(&e, scope, scope->returns);
        for (int j = 0; j < acount(scope->continuations); j++) {
            continuation *continuation = &scope->continuations[j];
            declaration
                *event = continuation->source
                           ? code_program_declaration(scope, continuation->source)
                           : NULL;
            e.caller = event && event->symbol ? event->symbol : owner;
            if (event && event->symbol &&
                continuation->source_end > continuation->source_start) {
                code_intelligence_use(
                    p->source,
                    (code_semantic_use){ event->symbol,
                                         continuation->source_start,
                                         continuation->source_end,
                                         CODE_USE_CALL }
                );
                uint64_t outcome = continuation->outcome ? member_id(
                                                               p->source,
                                                               event->symbol,
                                                               continuation->outcome
                                                           )
                                                         : 0;
                if (outcome && continuation->outcome_end > continuation->outcome_start) {
                    code_intelligence_use(
                        p->source,
                        (code_semantic_use){ outcome,
                                             continuation->outcome_start,
                                             continuation->outcome_end,
                                             CODE_USE_CALL }
                    );
                }
            }
            int declaration_count = acount(scope->declarations);
            for (int k = 0; k < acount(continuation->aliases); k++) {
                apush(scope->declarations, continuation->aliases[k]);
            }
            statements(&e, scope, continuation->body);
            asetlen(scope->declarations, declaration_count);
        }
    }
}
