#include "syntax.h"

#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const TSLanguage *tree_sitter_chibi();

script_range script_node_range(TSNode node)
{
    TSPoint point = ts_node_start_point(node);
    return (script_range){ (int)ts_node_start_byte(node),
                           (int)ts_node_end_byte(node),
                           (int)point.row + 1,
                           (int)point.column + 1,
                           0 };
}

static bool hidden_error(TSNode node)
{
    if (!ts_node_has_error(node)) {
        return false;
    }
    for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
        if (ts_node_has_error(ts_node_child(node, i))) {
            return false;
        }
    }
    return true;
}

static void diagnose(script_syntax *syntax)
{
    TSTreeCursor cursor = ts_tree_cursor_new(ts_tree_root_node(syntax->tree));
    for (;;) {
        TSNode node = ts_tree_cursor_current_node(&cursor);
        bool error = ts_node_is_error(node), missing = ts_node_is_missing(node);
        if (error || missing || hidden_error(node)) {
            script_diagnostic diagnostic = { .range = script_node_range(node) };
            if (missing) {
                snprintf(diagnostic.message, sizeof diagnostic.message, "Expected %s", ts_node_type(node));
            } else if (error) {
                snprintf(diagnostic.message, sizeof diagnostic.message, "Unexpected syntax");
            } else {
                /* Recovery can insert an invisible newline token. Its closest
                 * visible parent retains the error, even without an ERROR or
                 * MISSING child available to the public tree cursor. */
                snprintf(
                    diagnostic.message,
                    sizeof diagnostic.message,
                    "Missing syntax inside %s",
                    ts_node_type(node)
                );
            }
            apush(syntax->diagnostics, diagnostic);
        }
        /* A recovered ERROR already describes its whole range. */
        if (!error && ts_tree_cursor_goto_first_child(&cursor)) {
            continue;
        }
        while (!ts_tree_cursor_goto_next_sibling(&cursor)) {
            if (!ts_tree_cursor_goto_parent(&cursor)) {
                ts_tree_cursor_delete(&cursor);
                syntax->diagnostic_count = acount(syntax->diagnostics);
                assert(
                    syntax->diagnostic_count ||
                    !ts_node_has_error(ts_tree_root_node(syntax->tree))
                );
                return;
            }
        }
    }
}

static script_syntax *parse_owned(char *text, const TSTree *previous)
{
    size_t size = strlen(text);
    assert(size <= INT_MAX);
    script_syntax *syntax = xcalloc(1, sizeof *syntax);
    syntax->text = text;
    TSParser *parser = ts_parser_new();
    assert(parser);
    bool compatible = ts_parser_set_language(parser, tree_sitter_chibi());
    assert(compatible);
    (void)compatible;
    syntax->tree = ts_parser_parse_string(parser, previous, text, (uint32_t)size);
    assert(syntax->tree);
    ts_parser_delete(parser);
    diagnose(syntax);
    return syntax;
}

script_syntax *script_parse(const char *text)
{
    assert(text);
    return parse_owned(xstrdup(text), NULL);
}

static TSPoint advance(TSPoint point, const char *text, int size)
{
    for (int i = 0; i < size; i++) {
        if (text[i] == '\n') {
            point.row++;
            point.column = 0;
        } else {
            point.column++;
        }
    }
    return point;
}

script_syntax *script_edit(const script_syntax *previous, int start, int end, const char *replacement)
{
    assert(previous && replacement);
    size_t size = strlen(previous->text), added = strlen(replacement);
    assert(start >= 0 && end >= start && (size_t)end <= size);
    size_t new_size = size - (size_t)(end - start) + added;
    assert(new_size <= INT_MAX);
    char *text = xmalloc(new_size + 1);
    memcpy(text, previous->text, (size_t)start);
    memcpy(text + start, replacement, added);
    memcpy(text + start + added, previous->text + end, size - (size_t)end + 1);
    TSPoint start_point = advance((TSPoint){ 0 }, previous->text, start);
    TSInputEdit edit = {
        .start_byte = (uint32_t)start,
        .old_end_byte = (uint32_t)end,
        .new_end_byte = (uint32_t)((size_t)start + added),
        .start_point = start_point,
        .old_end_point = advance(start_point, previous->text + start, end - start),
        .new_end_point = advance(start_point, replacement, (int)added)
    };
    TSTree *tree = ts_tree_copy(previous->tree);
    ts_tree_edit(tree, &edit);
    script_syntax *syntax = parse_owned(text, tree);
    ts_tree_delete(tree);
    return syntax;
}

void script_syntax_free(script_syntax *syntax)
{
    if (!syntax) {
        return;
    }
    ts_tree_delete(syntax->tree);
    afree(syntax->diagnostics);
    free(syntax->text);
    free(syntax);
}
