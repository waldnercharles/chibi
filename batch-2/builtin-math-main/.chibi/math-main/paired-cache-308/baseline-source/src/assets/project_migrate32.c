#include "project_migrate32.h"

#include "project_migrate26.h"
#include "support/array.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

/* An action that only sets a state, which version 33 inlines. */
typedef struct
{
    TSNode declaration, value, after;
    bool inlining, qualified;
} state_action;

typedef struct
{
    const char *text;
    state_action *actions;
    TSNode refused;
    const char *reason;
    bool changed;
} rewrite;

static void render(rewrite *r, TSNode node, char **out);

static bool is(TSNode node, const char *type)
{
    return !ts_node_is_null(node) && !strcmp(ts_node_type(node), type);
}
static TSNode field(TSNode node, const char *name)
{
    return ts_node_child_by_field_name(node, name, (uint32_t)strlen(name));
}
static int start(TSNode node)
{
    return (int)ts_node_start_byte(node);
}
static int end(TSNode node)
{
    return (int)ts_node_end_byte(node);
}
static void append(char **out, const char *text, int length)
{
    if (!length) {
        return;
    }
    int at = acount(*out);
    asetlen(*out, at + length);
    memcpy(*out + at, text, (size_t)length);
}
static void append_string(char **out, const char *text)
{
    append(out, text, (int)strlen(text));
}
static void refuse(rewrite *r, TSNode node, const char *reason)
{
    if (ts_node_is_null(r->refused)) {
        r->refused = node;
        r->reason = reason;
    }
}
/* Whether node spells want as the catalog matches names: without case,
 * spaces, underscores or hyphens. */
static bool named(const char *text, TSNode node, const char *want)
{
    if (ts_node_is_null(node)) {
        return false;
    }
    for (int i = start(node); i < end(node); i++) {
        char c = text[i];
        if (c == ' ' || c == '_' || c == '-') {
            continue;
        }
        if (!*want || tolower((unsigned char)c) != *want++) {
            return false;
        }
    }
    return !*want;
}
static bool same_text(const char *text, TSNode a, TSNode b)
{
    return end(a) - start(a) == end(b) - start(b) &&
           !memcmp(text + start(a), text + start(b), (size_t)(end(a) - start(a)));
}
/* Whether a call's single type argument names its own callee, as in
 * `direction_to<Direction_To>(...)`, which version 32 ignored. */
static bool same_name(const char *text, TSNode function, TSNode types)
{
    if (!is(function, "identifier") || ts_node_named_child_count(types) != 1) {
        return false;
    }
    char want[64];
    int length = 0;
    for (int i = start(function); i < end(function) && length < (int)sizeof want - 1;
         i++) {
        if (text[i] != '_') {
            want[length++] = (char)tolower((unsigned char)text[i]);
        }
    }
    want[length] = '\0';
    return named(text, ts_node_named_child(types, 0), want);
}
static TSNode named_child(TSNode node, uint32_t index)
{
    return ts_node_named_child(node, index);
}
static uint32_t named_count(TSNode node)
{
    return ts_node_is_null(node) ? 0 : ts_node_named_child_count(node);
}
/* Appends the name a single type argument spells; refuses anything but a
 * type name. */
static void argument_name(rewrite *r, TSNode types, char **out)
{
    TSNode type = named_count(types) == 1 ? named_child(types, 0) : (TSNode){ 0 };
    TSNode name = is(type, "type") && named_count(type) == 1 ? field(type, "name")
                                                             : type;
    if (!is(name, "type_name")) {
        refuse(r, types, "type argument");
        return;
    }
    for (int i = start(name); i < end(name); i++) {
        apush(*out, r->text[i]);
    }
}
static char *rendered(rewrite *r, TSNode node)
{
    char *out = NULL;
    render(r, node, &out);
    apush(out, '\0');
    char *copy = xstrdup(out);
    afree(out);
    return copy;
}
/* `struct<T>`, `enum<T>` and `handle<ecs.entity>` name their argument;
 * `array<T>` is `T[]` and `task<T>` is `task(T)`. */
static void render_type(rewrite *r, TSNode node, char **out)
{
    TSNode name = field(node, "name"), arguments = field(node, "arguments");
    append(out, r->text + start(node), start(name) - start(node));
    TSNode argument = named_count(arguments) == 1 ? named_child(arguments, 0)
                                                  : (TSNode){ 0 };
    char *inner = ts_node_is_null(argument) ? NULL : rendered(r, argument);
    if (!inner) {
        refuse(r, node, "type argument list");
    } else if (named(r->text, name, "handle") && (!strcmp(inner, "ecs.entity") || !strcmp(inner, "\"ecs.entity\""))) {
        append_string(out, "entity");
    } else if (named(r->text, name, "struct") || named(r->text, name, "enum")) {
        append_string(out, inner);
    } else if (named(r->text, name, "array") && !strchr(inner, '|')) {
        append_string(out, inner);
        append_string(out, "[]");
    } else if (named(r->text, name, "task") && !strpbrk(inner, "[?|")) {
        append_string(out, "task(");
        append_string(out, inner);
        append_string(out, ")");
    } else {
        refuse(r, node, "type argument");
    }
    free(inner);
    append(out, r->text + end(arguments), end(node) - end(arguments));
    r->changed = true;
}
/* The value a call's single argument passes, or a null node. */
static TSNode sole_value(TSNode arguments)
{
    if (named_count(arguments) != 1) {
        return (TSNode){ 0 };
    }
    TSNode argument = named_child(arguments, 0);
    TSNode value = field(argument, "value");
    return ts_node_is_null(value) ? argument : value;
}
/* `send_event<E>(target, ...)` is `emit target.E(...)`. The target is the
 * argument named Entity, or else the first positional one. */
static void render_send(rewrite *r, TSNode node, char **out)
{
    TSNode types = field(node, "type_arguments");
    if (memchr(r->text + start(types), '.', (size_t)(end(types) - start(types)))) {
        refuse(r, types, "qualified event");
        return;
    }
    TSNode arguments = field(node, "arguments"), target = { 0 };
    for (uint32_t i = 0; i < named_count(arguments) && ts_node_is_null(target); i++) {
        TSNode argument = named_child(arguments, i);
        if (named(r->text, field(argument, "name"), "entity")) {
            target = argument;
        }
    }
    for (uint32_t i = 0; i < named_count(arguments) && ts_node_is_null(target); i++) {
        TSNode argument = named_child(arguments, i);
        if (!is(argument, "named_argument") &&
            !is(argument, "typed_argument") && !is(argument, "comment")) {
            target = argument;
        }
    }
    if (ts_node_is_null(target)) {
        refuse(r, node, "event send without a target");
        return;
    }
    TSNode value = ts_node_is_null(field(target, "value")) ? target
                                                           : field(target, "value");
    bool simple = is(value, "identifier") || is(value, "member_expression") ||
                  is(value, "call_expression") || is(value, "index_expression") ||
                  is(value, "parenthesized_expression");
    append_string(out, simple ? "emit " : "emit (");
    render(r, value, out);
    append_string(out, simple ? "." : ").");
    argument_name(r, types, out);
    append_string(out, "(");
    int count = 0;
    for (uint32_t i = 0; i < named_count(arguments); i++) {
        TSNode argument = named_child(arguments, i);
        if (ts_node_eq(argument, target) || is(argument, "comment")) {
            continue;
        }
        append_string(out, count++ ? ", " : "");
        render(r, argument, out);
    }
    append_string(out, ")");
}
/* Whether a declaration inside `node` binds the name `state` spells. */
static bool binds(const rewrite *r, TSNode node, TSNode state)
{
    for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
        TSNode child = ts_node_child(node, i);
        const char *name = ts_node_field_name_for_child(node, i);
        bool declares = is(node, "pattern_bindings") ||
                        (name && !strcmp(name, "name") &&
                         (is(node, "local_declaration") ||
                          is(node, "constant_declaration") ||
                          is(node, "parameter") || is(node, "for_statement"))) ||
                        (name && !strcmp(name, "key") && is(node, "for_statement")) ||
                        (name && !strcmp(name, "binding") &&
                         (is(node, "is_expression") || is(node, "as_expression")));
        if ((declares && same_text(r->text, child, state)) || binds(r, child, state)) {
            return true;
        }
    }
    return false;
}
/* Whether a declaration in the callable around `at` could shadow the state
 * `types` names. */
static bool shadowed(const rewrite *r, TSNode at, TSNode types)
{
    TSNode type = named_count(types) == 1 ? named_child(types, 0) : (TSNode){ 0 };
    TSNode name = is(type, "type") && named_count(type) == 1 ? field(type, "name")
                                                             : type;
    TSNode callable = at;
    /* An `after X { ... }` handler may have no parameters but still scopes
     * its locals. */
    while (!ts_node_is_null(callable) && !is(callable, "handler") &&
           ts_node_is_null(field(callable, "parameters"))) {
        callable = ts_node_parent(callable);
    }
    return !ts_node_is_null(callable) && binds(r, callable, name);
}
/* Appends the state `types` names, as `state.X` when it may be shadowed. */
static void state_name(rewrite *r, TSNode types, bool qualified, char **out)
{
    append_string(out, qualified ? "state." : "");
    argument_name(r, types, out);
}
static void render_call(rewrite *r, TSNode node, char **out)
{
    TSNode function = field(node, "function"),
           types = field(node, "type_arguments"),
           arguments = field(node, "arguments");
    r->changed = true;
    if (named(r->text, function, "convert")) {
        argument_name(r, types, out);
        render(r, arguments, out);
    } else if (named(r->text, function, "getvariable") && !named_count(arguments)) {
        state_name(r, types, shadowed(r, node, types), out);
    } else if (named(r->text, function, "sendevent")) {
        render_send(r, node, out);
    } else if (same_name(r->text, function, types)) {
        render(r, function, out);
        render(r, arguments, out);
    } else {
        refuse(r, node, "angle-bracket call");
    }
}
/* `set_variable<X>(v)` as `X = v`. */
static void render_store(rewrite *r, TSNode call, bool qualified, char **out)
{
    TSNode value = sole_value(field(call, "arguments"));
    if (ts_node_is_null(value)) {
        refuse(r, call, "state write without one value");
        return;
    }
    state_name(r, field(call, "type_arguments"), qualified, out);
    append_string(out, " = ");
    render(r, value, out);
    r->changed = true;
}
static bool store_call(const rewrite *r, TSNode node)
{
    return is(node, "call_expression") &&
           !ts_node_is_null(field(node, "type_arguments")) &&
           named(r->text, field(node, "function"), "setvariable");
}
static state_action *action_named(const rewrite *r, TSNode name)
{
    for (int i = 0; i < acount(r->actions); i++) {
        if (same_text(r->text, field(r->actions[i].declaration, "name"), name)) {
            return &r->actions[i];
        }
    }
    return NULL;
}
static bool dropped(const rewrite *r, TSNode node)
{
    for (int i = 0; i < acount(r->actions); i++) {
        if (ts_node_eq(node, r->actions[i].declaration) ||
            ts_node_eq(node, r->actions[i].after)) {
            return true;
        }
    }
    return false;
}
/* The offset where the whitespace before `node` on its line begins. */
static int line_start(const rewrite *r, TSNode node)
{
    int line = start(node);
    while (line > 0 && (r->text[line - 1] == ' ' || r->text[line - 1] == '\t')) {
        line--;
    }
    return line;
}
/* Whether `node` is `N.Value` for the action N. */
static bool value_read(const rewrite *r, TSNode node, const state_action *action)
{
    return is(node, "member_expression") &&
           named(r->text, field(node, "member"), "value") &&
           same_text(r->text, field(node, "object"), field(action->declaration, "name"));
}
/* Whether `node` reads `N.Value` for the action N. */
static bool reads_value(const rewrite *r, TSNode node, const state_action *action)
{
    if (value_read(r, node, action)) {
        return true;
    }
    for (uint32_t i = 0; i < named_count(node); i++) {
        if (reads_value(r, named_child(node, i), action)) {
            return true;
        }
    }
    return false;
}
/* Whether `node` itself may change the state N sets, or let other code run,
 * once its operands are evaluated: an assignment to the state, a state write,
 * an action, an await or a yield. */
static bool changes(const rewrite *r, TSNode node, const state_action *action)
{
    TSNode types = field(action->value, "type_arguments");
    TSNode state = field(named_child(types, 0), "name");
    TSNode target = field(node, "target");
    if (is(target, "member_expression") &&
        is(field(target, "object"), "builtin_reference")) {
        target = field(target, "member");
    }
    return (is(node, "assignment") && same_text(r->text, target, state)) ||
           is(node, "await_expression") || is(node, "yield_statement") ||
           store_call(r, node) ||
           (is(node, "call_expression") && action_named(r, field(node, "function")));
}
static bool may_change(const rewrite *r, TSNode node, const state_action *action)
{
    if (changes(r, node, action)) {
        return true;
    }
    for (uint32_t i = 0; i < named_count(node); i++) {
        if (may_change(r, named_child(node, i), action)) {
            return true;
        }
    }
    return false;
}
/* Whether `node` may read `N.Value` after the state could have changed, in
 * evaluation order. A loop that changes anything repeats its earlier reads. */
static bool stale_read(const rewrite *r, TSNode node, const state_action *action, bool *changed)
{
    if (value_read(r, node, action)) {
        return *changed;
    }
    if ((is(node, "while_statement") || is(node, "for_statement")) &&
        reads_value(r, node, action) && may_change(r, node, action)) {
        return true;
    }
    for (uint32_t i = 0; i < named_count(node); i++) {
        if (stale_read(r, named_child(node, i), action, changed)) {
            return true;
        }
    }
    *changed = *changed || changes(r, node, action);
    return false;
}
/* `N()` for an inlined action N: its assignment, then the body of `after N`
 * reindented to the statement's indentation, reading the state for
 * `N.Value`. That holds only while nothing in the body can change the state
 * before a read. */
static void render_invocation(rewrite *r, TSNode statement, state_action *action, char **out)
{
    if (action->inlining) {
        refuse(r, statement, "action invoked from its own after body");
        return;
    }
    TSNode body = ts_node_is_null(action->after) ? (TSNode){ 0 }
                                                 : field(action->after, "body");
    TSNode types = field(action->value, "type_arguments");
    bool changed = false;
    if (!ts_node_is_null(body) && stale_read(r, body, action, &changed)) {
        refuse(r, action->after, "action result read after the state may change");
        return;
    }
    action->inlining = true;
    action->qualified = shadowed(r, statement, types) ||
                        (!ts_node_is_null(body) &&
                         binds(r, body, field(named_child(types, 0), "name")));
    render_store(r, action->value, action->qualified, out);
    const char *to = r->text + line_start(r, statement);
    int to_size = start(statement) - line_start(r, statement);
    for (uint32_t i = 0; i < named_count(body); i++) {
        TSNode child = named_child(body, i);
        const char *from = r->text + line_start(r, child);
        size_t from_size = (size_t)(start(child) - line_start(r, child));
        char *text = rendered(r, child);
        append_string(out, "\n");
        append(out, to, to_size);
        for (const char *c = text; *c; c++) {
            apush(*out, *c);
            if (*c == '\n' && !strncmp(c + 1, from, from_size)) {
                append(out, to, to_size);
                c += from_size;
            }
        }
        free(text);
        /* The statement's own terminator follows the inlined body. */
        while (isspace((unsigned char)alast(*out))) {
            apop(*out);
        }
    }
    action->inlining = false;
}
static bool render_statement(rewrite *r, TSNode node, char **out)
{
    TSNode expression = named_child(node, 0);
    TSNode call = is(expression, "await_expression") ? field(expression, "value")
                                                     : expression;
    state_action *action = is(call, "call_expression") &&
                                   is(field(call, "function"), "identifier") &&
                                   !named_count(field(call, "arguments"))
                             ? action_named(r, field(call, "function"))
                             : NULL;
    if (!action && !store_call(r, call)) {
        return false;
    }
    append(out, r->text + start(node), start(expression) - start(node));
    if (action) {
        render_invocation(r, node, action, out);
    } else {
        render_store(r, call, shadowed(r, call, field(call, "type_arguments")), out);
    }
    append(out, r->text + end(expression), end(node) - end(expression));
    return true;
}
static void render(rewrite *r, TSNode node, char **out)
{
    if (is(node, "type") && !ts_node_is_null(field(node, "arguments"))) {
        render_type(r, node, out);
        return;
    }
    if (is(node, "call_expression") &&
        !ts_node_is_null(field(node, "type_arguments"))) {
        render_call(r, node, out);
        return;
    }
    if (is(node, "expression_statement") && render_statement(r, node, out)) {
        return;
    }
    const state_action *action = is(node, "member_expression") &&
                                         named(r->text, field(node, "member"), "value")
                                   ? action_named(r, field(node, "object"))
                                   : NULL;
    if (action && !action->inlining) {
        refuse(r, node, "action result read outside its after body");
        return;
    }
    if (action) {
        state_name(r, field(action->value, "type_arguments"), action->qualified, out);
        return;
    }
    if (is(node, "identifier") && action_named(r, node)) {
        refuse(r, node, "use of an action that sets a state");
    }
    int at = start(node);
    for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
        TSNode child = ts_node_child(node, i);
        append(out, r->text + at, start(child) - at);
        at = end(child);
        if (!dropped(r, child)) {
            render(r, child, out);
            continue;
        }
        /* A dropped declaration takes its line with it. Its terminator may
         * already end on the next line, after that line's indentation. */
        int last = at;
        while (last > start(child) && isspace((unsigned char)r->text[last - 1])) {
            last--;
        }
        if (memchr(r->text + last, '\n', (size_t)(at - last))) {
            continue;
        }
        while (acount(*out) && (alast(*out) == ' ' || alast(*out) == '\t')) {
            apop(*out);
        }
        int skip = at;
        while (r->text[skip] == ' ' || r->text[skip] == '\t' || r->text[skip] == ';') {
            skip++;
        }
        at = r->text[skip] == '\n' ? skip + 1 : at;
    }
    append(out, r->text + at, end(node) - at);
}
/* How many actions, functions and macros inside `node` declare `name`. */
static int declarations(const rewrite *r, TSNode node, TSNode name)
{
    int count = (is(node, "action_declaration") || is(node, "function_declaration") ||
                 is(node, "macro_declaration")) &&
                same_text(r->text, field(node, "name"), name);
    for (uint32_t i = 0; i < named_count(node); i++) {
        count += declarations(r, named_child(node, i), name);
    }
    return count;
}
static void collect_actions(rewrite *r, TSNode node)
{
    TSNode value = field(node, "value");
    if (is(node, "action_declaration") && store_call(r, value)) {
        /* Inlining resolves `N()` by name across the file. */
        TSNode root = node;
        while (!ts_node_is_null(ts_node_parent(root))) {
            root = ts_node_parent(root);
        }
        if (declarations(r, root, field(node, "name")) > 1) {
            refuse(r, node, "action name declared more than once");
        }
        apush(r->actions, ((state_action){ .declaration = node, .value = value }));
    }
    for (uint32_t i = 0; i < ts_node_named_child_count(node); i++) {
        collect_actions(r, ts_node_named_child(node, i));
    }
}
static void collect_after(rewrite *r, TSNode node)
{
    if (is(node, "handler") && named(r->text, field(node, "kind"), "after")) {
        for (int i = 0; i < acount(r->actions); i++) {
            TSNode name = field(r->actions[i].declaration, "name");
            if (!same_text(r->text, field(node, "name"), name)) {
                continue;
            }
            if (!ts_node_is_null(field(node, "parameters"))) {
                refuse(r, node, "after handler with parameters");
            }
            if (!ts_node_is_null(r->actions[i].after)) {
                refuse(r, node, "second after handler for one action");
            }
            r->actions[i].after = node;
        }
    }
    for (uint32_t i = 0; i < ts_node_named_child_count(node); i++) {
        collect_after(r, ts_node_named_child(node, i));
    }
}

int chibi_migrate32_angle_forms(const char *source, char **migrated, char *error, size_t size)
{
    *migrated = NULL;
    script_syntax *syntax = chibi_migrate_parse(source, 32);
    if (syntax->diagnostic_count) {
        script_syntax_free(syntax);
        snprintf(error, size, "Cannot migrate invalid version 32 source");
        return -1;
    }
    TSNode root = ts_tree_root_node(syntax->tree);
    rewrite r = { .text = source };
    collect_actions(&r, root);
    collect_after(&r, root);
    r.changed = acount(r.actions) > 0;
    char *out = NULL;
    render(&r, root, &out);
    int status = 0;
    if (!ts_node_is_null(r.refused)) {
        script_range range = script_node_range(r.refused);
        snprintf(
            error,
            size,
            "%s at %d:%d has no version 33 spelling; rewrite it first",
            r.reason,
            range.line,
            range.column
        );
        status = -1;
    } else if (r.changed) {
        apush(out, '\0');
        *migrated = xstrdup(out);
    }
    afree(out);
    afree(r.actions);
    script_syntax_free(syntax);
    return status;
}
