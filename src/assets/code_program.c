#include "assets/code_calls.h"
#include "assets/code_components.h"
#include "assets/code_program_internal.h"
#include "assets/code_query.h"
#include "assets/code_reactive.h"
#include "assets/code_source_context.h"
#include "assets/code_source_intelligence.h"
#include "assets/code_source_internal.h"
#include "assets/code_value.h"
#include "graph/document/events.h"
#include "graph/document/graph_signature.h"
#include "graph/document/serialize_internal.h"
#include "graph/model/local.h"
#include "graph/model/pin_conv.h"
#include "graph/model/registry.h"
#include "graph/nodes/behavior_expression.h"
#include "graph/nodes/component_nodes.h"
#include "graph/nodes/conv_nodes.h"
#include "support/array.h"
#include "support/code_lex.h"
#include "support/xalloc.h"

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void advance(parser *p)
{
    p->token = code_lex_next(&p->lexer);
}
static bool is(parser *p, const char *word)
{
    return code_token_is(&p->lexer, p->token, word);
}
static bool take(parser *p, const char *word)
{
    if (!is(p, word)) {
        return false;
    }
    advance(p);
    return true;
}
static bool failed(parser *p)
{
    return acount(p->source->diagnostics) != 0;
}
static void error(parser *p, const char *message)
{
    code_source_error(
        p->source,
        p->base + (int)p->token.start,
        p->base + (int)p->token.end,
        message
    );
}
static bool expect(parser *p, const char *word)
{
    if (take(p, word)) {
        return true;
    }
    char message[160];
    snprintf(message, sizeof message, "Expected %s", word);
    error(p, message);
    return false;
}
static char *keep(parser *p, const char *text)
{
    char *result = xstrdup(text);
    apush(p->program->strings, result);
    return result;
}
static char *slice(parser *p, int start, int end)
{
    char *result = xmalloc((size_t)(end - start) + 1);
    memcpy(result, p->lexer.text + start, (size_t)(end - start));
    result[end - start] = 0;
    apush(p->program->strings, result);
    return result;
}
static char *name(parser *p)
{
    code_token token = p->token;
    if (token.kind == CODE_TOKEN_NAME) {
        advance(p);
        return slice(p, (int)token.start, (int)token.end);
    }
    if (token.kind != CODE_TOKEN_STRING) {
        error(p, "Expected a name");
        return NULL;
    }
    char *quoted = slice(p, (int)token.start, (int)token.end), message[256];
    av_doc *value = code_value_read(quoted, message, sizeof message);
    const char *text = value ? av_get_str(av_doc_root(value)) : NULL;
    char *result = text ? keep(p, text) : NULL;
    if (!result) {
        error(p, "Invalid name string");
    }
    av_doc_free(value);
    advance(p);
    return result;
}
static char *reference_name(parser *p)
{
    if (p->token.kind == CODE_TOKEN_STRING) {
        return name(p);
    }
    int start = (int)p->token.start, end = (int)p->token.end;
    if (!name(p)) {
        return NULL;
    }
    while (take(p, ".")) {
        end = (int)p->token.end;
        if (!name(p)) {
            return NULL;
        }
    }
    return slice(p, start, end);
}

static code_call_port type(parser *p)
{
    code_program_complete_type(p, (int)p->token.start + p->base, (int)p->token.end + p->base);
    code_call_port port = { 0 };
    port.has_traits = take(p, "plain");
    while (is(p, "ref") || is(p, "service") || is(p, "self") || is(p, "setting")) {
        if (take(p, "ref")) {
            port.has_traits = true;
            port.flags |= PIN_FLAG_REFERENCE;
        } else if (take(p, "service")) {
            port.has_traits = true;
            port.flags |= PIN_FLAG_SERVICE;
        } else if (take(p, "self")) {
            port.has_traits = true;
            port.flags |= PIN_FLAG_DEFAULT_SELF;
        } else if (take(p, "setting")) {
            port.has_traits = true;
            port.flags |= PIN_FLAG_SETTING;
        }
    }
    if (take(p, "wildcard")) {
        port.flags |= PIN_FLAG_WILDCARD;
        if (expect(p, "(")) {
            char *n = slice(p, (int)p->token.start, (int)p->token.end);
            port.group = atoi(n);
            advance(p);
            port.has_traits = true;
            expect(p, ")");
        }
    }
    char *text = name(p);
    if (!text) {
        return port;
    }
    for (int i = 0; i < acount(p->type_parameters); i++) {
        if (!strcmp(p->type_parameters[i].name, text)) {
            code_call_port parameter = p->type_parameters[i];
            parameter.flags |= port.flags | PIN_FLAG_WILDCARD;
            parameter.has_traits = true;
            parameter.kind = PIN_DATA;
            if (take(p, "[")) {
                expect(p, "]");
                parameter.container = PIN_CONTAINER_ARRAY;
            }
            return parameter;
        }
    }
    if (!strcmp(text, "set") || !strcmp(text, "map")) {
        pin_container container = !strcmp(text, "set") ? PIN_CONTAINER_SET
                                                       : PIN_CONTAINER_MAP;
        expect(p, "<");
        code_call_port item = type(p);
        expect(p, ">");
        item.container = container;
        return item;
    }
    if (!text) {
        return port;
    }
    if (!strcmp(text, "any")) {
        port.type = PIN_FLOW;
        port.kind = PIN_DATA;
        port.type_name = NULL;
        return port;
    }
    port.kind = !strcmp(text, "flow") || !strcmp(text, "exec") ? PIN_EXEC : PIN_DATA;
    if (port.kind == PIN_EXEC) {
        port.type = PIN_FLOW;
        port.type_name = NULL;
    } else if (!strcmp(text, "entity")) {
        port.type = PIN_HANDLE;
        port.type_name = keep(p, "ecs.entity");
    } else if (!strcmp(text, "string")) {
        port.type = PIN_STR;
        port.type_name = keep(p, "const char *");
    } else if (!strcmp(text, "number")) {
        port.type = PIN_F64;
        port.type_name = keep(p, "double");
    } else {
        if (!serialize_read_pin_type(av_str(p->program->values, text), &port.type)) {
            error(p, "Unknown value type");
        }
        port.type_name = text;
    }
    if (take(p, "<")) {
        port.type_name = name(p);
        expect(p, ">");
    }
    if (take(p, "[")) {
        expect(p, "]");
        port.container = PIN_CONTAINER_ARRAY;
    }
    return port;
}
/* Capture one expression without reparsing its trivia or losing byte ranges. */
static char *expression_text(parser *p, int *start, int *end)
{
    *start = (int)p->token.start;
    *end = *start;
    int round = 0, square = 0, brace = 0;
    while (p->token.kind != CODE_TOKEN_END) {
        code_program_complete_cast(p);
        if (!round && !square && !brace &&
            (is(p, ",") || is(p, ")") || is(p, ";") || is(p, "}"))) {
            break;
        }
        if (is(p, "(")) {
            round++;
        }
        if (is(p, ")")) {
            round--;
        }
        if (is(p, "[")) {
            square++;
        }
        if (is(p, "]")) {
            square--;
        }
        if (is(p, "{")) {
            brace++;
        }
        if (is(p, "}")) {
            brace--;
        }
        *end = (int)p->token.end;
        advance(p);
    }
    if (*start == *end) {
        error(p, "Expected a value expression");
        return NULL;
    }
    return slice(p, *start, *end);
}
static bool type_token(parser *p)
{
    if (p->token.kind == CODE_TOKEN_NUMBER) {
        parser look = *p;
        advance(&look);
        if (is(&look, "=")) {
            return true;
        }
    }
    if (p->token.kind != CODE_TOKEN_NAME && p->token.kind != CODE_TOKEN_STRING) {
        return false;
    }
    if (!is(p, "wildcard")) {
        parser look = *p;
        advance(&look);
        if (is(&look, "(") || is(&look, ".") || is(&look, ",") ||
            is(&look, ")") || look.token.kind == CODE_TOKEN_END) {
            return false;
        }
    }
    if (is(p, "number") || is(p, "string") || is(p, "entity") ||
        is(p, "flow") || is(p, "exec") || is(p, "any") || is(p, "set") ||
        is(p, "map") || is(p, "plain") || is(p, "ref") || is(p, "self") ||
        is(p, "service") || is(p, "setting") || is(p, "wildcard")) {
        return true;
    }
    char *text = slice(p, (int)p->token.start, (int)p->token.end);
    for (int i = 0; i < acount(p->type_parameters); i++) {
        if (!strcmp(p->type_parameters[i].name, text)) {
            return true;
        }
    }
    pin_type value;
    return serialize_read_pin_type(av_str(p->program->values, text), &value);
}
static argument *arguments(parser *p, bool results)
{
    argument *args = NULL;
    if (!expect(p, "(")) {
        return args;
    }
    code_program_complete_arguments(
        p,
        p->completion_call,
        (int)p->token.start + p->base,
        (int)p->token.end + p->base
    );
    code_call_port first_port;
    if ((p->token.kind == CODE_TOKEN_END || is(p, ")")) &&
        code_program_positional_port(p, p->completion_call, 0, &first_port)) {
        code_program_complete_value(
            p,
            p->scope,
            (int)p->token.start + p->base,
            (int)p->token.end + p->base,
            &first_port
        );
    }
    while (!is(p, ")") && p->token.kind != CODE_TOKEN_END && !failed(p)) {
        code_program_complete_arguments(
            p,
            p->completion_call,
            (int)p->token.start + p->base,
            (int)p->token.end + p->base
        );
        argument a = { .name_start = (int)p->token.start + p->base,
                       .name_end = (int)p->token.end + p->base };
        parser look = *p;
        advance(&look);
        while (is(&look, ".")) {
            advance(&look);
            if (look.token.kind != CODE_TOKEN_NAME &&
                look.token.kind != CODE_TOKEN_STRING) {
                break;
            }
            advance(&look);
        }
        bool named = results || ((p->token.kind == CODE_TOKEN_NAME ||
                                  p->token.kind == CODE_TOKEN_STRING) &&
                                 is(&look, ":"));
        if (!named) {
            a.positional = true;
            a.ordinal = acount(args);
            code_call_port expected;
            if (code_program_positional_port(p, p->completion_call, a.ordinal, &expected)) {
                code_program_complete_value(p, p->scope, a.name_start, a.name_end, &expected);
            }
            a.name_end = a.name_start;
            a.text = expression_text(p, &a.start, &a.end);
            a.start += p->base;
            a.end += p->base;
            apush(args, a);
            if (!take(p, ",")) {
                break;
            }
            continue;
        }
        a.port.name = name(p);
        while (a.port.name && take(p, ".")) {
            a.name_end = (int)p->token.end + p->base;
            char *field = name(p);
            if (!field) {
                break;
            }
            size_t length = strlen(a.port.name) + strlen(field) + 2;
            char *path = xmalloc(length);
            snprintf(path, length, "%s.%s", a.port.name, field);
            a.port.name = keep(p, path);
            free(path);
        }
        if (!a.port.name || !expect(p, ":")) {
            break;
        }
        if (results || type_token(p)) {
            code_call_port t = type(p);
            t.name = a.port.name;
            a.port = t;
            a.annotated = true;
            if (take(p, "from") || take(p, "split")) {
                a.port.aggregate = name(p);
                expect(p, ".");
                a.port.field = name(p);
            }
            if (take(p, "=")) {
                code_program_complete_value(
                    p,
                    p->scope,
                    (int)p->token.start + p->base,
                    (int)p->token.end + p->base,
                    &a.port
                );
                a.text = expression_text(p, &a.start, &a.end);
            } else if (!results && a.port.kind == PIN_DATA) {
                error(p, "Typed argument requires a value");
            }
        } else {
            code_call_port expected;
            bool
                typed = code_program_argument_port(p, p->completion_call, a.port.name, &expected);
            code_program_complete_value(
                p,
                p->scope,
                (int)p->token.start + p->base,
                (int)p->token.end + p->base,
                typed ? &expected : NULL
            );
            a.text = expression_text(p, &a.start, &a.end);
        }
        a.start += p->base;
        a.end += p->base;
        apush(args, a);
        if (!take(p, ",")) {
            break;
        }
        code_program_complete_arguments(
            p,
            p->completion_call,
            (int)p->token.start + p->base,
            (int)p->token.end + p->base
        );
    }
    expect(p, ")");
    return args;
}
static bool literal(parser *p, graph *g, code_call_port *port, const char *text, pin_value *out)
{
    char message[256];
    av_doc *value = code_value_read(text, message, sizeof message);
    if (!value) {
        return false;
    }
    const av_value *root = av_doc_root(value);
    if (av_is_str(root) && memchr(av_get_str(root), 0, av_get_len(root))) {
        error(p, "String literal contains embedded NUL byte");
        av_doc_free(value);
        return false;
    }
    int result = port->container == PIN_CONTAINER_SINGLE
                   ? serialize_read_value(port->type, port->type_name, av_doc_root(value), out, g)
                   : serialize_read_array(port->type, port->type_name, av_doc_root(value), out, g);
    if (result && port->container == PIN_CONTAINER_SINGLE && av_is_int(root) &&
        ((port->type >= PIN_I8 && port->type <= PIN_U64) || port->type == PIN_ENUM)) {
        char message[128];
        snprintf(
            message,
            sizeof message,
            "Integer literal is outside %s storage range",
            pin_type_name(port->type)
        );
        error(p, message);
    }
    av_doc_free(value);
    if (result) {
        return false;
    }
    out->container = port->container;
    return true;
}
static void defaults(parser *p, graph *g, argument *args)
{
    for (int i = 0; i < acount(args); i++) {
        if (args[i].text) {
            args[i].port.has_default = literal(
                p,
                g,
                &args[i].port,
                args[i].text,
                &args[i].port.default_value
            );
        }
    }
}
static declaration *find(scope *s, const char *text)
{
    for (int i = acount(s->declarations) - 1; i >= 0; i--) {
        if (!strcmp(s->declarations[i]->name, text)) {
            return s->declarations[i];
        }
    }
    return NULL;
}
static void symbol(parser *p, declaration *d, code_symbol_kind kind)
{
    d->symbol = (uint64_t)d->start + 1;
    code_lexer lexer;
    code_lex_init(&lexer, p->source->text + d->start);
    code_token token = code_lex_next(&lexer);
    code_semantic_symbol fact = { .id = d->symbol,
                                  .kind = kind,
                                  .name = d->name,
                                  .start = d->start,
                                  .end = d->start + (int)token.end,
                                  .scope_start = p->scope->start,
                                  .scope_end = p->scope->end,
                                  .renameable = true };
    code_intelligence_symbol(p->source, &fact);
}
static char *callable(parser *p, bool *native)
{
    code_program_complete_callable(
        p,
        (int)p->token.start + p->base,
        (int)p->token.end + p->base
    );
    *native = take(p, "engine");
    if (*native) {
        expect(p, "[");
        char *result = name(p);
        expect(p, "]");
        return result;
    }
    return name(p);
}
static void expression_group(parser *p, char **text, int *start, int end)
{
    code_lexer lexer;
    code_lex_init(&lexer, *text);
    code_token token = code_lex_next(&lexer);
    if (token.punctuation != '(') {
        return;
    }
    int depth = 0;
    for (; token.kind != CODE_TOKEN_END; token = code_lex_next(&lexer)) {
        depth += token.punctuation == '(';
        depth -= token.punctuation == ')';
        if (!depth) {
            if ((int)token.end == end - *start) {
                *text = slice(p, *start + 1, end - 1);
                (*start)++;
            }
            return;
        }
    }
}
static bool declaration_expression(parser *p)
{
    if (is(p, "(") || is(p, "cast")) {
        return true;
    }
    if (is(p, "engine")) {
        return false;
    }
    parser look = *p;
    if (look.token.kind != CODE_TOKEN_NAME && look.token.kind != CODE_TOKEN_STRING) {
        return true;
    }
    int root_start = (int)look.token.start, root_end = (int)look.token.end;
    advance(&look);
    int members = 0;
    while (take(&look, ".")) {
        if (look.token.kind != CODE_TOKEN_NAME && look.token.kind != CODE_TOKEN_STRING) {
            return true;
        }
        members++;
        advance(&look);
    }
    if (!is(&look, "(") && !is(&look, "<")) {
        if (look.token.kind == CODE_TOKEN_END && members <= 1) {
            char *root = slice(p, root_start, root_end);
            if (!find(p->scope, root) && strcmp(root, "self") &&
                !behavior_asset_find_variable_by_name(p->asset, root)) {
                bool binding = false;
                for (int i = 0; i < acount(p->scope->components); i++) {
                    binding |= !strcmp(p->scope->components[i].name, root);
                }
                if (!binding) {
                    return false;
                }
            }
        }
        return true;
    }
    if (!members) {
        return false;
    }
    if (members != 1) {
        return true;
    }
    char *root = slice(p, root_start, root_end);
    if (find(p->scope, root) || !strcmp(root, "self") ||
        behavior_asset_find_variable_by_name(p->asset, root)) {
        return true;
    }
    for (int i = 0; i < acount(p->scope->components); i++) {
        if (!strcmp(p->scope->components[i].name, root)) {
            return true;
        }
    }
    for (int i = 0; i < acount(p->scope->inputs); i++) {
        if (!strcmp(p->scope->inputs[i].port.name, root)) {
            return true;
        }
    }
    for (int i = 0; i < p->source->context.library_count; i++) {
        if (!strcmp(p->source->context.libraries[i]->name, root)) {
            return false;
        }
    }
    /* Unknown qualified callables still use callable recovery and diagnostics. */
    return false;
}
static void declaration_parse(parser *p, bool disabled, bool event, bool lazy)
{
    declaration *d = xcalloc(1, sizeof *d);
    d->start = (int)p->token.start;
    d->name = name(p);
    d->disabled = disabled;
    d->lazy = lazy;
    if (!d->name) {
        free(d);
        return;
    }
    if (find(p->scope, d->name)) {
        error(p, "Duplicate callable name in this scope");
        free(d);
        return;
    }
    if (take(p, ":")) {
        if (!is(p, "(")) {
            code_call_port result = type(p);
            result.name = "Value";
            result.kind = PIN_DATA;
            if (!expect(p, "=")) {
                free(d);
                return;
            }
            int start, end;
            char *text = expression_text(p, &start, &end);
            result.has_default = literal(
                p,
                p->scope->native,
                &result,
                text,
                &result.default_value
            );
            if (!result.has_default) {
                expression_group(p, &text, &start, end);
                d->expression = text;
                d->expression_start = start;
                d->call.name = "BehaviorExpression";
                apush(d->results, result);
                expect(p, ";");
                d->end = (int)p->token.start;
                apush(p->scope->declarations, d);
                symbol(p, d, CODE_SYMBOL_LOCAL);
                return;
            }
            const char *constant = result.type == PIN_BOOL ? "Bool"
                                 : result.type == PIN_U8   ? "Byte"
                                 : result.type == PIN_I32  ? "Integer"
                                 : result.type == PIN_I64  ? "Integer64"
                                 : result.type == PIN_NAME ? "Name"
                                 : result.type == PIN_STR  ? "String"
                                                           : "Real";
            d->call.name = constant;
            apush(d->results, result);
            d->call.results = d->results;
            d->call.result_count = 1;
            expect(p, ";");
            d->end = (int)p->token.start;
            apush(p->scope->declarations, d);
            symbol(p, d, CODE_SYMBOL_LOCAL);
            return;
        }
        argument *result = arguments(p, true);
        defaults(p, p->scope->native, result);
        for (int i = 0; i < acount(result); i++) {
            apush(d->results, result[i].port);
        }
        afree(result);
    }
    if (!expect(p, "=")) {
        free(d);
        return;
    }
    if (!event &&
        ((lazy && declaration_expression(p)) || is(p, "(") || is(p, "cast"))) {
        int end;
        d->expression = expression_text(p, &d->expression_start, &end);
        expression_group(p, &d->expression, &d->expression_start, end);
        d->call.name = "BehaviorExpression";
        expect(p, ";");
        d->end = (int)p->token.start;
        apush(p->scope->declarations, d);
        symbol(p, d, CODE_SYMBOL_LOCAL);
        return;
    }
    d->call_start = (int)p->token.start;
    d->call_end = (int)p->token.end;
    d->call.name = callable(p, &d->call.native);
    if (take(p, ".")) {
        d->call.reference_asset = d->call.name;
        d->call_start = d->reference_start = (int)p->token.start;
        d->call_end = d->reference_end = (int)p->token.end;
        code_program_complete_qualified(
            p,
            d->call.reference_asset,
            d->call_start + p->base,
            d->call_end + p->base
        );
        d->call.name = name(p);
    }
    if (take(p, "<")) {
        d->reference_start = (int)p->token.start;
        d->reference_end = (int)p->token.end;
        d->call.reference = reference_name(p);
        if (take(p, ",")) {
            d->call.reference_asset = name(p);
        }
        expect(p, ">");
    }
    const code_call_spec *previous_call = p->completion_call;
    p->completion_call = &d->call;
    d->arguments = arguments(p, false);
    p->completion_call = previous_call;
    d->call.results = d->results;
    d->call.result_count = acount(d->results);
    expect(p, ";");
    d->end = (int)p->token.start;
    apush(p->scope->declarations, d);
    symbol(p, d, event ? CODE_SYMBOL_EVENT : CODE_SYMBOL_LOCAL);
}
static statement *block(parser *p)
{
    return code_statement_parse_block(p);
}
static const char *host_reference(const char *name)
{
    for (int i = 0; name && i < GRAPH_HOST_EVENT_COUNT; i++) {
        const char *native = GRAPH_HOST_EVENTS[i].name;
        const unsigned char *a = (const unsigned char *)name;
        const unsigned char *b = (const unsigned char *)native;
        while (*a || *b) {
            while (*a && !isalnum(*a)) {
                a++;
            }
            while (*b && !isalnum(*b)) {
                b++;
            }
            if (tolower(*a) != tolower(*b)) {
                break;
            }
            if (!*a) {
                return native;
            }
            a++;
            b++;
        }
        if (!*a && !*b) {
            return native;
        }
    }
    return name;
}
static const graph_host_event_desc *host_event(const char *name)
{
    for (int i = 0; i < GRAPH_HOST_EVENT_COUNT; i++) {
        if (name && !strcmp(GRAPH_HOST_EVENTS[i].name, name)) {
            return &GRAPH_HOST_EVENTS[i];
        }
    }
    return NULL;
}
static pin_spec *event_payload(parser *p, const char *reference)
{
    pin_spec *fields = NULL;
    const graph_host_event_desc *host = host_event(reference);
    if (host) {
        for (int i = 0; i < host->payload_count; i++) {
            apush(fields, host->payload[i]);
        }
        return fields;
    }
    char message[256];
    const behavior_asset *definition = code_source_context_event(
        &p->source->context,
        reference,
        message,
        sizeof message
    );
    for (int i = 0; definition && i < acount(definition->variables); i++) {
        const behavior_variable *variable = &definition->variables[i];
        pin_spec field = { .name = variable->name,
                           .type = variable->type,
                           .type_name = variable->type_name,
                           .container = variable->container,
                           .default_value = variable->default_value };
        apush(fields, field);
    }
    return fields;
}
static void payload_alias(
    parser *p,
    continuation *c,
    declaration *event,
    const pin_spec *field,
    char *name,
    int start,
    int end
)
{
    declaration *alias = xcalloc(1, sizeof *alias);
    alias->name = name;
    alias->call = event->call;
    alias->start = start;
    alias->end = end;
    alias->symbol = (uint64_t)start + 1;
    code_call_port port = { .name = field->name,
                            .type = field->type,
                            .type_name = field->type_name,
                            .container = field->container,
                            .kind = PIN_DATA };
    apush(alias->results, port);
    apush(c->aliases, alias);
    symbol(p, alias, CODE_SYMBOL_PARAMETER);
}
static void continuation_parse(parser *p, bool callback)
{
    continuation c = { .start = (int)p->token.start };
    int event_start = (int)p->token.start, event_end = (int)p->token.end;
    c.source_start = event_start + p->base;
    c.source_end = event_end + p->base;
    code_program_complete_event(p, event_start + p->base, event_end + p->base);
    char *reference = name(p);
    c.source = reference;
    declaration *event = reference ? find(p->scope, reference) : NULL;
    const char *native_reference = host_reference(reference);
    bool host = native_reference && graph_event_host_name(native_reference);
    if (take(p, "as")) {
        event_start = (int)p->token.start;
        event_end = (int)p->token.end;
        c.source_start = event_start + p->base;
        c.source_end = event_end + p->base;
        c.source = name(p);
        event = c.source ? find(p->scope, c.source) : NULL;
    }
    if (!callback && !event && reference && c.source &&
        (host || is(p, "(") || is(p, "{"))) {
        event = xcalloc(1, sizeof *event);
        event->name = c.source;
        event->call = (code_call_spec){ .name = "Event", .reference = native_reference };
        event->start = event_start;
        event->end = event_end;
        apush(p->scope->declarations, event);
        symbol(p, event, CODE_SYMBOL_EVENT);
    }
    if (callback &&
        (!event || (event->call.name && !strcmp(event->call.name, "Event")))) {
        error(p, "after requires an action completion");
        return;
    }
    if (!callback && event &&
        (!event->call.name || strcmp(event->call.name, "Event"))) {
        error(p, "on requires an event; use after for action completions");
        return;
    }
    if (take(p, ".")) {
        c.outcome_start = (int)p->token.start + p->base;
        c.outcome_end = (int)p->token.end + p->base;
        c.outcome = name(p);
    } else if (take(p, "[")) {
        c.outcome_start = (int)p->token.start + p->base;
        c.outcome_end = (int)p->token.end + p->base;
        c.outcome = name(p);
        expect(p, "]");
    }
    parser payload = *p;
    bool typed_payload = false, inferred_payload = false;
    if (is(&payload, "(")) {
        advance(&payload);
        if (payload.token.kind == CODE_TOKEN_NAME ||
            payload.token.kind == CODE_TOKEN_STRING) {
            advance(&payload);
            typed_payload = is(&payload, ":");
            inferred_payload = is(&payload, ",") || is(&payload, ")");
        }
    }
    if (typed_payload) {
        argument *fields = arguments(p, true);
        defaults(p, p->scope->native, fields);
        if (!event) {
            error(p, "Event payload requires an event declaration");
        }
        for (int i = 0; event && i < acount(fields); i++) {
            if (fields[i].port.kind == PIN_EXEC) {
                code_source_error(p->source, fields[i].start, fields[i].end, "Event payload fields must be data values");
            }
            apush(event->results, fields[i].port);
        }
        if (event) {
            event->call.results = event->results;
            event->call.result_count = acount(event->results);
        }
        afree(fields);
    } else if (inferred_payload && event) {
        pin_spec *fields = event_payload(p, native_reference);
        take(p, "(");
        int ordinal = 0;
        while (!is(p, ")") && p->token.kind != CODE_TOKEN_END && !failed(p)) {
            int start = (int)p->token.start, end = (int)p->token.end;
            char *parameter = name(p);
            if (ordinal >= acount(fields)) {
                error(p, "Too many event payload parameters");
                break;
            }
            payload_alias(p, &c, event, &fields[ordinal++], parameter, start, end);
            if (!take(p, ",")) {
                break;
            }
        }
        expect(p, ")");
        afree(fields);
    } else if (take(p, "(")) {
        while (!is(p, ")") && p->token.kind != CODE_TOKEN_END && !failed(p)) {
            int start = (int)p->token.start;
            char *component_name = name(p);
            int alias_start = (int)p->token.start, alias_end = (int)p->token.end;
            char *alias_name = name(p);
            if (!component_name || !alias_name) {
                break;
            }
            const ecs_component_desc *component = component_by_key(component_name);
            if (!component) {
                code_source_error(p->source, start, start + (int)strlen(component_name), "Unknown handler component type");
                break;
            }
            declaration *alias = xcalloc(1, sizeof *alias);
            alias->name = alias_name;
            alias->call = (code_call_spec){ .name = "Get Component",
                                            .reference = component_name };
            alias->start = alias_start;
            alias->end = alias_end;
            for (int i = 0; i < acount(c.aliases); i++) {
                if (!strcmp(c.aliases[i]->name, alias_name)) {
                    error(p, "Duplicate handler alias");
                }
            }
            apush(c.aliases, alias);
            symbol(p, alias, CODE_SYMBOL_COMPONENT);
            if (!take(p, ",")) {
                break;
            }
        }
        expect(p, ")");
    }
    if (event) {
        declaration *alias = xcalloc(1, sizeof *alias);
        alias->name = keep(p, "event");
        alias->call = event->call;
        alias->symbol = event->symbol;
        alias->start = event->start;
        alias->end = event->end;
        apush(c.aliases, alias);
    }
    c.body = block(p);
    c.end = (int)p->token.start;
    apush(p->scope->continuations, c);
}
static pin_type inferred_literal_type(const av_value *value)
{
    if (av_is_num(value)) {
        return PIN_F64;
    }
    if (av_is_bool(value)) {
        return PIN_BOOL;
    }
    return av_is_str(value) ? PIN_STR : PIN_FLOW;
}
static bool infer_state_type(const char *text, code_call_port *port)
{
    char message[256];
    av_doc *doc = code_value_read(text, message, sizeof message);
    if (!doc) {
        return false;
    }
    const av_value *value = av_doc_root(doc);
    bool array = av_is_arr(value);
    port->type = inferred_literal_type(array ? av_arr_get(value, 0) : value);
    if (array) {
        for (size_t i = 1; i < av_arr_size(value); i++) {
            if (inferred_literal_type(av_arr_get(value, i)) != port->type) {
                port->type = PIN_FLOW;
                break;
            }
        }
    }
    port->kind = PIN_DATA;
    port->container = array ? PIN_CONTAINER_ARRAY : PIN_CONTAINER_SINGLE;
    port->type_name = port->type == PIN_STR  ? "const char *"
                    : port->type == PIN_F64  ? "double"
                    : port->type == PIN_BOOL ? "bool"
                                             : NULL;
    av_doc_free(doc);
    return port->type != PIN_FLOW;
}
static void state_parse(parser *p, bool shared)
{
    if (shared && !p->program->system) {
        error(p, "shared storage requires a system");
        return;
    }
    parser saved = *p;
    int name_start = (int)p->token.start, name_end = (int)p->token.end;
    char *n = name(p);
    code_call_port t = { 0 };
    bool inferred = false;
    if (take(p, ":")) {
        inferred = is(p, "=");
        if (!inferred) {
            t = type(p);
        }
    } else {
        *p = saved;
        t = type(p);
        name_start = (int)p->token.start;
        name_end = (int)p->token.end;
        n = name(p);
    }
    if (!n || !expect(p, "=")) {
        return;
    }
    int start, end;
    char *text = expression_text(p, &start, &end);
    expect(p, ";");
    if (text && inferred && !infer_state_type(text, &t)) {
        code_source_error(p->source, start, end, "Cannot infer this state default; supply a type for empty collections and nominal values");
        return;
    }
    pin_value value = { 0 };
    if (!text || !literal(p, p->asset->graph, &t, text, &value)) {
        code_source_error(p->source, start, end, "Invalid typed state default");
        return;
    }
    if (behavior_asset_find_variable_by_name(p->asset, n)) {
        code_source_error(p->source, name_start, name_end, "Duplicate state name");
        return;
    }
    behavior_variable *
        v = behavior_asset_add_variable(p->asset, n, t.type, t.type_name, t.container, &value, NULL);
    v->shared = shared;
    state_declaration declaration = { .name = n,
                                      .type = t,
                                      .native_id = v->id,
                                      .start = name_start,
                                      .end = name_end,
                                      .symbol = (uint64_t)name_start + 1 };
    apush(p->program->states, declaration);
    code_semantic_symbol fact = { .id = declaration.symbol,
                                  .kind = CODE_SYMBOL_STATE,
                                  .name = n,
                                  .type = { t.type, t.container, t.type_name },
                                  .start = name_start,
                                  .end = name_end,
                                  .scope_end = (int)strlen(p->source->text),
                                  .renameable = true,
                                  .graph_id = p->asset->graph->graph_id,
                                  .node_id = v->id };
    code_intelligence_symbol(p->source, &fact);
}
static scope *new_scope(parser *p, graph *g)
{
    scope *s = xcalloc(1, sizeof *s);
    s->native = g;
    s->start = (int)p->token.start;
    s->end = (int)strlen(p->source->text);
    apush(p->program->scopes, s);
    return s;
}
static void scope_parse(parser *p);
static void function_parse(parser *p, bool macro, bool pure)
{
    int name_start = (int)p->token.start, name_end = (int)p->token.end;
    char *n = name(p);
    if (!n) {
        return;
    }
    code_call_port *outer_parameters = p->type_parameters, *parameters = NULL;
    if (take(p, "<")) {
        do {
            char *parameter_name = name(p);
            code_call_port parameter = { .type = PIN_FLOW, .kind = PIN_DATA };
            if (take(p, ":")) {
                parameter = type(p);
            }
            parameter.name = parameter_name;
            parameter.group = acount(parameters) + 1;
            parameter.has_traits = true;
            parameter.flags |= PIN_FLAG_WILDCARD;
            apush(parameters, parameter);
        } while (take(p, ","));
        expect(p, ">");
    }
    p->type_parameters = parameters;
    argument *inputs = arguments(p, true);
    expect(p, "-");
    expect(p, ">");
    argument *outputs = arguments(p, true);
    graph *g = macro ? behavior_asset_add_macro(p->asset, n)
                     : behavior_asset_add_function(p->asset, n, pure);
    if (!g) {
        error(p, graph_last_error());
        afree(inputs);
        afree(outputs);
        afree(parameters);
        p->type_parameters = outer_parameters;
        return;
    }
    node *entry = graph_node_by_id(g, g->entry_id),
         *ret = graph_node_by_id(g, g->return_id);
    for (int side = 0; side < 2; side++) {
        argument *ports = side ? outputs : inputs;
        bool explicit_exec = false;
        for (int i = 0; i < acount(ports); i++) {
            explicit_exec |= ports[i].port.kind == PIN_EXEC;
        }
        node *native = side ? ret : entry;
        int count = side ? native->input_count : native->output_count;
        for (int i = count - 1; i >= 0; i--) {
            const pin_spec *pin = side ? &native->input_pins[i]
                                       : &native->output_pins[i];
            if (pin->kind != PIN_EXEC || explicit_exec) {
                graph_remove_pin(g, native, !side, i);
            }
        }
    }
    defaults(p, g, inputs);
    defaults(p, g, outputs);
    for (int side = 0; side < 2; side++) {
        argument *ports = side ? outputs : inputs;
        for (int i = 0; i < acount(ports); i++) {
            code_call_port *t = &ports[i].port;
            const char *owned_type = t->type_name
                                       ? graph_own_str(g, t->type_name, strlen(t->type_name))
                                       : NULL;
            pin_value default_value = t->default_value;
            default_value.type_name = owned_type;
            pin_spec pin = { .name = t->name,
                             .type = t->type,
                             .type_name = owned_type,
                             .container = t->container,
                             .kind = t->kind,
                             .flags = t->flags,
                             .group = t->group,
                             .default_value = t->default_value };
            node *owner = side ? ret : entry;
            int index = graph_add_pin(g, owner, !side, &pin);
            if (index < 0 ||
                graph_set_pin_type(g, owner, !side, index, t->type, t->type_name) ||
                graph_set_pin_container(g, owner, !side, index, t->container) ||
                (t->has_default &&
                 graph_set_pin_default(g, owner, !side, index, &t->default_value))) {
                error(p, graph_last_error());
            }
        }
    }
    scope *outer = p->scope;
    p->scope = new_scope(p, g);
    scope *authored = p->scope;
    authored->name_start = name_start;
    authored->name_end = name_end;
    authored->symbol = (uint64_t)name_start + 1;
    authored->type_parameters = parameters;
    authored->inputs = inputs;
    authored->outputs = outputs;
    declaration *e = xcalloc(1, sizeof *e), *r = xcalloc(1, sizeof *r);
    e->name = keep(p, "entry");
    e->native = entry;
    e->symbol = (UINT64_C(1) << 63) | entry->id;
    r->name = keep(p, "return");
    r->native = ret;
    r->symbol = (UINT64_C(1) << 63) | ret->id;
    apush(p->scope->declarations, e);
    apush(p->scope->declarations, r);
    expect(p, "{");
    scope_parse(p);
    authored->end = (int)p->token.end;
    expect(p, "}");
    p->scope = outer;
    p->type_parameters = outer_parameters;
}
bool code_program_system_declaration(parser *p)
{
    if (take(p, "state")) {
        state_parse(p, false);
        return true;
    }
    if (take(p, "shared")) {
        state_parse(p, true);
        return true;
    }
    if (take(p, "bind")) {
        code_reactive_parse_bind(p);
        return true;
    }
    if (take(p, "on")) {
        if (!code_reactive_parse_event(p)) {
            continuation_parse(p, false);
        }
        return true;
    }
    if (take(p, "after")) {
        continuation_parse(p, true);
        return true;
    }
    return false;
}
static void implicit_tick(parser *p)
{
    declaration *event = xcalloc(1, sizeof *event);
    event->name = keep(p, "__implicit_tick");
    event->call = (code_call_spec){ .name = "Event", .reference = "Tick" };
    event->start = (int)p->token.start;
    event->end = event->start;
    apush(p->scope->declarations, event);
    continuation handler = { .source = event->name, .start = event->start };
    payload_alias(
        p,
        &handler,
        event,
        &host_event("Tick")->payload[0],
        keep(p, "dt"),
        event->start,
        event->end
    );
    declaration *alias = xcalloc(1, sizeof *alias);
    alias->name = keep(p, "event");
    alias->call = event->call;
    apush(handler.aliases, alias);
    handler.body = code_statement_parse_body(p);
    handler.end = (int)p->token.start;
    apush(p->scope->continuations, handler);
}
static av_value *query_component(parser *p, const char *key)
{
    av_value *term = av_obj(p->program->values);
    av_obj_add_str(p->program->values, term, "op", "has");
    av_obj_add_str(p->program->values, term, "component", key);
    return term;
}
static const ecs_component_desc *header_component(parser *p)
{
    int start = (int)p->token.start;
    char *spelling = reference_name(p);
    bool ambiguous = false;
    const ecs_component_desc *component = spelling
                                            ? code_component_named(spelling, &ambiguous)
                                            : NULL;
    if (!component) {
        code_source_error(
            p->source,
            start,
            (int)p->token.start,
            ambiguous ? "Ambiguous system component type" : "Unknown system component type"
        );
    }
    return component;
}
static void system_header(parser *p)
{
    bool parameters = take(p, "(");
    p->program->system_header = true;
    if (!parameters && !is(p, "without")) {
        return;
    }
    av_doc *doc = p->program->values;
    av_value *query = av_obj(doc);
    av_obj_add_str(doc, query, "op", "all");
    av_value *terms = av_obj_add_arr(doc, query, "terms");
    while (parameters && !is(p, ")") && p->token.kind != CODE_TOKEN_END && !failed(p)) {
        int start = (int)p->token.start, end = (int)p->token.end;
        char *binding = name(p);
        expect(p, ":");
        const ecs_component_desc *component = header_component(p);
        bool optional = take(p, "?");
        if (!component) {
            break;
        }
        for (int i = 0; i < acount(p->scope->components); i++) {
            if (!strcmp(p->scope->components[i].name, binding)) {
                error(p, "Duplicate system component parameter");
            }
        }
        apush(
            p->scope->components,
            ((
                code_component_binding
            ){ binding, keep(p, component->key), component->type, optional, false, start, end })
        );
        behavior_asset_add_component_parameter(p->asset, binding, component->key, optional);
        if (!optional) {
            av_arr_add_val(terms, query_component(p, component->key));
        }
        code_semantic_symbol fact = {
            .id = (uint64_t)start + 1,
            .kind = CODE_SYMBOL_PARAMETER,
            .name = binding,
            .type = { PIN_STRUCT, PIN_CONTAINER_SINGLE, component->type->name },
            .start = start,
            .end = end,
            .scope_end = (int)strlen(p->source->text),
            .renameable = true
        };
        char signature[256];
        snprintf(
            signature,
            sizeof signature,
            "%s: %s%s",
            binding,
            code_component_spelling(component),
            optional ? "?" : ""
        );
        fact.signature = signature;
        code_intelligence_symbol(p->source, &fact);
        if (!take(p, ",")) {
            break;
        }
    }
    if (parameters) {
        expect(p, ")");
    }
    if (take(p, "without")) {
        do {
            const ecs_component_desc *component = header_component(p);
            if (!component) {
                break;
            }
            for (int i = 0; i < acount(p->scope->components); i++) {
                if (!strcmp(p->scope->components[i].key, component->key)) {
                    error(p, "A system component parameter cannot also be excluded");
                }
            }
            av_value *term = av_obj(doc);
            av_obj_add_str(doc, term, "op", "not");
            av_value *excluded = av_obj_add_arr(doc, term, "terms");
            av_arr_add_val(excluded, query_component(p, component->key));
            av_arr_add_val(terms, term);
        } while (take(p, ","));
    }
    p->program->query = query;
}
static void scope_parse(parser *p)
{
    while (!is(p, "}") && p->token.kind != CODE_TOKEN_END && !failed(p)) {
        bool disabled = take(p, "disabled");
        if (take(p, "value")) {
            declaration_parse(p, disabled, false, true);
            continue;
        }
        if (take(p, "action")) {
            declaration_parse(p, disabled, false, false);
            continue;
        }
        if (take(p, "event")) {
            declaration_parse(p, disabled, true, false);
            continue;
        }
        if (disabled) {
            error(p, "disabled requires an action or value declaration");
            break;
        }
        if (take(p, "on")) {
            if (!code_reactive_parse_event(p)) {
                continuation_parse(p, false);
            }
            continue;
        }
        if (take(p, "after")) {
            continuation_parse(p, true);
            continue;
        }
        if (take(p, "bind")) {
            code_reactive_parse_bind(p);
            continue;
        }
        if (take(p, "local")) {
            code_statement_parse_local(p);
            continue;
        }
        if (take(p, "state")) {
            state_parse(p, false);
            continue;
        }
        if (take(p, "shared")) {
            state_parse(p, true);
            continue;
        }
        parser look = *p;
        advance(&look);
        if ((p->token.kind == CODE_TOKEN_NAME || p->token.kind == CODE_TOKEN_STRING) &&
            is(&look, ":")) {
            state_parse(p, false);
            continue;
        }
        if (take(p, "events")) {
            char *scope_name = name(p);
            graph *g = scope_name
                         ? behavior_asset_add_graph(p->asset, GRAPH_EVENT, scope_name)
                         : NULL;
            if (!g) {
                error(p, graph_last_error());
                break;
            }
            scope *outer = p->scope;
            p->scope = new_scope(p, g);
            expect(p, "{");
            scope_parse(p);
            p->scope->end = (int)p->token.end;
            expect(p, "}");
            p->scope = outer;
            continue;
        }
        if (take(p, "pure")) {
            expect(p, "function");
            function_parse(p, false, true);
            continue;
        }
        if (take(p, "function")) {
            function_parse(p, false, false);
            continue;
        }
        if (take(p, "macro")) {
            function_parse(p, true, false);
            continue;
        }
        if (is(p, "entry") || is(p, "exit")) {
            bool result = is(p, "exit");
            int start = (int)p->token.start;
            advance(p);
            char *label = name(p);
            if (p->scope->native->kind != GRAPH_MACRO) {
                error(p, "Named entry and exit declarations require a macro");
                break;
            }
            node *tunnel = graph_node_by_id(
                p->scope->native,
                result ? p->scope->native->return_id : p->scope->native->entry_id
            );
            bool *defined = result ? &p->scope->exits_defined
                                   : &p->scope->entries_defined;
            if (!*defined) {
                int count = result ? tunnel->input_count : tunnel->output_count;
                for (int i = count - 1; i >= 0; i--) {
                    pin_spec *pin = result ? &tunnel->input_pins[i]
                                           : &tunnel->output_pins[i];
                    if (pin->kind == PIN_EXEC) {
                        graph_remove_pin(p->scope->native, tunnel, !result, i);
                    }
                }
                *defined = true;
            }
            pin_spec pin = { .name = label, .kind = PIN_EXEC, .type = PIN_FLOW };
            if (graph_add_pin(p->scope->native, tunnel, !result, &pin) < 0) {
                error(p, graph_last_error());
                break;
            }
            if (result) {
                expect(p, ";");
                continue;
            }
            continuation handler = { .source = keep(p, "entry"),
                                     .outcome = label,
                                     .start = start };
            handler.body = block(p);
            handler.end = (int)p->token.start;
            apush(p->scope->continuations, handler);
            continue;
        }
        if (take(p, "result")) {
            argument a = { .name_start = (int)p->token.start,
                           .name_end = (int)p->token.end };
            a.port.name = name(p);
            expect(p, ":");
            code_call_port t = type(p);
            t.name = a.port.name;
            a.port = t;
            expect(p, "=");
            a.text = expression_text(p, &a.start, &a.end);
            expect(p, ";");
            apush(p->scope->returns, a);
            continue;
        }
        if (take(p, "owner")) {
            char *owner = name(p);
            if (owner) {
                behavior_asset_set_owner_type(p->asset, owner);
            }
            expect(p, ";");
            continue;
        }
        if (take(p, "description")) {
            char *description = name(p);
            if (description) {
                behavior_asset_set_description(p->asset, description);
            }
            expect(p, ";");
            continue;
        }
        if (take(p, "domain")) {
            char *domain = name(p);
            bool valid = false;
            for (int i = 0; i <= BEHAVIOR_DOMAIN_MODEL; i++) {
                if (domain &&
                    !strcmp(domain, behavior_domain_name((behavior_domain)i))) {
                    p->asset->domain = (behavior_domain)i;
                    valid = true;
                }
            }
            if (!valid) {
                error(p, "Unknown behavior domain");
            }
            expect(p, ";");
            continue;
        }
        if (take(p, "dataflow")) {
            p->scope->native->dataflow = true;
            expect(p, ";");
            continue;
        }
        if (take(p, "depth_limit")) {
            char *n = slice(p, (int)p->token.start, (int)p->token.end);
            p->scope->native->call_depth_limit = atoi(n);
            advance(p);
            expect(p, ";");
            continue;
        }
        if (is(p, "query")) {
            p->program->query_start = (int)p->token.start;
            advance(p);
            char message[256];
            av_value *explicit_query = code_query_parse(
                p->program->values,
                &p->lexer,
                &p->token,
                message,
                sizeof message
            );
            if (explicit_query && p->program->query) {
                av_value *combined = av_obj(p->program->values);
                av_obj_add_str(p->program->values, combined, "op", "all");
                av_value *terms = av_obj_add_arr(p->program->values, combined, "terms");
                av_arr_add_val(terms, p->program->query);
                av_arr_add_val(terms, explicit_query);
                p->program->query = combined;
            } else if (explicit_query) {
                p->program->query = explicit_query;
            }
            if (!explicit_query) {
                error(p, message);
            }
            p->program->query_end = (int)p->token.end;
            expect(p, ";");
            continue;
        }
        if (p->program->system && p->program->system_header) {
            implicit_tick(p);
            continue;
        }
        error(p, "Expected a state, callable, function, event or outcome handler");
    }
}
static bool whole_atom(const char *text);
static node *build_declaration(parser *p, scope *s, declaration *d);
static bool source_value(
    parser *p,
    scope *s,
    const char *text,
    int offset,
    code_call_port *expected,
    code_call_argument *out
);
static void append_text(char **out, const char *text)
{
    for (int i = 0; text[i]; i++) {
        apush(*out, text[i]);
    }
}
static int named_pin(const node *n, bool output, const char *name)
{
    const pin_spec *ports = output ? n->output_pins : n->input_pins;
    int count = output ? n->output_count : n->input_count;
    int result = -1;
    for (int i = 0; i < count; i++) {
        if (!strcmp(ports[i].name, name)) {
            if (result >= 0) {
                return -1;
            }
            result = i;
        }
    }
    return result;
}
static parser expression_parser(parser *p, scope *s, const char *text, int offset)
{
    parser expression = *p;
    expression.scope = s;
    code_lex_init(&expression.lexer, text);
    advance(&expression);
    expression.base = offset;
    return expression;
}
static const graph_local *source_local(parser *p, scope *s, const char *name)
{
    for (int i = 0; name && i < acount(s->native->locals); i++) {
        if (!strcmp(s->native->locals[i].name, name)) {
            return &s->native->locals[i];
        }
    }
    /* Intermediate graph edits may prune a producer local before its first
       source use. Its declaration still owns the proven producer. */
    for (int i = 0; name && i < acount(s->locals); i++) {
        local_declaration *local = &s->locals[i];
        if (strcmp(local->name, name)) {
            continue;
        }
        if (!local->has_resolved && !code_statement_lower_local(p, s, local)) {
            return NULL;
        }
        if (local->resolved.kind != CODE_CALL_OUTPUT) {
            return NULL;
        }
        const graph_local *existing = graph_local_by_id(s->native, local->id);
        if (existing) {
            return existing;
        }
        uint64_t created = graph_declare_local(
            s->native,
            local->resolved.source.node,
            local->resolved.source.pin,
            local->name
        );
        if (!created) {
            return NULL;
        }
        for (int j = 0; j < acount(s->native->locals); j++) {
            graph_local *native = &s->native->locals[j];
            if (native->id != created) {
                continue;
            }
            /* Preserve IDs already captured by other pending arguments. */
            native->id = local->id;
            return native;
        }
        assert(false);
    }
    return NULL;
}
static bool binding_value(parser *p, scope *s, const char *text, int offset, code_call_argument *out)
{
    parser value = expression_parser(p, s, text, offset);
    if (!take(&value, "&")) {
        return false;
    }
    char *root = name(&value);
    if (!root) {
        return false;
    }
    out->kind = CODE_CALL_BINDING;
    if (!strcmp(root, "delta")) {
        out->binding.kind = PIN_BINDING_DELTA_TIME;
        return true;
    }
    if (!strcmp(root, "state")) {
        expect(&value, ".");
        char *n = name(&value);
        behavior_variable *v = n ? behavior_asset_find_variable_by_name(p->asset, n)
                                 : NULL;
        if (!v) {
            code_source_error(p->source, offset, offset + (int)strlen(text), "Unknown state binding");
            return false;
        }
        out->binding.kind = PIN_BINDING_VARIABLE;
        out->binding.variable_id = v->id;
        return true;
    }
    if (!strcmp(root, "component")) {
        expect(&value, "(");
        out->binding.component_key = reference_name(&value);
        char message[256];
        const char *component = out->binding.component_key
                                  ? code_source_context_reference(
                                        &p->source->context,
                                        CODE_REFERENCE_COMPONENT,
                                        out->binding.component_key,
                                        message,
                                        sizeof message
                                    )
                                  : NULL;
        if (!component) {
            if (out->binding.component_key) {
                code_source_error(p->source, offset, offset + (int)strlen(text), message);
            }
            return false;
        }
        out->binding.component_key = graph_own_str(s->native, component, strlen(component));
        if (take(&value, ",")) {
            out->binding.field_name = name(&value);
        }
        expect(&value, ")");
        out->binding.kind = out->binding.field_name ? PIN_BINDING_COMPONENT_FIELD
                                                    : PIN_BINDING_COMPONENT;
        return !failed(p);
    }
    if (!strcmp(root, "local")) {
        expect(&value, ".");
        char *n = name(&value);
        const graph_local *local = source_local(p, s, n);
        if (local) {
            out->binding.kind = PIN_BINDING_LOCAL;
            out->binding.local_id = local->id;
            return true;
        }
        code_source_error(p->source, offset, offset + (int)strlen(text), "Unknown local binding");
        return false;
    }
    declaration *event = find(s, root);
    if (event) {
        node *n = build_declaration(p, s, event);
        expect(&value, ".");
        char *member = name(&value);
        if (n && member && n->desc == &NODE_Event && n->reference_name &&
            !strcmp(n->reference_name, "Tick") && !strcmp(member, "dt")) {
            member = "DeltaSeconds";
        }
        int pin = n && member ? named_pin(n, true, member) : -1;
        if (pin >= 0) {
            out->binding.kind = PIN_BINDING_EVENT_OUTPUT;
            out->binding.event_node_id = n->id;
            out->binding.event_pin_id = n->output_pins[pin].id;
            return true;
        }
    }
    code_source_error(p->source, offset, offset + (int)strlen(text), "Unknown reflected binding");
    return false;
}
static node *build_call(parser *p, scope *s, code_call_spec spec, argument *arguments, int start, int end)
{
    code_call_port *ports = NULL;
    code_call_argument *args = NULL;
    char message[256];
    const graph *local = !spec.native && !spec.reference && !spec.reference_asset
                           ? behavior_asset_find_graph_by_name(p->asset, spec.name)
                           : NULL;
    bool authored = !spec.native && !spec.reference &&
                    (spec.reference_asset ||
                     (local && (local->kind == GRAPH_FUNCTION ||
                                local->kind == GRAPH_MACRO)));
    const node_desc *desc = authored
                              ? NULL
                              : code_call_descriptor(spec.name, message, sizeof message);
    if (!desc && spec.native) {
        code_source_error(p->source, start, end, message);
        return NULL;
    }
    if (!desc) {
        spec.target = code_source_context_graph(
            &p->source->context,
            p->asset,
            spec.reference_asset,
            spec.name,
            GRAPH_FUNCTION,
            &spec.reference_asset,
            message,
            sizeof message
        );
        if (!spec.target) {
            spec.target = code_source_context_graph(
                &p->source->context,
                p->asset,
                spec.reference_asset,
                spec.name,
                GRAPH_MACRO,
                &spec.reference_asset,
                message,
                sizeof message
            );
        }
        if (!spec.target) {
            code_source_error(p->source, start, end, message);
            return NULL;
        }
        bool pure = spec.target->kind == GRAPH_FUNCTION;
        const node *entry = graph_node_by_id(spec.target, spec.target->entry_id);
        for (int i = 0; entry && i < entry->output_count; i++) {
            if (entry->output_pins[i].kind == PIN_EXEC) {
                pure = false;
            }
        }
        spec.reference = spec.target->name;
        spec.name = spec.target->kind == GRAPH_MACRO ? "call_macro"
                  : pure                             ? "call_pure_function"
                                                     : "call_function";
        desc = code_call_descriptor(spec.name, message, sizeof message);
    }
    if (spec.reference && desc &&
        (desc == &NODE_CallFunction || desc == &NODE_CallPureFunction ||
         desc == &NODE_CallMacro)) {
        spec.target = code_source_context_graph(
            &p->source->context,
            p->asset,
            spec.reference_asset,
            spec.reference,
            desc == &NODE_CallMacro ? GRAPH_MACRO : GRAPH_FUNCTION,
            &spec.reference_asset,
            message,
            sizeof message
        );
        if (!spec.target) {
            code_source_error(p->source, start, end, message);
            return NULL;
        }
    }
    if (spec.reference && desc &&
        (desc == &NODE_Event || !strcmp(desc->name, "Send Event") ||
         !strcmp(desc->name, "Call Event")) &&
        !graph_event_host_name(spec.reference)) {
        const behavior_asset *event = code_source_context_event(
            &p->source->context,
            spec.reference,
            message,
            sizeof message
        );
        if (event) {
            spec.event = event;
        }
    }
    for (int i = 0; i < acount(arguments) && !failed(p); i++) {
        argument *a = &arguments[i];
        if (!a->annotated) {
            const pin_spec *template = NULL;
            node shape = { 0 };
            if (spec.target) {
                const node *entry = graph_node_by_id(spec.target, spec.target->entry_id);
                if (entry) {
                    shape.input_pins = entry->output_pins;
                    shape.input_count = entry->output_count;
                }
            } else if (desc) {
                shape.input_pins = (pin_spec *)desc->inputs;
                shape.input_count = desc->input_count;
            }
            int port = -1;
            if (a->positional) {
                int ordinal = 0;
                for (int j = 0; j < shape.input_count; j++) {
                    if (shape.input_pins[j].kind != PIN_DATA ||
                        shape.input_pins[j].field_source) {
                        continue;
                    }
                    if (ordinal++ == a->ordinal) {
                        port = j;
                        break;
                    }
                }
                if (port < 0) {
                    code_source_error(p->source, a->start, a->end, "Too many positional callable arguments");
                    break;
                }
            } else {
                port = code_call_port_index(&shape, false, a->port.name, message, sizeof message);
            }
            if (port >= 0) {
                template = &shape.input_pins[port];
            }
            if (!template && shape.input_count &&
                (shape.input_pins[shape.input_count - 1].flags & PIN_FLAG_WILDCARD)) {
                template = &shape.input_pins[shape.input_count - 1];
            }
            if (template) {
                a->port.name = template->name;
                a->port.type = template->type;
                a->port.type_name = template->type_name;
                a->port.container = template->container;
                a->port.kind = template->kind;
                a->port.reference_kind = spec.target ? CODE_REFERENCE_NONE
                                                     : code_call_reference_kind(
                                                           desc,
                                                           template->name
                                                       );
            }
        }
        if (a->annotated) {
            apush(ports, a->port);
        }
        if (a->port.kind == PIN_EXEC) {
            continue;
        }
        code_call_argument input = { .name = a->port.name,
                                     .positional = a->positional,
                                     .ordinal = a->ordinal };
        bool valid = a->has_resolved && !a->text;
        if (valid) {
            input = a->resolved;
            input.name = a->port.name;
        } else {
            valid = a->text && source_value(p, s, a->text, a->start, &a->port, &input);
        }
        if (!valid) {
            if (!failed(p)) {
                code_source_error(p->source, a->start, a->end, "Invalid callable argument");
            }
            break;
        }
        input.name = a->port.name;
        input.positional = a->positional;
        input.ordinal = a->ordinal;
        if (input.kind == CODE_CALL_BINDING) {
            code_call_port actual = { 0 };
            if (!code_statement_value_type(p, s, &input, &actual)) {
                code_source_error(p->source, a->start, a->end, "Cannot resolve callable binding type");
                break;
            }
            input.has_binding_type = true;
            input.binding_type = actual.type;
            input.binding_type_name = actual.type_name;
            input.binding_container = actual.container;
        }
        a->resolved = input;
        a->has_resolved = true;
        apush(args, input);
    }
    spec.inputs = ports;
    spec.input_count = acount(ports);
    spec.arguments = args;
    spec.argument_count = acount(args);
    spec.complete_inputs = false;
    if (desc == &NODE_BehaviorExpression) {
        spec.expression_mode = p->expression_mode;
        spec.expression = spec.reference;
        spec.reference = NULL;
    }

    node *n = failed(p)
                ? NULL
                : code_call_build(p->asset, s->native, &spec, message, sizeof message);
    if (n) {
        code_program_record_inputs(p->program, n, args, acount(args));
    }
    if (!n && !failed(p)) {
        code_source_error(p->source, start, end, message);
    }
    afree(ports);
    afree(args);
    return n;
}
static node *build_declaration(parser *p, scope *s, declaration *d)
{
    if (d->native) {
        return d->native;
    }
    if (d->building) {
        code_source_error(p->source, d->start, d->end, "Cyclic lazy value dependency");
        return NULL;
    }
    d->building = 1;
    bool borrowed = false;
    int prior_nodes = acount(s->native->nodes);
    if (d->expression) {
        code_call_argument result = { 0 };
        code_call_port expected = acount(d->results) == 1
                                    ? d->results[0]
                                    : (code_call_port){ .kind = PIN_DATA };
        graph_expression_mode previous_mode = p->expression_mode;
        p->expression_mode = d->lazy ? GRAPH_EXPRESSION_VALUE : GRAPH_EXPRESSION_NONE;
        code_lexer expression_lexer;
        code_lex_init(&expression_lexer, d->expression);
        code_token first = code_lex_next(&expression_lexer);
        bool cast = code_token_is(&expression_lexer, first, "cast");
        bool self_value = code_token_is(&expression_lexer, first, "self") &&
                          code_lex_next(&expression_lexer).kind == CODE_TOKEN_END;
        if (self_value && !acount(d->results)) {
            expected.type = PIN_HANDLE;
            expected.type_name = p->asset->owner_type_name ? p->asset->owner_type_name
                                                           : "ecs.entity";
        }
        int component = !cast && !acount(d->results)
                          ? code_components_value(p, s, d->expression, d->expression_start, &expected, &result)
                          : -1;
        bool valid = component >= 0 ? component == 1
                   : (acount(d->results) == 1 || self_value) &&
                             whole_atom(d->expression) && !cast &&
                             !code_statement_math_call(p, d->expression)
                       ? source_value(p, s, d->expression, d->expression_start, &expected, &result)
                       : code_statement_expression(p, s, d->expression, d->expression_start, &expected, &result);
        if (valid && result.kind == CODE_CALL_OUTPUT) {
            d->native = result.source.node;
            bool existing = false;
            for (int i = 0; i < prior_nodes; i++) {
                existing |= s->native->nodes[i] == d->native;
            }
            if (!existing && acount(d->results) == 1 &&
                d->native->desc == &NODE_BehaviorExpression &&
                expected.container == PIN_CONTAINER_SINGLE &&
                expected.type >= PIN_I8 && expected.type <= PIN_F64) {
                bool explicit_number = false;
                for (int i = 0; i < acount(p->program->expressions); i++) {
                    const code_expression_source *expression = &p->program->expressions[i];
                    if (expression->native != d->native) {
                        continue;
                    }
                    for (int j = 0; j < acount(expression->reads); j++) {
                        explicit_number |= !expression->reads[j].contextual_number &&
                                           (expression->reads[j].resolved.has_type ||
                                            expression->reads[j].type == PIN_F64);
                    }
                }
                graph *generated = behavior_asset_find_graph(p->asset, d->native->reference_id);
                behavior_expression_error error;
                if (explicit_number &&
                    d->native->output_pins[result.source.pin].type != expected.type) {
                    code_call_argument input = result;
                    input.name = "_cast_value";
                    input.has_type = true;
                    input.type = expected.type;
                    code_call_port parameter = expected;
                    parameter.name = input.name;
                    code_call_port output = expected;
                    output.name = "Result";
                    code_call_spec cast = {
                        .name = "Behavior Expression",
                        .expression = input.name,
                        .expression_mode = GRAPH_EXPRESSION_VALUE,
                        .arguments = &input,
                        .argument_count = 1,
                        .inputs = &parameter,
                        .input_count = 1,
                        .results = &output,
                        .result_count = 1,
                    };
                    char message[192];
                    d->native = code_call_build(p->asset, s->native, &cast, message, sizeof message);
                    result.source = (code_call_output){ d->native, 0 };
                    if (!d->native) {
                        code_source_error(p->source, d->expression_start, d->end, message);
                    }
                } else if (
                    d->native->output_pins[result.source.pin].type != expected.type &&
                    (graph_set_pin_type(
                         s->native,
                         d->native,
                         true,
                         result.source.pin,
                         expected.type,
                         expected.type_name
                     ) ||
                     behavior_expression_refresh_mode(
                         p->asset,
                         d->native,
                         generated->expression_mode,
                         &error
                     ))
                ) {
                    code_source_error(p->source, d->expression_start, d->end, "Cannot apply computed expression result type");
                    d->native = NULL;
                }
            }
            if (d->native && acount(d->results) == 1) {
                pin_spec *actual = &d->native->output_pins[result.source.pin];
                if (!existing && d->native->desc != &NODE_BehaviorExpression &&
                    d->native->desc != &NODE_CallPureFunction &&
                    (actual->flags & PIN_FLAG_WILDCARD) &&
                    (actual->type != expected.type ||
                     actual->container != expected.container)) {
                    if (graph_set_pin_type(
                            s->native,
                            d->native,
                            true,
                            result.source.pin,
                            expected.type,
                            expected.type_name
                        ) ||
                        graph_set_pin_container(
                            s->native,
                            d->native,
                            true,
                            result.source.pin,
                            expected.container
                        )) {
                        code_source_error(p->source, d->expression_start, d->end, "Cannot specialize the computed native result type");
                        d->native = NULL;
                    }
                }
            }
            if (d->native && acount(d->results) == 1) {
                const pin_spec *output = &d->native->output_pins[result.source.pin];
                pin_spec type = { .type = expected.type,
                                  .type_name = expected.type_name,
                                  .container = expected.container,
                                  .kind = PIN_DATA };
                if (result.conversion || !pin_compatible(output, &type)) {
                    code_source_error(p->source, d->expression_start, d->end, "Computed value is incompatible with its declared type");
                    d->native = NULL;
                } else if (
                    expected.has_traits &&
                    ((output->flags & (PIN_FLAG_REFERENCE | PIN_FLAG_SERVICE |
                                       PIN_FLAG_DEFAULT_SELF | PIN_FLAG_SETTING |
                                       PIN_FLAG_WILDCARD)) != expected.flags ||
                     ((expected.flags & PIN_FLAG_WILDCARD) &&
                      output->group != expected.group))
                ) {
                    code_source_error(p->source, d->expression_start, d->end, "Computed value does not match its declared qualifiers");
                    d->native = NULL;
                } else {
                    d->results[0].name = output->name;
                }
            }
        }
        if (d->native && valid && result.kind == CODE_CALL_OUTPUT) {
            d->expression_output = result.source;
        }
        p->expression_mode = previous_mode;
        for (int i = 0; d->native && i < prior_nodes; i++) {
            borrowed |= s->native->nodes[i] == d->native;
        }
        for (int i = 0; d->native && i < acount(s->declarations); i++) {
            const declaration *owner = s->declarations[i];
            borrowed |= owner != d && owner->native == d->native;
        }
        bool executable = false;
        if (d->native) {
            for (int i = 0; i < d->native->input_count; i++) {
                executable |= d->native->input_pins[i].kind == PIN_EXEC;
            }
            for (int i = 0; i < d->native->output_count; i++) {
                executable |= d->native->output_pins[i].kind == PIN_EXEC;
            }
        }
        if (d->lazy && executable && !borrowed) {
            code_source_error(p->source, d->expression_start, d->end, "Lazy value expressions cannot mutate state");
            d->native = NULL;
        }
    } else {
        d->call.complete_results = d->results != NULL && strcmp(d->call.name, "Event");
        graph_expression_mode previous_mode = p->expression_mode;
        p->expression_mode = d->lazy ? GRAPH_EXPRESSION_VALUE : GRAPH_EXPRESSION_NONE;
        d->native = build_call(p, s, d->call, d->arguments, d->start, d->end);
        p->expression_mode = previous_mode;
    }
    if (d->native) {
        if (!borrowed) {
            d->native->disabled = d->disabled;
        }
        code_intelligence_location(
            p->source,
            d->symbol,
            s->native->graph_id,
            d->native->id,
            0
        );
    }
    d->building = 0;
    return d->native;
}
static bool resolve_output(graph *g, node *n, const char *member, code_call_output *out, char *message, size_t size)
{
    if (code_call_resolve_output(g, n, member, out, message, size)) {
        return true;
    }
    int aggregate = -1;
    for (int i = 0; i < n->output_count; i++) {
        if (n->output_pins[i].type != PIN_STRUCT || n->output_pins[i].field_source) {
            continue;
        }
        if (aggregate >= 0) {
            return false;
        }
        aggregate = i;
    }
    if (aggregate < 0) {
        return false;
    }
    size_t length = strlen(n->output_pins[aggregate].name) + strlen(member) + 2;
    char *path = xmalloc(length);
    snprintf(path, length, "%s.%s", n->output_pins[aggregate].name, member);
    bool resolved = code_call_resolve_output(g, n, path, out, message, size);
    free(path);
    return resolved;
}
static bool projected_value(parser *p, scope *s, const char *text, int offset, code_call_argument *out)
{
    parser value = expression_parser(p, s, text, offset);
    char *root = name(&value);
    if (!root) {
        return false;
    }
    declaration *d = find(s, root);
    if (!d) {
        return false;
    }
    char *member = NULL;
    if (take(&value, ".")) {
        member = name(&value);
    } else if (take(&value, "[")) {
        member = name(&value);
        expect(&value, "]");
    }
    while (member && take(&value, ".")) {
        char *field = name(&value);
        char *path = NULL;
        append_text(&path, member);
        append_text(&path, ".");
        if (field) {
            append_text(&path, field);
        }
        apush(path, 0);
        member = keep(p, path);
        afree(path);
    }
    if (value.token.kind != CODE_TOKEN_END) {
        return false;
    }
    node *n = build_declaration(p, s, d);
    if (d->expression_output.node) {
        const char *output = n->output_pins[d->expression_output.pin].name;
        bool result_projection = member && !strcmp(member, "Result") &&
                                 n->output_pins[d->expression_output.pin].type !=
                                     PIN_STRUCT;
        if (member && strcmp(member, output) && !result_projection) {
            char *path = NULL;
            append_text(&path, output);
            append_text(&path, ".");
            append_text(&path, member);
            apush(path, 0);
            member = keep(p, path);
            afree(path);
        } else {
            member = (char *)output;
        }
    }
    if (!member && n) {
        for (int i = 0; i < acount(d->results); i++) {
            if (d->results[i].kind == PIN_DATA) {
                if (member) {
                    code_source_error(p->source, offset, offset + (int)strlen(text), "Multiple results require a named projection");
                    return false;
                }
                member = (char *)d->results[i].name;
            }
        }
    }
    if (!member && n) {
        for (int i = 0; i < n->output_count; i++) {
            if (n->output_pins[i].kind == PIN_DATA && !n->output_pins[i].field_source) {
                if (member) {
                    code_source_error(p->source, offset, offset + (int)strlen(text), "Multiple results require a named projection");
                    return false;
                }
                member = (char *)n->output_pins[i].name;
            }
        }
    }
    if (!member) {
        return false;
    }
    char message[256];
    if (n && member && n->desc == &NODE_Event && n->reference_name &&
        !strcmp(n->reference_name, "Tick") && !strcmp(member, "dt")) {
        member = "DeltaSeconds";
    }
    bool resolved = n &&
                    resolve_output(s->native, n, member, &out->source, message, sizeof message);
    if (!resolved) {
        if (!failed(p)) {
            code_source_error(p->source, offset, offset + (int)strlen(text), message);
        }
        return false;
    }
    out->kind = CODE_CALL_OUTPUT;
    code_intelligence_use(
        p->source,
        (code_semantic_use){ d->symbol, offset, offset + (int)strlen(root), CODE_USE_READ }
    );
    return true;
}
static bool inline_value(
    parser *p,
    scope *s,
    const char *text,
    int offset,
    code_call_port *expected,
    code_call_argument *out
)
{
    parser value = expression_parser(p, s, text, offset);
    bool builtin_cast = is(&value, "cast");
    bool builtin_fallback = is(&value, "fallback");
    bool native = false;
    char *call_name = callable(&value, &native);
    if (!call_name) {
        return false;
    }
    code_call_spec call = { .name = call_name, .native = native };
    if (!native && builtin_fallback && !strcmp(call_name, "fallback")) {
        expect(&value, "(");
        int start, end;
        char *primary = expression_text(&value, &start, &end);
        code_call_port inferred = { .kind = PIN_DATA, .type = PIN_FLOW };
        if (!primary || !source_value(p, s, primary, offset + start, &inferred, out) ||
            out->kind == CODE_CALL_LITERAL || out->has_fallback_binding ||
            out->has_fallback_default) {
            code_source_error(p->source, offset, offset + (int)strlen(text), "Fallback requires a driver or binding value");
            return false;
        }
        while (take(&value, ",")) {
            bool binding = take(&value, "binding");
            bool fallback_default = !binding && take(&value, "default");
            if (!binding && !fallback_default) {
                error(&value, "Expected binding or default fallback");
                return false;
            }
            expect(&value, ":");
            char *item = expression_text(&value, &start, &end);
            if (!item) {
                return false;
            }
            if (binding) {
                code_call_argument resolved = { 0 };
                if (out->kind != CODE_CALL_OUTPUT || out->has_fallback_binding ||
                    !binding_value(p, s, item, offset + start, &resolved)) {
                    code_source_error(p->source, offset + start, offset + end, "Fallback binding requires one driver and one reflected binding");
                    return false;
                }
                code_call_port actual = { 0 };
                if (!code_statement_value_type(p, s, &resolved, &actual)) {
                    code_source_error(p->source, offset + start, offset + end, "Cannot resolve fallback binding type");
                    return false;
                }
                pin_spec binding_type = { .kind = PIN_DATA,
                                          .type = actual.type,
                                          .type_name = actual.type_name,
                                          .container = actual.container };
                code_call_port target = *expected;
                if (target.type == PIN_FLOW &&
                    !code_statement_value_type(p, s, out, &target)) {
                    code_source_error(p->source, offset + start, offset + end, "Cannot resolve fallback driver type");
                    return false;
                }
                pin_spec target_type = { .kind = PIN_DATA,
                                         .type = target.type,
                                         .type_name = target.type_name,
                                         .container = target.container };
                if (!pin_compatible(&binding_type, &target_type)) {
                    code_source_error(p->source, offset + start, offset + end, "Fallback binding is incompatible with its input type");
                    return false;
                }
                code_program_resolved_use(p, s, item, offset + start, &resolved, CODE_USE_READ);
                out->has_fallback_binding = true;
                out->fallback_binding = resolved.binding;
            } else {
                code_call_port actual = *expected;
                if (out->has_fallback_default ||
                    (actual.type == PIN_FLOW &&
                     !code_statement_value_type(p, s, out, &actual)) ||
                    !literal(p, s->native, &actual, item, &out->fallback_default)) {
                    code_source_error(p->source, offset + start, offset + end, "Fallback default must be a compatible literal");
                    return false;
                }
                out->has_fallback_default = true;
            }
        }
        expect(&value, ")");
        if ((!out->has_fallback_binding && !out->has_fallback_default) ||
            value.token.kind != CODE_TOKEN_END) {
            error(&value, "Fallback requires binding or default clauses");
            return false;
        }
        return !failed(p);
    }
    if (!native && builtin_cast && !strcmp(call_name, "cast")) {
        expect(&value, "<");
        bool previous_numeric = value.numeric_type;
        value.numeric_type = true;
        code_call_port target = type(&value);
        value.numeric_type = previous_numeric;
        expect(&value, ">");
        expect(&value, "(");
        int start, end;
        char *input = expression_text(&value, &start, &end);
        expect(&value, ")");
        if (failed(p) || !input || value.token.kind != CODE_TOKEN_END) {
            return false;
        }
        code_call_port inferred = { .kind = PIN_DATA, .type = PIN_FLOW };
        if (!source_value(p, s, input, offset + start, &inferred, out)) {
            return false;
        }
        code_call_port actual = { 0 };
        if (!code_statement_value_type(p, s, out, &actual)) {
            return false;
        }
        bool numeric_actual = actual.type >= PIN_BOOL && actual.type <= PIN_F64;
        bool numeric_target = target.type >= PIN_BOOL && target.type <= PIN_F64;
        pin_spec from = { .kind = PIN_DATA,
                          .type = actual.type,
                          .container = actual.container };
        pin_spec to = { .kind = PIN_DATA,
                        .type = target.type,
                        .container = target.container };
        const node_desc *conversion = conversion_for_pins(&from, &to);
        if (!numeric_actual || !numeric_target ||
            actual.container != PIN_CONTAINER_SINGLE ||
            target.container != PIN_CONTAINER_SINGLE || target.kind != PIN_DATA ||
            (!pin_compatible(&from, &to) && !conversion)) {
            code_source_error(p->source, offset, offset + (int)strlen(text), "Unsupported scalar numeric cast");
            return false;
        }
        if (out->has_type && out->type != target.type) {
            char message[192];
            if (!code_call_materialize_cast(p->asset, s->native, out, message, sizeof message)) {
                code_source_error(p->source, offset, offset + (int)strlen(text), message);
                return false;
            }
        }
        if (out->kind == CODE_CALL_LITERAL) {
            if (out->conversion) {
                out->literal = out->conversion->fn(&out->literal, 1, NULL);
                out->conversion = NULL;
            }
            if (conversion) {
                out->literal = conversion->fn(&out->literal, 1, NULL);
            }
            pin_coerce(&out->literal, &to);
        } else if (out->kind == CODE_CALL_OUTPUT) {
            if (out->conversion && conversion) {
                code_source_error(p->source, offset, offset + (int)strlen(text), "Chained numeric conversions require a named value");
                return false;
            }
            if (conversion) {
                out->conversion = conversion;
            }
        }
        out->has_type = true;
        out->type = target.type;
        if (out->has_fallback_binding || out->has_fallback_default) {
            char message[192];
            if (!code_call_materialize_cast_fallback(p->asset, s->native, out, message, sizeof message)) {
                code_source_error(p->source, offset, offset + (int)strlen(text), message);
                return false;
            }
        }
        return true;
    }
    if (take(&value, ".")) {
        call.reference_asset = call.name;
        call.name = name(&value);
    }
    behavior_variable *collection = call.reference_asset
                                      ? behavior_asset_find_variable_by_name(p->asset, call.reference_asset)
                                      : NULL;
    if (collection && collection->container == PIN_CONTAINER_SET &&
        (!strcmp(call.name, "contains") || !strcmp(call.name, "length"))) {
        bool contains = !strcmp(call.name, "contains");
        expect(&value, "(");
        code_call_port ports[2] = { { .name = "Set",
                                      .type = collection->type,
                                      .type_name = collection->type_name,
                                      .container = PIN_CONTAINER_SET,
                                      .kind = PIN_DATA },
                                    { .name = "Item",
                                      .type = collection->type,
                                      .type_name = collection->type_name,
                                      .kind = PIN_DATA } };
        code_call_argument inputs[2] = {
            { .name = "Set",
              .kind = CODE_CALL_BINDING,
              .binding = { .kind = PIN_BINDING_VARIABLE,
                           .variable_id = collection->id } }
        };
        if (contains) {
            int start, end;
            char *item = expression_text(&value, &start, &end);
            inputs[1].name = "Item";
            if (!source_value(p, s, item, offset + start, &ports[1], &inputs[1])) {
                return false;
            }
        }
        expect(&value, ")");
        code_call_spec operation = { .name = contains ? "set_contains" : "set_length",
                                     .inputs = ports,
                                     .input_count = contains ? 2 : 1,
                                     .arguments = inputs,
                                     .argument_count = contains ? 2 : 1 };
        char message[256];
        node *n = code_call_build(p->asset, s->native, &operation, message, sizeof message);
        if (!n) {
            code_source_error(p->source, offset, offset + (int)strlen(text), message);
            return false;
        }
        out->kind = CODE_CALL_OUTPUT;
        out->source = (code_call_output){ n, 0 };
        return value.token.kind == CODE_TOKEN_END;
    }
    if (!strcmp(call_name, "convert")) {
        expect(&value, "<");
        char *conversion_name = name(&value);
        expect(&value, ">");
        expect(&value, "(");
        int start, end;
        char *input = expression_text(&value, &start, &end);
        expect(&value, ")");
        const node_desc *conversion = conversion_name
                                        ? registry_node_by_name(conversion_name)
                                        : NULL;
        if (!conversion || conversion->input_count != 1 ||
            conversion->output_count != 1) {
            code_source_error(p->source, offset, offset + (int)strlen(text), "Unknown scalar conversion");
            return false;
        }
        code_call_port type = { .type = conversion->inputs[0].type,
                                .type_name = conversion->inputs[0].type_name,
                                .container = conversion->inputs[0].container,
                                .kind = PIN_DATA };
        if (!source_value(p, s, input, offset + start, &type, out)) {
            return false;
        }
        out->conversion = conversion;
        return value.token.kind == CODE_TOKEN_END;
    }
    if (take(&value, "<")) {
        call.reference = reference_name(&value);
        if (take(&value, ",")) {
            call.reference_asset = name(&value);
        }
        expect(&value, ">");
    }
    if (!is(&value, "(")) {
        return false;
    }
    argument *args = arguments(&value, false);
    char *member = NULL;
    if (take(&value, ".")) {
        member = name(&value);
    } else if (take(&value, "[")) {
        member = name(&value);
        expect(&value, "]");
    }
    while (member && take(&value, ".")) {
        char *field = name(&value);
        if (!field) {
            break;
        }
        size_t length = strlen(member) + strlen(field) + 2;
        char *path = xmalloc(length);
        snprintf(path, length, "%s.%s", member, field);
        member = keep(p, path);
        free(path);
    }
    if (value.token.kind != CODE_TOKEN_END) {
        afree(args);
        return false;
    }
    node *n = build_call(p, s, call, args, offset, offset + (int)strlen(text));
    afree(args);
    if (!n) {
        return false;
    }
    for (int i = 0; i < n->input_count; i++) {
        if (n->input_pins[i].kind != PIN_EXEC) {
            continue;
        }
        code_source_error(p->source, offset, offset + (int)strlen(text), "A nested value call must be pure");
        return false;
    }
    int pin = -1;
    if (member) {
        char message[256];
        if (!resolve_output(s->native, n, member, &out->source, message, sizeof message)) {
            code_source_error(p->source, offset, offset + (int)strlen(text), message);
            return false;
        }
        out->kind = CODE_CALL_OUTPUT;
        return true;
    } else {
        for (int i = 0; i < n->output_count; i++) {
            if (n->output_pins[i].kind == PIN_DATA && !n->output_pins[i].field_source) {
                if (pin >= 0) {
                    code_source_error(p->source, offset, offset + (int)strlen(text), "Multiple result values require a named projection");
                    return false;
                }
                pin = i;
            }
        }
    }
    if (pin < 0) {
        code_source_error(p->source, offset, offset + (int)strlen(text), "Callable has no requested value result");
        return false;
    }
    out->kind = CODE_CALL_OUTPUT;
    out->source = (code_call_output){ n, pin };
    return true;
}
static bool whole_atom(const char *text)
{
    code_lexer lexer;
    code_lex_init(&lexer, text);
    int round = 0, square = 0, angle = 0;
    code_token previous = { .kind = CODE_TOKEN_END };
    for (;;) {
        code_token t = code_lex_next(&lexer);
        if (t.kind == CODE_TOKEN_END) {
            return !round && !square && !angle;
        }
        if (t.kind == CODE_TOKEN_ERROR) {
            return false;
        }
        if (t.punctuation == '(') {
            round++;
        } else if (t.punctuation == ')') {
            round--;
        } else if (t.punctuation == '[') {
            square++;
        } else if (t.punctuation == ']') {
            square--;
        }
        if (round || square) {
            previous = t;
            continue;
        }
        if (t.punctuation == '<') {
            if (previous.kind != CODE_TOKEN_NAME &&
                previous.kind != CODE_TOKEN_STRING && previous.punctuation != ']') {
                return false;
            }
            code_lexer look = lexer;
            code_token close;
            do {
                close = code_lex_next(&look);
                if (close.kind != CODE_TOKEN_NAME && close.kind != CODE_TOKEN_STRING &&
                    close.kind != CODE_TOKEN_NUMBER && close.punctuation != ',' &&
                    close.punctuation != '.' && close.punctuation != '>') {
                    return false;
                }
            } while (close.kind != CODE_TOKEN_END && close.punctuation != '>');
            code_token following = code_lex_next(&look);
            if (close.punctuation != '>' || following.punctuation != '(') {
                return false;
            }
            angle++;
            previous = t;
            continue;
        }
        if (t.kind == CODE_TOKEN_NUMBER && t.start &&
            (text[t.start] == '+' || text[t.start] == '-')) {
            return false;
        }
        if (t.punctuation == '>' && angle) {
            angle--;
            previous = t;
            continue;
        }
        if (angle) {
            previous = t;
            continue;
        }
        if (t.punctuation == '+' || t.punctuation == '-' ||
            t.punctuation == '*' || t.punctuation == '/' || t.punctuation == '%' ||
            t.punctuation == '=' || t.punctuation == '!' || t.punctuation == '|' ||
            t.punctuation == '?' || t.punctuation == '>') {
            return false;
        }
        if (t.punctuation == '&' && t.start != 0) {
            return false;
        }
        previous = t;
    }
}
static bool value_expression(
    parser *p,
    scope *s,
    const char *text,
    int offset,
    code_call_port *expected,
    code_call_argument *out
)
{
    graph_expression_mode previous_mode = p->expression_mode;
    p->expression_mode = GRAPH_EXPRESSION_VALUE;
    bool valid = code_statement_expression(p, s, text, offset, expected, out);
    p->expression_mode = previous_mode;
    if (valid && out->kind == CODE_CALL_OUTPUT &&
        out->source.node->desc == &NODE_BehaviorExpression) {
        const graph
            *generated = behavior_asset_find_graph(p->asset, out->source.node->reference_id);
        if (generated && generated->expression_mode == GRAPH_EXPRESSION_ACTION) {
            code_source_error(p->source, offset, offset + (int)strlen(text), "Value expressions cannot mutate state");
            valid = false;
        }
    }
    return valid;
}
static bool source_value(
    parser *p,
    scope *s,
    const char *text,
    int offset,
    code_call_port *expected,
    code_call_argument *out
)
{
    if (++p->depth > 128) {
        code_source_error(p->source, offset, offset + (int)strlen(text), "Expression nesting exceeds 128 levels");
        p->depth--;
        return false;
    }
    bool valid = false;
    int component = code_components_value(p, s, text, offset, expected, out);
    if (component >= 0) {
        valid = component != 0;
        goto done;
    }
    int method = code_statement_method(p, s, text, offset, out);
    if (method >= 0) {
        valid = method != 0;
        goto done;
    }
    code_lexer completion_lexer;
    code_lex_init(&completion_lexer, text);
    code_token completion_token = code_lex_next(&completion_lexer);
    if (completion_token.punctuation == '&') {
        completion_token = code_lex_next(&completion_lexer);
    }
    code_program_complete_value(
        p,
        s,
        offset + (int)completion_token.start,
        offset + (int)completion_token.end,
        expected
    );
    if (literal(p, s->native, expected, text, &out->literal)) {
        out->kind = CODE_CALL_LITERAL;
        valid = true;
        goto done;
    }
    if (failed(p)) {
        goto done;
    }
    if (text[0] == '&' && whole_atom(text)) {
        valid = binding_value(p, s, text, offset, out);
        goto done;
    }
    code_lexer lexer;
    code_lex_init(&lexer, text);
    code_token first = code_lex_next(&lexer), next = code_lex_next(&lexer);
    if (!whole_atom(text)) {
        valid = value_expression(p, s, text, offset, expected, out);
        goto done;
    }
    if (first.kind == CODE_TOKEN_NAME &&
        (next.kind == CODE_TOKEN_END || next.punctuation == '.' ||
         next.punctuation == '[')) {
        char n[256];
        size_t length = first.end - first.start;
        if (length < sizeof n) {
            memcpy(n, text + first.start, length);
            n[length] = 0;
            if (next.kind == CODE_TOKEN_END) {
                if (!strcmp(n, "self")) {
                    char message[256];
                    code_call_spec call = { .name = "self" };
                    node *self = code_call_build(p->asset, s->native, &call, message, sizeof message);
                    if (!self) {
                        code_source_error(p->source, offset, offset + (int)strlen(text), message);
                        goto done;
                    }
                    if (!p->asset->owner_type_name &&
                        expected->type == PIN_HANDLE && expected->type_name) {
                        graph_set_pin_type(
                            s->native,
                            self,
                            true,
                            0,
                            PIN_HANDLE,
                            expected->type_name
                        );
                    }
                    out->kind = CODE_CALL_OUTPUT;
                    out->source = (code_call_output){ self, 0 };
                    valid = true;
                    goto done;
                }
                const behavior_variable *state = behavior_asset_find_variable_by_name(p->asset, n);
                if (state) {
                    out->kind = CODE_CALL_BINDING;
                    out->binding.kind = PIN_BINDING_VARIABLE;
                    out->binding.variable_id = state->id;
                    valid = true;
                    goto done;
                }
                const graph_local *local = source_local(p, s, n);
                if (local) {
                    out->kind = CODE_CALL_BINDING;
                    out->binding.kind = PIN_BINDING_LOCAL;
                    out->binding.local_id = local->id;
                    valid = true;
                    goto done;
                }
                node *entry = graph_node_by_id(s->native, s->native->entry_id);
                int pin = entry ? named_pin(entry, true, n) : -1;
                if (pin >= 0 && entry->output_pins[pin].kind == PIN_DATA) {
                    out->kind = CODE_CALL_OUTPUT;
                    out->source = (code_call_output){ entry, pin };
                    valid = true;
                    goto done;
                }
            }
            if (find(s, n)) {
                valid = projected_value(p, s, text, offset, out);
                goto done;
            }
        }
    }
    if (first.kind == CODE_TOKEN_NAME && expected->reference_kind && whole_atom(text)) {
        code_lexer symbol;
        code_lex_init(&symbol, text);
        code_token token = code_lex_next(&symbol);
        bool qualified = true;
        while (qualified) {
            token = code_lex_next(&symbol);
            if (token.kind == CODE_TOKEN_END) {
                break;
            }
            qualified = token.punctuation == '.' &&
                        code_lex_next(&symbol).kind == CODE_TOKEN_NAME;
        }
        if (qualified) {
            char message[256];
            const char *resolved = code_source_context_reference(
                &p->source->context,
                expected->reference_kind,
                text,
                message,
                sizeof message
            );
            if (!resolved) {
                code_source_error(p->source, offset, offset + (int)strlen(text), message);
                goto done;
            }
            out->kind = CODE_CALL_LITERAL;
            out->literal = (pin_value){
                .type = PIN_NAME,
                .str = graph_own_str(s->native, resolved, strlen(resolved))
            };
            valid = true;
            goto done;
        }
    }
    if (code_statement_math_call(p, text)) {
        valid = value_expression(p, s, text, offset, expected, out);
        goto done;
    }
    if ((first.kind == CODE_TOKEN_NAME || first.kind == CODE_TOKEN_STRING) &&
        (next.punctuation == '(' || next.punctuation == '<' ||
         next.punctuation == '.' || code_token_is(&lexer, first, "engine"))) {
        valid = inline_value(p, s, text, offset, expected, out);
        if (valid && out->kind == CODE_CALL_OUTPUT &&
            !strcmp(out->source.node->desc->name, "Self") && !p->asset->owner_type_name &&
            expected->type == PIN_HANDLE && expected->type_name) {
            graph_set_pin_type(
                s->native,
                out->source.node,
                true,
                out->source.pin,
                PIN_HANDLE,
                expected->type_name
            );
        }
        goto done;
    }
    valid = value_expression(p, s, text, offset, expected, out);
done:
    if (valid && out->kind == CODE_CALL_OUTPUT && expected->type != PIN_FLOW) {
        const pin_spec *output = &out->source.node->output_pins[out->source.pin];
        if (output->type == PIN_FLOW && (output->flags & PIN_FLAG_WILDCARD) &&
            (graph_set_pin_type(
                 s->native,
                 out->source.node,
                 true,
                 out->source.pin,
                 expected->type,
                 expected->type_name
             ) ||
             graph_set_pin_container(
                 s->native,
                 out->source.node,
                 true,
                 out->source.pin,
                 expected->container
             ))) {
            code_source_error(p->source, offset, offset + (int)strlen(text), "Cannot specialize the declared value type");
            valid = false;
        }
    }
    if (valid) {
        code_program_resolved_use(p, s, text, offset, out, CODE_USE_READ);
    }
    if (valid && out->kind == CODE_CALL_BINDING &&
        out->binding.kind == PIN_BINDING_VARIABLE &&
        (p->asset->domain == BEHAVIOR_DOMAIN_LIBRARY || p->program->system)) {
        node *get = behavior_asset_add_get_node_to_graph(
            p->asset,
            s->native,
            out->binding.variable_id
        );
        if (get) {
            out->kind = CODE_CALL_OUTPUT;
            out->source = (code_call_output){ get, 0 };
        } else {
            code_source_error(p->source, offset, offset + (int)strlen(text), "Cannot read the declared library state");
            valid = false;
        }
    }
    p->depth--;
    return valid;
}
static int execution_pin(const node *n, bool output, const char *name)
{
    if (name) {
        return named_pin(n, output, name);
    }
    const pin_spec *ports = output ? n->output_pins : n->input_pins;
    int count = output ? n->output_count : n->input_count, result = -1;
    for (int i = 0; i < count; i++) {
        if (ports[i].kind == PIN_EXEC) {
            if (result >= 0) {
                return -1;
            }
            result = i;
        }
    }
    return result;
}
static void wire_block(parser *p, scope *s, node *from, int output, statement *body)
{
    code_statement_lower_block(p, s, from, output, body);
}
static void lower_scope(parser *p, scope *s)
{
    code_statement_prepare_labels(p, s);
    code_statement_lower_locals(p, s);
    for (int i = 0; i < acount(s->declarations) && !failed(p); i++) {
        build_declaration(p, s, s->declarations[i]);
    }
    for (int i = 0; i < acount(s->returns) && !failed(p); i++) {
        argument *a = &s->returns[i];
        node *ret = graph_node_by_id(s->native, s->native->return_id);
        int pin = ret ? named_pin(ret, false, a->port.name) : -1;
        code_call_argument value = { 0 };
        if (pin < 0 || !source_value(p, s, a->text, a->start, &a->port, &value)) {
            if (!failed(p)) {
                code_source_error(p->source, a->start, a->end, "Unknown function result");
            }
            break;
        }
        int result = 0;
        if (value.has_type && value.type != ret->input_pins[pin].type) {
            char message[192];
            if (!code_call_materialize_cast(p->asset, s->native, &value, message, sizeof message)) {
                code_source_error(p->source, a->start, a->end, message);
                break;
            }
        }
        if (value.kind == CODE_CALL_OUTPUT) {
            pin_spec *output = &value.source.node->output_pins[value.source.pin];
            unsigned flags = output->flags;
            unsigned result_flags = ret->input_pins[pin].flags;
            if (output->type != PIN_FLOW && ret->input_pins[pin].type != PIN_FLOW &&
                pin_compatible(output, &ret->input_pins[pin])) {
                output->flags &= ~PIN_FLAG_WILDCARD;
                ret->input_pins[pin].flags &= ~PIN_FLAG_WILDCARD;
            }
            result = graph_connect_data_conversion(
                s->native,
                value.source.node,
                value.source.pin,
                ret,
                pin,
                value.conversion
            );
            output->flags = flags;
            ret->input_pins[pin].flags = result_flags;
        } else if (value.kind == CODE_CALL_BINDING) {
            if (value.binding.kind == PIN_BINDING_LOCAL && !value.conversion) {
                result = code_call_use_local(s->native, value.binding.local_id, ret, pin);
            } else {
                if (value.binding.kind == PIN_BINDING_LOCAL) {
                    const graph_local
                        *local = graph_local_by_id(s->native, value.binding.local_id);
                    node *source = local ? graph_node_by_id(s->native, local->source_id)
                                         : NULL;
                    int output = 0, source_pin = -1;
                    result = !source ||
                             !graph_pin_by_id(source, local->source_pin_id, &output, &source_pin) ||
                             !output ||
                             graph_connect_data_conversion(
                                 s->native,
                                 source,
                                 source_pin,
                                 ret,
                                 pin,
                                 value.conversion
                             );
                }
                if (!result) {
                    result = graph_set_pin_binding(
                        s->native,
                        ret,
                        false,
                        pin,
                        &value.binding
                    );
                }
            }
        } else {
            result = graph_set_pin_default(s->native, ret, false, pin, &value.literal);
        }
        if (!result) {
            result = code_call_apply_fallback(s->native, ret, pin, &value);
        }
        if (result) {
            code_source_error(p->source, a->start, a->end, "Function result value is incompatible with its declared type");
            break;
        }
    }
    for (int i = 0; i < acount(s->continuations) && !failed(p); i++) {
        continuation *c = &s->continuations[i];
        declaration *d = find(s, c->source);
        node *n = d ? build_declaration(p, s, d) : NULL;
        if (!n) {
            code_source_error(p->source, c->start, c->end, "Unknown event or callable outcome");
            break;
        }
        int output = execution_pin(n, true, c->outcome);
        if (output < 0) {
            code_source_error(p->source, c->start, c->end, "Unknown or ambiguous execution outcome");
            break;
        }
        int previous_count = acount(s->declarations);
        for (int j = 0; j < acount(c->aliases); j++) {
            declaration *alias = c->aliases[j];
            if (!strcmp(alias->name, "event") ||
                (alias->call.name && !strcmp(alias->call.name, "Event"))) {
                alias->native = n;
            }
            apush(s->declarations, alias);
        }
        wire_block(p, s, n, output, c->body);
        asetlen(s->declarations, previous_count);
    }
    if (!failed(p)) {
        code_statement_finish_labels(p, s);
        code_statement_order_locals(s);
    }
}
bool code_program_detect(const char *text)
{
    code_lexer lexer;
    code_lex_init(&lexer, text);
    code_token root = code_lex_next(&lexer);
    return (
        code_token_is(&lexer, root, "behavior") ||
        code_token_is(&lexer, root, "library") || code_token_is(&lexer, root, "system")
    );
}
static behavior_asset *parse_program(code_source *source, bool declarations_only)
{
    code_program *program = xcalloc(1, sizeof *program);
    program->values = av_doc_new();
    source->program = program;
    parser p = { .source = source, .program = program };
    code_lex_init(&p.lexer, source->text);
    advance(&p);
    bool library = take(&p, "library"), system = !library && take(&p, "system");
    program->system = system;
    if (!library && !system) {
        expect(&p, "behavior");
    }
    char *n = name(&p);
    if (!n) {
        code_program_free(program);
        source->program = NULL;
        return NULL;
    }
    p.asset = behavior_asset_new(n);
    uint64_t counter = source->id_floor;
    if (counter > p.asset->graph->id_counter) {
        p.asset->graph->id_counter = counter;
    }
    if (library) {
        p.asset->domain = BEHAVIOR_DOMAIN_LIBRARY;
    } else {
        behavior_asset_set_owner_type(p.asset, "ecs.entity");
    }
    p.scope = new_scope(&p, p.asset->graph);
    if (system) {
        system_header(&p);
    }
    expect(&p, "{");
    scope_parse(&p);
    if (system && !failed(&p)) {
        bool tick = false;
        for (int i = 0; i < acount(p.scope->declarations); i++) {
            const code_call_spec *call = &p.scope->declarations[i]->call;
            tick |= call->name && !strcmp(call->name, "Event") &&
                    call->reference && !strcmp(call->reference, "Tick");
        }
        if (!tick) {
            implicit_tick(&p);
        }
    }
    expect(&p, "}");
    if (p.token.kind != CODE_TOKEN_END && !failed(&p)) {
        error(&p, "Unexpected content after behavior");
    }
    code_program_validate(source);
    for (int i = 0; !declarations_only && i < acount(program->scopes) && !failed(&p);
         i++) {
        lower_scope(&p, program->scopes[i]);
    }
    if (!declarations_only && !failed(&p)) {
        code_reactive_finish(&p);
    }
    for (int i = 0; i < acount(program->scopes); i++) {
        graph *g = program->scopes[i]->native;
        for (int j = 0; j < acount(g->nodes); j++) {
            g->nodes[j]->placed = false;
        }
    }
    if (!declarations_only) {
        if (failed(&p)) {
            code_program_partial_intelligence(&p);
        }
        code_program_intelligence(&p);
    }
    if (failed(&p)) {
        code_program_free(program);
        source->program = NULL;
        behavior_asset_free(p.asset);
        return NULL;
    }
    source->document = av_doc_new();
    av_value *root = behavior_asset_to_value(source->document, p.asset);
    if (program->query) {
        av_obj_set(
            source->document,
            root,
            "query",
            av_clone(source->document, program->query)
        );
    }
    if (system) {
        av_obj_set(source->document, root, "kind", av_str(source->document, "system"));
    }
    av_doc_set_root(source->document, root);
    return p.asset;
}
behavior_asset *code_program_parse(code_source *source)
{
    return parse_program(source, false);
}
behavior_asset *code_program_parse_declarations(code_source *source)
{
    return parse_program(source, true);
}
void code_program_free(code_program *program)
{
    if (!program) {
        return;
    }
    for (int i = 0; i < acount(program->scopes); i++) {
        scope *s = program->scopes[i];
        for (int j = 0; j < acount(s->declarations); j++) {
            declaration *d = s->declarations[j];
            afree(d->arguments);
            afree(d->results);
            free(d);
        }
        for (int j = 0; j < acount(s->continuations); j++) {
            continuation *c = &s->continuations[j];
            code_statement_free(c->body);
            for (int k = 0; k < acount(c->aliases); k++) {
                declaration *d = c->aliases[k];
                afree(d->arguments);
                afree(d->results);
                free(d);
            }
            afree(c->aliases);
        }
        afree(s->declarations);
        afree(s->continuations);
        afree(s->returns);
        afree(s->locals);
        afree(s->components);
        afree(s->labels);
        afree(s->inputs);
        afree(s->outputs);
        afree(s->type_parameters);
        free(s);
    }
    for (int i = 0; i < acount(program->strings); i++) {
        free(program->strings[i]);
    }
    afree(program->strings);
    afree(program->scopes);
    for (int i = 0; i < acount(program->expressions); i++) {
        afree(program->expressions[i].reads);
    }
    afree(program->expressions);
    for (int i = 0; i < acount(program->call_inputs); i++) {
        afree(program->call_inputs[i].explicit_inputs);
    }
    afree(program->call_inputs);
    afree(program->states);
    av_doc_free(program->values);
    free(program);
}
static bool argument_location(
    argument *arguments,
    const node *native,
    uint64_t pin,
    uint64_t node_id,
    int *start,
    int *end
)
{
    for (int i = 0; i < acount(arguments); i++) {
        argument *argument = &arguments[i];
        if (argument->start >= argument->end) {
            continue;
        }
        bool matched = argument->has_resolved &&
                       argument->resolved.kind == CODE_CALL_OUTPUT &&
                       argument->resolved.source.node &&
                       argument->resolved.source.node->id == node_id;
        if (!matched && native && native->id == node_id && pin) {
            for (int j = 0; j < native->input_count; j++) {
                matched |= native->input_pins[j].id == pin &&
                           !strcmp(native->input_pins[j].name, argument->port.name);
            }
        }
        if (!matched) {
            continue;
        }
        *start = argument->start;
        *end = argument->end;
        return true;
    }
    return false;
}
static bool statement_location(statement *body, uint64_t node_id, uint64_t pin, int *start, int *end)
{
    for (int i = 0; i < acount(body); i++) {
        statement *statement = &body[i];
        if (argument_location(statement->arguments, statement->native, pin, node_id, start, end)) {
            return true;
        }
        if (statement->kind != CODE_STMT_GOTO && !statement->label_alias &&
            ((statement->native && statement->native->id == node_id) ||
             (statement->sequence && statement->sequence->id == node_id))) {
            *start = statement->start;
            *end = statement->end;
            return true;
        }
        if (statement_location(statement->body, node_id, pin, start, end) ||
            statement_location(statement->alternate, node_id, pin, start, end)) {
            return true;
        }
    }
    return false;
}
static bool program_location(
    const code_program *program,
    uint64_t graph_id,
    uint64_t node_id,
    uint64_t pin_id,
    int *start,
    int *end,
    int depth
)
{
    if (depth > 128) {
        return false;
    }
    for (int i = 0; i < acount(program->scopes); i++) {
        scope *scope = program->scopes[i];
        if (scope->native->graph_id != graph_id) {
            continue;
        }
        for (int j = 0; j < acount(scope->declarations); j++) {
            declaration *declaration = scope->declarations[j];
            if (argument_location(declaration->arguments, declaration->native, pin_id, node_id, start, end)) {
                return true;
            }
            if (declaration->native && declaration->native->id == node_id &&
                declaration->start < declaration->end) {
                *start = declaration->start;
                *end = declaration->end;
                return true;
            }
        }
        for (int j = 0; j < acount(scope->continuations); j++) {
            if (statement_location(scope->continuations[j].body, node_id, pin_id, start, end)) {
                return true;
            }
        }
        if (argument_location(scope->returns, NULL, pin_id, node_id, start, end)) {
            return true;
        }
        if (node_id == scope->native->entry_id || node_id == scope->native->return_id) {
            argument *arguments = node_id == scope->native->entry_id
                                    ? scope->inputs
                                    : scope->outputs;
            const node *native = graph_node_by_id(scope->native, node_id);
            const pin_spec *ports = native && node_id == scope->native->entry_id
                                      ? native->output_pins
                                  : native ? native->input_pins
                                           : NULL;
            int count = native && node_id == scope->native->entry_id ? native->output_count
                      : native ? native->input_count
                               : 0;
            for (int j = 0; pin_id && j < count; j++) {
                if (ports[j].id != pin_id) {
                    continue;
                }
                for (int k = 0; k < acount(arguments); k++) {
                    if (!strcmp(ports[j].name, arguments[k].port.name) &&
                        arguments[k].name_start < arguments[k].name_end) {
                        *start = arguments[k].name_start;
                        *end = arguments[k].name_end;
                        return true;
                    }
                }
            }
            *start = scope->name_start;
            *end = scope->name_end;
            if (*start < *end) {
                return true;
            }
        }
        for (int j = 0; j < acount(scope->native->edges); j++) {
            const edge *edge = &scope->native->edges[j];
            if (edge->kind == GRAPH_EDGE_DATA && edge->from_id == node_id &&
                edge->to_id != node_id &&
                program_location(program, graph_id, edge->to_id, edge->to_pin_id, start, end, depth + 1)) {
                return true;
            }
        }
        if (!node_id && scope->name_start < scope->name_end) {
            *start = scope->name_start;
            *end = scope->name_end;
            return true;
        }
    }
    /* Expression bodies are compiler-generated graphs. Their executable source
     * is the authored caller, not a fabricated declaration in generated code. */
    for (int i = 0; i < acount(program->scopes); i++) {
        graph *graph = program->scopes[i]->native;
        for (int j = 0; j < acount(graph->nodes); j++) {
            const node *node = graph->nodes[j];
            if (node->desc == &NODE_BehaviorExpression && node->reference_id == graph_id &&
                program_location(program, graph->graph_id, node->id, 0, start, end, depth + 1)) {
                return true;
            }
        }
    }
    return false;
}
bool code_program_location(
    const code_program *program,
    uint64_t graph_id,
    uint64_t node_id,
    uint64_t pin_id,
    int *start,
    int *end
)
{
    return program_location(program, graph_id, node_id, pin_id, start, end, 0);
}
static uint64_t remapped(uint64_t id, const uint64_t *from, const uint64_t *to, int count)
{
    if (!id) {
        return 0;
    }
    for (int i = 0; i < count; i++) {
        if (from[i] == id) {
            return to[i];
        }
    }
    return id;
}
static void remap_value(code_call_argument *value, const uint64_t *from, const uint64_t *to, int count)
{
    if (value->has_fallback_binding) {
        code_call_argument fallback = { .kind = CODE_CALL_BINDING,
                                        .binding = value->fallback_binding };
        remap_value(&fallback, from, to, count);
        value->fallback_binding = fallback.binding;
    }
    if (value->kind != CODE_CALL_BINDING || value->binding.source_asset) {
        return;
    }
    value->binding.variable_id = remapped(value->binding.variable_id, from, to, count);
    value->binding.local_id = remapped(value->binding.local_id, from, to, count);
    value->binding.event_node_id = remapped(value->binding.event_node_id, from, to, count);
    value->binding.event_pin_id = remapped(value->binding.event_pin_id, from, to, count);
}
static void remap_arguments(argument *arguments, const uint64_t *from, const uint64_t *to, int count)
{
    for (int i = 0; i < acount(arguments); i++) {
        if (arguments[i].has_resolved) {
            remap_value(&arguments[i].resolved, from, to, count);
        }
    }
}
static void remap_statements(statement *body, const uint64_t *from, const uint64_t *to, int count)
{
    for (int i = 0; i < acount(body); i++) {
        remap_arguments(body[i].arguments, from, to, count);
        remap_statements(body[i].body, from, to, count);
        remap_statements(body[i].alternate, from, to, count);
    }
}
static void reorder_output(
    code_call_output *value,
    node *n,
    const int *indices,
    int count,
    code_call_output ***seen
)
{
    if (value->node != n) {
        return;
    }
    for (int i = 0; i < acount(*seen); i++) {
        if ((*seen)[i] == value) {
            return;
        }
    }
    assert(value->pin >= 0 && value->pin < count);
    apush(*seen, value);
    value->pin = indices[value->pin];
}

static void reorder_arguments(argument *args, node *n, const int *indices, int count, code_call_output ***seen)
{
    for (int i = 0; i < acount(args); i++) {
        if (args[i].has_resolved && args[i].resolved.kind == CODE_CALL_OUTPUT) {
            reorder_output(&args[i].resolved.source, n, indices, count, seen);
        }
    }
}

static void reorder_statements(statement *body, node *n, const int *indices, int count, code_call_output ***seen)
{
    for (int i = 0; i < acount(body); i++) {
        reorder_arguments(body[i].arguments, n, indices, count, seen);
        reorder_statements(body[i].body, n, indices, count, seen);
        reorder_statements(body[i].alternate, n, indices, count, seen);
    }
}

void code_program_reorder_outputs(code_program *program, node *n, const int *indices, int count)
{
    code_call_output **seen = NULL;
    for (int i = 0; i < acount(program->scopes); i++) {
        scope *s = program->scopes[i];
        for (int j = 0; j < acount(s->locals); j++) {
            if (s->locals[j].has_resolved &&
                s->locals[j].resolved.kind == CODE_CALL_OUTPUT) {
                reorder_output(&s->locals[j].resolved.source, n, indices, count, &seen);
            }
        }
        for (int j = 0; j < acount(s->declarations); j++) {
            declaration *d = s->declarations[j];
            reorder_output(&d->expression_output, n, indices, count, &seen);
            reorder_arguments(d->arguments, n, indices, count, &seen);
        }
        reorder_arguments(s->returns, n, indices, count, &seen);
        for (int j = 0; j < acount(s->continuations); j++) {
            continuation *c = &s->continuations[j];
            reorder_statements(c->body, n, indices, count, &seen);
            for (int k = 0; k < acount(c->aliases); k++) {
                declaration *d = c->aliases[k];
                reorder_output(&d->expression_output, n, indices, count, &seen);
                reorder_arguments(d->arguments, n, indices, count, &seen);
            }
        }
    }
    for (int i = 0; i < acount(program->expressions); i++) {
        code_expression_read *reads = program->expressions[i].reads;
        for (int j = 0; j < acount(reads); j++) {
            if (reads[j].resolved.kind == CODE_CALL_OUTPUT) {
                reorder_output(&reads[j].resolved.source, n, indices, count, &seen);
            }
        }
    }
    afree(seen);
}

void code_program_remap(code_program *program, const uint64_t *from, const uint64_t *to, int count)
{
    for (int i = 0; i < acount(program->states); i++) {
        program->states[i].native_id = remapped(program->states[i].native_id, from, to, count);
    }
    for (int i = 0; i < acount(program->scopes); i++) {
        scope *scope = program->scopes[i];
        for (int j = 0; j < acount(scope->locals); j++) {
            scope->locals[j].id = remapped(scope->locals[j].id, from, to, count);
            if (scope->locals[j].has_resolved) {
                remap_value(&scope->locals[j].resolved, from, to, count);
            }
        }
        for (int j = 0; j < acount(scope->declarations); j++) {
            remap_arguments(scope->declarations[j]->arguments, from, to, count);
        }
        remap_arguments(scope->returns, from, to, count);
        for (int j = 0; j < acount(scope->continuations); j++) {
            remap_statements(scope->continuations[j].body, from, to, count);
            for (int k = 0; k < acount(scope->continuations[j].aliases); k++) {
                remap_arguments(scope->continuations[j].aliases[k]->arguments, from, to, count);
            }
        }
    }
}
void code_program_refresh_locations(code_source *source)
{
    if (!source->asset || !source->program) {
        return;
    }
    code_program *program = source->program;
    for (int i = 0; i < acount(program->states); i++) {
        code_intelligence_location(
            source,
            program->states[i].symbol,
            source->asset->graph->graph_id,
            program->states[i].native_id,
            0
        );
    }
    for (int i = 0; i < acount(program->scopes); i++) {
        scope *scope = program->scopes[i];
        if (scope->symbol) {
            code_intelligence_location(source, scope->symbol, scope->native->graph_id, 0, 0);
        }
        for (int j = 0; j < acount(scope->declarations); j++) {
            declaration *declaration = scope->declarations[j];
            if (declaration->symbol && declaration->native) {
                code_intelligence_location(
                    source,
                    declaration->symbol,
                    scope->native->graph_id,
                    declaration->native->id,
                    0
                );
            }
        }
    }
}

void code_program_next(parser *p)
{
    advance(p);
}
void code_program_record_inputs(
    code_program *program,
    node *native,
    const code_call_argument *arguments,
    int count
)
{
    code_program_call_inputs *record = NULL;
    for (int i = 0; i < acount(program->call_inputs); i++) {
        if (program->call_inputs[i].native == native) {
            record = &program->call_inputs[i];
            break;
        }
    }
    if (!record) {
        apush(program->call_inputs, ((code_program_call_inputs){ .native = native }));
        record = &program->call_inputs[acount(program->call_inputs) - 1];
    }
    for (int i = 0; i < count; i++) {
        const code_call_argument *argument = &arguments[i];
        int index = -1;
        if (argument->positional) {
            int ordinal = 0;
            for (int j = 0; j < native->input_count; j++) {
                if (native->input_pins[j].kind == PIN_DATA &&
                    ordinal++ == argument->ordinal) {
                    index = j;
                    break;
                }
            }
        } else {
            char error[192];
            index = code_call_port_index(native, false, argument->name, error, sizeof error);
        }
        if (index < 0) {
            /* A configured aggregate can replace its named root. Without its
             * exact resolved parameter, retain conservative authored ownership. */
            for (int j = 0; j < native->input_count; j++) {
                if (native->input_pins[j].kind == PIN_DATA) {
                    apush(record->explicit_inputs, j);
                }
            }
            return;
        }
        bool exists = false;
        for (int j = 0; j < acount(record->explicit_inputs); j++) {
            exists |= record->explicit_inputs[j] == index;
        }
        if (!exists) {
            apush(record->explicit_inputs, index);
        }
    }
}
bool code_program_take(parser *p, const char *word)
{
    return take(p, word);
}
bool code_program_expect(parser *p, const char *word)
{
    return expect(p, word);
}
char *code_program_name(parser *p)
{
    return name(p);
}
char *code_program_reference_name(parser *p)
{
    return reference_name(p);
}
char *code_program_capture(parser *p, int *start, int *end)
{
    return expression_text(p, start, end);
}
bool code_program_type_token(parser *p)
{
    return type_token(p);
}
code_call_port code_program_type(parser *p)
{
    return type(p);
}
argument *code_program_arguments(parser *p, bool results)
{
    return arguments(p, results);
}
node *code_program_call(parser *p, scope *s, code_call_spec call, argument *arguments, int start, int end)
{
    return build_call(p, s, call, arguments, start, end);
}
bool code_program_value(
    parser *p,
    scope *s,
    const char *text,
    int offset,
    code_call_port *expected,
    code_call_argument *out
)
{
    return source_value(p, s, text, offset, expected, out);
}
declaration *code_program_declaration(scope *s, const char *name)
{
    return find(s, name);
}
node *code_program_build(parser *p, scope *s, declaration *d)
{
    return build_declaration(p, s, d);
}

bool code_program_atomic_value(const char *text)
{
    return whole_atom(text);
}
