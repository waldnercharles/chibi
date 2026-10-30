#include "assets/ui_source_internal.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define RESOURCE_TEXT_MAX (8 * 1024 * 1024)
typedef struct
{
    ui_source *source;
    ui_source_resource *items;
    size_t nodes;
    const char *error;
} collector;
void ui_source_resources_free(ui_source_resource *items)
{
    for (int i = 0; i < acount(items); i++) {
        free(items[i].text);
    }
    afree(items);
}
static bool type(TSNode n, const char *name)
{
    return !strcmp(ts_node_type(n), name);
}
static char *text(const char *source, int a, int b)
{
    while (a < b && isspace((unsigned char)source[a])) {
        a++;
    }
    while (b > a && isspace((unsigned char)source[b - 1])) {
        b--;
    }
    if (b - a >= 2 && ((source[a] == '"' && source[b - 1] == '"') ||
                       (source[a] == '\'' && source[b - 1] == '\''))) {
        a++;
        b--;
    }
    char *value = xmalloc((size_t)(b - a) + 1);
    memcpy(value, source + a, (size_t)(b - a));
    value[b - a] = 0;
    return value;
}
static void add(collector *c, ui_source_resource_kind kind, char *value)
{
    if (!value || !*value || strchr(value, '\\') || strstr(value, "{{") ||
        strstr(value, "}}")) {
        c->error = "UI resource needs a static unescaped name";
        free(value);
        return;
    }
    if (kind == UI_SOURCE_RESOURCE_FONT_FAMILY &&
        (!strcmp(value, "inherit") || !strcmp(value, "initial") ||
         !strcmp(value, "unset") || !strcmp(value, "sans-serif") ||
         !strcmp(value, "serif") || !strcmp(value, "monospace"))) {
        free(value);
        return;
    }
    for (int i = 0; i < acount(c->items); i++) {
        if (c->items[i].kind == kind && !strcmp(c->items[i].text, value)) {
            free(value);
            return;
        }
    }
    if (acount(c->items) >= 1024) {
        c->error = "UI resource count exceeds limit";
        free(value);
        return;
    }
    apush(c->items, ((ui_source_resource){ kind, value }));
}
static bool bounded(collector *c, int depth)
{
    if (c->error) {
        return false;
    }
    if (depth > 64 || ++c->nodes > 1000000) {
        c->error = "UI resource syntax traversal exceeds limit";
        return false;
    }
    return true;
}
static TSNode child(TSNode n, const char *name)
{
    for (uint32_t i = 0; i < ts_node_named_child_count(n); i++) {
        TSNode x = ts_node_named_child(n, i);
        if (type(x, name)) {
            return x;
        }
    }
    return (TSNode){ 0 };
}
static void families(collector *, TSNode, int, bool);
static void variable(collector *c, TSNode call, int depth)
{
    TSNode args = child(call, "arguments");
    TSNode first = ts_node_named_child(args, 0);
    if (!type(first, "plain_value")) {
        c->error = "Font variable needs a declared static name";
        return;
    }
    char *name = text(c->source->text, (int)ts_node_start_byte(first), (int)ts_node_end_byte(first));
    bool found = false;
    for (int i = 0; i < acount(c->source->rules) && !c->error; i++) {
        for (int j = 0; j < acount(c->source->rules[i].declarations) && !c->error; j++) {
            const ui_source_declaration *d = &c->source->rules[i].declarations[j];
            char *key = text(c->source->text, d->property_start, d->property_end);
            bool same = !strcmp(key, name);
            free(key);
            if (!same) {
                continue;
            }
            TSNode root = ts_tree_root_node(c->source->trees->tree);
            TSNode declaration = ts_node_named_descendant_for_byte_range(
                root,
                (uint32_t)d->start,
                (uint32_t)d->end
            );
            found = true;
            families(c, declaration, depth + 1, false);
        }
    }
    if (ts_node_named_child_count(args) > 1) {
        found = true;
        families(c, args, depth + 1, true);
    }
    if (!found && !c->error) {
        c->error = "Font variable has no statically known declaration or "
                   "fallback";
    }
    free(name);
}
static void families(collector *c, TSNode n, int depth, bool skip_first)
{
    if (!bounded(c, depth)) {
        return;
    }
    int start = -1, end = -1;
    bool skipped = !skip_first;
    for (uint32_t i = 0; i < ts_node_child_count(n) && !c->error; i++) {
        TSNode x = ts_node_child(n, i);
        if (type(x, "property_name") || type(x, ":") || type(x, ";") || type(x, "(") ||
            type(x, ")") || type(x, "comment") || type(x, "important")) {
            continue;
        }
        if (type(x, ",")) {
            if (start >= 0) {
                add(c, UI_SOURCE_RESOURCE_FONT_FAMILY, text(c->source->text, start, end));
            }
            start = end = -1;
            continue;
        }
        if (!ts_node_is_named(x)) {
            continue;
        }
        if (!skipped) {
            skipped = true;
            continue;
        }
        if (type(x, "call_expression")) {
            if (start >= 0) {
                add(c, UI_SOURCE_RESOURCE_FONT_FAMILY, text(c->source->text, start, end));
            }
            start = end = -1;
            TSNode fn = child(x, "function_name");
            char *name = text(c->source->text, (int)ts_node_start_byte(fn), (int)ts_node_end_byte(fn));
            if (!strcmp(name, "var")) {
                variable(c, x, depth + 1);
            } else {
                c->error = "Font family expression is not statically "
                           "resolvable";
            }
            free(name);
            continue;
        }
        if (!type(x, "plain_value") && !type(x, "string_value")) {
            c->error = "Font family is not a static name";
            break;
        }
        if (start < 0) {
            start = (int)ts_node_start_byte(x);
        }
        end = (int)ts_node_end_byte(x);
    }
    if (start >= 0 && !c->error) {
        add(c, UI_SOURCE_RESOURCE_FONT_FAMILY, text(c->source->text, start, end));
    }
}
static void css(collector *c, TSNode n, int depth)
{
    if (!bounded(c, depth)) {
        return;
    }
    if (type(n, "import_statement")) {
        c->error = "Prepared UI has an unexpanded stylesheet import";
        return;
    }
    if (type(n, "at_rule")) {
        char *raw = text(c->source->text, (int)ts_node_start_byte(n), (int)ts_node_end_byte(n));
        if (!strncmp(raw, "@font-face", 10)) {
            c->error = "Authored font-face URLs need an explicit prepared font "
                       "resource";
        }
        free(raw);
        if (c->error) {
            return;
        }
    }
    if (type(n, "declaration")) {
        TSNode prop = child(n, "property_name");
        char *name = text(c->source->text, (int)ts_node_start_byte(prop), (int)ts_node_end_byte(prop));
        if (!strcmp(name, "font-family")) {
            families(c, n, depth + 1, false);
        }
        free(name);
    }
    if (type(n, "call_expression")) {
        TSNode fn = child(n, "function_name");
        char *name = text(c->source->text, (int)ts_node_start_byte(fn), (int)ts_node_end_byte(fn));
        if (!strcmp(name, "url")) {
            TSNode args = child(n, "arguments");
            int start = (int)ts_node_start_byte(args),
                end = (int)ts_node_end_byte(args);
            if (end - start < 2) {
                c->error = "UI URL has no static value";
            } else {
                add(c,
                    UI_SOURCE_RESOURCE_IMAGE_URL,
                    text(c->source->text, start + 1, end - 1));
            }
        }
        free(name);
    }
    for (uint32_t i = 0; i < ts_node_named_child_count(n) && !c->error; i++) {
        css(c, ts_node_named_child(n, i), depth + 1);
    }
}
static bool styles(collector *c, const char *text)
{
    if (strnlen(text, RESOURCE_TEXT_MAX + 1) > RESOURCE_TEXT_MAX) {
        c->error = "UI stylesheet exceeds input limit";
        return false;
    }
    ui_source *previous = c->source;
    c->source = ui_source_parse(text, UI_SOURCE_RCSS);
    if (acount(c->source->diagnostics)) {
        c->error = "UI stylesheet resource syntax is invalid";
    }
    if (!c->error) {
        css(c, ts_tree_root_node(c->source->trees->tree), 0);
    }
    ui_source_free(c->source);
    c->source = previous;
    return !c->error;
}
bool ui_source_resources(
    const char *markup,
    const char *style,
    ui_source_resource **out,
    char *error,
    size_t size
)
{
    collector c = { 0 };
    if (!markup || !style || !out ||
        strnlen(markup, RESOURCE_TEXT_MAX + 1) > RESOURCE_TEXT_MAX ||
        strnlen(style, RESOURCE_TEXT_MAX + 1) > RESOURCE_TEXT_MAX) {
        if (size) {
            snprintf(error, size, "UI resource input exceeds limit or is absent");
        }
        return false;
    }
    c.source = ui_source_parse(markup, UI_SOURCE_RML);
    if (acount(c.source->diagnostics)) {
        c.error = "UI markup resource syntax is invalid";
    }
    for (int i = 0; i < acount(c.source->elements) && !c.error; i++) {
        const ui_source_element *e = &c.source->elements[i];
        if (!strcmp(e->tag, "use")) {
            c.error = "Prepared UI has an unexpanded widget";
            break;
        }
        if (!strcmp(e->tag, "img")) {
            char *dynamic = ui_source_attribute_value(c.source, i, "data-attr-src");
            if (dynamic) {
                free(dynamic);
                c.error = "Dynamic image URL needs an explicit prepared "
                          "resource set";
                break;
            }
            char *src = ui_source_attribute_value(c.source, i, "src");
            if (src) {
                add(&c, UI_SOURCE_RESOURCE_IMAGE_URL, src);
            }
        }
        char *inline_style = ui_source_attribute_value(c.source, i, "style");
        if (inline_style) {
            size_t length = strlen(inline_style);
            size_t style_length = strlen(style);
            char *sheet = xmalloc(style_length + length + 7);
            snprintf(sheet, style_length + length + 7, "%s\nx{%s}", style, inline_style);
            styles(&c, sheet);
            free(sheet);
            free(inline_style);
        }
    }
    if (!c.error) {
        styles(&c, style);
    }
    ui_source_free(c.source);
    if (c.error) {
        if (size) {
            snprintf(error, size, "%s", c.error);
        }
        ui_source_resources_free(c.items);
        return false;
    }
    ui_source_resources_free(*out);
    *out = c.items;
    return true;
}
