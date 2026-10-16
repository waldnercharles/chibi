#include "project_migrate26.h"

#include "language/syntax.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum
{
    REF_NONE,
    REF_PROTOTYPE,
    REF_COMPONENT,
    REF_RELATIONSHIP
} reference_kind;
typedef struct
{
    int document, callable;
    TSNode node, name, scope;
    reference_kind reference;
    bool pending;
} reference_binding;
typedef struct
{
    int document;
    TSNode node, name, library, scope;
    int *parameters;
} reference_callable;
typedef struct
{
    const char *const *sources;
    script_syntax **syntax;
    text_edit **edits;
    reference_binding *bindings;
    reference_callable *callables;
    char *error;
    size_t size;
    bool valid;
} reference_flow;

static bool is(TSNode node, const char *kind)
{
    return !ts_node_is_null(node) && !strcmp(ts_node_type(node), kind);
}
static TSNode field(TSNode node, const char *name)
{
    return ts_node_child_by_field_name(node, name, (uint32_t)strlen(name));
}
static bool contains(TSNode scope, TSNode node)
{
    return ts_node_start_byte(scope) <= ts_node_start_byte(node) &&
           ts_node_end_byte(scope) >= ts_node_end_byte(node);
}
static char *text(reference_flow *f, int document, TSNode node)
{
    int start = (int)ts_node_start_byte(node), end = (int)ts_node_end_byte(node);
    if (is(node, "string")) {
        start++;
        end--;
    }
    char *out = xmalloc((size_t)(end - start) + 1);
    memcpy(out, f->sources[document] + start, (size_t)(end - start));
    out[end - start] = 0;
    return out;
}
static bool spells(reference_flow *f, int document, TSNode node, const char *name)
{
    if (ts_node_is_null(node)) {
        return false;
    }
    char *value = text(f, document, node);
    bool equal = !strcmp(value, name);
    free(value);
    return equal;
}
static void fail(reference_flow *f, const char *message)
{
    if (f->valid) {
        snprintf(f->error, f->size, "%s", message);
    }
    f->valid = false;
}
static void edit(reference_flow *f, int document, TSNode node, const char *replacement)
{
    int start = (int)ts_node_start_byte(node), end = (int)ts_node_end_byte(node);
    for (int i = 0; i < acount(f->edits[document]); i++) {
        const text_edit *previous = &f->edits[document][i];
        if (previous->start == start && previous->end == end) {
            if (strcmp(previous->text, replacement)) {
                fail(f, "Reference migration found contradictory symbolic uses");
            }
            return;
        }
    }
    apush(f->edits[document], ((text_edit){ start, end, xstrdup(replacement) }));
}
static char *identifier(reference_flow *f, int document, TSNode node)
{
    char *before = text(f, document, node), *out = xmalloc(strlen(before) + 3),
         *at = out;
    bool separator = false, letter = false;
    if (isdigit((unsigned char)*before)) {
        *at++ = '_';
    }
    for (const unsigned char *p = (const unsigned char *)before; *p; p++) {
        bool ascii = (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
                     (*p >= '0' && *p <= '9');
        if (ascii || *p == '_') {
            if (separator && at > out && at[-1] != '_') {
                *at++ = '_';
            }
            *at++ = (char)*p;
            separator = false;
            letter |= ascii;
        } else if (*p == '.') {
            *at++ = '.';
            separator = false;
        } else {
            separator = true;
        }
    }
    *at = 0;
    free(before);
    if (!letter) {
        fail(f, "Reference migration requires a named project symbol; replace the empty or computed name before migration");
    }
    return out;
}
static TSNode scope(TSNode node)
{
    for (TSNode parent = ts_node_parent(node); !ts_node_is_null(parent);
         parent = ts_node_parent(parent)) {
        if (is(parent, "declaration_body") || is(parent, "system_body") ||
            is(parent, "callable_body") || is(parent, "block")) {
            return parent;
        }
    }
    return node;
}
static int binding(reference_flow *f, int document, TSNode use, TSNode name, bool state)
{
    int found = -1;
    for (int i = 0; i < acount(f->bindings); i++) {
        reference_binding *b = &f->bindings[i];
        if (b->document != document || (state && !is(b->node, "state_declaration")) ||
            !contains(b->scope, use)) {
            continue;
        }
        char *wanted = text(f, document, name);
        bool equal = spells(f, document, b->name, wanted);
        free(wanted);
        if (equal && (found < 0 ||
                      ts_node_end_byte(b->scope) - ts_node_start_byte(b->scope) <
                          ts_node_end_byte(f->bindings[found].scope) -
                              ts_node_start_byte(f->bindings[found].scope))) {
            found = i;
        }
    }
    return found;
}
static void collect(reference_flow *f, int document, TSNode node, TSNode library, int callable)
{
    if (is(node, "library")) {
        library = field(node, "name");
    }
    if (is(node, "function_declaration") || is(node, "action_declaration")) {
        callable = acount(f->callables);
        apush(
            f->callables,
            ((
                reference_callable
            ){ document, node, field(node, "name"), library, scope(node), NULL })
        );
        TSNode parameters = field(node, "parameters");
        for (uint32_t i = 0; !ts_node_is_null(parameters) &&
                             i < ts_node_named_child_count(parameters);
             i++) {
            TSNode parameter = ts_node_named_child(parameters, i);
            if (!is(parameter, "parameter")) {
                continue;
            }
            apush(f->callables[callable].parameters, acount(f->bindings));
            apush(
                f->bindings,
                ((reference_binding){ document,
                                      callable,
                                      parameter,
                                      field(parameter, "name"),
                                      field(node, "body"),
                                      REF_NONE,
                                      false })
            );
        }
    }
    if (is(node, "state_declaration") || is(node, "local_declaration") ||
        is(node, "constant_declaration")) {
        apush(
            f->bindings,
            ((
                reference_binding
            ){ document, -1, node, field(node, "name"), scope(node), REF_NONE, false })
        );
    }
    for (uint32_t i = 0; i < ts_node_named_child_count(node); i++) {
        collect(f, document, ts_node_named_child(node, i), library, callable);
    }
}
static int callee(reference_flow *f, int document, TSNode target)
{
    TSNode name = target, library = { 0 };
    if (is(target, "member_expression")) {
        name = field(target, "member");
        library = field(target, "object");
    }
    if (!is(name, "identifier") ||
        (!ts_node_is_null(library) && !is(library, "identifier"))) {
        return -1;
    }
    int found = -1;
    char *wanted = text(f, document, name),
         *qualifier = ts_node_is_null(library) ? NULL : text(f, document, library);
    for (int i = 0; i < acount(f->callables); i++) {
        reference_callable *c = &f->callables[i];
        if (!spells(f, c->document, c->name, wanted)) {
            continue;
        }
        if (qualifier) {
            if (!ts_node_is_null(c->library) &&
                spells(f, c->document, c->library, qualifier)) {
                found = i;
                break;
            }
        } else if (c->document == document && contains(c->scope, target)) {
            if (found < 0 ||
                ts_node_end_byte(c->scope) - ts_node_start_byte(c->scope) <
                    ts_node_end_byte(f->callables[found].scope) -
                        ts_node_start_byte(f->callables[found].scope)) {
                found = i;
            }
        }
    }
    if (found < 0 && !qualifier) {
        for (int i = 0; i < acount(f->callables); i++) {
            reference_callable *c = &f->callables[i];
            if (!ts_node_is_null(c->library) &&
                spells(f, c->document, c->name, wanted)) {
                if (found >= 0) {
                    found = -1;
                    break;
                }
                found = i;
            }
        }
    }
    free(wanted);
    free(qualifier);
    return found;
}
static void mark_value(reference_flow *, int, TSNode, reference_kind);
static void mark_binding(reference_flow *f, int index, reference_kind kind)
{
    if (index < 0) {
        return; /* A bare, unbound identifier already names a project symbol. */
    }
    reference_binding *b = &f->bindings[index];
    if (b->reference && b->reference != kind) {
        fail(f, "A value reaches incompatible Prototype, Component or Relationship parameters; split the value before migration");
        return;
    }
    if (!b->reference) {
        b->reference = kind;
        b->pending = true;
    }
}
static void mark_value(reference_flow *f, int document, TSNode value, reference_kind kind)
{
    if (ts_node_is_null(value)) {
        return;
    }
    if (is(value, "typed_argument")) {
        static const char *const names[] = { "", "Prototype", "Component", "Relationship" };
        edit(f, document, field(value, "type"), names[kind]);
        TSNode source = field(value, "source");
        mark_value(f, document, ts_node_is_null(source) ? field(value, "value") : source, kind);
        return;
    }
    if (is(value, "string")) {
        char *name = identifier(f, document, value);
        for (int i = 0; i < acount(f->bindings); i++) {
            reference_binding *b = &f->bindings[i];
            bool visible = contains(b->scope, value) ||
                           (b->callable >= 0 &&
                            contains(f->callables[b->callable].node, value));
            if (b->document == document && visible &&
                spells(f, document, b->name, name)) {
                fail(f, "A reference literal would be captured by a local binding after migration; rename that binding before migration");
                break;
            }
        }
        if (f->valid) {
            edit(f, document, value, name);
        }
        free(name);
        return;
    }
    if (is(value, "identifier")) {
        mark_binding(f, binding(f, document, value, value, false), kind);
        return;
    }
    if (is(value, "member_expression")) {
        TSNode object = field(value, "object"), member = field(value, "member");
        if (spells(f, document, object, "entry")) {
            char *name = text(f, document, member);
            int found = -1;
            for (int i = 0; i < acount(f->bindings); i++) {
                reference_binding *b = &f->bindings[i];
                if (b->document == document && b->callable >= 0 &&
                    contains(b->scope, value) && spells(f, document, b->name, name)) {
                    found = i;
                }
            }
            free(name);
            mark_binding(f, found, kind);
            return;
        }
        if (spells(f, document, object, "state")) {
            mark_binding(f, binding(f, document, value, member, spells(f, document, object, "state")), kind);
            return;
        }
    }
    if (is(value, "parenthesized_expression")) {
        for (uint32_t i = 0; i < ts_node_named_child_count(value); i++) {
            TSNode child = ts_node_named_child(value, i);
            if (!ts_node_is_extra(child)) {
                mark_value(f, document, child, kind);
                return;
            }
        }
    }
    fail(f, "A symbolic reference is computed from text or another unsupported value; replace it with an explicit project symbol before migration");
}
static TSNode argument(reference_flow *f, int document, TSNode call, int ordinal, const char *name)
{
    TSNode arguments = field(call, "arguments");
    int position = 0;
    for (uint32_t i = 0; i < ts_node_named_child_count(arguments); i++) {
        TSNode actual = ts_node_named_child(arguments, i);
        if (ts_node_is_extra(actual)) {
            continue;
        }
        TSNode label = field(actual, "name"), value = field(actual, "value");
        int parameter = position++;
        if (!ts_node_is_null(label)) {
            if (spells(f, document, label, name)) {
                return is(actual, "typed_argument") ? actual : value;
            }
        } else if (parameter == ordinal) {
            return actual;
        }
    }
    return (TSNode){ 0 };
}
static void seed(reference_flow *f, int document, TSNode node)
{
    if (is(node, "call_expression")) {
        TSNode target = field(node, "function");
        if (is(target, "identifier") && callee(f, document, target) < 0 &&
            binding(f, document, target, target, false) < 0) {
            char *name = text(f, document, target);
            if (!strcmp(name, "component")) {
                TSNode arguments = field(node, "arguments");
                TSNode values[2] = { 0 };
                int count = 0;
                for (uint32_t i = 0; i < ts_node_named_child_count(arguments); i++) {
                    TSNode value = ts_node_named_child(arguments, i);
                    if (ts_node_is_extra(value)) {
                        continue;
                    }
                    if (count >= 2 ||
                        (!is(value, "string") && !is(value, "identifier"))) {
                        fail(f, "Legacy component() migration requires a static component and optional field");
                        break;
                    }
                    values[count++] = value;
                }
                if (f->valid && count) {
                    char *component = identifier(f, document, values[0]);
                    char *member = count == 2 ? identifier(f, document, values[1])
                                              : xstrdup("");
                    size_t length = strlen(component) + strlen(member) + 8;
                    char *path = xmalloc(length);
                    snprintf(path, length, "self.%s%s%s", component, *member ? "." : "", member);
                    edit(f, document, node, path);
                    free(component);
                    free(member);
                    free(path);
                } else if (f->valid) {
                    fail(f, "Legacy component() migration requires a component name");
                }
                free(name);
                return;
            }
            if (!strcmp(name, "spawn") || !strcmp(name, "spawn_prototype")) {
                if (!strcmp(name, "spawn")) {
                    edit(f, document, target, "spawn_prototype");
                }
                mark_value(f, document, argument(f, document, node, 0, "Prototype"), REF_PROTOTYPE);
            }
            static const char *const components[] = {
                "get_component",
                "set_component",
                "has_component",
                "add_component",
                "remove_component",
                "find_entities_with_component",
                "has_entities_with_component",
                "make_entity_condition"
            };
            for (size_t i = 0; i < sizeof components / sizeof *components; i++) {
                if (!strcmp(name, components[i])) {
                    mark_value(f, document, argument(f, document, node, 0, "Component"), REF_COMPONENT);
                }
            }
            if (!strcmp(name, "add_pair") || !strcmp(name, "remove_pair") ||
                !strcmp(name, "has_pair")) {
                mark_value(f, document, argument(f, document, node, 1, "Relationship"), REF_RELATIONSHIP);
            }
            free(name);
        }
    }
    for (uint32_t i = 0; i < ts_node_named_child_count(node); i++) {
        seed(f, document, ts_node_named_child(node, i));
    }
}
static void follow_writes(reference_flow *f, int document, TSNode node, int index)
{
    reference_binding *b = &f->bindings[index];
    if (is(node, "assignment")) {
        TSNode target = field(node, "target"), name = target;
        bool state = false;
        if (is(target, "member_expression")) {
            TSNode object = field(target, "object");
            state = spells(f, document, object, "state");
            if (!state && !spells(f, document, object, "entry")) {
                return;
            }
            name = field(target, "member");
        }
        if (binding(f, document, target, name, state) == index) {
            if (!spells(f, document, field(node, "operator"), "=")) {
                fail(f, "A symbolic reference has compound text writes; replace them with project symbols before migration");
            } else {
                mark_value(f, document, field(node, "value"), b->reference);
            }
        }
    }
    for (uint32_t i = 0; i < ts_node_named_child_count(node); i++) {
        follow_writes(f, document, ts_node_named_child(node, i), index);
    }
}
static void follow_callers(reference_flow *f, int document, TSNode node, int callable, int ordinal, reference_kind kind)
{
    if (is(node, "call_expression") &&
        callee(f, document, field(node, "function")) == callable) {
        reference_callable *c = &f->callables[callable];
        reference_binding *parameter = &f->bindings[c->parameters[ordinal]];
        char *name = text(f, c->document, parameter->name);
        mark_value(f, document, argument(f, document, node, ordinal, name), kind);
        free(name);
    }
    for (uint32_t i = 0; i < ts_node_named_child_count(node); i++) {
        follow_callers(f, document, ts_node_named_child(node, i), callable, ordinal, kind);
    }
}
static reference_kind referenced_value(reference_flow *f, int document, TSNode value, int depth)
{
    if (ts_node_is_null(value) || depth >= 64) {
        return REF_NONE;
    }
    if (is(value, "typed_argument")) {
        TSNode source = field(value, "source");
        return referenced_value(
            f,
            document,
            ts_node_is_null(source) ? field(value, "value") : source,
            depth + 1
        );
    }
    if (is(value, "parenthesized_expression")) {
        return referenced_value(f, document, field(value, "expression"), depth + 1);
    }
    int index = -1;
    if (is(value, "identifier")) {
        index = binding(f, document, value, value, false);
    } else if (is(value, "member_expression")) {
        TSNode object = field(value, "object");
        bool state = spells(f, document, object, "state");
        if (state || spells(f, document, object, "entry")) {
            index = binding(f, document, value, field(value, "member"), state);
        }
    }
    if (index < 0) {
        return REF_NONE;
    }
    reference_binding *b = &f->bindings[index];
    return b->reference
             ? b->reference
             : referenced_value(f, document, field(b->node, "value"), depth + 1);
}
static void text_use(reference_flow *f, int document, TSNode value)
{
    if (referenced_value(f, document, value, 0)) {
        fail(f, "A value is used both as a symbolic reference and ordinary text; split the text and reference bindings before migration");
    }
}
static void check_text_uses(reference_flow *f, int document, TSNode node)
{
    if (!f->valid) {
        return;
    }
    if (is(node, "call_expression")) {
        TSNode target = field(node, "function");
        int callable = callee(f, document, target);
        if (callable >= 0) {
            reference_callable *c = &f->callables[callable];
            for (int i = 0; i < acount(c->parameters); i++) {
                reference_binding *parameter = &f->bindings[c->parameters[i]];
                TSNode type = field(parameter->node, "type");
                if (parameter->reference ||
                    (!spells(f, c->document, type, "name") &&
                     !spells(f, c->document, type, "string"))) {
                    continue;
                }
                char *name = text(f, c->document, parameter->name);
                text_use(f, document, argument(f, document, node, i, name));
                free(name);
            }
        } else if (is(target, "identifier") && binding(f, document, target, target, false) < 0) {
            static const struct
            {
                const char *call, *parameter;
                int ordinal;
            } sinks[] = {
                { "print_string", "InString", 0 },
                { "format_text", "Format", 0 },
                { "text_equal", "A", 0 },
                { "text_equal", "B", 1 },
                { "set_ui_text", "Text", 2 },
                { "set_ui_text", "Element", 1 },
                { "set_ui_class", "Class", 2 },
                { "set_ui_class", "Element", 1 },
                { "get_global_variable", "Name", 0 },
                { "set_global_variable", "Name", 0 },
            };
            for (size_t i = 0; i < sizeof sinks / sizeof *sinks; i++) {
                if (spells(f, document, target, sinks[i].call)) {
                    text_use(
                        f,
                        document,
                        argument(f, document, node, sinks[i].ordinal, sinks[i].parameter)
                    );
                }
            }
        }
    }
    for (uint32_t i = 0; i < ts_node_named_child_count(node) && f->valid; i++) {
        check_text_uses(f, document, ts_node_named_child(node, i));
    }
}

static void annotate(reference_flow *f, reference_binding *b)
{
    if (is(b->node, "constant_declaration")) {
        return; /* Its symbolic initializer supplies the nominal type. */
    }
    static const char *const names[] = { "", "Prototype", "Component", "Relationship" };
    TSNode type = field(b->node, "type");
    const char *name = names[b->reference];
    if (!ts_node_is_null(type)) {
        edit(f, b->document, type, name);
        return;
    }
    TSNode value = field(b->node, "value");
    bool inferred = !ts_node_is_null(value) && !is(b->node, "parameter");
    if (inferred) {
        for (uint32_t i = 0; i < ts_node_child_count(b->node); i++) {
            TSNode child = ts_node_child(b->node, i);
            if (is(child, ":=")) {
                char annotation[48];
                snprintf(annotation, sizeof annotation, ": %s =", name);
                edit(f, b->document, child, annotation);
                return;
            }
        }
    }
    char annotation[48];
    snprintf(annotation, sizeof annotation, "%s%s ", is(b->node, "parameter") ? ": " : "", name);
    TSNode at = b->name;
    int offset = (int)ts_node_end_byte(at);
    if (!is(b->node, "parameter")) {
        TSNode storage = field(b->node, "storage");
        offset = ts_node_is_null(storage) ? offset : (int)ts_node_end_byte(storage);
        TSNode owner = field(b->node, "owner");
        if (!ts_node_is_null(owner)) {
            for (uint32_t i = 0; i < ts_node_child_count(b->node); i++) {
                TSNode child = ts_node_child(b->node, i);
                if (is(child, ")")) {
                    offset = (int)ts_node_end_byte(child);
                }
            }
        }
        snprintf(annotation, sizeof annotation, " %s", name);
    }
    apush(f->edits[b->document], ((text_edit){ offset, offset, xstrdup(annotation) }));
}

bool chibi_migrate26_references(
    const char *const *sources,
    int count,
    text_edit ***edits,
    char *error,
    size_t size
)
{
    reference_flow f = { .sources = sources,
                         .syntax = xcalloc((size_t)count + 1, sizeof *f.syntax),
                         .edits = xcalloc((size_t)count + 1, sizeof *f.edits),
                         .error = error,
                         .size = size,
                         .valid = true };
    for (int i = 0; i < count && f.valid; i++) {
        if (!sources[i]) {
            continue;
        }
        f.syntax[i] = script_parse(sources[i]);
        if (f.syntax[i]->diagnostic_count) {
            fail(&f, "Cannot migrate references in invalid current source");
            break;
        }
        collect(&f, i, ts_tree_root_node(f.syntax[i]->tree), (TSNode){ 0 }, -1);
    }
    for (int i = 0; i < count && f.valid; i++) {
        if (f.syntax[i]) {
            seed(&f, i, ts_tree_root_node(f.syntax[i]->tree));
        }
    }
    bool progress = true;
    while (progress && f.valid) {
        progress = false;
        for (int i = 0; i < acount(f.bindings) && f.valid; i++) {
            reference_binding *b = &f.bindings[i];
            if (!b->pending) {
                continue;
            }
            b->pending = false;
            progress = true;
            TSNode value = field(b->node, "value");
            if (b->callable >= 0 && is(value, "string") &&
                spells(&f, b->document, value, "")) {
                edit(&f, b->document, value, "");
                for (uint32_t child = 0; child < ts_node_child_count(b->node); child++) {
                    TSNode token = ts_node_child(b->node, child);
                    if (is(token, "=")) {
                        edit(&f, b->document, token, "");
                    }
                }
            } else {
                mark_value(&f, b->document, value, b->reference);
            }
            follow_writes(
                &f,
                b->document,
                ts_tree_root_node(f.syntax[b->document]->tree),
                i
            );
            if (b->callable >= 0) {
                reference_callable *c = &f.callables[b->callable];
                for (int ordinal = 0; ordinal < acount(c->parameters); ordinal++) {
                    if (c->parameters[ordinal] != i) {
                        continue;
                    }
                    for (int document = 0; document < count; document++) {
                        if (f.syntax[document]) {
                            follow_callers(
                                &f,
                                document,
                                ts_tree_root_node(f.syntax[document]->tree),
                                b->callable,
                                ordinal,
                                b->reference
                            );
                        }
                    }
                }
            }
            annotate(&f, b);
        }
    }
    for (int i = 0; i < count && f.valid; i++) {
        if (f.syntax[i]) {
            check_text_uses(&f, i, ts_tree_root_node(f.syntax[i]->tree));
        }
    }
    for (int i = 0; i < count; i++) {
        script_syntax_free(f.syntax[i]);
    }
    for (int i = 0; i < acount(f.callables); i++) {
        afree(f.callables[i].parameters);
    }
    afree(f.callables);
    afree(f.bindings);
    free(f.syntax);
    *edits = f.edits;
    return f.valid;
}

void chibi_migrate26_edits_free(text_edit **edits, int count)
{
    for (int i = 0; i < count; i++) {
        for (int j = 0; j < acount(edits[i]); j++) {
            free((void *)edits[i][j].text);
        }
        afree(edits[i]);
    }
    free(edits);
}
