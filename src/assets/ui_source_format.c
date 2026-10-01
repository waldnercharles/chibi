/* The editor's opinionated formatter for RML and RCSS. RML keeps every byte
 * between tags and only lays out long attribute lists; those bytes can be
 * visible text in RmlUi. RCSS changes trivia around syntax nodes while
 * preserving selectors, declaration values, comments and bindings. */
#include "assets/ui_source.h"
#include "assets/ui_source_internal.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <ctype.h>
#include <string.h>

const TSLanguage *tree_sitter_css();
const TSLanguage *tree_sitter_html();

typedef struct writer
{
    const char *text;
    char *out;
} writer;

static int start_of(TSNode node)
{
    return (int)ts_node_start_byte(node);
}

static int end_of(TSNode node)
{
    return (int)ts_node_end_byte(node);
}

static bool type_is(TSNode node, const char *type)
{
    return !strcmp(ts_node_type(node), type);
}

static void put(writer *w, char c)
{
    apush(w->out, c);
}

static void put_range(writer *w, int start, int end)
{
    for (int i = start; i < end; i++) {
        put(w, w->text[i]);
    }
}

static void indent(writer *w, int depth)
{
    for (int i = 0; i < depth * 2; i++) {
        put(w, ' ');
    }
}

static void trim(const char *text, int *start, int *end)
{
    while (*start < *end && isspace((unsigned char)text[*start])) {
        (*start)++;
    }
    while (*end > *start && isspace((unsigned char)text[*end - 1])) {
        (*end)--;
    }
}

/* The start tag's syntax whitespace has no rendered text, so long tags can
 * be split without changing quoted values or RmlUi's mixed content. */
static void format_rml(writer *w, const ui_source *source)
{
    int at = 0, length = (int)strlen(w->text);
    for (int i = 0; i < acount(source->elements); i++) {
        const ui_source_element *element = &source->elements[i];
        int attrs = acount(element->attributes);
        if (attrs < 2 || element->open_end - element->start < 100) {
            continue;
        }
        put_range(w, at, element->start);
        int name_end = element->start + 1 + (int)strlen(element->tag);
        put_range(w, element->start, name_end);
        for (int a = 0; a < attrs; a++) {
            put(w, '\n');
            indent(w, 2);
            const ui_source_attribute *attribute = &element->attributes[a];
            put_range(w, attribute->start, attribute->end);
        }
        put_range(w, element->attributes[attrs - 1].end, element->open_end);
        at = element->open_end;
    }
    put_range(w, at, length);
}

static TSNode block_of(TSNode node)
{
    uint32_t count = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < count; i++) {
        TSNode child = ts_node_named_child(node, i);
        if (type_is(child, "block") || type_is(child, "keyframe_block_list")) {
            return child;
        }
    }
    return (TSNode){ 0 };
}

static int declaration_colon(writer *w, int start, int end)
{
    char quote = 0;
    bool comment = false, escape = false;
    for (int i = start; i < end; i++) {
        char c = w->text[i], next = i + 1 < end ? w->text[i + 1] : '\0';
        if (comment) {
            if (c == '*' && next == '/') {
                comment = false;
                i++;
            }
            continue;
        }
        if (quote) {
            if (escape) {
                escape = false;
            } else if (c == '\\') {
                escape = true;
            } else if (c == quote) {
                quote = 0;
            }
            continue;
        }
        if (c == '/' && next == '*') {
            comment = true;
            i++;
            continue;
        }
        if (c == '\'' || c == '"') {
            quote = c;
            continue;
        }
        if (c == ':') {
            return i;
        }
    }
    return -1;
}

static bool custom_property(writer *w, TSNode declaration)
{
    uint32_t count = ts_node_child_count(declaration);
    for (uint32_t i = 0; i < count; i++) {
        TSNode child = ts_node_child(declaration, i);
        if (!type_is(child, "property_name")) {
            continue;
        }
        int start = start_of(child);
        return end_of(child) - start >= 2 && w->text[start] == '-' &&
               w->text[start + 1] == '-';
    }
    return false;
}

static void format_css_sequence(writer *w, TSNode parent, int depth, bool top_level);

static void format_css_declaration(writer *w, TSNode declaration, int depth)
{
    int start = start_of(declaration), end = end_of(declaration);
    int colon = declaration_colon(w, start, end);
    if (colon < 0 || memchr(w->text + start, '{', (size_t)(end - start))) {
        indent(w, depth);
        trim(w->text, &start, &end);
        put_range(w, start, end);
        put(w, '\n');
        return;
    }

    int property_end = colon, value_start = colon + 1, value_end = end;
    trim(w->text, &start, &property_end);
    bool semicolon = value_end > value_start && w->text[value_end - 1] == ';';
    if (semicolon) {
        value_end--;
    }
    bool custom = custom_property(w, declaration);
    if (custom) {
        indent(w, depth);
        put_range(w, start, property_end);
        put(w, ':');
        put_range(w, colon + 1, value_end);
        if (semicolon) {
            put(w, ';');
        }
        put(w, '\n');
        return;
    }
    trim(w->text, &value_start, &value_end);
    indent(w, depth);
    put_range(w, start, property_end);
    put(w, ':');
    if (value_start < value_end) {
        put(w, ' ');
        put_range(w, value_start, value_end);
    }
    if (semicolon) {
        put(w, ';');
    }
    put(w, '\n');
}

static void format_css_rule(writer *w, TSNode rule, int depth, TSNode block)
{
    int start = start_of(rule), end = start_of(block);
    trim(w->text, &start, &end);
    indent(w, depth);
    put_range(w, start, end);
    put(w, ' ');
    put(w, '{');
    put(w, '\n');
    format_css_sequence(w, block, depth + 1, false);
    indent(w, depth);
    put(w, '}');
}

static void format_css_node(writer *w, TSNode node, int depth)
{
    if (type_is(node, "declaration")) {
        format_css_declaration(w, node, depth);
        return;
    }
    int start = start_of(node), end = end_of(node);
    TSNode block = block_of(node);
    if (!ts_node_is_null(block)) {
        format_css_rule(w, node, depth, block);
        put(w, '\n');
        return;
    }
    trim(w->text, &start, &end);
    if (start == end) {
        return;
    }
    indent(w, depth);
    put_range(w, start, end);
    put(w, '\n');
}

static void format_css_sequence(writer *w, TSNode parent, int depth, bool top_level)
{
    uint32_t count = ts_node_named_child_count(parent);
    bool emitted = false;
    for (uint32_t i = 0; i < count; i++) {
        TSNode child = ts_node_named_child(parent, i);
        if (emitted && top_level && !type_is(child, "comment")) {
            put(w, '\n');
        }
        format_css_node(w, child, depth);
        emitted = true;
    }
}

char *ui_source_format(const char *text, ui_source_language language)
{
    ui_source *source = ui_source_parse(text, language);
    if (acount(source->diagnostics)) {
        ui_source_free(source);
        return xstrdup(text);
    }

    writer w = { .text = text };
    if (language == UI_SOURCE_RML) {
        format_rml(&w, source);
    } else {
        format_css_sequence(&w, ts_tree_root_node(source->trees->tree), 0, true);
        if (acount(w.out) && w.out[acount(w.out) - 1] != '\n') {
            put(&w, '\n');
        }
    }
    put(&w, '\0');
    ui_source_free(source);
    char *formatted = w.out ? xstrdup(w.out) : xstrdup("");
    afree(w.out);
    return formatted;
}
