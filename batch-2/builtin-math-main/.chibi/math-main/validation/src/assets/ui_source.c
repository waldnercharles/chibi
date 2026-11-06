/* The ui language service's parse: a markup or style file's tree-sitter
 * tree, and the highlighting, outline and diagnostics read from it. */
#include "assets/ui_source.h"

#include "assets/ui_source_internal.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const TSLanguage *tree_sitter_css();
const TSLanguage *tree_sitter_html();

/* ---------- Lines ---------- */

static int *line_starts(const char *text)
{
    int *lines = NULL;
    apush(lines, 0);
    for (const char *p = text; (p = strchr(p, '\n')); p++) {
        apush(lines, (int)(p - text) + 1);
    }
    return lines;
}

static int line_end(const char *text, int at)
{
    const char *newline = strchr(text + at, '\n');
    return newline ? (int)(newline - text) : at + (int)strlen(text + at);
}

void ui_source_position(const ui_source *source, int offset, int *line, int *column)
{
    const int *lines = source->trees->lines;
    int low = 0, high = acount(lines) - 1;
    while (low < high) {
        int middle = (low + high + 1) / 2;
        if (lines[middle] <= offset) {
            low = middle;
        } else {
            high = middle - 1;
        }
    }
    *line = low + 1;
    *column = offset - lines[low] + 1;
}

int ui_source_offset(const ui_source *source, int line, int column)
{
    const int *lines = source->trees->lines;
    assert(line >= 1 && column >= 1);
    if (line > acount(lines)) {
        return (int)strlen(source->text);
    }
    int start = lines[line - 1], end = line_end(source->text, start);
    return start + column - 1 < end ? start + column - 1 : end;
}

/* ---------- Nodes ---------- */

static bool is_type(TSNode node, const char *type)
{
    return !strcmp(ts_node_type(node), type);
}

static int node_start(TSNode node)
{
    return (int)ts_node_start_byte(node);
}

static int node_end(TSNode node)
{
    return (int)ts_node_end_byte(node);
}

/* The type of the node holding `node`; "" for the root. */
static const char *parent_type(TSNode node)
{
    TSNode parent = ts_node_parent(node);
    return ts_node_is_null(parent) ? "" : ts_node_type(parent);
}

/* The first child of `type`, or a null node. */
static TSNode child_of_type(TSNode node, const char *type)
{
    uint32_t count = ts_node_child_count(node);
    for (uint32_t i = 0; i < count; i++) {
        TSNode child = ts_node_child(node, i);
        if (is_type(child, type)) {
            return child;
        }
    }
    return (TSNode){ 0 };
}

static char *copy_range(const char *text, int start, int end)
{
    char *copy = xmalloc((size_t)(end - start) + 1);
    memcpy(copy, text + start, (size_t)(end - start));
    copy[end - start] = '\0';
    return copy;
}

/* Text with each run of spaces and line breaks made one space. */
static char *collapsed(const char *text, int start, int end)
{
    char *out = xmalloc((size_t)(end - start) + 1);
    int length = 0;
    for (int i = start; i < end; i++) {
        bool space = isspace((unsigned char)text[i]);
        if (!space || (length && out[length - 1] != ' ')) {
            out[length++] = space ? ' ' : text[i];
        }
    }
    while (length && out[length - 1] == ' ') {
        length--;
    }
    out[length] = '\0';
    return out;
}

/* ---------- Highlighting ---------- */

static const char *const KIND_CLASSES[UI_SOURCE_KIND_COUNT] = {
    NULL,
    "tok-number",
    "tok-string",
    "tok-keyword",
    "tok-name",
    "tok-path",
    "tok-punct",
    "tok-muted",
    "tok-error",
    "tok-warning",
    "tok-keyword tok-tag",
    "tok-name tok-attribute",
    "tok-keyword tok-selector",
    "tok-name tok-property",
    "tok-path tok-variable",
    "tok-path tok-binding",
};

const char *ui_source_kind_class(ui_source_kind kind)
{
    assert(kind >= 0 && kind < UI_SOURCE_KIND_COUNT);
    return KIND_CLASSES[kind];
}

/* Spans of text parsed apart from the source, as a style attribute is:
 * `shift` turns the parsed text's offsets into the source's, and only spans
 * inside [low, high) of the source count. */
typedef struct painter
{
    ui_source *source;
    const char *text;
    int shift, low, high;
} painter;

static void paint(painter *p, int start, int end, ui_source_kind kind)
{
    start += p->shift;
    end += p->shift;
    start = start < p->low ? p->low : start;
    end = end > p->high ? p->high : end;
    if (start >= end || kind == UI_SOURCE_TEXT) {
        return;
    }
    apush(p->source->spans, ((ui_source_span){ start, end, kind }));
}

static bool word_char(char c)
{
    return isalnum((unsigned char)c) || c == '_';
}

static bool is_word(const char *text, int length, const char *const *words)
{
    for (int i = 0; words[i]; i++) {
        if ((int)strlen(words[i]) == length &&
            !strncmp(text, words[i], (size_t)length)) {
            return true;
        }
    }
    return false;
}

/* One token of a data binding expression at `i`; returns its end. */
static int paint_expression_token(painter *p, int i, int end)
{
    static const char *const LITERALS[] = { "true", "false", "null", NULL };
    const char *t = p->text;
    char c = t[i];
    int j = i + 1;
    ui_source_kind kind = UI_SOURCE_PUNCTUATION;
    if (c == '\'' || c == '"') {
        while (j < end && t[j] != c) {
            j++;
        }
        j += j < end;
        kind = UI_SOURCE_STRING;
    } else if (isdigit((unsigned char)c)) {
        while (j < end && (isalnum((unsigned char)t[j]) || t[j] == '.')) {
            j++;
        }
        kind = UI_SOURCE_NUMBER;
    } else if (word_char(c)) {
        while (j < end && word_char(t[j])) {
            j++;
        }
        int k = j;
        while (k < end && t[k] == ' ') {
            k++;
        }
        kind = is_word(t + i, j - i, LITERALS) ? UI_SOURCE_KEYWORD
             : k < end && t[k] == '('          ? UI_SOURCE_NAME
                                               : UI_SOURCE_BINDING;
    } else if (isspace((unsigned char)c)) {
        return j;
    }
    paint(p, i, j, kind);
    return j;
}

static void paint_expression(painter *p, int start, int end)
{
    for (int i = start; i < end;) {
        i = paint_expression_token(p, i, end);
    }
}

/* Text with {{ bindings }} in it. */
static void paint_text(painter *p, int start, int end)
{
    const char *t = p->text;
    for (int i = start; i + 1 < end; i++) {
        if (t[i] != '{' || t[i + 1] != '{') {
            continue;
        }
        int close = i + 2;
        while (close + 1 < end && !(t[close] == '}' && t[close + 1] == '}')) {
            close++;
        }
        bool closed = close + 1 < end;
        paint(p, i, i + 2, UI_SOURCE_PUNCTUATION);
        paint_expression(p, i + 2, closed ? close : end);
        if (closed) {
            paint(p, close, close + 2, UI_SOURCE_PUNCTUATION);
        }
        i = closed ? close + 1 : end;
    }
}

bool ui_source_is_binding(const char *name, int length)
{
    static const char *const EXACT[] = {
        "data-if",      "data-when",       "data-for",
        "data-value",   "data-value-lazy", "data-checked",
        "data-visible", "data-rml",        NULL
    };
    static const char *const PREFIXES[] = {
        "data-event-",  "data-class-", "data-style-", "data-attr-",
        "data-attrif-", "data-alias-", NULL
    };
    if (is_word(name, length, EXACT)) {
        return true;
    }
    for (int i = 0; PREFIXES[i]; i++) {
        int prefix = (int)strlen(PREFIXES[i]);
        if (length > prefix && !strncmp(name, PREFIXES[i], (size_t)prefix)) {
            return true;
        }
    }
    return false;
}

static void paint_css_node(painter *p, TSNode node);

/* A style attribute's declarations, parsed as a rule's block. */
static void paint_style(painter *p, int start, int end)
{
    int length = end - start;
    char *wrapped = xmalloc((size_t)length + 4);
    snprintf(wrapped, (size_t)length + 4, "x{%.*s}", length, p->text + start);
    TSParser *parser = ts_parser_new();
    ts_parser_set_language(parser, tree_sitter_css());
    TSTree *tree = ts_parser_parse_string(parser, NULL, wrapped, (uint32_t)length + 3);
    painter inner = { p->source, wrapped, p->shift + start - 2, start + p->shift, end + p->shift };
    paint_css_node(&inner, ts_tree_root_node(tree));
    ts_tree_delete(tree);
    ts_parser_delete(parser);
    free(wrapped);
}

/* An attribute's value: bindings as expressions, style as declarations. */
static void paint_value(painter *p, TSNode value, TSNode name)
{
    int start = node_start(value), end = node_end(value);
    bool quoted = is_type(value, "quoted_attribute_value");
    if (quoted) {
        paint(p, start, start + 1, UI_SOURCE_STRING);
        paint(p, end - 1, end, UI_SOURCE_STRING);
        start++;
        end -= end > start;
    }
    const char *attribute = p->text + node_start(name);
    int length = node_end(name) - node_start(name);
    if (ui_source_is_binding(attribute, length)) {
        paint_expression(p, start, end);
    } else if (length == 5 && !strncmp(attribute, "style", 5)) {
        paint_style(p, start, end);
    } else {
        paint(p, start, end, UI_SOURCE_STRING);
    }
}

static void paint_rml_node(painter *p, TSNode node)
{
    const char *type = ts_node_type(node);
    uint32_t count = ts_node_child_count(node);
    if (!strcmp(type, "attribute")) {
        TSNode name = child_of_type(node, "attribute_name");
        for (uint32_t i = 0; i < count; i++) {
            TSNode child = ts_node_child(node, i);
            bool value = is_type(child, "attribute_value") ||
                         is_type(child, "quoted_attribute_value");
            if (value && !ts_node_is_null(name)) {
                paint_value(p, child, name);
            } else {
                paint_rml_node(p, child);
            }
        }
        return;
    }
    ui_source_kind kind = !strcmp(type, "tag_name") ||
                                  !strcmp(type, "erroneous_end_tag_name")
                            ? UI_SOURCE_TAG
                        : !strcmp(type, "attribute_name") ? UI_SOURCE_ATTRIBUTE
                        : !strcmp(type, "entity")         ? UI_SOURCE_NUMBER
                        : !strcmp(type, "comment")        ? UI_SOURCE_COMMENT
                        : !strcmp(type, "doctype")        ? UI_SOURCE_KEYWORD
                        : !ts_node_is_named(node) ? UI_SOURCE_PUNCTUATION
                                                  : UI_SOURCE_KIND_COUNT;
    if (!strcmp(type, "text")) {
        paint_text(p, node_start(node), node_end(node));
        return;
    }
    if (kind != UI_SOURCE_KIND_COUNT) {
        paint(p, node_start(node), node_end(node), kind);
        return;
    }
    for (uint32_t i = 0; i < count; i++) {
        paint_rml_node(p, ts_node_child(node, i));
    }
}

/* What a CSS node is as a whole, or KIND_COUNT to look at its children. */
static ui_source_kind css_kind(const painter *p, TSNode node)
{
    static const char *const KEYWORDS[] = { "at_keyword", "keyframes_name",
                                            "from",       "to",
                                            "important",  "nesting_selector",
                                            NULL };
    static const char *const NUMBERS[] = { "integer_value", "float_value", "color_value", NULL };
    const char *type = ts_node_type(node);
    int length = (int)strlen(type);
    const char *text = p->text + node_start(node);
    bool custom = !strncmp(text, "--", 2);
    const char *parent = parent_type(node);
    if (!strcmp(type, "comment") || !strcmp(type, "js_comment")) {
        return UI_SOURCE_COMMENT;
    }
    if (is_word(type, length, KEYWORDS) ||
        (!strcmp(type, "class_name") && !strcmp(parent, "pseudo_class_selector"))) {
        return UI_SOURCE_KEYWORD;
    }
    if (is_word(type, length, NUMBERS)) {
        return UI_SOURCE_NUMBER;
    }
    if (!strcmp(type, "class_name") || !strcmp(type, "id_name")) {
        return UI_SOURCE_SELECTOR;
    }
    if (!strcmp(type, "property_name") || !strcmp(type, "feature_name")) {
        return custom ? UI_SOURCE_VARIABLE : UI_SOURCE_PROPERTY;
    }
    if (!strcmp(type, "plain_value")) {
        return custom ? UI_SOURCE_VARIABLE : UI_SOURCE_STRING;
    }
    if (!strcmp(type, "tag_name")) {
        return UI_SOURCE_TAG;
    }
    if (!strcmp(type, "attribute_name")) {
        return UI_SOURCE_ATTRIBUTE;
    }
    if (!strcmp(type, "string_value")) {
        return UI_SOURCE_STRING;
    }
    if (!strcmp(type, "function_name")) {
        return UI_SOURCE_NAME;
    }
    if (ts_node_is_named(node) || ts_node_child_count(node)) {
        return UI_SOURCE_KIND_COUNT;
    }
    bool selector = !strcmp(parent, "class_selector") ||
                    !strcmp(parent, "id_selector");
    return type[0] == '@' || isalpha((unsigned char)type[0]) ? UI_SOURCE_KEYWORD
         : selector                                 ? UI_SOURCE_SELECTOR
         : !strcmp(parent, "pseudo_class_selector") ? UI_SOURCE_KEYWORD
                                                    : UI_SOURCE_PUNCTUATION;
}

static void paint_css_node(painter *p, TSNode node)
{
    ui_source_kind kind = css_kind(p, node);
    if (kind != UI_SOURCE_KIND_COUNT) {
        paint(p, node_start(node), node_end(node), kind);
        return;
    }
    uint32_t count = ts_node_child_count(node);
    for (uint32_t i = 0; i < count; i++) {
        paint_css_node(p, ts_node_child(node, i));
    }
}

static int span_order(const void *a, const void *b)
{
    const ui_source_span *x = a, *y = b;
    return (x->start > y->start) - (x->start < y->start);
}

static void paint_source(ui_source *source)
{
    painter p = { source, source->text, 0, 0, (int)strlen(source->text) };
    TSNode root = ts_tree_root_node(source->trees->tree);
    if (source->language == UI_SOURCE_RML) {
        paint_rml_node(&p, root);
    } else {
        paint_css_node(&p, root);
    }
}

/* Tree-sitter's HTML grammar reads `>` inside an RML text binding as markup.
 * Parse same-length placeholder text in place of complete bindings so text
 * nodes and offsets still address the original source, whose expressions
 * RmlUi validates. */
static char *rml_parse_text(const char *text)
{
    char *parsed = xstrdup(text);
    int length = (int)strlen(text);
    bool in_tag = false;
    char tag_quote = 0;
    for (int i = 0; i < length; i++) {
        if (!in_tag && i + 3 < length && !strncmp(text + i, "<!--", 4)) {
            const char *end = strstr(text + i + 4, "-->");
            if (end) {
                i = (int)(end - text) + 2;
                continue;
            }
        }
        if (!in_tag && text[i] == '<' && i + 1 < length &&
            (isalpha((unsigned char)text[i + 1]) || text[i + 1] == '/' ||
             text[i + 1] == '!')) {
            in_tag = true;
            continue;
        }
        if (in_tag) {
            if (tag_quote) {
                if (text[i] == tag_quote) {
                    tag_quote = 0;
                }
            } else if (text[i] == '\'' || text[i] == '"') {
                tag_quote = text[i];
            } else if (text[i] == '>') {
                in_tag = false;
            }
            continue;
        }
        if (i + 1 >= length || text[i] != '{' || text[i + 1] != '{') {
            continue;
        }
        char quote = 0;
        bool escape = false;
        int close = i + 2;
        for (; close + 1 < length; close++) {
            char c = text[close];
            if (quote) {
                if (escape) {
                    escape = false;
                } else if (c == '\\') {
                    escape = true;
                } else if (c == quote) {
                    quote = 0;
                }
            } else if (c == '\'' || c == '"') {
                quote = c;
            } else if (c == '}' && text[close + 1] == '}') {
                break;
            }
        }
        if (close + 1 >= length || text[close] != '}' || text[close + 1] != '}') {
            continue;
        }
        for (int at = i; at <= close + 1; at++) {
            if (parsed[at] != '\n' && parsed[at] != '\r') {
                parsed[at] = 'x';
            }
        }
        i = close + 1;
    }
    return parsed;
}

/* ---------- Outline ---------- */

static ui_source_attribute read_attribute(TSNode node)
{
    ui_source_attribute a = { .start = node_start(node),
                              .end = node_end(node),
                              .value_start = -1,
                              .value_end = -1 };
    uint32_t count = ts_node_child_count(node);
    for (uint32_t i = 0; i < count; i++) {
        TSNode child = ts_node_child(node, i);
        if (is_type(child, "attribute_name")) {
            a.name_start = node_start(child);
            a.name_end = node_end(child);
        } else if (is_type(child, "attribute_value")) {
            a.value_start = node_start(child);
            a.value_end = node_end(child);
        } else if (is_type(child, "quoted_attribute_value")) {
            TSNode last = ts_node_child(child, ts_node_child_count(child) - 1);
            bool closed = ts_node_child_count(child) > 1 && !ts_node_is_missing(last);
            a.value_start = node_start(child) + 1;
            a.value_end = node_end(child) - closed;
        }
    }
    return a;
}

/* Attribute `name` among the element's; NULL. */
static const ui_source_attribute *find_attribute(
    const char *text,
    const ui_source_element *e,
    const char *name
)
{
    size_t length = strlen(name);
    for (int i = 0; i < acount(e->attributes); i++) {
        const ui_source_attribute *a = &e->attributes[i];
        if ((size_t)(a->name_end - a->name_start) == length &&
            !strncmp(text + a->name_start, name, length)) {
            return a;
        }
    }
    return NULL;
}

static char *attribute_copy(const char *text, const ui_source_element *e, const char *name)
{
    const ui_source_attribute *a = find_attribute(text, e, name);
    return a && a->value_start >= 0 ? copy_range(text, a->value_start, a->value_end)
         : a ? xstrdup("")
             : NULL;
}

/* An element's open tag, attributes and close tag. */
static ui_source_element read_element(const ui_source *source, TSNode node, int parent)
{
    TSNode open = ts_node_child(node, 0);
    TSNode last = ts_node_child(node, ts_node_child_count(node) - 1);
    TSNode name = child_of_type(open, "tag_name");
    ui_source_element e = {
        .start = node_start(node),
        .end = node_end(node),
        .open_end = node_end(open),
        .close_start = node_end(node),
        .self_closing = is_type(open, "self_closing_tag"),
        .parent = parent,
    };
    if (!ts_node_is_null(name)) {
        snprintf(
            e.tag,
            sizeof e.tag,
            "%.*s",
            node_end(name) - node_start(name),
            source->text + node_start(name)
        );
    }
    int column;
    ui_source_position(source, e.start, &e.line, &column);
    if (is_type(last, "end_tag") && !ts_node_is_missing(last) &&
        ts_node_end_byte(last) > ts_node_start_byte(last)) {
        e.close_start = node_start(last);
    }
    uint32_t count = ts_node_child_count(open);
    for (uint32_t i = 0; i < count; i++) {
        TSNode child = ts_node_child(open, i);
        if (is_type(child, "attribute")) {
            apush(e.attributes, read_attribute(child));
        }
    }
    e.id = attribute_copy(source->text, &e, "id");
    e.classes = attribute_copy(source->text, &e, "class");
    return e;
}

static bool element_type(TSNode node)
{
    return is_type(node, "element") || is_type(node, "script_element") ||
           is_type(node, "style_element");
}

static void outline_rml(ui_source *source, TSNode node, int parent)
{
    uint32_t count = ts_node_child_count(node);
    for (uint32_t i = 0; i < count; i++) {
        TSNode child = ts_node_child(node, i);
        if (element_type(child)) {
            apush(source->elements, read_element(source, child, parent));
            outline_rml(source, child, acount(source->elements) - 1);
        } else if (ts_node_is_error(child)) {
            outline_rml(source, child, parent);
        }
    }
}

static ui_source_declaration read_declaration(TSNode node)
{
    ui_source_declaration d = { .start = node_start(node), .end = node_end(node) };
    int value_start = -1, value_end = -1;
    uint32_t count = ts_node_child_count(node);
    bool after_colon = false;
    for (uint32_t i = 0; i < count; i++) {
        TSNode child = ts_node_child(node, i);
        if (is_type(child, "property_name")) {
            d.property_start = node_start(child);
            d.property_end = node_end(child);
        } else if (is_type(child, ":") && !after_colon) {
            after_colon = true;
        } else if (after_colon && !is_type(child, ";") && !ts_node_is_missing(child)) {
            value_start = value_start < 0 ? node_start(child) : value_start;
            value_end = node_end(child);
        }
    }
    d.value_start = value_start < 0 ? d.end : value_start;
    d.value_end = value_start < 0 ? d.end : value_end;
    return d;
}

static void outline_rcss(ui_source *source, TSNode node, int parent);

/* A rule or at-rule: its selector or prelude, its declarations, and the
 * rules nested in its block. */
static void add_rule(ui_source *source, TSNode node, TSNode block, int parent)
{
    int start = node_start(node), prelude_end = node_start(block);
    int line, column;
    ui_source_position(source, start, &line, &column);
    ui_source_rule rule = {
        .selector = collapsed(source->text, start, prelude_end),
        .start = start,
        .end = node_end(node),
        .block_start = node_start(block),
        .parent = parent,
        .line = line,
    };
    uint32_t count = ts_node_child_count(block);
    for (uint32_t i = 0; i < count; i++) {
        TSNode child = ts_node_child(block, i);
        if (is_type(child, "declaration")) {
            apush(rule.declarations, read_declaration(child));
        }
    }
    apush(source->rules, rule);
    outline_rcss(source, block, acount(source->rules) - 1);
}

static void outline_rcss(ui_source *source, TSNode node, int parent)
{
    static const char *const BLOCKS[] = { "block", "keyframe_block_list", NULL };
    uint32_t count = ts_node_child_count(node);
    for (uint32_t i = 0; i < count; i++) {
        TSNode child = ts_node_child(node, i);
        if (ts_node_is_error(child)) {
            outline_rcss(source, child, parent);
            continue;
        }
        uint32_t parts = ts_node_child_count(child);
        TSNode block = { 0 };
        for (uint32_t k = 0; k < parts && ts_node_is_null(block); k++) {
            TSNode part = ts_node_child(child, k);
            const char *type = ts_node_type(part);
            if (is_word(type, (int)strlen(type), BLOCKS)) {
                block = part;
            }
        }
        if (!ts_node_is_null(block)) {
            add_rule(source, child, block, parent);
        }
    }
}

/* ---------- Diagnostics ---------- */

static void add_diagnostic(ui_source *source, int start, int end, const char *format, ...)
{
    ui_source_diagnostic d = { .start = start, .end = end };
    ui_source_position(source, start, &d.line, &d.column);
    va_list args;
    va_start(args, format);
    vsnprintf(d.message, sizeof d.message, format, args);
    va_end(args);
    apush(source->diagnostics, d);
}

/* The first line of [start, end) of the text, cut short. */
static void excerpt(const char *text, int start, int end, char *out, size_t size)
{
    int length = 0;
    while (start + length < end && text[start + length] != '\n' && length < 40) {
        length++;
    }
    snprintf(out, size, "%.*s%s", length, text + start, start + length < end ? "..." : "");
}

/* RCSS nests with Sass's `&`, which tree-sitter-css rejects after a
 * descendant combinator, as in `.dark &`. */
static bool nesting_only(TSNode error)
{
    uint32_t count = ts_node_named_child_count(error);
    for (uint32_t i = 0; i < count; i++) {
        if (!is_type(ts_node_named_child(error, i), "nesting_selector")) {
            return false;
        }
    }
    return count > 0;
}

/* The tag of the element holding `node`. */
static void holder_tag(const char *text, TSNode node, char *out, size_t size)
{
    for (TSNode p = ts_node_parent(node); !ts_node_is_null(p); p = ts_node_parent(p)) {
        TSNode name = element_type(p) ? child_of_type(ts_node_child(p, 0), "tag_name")
                                      : (TSNode){ 0 };
        if (!ts_node_is_null(name)) {
            snprintf(out, size, "%.*s", node_end(name) - node_start(name), text + node_start(name));
            return;
        }
    }
    snprintf(out, size, "element");
}

/* RML closes every element, as XML does, where HTML may leave it open. */
static void diagnose_unclosed(ui_source *source, TSNode element)
{
    TSNode open = ts_node_child(element, 0);
    TSNode last = ts_node_child(element, ts_node_child_count(element) - 1);
    if (is_type(open, "self_closing_tag") ||
        (is_type(last, "end_tag") && !ts_node_is_missing(last))) {
        return;
    }
    char tag[64];
    holder_tag(source->text, ts_node_child(open, 0), tag, sizeof tag);
    add_diagnostic(source, node_start(element), node_end(open), "<%s> is not closed", tag);
}

static void diagnose(ui_source *source, TSNode node)
{
    char text[64];
    int start = node_start(node), end = node_end(node);
    if (ts_node_is_missing(node)) {
        if (source->language == UI_SOURCE_RCSS ||
            !strstr(ts_node_type(node), "end_tag")) {
            add_diagnostic(source, start, start, "missing \"%s\"", ts_node_type(node));
        }
        return;
    }
    if (source->language == UI_SOURCE_RML && element_type(node)) {
        diagnose_unclosed(source, node);
    }
    if (ts_node_is_error(node) || is_type(node, "erroneous_end_tag")) {
        if (source->language == UI_SOURCE_RCSS && nesting_only(node)) {
            return;
        }
        excerpt(source->text, start, end, text, sizeof text);
        bool close = !strncmp(source->text + start, "</", 2);
        add_diagnostic(source, start, end, close ? "%s closes no open element" : "unexpected \"%s\"", text);
        return;
    }
    uint32_t count = ts_node_child_count(node);
    for (uint32_t i = 0; i < count; i++) {
        diagnose(source, ts_node_child(node, i));
    }
}

/* ---------- Parse ---------- */

ui_source *ui_source_parse(const char *text, ui_source_language language)
{
    ui_source *source = xcalloc(1, sizeof *source);
    source->text = xstrdup(text);
    source->language = language;
    source->trees = xcalloc(1, sizeof *source->trees);
    source->trees->lines = line_starts(source->text);
    TSParser *parser = ts_parser_new();
    ts_parser_set_language(parser, language == UI_SOURCE_RML ? tree_sitter_html() : tree_sitter_css());
    char *rml = language == UI_SOURCE_RML ? rml_parse_text(source->text) : NULL;
    const char *parse_text = rml ? rml : source->text;
    source->trees->tree = ts_parser_parse_string(parser, NULL, parse_text, (uint32_t)strlen(parse_text));
    free(rml);
    ts_parser_delete(parser);
    TSNode root = ts_tree_root_node(source->trees->tree);
    if (language == UI_SOURCE_RML) {
        outline_rml(source, root, -1);
    } else {
        outline_rcss(source, root, -1);
    }
    paint_source(source);
    if (acount(source->spans) > 1) {
        asort(source->spans, span_order);
    }
    diagnose(source, root);
    return source;
}

static void free_element(ui_source_element *e)
{
    free(e->id);
    free(e->classes);
    afree(e->attributes);
}

void ui_source_free(ui_source *source)
{
    if (!source) {
        return;
    }
    ts_tree_delete(source->trees->tree);
    afree(source->trees->lines);
    free(source->trees);
    for (int i = 0; i < acount(source->elements); i++) {
        free_element(&source->elements[i]);
    }
    for (int i = 0; i < acount(source->rules); i++) {
        free(source->rules[i].selector);
        afree(source->rules[i].declarations);
    }
    afree(source->spans);
    afree(source->elements);
    afree(source->rules);
    afree(source->diagnostics);
    free(source->text);
    free(source);
}

/* ---------- Queries ---------- */

int ui_source_element_at(const ui_source *source, int offset)
{
    int found = -1;
    for (int i = 0; i < acount(source->elements); i++) {
        if (source->elements[i].start <= offset && offset < source->elements[i].end) {
            found = i;
        }
    }
    return found;
}

char *ui_source_attribute_value(const ui_source *source, int element, const char *name)
{
    assert(
        source->language == UI_SOURCE_RML && element >= 0 &&
        element < acount(source->elements)
    );
    return attribute_copy(source->text, &source->elements[element], name);
}

/* ---------- Occurrences ---------- */

static void add_occurrence(
    ui_source_symbol **out,
    const char *text,
    ui_source_symbol_kind kind,
    int start,
    int end,
    bool definition
)
{
    ui_source_symbol o = { .kind = kind, .start = start, .end = end, .definition = definition };
    snprintf(o.name, sizeof o.name, "%.*s", end - start, text + start);
    apush(*out, o);
}

static bool class_char(char c)
{
    return isalnum((unsigned char)c) || c == '-' || c == '_';
}

/* Each class a class attribute's value names. */
static void class_occurrences(const char *text, const ui_source_attribute *a, ui_source_symbol **out)
{
    for (int i = a->value_start; i < a->value_end;) {
        int start = i;
        while (i < a->value_end && !isspace((unsigned char)text[i])) {
            i++;
        }
        if (i > start) {
            add_occurrence(out, text, UI_SYMBOL_CLASS, start, i, false);
        }
        i += i < a->value_end;
    }
}

/* Each var(--name) in [start, end) of the text. */
static void variable_occurrences(const char *text, int start, int end, ui_source_symbol **out)
{
    for (int i = start; i + 6 <= end; i++) {
        if (strncmp(text + i, "var(--", 6)) {
            continue;
        }
        int name = i + 4, stop = name;
        while (stop < end && class_char(text[stop])) {
            stop++;
        }
        add_occurrence(out, text, UI_SYMBOL_VARIABLE, name, stop, false);
        i = stop - 1;
    }
}

static void element_occurrences(const char *text, const ui_source_element *e, ui_source_symbol **out)
{
    static const char CLASS_BINDING[] = "data-class-";
    for (int i = 0; i < acount(e->attributes); i++) {
        const ui_source_attribute *a = &e->attributes[i];
        const char *name = text + a->name_start;
        int length = a->name_end - a->name_start;
        bool valued = a->value_start >= 0;
        if (valued && !strcmp(e->tag, "use") && length == 2 &&
            !strncmp(name, "ui", 2)) {
            add_occurrence(out, text, UI_SYMBOL_WIDGET, a->value_start, a->value_end, false);
        } else if (valued && length == 5 && !strncmp(name, "class", 5)) {
            class_occurrences(text, a, out);
        } else if (valued && length == 5 && !strncmp(name, "style", 5)) {
            variable_occurrences(text, a->value_start, a->value_end, out);
        } else if (length > (int)sizeof CLASS_BINDING - 1 && !strncmp(name, CLASS_BINDING, sizeof CLASS_BINDING - 1)) {
            add_occurrence(
                out,
                text,
                UI_SYMBOL_CLASS,
                a->name_start + (int)sizeof CLASS_BINDING - 1,
                a->name_end,
                false
            );
        }
    }
}

/* Each save.key the markup binds. */
static void save_occurrences(const char *text, ui_source_symbol **out)
{
    for (const char *p = text; (p = strstr(p, "save.")); p += 5) {
        if (p > text && (word_char(p[-1]) || p[-1] == '.')) {
            continue;
        }
        int start = (int)(p - text) + 5, end = start;
        while (word_char(text[end])) {
            end++;
        }
        if (end > start) {
            add_occurrence(out, text, UI_SYMBOL_SAVE_KEY, start, end, false);
        }
    }
}

static void css_occurrences(const char *source, TSNode node, ui_source_symbol **out)
{
    const char *type = ts_node_type(node);
    const char *text = source + node_start(node);
    const char *parent = parent_type(node);
    if (!strcmp(type, "class_name") && !strcmp(parent, "class_selector")) {
        add_occurrence(out, source, UI_SYMBOL_CLASS, node_start(node), node_end(node), true);
        return;
    }
    if ((!strcmp(type, "property_name") || !strcmp(type, "plain_value")) &&
        !strncmp(text, "--", 2)) {
        bool declared = !strcmp(type, "property_name");
        add_occurrence(out, source, UI_SYMBOL_VARIABLE, node_start(node), node_end(node), declared);
        return;
    }
    uint32_t count = ts_node_child_count(node);
    for (uint32_t i = 0; i < count; i++) {
        css_occurrences(source, ts_node_child(node, i), out);
    }
}

void ui_source_symbols(const ui_source *source, ui_source_symbol **out)
{
    if (source->language == UI_SOURCE_RCSS) {
        css_occurrences(source->text, ts_tree_root_node(source->trees->tree), out);
        return;
    }
    for (int i = 0; i < acount(source->elements); i++) {
        element_occurrences(source->text, &source->elements[i], out);
    }
    save_occurrences(source->text, out);
}
