#include "language/semantic_internal.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <stdlib.h>
#include <string.h>

/* Only structural tokens emitted by the canonical parser participate. Strings,
 * comments and identifiers never supply delimiters. Missing tokens have no
 * original source bytes and are not evidence for a draft repair. */
static bool delimiters(TSNode node, char *stack, int *count, int depth)
{
    if (depth >= SCRIPT_MAX_DEPTH) {
        return false;
    }
    uint32_t children = script_analyzer_child_count(node);
    for (uint32_t i = 0; i < children; i++) {
        if (!delimiters(ts_node_child(node, i), stack, count, depth + 1)) {
            return false;
        }
    }
    if (children || ts_node_is_missing(node) || ts_node_is_extra(node)) {
        return true;
    }
    const char *kind = ts_node_type(node);
    if (kind[0] && !kind[1]) {
        char close = kind[0] == '(' ? ')'
                   : kind[0] == '{' ? '}'
                   : kind[0] == '[' ? ']'
                                    : 0;
        if (close) {
            if (*count >= SCRIPT_MAX_DEPTH) {
                return false;
            }
            stack[(*count)++] = close;
        } else if (kind[0] == ')' || kind[0] == '}' || kind[0] == ']') {
            if (!*count || stack[*count - 1] != kind[0]) {
                return false;
            }
            (*count)--;
        }
    }
    return true;
}
script_syntax *script_analyzer_recovery_syntax(const script_syntax *syntax)
{
    if (!syntax->diagnostic_count) {
        return NULL;
    }
    TSNode root = ts_tree_root_node(syntax->tree);
    TSNode first = script_analyzer_is(root, "ERROR") ? root
                                                     : ts_node_named_child(root, 0);
    if (!script_analyzer_is(first, "ERROR")) {
        return NULL;
    }
    TSNode token = ts_node_child(first, 0);
    if (!script_analyzer_is(token, "behavior") &&
        !script_analyzer_is(token, "library") &&
        !script_analyzer_is(token, "system")) {
        return NULL;
    }
    char stack[SCRIPT_MAX_DEPTH];
    int count = 0;
    if (!delimiters(root, stack, &count, 0) || !count) {
        return NULL;
    }
    size_t bytes = strlen(syntax->text);
    if (bytes > 8 * 1024 * 1024) {
        return NULL;
    }
    char *text = xmalloc(bytes + (size_t)count + 1);
    memcpy(text, syntax->text, bytes);
    for (int i = 0; i < count; i++) {
        text[bytes + (size_t)i] = stack[count - i - 1];
    }
    text[bytes + (size_t)count] = 0;
    script_syntax *recovered = script_parse(text);
    free(text);
    /* Do not chain repairs when the canonical parser still cannot identify
     * the declaration. The original failed analysis remains available. */
    TSNode program = ts_node_named_child(ts_tree_root_node(recovered->tree), 0);
    if (!script_analyzer_is(program, "behavior") &&
        !script_analyzer_is(program, "library") &&
        !script_analyzer_is(program, "system")) {
        script_syntax_free(recovered);
        return NULL;
    }
    return recovered;
}
static void clip(script_range *range, script_range boundary)
{
    if (range->start > boundary.end) {
        range->start = boundary.end;
        range->line = boundary.line;
        range->column = boundary.column;
    }
    if (range->end > boundary.end) {
        range->end = boundary.end;
    }
}
void script_analyzer_recovery_restore(script_analysis *out, const script_syntax *original)
{
    int end = (int)strlen(original->text);
    script_range boundary = { .end = end, .line = 1, .column = 1 };
    for (int i = 0; i < end; i++) {
        if (original->text[i] == '\n') {
            boundary.line++;
            boundary.column = 1;
        } else {
            boundary.column++;
        }
    }
    free(out->text);
    out->text = xstrdup(original->text);
    for (int i = 0; i < out->symbol_count; i++) {
        clip(&out->symbols[i].declaration, boundary);
    }
    for (int i = 0; i < out->use_count; i++) {
        clip(&out->uses[i].range, boundary);
    }
    for (int i = 0; i < out->scope_count; i++) {
        clip(&out->scopes[i].range, boundary);
    }
    for (int i = 0; i < out->expression_count; i++) {
        script_expression *expression = &out->expressions[i];
        expression->partial |= expression->range.end > end;
        clip(&expression->range, boundary);
        for (int j = 0; j < expression->argument_count; j++) {
            clip(&expression->arguments[j].range, boundary);
        }
    }
    for (int i = 0; i < out->statement_count; i++) {
        clip(&out->statements[i].range, boundary);
    }
    for (int i = 0; i < out->function_count; i++) {
        clip(&out->functions[i].range, boundary);
    }
    for (int i = 0; i < out->query_count; i++) {
        clip(&out->queries[i].range, boundary);
    }
    for (int i = 0; i < out->binding_count; i++) {
        clip(&out->bindings[i].range, boundary);
    }
    for (int i = 0; i < out->diagnostic_count; i++) {
        clip(&out->diagnostics[i].range, boundary);
    }
    for (int i = 0; i < original->diagnostic_count; i++) {
        apush(out->diagnostics, original->diagnostics[i]);
    }
    out->diagnostic_count = acount(out->diagnostics);
    out->valid = false;
    /* Recovered drafts do not publish advisory diagnostics. */
    asetlen(out->warnings, 0);
    out->warning_count = 0;
}
