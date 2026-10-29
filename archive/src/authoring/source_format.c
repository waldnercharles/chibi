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
    int group, pair, flat_width, hard_count;
    bool wraps;
    bool open, close, end_line, nest_open, nest_close, case_open, case_close;
    int chain_open, chain_close; /* Operator chains starting or ending here. */
} token;

/* Leading whitespace: indentation levels, then alignment spaces. */
typedef struct
{
    int levels, spaces;
} margin;

/* An open block, case arm, bracket or operator chain: the margin of the lines
 * inside it, and of a line that starts with its closing token. */
typedef struct
{
    margin inside, close;
} frame;

static bool kind(TSNode node, const char *name)
{
    return !strcmp(ts_node_type(node), name);
}

static bool block(TSNode node)
{
    return kind(node, "block") || kind(node, "declaration_body") ||
           kind(node, "system_body") || kind(node, "callable_body") ||
           kind(node, "component_body") || kind(node, "record_body") ||
           kind(node, "prototype_body") ||
           (kind(node, "struct_expression") &&
            (kind(ts_node_parent(node), "prototype_component") ||
             kind(ts_node_parent(node), "relationship_declaration"))) ||
           kind(node, "switch_statement") || kind(node, "union_declaration") ||
           kind(node, "sequence_statement");
}

/* Brackets whose contents may continue on later lines. */
static bool nest(TSNode node)
{
    return !block(node) &&
           (kind(node, "argument_list") || kind(node, "parameter_list") ||
            kind(node, "query_list") || kind(node, "pattern_bindings") ||
            kind(node, "array_expression") || kind(node, "map_expression") ||
            kind(node, "struct_expression") || kind(node, "prototype_bases") ||
            kind(node, "struct_field") || kind(node, "parenthesized_expression"));
}

/* The outermost operator of an expression, whose operands may continue on
 * later lines. */
static bool chain(TSNode node)
{
    return kind(node, "binary_expression") &&
           !kind(ts_node_parent(node), "binary_expression");
}

static bool end_line(TSNode node)
{
    const char *name = ts_node_type(node);
    size_t n = strlen(name);
    return (n >= 12 && !strcmp(name + n - 12, "_declaration")) ||
           (n >= 10 && !strcmp(name + n - 10, "_statement")) ||
           kind(node, "assignment") || kind(node, "handler") ||
           kind(node, "component_field") || kind(node, "record_field");
}

static token leaf(TSNode node)
{
    token t = { .node = node,
                .start = (int)ts_node_start_byte(node),
                .end = (int)ts_node_end_byte(node) };
    TSNode parent = ts_node_parent(node);
    t.open = kind(node, "{") && block(parent);
    t.close = kind(node, "}") && block(parent);
    t.end_line = kind(node, ",") && block(parent);
    if (kind(node, ":") && kind(parent, "switch_arm")) {
        TSNode body = ts_node_child_by_field_name(parent, "body", 4);
        t.case_open = !kind(ts_node_child(body, 0), "{");
    }
    t.nest_open = (kind(node, "(") || kind(node, "[") || kind(node, "{")) &&
                  nest(parent);
    t.nest_close = (kind(node, ")") || kind(node, "]") || kind(node, "}")) &&
                   nest(parent);
    for (TSNode p = parent;
         !ts_node_is_null(p) && ts_node_start_byte(p) == (uint32_t)t.start;
         p = ts_node_parent(p)) {
        t.chain_open += chain(p);
    }
    for (TSNode p = parent;
         !ts_node_is_null(p) && ts_node_end_byte(p) == (uint32_t)t.end;
         p = ts_node_parent(p)) {
        t.end_line |= end_line(p);
        t.chain_close += chain(p);
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

static bool append_margin(char **out, margin m, authoring_format_options config)
{
    for (int d = 0; d < m.levels; d++) {
        if (!append(
                out,
                config.use_tabs ? "\t" : "                ",
                config.use_tabs ? 1 : config.indent_width
            )) {
            return false;
        }
    }
    for (int d = 0; d < m.spaces; d++) {
        if (!append(out, " ", 1)) {
            return false;
        }
    }
    return true;
}

/* Aligns later lines at column, indenting with as many of line's levels as fit. */
static margin align(margin line, int column, int width)
{
    int levels = line.levels < column / width ? line.levels : column / width;
    return (margin){ levels, column - levels * width };
}

/* The margin of a line inside the innermost open frame. */
static margin inner(frame *frames)
{
    return acount(frames) ? frames[acount(frames) - 1].inside : (margin){ 0 };
}

/* u/s^2 */
static bool unit_operator(TSNode node)
{
    return (kind(node, "/") || kind(node, "^")) && kind(ts_node_parent(node), "unit");
}

/* `Name: T : value` spaces the colon that follows a constant's type. */
static bool constant_colon(TSNode n)
{
    return kind(n, ":") && kind(ts_node_parent(n), "constant_declaration") &&
           kind(ts_node_prev_sibling(n), "type");
}

static bool tight_before(token t)
{
    TSNode n = t.node, p = ts_node_parent(n);
    return kind(n, ",") || kind(n, ";") || (kind(n, ":") && !constant_colon(n)) ||
           (kind(n, ".") && !kind(p, "variant_pattern")) || kind(n, "..") ||
           kind(n, "..<") || kind(n, ")") || kind(n, "]") ||
           (kind(n, "}") && kind(p, "struct_expression")) ||
           (kind(n, "(") && (kind(p, "parameter_list") || kind(p, "query_list") ||
                             kind(p, "pattern_bindings") ||
                             kind(p, "reduce_statement") || kind(p, "prototype_bases") ||
                             (kind(p, "argument_list") &&
                              !kind(ts_node_parent(p), "return_statement")))) ||
           (kind(n, "[") && (kind(p, "type") || kind(p, "index_expression"))) ||
           (kind(n, "?") && kind(p, "type")) ||
           (kind(n, "^") && kind(p, "dereference_expression")) ||
           (kind(n, "(") && kind(p, "parenthesized_type")) || unit_operator(n);
}

static bool tight_after(token t)
{
    TSNode n = t.node, p = ts_node_parent(n);
    return kind(n, "(") || kind(n, "[") || kind(n, ".") || kind(n, "..") ||
           kind(n, "..<") || kind(n, "@") || unit_operator(n) ||
           (kind(n, "$") && (kind(p, "type_binder") || kind(p, "parameter"))) ||
           (kind(n, "{") && kind(p, "struct_expression")) ||
           (kind(n, "^") && kind(p, "pointer_type")) ||
           (kind(n, "&") &&
            (kind(p, "address_expression") || kind(p, "for_statement"))) ||
           (kind(p, "unary_expression") && !kind(n, "not") &&
            ts_node_start_byte(p) == (uint32_t)t.start);
}

static bool space_between(token previous, token current)
{
    return !(previous.open && current.close) &&
           ((!tight_before(current) && !tight_after(previous)) ||
            kind(previous.node, "->") || kind(previous.node, "::"));
}

/* Measure flat groups once, without reparsing or inventing another printer.
 * Comments and statement bodies keep their line boundaries. Nested groups are
 * subsequently laid out against the actual column at their opening bracket. */
static void measure_groups(token *list)
{
    int stack[FORMAT_MAX_DEPTH], depth = 0, width = 0, hard = 0;
    for (int i = 0; i < acount(list); i++) {
        token *t = &list[i];
        width += i && space_between(list[i - 1], *t);
        t->group = depth ? stack[depth - 1] : -1;
        if (t->nest_open) {
            assert(depth < FORMAT_MAX_DEPTH);
            stack[depth++] = i;
            t->flat_width = width;
            t->hard_count = hard;
        }
        width += t->end - t->start;
        hard += kind(t->node, "comment") || t->open || t->close;
        if (t->nest_close) {
            assert(depth);
            token *open = &list[stack[--depth]];
            open->pair = i;
            open->flat_width = hard == open->hard_count ? width - open->flat_width
                                                        : FORMAT_MAX_BYTES;
        }
    }
    assert(!depth);
}

static bool break_after(token previous, token open)
{
    TSNode parent = ts_node_parent(previous.node);
    if (kind(previous.node, ",")) {
        return ts_node_eq(parent, ts_node_parent(open.node));
    }
    return kind(ts_node_parent(open.node), "parenthesized_expression") &&
           kind(parent, "binary_expression") &&
           ts_node_eq(previous.node, ts_node_child_by_field_name(parent, "operator", 8));
}

static void record_edit(
    authoring_format_edit **edits,
    const char *before,
    const char *after,
    int start,
    int end,
    int new_start,
    int new_end
)
{
    if (end - start != new_end - new_start ||
        (end > start &&
         memcmp(before + start, after + new_start, (size_t)(end - start)))) {
        apush(*edits, ((authoring_format_edit){ start, end, new_start, new_end }));
    }
}

authoring_format_options authoring_format_defaults()
{
    return (authoring_format_options){ .indent_width = 4 };
}

void authoring_format_map_free(authoring_format_map *map)
{
    afree(map->edits);
    *map = (authoring_format_map){ 0 };
}

int authoring_format_map_position(const authoring_format_map *map, int offset, authoring_format_affinity affinity)
{
    assert(offset >= 0 && offset <= map->source_size);
    assert(affinity == AUTHORING_FORMAT_BEFORE || affinity == AUTHORING_FORMAT_AFTER);
    int delta = 0;
    for (int i = 0; i < map->count; i++) {
        authoring_format_edit e = map->edits[i];
        if (offset < e.start) {
            break;
        }
        if (offset == e.start && e.start != e.end) {
            return e.new_start;
        }
        if (offset < e.end || (offset == e.start && e.start == e.end)) {
            return affinity == AUTHORING_FORMAT_BEFORE ? e.new_start : e.new_end;
        }
        if (offset == e.end) {
            return e.new_end;
        }
        delta = e.new_end - e.end;
    }
    return offset + delta;
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
    return authoring_source_format_with_options(source, NULL, NULL, error, size);
}

char *authoring_source_format_with_options(
    const script_syntax *source,
    const authoring_format_options *options,
    authoring_format_map *map,
    char *error,
    size_t size
)
{
    assert(source && source->tree && source->text);
    if (size) {
        error[0] = 0;
    }
    if (map) {
        *map = (authoring_format_map){ 0 };
    }
    authoring_format_options config = options ? *options
                                              : authoring_format_defaults();
    if (config.indent_width < 1 || config.indent_width > 16 ||
        config.preferred_width < 0 || config.preferred_width > 4096) {
        if (size) {
            snprintf(error, size, "Invalid formatter options: indent_width must be 1..16 and preferred_width 0..4096");
        }
        return NULL;
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
    frame *frames = NULL;
    authoring_format_edit *edits = NULL;
    size_t source_size = strlen(source->text);
    if (source_size > FORMAT_MAX_BYTES || !tokens(source->tree, &list)) {
        goto limit;
    }
    if (config.preferred_width) {
        measure_groups(list);
    }
    /* Count package CR separators without changing legacy root layout. */
    bool package = !ts_node_is_null(
        ts_node_child_by_field_name(ts_tree_root_node(source->tree), "package", 7)
    );
    margin line = { 0 }; /* The current output line's margin. */
    int column = 0;
    for (int i = 0; i < acount(list); i++) {
        token current = list[i];
        int gap_start = acount(out);
        margin at = inner(frames);
        if (current.close || current.nest_close) {
            assert(acount(frames));
            at = apop(frames).close;
        }
        if (i) {
            token previous = list[i - 1];
            int lines = 0;
            for (int p = previous.end; p < current.start; p++) {
                lines += source->text[p] == '\n' ||
                         (package && source->text[p] == '\r' &&
                          source->text[p + 1] != '\n');
            }
            bool comment = kind(current.node, "comment");
            bool inline_comment = comment && !lines;
            bool empty_block = previous.open && current.close;
            bool chained = previous.close &&
                           (kind(current.node, "else") || kind(current.node, ";"));
            bool group_break = false;
            if (config.preferred_width && current.group >= 0) {
                token open = list[current.group];
                if (!comment && !kind(previous.node, "comment")) {
                    lines = 0;
                }
                group_break = open.wraps && !inline_comment &&
                              (i - 1 == current.group || current.nest_close ||
                               break_after(previous, open));
            }
            bool newline = group_break || (lines && !chained) ||
                           (!inline_comment && !empty_block && !chained &&
                            (previous.open || previous.close || previous.end_line ||
                             previous.case_open || current.close));
            if (newline) {
                if (!append(&out, lines > 1 ? "\n\n" : "\n", lines > 1 ? 2 : 1) ||
                    !append_margin(&out, at, config)) {
                    goto limit;
                }
                line = at;
                column = at.levels * config.indent_width + at.spaces;
            } else if (space_between(previous, current)) {
                if (!append(&out, " ", 1)) {
                    goto limit;
                }
                column++;
            }
        }
        if (map) {
            record_edit(
                &edits,
                source->text,
                out,
                i ? list[i - 1].end : 0,
                current.start,
                gap_start,
                acount(out)
            );
        }
        int start_column = column;
        if (!append(&out, source->text + current.start, current.end - current.start)) {
            goto limit;
        }
        for (int p = current.start; p < current.end; p++) {
            column = source->text[p] == '\n' || (package && source->text[p] == '\r')
                       ? 0
                       : column + 1;
        }
        /* Operands continued on later lines align with the first one. */
        for (int c = 0; c < current.chain_open; c++) {
            apush(frames, ((frame){ align(line, start_column, config.indent_width), line }));
        }
        if (current.open || current.case_open) {
            margin outer = inner(frames);
            apush(frames, ((frame){ { outer.levels + 1, outer.spaces }, outer }));
        }
        int ended = current.chain_close + current.case_close;
        assert(acount(frames) >= ended);
        asetlen(frames, acount(frames) - ended);
        if (current.nest_open) {
            bool wraps = false;
            if (config.preferred_width) {
                wraps = current.pair > i + 1 &&
                        current.flat_width > config.preferred_width - start_column;
                list[i].wraps = wraps;
            } else {
                for (int p = current.end;
                     i + 1 < acount(list) && p < list[i + 1].start;
                     p++) {
                    wraps |= source->text[p] == '\n' ||
                             (package && source->text[p] == '\r');
                }
            }
            /* A bracket that ends its line indents the next lines one level
             * past that line; otherwise they align with its first item. */
            margin within = wraps ? (margin){ line.levels + 1, line.spaces }
                                  : align(line, column, config.indent_width);
            apush(frames, ((frame){ within, line }));
        }
    }
    afree(frames);
    int last = acount(out);
    if (!append(&out, "\n", 1)) {
        goto limit;
    }
    if (map) {
        record_edit(
            &edits,
            source->text,
            out,
            acount(list) ? list[acount(list) - 1].end : 0,
            (int)source_size,
            last,
            acount(out)
        );
    }
    if (!append(&out, "", 1)) {
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
        afree(edits);
        afree(out);
        return NULL;
    }
    if (map) {
        *map = (authoring_format_map){ .edits = edits,
                                       .count = acount(edits),
                                       .source_size = (int)source_size,
                                       .formatted_size = acount(out) - 1 };
    }
    char *result = xstrdup(out);
    afree(out);
    return result;

limit:
    afree(frames);
    afree(list);
    afree(edits);
    afree(out);
    if (size) {
        snprintf(error, size, "Source exceeds formatter byte, nesting or token limit");
    }
    return NULL;
}
