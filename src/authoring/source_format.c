#include "authoring/source_format.h"

#include "support/array.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

enum
{
    FORMAT_MAX_BYTES = 8 * 1024 * 1024,
    FORMAT_MAX_DEPTH = 256,
    FORMAT_MAX_TOKENS = 262144,
};

typedef struct
{
    TSNode node;
    int start, end;
    bool open, close, end_line, nest_open, nest_close, case_open, case_close;
} token;

static bool kind(TSNode node, const char *name)
{
    return !strcmp(ts_node_type(node), name);
}

static bool block(TSNode node)
{
    return kind(node, "block") || kind(node, "declaration_body") ||
           kind(node, "system_body") || kind(node, "callable_body") ||
           kind(node, "component_body") || kind(node, "switch_statement") ||
           kind(node, "union_declaration") || kind(node, "sequence_statement");
}

/* Brackets whose contents may continue on later lines. */
static bool nest(TSNode node)
{
    return kind(node, "argument_list") || kind(node, "parameter_list") ||
           kind(node, "array_expression") || kind(node, "struct_expression") ||
           kind(node, "parenthesized_expression");
}

static bool end_line(TSNode node)
{
    const char *name = ts_node_type(node);
    size_t n = strlen(name);
    return (n >= 12 && !strcmp(name + n - 12, "_declaration")) ||
           (n >= 10 && !strcmp(name + n - 10, "_statement")) ||
           kind(node, "assignment") || kind(node, "handler") ||
           kind(node, "component_field");
}

static token leaf(TSNode node)
{
    token t = { .node = node,
                .start = (int)ts_node_start_byte(node),
                .end = (int)ts_node_end_byte(node) };
    TSNode parent = ts_node_parent(node);
    t.open = kind(node, "{") && block(parent);
    t.close = kind(node, "}") && block(parent);
    if (kind(node, ":") && kind(parent, "switch_arm")) {
        TSNode body = ts_node_child_by_field_name(parent, "body", 4);
        t.case_open = !kind(ts_node_child(body, 0), "{");
    }
    t.nest_open = (kind(node, "(") || kind(node, "[") || kind(node, "{")) &&
                  nest(parent);
    t.nest_close = (kind(node, ")") || kind(node, "]") || kind(node, "}")) &&
                   nest(parent);
    for (TSNode p = parent;
         !ts_node_is_null(p) && ts_node_end_byte(p) == (uint32_t)t.end;
         p = ts_node_parent(p)) {
        t.end_line |= end_line(p);
        if (kind(p, "block") && kind(ts_node_parent(p), "switch_arm") &&
            !kind(ts_node_child(p, 0), "{")) {
            t.case_close = true;
        }
    }
    return t;
}

static bool tokens(TSTree *tree, token **out)
{
    int depth = 0;
    TSTreeCursor cursor = ts_tree_cursor_new(ts_tree_root_node(tree));
    for (;;) {
        if (ts_tree_cursor_goto_first_child(&cursor)) {
            if (++depth > FORMAT_MAX_DEPTH) {
                ts_tree_cursor_delete(&cursor);
                return false;
            }
            continue;
        }
        TSNode node = ts_tree_cursor_current_node(&cursor);
        if (ts_node_end_byte(node) > ts_node_start_byte(node)) {
            if (acount(*out) == FORMAT_MAX_TOKENS) {
                ts_tree_cursor_delete(&cursor);
                return false;
            }
            apush(*out, leaf(node));
        }
        while (!ts_tree_cursor_goto_next_sibling(&cursor)) {
            if (!ts_tree_cursor_goto_parent(&cursor)) {
                ts_tree_cursor_delete(&cursor);
                return true;
            }
            depth--;
        }
    }
}

static bool append(char **out, const char *text, int n)
{
    int old = acount(*out);
    if (n < 0 || n > FORMAT_MAX_BYTES - old) {
        return false;
    }
    asetlen(*out, old + n);
    memcpy(*out + old, text, (size_t)n);
    return true;
}

/* u/s^2 */
static bool unit_operator(TSNode node)
{
    return (kind(node, "/") || kind(node, "^")) && kind(ts_node_parent(node), "unit");
}

static bool tight_before(token t)
{
    TSNode n = t.node, p = ts_node_parent(n);
    return kind(n, ",") || kind(n, ";") || kind(n, ":") || kind(n, ".") ||
           kind(n, "..") || kind(n, ")") || kind(n, "]") ||
           (kind(n, "(") && (kind(p, "parameter_list") || kind(p, "pattern_bindings") ||
                             kind(p, "cast_expression") ||
                             (kind(p, "argument_list") &&
                              !kind(ts_node_parent(p), "return_statement")))) ||
           (kind(n, "[") && (kind(p, "type") || kind(p, "index_expression") ||
                             kind(p, "engine_reference"))) ||
           ((kind(n, "<") || kind(n, ">")) &&
            (kind(p, "type_argument_list") || kind(p, "type_parameter_list") ||
             kind(p, "cast_expression"))) ||
           (kind(n, "?") && kind(p, "type")) || unit_operator(n);
}

static bool tight_after(token t)
{
    TSNode n = t.node, p = ts_node_parent(n);
    return kind(n, "(") || kind(n, "[") || kind(n, ".") || kind(n, "..") ||
           unit_operator(n) ||
           (kind(p, "unary_expression") && !kind(n, "not") &&
            ts_node_start_byte(p) == (uint32_t)t.start) ||
           (kind(n, "<") &&
            (kind(p, "type_argument_list") || kind(p, "type_parameter_list") ||
             kind(p, "cast_expression")));
}

/* Compare the complete CST, including comments and field relationships. Byte
 * positions alone may change; zero-width automatic terminators are omitted. */
static bool advance(TSTreeCursor *cursor, int *depth)
{
    for (;;) {
        if (ts_tree_cursor_goto_first_child(cursor)) {
            (*depth)++;
        } else {
            while (!ts_tree_cursor_goto_next_sibling(cursor)) {
                if (!ts_tree_cursor_goto_parent(cursor)) {
                    return false;
                }
                (*depth)--;
            }
        }
        TSNode node = ts_tree_cursor_current_node(cursor);
        if (ts_node_start_byte(node) != ts_node_end_byte(node)) {
            return true;
        }
    }
}

static bool equivalent(TSNode a, TSNode b, const char *before, const char *after)
{
    TSTreeCursor ca = ts_tree_cursor_new(a), cb = ts_tree_cursor_new(b);
    int da = 0, db = 0;
    bool same = true;
    for (;;) {
        a = ts_tree_cursor_current_node(&ca);
        b = ts_tree_cursor_current_node(&cb);
        const char *fa = ts_tree_cursor_current_field_name(&ca),
                   *fb = ts_tree_cursor_current_field_name(&cb);
        if (da != db || strcmp(ts_node_type(a), ts_node_type(b)) ||
            ts_node_is_named(a) != ts_node_is_named(b) ||
            (fa == NULL) != (fb == NULL) || (fa && strcmp(fa, fb))) {
            same = false;
            break;
        }
        if (!ts_node_child_count(a) && !ts_node_child_count(b)) {
            uint32_t sa = ts_node_start_byte(a), sb = ts_node_start_byte(b);
            uint32_t length = ts_node_end_byte(a) - sa;
            if (length != ts_node_end_byte(b) - sb ||
                memcmp(before + sa, after + sb, length)) {
                same = false;
                break;
            }
        }
        bool next_a = advance(&ca, &da), next_b = advance(&cb, &db);
        if (!next_a || !next_b) {
            same = next_a == next_b;
            break;
        }
    }
    ts_tree_cursor_delete(&ca);
    ts_tree_cursor_delete(&cb);
    return same;
}

char *authoring_source_format(const script_syntax *source, char *error, size_t size)
{
    assert(source && source->tree && source->text);
    if (size) {
        error[0] = 0;
    }
    if (source->diagnostic_count ||
        ts_node_has_error(ts_tree_root_node(source->tree))) {
        if (size) {
            snprintf(error, size, "Cannot format source with syntax errors");
        }
        return NULL;
    }
    token *list = NULL;
    char *out = NULL;
    bool *nested = NULL; /* Whether each open bracket indented its lines. */
    if (ts_node_end_byte(ts_tree_root_node(source->tree)) > FORMAT_MAX_BYTES ||
        !tokens(source->tree, &list)) {
        goto limit;
    }
    int indent = 0;
    for (int i = 0; i < acount(list); i++) {
        token current = list[i];
        if (current.close) {
            indent--;
        }
        if (current.nest_close && acount(nested)) {
            indent -= nested[acount(nested) - 1];
            asetlen(nested, acount(nested) - 1);
        }
        if (i) {
            token previous = list[i - 1];
            int lines = 0;
            for (int p = previous.end; p < current.start; p++) {
                lines += source->text[p] == '\n';
            }
            bool comment = kind(current.node, "comment");
            bool inline_comment = comment && !lines;
            bool empty_block = previous.open && current.close;
            bool chained = previous.close &&
                           (kind(current.node, "else") || kind(current.node, ";"));
            bool newline = (lines && !chained) ||
                           (!inline_comment && !empty_block && !chained &&
                            (previous.open || previous.close || previous.end_line ||
                             previous.case_open || current.close));
            if (newline) {
                if (!append(&out, lines > 1 ? "\n\n" : "\n", lines > 1 ? 2 : 1)) {
                    goto limit;
                }
                for (int d = 0; d < indent; d++) {
                    if (!append(&out, "    ", 4)) {
                        goto limit;
                    }
                }
            } else if (
                !empty_block &&
                ((!tight_before(current) && !tight_after(previous)) ||
                 kind(previous.node, "->") || kind(previous.node, "::"))
            ) {
                if (!append(&out, " ", 1)) {
                    goto limit;
                }
            }
        }
        if (!append(&out, source->text + current.start, current.end - current.start)) {
            goto limit;
        }
        if (current.open || current.case_open) {
            indent++;
        }
        if (current.case_close) {
            indent--;
        }
        if (current.nest_open) {
            bool wraps = false;
            for (int p = current.end; i + 1 < acount(list) && p < list[i + 1].start;
                 p++) {
                wraps |= source->text[p] == '\n';
            }
            apush(nested, wraps);
            indent += wraps;
        }
    }
    afree(nested);
    if (!append(&out, "\n", 1) || !append(&out, "", 1)) {
        goto limit;
    }
    script_syntax *check = script_parse(out);
    bool same = !check->diagnostic_count && equivalent(
                                                ts_tree_root_node(source->tree),
                                                ts_tree_root_node(check->tree),
                                                source->text,
                                                out
                                            );
    script_syntax_free(check);
    afree(list);
    if (!same) {
        if (size) {
            snprintf(error, size, "Formatting would change the syntax tree");
        }
        afree(out);
        return NULL;
    }
    char *result = xstrdup(out);
    afree(out);
    return result;

limit:
    afree(nested);
    afree(list);
    afree(out);
    if (size) {
        snprintf(error, size, "Source exceeds formatter byte, nesting or token limit");
    }
    return NULL;
}
