#include "language/semantic_internal.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <stdlib.h>
#include <string.h>

int script_analyzer_statement_add(analyzer *a, script_statement statement)
{
    statement.child_count = acount(statement.children);
    apush(a->out->statements, statement);
    return acount(a->out->statements) - 1;
}
void script_analyzer_narrow_condition(analyzer *a, int expression_index, bool truth)
{
    if (expression_index < 0) {
        return;
    }
    const script_expression *condition = &a->out->expressions[expression_index];
    if (condition->kind == SCRIPT_EXPR_SYMBOL &&
        condition->type.flags & SCRIPT_TYPE_OPTIONAL) {
        apush(a->narrowed, ((narrowing){ condition->symbol, truth }));
        return;
    }
    if (condition->kind == SCRIPT_EXPR_UNARY && condition->op == SCRIPT_OP_NOT) {
        script_analyzer_narrow_condition(a, condition->left, !truth);
        return;
    }
    if (condition->kind == SCRIPT_EXPR_BINARY &&
        ((condition->op == SCRIPT_OP_AND && truth) ||
         (condition->op == SCRIPT_OP_OR && !truth))) {
        script_analyzer_narrow_condition(a, condition->left, truth);
        script_analyzer_narrow_condition(a, condition->right, truth);
        return;
    }
    if (condition->kind == SCRIPT_EXPR_BINARY &&
        (condition->op == SCRIPT_OP_EQ || condition->op == SCRIPT_OP_NE)) {
        int value = -1;
        if (a->out->expressions[condition->left].kind == SCRIPT_EXPR_NIL) {
            value = condition->right;
        }
        if (a->out->expressions[condition->right].kind == SCRIPT_EXPR_NIL) {
            value = condition->left;
        }
        if (value >= 0) {
            script_analyzer_narrow_condition(a, value, condition->op == SCRIPT_OP_NE ? truth : !truth);
        }
    }
}
/* A component path tested as a condition asks whether the entity has it. */
script_type script_analyzer_presence(analyzer *a, int value)
{
    script_expression *e = &a->out->expressions[value];
    if (e->kind == SCRIPT_EXPR_COMPONENT) {
        e->type.flags |= SCRIPT_TYPE_OPTIONAL;
    }
    return e->type;
}
void script_analyzer_condition_type(analyzer *a, TSNode node, int value)
{
    script_type type = script_analyzer_presence(a, value);
    if (!(type.flags & (SCRIPT_TYPE_OPTIONAL | SCRIPT_TYPE_TASK))) {
        script_analyzer_require(a, node, script_analyzer_boolean_type(), type);
    }
}
int script_analyzer_statement_block(analyzer *a, TSNode node)
{
    int old_scope = a->scope;
    a->scope = script_analyzer_scope_new(a, node);
    script_statement result = { .kind = SCRIPT_STMT_BLOCK,
                                .scope = a->scope,
                                .range = script_analyzer_range(node),
                                .symbol = -1,
                                .expression = -1,
                                .second = -1,
                                .third = -1,
                                .body = -1,
                                .alternative = -1 };
    for (uint32_t i = 0; i < script_analyzer_named_count(node); i++) {
        TSNode child = ts_node_named_child(node, i);
        if (script_analyzer_is(child, "label_statement")) {
            script_analyzer_symbol_new(
                a,
                script_analyzer_field(child, "name"),
                SCRIPT_LABEL,
                (script_type){ .storage = PIN_FLOW },
                false
            );
        }
    }
    for (uint32_t i = 0; i < script_analyzer_named_count(node); i++) {
        TSNode child = ts_node_named_child(node, i);
        if (ts_node_is_extra(child)) {
            continue;
        }
        int index = script_analyzer_statement(a, child);
        apush(result.children, index);
        result.effects |= a->out->statements[index].effects;
    }
    a->scope = old_scope;
    return script_analyzer_statement_add(a, result);
}
/* A task symbol declared without its function takes the function of the
 * tasks assigned to it, so awaiting it gives that function's results. Tasks
 * of different or unknown functions leave it awaitable only for completion. */
void script_analyzer_infer_task(analyzer *a, int symbol, script_type value)
{
    script_symbol *s = &a->out->symbols[symbol];
    if (!(s->type.flags & SCRIPT_TYPE_TASK) || !(value.flags & SCRIPT_TYPE_TASK) ||
        (value.nominal && s->type.nominal == value.nominal) || s->mixed_tasks) {
        return;
    }
    s->mixed_tasks = s->type.nominal != 0 || !value.nominal;
    s->type.nominal = s->mixed_tasks ? 0 : value.nominal;
}
/* The function a call starts a task of, by name: a local function or a
 * library function that waits. */
static uint64_t task_function(analyzer *a, TSNode call)
{
    char *name = script_analyzer_qualified_name(a, script_analyzer_field(call, "function"));
    int symbol = script_analyzer_callable_symbol(a, name);
    int index = script_catalog_find_capability(a->catalog, name, SCRIPT_CAPABILITY_FUNCTION);
    free(name);
    if (symbol >= 0 && a->out->symbols[symbol].kind == SCRIPT_FUNCTION) {
        return a->out->symbols[symbol].id;
    }
    const script_catalog_capability *library = index >= 0
                                                 ? &a->catalog->capabilities[index]
                                                 : NULL;
    return library && (library->effects & SCRIPT_EFFECT_SUSPEND) ? library->id : 0;
}
/* Whether a local or parameter declared directly in `scope` is named `name`. */
static bool declares(analyzer *a, TSNode scope, const char *name)
{
    for (uint32_t i = 0; i < script_analyzer_named_count(scope); i++) {
        TSNode child = ts_node_named_child(scope, i);
        if (script_analyzer_is(child, "parameter_list") && declares(a, child, name)) {
            return true;
        }
        if (!script_analyzer_is(child, "local_declaration") &&
            !script_analyzer_is(child, "parameter")) {
            continue;
        }
        char *declared = script_analyzer_name_copy(a, script_analyzer_field(child, "name"));
        bool same = !strcmp(declared, name);
        free(declared);
        if (same) {
            return true;
        }
    }
    return false;
}
/* Whether a block or callable enclosing `node` declares `name`. */
static bool shadowed(analyzer *a, TSNode node, const char *name)
{
    for (; !ts_node_is_null(node); node = ts_node_parent(node)) {
        if (declares(a, node, name)) {
            return true;
        }
    }
    return false;
}
/* The task state an assignment targets: `name` unless a local shadows it, or
 * `state.name`. */
static int task_target(analyzer *a, TSNode node)
{
    TSNode target = script_analyzer_field(node, "target");
    if (script_analyzer_is(target, "member_expression")) {
        TSNode object = script_analyzer_field(target, "object");
        return script_analyzer_is(object, "builtin_reference") &&
                       script_analyzer_text_is(a, object, "state")
                 ? script_analyzer_lookup_storage(a, script_analyzer_field(target, "member"), SCRIPT_STATE)
                 : -1;
    }
    int symbol = script_analyzer_is(target, "identifier")
                   ? script_analyzer_lookup(a, target)
                   : -1;
    return symbol >= 0 && !shadowed(a, node, a->out->symbols[symbol].name) ? symbol
                                                                           : -1;
}
/* Whether a callable between `node` and the program `body` declares a
 * function or macro named like the call's function, which the prepass cannot
 * resolve yet. */
static bool nested_callee(analyzer *a, TSNode body, TSNode node, TSNode call)
{
    TSNode callee = script_analyzer_field(call, "function");
    if (!script_analyzer_is(callee, "identifier")) {
        return false;
    }
    char *name = script_analyzer_name_copy(a, callee);
    bool found = false;
    for (; !found && !ts_node_eq(node, body); node = ts_node_parent(node)) {
        for (uint32_t i = 0; !found && i < script_analyzer_named_count(node); i++) {
            TSNode child = ts_node_named_child(node, i);
            if (!script_analyzer_is(child, "function_declaration") &&
                !script_analyzer_is(child, "macro_declaration")) {
                continue;
            }
            char *declared = script_analyzer_name_copy(a, script_analyzer_field(child, "name"));
            found = !strcmp(declared, name);
            free(declared);
        }
    }
    free(name);
    return found;
}
/* The state `set_variable<"name">(value)` writes, or -1. */
static int state_write(analyzer *a, TSNode call, TSNode *value)
{
    char *name = script_analyzer_qualified_name(a, script_analyzer_field(call, "function"));
    int index = script_catalog_find_capability(a->catalog, name, SCRIPT_CAPABILITY_FUNCTION);
    free(name);
    TSNode types = script_analyzer_field(call, "type_arguments");
    TSNode arguments = script_analyzer_field(call, "arguments");
    if (index < 0 ||
        a->catalog->capabilities[index].state_access != SCRIPT_STATE_ACCESS_WRITE ||
        script_analyzer_named_count(types) != 1 ||
        script_analyzer_named_count(arguments) != 1) {
        return -1;
    }
    TSNode argument = ts_node_named_child(arguments, 0);
    *value = script_analyzer_field(argument, "value");
    if (ts_node_is_null(*value)) {
        *value = argument;
    }
    TSNode type = ts_node_named_child(types, 0);
    return script_analyzer_lookup_storage(a, script_analyzer_field(type, "name"), SCRIPT_STATE);
}
static void task_hints(analyzer *a, TSNode body, TSNode node)
{
    TSNode value = { 0 };
    int symbol = -1;
    if (script_analyzer_is(node, "assignment") &&
        script_analyzer_is(script_analyzer_field(node, "operator"), "=")) {
        value = script_analyzer_field(node, "value");
        symbol = task_target(a, node);
    } else if (script_analyzer_is(node, "call_expression")) {
        symbol = state_write(a, node, &value);
    }
    while (script_analyzer_is(value, "parenthesized_expression")) {
        value = script_analyzer_field(value, "expression");
    }
    if (symbol >= 0 && (a->out->symbols[symbol].type.flags & SCRIPT_TYPE_TASK) &&
        script_analyzer_is(value, "call_expression") &&
        !nested_callee(a, body, node, value)) {
        script_type task = { .storage = PIN_U64,
                             .nominal = task_function(a, value),
                             .flags = SCRIPT_TYPE_TASK };
        script_analyzer_infer_task(a, symbol, task);
    }
    for (uint32_t i = 0; i < script_analyzer_named_count(node); i++) {
        task_hints(a, body, ts_node_named_child(node, i));
    }
}
/* Infers task states from every `state = Call()` before any body is analyzed,
 * so an `await` reads the same type whichever handler comes first. A name
 * that a local or parameter of the same callable also uses, or a call to a
 * nested callable, is left to the in-order inference, which resolves it in
 * its own scope. */
void script_analyzer_task_hints(analyzer *a, TSNode body)
{
    task_hints(a, body, body);
}
/* The first `return` of an inferred function declares its results: one
 * `Result`, or the labels of `return(Hit: h, Distance: d)`. */
static void infer_results(analyzer *a, script_function *function, TSNode values)
{
    bool list = script_analyzer_is(values, "argument_list");
    uint32_t count = list ? script_analyzer_named_count(values) : 1;
    int old_scope = a->scope;
    a->scope = function->scope;
    for (uint32_t i = 0; i < count; i++) {
        TSNode child = list ? ts_node_named_child(values, i) : values;
        if (ts_node_is_extra(child)) {
            continue;
        }
        TSNode name = list ? script_analyzer_field(child, "name") : (TSNode){ 0 };
        if (ts_node_is_null(name) && count > 1) {
            script_analyzer_diagnostic(a, child, "Name each result, as in return(Hit: h, Distance: d)");
            break;
        }
        script_type untyped = { .flags = SCRIPT_TYPE_UNTYPED };
        int symbol = ts_node_is_null(name)
                       ? script_analyzer_result_new(a, child, "Result", untyped)
                       : script_analyzer_symbol_new(a, name, SCRIPT_RESULT, untyped, true);
        apush(function->results, symbol);
    }
    a->scope = old_scope;
}
static void infer_type(analyzer *a, int symbol, int expression)
{
    script_type type = script_analyzer_expr_type(a, expression);
    type.flags &= ~SCRIPT_TYPE_REFERENCE;
    a->out->symbols[symbol].type = type;
}
int script_analyzer_statement_inner(analyzer *a, TSNode node)
{
    if (script_analyzer_is(node, "block")) {
        return script_analyzer_statement_block(a, node);
    }
    script_statement result = { .scope = a->scope,
                                .range = script_analyzer_range(node),
                                .symbol = -1,
                                .expression = -1,
                                .second = -1,
                                .third = -1,
                                .body = -1,
                                .alternative = -1 };
    if (ts_node_is_null(node)) {
        script_analyzer_diagnostic(a, node, "Missing statement");
        result.kind = SCRIPT_STMT_BLOCK;
        return script_analyzer_statement_add(a, result);
    }
    if (script_analyzer_is(node, "local_declaration")) {
        result.kind = SCRIPT_STMT_DECLARE;
        script_type type = script_analyzer_type_node(a, script_analyzer_field(node, "type"));
        result.expression = script_analyzer_expression(a, script_analyzer_field(node, "value"), type);
        if (type.flags & SCRIPT_TYPE_UNTYPED) {
            type = script_analyzer_expr_type(a, result.expression);
            type.flags &= ~SCRIPT_TYPE_UNTYPED;
        } else {
            script_analyzer_require(
                a,
                node,
                type,
                script_analyzer_expr_type(a, result.expression)
            );
        }
        int existing = script_analyzer_lookup(a, script_analyzer_field(node, "name"));
        if (existing >= 0 && a->out->symbols[existing].kind == SCRIPT_LOCAL &&
            a->out->symbols[existing].scope == a->scope &&
            a->out->symbols[existing].declaration.start ==
                script_analyzer_range(script_analyzer_field(node, "name")).start) {
            result.symbol = existing;
            if (a->out->symbols[existing].type.flags & SCRIPT_TYPE_UNTYPED) {
                a->out->symbols[existing].type = type;
            } else {
                script_analyzer_require(a, node, a->out->symbols[existing].type, type);
            }
        } else {
            result.symbol = script_analyzer_symbol_new(
                a,
                script_analyzer_field(node, "name"),
                SCRIPT_LOCAL,
                type,
                true
            );
        }
        a->out->symbols[result.symbol].initializer = result.expression;
        script_analyzer_infer_task(
            a,
            result.symbol,
            script_analyzer_expr_type(a, result.expression)
        );
        result.effects = script_analyzer_expr_effects(a, result.expression);
        if (a->out->expressions[result.expression].awaited) {
            for (int i = 0; i < acount(a->out->symbols); i++) {
                if (a->out->symbols[i].scope == a->scope &&
                    a->out->symbols[i].kind == SCRIPT_LABEL) {
                    script_analyzer_diagnostic(a, node, "Awaited locals are not allowed in a scope with labels");
                    break;
                }
            }
        }
        return script_analyzer_statement_add(a, result);
    }
    if (script_analyzer_is(node, "assignment")) {
        result.kind = SCRIPT_STMT_ASSIGN;
        TSNode target = script_analyzer_field(node, "target");
        result.expression = script_analyzer_expression(a, target, (script_type){ .flags = SCRIPT_TYPE_UNTYPED });
        script_expression address = a->out->expressions[result.expression];
        if (!address.writable) {
            script_analyzer_diagnostic(a, target, "Assignment requires a writable value");
        }
        result.second = script_analyzer_expression(
            a,
            script_analyzer_field(node, "value"),
            address.type
        );
        script_analyzer_require_values(a, node, result.expression, result.second);
        result.op = script_analyzer_operator_node(a, script_analyzer_field(node, "operator"));
        if (result.op != SCRIPT_OP_NONE && !script_type_numeric(address.type)) {
            script_expression operation = {
                .kind = SCRIPT_EXPR_BINARY,
                .op = result.op,
                .left = result.expression,
                .right = result.second,
                .symbol = -1,
                .range = script_analyzer_range(node),
                .type = script_analyzer_bad_type(),
                .effects = address.effects |
                           script_analyzer_expr_effects(a, result.second)
            };
            if (script_analyzer_resolve_operator(a, node, script_analyzer_field(node, "operator"), &operation)) {
                result.second = script_analyzer_expr_add(a, operation);
                result.op = SCRIPT_OP_NONE;
            } else {
                script_analyzer_diagnostic(a, node, "Compound assignment requires a compatible operator");
            }
        }
        result.effects = address.effects |
                         script_analyzer_expr_effects(a, result.second);
        if (address.kind == SCRIPT_EXPR_MEMBER || address.kind == SCRIPT_EXPR_COMPONENT ||
            (address.symbol >= 0 &&
             (a->out->symbols[address.symbol].kind == SCRIPT_STATE ||
              a->out->symbols[address.symbol].kind == SCRIPT_SHARED_STATE ||
              (a->out->symbols[address.symbol].type.flags & SCRIPT_TYPE_REFERENCE)))) {
            result.effects |= SCRIPT_EFFECT_WRITE;
        }
        if (address.symbol >= 0) {
            script_analyzer_use(a, target, address.symbol, SCRIPT_WRITE, address.member);
            script_analyzer_infer_task(
                a,
                address.symbol,
                script_analyzer_expr_type(a, result.second)
            );
        }
        return script_analyzer_statement_add(a, result);
    }
    if (script_analyzer_is(node, "expression_statement")) {
        result.kind = SCRIPT_STMT_EXPRESSION;
        for (uint32_t i = 0; i < script_analyzer_named_count(node); i++) {
            TSNode value = ts_node_named_child(node, i);
            if (!ts_node_is_extra(value)) {
                result.expression = script_analyzer_expression(
                    a,
                    value,
                    (script_type){ .storage = PIN_FLOW }
                );
                break;
            }
        }
        if (result.expression >= 0 &&
            a->out->expressions[result.expression].kind != SCRIPT_EXPR_CALL &&
            a->out->expressions[result.expression].kind != SCRIPT_EXPR_STORE &&
            a->out->expressions[result.expression].kind != SCRIPT_EXPR_JOIN) {
            script_analyzer_diagnostic(a, node, "Expression statements must invoke a callable");
        }
        result.effects = script_analyzer_expr_effects(a, result.expression);
        return script_analyzer_statement_add(a, result);
    }
    if (script_analyzer_is(node, "if_statement")) {
        result.kind = SCRIPT_STMT_IF;
        result.expression = script_analyzer_expression(
            a,
            script_analyzer_field(node, "condition"),
            script_analyzer_boolean_type()
        );
        script_analyzer_condition_type(
            a,
            script_analyzer_field(node, "condition"),
            result.expression
        );
        narrowing *old = NULL;
        for (int i = 0; i < acount(a->narrowed); i++) {
            apush(old, a->narrowed[i]);
        }
        script_analyzer_narrow_condition(a, result.expression, true);
        result.body = script_analyzer_statement(a, script_analyzer_field(node, "consequence"));
        asetlen(a->narrowed, 0);
        for (int i = 0; i < acount(old); i++) {
            apush(a->narrowed, old[i]);
        }
        TSNode alternative = script_analyzer_field(node, "alternative");
        if (!ts_node_is_null(alternative)) {
            script_analyzer_narrow_condition(a, result.expression, false);
            result.alternative = script_analyzer_statement(a, alternative);
        }
        result.effects = script_analyzer_expr_effects(a, result.expression) |
                         a->out->statements[result.body].effects |
                         (result.alternative >= 0
                              ? a->out->statements[result.alternative].effects
                              : 0);
        asetlen(a->narrowed, 0);
        if (!(result.effects & SCRIPT_EFFECT_SUSPEND)) {
            for (int i = 0; i < acount(old); i++) {
                apush(a->narrowed, old[i]);
            }
        }
        afree(old);
        return script_analyzer_statement_add(a, result);
    }
    if (script_analyzer_is(node, "while_statement")) {
        result.kind = SCRIPT_STMT_WHILE;
        result.expression = script_analyzer_expression(
            a,
            script_analyzer_field(node, "condition"),
            script_analyzer_boolean_type()
        );
        script_analyzer_condition_type(
            a,
            script_analyzer_field(node, "condition"),
            result.expression
        );
        int mark = acount(a->narrowed);
        script_analyzer_narrow_condition(a, result.expression, true);
        a->loops++;
        result.body = script_analyzer_statement(a, script_analyzer_field(node, "body"));
        a->loops--;
        result.effects = script_analyzer_expr_effects(a, result.expression) |
                         a->out->statements[result.body].effects;
        if (result.effects & SCRIPT_EFFECT_SUSPEND) {
            asetlen(a->narrowed, 0);
        } else {
            asetlen(a->narrowed, mark);
        }
        return script_analyzer_statement_add(a, result);
    }
    if (script_analyzer_is(node, "for_statement") ||
        script_analyzer_is(node, "foreach_statement")) {
        int old_scope = a->scope;
        a->scope = script_analyzer_scope_new(a, node);
        result.scope = a->scope;
        TSNode first = script_analyzer_field(node, "first"),
               collection = script_analyzer_field(node, "value");
        bool range = !ts_node_is_null(first);
        result.kind = range ? SCRIPT_STMT_FOR : SCRIPT_STMT_FOREACH;
        script_type element;
        if (range) {
            result.expression = script_analyzer_expression(a, first, (script_type){ .storage = PIN_I32 });
            result.second = script_analyzer_expression(
                a,
                script_analyzer_field(node, "last"),
                (script_type){ .storage = PIN_I32 }
            );
            element = (script_type){ .storage = PIN_I32 };
            script_analyzer_require(
                a,
                first,
                element,
                script_analyzer_expr_type(a, result.expression)
            );
            script_analyzer_require(
                a,
                script_analyzer_field(node, "last"),
                element,
                script_analyzer_expr_type(a, result.second)
            );
        } else {
            result.expression = script_analyzer_expression(
                a,
                collection,
                (script_type){ .flags = SCRIPT_TYPE_UNTYPED }
            );
            element = script_analyzer_expr_type(a, result.expression);
            if (element.container != PIN_CONTAINER_ARRAY) {
                script_analyzer_diagnostic(a, collection, "Foreach requires an array");
            }
            element.container = PIN_CONTAINER_SINGLE;
        }
        result.symbol = script_analyzer_symbol_new(
            a,
            script_analyzer_field(node, "name"),
            SCRIPT_LOCAL,
            element,
            false
        );
        a->loops++;
        result.body = script_analyzer_statement(a, script_analyzer_field(node, "body"));
        a->loops--;
        result.effects = script_analyzer_expr_effects(a, result.expression) |
                         script_analyzer_expr_effects(a, result.second) |
                         a->out->statements[result.body].effects;
        a->scope = old_scope;
        return script_analyzer_statement_add(a, result);
    }
    if (script_analyzer_is(node, "yield_statement") ||
        script_analyzer_is(node, "cancel_statement")) {
        bool yield = script_analyzer_is(node, "yield_statement");
        script_expression task = { .kind = yield ? SCRIPT_EXPR_YIELD : SCRIPT_EXPR_CANCEL,
                                   .type = { .storage = PIN_FLOW },
                                   .range = script_analyzer_range(node),
                                   .symbol = -1,
                                   .left = -1,
                                   .right = -1,
                                   .entry = -1,
                                   .effects = yield ? SCRIPT_EFFECT_SUSPEND
                                                    : SCRIPT_EFFECT_WRITE };
        if (!yield) {
            TSNode value = script_analyzer_field(node, "value");
            task.left = script_analyzer_expression(
                a,
                value,
                (script_type){ .storage = PIN_U64, .flags = SCRIPT_TYPE_TASK }
            );
            if (!(script_analyzer_expr_type(a, task.left).flags & SCRIPT_TYPE_TASK)) {
                script_analyzer_diagnostic(a, value, "Cancel requires a task");
            }
            task.effects |= script_analyzer_expr_effects(a, task.left);
        }
        result.kind = SCRIPT_STMT_EXPRESSION;
        result.expression = script_analyzer_expr_add(a, task);
        result.effects = task.effects;
        if (yield) {
            asetlen(a->narrowed, 0);
        }
        return script_analyzer_statement_add(a, result);
    }
    if (script_analyzer_is(node, "break_statement") ||
        script_analyzer_is(node, "continue_statement")) {
        result.kind = script_analyzer_is(node, "break_statement")
                        ? SCRIPT_STMT_BREAK
                        : SCRIPT_STMT_CONTINUE;
        if (!a->loops) {
            script_analyzer_diagnostic(a, node, "Loop control requires a containing loop");
        }
        return script_analyzer_statement_add(a, result);
    }
    if (script_analyzer_is(node, "label_statement") ||
        script_analyzer_is(node, "goto_statement")) {
        result.kind = script_analyzer_is(node, "label_statement")
                        ? SCRIPT_STMT_LABEL
                        : SCRIPT_STMT_GOTO;
        result.symbol = script_analyzer_lookup(a, script_analyzer_field(node, "name"));
        if (result.symbol < 0 || a->out->symbols[result.symbol].kind != SCRIPT_LABEL) {
            script_analyzer_diagnostic(a, node, "Unknown label");
        } else {
            script_analyzer_use(
                a,
                script_analyzer_field(node, "name"),
                result.symbol,
                result.kind == SCRIPT_STMT_GOTO ? SCRIPT_CALL : SCRIPT_HANDLE,
                0
            );
        }
        return script_analyzer_statement_add(a, result);
    }
    if (script_analyzer_is(node, "return_statement")) {
        result.kind = SCRIPT_STMT_RETURN;
        script_function *function = a->function >= 0 ? &a->out->functions[a->function]
                                                     : NULL;
        if (!function) {
            script_analyzer_diagnostic(a, node, "Return requires a callable body");
            return script_analyzer_statement_add(a, result);
        }
        /* Entries and outlined continuations return through the owning
         * callable's result bank, while exit lookup retains the current scope. */
        while (function->parent >= 0 &&
               (function->kind == SCRIPT_FUNCTION_MACRO_ENTRY ||
                function->kind == SCRIPT_FUNCTION_CONTINUATION)) {
            function = &a->out->functions[function->parent];
        }
        TSNode values = script_analyzer_field(node, "value"),
               outcome = script_analyzer_field(node, "outcome");
        if (!ts_node_is_null(outcome)) {
            result.symbol = script_analyzer_lookup(a, outcome);
            if (result.symbol < 0 || a->out->symbols[result.symbol].kind != SCRIPT_LABEL) {
                script_analyzer_diagnostic(a, outcome, "Unknown macro exit");
            }
        }
        if (function->inferred && !acount(function->results) &&
            !ts_node_is_null(values)) {
            infer_results(a, function, values);
        }
        /* `return x` returns the single result like `return(x)`. */
        bool single = !ts_node_is_null(values) &&
                      !script_analyzer_is(values, "argument_list") &&
                      acount(function->results) == 1;
        if (single || script_analyzer_is(values, "argument_list")) {
            int ordinal = 0;
            uint32_t count = single ? 1 : script_analyzer_named_count(values);
            for (uint32_t i = 0; i < count; i++) {
                TSNode child = single ? values : ts_node_named_child(values, i);
                if (ts_node_is_extra(child)) {
                    continue;
                }
                TSNode value = script_analyzer_field(child, "value"),
                       name_node = single ? (TSNode){ 0 }
                                          : script_analyzer_field(child, "name");
                int parameter = ordinal++;
                if (!ts_node_is_null(name_node)) {
                    char *name = script_analyzer_name_copy(a, name_node);
                    parameter = -1;
                    for (int j = 0; j < acount(function->results); j++) {
                        if (!strcmp(a->out->symbols[function->results[j]].name, name)) {
                            parameter = j;
                            break;
                        }
                    }
                    free(name);
                } else {
                    value = child;
                }
                script_type
                    type = parameter >= 0 && parameter < acount(function->results)
                             ? a->out->symbols[function->results[parameter]].type
                             : script_analyzer_bad_type();
                int expr = script_analyzer_expression(a, value, type);
                script_analyzer_require(a, value, type, script_analyzer_expr_type(a, expr));
                if (type.flags & SCRIPT_TYPE_UNTYPED) {
                    infer_type(a, function->results[parameter], expr);
                }
                script_statement assign = {
                    .kind = SCRIPT_STMT_ASSIGN,
                    .scope = a->scope,
                    .symbol = parameter >= 0 && parameter < acount(function->results)
                                ? function->results[parameter]
                                : -1,
                    .expression = -1,
                    .second = expr,
                    .third = -1,
                    .body = -1,
                    .alternative = -1,
                    .range = script_analyzer_range(child),
                    .effects = script_analyzer_expr_effects(a, expr)
                };
                apush(result.children, script_analyzer_statement_add(a, assign));
                result.effects |= assign.effects;
            }
        } else if (!ts_node_is_null(values)) {
            script_type type = acount(function->results) == 1
                                 ? a->out->symbols[function->results[0]].type
                                 : script_analyzer_bad_type();
            result.expression = script_analyzer_expression(a, values, type);
            script_analyzer_require(
                a,
                values,
                type,
                script_analyzer_expr_type(a, result.expression)
            );
            result.effects = script_analyzer_expr_effects(a, result.expression);
        }
        return script_analyzer_statement_add(a, result);
    }
    if (script_analyzer_is(node, "sequence_statement") ||
        script_analyzer_is(node, "match_statement")) {
        bool match = script_analyzer_is(node, "match_statement");
        result.kind = match ? SCRIPT_STMT_MATCH : SCRIPT_STMT_SEQUENCE;
        if (match) {
            result.expression = script_analyzer_expression(
                a,
                script_analyzer_field(node, "value"),
                (script_type){ .flags = SCRIPT_TYPE_UNTYPED }
            );
        }
        for (uint32_t i = 0; i < script_analyzer_named_count(node); i++) {
            TSNode child = ts_node_named_child(node, i);
            if (script_analyzer_is(child, "match_arm") ||
                script_analyzer_is(child, "branch_statement")) {
                int arm = script_analyzer_statement(a, child);
                apush(result.children, arm);
                result.effects |= a->out->statements[arm].effects;
            }
        }
        result.effects |= script_analyzer_expr_effects(a, result.expression);
        return script_analyzer_statement_add(a, result);
    }
    if (script_analyzer_is(node, "match_arm") ||
        script_analyzer_is(node, "branch_statement")) {
        result.kind = SCRIPT_STMT_CASE;
        TSNode value = script_analyzer_field(node, "value");
        if (!ts_node_is_null(value)) {
            result.expression = script_analyzer_expression(a, value, (script_type){ .flags = SCRIPT_TYPE_UNTYPED });
        }
        result.body = script_analyzer_statement(a, script_analyzer_field(node, "body"));
        result.effects = script_analyzer_expr_effects(a, result.expression) |
                         a->out->statements[result.body].effects;
        return script_analyzer_statement_add(a, result);
    }
    script_analyzer_diagnostic(a, node, "Unsupported statement '%s'", ts_node_type(node));
    result.kind = SCRIPT_STMT_BLOCK;
    return script_analyzer_statement_add(a, result);
}
int script_analyzer_statement(analyzer *a, TSNode node)
{
    if (a->depth >= SCRIPT_MAX_DEPTH) {
        script_analyzer_diagnostic(a, node, "Statement nesting exceeds %d", SCRIPT_MAX_DEPTH);
        return script_analyzer_statement_add(
            a,
            (script_statement){ .kind = SCRIPT_STMT_BLOCK,
                                .range = script_analyzer_range(node),
                                .scope = a->scope,
                                .symbol = -1,
                                .expression = -1,
                                .second = -1,
                                .third = -1,
                                .body = -1,
                                .alternative = -1 }
        );
    }
    a->depth++;
    int result = script_analyzer_statement_inner(a, node);
    a->depth--;
    return result;
}
