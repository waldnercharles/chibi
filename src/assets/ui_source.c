/* The ui language service's parse: where a ui asset's blocks are in its
 * file, their tree-sitter trees, and the highlighting, outline and
 * diagnostics read from them. */
#include "assets/ui_source.h"

#include "assets/ui_document.h"
#include "assets/ui_source_internal.h"
#include "support/array.h"
#include "support/asset_text.h"
#include "support/xalloc.h"

#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const TSLanguage *tree_sitter_css();
const TSLanguage *tree_sitter_html();

/* The keys a ui asset's blocks hold, by language. */
static const char *const BLOCK_KEYS[] = { "rml", "rcss" };

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

/* ---------- Blocks ---------- */

static void map_push(block_map *m, char c, int offset)
{
    apush(m->value, c);
    apush(m->offsets, offset);
}

/* Ends the value, mapping its end one past its last byte. */
static void map_finish(block_map *m, int empty_offset)
{
    m->length = acount(m->value);
    apush(m->offsets, m->length ? m->offsets[m->length - 1] + 1 : empty_offset);
    apush(m->value, '\0');
}

static int leading_spaces(const char *text, int at, int end)
{
    int count = 0;
    while (at + count < end && text[at + count] == ' ') {
        count++;
    }
    return count;
}

/* The first line of a block's lines at `at` that holds more than spaces. */
static int first_indent(const char *text, int at)
{
    while (text[at]) {
        int end = line_end(text, at);
        int spaces = leading_spaces(text, at, end);
        if (at + spaces < end) {
            return spaces;
        }
        at = text[end] ? end + 1 : end;
    }
    return 0;
}

/* A literal block's lines, from the line after its header at `header`: the
 * `|` with its chomping and indentation indicators. */
static bool read_literal(const char *text, int header, block_map *m)
{
    int q = header + 1, indent = 0;
    char chomp = 'c';
    for (int i = 0; i < 2; i++) {
        if (text[q] == '-' || text[q] == '+') {
            chomp = text[q++];
        } else if (text[q] >= '1' && text[q] <= '9') {
            indent = text[q++] - '0';
        }
    }
    int header_end = line_end(text, header);
    int rest = q + leading_spaces(text, q, header_end);
    if (rest < header_end && text[rest] != '#') {
        return false;
    }
    int p = text[header_end] ? header_end + 1 : header_end;
    m->indent = indent ? indent : first_indent(text, p);
    int kept = 0; /* the value's length through its last line of content */
    m->scalar_end = header_end;
    while (m->indent && text[p]) {
        int end = line_end(text, p), spaces = leading_spaces(text, p, end);
        bool blank = p + spaces == end;
        if (!blank && spaces < m->indent) {
            break;
        }
        for (int k = p + m->indent; k < end; k++) {
            map_push(m, text[k], k);
        }
        map_push(m, '\n', end);
        if (!blank || spaces > m->indent) {
            kept = acount(m->value);
            m->scalar_end = end;
        }
        p = text[end] ? end + 1 : end;
    }
    asetlen(m->value, chomp == '+' ? acount(m->value) : kept - (chomp == '-' && kept));
    asetlen(m->offsets, acount(m->value));
    m->style = SCALAR_LITERAL;
    map_finish(m, p);
    /* A last line without a line break ends at the text's end. */
    int size = (int)strlen(text);
    m->offsets[m->length] = m->offsets[m->length] > size ? size
                                                         : m->offsets[m->length];
    return true;
}

static int hex_value(const char *p, int digits)
{
    int value = 0;
    for (int i = 0; i < digits; i++) {
        if (!isxdigit((unsigned char)p[i])) {
            return -1;
        }
        value = value * 16 + (isdigit((unsigned char)p[i])
                                  ? p[i] - '0'
                                  : (tolower((unsigned char)p[i]) - 'a' + 10));
    }
    return value;
}

/* Appends code point `value` as UTF-8, each byte at `offset`. */
static void map_code_point(block_map *m, int value, int offset)
{
    if (value < 0x80) {
        map_push(m, (char)value, offset);
        return;
    }
    if (value < 0x800) {
        map_push(m, (char)(0xc0 | (value >> 6)), offset);
    } else {
        map_push(m, (char)(0xe0 | (value >> 12)), offset);
        map_push(m, (char)(0x80 | ((value >> 6) & 0x3f)), offset);
    }
    map_push(m, (char)(0x80 | (value & 0x3f)), offset);
}

/* One escape of a double-quoted scalar at `p`; its length, or 0 when it is
 * one this reader leaves to YAML. */
static int read_escape(const char *text, int p, block_map *m)
{
    static const char SIMPLE[] = "\"\\/ntr", DECODED[] = "\"\\/\n\t\r";
    const char *simple = text[p + 1] ? strchr(SIMPLE, text[p + 1]) : NULL;
    if (simple) {
        map_push(m, DECODED[simple - SIMPLE], p);
        return 2;
    }
    int digits = text[p + 1] == 'x' ? 2 : text[p + 1] == 'u' ? 4 : 0;
    int value = digits ? hex_value(text + p + 2, digits) : -1;
    if (value < 0) {
        return 0;
    }
    map_code_point(m, value, p);
    return 2 + digits;
}

/* A quoted scalar that ends on its line; false for any other. */
static bool read_quoted(const char *text, int open, block_map *m)
{
    char quote = text[open];
    int p = open + 1;
    for (;;) {
        char c = text[p];
        if (!c || c == '\n') {
            return false;
        }
        if (c == quote && !(quote == '\'' && text[p + 1] == '\'')) {
            break;
        }
        int length = quote == '\'' ? 1 + (c == '\'')
                   : c == '\\'     ? read_escape(text, p, m)
                                   : 1;
        if (!length) {
            return false;
        }
        if (quote == '\'' || c != '\\') {
            map_push(m, c, p);
        }
        p += length;
    }
    m->style = SCALAR_INLINE;
    m->scalar_end = p + 1;
    map_finish(m, open + 1);
    return true;
}

/* A plain scalar on one line; false when it continues on the next. */
static bool read_plain(const char *text, int start, block_map *m)
{
    int end = line_end(text, start);
    for (int p = start; p < end; p++) {
        if (text[p] == '#' && p > start && text[p - 1] == ' ') {
            end = p;
        }
    }
    while (end > start && text[end - 1] == ' ') {
        end--;
    }
    int next = line_end(text, start);
    if (text[next] && text[next + 1] == ' ' && first_indent(text, next + 1)) {
        return false;
    }
    for (int p = start; p < end; p++) {
        map_push(m, text[p], p);
    }
    m->style = SCALAR_INLINE;
    m->scalar_end = end;
    map_finish(m, start);
    return true;
}

/* The end of the lines a value at `start` spans: up to the next line that
 * starts at the left margin. */
static int region_end(const char *text, int start)
{
    int end = line_end(text, start);
    while (text[end] && text[end + 1] &&
           (text[end + 1] == ' ' || text[end + 1] == '\n')) {
        end = line_end(text, end + 1);
    }
    return end;
}

/* A value this reader does not map, read by YAML: every position falls on
 * the scalar's start. */
static bool read_other(const char *text, const char *key, int start, block_map *m)
{
    char error[160];
    yyjson_doc *doc = asset_text_read(text, error, sizeof error);
    const char *value = yyjson_get_str(yyjson_obj_get(yyjson_doc_get_root(doc), key));
    for (const char *p = value; p && *p; p++) {
        map_push(m, *p, start);
    }
    yyjson_doc_free(doc);
    if (!value) {
        return false;
    }
    m->style = SCALAR_OTHER;
    m->scalar_end = region_end(text, start);
    map_finish(m, start);
    return true;
}

static void map_free(block_map *m);

static bool read_block(const char *text, int key_start, ui_source_language language, block_map *m)
{
    const char *key = BLOCK_KEYS[language];
    int p = key_start + (int)strlen(key) + 1;
    p += leading_spaces(text, p, line_end(text, p));
    *m = (block_map){ .language = language, .scalar_start = p };
    bool read = text[p] == '|'                    ? read_literal(text, p, m)
              : text[p] == '\'' || text[p] == '"' ? read_quoted(text, p, m)
              : text[p] && text[p] != '\n' && text[p] != '>' && text[p] != '#'
                  ? read_plain(text, p, m)
                  : false;
    if (read) {
        return true;
    }
    map_free(m);
    *m = (block_map){ .language = language, .scalar_start = p };
    return read_other(text, key, p, m);
}

/* The block key a line starts with, or -1. */
static int block_key(const char *line)
{
    for (int i = 0; i < 2; i++) {
        size_t length = strlen(BLOCK_KEYS[i]);
        if (strncmp(line, BLOCK_KEYS[i], length) || line[length] != ':') {
            continue;
        }
        char after = line[length + 1];
        if (after == ' ' || after == '\n' || after == '\r' || !after) {
            return i;
        }
    }
    return -1;
}

static void add_map(ui_source *source, block_map *m)
{
    int line, column;
    ui_source_position(source, m->offsets[0], &line, &column);
    apush(source->trees->maps, *m);
    apush(
        source->blocks,
        ((ui_source_block){ .language = m->language,
                            .start = m->offsets[0],
                            .end = m->offsets[m->length],
                            .line = line })
    );
}

static void locate_blocks(ui_source *source)
{
    const int *lines = source->trees->lines;
    bool seen[2] = { false, false };
    for (int i = 0; i < acount(lines); i++) {
        int key = block_key(source->text + lines[i]);
        block_map m;
        if (key < 0 || seen[key] || !read_block(source->text, lines[i], key, &m)) {
            continue;
        }
        seen[key] = true;
        add_map(source, &m);
    }
}

static void add_stylesheet(ui_source *source)
{
    block_map m = { .language = UI_SOURCE_RCSS, .style = SCALAR_LITERAL };
    int length = (int)strlen(source->text);
    for (int i = 0; i < length; i++) {
        map_push(&m, source->text[i], i);
    }
    m.scalar_end = length;
    map_finish(&m, 0);
    add_map(source, &m);
}

int ui_source_file_offset(const block_map *m, int at)
{
    assert(at >= 0 && at <= m->length);
    return m->offsets[at];
}

/* The file offset one past value byte `at - 1`, for a range's end. */
static int file_end(const block_map *m, int at)
{
    return at > 0 ? m->offsets[at - 1] + 1 : m->offsets[0];
}

const block_map *ui_source_map(const ui_source *source, ui_source_language language)
{
    for (int i = 0; i < acount(source->trees->maps); i++) {
        if (source->trees->maps[i].language == language) {
            return &source->trees->maps[i];
        }
    }
    return NULL;
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

/* Spans of text parsed apart from its block, as a style attribute is:
 * `shift` turns the parsed text's offsets into the block value's, and only
 * spans inside [low, high) of the value count. */
typedef struct painter
{
    ui_source *source;
    const block_map *map;
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
    apush(
        p->source->spans,
        ((ui_source_span){ p->map->offsets[start], file_end(p->map, end), kind })
    );
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
    static const char *const EXACT[] = { "data-if",      "data-for",
                                         "data-value",   "data-value-lazy",
                                         "data-checked", "data-visible",
                                         "data-rml",     NULL };
    static const char *const PREFIXES[] = { "data-event-", "data-class-",
                                            "data-style-", "data-attr-",
                                            "data-alias-", NULL };
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
    painter inner = { p->source,        p->map,
                      wrapped,          p->shift + start - 2,
                      start + p->shift, end + p->shift };
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

/* The key, colon and indicator that start an asset's block. */
static void paint_block_key(ui_source *source, const block_map *m)
{
    int start = m->scalar_start;
    while (start > 0 && source->text[start - 1] != '\n') {
        start--;
    }
    int key = (int)strlen(BLOCK_KEYS[m->language]);
    apush(source->spans, ((ui_source_span){ start, start + key, UI_SOURCE_NAME }));
    apush(source->spans, ((ui_source_span){ start + key, start + key + 1, UI_SOURCE_PUNCTUATION }));
    if (m->style == SCALAR_LITERAL && source->text[m->scalar_start] == '|') {
        int end = m->scalar_start + 1;
        while (source->text[end] == '-' || source->text[end] == '+' ||
               isdigit((unsigned char)source->text[end])) {
            end++;
        }
        apush(source->spans, ((ui_source_span){ m->scalar_start, end, UI_SOURCE_PUNCTUATION }));
    }
}

static int span_order(const void *a, const void *b)
{
    const ui_source_span *x = a, *y = b;
    return (x->start > y->start) - (x->start < y->start);
}

static void paint_block(ui_source *source, const block_map *m, bool stylesheet)
{
    if (!stylesheet) {
        paint_block_key(source, m);
    }
    if (m->style == SCALAR_OTHER) {
        return;
    }
    painter p = { source, m, m->value, 0, 0, m->length };
    TSNode root = ts_tree_root_node(m->tree);
    if (m->language == UI_SOURCE_RML) {
        paint_rml_node(&p, root);
    } else {
        paint_css_node(&p, root);
    }
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

/* The value of attribute `name` among the element's, in the value; NULL. */
static const ui_source_attribute *find_attribute(
    const block_map *m,
    const ui_source_element *e,
    const char *name
)
{
    size_t length = strlen(name);
    for (int i = 0; i < acount(e->attributes); i++) {
        const ui_source_attribute *a = &e->attributes[i];
        if ((size_t)(a->name_end - a->name_start) == length &&
            !strncmp(m->value + a->name_start, name, length)) {
            return a;
        }
    }
    return NULL;
}

static char *attribute_copy(const block_map *m, const ui_source_element *e, const char *name)
{
    const ui_source_attribute *a = find_attribute(m, e, name);
    return a && a->value_start >= 0
             ? copy_range(m->value, a->value_start, a->value_end)
         : a ? xstrdup("")
             : NULL;
}

/* An element's open tag, attributes and close tag, in the value. */
static ui_source_element read_element(const block_map *m, TSNode node, int parent)
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
        snprintf(e.tag, sizeof e.tag, "%.*s", node_end(name) - node_start(name), m->value + node_start(name));
    }
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
    e.id = attribute_copy(m, &e, "id");
    e.classes = attribute_copy(m, &e, "class");
    return e;
}

static bool element_type(TSNode node)
{
    return is_type(node, "element") || is_type(node, "script_element") ||
           is_type(node, "style_element");
}

static void outline_rml(block_map *m, TSNode node, int parent)
{
    uint32_t count = ts_node_child_count(node);
    for (uint32_t i = 0; i < count; i++) {
        TSNode child = ts_node_child(node, i);
        if (element_type(child)) {
            apush(m->elements, read_element(m, child, parent));
            outline_rml(m, child, acount(m->elements) - 1);
        } else if (ts_node_is_error(child)) {
            outline_rml(m, child, parent);
        }
    }
}

/* The element with file offsets, owning copies of its strings. */
static ui_source_element mapped_element(const ui_source *source, const block_map *m, const ui_source_element *e)
{
    int line, column;
    ui_source_position(source, m->offsets[e->start], &line, &column);
    ui_source_element out = *e;
    out.start = m->offsets[e->start];
    out.end = file_end(m, e->end);
    out.open_end = file_end(m, e->open_end);
    out.close_start = e->close_start == e->end ? out.end : m->offsets[e->close_start];
    out.line = line;
    out.id = e->id ? xstrdup(e->id) : NULL;
    out.classes = e->classes ? xstrdup(e->classes) : NULL;
    out.attributes = NULL;
    for (int i = 0; i < acount(e->attributes); i++) {
        ui_source_attribute a = e->attributes[i];
        bool valued = a.value_start >= 0;
        apush(
            out.attributes,
            ((ui_source_attribute){
                .start = m->offsets[a.start],
                .end = file_end(m, a.end),
                .name_start = m->offsets[a.name_start],
                .name_end = file_end(m, a.name_end),
                .value_start = valued ? m->offsets[a.value_start] : -1,
                .value_end = valued ? (a.value_end > a.value_start
                                           ? file_end(m, a.value_end)
                                           : m->offsets[a.value_start])
                                    : -1 })
        );
    }
    return out;
}

static ui_source_declaration read_declaration(const block_map *m, TSNode node)
{
    ui_source_declaration d = { .start = m->offsets[node_start(node)],
                                .end = file_end(m, node_end(node)) };
    int value_start = -1, value_end = -1;
    uint32_t count = ts_node_child_count(node);
    bool after_colon = false;
    for (uint32_t i = 0; i < count; i++) {
        TSNode child = ts_node_child(node, i);
        if (is_type(child, "property_name")) {
            d.property_start = m->offsets[node_start(child)];
            d.property_end = file_end(m, node_end(child));
        } else if (is_type(child, ":") && !after_colon) {
            after_colon = true;
        } else if (after_colon && !is_type(child, ";") && !ts_node_is_missing(child)) {
            value_start = value_start < 0 ? node_start(child) : value_start;
            value_end = node_end(child);
        }
    }
    d.value_start = value_start < 0 ? d.end : m->offsets[value_start];
    d.value_end = value_start < 0 ? d.end : file_end(m, value_end);
    return d;
}

static void outline_rcss(ui_source *source, const block_map *m, TSNode node, int parent);

/* A rule or at-rule: its selector or prelude, its declarations, and the
 * rules nested in its block. */
static void add_rule(ui_source *source, const block_map *m, TSNode node, TSNode block, int parent)
{
    int start = node_start(node), prelude_end = node_start(block);
    int line, column;
    ui_source_position(source, m->offsets[start], &line, &column);
    ui_source_rule rule = {
        .selector = collapsed(m->value, start, prelude_end),
        .start = m->offsets[start],
        .end = file_end(m, node_end(node)),
        .block_start = m->offsets[node_start(block)],
        .parent = parent,
        .line = line,
    };
    uint32_t count = ts_node_child_count(block);
    for (uint32_t i = 0; i < count; i++) {
        TSNode child = ts_node_child(block, i);
        if (is_type(child, "declaration")) {
            apush(rule.declarations, read_declaration(m, child));
        }
    }
    apush(source->rules, rule);
    outline_rcss(source, m, block, acount(source->rules) - 1);
}

static void outline_rcss(ui_source *source, const block_map *m, TSNode node, int parent)
{
    static const char *const BLOCKS[] = { "block", "keyframe_block_list", NULL };
    uint32_t count = ts_node_child_count(node);
    for (uint32_t i = 0; i < count; i++) {
        TSNode child = ts_node_child(node, i);
        if (ts_node_is_error(child)) {
            outline_rcss(source, m, child, parent);
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
            add_rule(source, m, child, block, parent);
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

/* The first line of [start, end) of the value, cut short. */
static void excerpt(const block_map *m, int start, int end, char *out, size_t size)
{
    int length = 0;
    while (start + length < end && m->value[start + length] != '\n' && length < 40) {
        length++;
    }
    snprintf(out, size, "%.*s%s", length, m->value + start, start + length < end ? "..." : "");
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
static void holder_tag(const block_map *m, TSNode node, char *out, size_t size)
{
    for (TSNode p = ts_node_parent(node); !ts_node_is_null(p); p = ts_node_parent(p)) {
        TSNode name = element_type(p) ? child_of_type(ts_node_child(p, 0), "tag_name")
                                      : (TSNode){ 0 };
        if (!ts_node_is_null(name)) {
            snprintf(out, size, "%.*s", node_end(name) - node_start(name), m->value + node_start(name));
            return;
        }
    }
    snprintf(out, size, "element");
}

/* RML closes every element, as XML does, where HTML may leave it open. */
static void diagnose_unclosed(ui_source *source, const block_map *m, TSNode element)
{
    TSNode open = ts_node_child(element, 0);
    TSNode last = ts_node_child(element, ts_node_child_count(element) - 1);
    if (is_type(open, "self_closing_tag") ||
        (is_type(last, "end_tag") && !ts_node_is_missing(last))) {
        return;
    }
    char tag[64];
    holder_tag(m, ts_node_child(open, 0), tag, sizeof tag);
    add_diagnostic(source, m->offsets[node_start(element)], file_end(m, node_end(open)), "<%s> is not closed", tag);
}

static void diagnose(ui_source *source, const block_map *m, TSNode node)
{
    char text[64];
    int start = node_start(node), end = node_end(node);
    if (ts_node_is_missing(node)) {
        if (m->language == UI_SOURCE_RCSS || !strstr(ts_node_type(node), "end_tag")) {
            int at = m->offsets[node_start(node)];
            add_diagnostic(source, at, at, "missing \"%s\"", ts_node_type(node));
        }
        return;
    }
    if (m->language == UI_SOURCE_RML && element_type(node)) {
        diagnose_unclosed(source, m, node);
    }
    if (ts_node_is_error(node) || is_type(node, "erroneous_end_tag")) {
        if (m->language == UI_SOURCE_RCSS && nesting_only(node)) {
            return;
        }
        excerpt(m, start, end, text, sizeof text);
        bool close = !strncmp(m->value + start, "</", 2);
        add_diagnostic(
            source,
            m->offsets[start],
            file_end(m, end),
            close ? "%s closes no open element" : "unexpected \"%s\"",
            text
        );
        return;
    }
    uint32_t count = ts_node_child_count(node);
    for (uint32_t i = 0; i < count; i++) {
        diagnose(source, m, ts_node_child(node, i));
    }
}

/* What ui_document_read rejects that the grammars accept, as an unknown
 * key; placed at the line YAML names, or the first. */
static void diagnose_asset(ui_source *source)
{
    char error[200];
    ui_document document = { 0 };
    bool read = ui_document_read(source->text, NULL, NULL, &document, error, sizeof error);
    ui_document_free(&document);
    if (read) {
        return;
    }
    int line = 1, column = 1, skip = 0;
    if (sscanf(error, "line %d, column %d: %n", &line, &column, &skip) < 2) {
        skip = 0;
    }
    int at = ui_source_offset(source, line, column);
    add_diagnostic(source, at, at, "%s", error + skip);
}

/* ---------- Parse ---------- */

static void parse_block(ui_source *source, block_map *m, bool stylesheet)
{
    TSParser *parser = ts_parser_new();
    ts_parser_set_language(parser, m->language == UI_SOURCE_RML ? tree_sitter_html() : tree_sitter_css());
    m->tree = ts_parser_parse_string(parser, NULL, m->value, (uint32_t)m->length);
    ts_parser_delete(parser);
    TSNode root = ts_tree_root_node(m->tree);
    if (m->language == UI_SOURCE_RML) {
        outline_rml(m, root, -1);
        for (int i = 0; i < acount(m->elements); i++) {
            apush(source->elements, mapped_element(source, m, &m->elements[i]));
        }
    } else {
        outline_rcss(source, m, root, -1);
    }
    paint_block(source, m, stylesheet);
    if (m->style != SCALAR_OTHER) {
        diagnose(source, m, root);
    }
}

ui_source *ui_source_parse(const char *text, bool stylesheet)
{
    ui_source *source = xcalloc(1, sizeof *source);
    source->text = xstrdup(text);
    source->trees = xcalloc(1, sizeof *source->trees);
    source->trees->lines = line_starts(source->text);
    source->trees->stylesheet = stylesheet;
    if (stylesheet) {
        add_stylesheet(source);
    } else {
        locate_blocks(source);
    }
    for (int i = 0; i < acount(source->trees->maps); i++) {
        parse_block(source, &source->trees->maps[i], stylesheet);
    }
    qsort(source->spans, (size_t)acount(source->spans), sizeof *source->spans, span_order);
    if (!stylesheet && !acount(source->diagnostics)) {
        diagnose_asset(source);
    }
    return source;
}

static void free_element(ui_source_element *e)
{
    free(e->id);
    free(e->classes);
    afree(e->attributes);
}

static void map_free(block_map *m)
{
    afree(m->value);
    afree(m->offsets);
    if (m->tree) {
        ts_tree_delete(m->tree);
    }
    for (int i = 0; i < acount(m->elements); i++) {
        free_element(&m->elements[i]);
    }
    afree(m->elements);
}

void ui_source_free(ui_source *source)
{
    if (!source) {
        return;
    }
    for (int i = 0; i < acount(source->trees->maps); i++) {
        map_free(&source->trees->maps[i]);
    }
    afree(source->trees->maps);
    afree(source->trees->lines);
    free(source->trees);
    for (int i = 0; i < acount(source->elements); i++) {
        free_element(&source->elements[i]);
    }
    for (int i = 0; i < acount(source->rules); i++) {
        free(source->rules[i].selector);
        afree(source->rules[i].declarations);
    }
    afree(source->blocks);
    afree(source->spans);
    afree(source->elements);
    afree(source->rules);
    afree(source->diagnostics);
    free(source->text);
    free(source);
}

/* ---------- Queries ---------- */

const ui_source_block *ui_source_block_at(const ui_source *source, int offset)
{
    for (int i = 0; i < acount(source->blocks); i++) {
        if (source->blocks[i].start <= offset && offset <= source->blocks[i].end) {
            return &source->blocks[i];
        }
    }
    return NULL;
}

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
    const block_map *m = ui_source_map(source, UI_SOURCE_RML);
    assert(m && element >= 0 && element < acount(m->elements));
    return attribute_copy(m, &m->elements[element], name);
}

/* ---------- Occurrences ---------- */

static void add_occurrence(occurrence **out, const block_map *m, int kind, int start, int end, bool definition)
{
    occurrence o = { .kind = kind,
                     .start = m->offsets[start],
                     .end = file_end(m, end),
                     .definition = definition };
    snprintf(o.name, sizeof o.name, "%.*s", end - start, m->value + start);
    apush(*out, o);
}

static bool class_char(char c)
{
    return isalnum((unsigned char)c) || c == '-' || c == '_';
}

/* Each class a class attribute's value names. */
static void class_occurrences(const block_map *m, const ui_source_attribute *a, occurrence **out)
{
    for (int i = a->value_start; i < a->value_end;) {
        int start = i;
        while (i < a->value_end && !isspace((unsigned char)m->value[i])) {
            i++;
        }
        if (i > start) {
            add_occurrence(out, m, UI_SYMBOL_CLASS, start, i, false);
        }
        i += i < a->value_end;
    }
}

/* Each var(--name) in [start, end) of the value. */
static void variable_occurrences(const block_map *m, int start, int end, occurrence **out)
{
    for (int i = start; i + 6 <= end; i++) {
        if (strncmp(m->value + i, "var(--", 6)) {
            continue;
        }
        int name = i + 4, stop = name;
        while (stop < end && class_char(m->value[stop])) {
            stop++;
        }
        add_occurrence(out, m, UI_SYMBOL_VARIABLE, name, stop, false);
        i = stop - 1;
    }
}

static void element_occurrences(const block_map *m, const ui_source_element *e, occurrence **out)
{
    static const char CLASS_BINDING[] = "data-class-";
    for (int i = 0; i < acount(e->attributes); i++) {
        const ui_source_attribute *a = &e->attributes[i];
        const char *name = m->value + a->name_start;
        int length = a->name_end - a->name_start;
        bool valued = a->value_start >= 0;
        if (valued && !strcmp(e->tag, "use") && length == 2 &&
            !strncmp(name, "ui", 2)) {
            add_occurrence(out, m, UI_SYMBOL_WIDGET, a->value_start, a->value_end, false);
        } else if (valued && length == 5 && !strncmp(name, "class", 5)) {
            class_occurrences(m, a, out);
        } else if (valued && length == 5 && !strncmp(name, "style", 5)) {
            variable_occurrences(m, a->value_start, a->value_end, out);
        } else if (length > (int)sizeof CLASS_BINDING - 1 && !strncmp(name, CLASS_BINDING, sizeof CLASS_BINDING - 1)) {
            add_occurrence(
                out,
                m,
                UI_SYMBOL_CLASS,
                a->name_start + (int)sizeof CLASS_BINDING - 1,
                a->name_end,
                false
            );
        }
    }
}

/* Each save.key the markup binds. */
static void save_occurrences(const block_map *m, occurrence **out)
{
    for (const char *p = m->value; (p = strstr(p, "save.")); p += 5) {
        if (p > m->value && (word_char(p[-1]) || p[-1] == '.')) {
            continue;
        }
        int start = (int)(p - m->value) + 5, end = start;
        while (word_char(m->value[end])) {
            end++;
        }
        if (end > start) {
            add_occurrence(out, m, UI_SYMBOL_SAVE_KEY, start, end, false);
        }
    }
}

static void css_occurrences(const block_map *m, TSNode node, occurrence **out)
{
    const char *type = ts_node_type(node);
    const char *text = m->value + node_start(node);
    const char *parent = parent_type(node);
    if (!strcmp(type, "class_name") && !strcmp(parent, "class_selector")) {
        add_occurrence(out, m, UI_SYMBOL_CLASS, node_start(node), node_end(node), true);
        return;
    }
    if ((!strcmp(type, "property_name") || !strcmp(type, "plain_value")) &&
        !strncmp(text, "--", 2)) {
        bool declared = !strcmp(type, "property_name");
        add_occurrence(out, m, UI_SYMBOL_VARIABLE, node_start(node), node_end(node), declared);
        return;
    }
    uint32_t count = ts_node_child_count(node);
    for (uint32_t i = 0; i < count; i++) {
        css_occurrences(m, ts_node_child(node, i), out);
    }
}

void ui_source_occurrences(const ui_source *source, occurrence **out)
{
    for (int b = 0; b < acount(source->trees->maps); b++) {
        const block_map *m = &source->trees->maps[b];
        if (m->style == SCALAR_OTHER) {
            continue;
        }
        if (m->language == UI_SOURCE_RCSS) {
            css_occurrences(m, ts_tree_root_node(m->tree), out);
            continue;
        }
        for (int i = 0; i < acount(m->elements); i++) {
            element_occurrences(m, &m->elements[i], out);
        }
        save_occurrences(m, out);
    }
}
