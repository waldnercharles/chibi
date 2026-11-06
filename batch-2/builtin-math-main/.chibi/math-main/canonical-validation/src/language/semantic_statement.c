#include "language/semantic_internal.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <stdlib.h>
#include <string.h>

/* A pure native returns a new value, so a call statement drops all it did,
 * including when a named action invokes it. */
static bool discards_result(const analyzer *a, const script_expression *call)
{
    if (call->kind != SCRIPT_EXPR_CALL || call->right >= 0 || !call->result_count) {
        return false;
    }
    const script_catalog_capability
        *native = script_catalog_capability_by_id(a->catalog, call->capability);
    /* An array method that changes its array reports that itself. */
    return native && native->pure &&
           !(call->left >= 0 && script_analyzer_array_changer(native));
}

int script_analyzer_statement_add(analyzer *a, script_statement statement)
{
    statement.child_count = acount(statement.children);
    apush(a->out->statements, statement);
    return acount(a->out->statements) - 1;
}
static bool terminates(const script_analysis *a, int index)
{
    if (index < 0) {
        return false;
    }
    const script_statement *s = &a->statements[index];
    if (s->kind == SCRIPT_STMT_RETURN || s->kind == SCRIPT_STMT_BREAK ||
        s->kind == SCRIPT_STMT_CONTINUE) {
        return true;
    }
    if (s->kind == SCRIPT_STMT_BLOCK) {
        return s->child_count && terminates(a, s->children[s->child_count - 1]);
    }
    return s->kind == SCRIPT_STMT_IF && terminates(a, s->body) &&
           terminates(a, s->alternative);
}
/* Whether a break in this statement leaves the loop around it: one inside a
 * nested loop leaves that loop instead, and a switch passes it through. */
static bool breaks_out(const script_analysis *a, int index)
{
    if (index < 0) {
        return false;
    }
    const script_statement *s = &a->statements[index];
    if (s->kind == SCRIPT_STMT_BREAK) {
        return true;
    }
    if (s->kind == SCRIPT_STMT_WHILE || s->kind == SCRIPT_STMT_FOR ||
        s->kind == SCRIPT_STMT_FOREACH) {
        return false;
    }
    for (int i = 0; i < s->child_count; i++) {
        if (breaks_out(a, s->children[i])) {
            return true;
        }
    }
    return breaks_out(a, s->body) || breaks_out(a, s->alternative);
}
/* A while (true) that nothing breaks out of never falls through. */
static bool endless(const script_analysis *a, const script_statement *s)
{
    const script_expression *condition = s->expression >= 0
                                           ? &a->expressions[s->expression]
                                           : NULL;
    return s->kind == SCRIPT_STMT_WHILE && condition &&
           condition->kind == SCRIPT_EXPR_CONSTANT &&
           condition->constant.type == PIN_BOOL && condition->constant.b &&
           !breaks_out(a, s->body);
}
/* Go's terminating statement, which Odin's checker also requires at the end
 * of a procedure with results: control cannot fall past it. */
static bool ends(const script_analysis *a, int index)
{
    if (index < 0) {
        return false;
    }
    const script_statement *s = &a->statements[index];
    switch (s->kind) {
        case SCRIPT_STMT_RETURN:
        case SCRIPT_STMT_GOTO: return true;
        case SCRIPT_STMT_BLOCK:
            return s->child_count && ends(a, s->children[s->child_count - 1]);
        case SCRIPT_STMT_IF: return ends(a, s->body) && ends(a, s->alternative);
        case SCRIPT_STMT_WHILE: return endless(a, s);
        case SCRIPT_STMT_MATCH: {
            bool exhaustive = s->expression >= 0 &&
                              (a->expressions[s->expression].type.flags &
                               SCRIPT_TYPE_UNION);
            for (int i = 0; i < s->child_count; i++) {
                const script_statement *arm = &a->statements[s->children[i]];
                exhaustive |= arm->expression < 0;
                if (!ends(a, arm->body)) {
                    return false;
                }
            }
            return exhaustive;
        }
        default: return false;
    }
}
static bool reads_result(const script_analysis *a, int index, int symbol, int depth)
{
    if (index < 0 || depth == SCRIPT_MAX_DEPTH) {
        return false;
    }
    const script_expression *e = &a->expressions[index];
    if (e->kind == SCRIPT_EXPR_SYMBOL && e->symbol == symbol) {
        return true;
    }
    if (reads_result(a, e->left, symbol, depth + 1)) {
        return true;
    }
    if ((e->kind == SCRIPT_EXPR_BINARY || e->kind == SCRIPT_EXPR_INDEX ||
         e->kind == SCRIPT_EXPR_SEQUENCE) &&
        reads_result(a, e->right, symbol, depth + 1)) {
        return true;
    }
    for (int i = 0; i < e->argument_count; i++) {
        if (reads_result(a, e->arguments[i].value, symbol, depth + 1)) {
            return true;
        }
    }
    return false;
}
/* Track definite initialization of union results through each exit.  A null
 * physical slot is not a member of a nonoptional union. */
static bool union_result_paths(
    const script_analysis *a,
    int index,
    int symbol,
    bool initialized,
    bool *falls,
    bool *valid
)
{
    if (index < 0) {
        *falls = true;
        return initialized;
    }
    const script_statement *s = &a->statements[index];
    *falls = true;
    if (!initialized) {
        *valid &= !reads_result(a, s->second, symbol, 0) &&
                  !reads_result(a, s->third, symbol, 0);
        if (s->kind != SCRIPT_STMT_ASSIGN) {
            *valid &= !reads_result(a, s->expression, symbol, 0);
        }
        *valid &= s->kind != SCRIPT_STMT_GOTO;
    }
    if (s->kind == SCRIPT_STMT_ASSIGN && s->symbol == symbol) {
        return true;
    }
    if (s->kind == SCRIPT_STMT_BLOCK || s->kind == SCRIPT_STMT_RETURN) {
        for (int i = 0; i < s->child_count && *falls; i++) {
            initialized = union_result_paths(a, s->children[i], symbol, initialized, falls, valid);
        }
        if (s->kind == SCRIPT_STMT_RETURN) {
            *valid &= initialized;
            *falls = false;
        }
        return initialized;
    }
    if (s->kind == SCRIPT_STMT_IF) {
        bool left_falls, right_falls;
        bool left = union_result_paths(a, s->body, symbol, initialized, &left_falls, valid);
        bool right = union_result_paths(a, s->alternative, symbol, initialized, &right_falls, valid);
        *falls = left_falls || right_falls;
        return (!left_falls || left) && (!right_falls || right);
    }
    if (s->kind == SCRIPT_STMT_MATCH) {
        bool exhaustive = s->expression >= 0 &&
                          (a->expressions[s->expression].type.flags & SCRIPT_TYPE_UNION);
        bool any_falls = false, all_initialized = true;
        for (int i = 0; i < s->child_count; i++) {
            const script_statement *arm = &a->statements[s->children[i]];
            exhaustive |= arm->expression < 0;
            bool arm_falls;
            bool arm_initialized = union_result_paths(a, arm->body, symbol, initialized, &arm_falls, valid);
            any_falls |= arm_falls;
            all_initialized &= !arm_falls || arm_initialized;
        }
        *falls = any_falls || !exhaustive;
        return all_initialized && (exhaustive || initialized);
    }
    if (s->body >= 0) {
        bool body_falls;
        union_result_paths(a, s->body, symbol, initialized, &body_falls, valid);
    }
    *falls = !endless(a, s);
    return initialized;
}
/* Whether any return lies in this statement. */
static bool returns(const script_analysis *a, int index)
{
    if (index < 0) {
        return false;
    }
    const script_statement *s = &a->statements[index];
    if (s->kind == SCRIPT_STMT_RETURN) {
        return true;
    }
    for (int i = 0; i < s->child_count; i++) {
        if (returns(a, s->children[i])) {
            return true;
        }
    }
    return returns(a, s->body) || returns(a, s->alternative);
}
/* A function whose `after` handler or macro entry returns may give its
 * results there, after its body ends; `on` handlers cannot return them. */
static bool handles_actions(const analyzer *a, int function)
{
    for (int i = 0; i < acount(a->out->functions); i++) {
        const script_function *child = &a->out->functions[i];
        if (child->parent == function &&
            (child->kind == SCRIPT_FUNCTION_CONTINUATION ||
             child->kind == SCRIPT_FUNCTION_MACRO_ENTRY) &&
            returns(a->out, child->body)) {
            return true;
        }
    }
    return false;
}
void script_analyzer_union_results(analyzer *a, int function, TSNode body)
{
    const script_function *f = &a->out->functions[function];
    if (f->kind == SCRIPT_FUNCTION_AUTHORED && acount(f->results) &&
        !ends(a->out, f->body) && !handles_actions(a, function)) {
        script_analyzer_diagnostic(a, ts_node_child(body, ts_node_child_count(body) - 1), "Missing return");
        return;
    }
    for (int i = 0; i < acount(f->results); i++) {
        int symbol = f->results[i];
        const script_symbol *result = &a->out->symbols[symbol];
        bool required = result->type.container == PIN_CONTAINER_VIEW ||
                        ((result->type.storage == PIN_UNION ||
                          result->type.storage == PIN_POINTER) &&
                         result->type.container == PIN_CONTAINER_SINGLE);
        if (!required || (result->type.flags & SCRIPT_TYPE_OPTIONAL)) {
            continue;
        }
        bool falls, valid = true;
        bool initialized = union_result_paths(
            a->out,
            f->body,
            symbol,
            result->initializer >= 0,
            &falls,
            &valid
        );
        if (!valid || (falls && !initialized)) {
            script_analyzer_diagnostic(
                a,
                body,
                "%s result '%s' requires a value on every return path",
                result->type.container == PIN_CONTAINER_VIEW ? "View"
                : result->type.storage == PIN_POINTER        ? "Pointer"
                                                             : "Union",
                result->name
            );
        }
    }
}
void script_analyzer_narrow_condition(analyzer *a, int expression_index, bool truth)
{
    if (expression_index < 0) {
        return;
    }
    const script_expression *condition = &a->out->expressions[expression_index];
    if ((condition->kind == SCRIPT_EXPR_SYMBOL ||
         condition->kind == SCRIPT_EXPR_QUERY_FIELD) &&
        condition->symbol >= 0 && (condition->type.flags & SCRIPT_TYPE_OPTIONAL)) {
        apush(
            a->narrowed,
            ((narrowing){ .symbol = condition->symbol,
                          .present = truth,
                          .mutations = a->out->symbols[condition->symbol].mutations })
        );
        return;
    }
    if (condition->kind == SCRIPT_EXPR_IS) {
        const script_expression *source = &a->out->expressions[condition->left];
        if (source->kind != SCRIPT_EXPR_SYMBOL || source->symbol < 0) {
            return;
        }
        bool matches = truth != condition->negated;
        script_type narrowed = condition->tested;
        if (!matches) {
            const script_catalog_type
                *type = script_catalog_type_by_id(a->catalog, source->type.nominal);
            script_type *remaining = NULL;
            for (int i = 0; type && i < type->variant_count; i++) {
                bool tested = false;
                for (int j = 0; j < condition->pattern_tag_count; j++) {
                    tested |= type->variants[i].tag == condition->pattern_tags[j];
                }
                if (!tested) {
                    apush(remaining, type->variants[i].type);
                }
            }
            if (!acount(remaining)) {
                afree(remaining);
                return;
            }
            narrowed = acount(remaining) == 1
                         ? remaining[0]
                         : script_analyzer_union_type(a, (TSNode){ 0 }, remaining, acount(remaining));
            afree(remaining);
            if (source->type.flags & SCRIPT_TYPE_OPTIONAL) {
                narrowed.flags |= SCRIPT_TYPE_OPTIONAL | SCRIPT_TYPE_BOXED_OPTIONAL;
            }
        }
        apush(
            a->narrowed,
            ((narrowing){ .symbol = source->symbol,
                          .present = !(narrowed.flags & SCRIPT_TYPE_OPTIONAL),
                          .type = narrowed,
                          .typed = true,
                          .mutations = a->out->symbols[source->symbol].mutations })
        );
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
    const script_expression *condition = &a->out->expressions[value];
    bool narrowed_optional = (type.storage == PIN_POINTER || type.storage == PIN_STRUCT) &&
                             condition->symbol >= 0 &&
                             (a->out->symbols[condition->symbol].type.flags &
                              SCRIPT_TYPE_OPTIONAL);
    if (!narrowed_optional && !(type.flags & SCRIPT_TYPE_OPTIONAL) &&
        !((type.flags & SCRIPT_TYPE_TASK) && type.container == PIN_CONTAINER_SINGLE)) {
        script_analyzer_require(a, node, script_analyzer_boolean_type(), type);
    }
}
int script_analyzer_statement_block(analyzer *a, TSNode node)
{
    int old_scope = a->scope;
    a->scope = script_analyzer_scope_new(a, node);
    script_statement result = { .kind = SCRIPT_STMT_BLOCK,
                                .scope = a->scope,
                                .range = script_analyzer_range(a, node),
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
    int symbol = script_analyzer_import_namespace(a, call, name)
                   ? script_analyzer_package_function(a, call, name)
                   : script_analyzer_callable_symbol(a, name);
    int index = script_analyzer_find_capability(a, call, name, SCRIPT_CAPABILITY_FUNCTION);
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
static void task_hints(analyzer *a, TSNode body, TSNode node)
{
    TSNode value = { 0 };
    int symbol = -1;
    if (script_analyzer_is(node, "assignment") &&
        script_analyzer_is(script_analyzer_field(node, "operator"), "=")) {
        value = script_analyzer_field(node, "value");
        symbol = task_target(a, node);
    }
    while (script_analyzer_is(value, "parenthesized_expression")) {
        value = script_analyzer_field(value, "expression");
    }
    if (symbol >= 0 && (a->out->symbols[symbol].type.flags & SCRIPT_TYPE_TASK) &&
        script_analyzer_is(value, "call_expression") &&
        !nested_callee(a, body, node, value)) {
        script_type task = { .storage = PIN_TASK,
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
/* A literal settles to its number, so later returns convert to it. A result
 * holds one value, so a call with several results is unpacked. */
static void infer_type(analyzer *a, int symbol, int expression, TSNode node)
{
    script_type type = script_analyzer_expr_type(a, expression);
    type.flags &= ~(SCRIPT_TYPE_REFERENCE | SCRIPT_TYPE_UNTYPED | SCRIPT_TYPE_LITERAL);
    if (type.flags & SCRIPT_TYPE_RESULTS) {
        script_analyzer_diagnostic(
            a,
            node,
            "This call has several results; return the ones you need by name, "
            "as in "
            "return(Hit: r.Hit, Distance: r.Distance)"
        );
        type = script_analyzer_bad_type();
    }
    a->out->symbols[symbol].type = type;
}
/* Only stored member paths rooted in ECS storage can receive contributions. */
uint64_t script_analyzer_component_target(const script_analysis *out, int index)
{
    int remaining = acount(out->expressions);
    while (index >= 0 && remaining--) {
        const script_expression *e = &out->expressions[index];
        if (e->kind == SCRIPT_EXPR_MEMBER || e->kind == SCRIPT_EXPR_DEREFERENCE ||
            (e->kind == SCRIPT_EXPR_MATH && e->math_op == SCRIPT_MATH_SWIZZLE)) {
            if (e->read || e->write || e->type.container != PIN_CONTAINER_SINGLE) {
                return 0;
            }
            index = e->left;
            continue;
        }
        if (e->kind == SCRIPT_EXPR_COMPONENT) {
            return e->capability;
        }
        if (e->kind == SCRIPT_EXPR_SYMBOL && e->symbol >= 0) {
            const script_symbol *symbol = &out->symbols[e->symbol];
            if (symbol->kind == SCRIPT_COMPONENT) {
                return symbol->component_type.nominal;
            }
            if (symbol->kind == SCRIPT_LOCAL && symbol->type.storage == PIN_POINTER &&
                !symbol->mutations && symbol->initializer >= 0) {
                index = symbol->initializer;
                continue;
            }
        }
        return 0;
    }
    return 0;
}
/* Member/index stores replace owned local values. A path through a reference,
 * component or reflected setter writes storage outside that value instead. */
bool script_analyzer_writes_external(const script_analysis *out, int index)
{
    while (index >= 0) {
        const script_expression *e = &out->expressions[index];
        if ((e->type.flags & SCRIPT_TYPE_REFERENCE) || e->write ||
            e->kind == SCRIPT_EXPR_COMPONENT) {
            return true;
        }
        if (e->kind == SCRIPT_EXPR_MEMBER || e->kind == SCRIPT_EXPR_INDEX ||
            (e->kind == SCRIPT_EXPR_MATH && e->math_op == SCRIPT_MATH_SWIZZLE)) {
            index = e->left;
            continue;
        }
        if (e->kind != SCRIPT_EXPR_SYMBOL || e->symbol < 0) {
            return true;
        }
        const script_symbol *symbol = &out->symbols[e->symbol];
        if (symbol->type.flags & SCRIPT_TYPE_REFERENCE) {
            return true;
        }
        return symbol->kind != SCRIPT_LOCAL &&
               symbol->kind != SCRIPT_PARAMETER && symbol->kind != SCRIPT_RESULT;
    }
    return true;
}
static bool reduction_bounded(const analyzer *a, int index)
{
    while (index >= 0 && a->out->expressions[index].kind == SCRIPT_EXPR_MEMBER) {
        const script_expression *e = &a->out->expressions[index];
        const script_catalog_type
            *type = e->left >= 0 ? script_catalog_type_by_id(
                                       a->catalog,
                                       a->out->expressions[e->left].type.nominal
                                   )
                                 : NULL;
        for (int i = 0; type && i < type->field_count; i++) {
            if (type->fields[i].id == e->member && type->fields[i].range_contract) {
                return true;
            }
        }
        index = e->left;
    }
    return false;
}
/* `delete m[k]` and `m.delete(k)` assign the map without the key, which
 * lowering does in place for a local. */
static int delete_key(analyzer *a, TSNode node, script_statement result, TSNode map, TSNode key)
{
    result.kind = SCRIPT_STMT_ASSIGN;
    if (script_analyzer_is(map, "identifier")) {
        int symbol = script_analyzer_lookup(a, map);
        if (symbol >= 0) {
            a->out->symbols[symbol].mutations++;
            script_analyzer_invalidate_narrowing(a, symbol);
        }
    }
    result.expression = script_analyzer_expression(a, map, (script_type){ .flags = SCRIPT_TYPE_UNTYPED });
    script_expression address = a->out->expressions[result.expression];
    if (!address.writable) {
        script_analyzer_diagnostic(a, map, "Deleting a key requires a writable map");
    }
    result.second = script_analyzer_map_operation(
        a,
        node,
        SCRIPT_EXPR_MAP_REMOVE,
        result.expression,
        key
    );
    result.effects = script_analyzer_expr_effects(a, result.second);
    if (script_analyzer_writes_external(a->out, result.expression)) {
        result.effects |= SCRIPT_EFFECT_WRITE;
    }
    if (address.symbol >= 0) {
        script_analyzer_use(a, map, address.symbol, SCRIPT_WRITE, address.member);
    }
    return script_analyzer_statement_add(a, result);
}
int script_analyzer_statement_inner(analyzer *a, TSNode node)
{
    if (script_analyzer_is(node, "block")) {
        return script_analyzer_statement_block(a, node);
    }
    script_statement result = { .scope = a->scope,
                                .range = script_analyzer_range(a, node),
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
    if (script_analyzer_is(node, "constant_declaration")) {
        script_analyzer_declare_constant(a, node);
        int symbol = script_analyzer_lookup(a, script_analyzer_field(node, "name"));
        script_analyzer_resolve_initializer(a, symbol);
        result.kind = SCRIPT_STMT_BLOCK;
        return script_analyzer_statement_add(a, result);
    }
    if (script_analyzer_is(node, "local_declaration")) {
        result.kind = SCRIPT_STMT_DECLARE;
        script_type type = script_analyzer_type_node(a, script_analyzer_field(node, "type"));
        result.expression = script_analyzer_expression(a, script_analyzer_field(node, "value"), type);
        if (type.flags & SCRIPT_TYPE_UNTYPED) {
            /* A local copies what a scoped parameter or a reference item
             * names, so it is not a reference itself. */
            type = script_analyzer_expr_type(a, result.expression);
            type.flags &= ~(SCRIPT_TYPE_UNTYPED | SCRIPT_TYPE_LITERAL | SCRIPT_TYPE_REFERENCE);
        } else {
            script_analyzer_require(
                a,
                node,
                type,
                script_analyzer_expr_type(a, result.expression)
            );
        }
        int existing = script_analyzer_lookup(a, script_analyzer_field(node, "name"));
        script_range name_range = script_analyzer_range(a, script_analyzer_field(node, "name"));
        if (existing >= 0 && a->out->symbols[existing].kind == SCRIPT_LOCAL &&
            a->out->symbols[existing].scope == a->scope &&
            a->out->symbols[existing].declaration.document == name_range.document &&
            a->out->symbols[existing].declaration.start == name_range.start) {
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
    if (script_analyzer_is(node, "reduce_statement")) {
        result.kind = SCRIPT_STMT_REDUCE;
        if (a->function < 0 ||
            a->out->functions[a->function].kind != SCRIPT_FUNCTION_SYSTEM) {
            script_analyzer_diagnostic(a, node, "Reduction requires a synchronous system body");
        }
        TSNode target = script_analyzer_field(node, "target");
        result.expression = script_analyzer_expression(a, target, (script_type){ .flags = SCRIPT_TYPE_UNTYPED });
        script_expression address = a->out->expressions[result.expression];
        if (!address.writable || address.kind != SCRIPT_EXPR_MEMBER ||
            !script_type_numeric(address.type) ||
            !script_analyzer_component_target(a->out, result.expression)) {
            script_analyzer_diagnostic(a, target, "Reduction requires a writable scalar numeric component field");
        }
        result.second = script_analyzer_expression(
            a,
            script_analyzer_field(node, "value"),
            address.type
        );
        script_analyzer_require_values(a, node, result.expression, result.second);
        TSNode op = script_analyzer_field(node, "operator");
        result.reduction = script_analyzer_text_is(a, op, "min") ? SCRIPT_REDUCTION_MIN
                         : script_analyzer_text_is(a, op, "max")
                             ? SCRIPT_REDUCTION_MAX
                             : SCRIPT_REDUCTION_ADD;
        if (!ts_node_is_null(script_analyzer_field(node, "ordering"))) {
            result.reduction = SCRIPT_REDUCTION_ADD_UNORDERED;
            if (reduction_bounded(a, result.expression)) {
                script_analyzer_diagnostic(a, target, "Unordered reduction requires an unbounded component field");
            }
        }
        result.effects = address.effects |
                         script_analyzer_expr_effects(a, result.second) |
                         SCRIPT_EFFECT_WRITE;
        script_analyzer_use(a, target, address.symbol, SCRIPT_WRITE, address.member);
        return script_analyzer_statement_add(a, result);
    }
    if (script_analyzer_is(node, "assignment")) {
        result.kind = SCRIPT_STMT_ASSIGN;
        TSNode target = script_analyzer_field(node, "target");
        if (script_analyzer_is(target, "identifier")) {
            int symbol = script_analyzer_lookup(a, target);
            if (symbol >= 0) {
                a->out->symbols[symbol].mutations++;
                script_analyzer_invalidate_narrowing(a, symbol);
            }
        }
        result.expression = script_analyzer_expression(a, target, (script_type){ .flags = SCRIPT_TYPE_UNTYPED });
        script_expression address = a->out->expressions[result.expression];
        (void)script_analyzer_math_write(a, target, &address);
        result.op = script_analyzer_operator_node(a, script_analyzer_field(node, "operator"));
        bool compound_math = result.op != SCRIPT_OP_NONE &&
                             script_math_of(a->catalog, address.type) >= SCRIPT_MATH_VEC2;
        if (!address.writable) {
            script_analyzer_diagnostic(a, target, "Assignment requires a writable value");
        }
        result.second = script_analyzer_expression(
            a,
            script_analyzer_field(node, "value"),
            address.type
        );
        if (!compound_math) {
            script_analyzer_require_values(a, node, result.expression, result.second);
        }
        if (address.kind == SCRIPT_EXPR_MEMBER && address.left >= 0) {
            script_type owner = script_analyzer_expr_type(a, address.left);
            const script_catalog_type
                *record = script_catalog_type_by_id(a->catalog, owner.nominal);
            for (int i = 0; record && i < record->field_count; i++) {
                const script_catalog_field *field = &record->fields[i];
                if (field->id == address.member && field->c_string) {
                    script_analyzer_c_string_argument(
                        a,
                        script_analyzer_field(node, "value"),
                        result.second,
                        field->name
                    );
                }
            }
        }
        result.op = script_analyzer_operator_node(a, script_analyzer_field(node, "operator"));
        script_type value = script_analyzer_expr_type(a, result.second);
        if (result.op != SCRIPT_OP_NONE && script_type_numeric(address.type) &&
            script_type_numeric(value) && !(value.flags & SCRIPT_TYPE_UNTYPED) &&
            address.type.storage >= PIN_I8 && address.type.storage <= PIN_U64 &&
            value.storage >= PIN_I8 && value.storage <= PIN_U64 &&
            !script_analyzer_integers_match(address.type, value)) {
            script_analyzer_mismatched_integers(a, node, address.type, value);
        }
        if (result.op != SCRIPT_OP_NONE && !script_type_numeric(address.type)) {
            script_expression operation = {
                .kind = SCRIPT_EXPR_BINARY,
                .op = result.op,
                .left = result.expression,
                .right = result.second,
                .symbol = -1,
                .range = script_analyzer_range(a, node),
                .type = script_analyzer_bad_type(),
                .effects = address.effects |
                           script_analyzer_expr_effects(a, result.second)
            };
            if (script_analyzer_math_operator(a, node, &operation) ||
                script_analyzer_resolve_operator(a, node, script_analyzer_field(node, "operator"), &operation)) {
                script_analyzer_require(a, node, address.type, operation.type);
                result.second = script_analyzer_expr_add(a, operation);
                result.op = SCRIPT_OP_NONE;
            } else {
                script_analyzer_diagnostic(a, node, "Compound assignment requires a compatible operator");
            }
        }
        result.effects = address.effects |
                         script_analyzer_expr_effects(a, result.second);
        if (script_analyzer_writes_external(a->out, result.expression)) {
            result.effects |= SCRIPT_EFFECT_WRITE;
            script_analyzer_invalidate_pointers(a);
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
    if (script_analyzer_is(node, "delete_statement")) {
        TSNode target = script_analyzer_field(node, "target");
        return delete_key(
            a,
            node,
            result,
            script_analyzer_field(target, "object"),
            script_analyzer_field(target, "index")
        );
    }
    if (script_analyzer_is(node, "expression_statement")) {
        result.kind = SCRIPT_STMT_EXPRESSION;
        TSNode value = { 0 };
        for (uint32_t i = 0; i < script_analyzer_named_count(node); i++) {
            value = ts_node_named_child(node, i);
            TSNode function = script_analyzer_field(value, "function");
            if (script_analyzer_is(value, "call_expression") &&
                script_analyzer_map_method(a, function, "delete")) {
                TSNode arguments = script_analyzer_field(value, "arguments");
                if (script_analyzer_named_count(arguments) != 1) {
                    script_analyzer_diagnostic(a, value, "Map method requires one key");
                }
                return delete_key(
                    a,
                    value,
                    result,
                    script_analyzer_field(function, "object"),
                    ts_node_named_child(arguments, 0)
                );
            }
            if (!ts_node_is_extra(value)) {
                TSNode changing = a->changing;
                a->changing = value;
                result.expression = script_analyzer_expression(
                    a,
                    value,
                    (script_type){ .storage = PIN_FLOW }
                );
                a->changing = changing;
                break;
            }
        }
        if (result.expression >= 0 &&
            a->out->expressions[result.expression].kind != SCRIPT_EXPR_CALL &&
            a->out->expressions[result.expression].kind != SCRIPT_EXPR_ARRAY_MUTATE &&
            a->out->expressions[result.expression].kind != SCRIPT_EXPR_JOIN) {
            script_analyzer_diagnostic(a, node, "Expression statements must invoke a callable");
        }
        result.effects = script_analyzer_expr_effects(a, result.expression);
        if (result.expression >= 0 &&
            discards_result(a, &a->out->expressions[result.expression])) {
            char *name = script_analyzer_text_copy(a, script_analyzer_field(value, "function"));
            script_analyzer_diagnostic(a, node, "%s returns a new value and changes nothing; assign its result", name);
            free(name);
        }
        return script_analyzer_statement_add(a, result);
    }
    if (script_analyzer_is(node, "if_statement")) {
        int old_scope = a->scope;
        a->scope = script_analyzer_scope_new(a, node);
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
            a->scope = old_scope;
            script_analyzer_narrow_condition(a, result.expression, false);
            result.alternative = script_analyzer_statement(a, alternative);
        }
        result.effects = script_analyzer_expr_effects(a, result.expression) |
                         a->out->statements[result.body].effects |
                         (result.alternative >= 0
                              ? a->out->statements[result.alternative].effects
                              : 0);
        asetlen(a->narrowed, 0);
        if (!(result.effects & (SCRIPT_EFFECT_SUSPEND | SCRIPT_EFFECT_STRUCTURAL |
                                SCRIPT_EFFECT_WRITE))) {
            for (int i = 0; i < acount(old); i++) {
                apush(a->narrowed, old[i]);
            }
        }
        a->scope = old_scope;
        if (!(result.effects & (SCRIPT_EFFECT_SUSPEND | SCRIPT_EFFECT_STRUCTURAL |
                                SCRIPT_EFFECT_WRITE))) {
            if (terminates(a->out, result.body)) {
                script_analyzer_narrow_condition(a, result.expression, false);
            } else if (terminates(a->out, result.alternative)) {
                script_analyzer_narrow_condition(a, result.expression, true);
            }
        }
        afree(old);
        return script_analyzer_statement_add(a, result);
    }
    if (script_analyzer_is(node, "while_statement")) {
        int old_scope = a->scope;
        a->scope = script_analyzer_scope_new(a, node);
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
        a->scope = old_scope;
        return script_analyzer_statement_add(a, result);
    }
    if (script_analyzer_is(node, "for_statement")) {
        int old_scope = a->scope;
        a->scope = script_analyzer_scope_new(a, node);
        result.scope = a->scope;
        TSNode first = script_analyzer_field(node, "first"),
               collection = script_analyzer_field(node, "value");
        bool range = !ts_node_is_null(first);
        result.kind = range ? SCRIPT_STMT_FOR : SCRIPT_STMT_FOREACH;
        result.key_symbol = -1;
        TSNode item_name = script_analyzer_field(node, "name");
        bool reference_iteration = !ts_node_is_null(script_analyzer_field(node, "reference"));
        result.reverse = !ts_node_is_null(script_analyzer_field(node, "reverse"));
        script_type element;
        if (range) {
            result.exclusive = script_analyzer_text_is(a, script_analyzer_field(node, "bound"), "..<");
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
            /* Ranges never count down, so constant bounds that descend are a
             * mistake rather than an empty loop. */
            pin_value low, high;
            if (!script_analyzer_scalar_constant(a, result.expression, PIN_I32, &low) &&
                !script_analyzer_scalar_constant(a, result.second, PIN_I32, &high) &&
                low.i > high.i) {
                script_analyzer_diagnostic(
                    a,
                    node,
                    result.reverse
                        ? "Range bounds descend; for < takes the low bound "
                          "first, as in for < i in 0..10"
                        : "Range bounds descend; count down with for <"
                );
            }
        } else {
            result.expression = script_analyzer_expression(
                a,
                collection,
                (script_type){ .flags = SCRIPT_TYPE_UNTYPED }
            );
            element = script_analyzer_expr_type(a, result.expression);
            bool query = (element.flags & SCRIPT_TYPE_QUERY) != 0;
            bool text = element.container == PIN_CONTAINER_SINGLE &&
                        element.storage == PIN_STR &&
                        !(element.flags & SCRIPT_TYPE_OPTIONAL);
            if (element.container != PIN_CONTAINER_ARRAY &&
                element.container != PIN_CONTAINER_VIEW &&
                element.container != PIN_CONTAINER_MAP && !text && !query) {
                script_analyzer_diagnostic(a, collection, "For requires an array, Map, string or query");
            }
            if (result.reverse && ((element.container != PIN_CONTAINER_ARRAY &&
                                    element.container != PIN_CONTAINER_VIEW) ||
                                   a->out->expressions[result.expression].kind ==
                                       SCRIPT_EXPR_RELATION)) {
                script_analyzer_diagnostic(a, collection, "for < reverses only ranges and arrays");
            }
            TSNode key = script_analyzer_field(node, "key");
            if (!ts_node_is_null(key)) {
                if (text || query) {
                    script_analyzer_diagnostic(a, key, "This iterator binds one value at a time");
                }
                if (element.container == PIN_CONTAINER_ARRAY ||
                    element.container == PIN_CONTAINER_VIEW) {
                    /* Arrays bind item,index; Maps bind key,value. */
                    result.key_symbol = script_analyzer_symbol_new(
                        a,
                        item_name,
                        SCRIPT_LOCAL,
                        (script_type){ .storage = PIN_I32 },
                        false
                    );
                    item_name = key;
                } else {
                    result.key_symbol = script_analyzer_symbol_new(
                        a,
                        key,
                        SCRIPT_LOCAL,
                        script_catalog_key_type(a->catalog, element),
                        false
                    );
                }
            }
            element = text  ? (script_type){ .storage = PIN_STR }
                    : query ? (script_type){ .storage = PIN_QUERY,
                                             .nominal = element.nominal,
                                             .flags = SCRIPT_TYPE_QUERY_ROW }
                            : script_catalog_element_type(a->catalog, element);
            if (reference_iteration) {
                const script_expression *source = &a->out->expressions[result.expression];
                const script_symbol *binding = source->kind == SCRIPT_EXPR_SYMBOL &&
                                                       source->symbol >= 0
                                                 ? &a->out->symbols[source->symbol]
                                                 : NULL;
                if (source->type.container != PIN_CONTAINER_ARRAY || !binding ||
                    !binding->writable ||
                    (binding->type.flags & SCRIPT_TYPE_REFERENCE) ||
                    (binding->kind != SCRIPT_LOCAL && binding->kind != SCRIPT_STATE &&
                     binding->kind != SCRIPT_SHARED_STATE) ||
                    result.key_symbol >= 0) {
                    script_analyzer_diagnostic(a, collection, "Reference iteration requires one item and a mutable owned array binding");
                }
                element.flags |= SCRIPT_TYPE_REFERENCE;
            }
        }
        result.symbol = script_analyzer_symbol_new(a, item_name, SCRIPT_LOCAL, element, reference_iteration);
        a->loops++;
        result.body = script_analyzer_statement(a, script_analyzer_field(node, "body"));
        a->loops--;
        result.effects = script_analyzer_expr_effects(a, result.expression) |
                         script_analyzer_expr_effects(a, result.second) |
                         a->out->statements[result.body].effects;
        script_type source_type = script_analyzer_expr_type(a, result.expression);
        if (source_type.container == PIN_CONTAINER_VIEW) {
            result.effects |= SCRIPT_EFFECT_READ;
        }
        a->scope = old_scope;
        return script_analyzer_statement_add(a, result);
    }
    if (script_analyzer_is(node, "yield_statement") ||
        script_analyzer_is(node, "cancel_statement")) {
        bool yield = script_analyzer_is(node, "yield_statement");
        script_expression task = { .kind = yield ? SCRIPT_EXPR_YIELD : SCRIPT_EXPR_CANCEL,
                                   .type = { .storage = PIN_FLOW },
                                   .range = script_analyzer_range(a, node),
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
                (script_type){ .storage = PIN_TASK, .flags = SCRIPT_TYPE_TASK }
            );
            script_type type = script_analyzer_expr_type(a, task.left);
            if (!(type.flags & SCRIPT_TYPE_TASK) ||
                type.container != PIN_CONTAINER_SINGLE ||
                (type.flags & SCRIPT_TYPE_OPTIONAL)) {
                script_analyzer_diagnostic(a, value, "Cancel requires a task");
            }
            task.effects |= script_analyzer_expr_effects(a, task.left);
            TSNode duration = script_analyzer_field(node, "duration");
            if (!ts_node_is_null(duration)) {
                task.right = script_analyzer_expression(a, duration, (script_type){ .storage = PIN_F32 });
                script_type type = script_analyzer_expr_type(a, task.right);
                if (!script_type_numeric(type)) {
                    script_analyzer_diagnostic(a, duration, "Cancel duration requires a number");
                }
                task.effects |= script_analyzer_expr_effects(a, task.right);
            }
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
                    infer_type(a, function->results[parameter], expr, value);
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
                    .range = script_analyzer_range(a, child),
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
    if (script_analyzer_is(node, "switch_statement")) {
        result.kind = SCRIPT_STMT_MATCH;
        result.expression = script_analyzer_expression(
            a,
            script_analyzer_field(node, "value"),
            (script_type){ .flags = SCRIPT_TYPE_UNTYPED }
        );
        script_type source = script_analyzer_expr_type(a, result.expression);
        if ((source.flags & (SCRIPT_TYPE_UNION | SCRIPT_TYPE_OPTIONAL)) ==
            (SCRIPT_TYPE_UNION | SCRIPT_TYPE_OPTIONAL)) {
            script_analyzer_diagnostic(a, node, "Optional union requires a presence guard before switching");
        }
        const script_catalog_type
            *union_type = source.flags & SCRIPT_TYPE_UNION
                            ? script_catalog_type_by_id(a->catalog, source.nominal)
                            : NULL;
        script_catalog_type definition = union_type ? *union_type
                                                    : (script_catalog_type){ 0 };
        if (union_type && source.parameter) {
            for (int i = 0; i < definition.variant_count; i++) {
                if (definition.variants[i].tag == source.parameter) {
                    definition.variants = &definition.variants[i];
                    definition.variant_count = 1;
                    break;
                }
            }
        }
        union_type = union_type ? &definition : NULL;
        bool *covered = xcalloc((size_t)(union_type ? union_type->variant_count : 0) + 1, sizeof *covered);
        bool fallback = false;
        int old_scope = a->scope;
        narrowing *old = NULL;
        for (int i = 0; i < acount(a->narrowed); i++) {
            apush(old, a->narrowed[i]);
        }
        for (uint32_t i = 0; i < script_analyzer_named_count(node); i++) {
            TSNode arm_node = ts_node_named_child(node, i);
            if (!script_analyzer_is(arm_node, "switch_arm")) {
                continue;
            }
            a->scope = old_scope;
            a->scope = script_analyzer_scope_new(a, arm_node);
            asetlen(a->narrowed, 0);
            for (int j = 0; j < acount(old); j++) {
                apush(a->narrowed, old[j]);
            }
            script_statement arm = { .kind = SCRIPT_STMT_CASE,
                                     .range = script_analyzer_range(a, arm_node),
                                     .scope = a->scope,
                                     .symbol = -1,
                                     .expression = -1,
                                     .second = -1,
                                     .third = -1,
                                     .body = -1,
                                     .alternative = -1 };
            TSNode pattern = script_analyzer_field(arm_node, "value");
            if (ts_node_is_null(pattern)) {
                if (fallback) {
                    script_analyzer_diagnostic(a, arm_node, "Duplicate default arm");
                }
                fallback = true;
                const script_expression *value = &a->out->expressions[result.expression];
                if (union_type && value->kind == SCRIPT_EXPR_SYMBOL &&
                    value->symbol >= 0) {
                    int symbol = value->symbol;
                    script_type *remaining = NULL;
                    for (int j = 0; j < union_type->variant_count; j++) {
                        if (!covered[j]) {
                            apush(remaining, union_type->variants[j].type);
                        }
                    }
                    if (acount(remaining)) {
                        script_type narrowed = acount(remaining) == 1
                                                 ? remaining[0]
                                                 : script_analyzer_union_type(
                                                       a,
                                                       pattern,
                                                       remaining,
                                                       acount(remaining)
                                                   );
                        apush(
                            a->narrowed,
                            ((narrowing){ .symbol = symbol,
                                          .present = true,
                                          .typed = true,
                                          .type = narrowed,
                                          .mutations = a->out->symbols[symbol].mutations })
                        );
                    }
                    afree(remaining);
                }
            } else if (union_type) {
                arm.expression = script_analyzer_pattern(a, pattern, result.expression);
                uint64_t tag = a->out->expressions[arm.expression].tag;
                if (!tag) {
                    script_analyzer_diagnostic(a, pattern, "Pattern cannot match this union");
                }
                for (int j = 0; j < union_type->variant_count; j++) {
                    bool matching = false;
                    const script_expression *test = &a->out->expressions[arm.expression];
                    for (int k = 0; k < test->pattern_tag_count; k++) {
                        matching |= union_type->variants[j].tag ==
                                    test->pattern_tags[k];
                    }
                    if (matching) {
                        if (covered[j] || fallback) {
                            script_analyzer_diagnostic(a, pattern, "Unreachable duplicate union pattern");
                        }
                        covered[j] = true;
                    }
                }
                script_analyzer_narrow_condition(a, arm.expression, true);
            } else {
                arm.expression = script_analyzer_expression(a, pattern, source);
                script_analyzer_require(a, pattern, source, script_analyzer_expr_type(a, arm.expression));
            }
            arm.body = script_analyzer_statement(a, script_analyzer_field(arm_node, "body"));
            arm.effects = script_analyzer_expr_effects(a, arm.expression) |
                          a->out->statements[arm.body].effects;
            result.effects |= arm.effects;
            apush(result.children, script_analyzer_statement_add(a, arm));
        }
        if (union_type && !fallback) {
            for (int i = 0; i < union_type->variant_count; i++) {
                if (!covered[i]) {
                    script_analyzer_diagnostic(a, node, "Non-exhaustive switch: a union alternative is missing");
                    break;
                }
            }
        }
        free(covered);
        a->scope = old_scope;
        asetlen(a->narrowed, 0);
        if (!(result.effects & (SCRIPT_EFFECT_SUSPEND | SCRIPT_EFFECT_STRUCTURAL |
                                SCRIPT_EFFECT_WRITE))) {
            for (int i = 0; i < acount(old); i++) {
                apush(a->narrowed, old[i]);
            }
        }
        afree(old);
        result.effects |= script_analyzer_expr_effects(a, result.expression);
        return script_analyzer_statement_add(a, result);
    }
    if (script_analyzer_is(node, "sequence_statement")) {
        result.kind = SCRIPT_STMT_SEQUENCE;
        for (uint32_t i = 0; i < script_analyzer_named_count(node); i++) {
            TSNode child = ts_node_named_child(node, i);
            if (script_analyzer_is(child, "branch_statement")) {
                int arm = script_analyzer_statement(a, child);
                apush(result.children, arm);
                result.effects |= a->out->statements[arm].effects;
            }
        }
        result.effects |= script_analyzer_expr_effects(a, result.expression);
        return script_analyzer_statement_add(a, result);
    }
    if (script_analyzer_is(node, "branch_statement")) {
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
                                .range = script_analyzer_range(a, node),
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
