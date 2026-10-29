#include "authoring/source_edit_refactor_internal.h"
#include "authoring/source_literal.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_REFACTOR_SYMBOLS 256

static bool lexical_value(const script_symbol *symbol)
{
    return symbol->kind == SCRIPT_LOCAL || symbol->kind == SCRIPT_PARAMETER ||
           symbol->kind == SCRIPT_RESULT || symbol->kind == SCRIPT_COMPONENT ||
           (symbol->kind == SCRIPT_STATE && symbol->scope > 0);
}
static bool used_in(const script_analysis *a, int symbol, script_range range)
{
    for (int i = 0; i < a->use_count; i++) {
        if (a->uses[i].symbol == symbol &&
            source_refactor_contains(range, a->uses[i].range)) {
            return true;
        }
    }
    return false;
}
static bool escapes(const script_analysis *a, int symbol, script_range range)
{
    for (int i = 0; i < a->use_count; i++) {
        if (a->uses[i].symbol == symbol &&
            !source_refactor_contains(range, a->uses[i].range)) {
            return true;
        }
    }
    return false;
}
static bool closed_control(
    const script_analysis *a,
    const script_syntax *syntax,
    script_range range,
    char *error,
    size_t size
)
{
    for (int i = 0; i < a->statement_count; i++) {
        const script_statement *s = &a->statements[i];
        if (!source_refactor_contains(range, s->range)) {
            continue;
        }
        if (s->kind == SCRIPT_STMT_RETURN) {
            return source_refactor_fail(error, size, "Extraction cannot change the owning callable of Return");
        }
        if (s->kind == SCRIPT_STMT_GOTO &&
            (s->symbol < 0 ||
             !source_refactor_contains(range, a->symbols[s->symbol].declaration))) {
            return source_refactor_fail(error, size, "Extraction cannot move a jump outside its label scope");
        }
        if (s->kind != SCRIPT_STMT_BREAK && s->kind != SCRIPT_STMT_CONTINUE) {
            continue;
        }
        TSNode node = ts_node_named_descendant_for_byte_range(
            ts_tree_root_node(syntax->tree),
            (uint32_t)s->range.start,
            (uint32_t)s->range.end
        );
        bool inside = false;
        for (node = ts_node_parent(node); !ts_node_is_null(node);
             node = ts_node_parent(node)) {
            const char *kind = ts_node_type(node);
            if (!strcmp(kind, "while_statement") || !strcmp(kind, "for_statement")) {
                inside = source_refactor_contains(range, script_node_range(node));
                break;
            }
        }
        if (!inside) {
            return source_refactor_fail(error, size, "Select the owning loop together with Break or Continue");
        }
    }
    return true;
}
static bool extract(
    const script_analysis *a,
    const script_catalog *catalog,
    const authoring_source_presentation *presentation,
    const authoring_edit_request *request,
    source_edit_changes *changes,
    char *error,
    size_t size
)
{
    if (!request->name || !*request->name) {
        return source_refactor_fail(error, size, "Give the extracted callable a name");
    }
    for (int i = 0; i < a->symbol_count; i++) {
        if (!strcmp(a->symbols[i].name, request->name)) {
            return source_refactor_fail(error, size, "Extracted callable name already exists");
        }
    }
    script_syntax *syntax = script_parse(a->text);
    script_range range = { 0 };
    TSNode region = { 0 };
    const uint64_t *ids = request->origin_count ? request->origins : &request->origin;
    int count = request->origin_count ? request->origin_count : 1;
    bool valid = source_edit_selection_region(presentation, syntax, ids, count, &range, &region, error, size);
    if (valid && strcmp(ts_node_type(region), "block") &&
        strcmp(ts_node_type(region), "callable_body") &&
        strcmp(ts_node_type(region), "system_body")) {
        valid = source_refactor_fail(error, size, "Extract executable statements inside one callable or handler body");
    }
    if (valid) {
        valid = closed_control(a, syntax, range, error, size);
    }
    int *inputs = NULL, *outputs = NULL;
    for (int i = 0; valid && i < a->symbol_count; i++) {
        const script_symbol *symbol = &a->symbols[i];
        if (source_refactor_contains(range, symbol->declaration)) {
            if (symbol->kind == SCRIPT_STATE || symbol->kind == SCRIPT_SHARED_STATE ||
                symbol->kind == SCRIPT_ACTION) {
                valid = source_refactor_fail(error, size, "Extraction must retain persistent or deferred storage identity");
                break;
            }
            if (escapes(a, i, range)) {
                if (symbol->kind != SCRIPT_LOCAL ||
                    (symbol->type.flags & SCRIPT_TYPE_REFERENCE)) {
                    valid = source_refactor_fail(error, size, "Only local values may escape an extracted statement region");
                    break;
                }
                apush(outputs, i);
            }
        } else if (lexical_value(symbol) && used_in(a, i, range)) {
            apush(inputs, i);
        }
    }
    if (acount(inputs) + acount(outputs) > MAX_REFACTOR_SYMBOLS) {
        valid = source_refactor_fail(error, size, "Extraction exceeds 256 captured and returned values");
    }
    char *definition = NULL, *call = NULL;
    char *name = source_refactor_quote(catalog, request->name);
    if (!name) {
        afree(inputs);
        afree(outputs);
        return source_refactor_fail(error, size, "Extracted declaration needs an identifier name");
    }
    bool macro = request->kind == AUTHORING_EDIT_EXTRACT_MACRO;
    int input_namespace = 0;
    bool input_collision;
    do {
        input_collision = false;
        char prefix[64];
        snprintf(prefix, sizeof prefix, "refactor_input_%d_", ++input_namespace);
        for (int i = 0; i < a->symbol_count; i++) {
            input_collision |= !strncmp(a->symbols[i].name, prefix, strlen(prefix));
        }
    } while (input_collision);
    source_refactor_append(&definition, macro ? "\nmacro %s(" : "\n%s :: (", name);
    for (int i = 0; valid && i < acount(inputs); i++) {
        const script_symbol *symbol = &a->symbols[inputs[i]];
        char parameter[64];
        snprintf(parameter, sizeof parameter, "refactor_input_%d_%d", input_namespace, i);
        source_refactor_append(&definition, "%s%s: ", i ? ", " : "", parameter);
        valid = source_refactor_type(
            catalog,
            symbol->type,
            symbol->writable || (symbol->type.flags & SCRIPT_TYPE_REFERENCE),
            &definition,
            error,
            size
        );
    }
    int output_namespace = 0;
    bool output_collision;
    do {
        output_collision = false;
        char prefix[64];
        snprintf(prefix, sizeof prefix, "refactor_return_%d_", ++output_namespace);
        for (int i = 0; i < a->symbol_count; i++) {
            output_collision |= !strncmp(a->symbols[i].name, prefix, strlen(prefix));
        }
    } while (output_collision);
    source_refactor_append(&definition, acount(outputs) ? ") -> (" : ")");
    for (int i = 0; valid && i < acount(outputs); i++) {
        source_refactor_append(&definition, "%srefactor_return_%d_%d: ", i ? ", " : "", output_namespace, i);
        valid = source_refactor_type(catalog, a->symbols[outputs[i]].type, false, &definition, error, size);
    }
    source_refactor_append(&definition, "%s {\n%s", acount(outputs) ? ")" : "", macro ? "after entry {\n" : "");
    char *selected = valid ? source_refactor_slice(a->text, range) : xstrdup("");
    text_edit *capture_edits = NULL;
    for (int i = 0; valid && i < a->expression_count; i++) {
        const script_expression *expression = &a->expressions[i];
        if (expression->kind != SCRIPT_EXPR_SYMBOL ||
            !source_refactor_contains(range, expression->range)) {
            continue;
        }
        for (int j = 0; j < acount(inputs); j++) {
            if (inputs[j] == expression->symbol) {
                /* A captured location becomes a pointer the body reads through;
                 * a pointer parameter's uses already spell their own `^`. */
                const script_symbol *symbol = &a->symbols[inputs[j]];
                bool pointer = symbol->writable &&
                               !(symbol->type.flags & SCRIPT_TYPE_REFERENCE);
                char parameter[64];
                snprintf(
                    parameter,
                    sizeof parameter,
                    "refactor_input_%d_%d%s",
                    input_namespace,
                    j,
                    pointer ? "^" : ""
                );
                apush(
                    capture_edits,
                    ((text_edit){ expression->range.start - range.start,
                                  expression->range.end - range.start,
                                  xstrdup(parameter) })
                );
                break;
            }
        }
    }
    if (acount(capture_edits)) {
        char *rewritten = text_edits_apply(selected, capture_edits, acount(capture_edits));
        if (!rewritten) {
            valid = source_refactor_fail(error, size, "Captured source expressions overlap");
        } else {
            free(selected);
            selected = rewritten;
        }
    }
    for (int i = 0; i < acount(capture_edits); i++) {
        free((char *)capture_edits[i].text);
    }
    afree(capture_edits);
    source_refactor_append(&definition, "%s\nreturn%s(", selected, macro ? ".Then" : "");
    for (int i = 0; i < acount(outputs); i++) {
        source_refactor_append(
            &definition,
            "%s%s",
            i ? ", " : "",
            a->symbols[outputs[i]].name
        );
    }
    source_refactor_append(&definition, ");\n%s}\n", macro ? "}\nexit Then;\n" : "");
    char retained[64];
    int unique = 0;
    bool collision;
    do {
        snprintf(retained, sizeof retained, "refactor_result_%d", ++unique);
        collision = false;
        for (int i = 0; i < a->symbol_count; i++) {
            collision |= !strcmp(a->symbols[i].name, retained);
        }
    } while (collision);
    if (acount(outputs)) {
        source_refactor_append(&call, "%s := ", retained);
    }
    source_refactor_append(&call, "await %s(", name);
    for (int i = 0; i < acount(inputs); i++) {
        char *input = NULL;
        for (int j = 0; j < a->expression_count; j++) {
            const script_expression *expression = &a->expressions[j];
            if (expression->kind == SCRIPT_EXPR_SYMBOL &&
                expression->symbol == inputs[i] &&
                source_refactor_contains(range, expression->range)) {
                input = source_refactor_slice(a->text, expression->range);
                break;
            }
        }
        if (!input) {
            valid = source_refactor_fail(error, size, "Captured value has no resolved source expression");
            break;
        }
        const script_symbol *symbol = &a->symbols[inputs[i]];
        bool address = symbol->writable &&
                       !(symbol->type.flags & SCRIPT_TYPE_REFERENCE);
        source_refactor_append(&call, "%s%s%s", i ? ", " : "", address ? "&" : "", input);
        free(input);
    }
    source_refactor_append(&call, ");\n");
    for (int i = 0; i < acount(outputs); i++) {
        char *output = source_refactor_quote(catalog, a->symbols[outputs[i]].name);
        source_refactor_append(&call, "%s := %s%s", output, retained, acount(outputs) == 1 ? ";\n" : ".");
        if (acount(outputs) > 1) {
            source_refactor_append(&call, "refactor_return_%d_%d;\n", output_namespace, i);
        }
        free(output);
    }
    if (valid) {
        TSNode root = ts_tree_root_node(syntax->tree);
        int end = -1;
        for (uint32_t i = 0; i < ts_node_named_child_count(root); i++) {
            TSNode document = ts_node_named_child(root, i);
            TSNode body = ts_node_child_by_field_name(document, "body", 4);
            if (!ts_node_is_null(body)) {
                end = (int)ts_node_end_byte(body) - 1;
                break;
            }
        }
        if (end < 0) {
            valid = source_refactor_fail(error, size, "Document has no callable declaration body");
        } else {
            apush(
                changes->patches,
                ((text_edit){ range.start, range.end, source_refactor_take(&call) })
            );
            apush(changes->patches, ((text_edit){ end, end, source_refactor_take(&definition) }));
            changes->text = text_edits_apply(
                a->text,
                changes->patches,
                acount(changes->patches)
            );
            valid = changes->text != NULL;
        }
    }
    free(selected);
    free(name);
    afree(call);
    afree(definition);
    afree(inputs);
    afree(outputs);
    script_syntax_free(syntax);
    return valid;
}

bool source_edit_refactor(
    const script_analysis *analysis,
    const script_catalog *catalog,
    const authoring_source_presentation *presentation,
    const authoring_edit_request *request,
    source_edit_changes *changes,
    char *error,
    size_t size
)
{
    if (request->kind == AUTHORING_EDIT_EXTRACT_FUNCTION ||
        request->kind == AUTHORING_EDIT_EXTRACT_MACRO) {
        return extract(analysis, catalog, presentation, request, changes, error, size);
    }
    return source_edit_inline(analysis, catalog, presentation, request, changes, error, size);
}
