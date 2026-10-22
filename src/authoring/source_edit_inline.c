#include "authoring/source_edit_refactor_internal.h"
#include "authoring/source_literal.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void patches_free(text_edit *patches)
{
    for (int i = 0; i < acount(patches); i++) {
        free((char *)patches[i].text);
    }
    afree(patches);
}
static bool owned_symbol(const script_analysis *a, int function, int symbol)
{
    if (source_refactor_contains(
            a->functions[function].range,
            a->symbols[symbol].declaration
        )) {
        return true;
    }
    for (int owner = a->symbols[symbol].function; owner >= 0;
         owner = a->functions[owner].parent) {
        if (owner == function) {
            return true;
        }
    }
    return false;
}
static bool identifier_name(const char *name)
{
    static const char prefix[] = "Inline :: behavior { value := ";
    size_t length = strlen(name);
    char *text = xmalloc(sizeof prefix + length + 4);
    snprintf(text, sizeof prefix + length + 4, "%s%s; }", prefix, name);
    script_syntax *syntax = script_parse(text);
    TSNode expression = ts_node_named_descendant_for_byte_range(
        ts_tree_root_node(syntax->tree),
        sizeof prefix - 1,
        (uint32_t)(sizeof prefix - 1 + length)
    );
    bool valid = !syntax->diagnostic_count &&
                 !strcmp(ts_node_type(expression), "identifier");
    script_syntax_free(syntax);
    free(text);
    return valid;
}
static char *inline_name(const script_analysis *a, int function, const char *base, char **names)
{
    size_t length = strlen(base);
    char *identifier = xmalloc(length + 3);
    int count = 0;
    if (!length || !((base[0] >= 'a' && base[0] <= 'z') ||
                     (base[0] >= 'A' && base[0] <= 'Z') || base[0] == '_')) {
        identifier[count++] = '_';
    }
    for (size_t i = 0; i < length; i++) {
        char c = base[i];
        identifier[count++] = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                                      (c >= '0' && c <= '9') || c == '_'
                                ? c
                                : '_';
    }
    identifier[count] = '\0';
    if (!identifier_name(identifier)) {
        memmove(identifier + 1, identifier, (size_t)count + 1);
        identifier[0] = '_';
    }
    size_t size = strlen(identifier) + 32;
    char *name = xmalloc(size);
    for (int ordinal = 1;; ordinal++) {
        if (ordinal == 1) {
            snprintf(name, size, "%s", identifier);
        } else {
            snprintf(name, size, "%s_%d", identifier, ordinal);
        }
        bool collision = false;
        for (int i = 0; i < a->symbol_count; i++) {
            collision |= (!owned_symbol(a, function, i) &&
                          !strcmp(a->symbols[i].name, name)) ||
                         (names[i] && !strcmp(names[i], name));
        }
        if (!collision) {
            free(identifier);
            return name;
        }
    }
}
static bool retained_annotations(const authoring_source_presentation *p, script_range call, script_range definition)
{
    for (int i = 0; i < authoring_source_presentation_origin_count(p); i++) {
        const source_presentation_origin *o = authoring_source_presentation_origin_at(p, i);
        if (!o->node || o->pin ||
            (!source_refactor_contains(call, o->range) &&
             !source_refactor_contains(definition, o->range))) {
            continue;
        }
        const graph *g = behavior_asset_find_graph(authoring_source_presentation_view(p), o->graph);
        const node *n = graph_node_by_id(g, o->node);
        if (n->breakpoint || (n->comment && n->comment[0])) {
            return true;
        }
        for (int output = 0; output < 2; output++) {
            const pin_spec *pins = output ? n->output_pins : n->input_pins;
            int count = output ? n->output_count : n->input_count;
            for (int j = 0; j < count; j++) {
                if (pins[j].flags & PIN_FLAG_WATCH) {
                    return true;
                }
            }
        }
    }
    return false;
}
static char *renamed(const script_analysis *a, script_range range, char **names)
{
    text_edit *patches = NULL;
    for (int i = 0; i < a->expression_count; i++) {
        const script_expression *e = &a->expressions[i];
        if (e->kind == SCRIPT_EXPR_SYMBOL && e->symbol >= 0 &&
            names[e->symbol] && source_refactor_contains(range, e->range)) {
            apush(
                patches,
                ((text_edit){ e->range.start - range.start,
                              e->range.end - range.start,
                              xstrdup(names[e->symbol]) })
            );
        }
    }
    for (int i = 0; i < a->symbol_count; i++) {
        if (names[i] && source_refactor_contains(range, a->symbols[i].declaration)) {
            script_range r = a->symbols[i].declaration;
            apush(
                patches,
                ((text_edit){ r.start - range.start, r.end - range.start, xstrdup(names[i]) })
            );
        }
    }
    for (int i = 0; i < a->use_count; i++) {
        const script_use *use = &a->uses[i];
        if (use->symbol >= 0 && names[use->symbol] &&
            source_refactor_contains(range, use->range)) {
            bool duplicate = false;
            for (int j = 0; j < acount(patches); j++) {
                duplicate |= patches[j].start <= use->range.start - range.start &&
                             patches[j].end >= use->range.end - range.start;
            }
            if (!duplicate) {
                apush(
                    patches,
                    ((text_edit){ use->range.start - range.start,
                                  use->range.end - range.start,
                                  xstrdup(names[use->symbol]) })
                );
            }
        }
    }
    char *old = source_refactor_slice(a->text, range);
    char *text = text_edits_apply(old, patches, acount(patches));
    free(old);
    patches_free(patches);
    return text;
}
bool source_edit_inline(
    const script_analysis *a,
    const script_catalog *catalog,
    const authoring_source_presentation *p,
    const authoring_edit_request *request,
    source_edit_changes *changes,
    char *error,
    size_t size
)
{
    const source_presentation_origin *origin = NULL;
    for (int i = 0; i < authoring_source_presentation_origin_count(p); i++) {
        const source_presentation_origin *o = authoring_source_presentation_origin_at(p, i);
        if (o->identity == request->origin) {
            origin = o;
            break;
        }
    }
    int call_index = -1;
    if (origin && origin->kind == SOURCE_PRESENTATION_STATEMENT &&
        origin->record >= 0 && origin->record < a->statement_count) {
        const script_statement *statement = &a->statements[origin->record];
        int value = statement->kind == SCRIPT_STMT_ASSIGN ? statement->second
                                                          : statement->expression;
        if (value >= 0 && a->expressions[value].kind == SCRIPT_EXPR_CALL) {
            call_index = value;
        }
    }
    if (origin && origin->kind == SOURCE_PRESENTATION_SYMBOL &&
        origin->record >= 0 && origin->record < a->symbol_count) {
        int value = a->symbols[origin->record].initializer;
        if (value >= 0 && a->expressions[value].kind == SCRIPT_EXPR_CALL) {
            call_index = value;
        }
    }
    for (int i = 0; origin && i < a->expression_count; i++) {
        const script_expression *e = &a->expressions[i];
        if (e->kind == SCRIPT_EXPR_CALL && e->range.start == origin->range.start &&
            e->range.end == origin->range.end) {
            call_index = i;
        }
    }
    if (call_index < 0) {
        return source_refactor_fail(error, size, "Select a complete authored call expression");
    }
    const script_expression *call = &a->expressions[call_index];
    if (call->right < 0 || call->right >= a->function_count) {
        return source_refactor_fail(error, size, "Inlining requires the owning authored source definition");
    }
    const script_function *function = &a->functions[call->right];
    bool macro = function->symbol >= 0 &&
                 a->symbols[function->symbol].kind == SCRIPT_MACRO;
    if (function->kind != SCRIPT_FUNCTION_AUTHORED ||
        (a->symbols[function->symbol].kind != SCRIPT_FUNCTION && !macro)) {
        return source_refactor_fail(error, size, "Select a concrete authored callable");
    }
    if (call->instance) {
        return source_refactor_fail(error, size, "Inlining must retain the declared action's result and outcome owner");
    }
    int selected_entry = -1;
    if (macro) {
        for (int i = 0; i < a->symbol_count; i++) {
            if (a->symbols[i].outcome &&
                source_refactor_contains(function->range, a->symbols[i].declaration) &&
                strcmp(a->symbols[i].name, "Then")) {
                return source_refactor_fail(error, size, "Inline must retain the macro's selected outcome routing");
            }
        }
        for (int i = 0; i < a->function_count; i++) {
            const script_function *entry = &a->functions[i];
            if (entry->parent == call->right &&
                entry->kind == SCRIPT_FUNCTION_MACRO_ENTRY && entry->entry &&
                !strcmp(entry->entry, "entry")) {
                selected_entry = i;
            }
        }
        if (selected_entry < 0) {
            return source_refactor_fail(error, size, "Macro has no canonical default entry");
        }
    }
    for (int i = 0; i < a->use_count; i++) {
        if (a->uses[i].kind == SCRIPT_CALL && a->uses[i].symbol == function->symbol &&
            source_refactor_contains(function->range, a->uses[i].range)) {
            return source_refactor_fail(error, size, "Inline must retain recursive callable depth limits");
        }
    }
    for (int i = 0; i < a->function_count; i++) {
        if (a->functions[i].parent == call->right &&
            (!macro || a->functions[i].kind != SCRIPT_FUNCTION_MACRO_ENTRY)) {
            return source_refactor_fail(error, size, "Inlining must retain callable-owned action continuations");
        }
    }
    for (int i = 0; i < a->symbol_count; i++) {
        if (source_refactor_contains(function->range, a->symbols[i].declaration) &&
            (a->symbols[i].kind == SCRIPT_STATE || a->symbols[i].kind == SCRIPT_SHARED_STATE ||
             a->symbols[i].kind == SCRIPT_ACTION)) {
            return source_refactor_fail(error, size, "Inlining must retain callable-owned persistent or deferred storage");
        }
    }
    int statement_index = -1;
    for (int i = 0; i < a->statement_count; i++) {
        const script_statement *s = &a->statements[i];
        if ((s->kind == SCRIPT_STMT_DECLARE || s->kind == SCRIPT_STMT_EXPRESSION) &&
            s->expression == call_index) {
            statement_index = i;
            break;
        }
        if (s->kind == SCRIPT_STMT_ASSIGN && s->second == call_index) {
            statement_index = i;
            break;
        }
    }
    if (statement_index < 0) {
        return source_refactor_fail(error, size, "Inline a whole call statement or complete assigned call result");
    }
    const script_statement *site = &a->statements[statement_index];
    if (retained_annotations(p, site->range, function->range)) {
        return source_refactor_fail(error, size, "Expansion would discard a breakpoint or watched value, or a node comment, on the call or authored definition");
    }
    /* Free names retain their resolved meaning when moved into the caller.
     * Quoting a name changes its spelling, not its lexical binding. */
    for (int i = 0; i < a->use_count; i++) {
        const script_use *use = &a->uses[i];
        if (use->symbol < 0 || !source_refactor_contains(function->range, use->range) ||
            owned_symbol(a, call->right, use->symbol)) {
            continue;
        }
        const script_symbol *free_symbol = &a->symbols[use->symbol];
        for (int j = 0; j < a->symbol_count; j++) {
            if (j == use->symbol || strcmp(a->symbols[j].name, free_symbol->name)) {
                continue;
            }
            for (int scope = site->scope; scope >= 0; scope = a->scopes[scope].parent) {
                if (a->symbols[j].scope == scope) {
                    return source_refactor_fail(error, size, "Caller shadows a resolved name used by the authored definition");
                }
            }
        }
    }
    for (int i = 0; i < function->parameter_count; i++) {
        const script_symbol *parameter = &a->symbols[function->parameters[i]];
        if (parameter->type.flags & SCRIPT_TYPE_REFERENCE) {
            return source_refactor_fail(error, size, "Inlining reference parameters requires local reference aliases");
        }
        bool supplied = false;
        for (int j = 0; j < call->argument_count; j++) {
            supplied |= call->arguments[j].parameter == i;
        }
        if (!supplied && parameter->initializer >= 0 &&
            !a->expressions[parameter->initializer].constant_snapshot) {
            return source_refactor_fail(error, size, "Inlining must retain deferred parameter default evaluation");
        }
    }
    if (site->kind == SCRIPT_STMT_ASSIGN &&
        (site->op != SCRIPT_OP_NONE || site->expression < 0 ||
         a->expressions[site->expression].kind != SCRIPT_EXPR_SYMBOL)) {
        return source_refactor_fail(error, size, "Inline a complete simple value assignment to preserve lvalue evaluation");
    }
    if (function->result_count > 1) {
        return source_refactor_fail(error, size, "Inlining a result group requires an authored tuple storage declaration");
    }
    for (int i = 0; i < function->result_count; i++) {
        const script_symbol *r = &a->symbols[function->results[i]];
        if (r->initializer >= 0 && !a->expressions[r->initializer].constant_snapshot) {
            return source_refactor_fail(error, size, "Inlining must retain deferred result initializer evaluation");
        }
    }
    char **names = xcalloc((size_t)a->symbol_count, sizeof *names);
    for (int i = 0; i < a->symbol_count; i++) {
        if (source_refactor_contains(function->range, a->symbols[i].declaration) &&
            a->symbols[i].kind != SCRIPT_FUNCTION) {
            names[i] = inline_name(a, call->right, a->symbols[i].name, names);
        }
    }
    if (selected_entry >= 0) {
        const script_function *entry = &a->functions[selected_entry];
        if (entry->parameter_count != function->parameter_count) {
            for (int i = 0; i < a->symbol_count; i++) {
                free(names[i]);
            }
            free(names);
            return source_refactor_fail(error, size, "Macro entry signature differs from its owning callable");
        }
        for (int i = 0; i < entry->parameter_count; i++) {
            int parameter = entry->parameters[i];
            free(names[parameter]);
            names[parameter] = xstrdup(names[function->parameters[i]]);
        }
    }
    char *replacement = NULL;
    bool valid = true;
    bool *supplied = xcalloc((size_t)function->parameter_count, sizeof *supplied);
    /* Preserve the authored argument evaluation order, including named arguments. */
    for (int i = 0; valid && i < call->argument_count; i++) {
        const script_argument *arg = &call->arguments[i];
        int parameter = arg->parameter;
        if (parameter < 0 || parameter >= function->parameter_count ||
            supplied[parameter]) {
            valid = source_refactor_fail(error, size, "Call arguments have no unique resolved parameter mapping");
            break;
        }
        supplied[parameter] = true;
        const script_symbol *symbol = &a->symbols[function->parameters[parameter]];
        source_refactor_append(&replacement, "%s: ", names[function->parameters[parameter]]);
        valid = source_refactor_type(
            catalog,
            symbol->type,
            symbol->type.flags & SCRIPT_TYPE_REFERENCE,
            &replacement,
            error,
            size
        );
        char *value = source_refactor_slice(a->text, a->expressions[arg->value].range);
        source_refactor_append(&replacement, " = (%s);\n", value);
        free(value);
    }
    for (int i = 0; valid && i < function->parameter_count; i++) {
        if (supplied[i]) {
            continue;
        }
        const script_symbol *parameter = &a->symbols[function->parameters[i]];
        if (parameter->initializer < 0) {
            valid = source_refactor_fail(error, size, "Missing call parameter has no authored initializer");
            break;
        }
        char *value = renamed(a, a->expressions[parameter->initializer].range, names);
        source_refactor_append(&replacement, "%s: ", names[function->parameters[i]]);
        valid = source_refactor_type(
            catalog,
            parameter->type,
            parameter->type.flags & SCRIPT_TYPE_REFERENCE,
            &replacement,
            error,
            size
        );
        source_refactor_append(&replacement, " = %s;\n", value);
        free(value);
    }
    free(supplied);
    for (int i = 0; valid && i < function->result_count; i++) {
        const script_symbol *result = &a->symbols[function->results[i]];
        char *value = result->initializer >= 0
                        ? source_refactor_slice(
                              a->text,
                              a->expressions[result->initializer].range
                          )
                        : authoring_source_literal(
                              catalog,
                              result->type,
                              &(pin_value){ .type = result->type.storage,
                                            .container = result->type.container },
                              error,
                              size
                          );
        if (!value) {
            valid = false;
            break;
        }
        source_refactor_append(&replacement, "%s: ", names[function->results[i]]);
        valid = source_refactor_type(catalog, result->type, false, &replacement, error, size);
        source_refactor_append(&replacement, " = %s;\n", value);
        free(value);
    }
    script_range body = a->statements[function->body].range;
    const script_statement *block = &a->statements[function->body];
    int final_return = !macro && block->child_count
                         ? block->children[block->child_count - 1]
                         : -1;
    if (final_return >= 0 && a->statements[final_return].kind != SCRIPT_STMT_RETURN) {
        final_return = -1;
    }
    char *exit_base = NULL;
    source_refactor_append(
        &exit_base,
        "%s Completed",
        a->symbols[function->symbol].name
    );
    char *exit_text = source_refactor_take(&exit_base);
    char *exit_name = inline_name(a, call->right, exit_text, names);
    free(exit_text);
    bool needs_exit = false;
    body.start++;
    body.end--;
    text_edit *returns = NULL;
    script_syntax *syntax = script_parse(a->text);
    TSNode body_node = ts_node_named_descendant_for_byte_range(
        ts_tree_root_node(syntax->tree),
        (uint32_t)body.start - 1,
        (uint32_t)body.end + 1
    );
    for (uint32_t i = 0; i < ts_node_named_child_count(body_node); i++) {
        TSNode node = ts_node_named_child(body_node, i);
        const char *kind = ts_node_type(node);
        if (macro && (!strcmp(kind, "entry_declaration") || !strcmp(kind, "handler"))) {
            script_range range = script_node_range(node);
            if (range.start == a->functions[selected_entry].range.start) {
                TSNode block = ts_node_child_by_field_name(node, "body", 4);
                script_range inner = script_node_range(block);
                apush(
                    returns,
                    ((text_edit){ range.start - body.start,
                                  inner.start + 1 - body.start,
                                  xstrdup("") })
                );
                apush(
                    returns,
                    ((text_edit){ inner.end - 1 - body.start,
                                  range.end - body.start,
                                  xstrdup("") })
                );
            } else {
                apush(
                    returns,
                    ((text_edit){ range.start - body.start,
                                  range.end - body.start,
                                  xstrdup("") })
                );
            }
            continue;
        }
        if (macro && !strcmp(kind, "exit_declaration")) {
            script_range range = script_node_range(node);
            apush(returns, ((text_edit){ range.start - body.start, range.end - body.start, xstrdup("") }));
            continue;
        }
    }
    script_syntax_free(syntax);
    for (int i = 0; valid && i < a->statement_count; i++) {
        const script_statement *s = &a->statements[i];
        bool outcome = macro && s->kind == SCRIPT_STMT_GOTO && s->symbol >= 0 &&
                       a->symbols[s->symbol].outcome;
        if ((s->kind != SCRIPT_STMT_RETURN && !outcome) ||
            !source_refactor_contains(body, s->range)) {
            continue;
        }
        bool omitted = false;
        for (int j = 0; j < acount(returns); j++) {
            omitted |= s->range.start - body.start >= returns[j].start &&
                       s->range.end - body.start <= returns[j].end;
        }
        if (omitted) {
            continue;
        }
        char *text = NULL;
        if (s->expression >= 0 && function->result_count == 1) {
            char *value = renamed(a, a->expressions[s->expression].range, names);
            source_refactor_append(&text, "%s = %s;\n", names[function->results[0]], value);
            free(value);
        }
        for (int j = 0; j < s->child_count; j++) {
            const script_statement *store = &a->statements[s->children[j]];
            char *value = renamed(a, a->expressions[store->second].range, names);
            source_refactor_append(&text, "%s = %s;\n", names[store->symbol], value);
            free(value);
        }
        if (i != final_return) {
            source_refactor_append(&text, "goto %s;", exit_name);
            needs_exit = true;
        }
        apush(
            returns,
            ((text_edit){ s->range.start - body.start,
                          s->range.end - body.start,
                          source_refactor_take(&text) })
        );
    }
    /* Rename outside Return replacements so edits never overlap. */
    text_edit *body_edits = returns;
    for (int i = 0; valid && i < a->expression_count; i++) {
        const script_expression *e = &a->expressions[i];
        if (e->kind != SCRIPT_EXPR_SYMBOL || e->symbol < 0 ||
            !names[e->symbol] || !source_refactor_contains(body, e->range)) {
            continue;
        }
        bool covered = false;
        for (int j = 0; j < acount(body_edits); j++) {
            covered |= e->range.start - body.start >= body_edits[j].start &&
                       e->range.end - body.start <= body_edits[j].end;
        }
        if (!covered) {
            apush(
                body_edits,
                ((text_edit){ e->range.start - body.start,
                              e->range.end - body.start,
                              xstrdup(names[e->symbol]) })
            );
        }
    }
    for (int i = 0; valid && i < a->symbol_count + a->use_count; i++) {
        script_range range;
        int symbol;
        if (i < a->symbol_count) {
            symbol = i;
            range = a->symbols[i].declaration;
        } else {
            const script_use *use = &a->uses[i - a->symbol_count];
            symbol = use->symbol;
            range = use->range;
        }
        if (symbol < 0 || !names[symbol] || !source_refactor_contains(body, range)) {
            continue;
        }
        bool covered = false;
        for (int j = 0; j < acount(body_edits); j++) {
            covered |= range.start - body.start >= body_edits[j].start &&
                       range.end - body.start <= body_edits[j].end;
        }
        if (!covered) {
            apush(
                body_edits,
                ((text_edit){ range.start - body.start,
                              range.end - body.start,
                              xstrdup(names[symbol]) })
            );
        }
    }
    char *old_body = source_refactor_slice(a->text, body);
    char *new_body = text_edits_apply(old_body, body_edits, acount(body_edits));
    if (!new_body) {
        valid = source_refactor_fail(error, size, "Resolved inline edits overlap");
    } else {
        /* A label immediately followed by an identifier assignment can parse as
         * a typed declaration. An empty sequence is an inert statement with
         * an unambiguous reserved token after the label. */
        source_refactor_append(&replacement, "%s\n", new_body);
        if (needs_exit) {
            source_refactor_append(&replacement, "%s:\nsequence {}\n", exit_name);
        }
    }
    if (valid && function->result_count && site->kind != SCRIPT_STMT_EXPRESSION) {
        char *original = source_refactor_slice(a->text, site->range);
        text_edit use = { call->range.start - site->range.start,
                          call->range.end - site->range.start,
                          names[function->results[0]] };
        char *assigned = text_edits_apply(original, &use, 1);
        if (!assigned) {
            valid = source_refactor_fail(error, size, "Call does not occupy the complete assignment value");
        } else {
            source_refactor_append(&replacement, "%s\n", assigned);
        }
        free(original);
        free(assigned);
    }
    if (valid) {
        apush(
            changes->patches,
            ((text_edit){ site->range.start, site->range.end, source_refactor_take(&replacement) })
        );
        changes
            ->text = text_edits_apply(a->text, changes->patches, acount(changes->patches));
        valid = changes->text != NULL;
    }
    free(old_body);
    free(new_body);
    patches_free(body_edits);
    for (int i = 0; i < a->symbol_count; i++) {
        free(names[i]);
    }
    free(names);
    free(exit_name);
    afree(replacement);
    return valid;
}
