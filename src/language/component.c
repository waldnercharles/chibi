#include "language/component.h"

#include "support/array.h"
#include "support/xalloc.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Units a field's type may name alone, where a typo of a type name must not
 * become a unit. A unit with a range or a '/' may use any name. */
static const char *const UNITS[] = {
    "u",  "px",  "m",   "s",  "ms", "rad", "deg",
    "Hz", "fps", "bpm", "EV", "dB", "kg",  "N",
};

typedef struct
{
    const script_syntax *syntax;
    const script_catalog *catalog;
    script_diagnostic **diagnostics;
    bool failed;
} reader;

static void report(reader *r, TSNode node, const char *format, ...)
{
    script_diagnostic d = { .range = script_node_range(node) };
    va_list args;
    va_start(args, format);
    vsnprintf(d.message, sizeof d.message, format, args);
    va_end(args);
    apush(*r->diagnostics, d);
    r->failed = true;
}
static bool is(TSNode node, const char *type)
{
    return !ts_node_is_null(node) && !strcmp(ts_node_type(node), type);
}
static TSNode field(TSNode node, const char *name)
{
    return ts_node_child_by_field_name(node, name, (uint32_t)strlen(name));
}
static char *text(const reader *r, TSNode node)
{
    uint32_t start = ts_node_start_byte(node), end = ts_node_end_byte(node);
    char *out = xmalloc(end - start + 1);
    memcpy(out, r->syntax->text + start, end - start);
    out[end - start] = 0;
    return out;
}
bool script_unit_shorthand(const char *name)
{
    for (size_t i = 0; i < sizeof UNITS / sizeof *UNITS; i++) {
        if (!strcmp(name, UNITS[i])) {
            return true;
        }
    }
    return false;
}
static char *name(reader *r, TSNode node)
{
    if (!is(node, "identifier")) {
        report(r, node, "A component and its fields are named by identifiers");
        return NULL;
    }
    return text(r, node);
}
/* A `//` comment alone on its line. */
static bool line_comment(const reader *r, TSNode node)
{
    uint32_t start = ts_node_start_byte(node);
    if (!is(node, "comment") || r->syntax->text[start + 1] != '/') {
        return false;
    }
    while (start && r->syntax->text[start - 1] != '\n') {
        start--;
        if (r->syntax->text[start] != ' ' && r->syntax->text[start] != '\t') {
            return false;
        }
    }
    return true;
}
/* The line comments directly above `node`, joined by line breaks. */
static char *description(const reader *r, TSNode node)
{
    TSNode first = node;
    for (TSNode prev = ts_node_prev_sibling(node);
         line_comment(r, prev) &&
         ts_node_end_point(prev).row + 1 == ts_node_start_point(first).row;
         prev = ts_node_prev_sibling(prev)) {
        first = prev;
    }
    if (ts_node_eq(first, node)) {
        return NULL;
    }
    char *out = NULL;
    for (TSNode line = first; !ts_node_eq(line, node);
         line = ts_node_next_sibling(line)) {
        const char *start = r->syntax->text + ts_node_start_byte(line) + 2;
        int length = (int)(ts_node_end_byte(line) - ts_node_start_byte(line)) - 2;
        if (length && *start == ' ') {
            start++;
            length--;
        }
        if (length && start[length - 1] == '\r') {
            length--;
        }
        if (!ts_node_eq(line, first)) {
            apush(out, '\n');
        }
        for (int i = 0; i < length; i++) {
            apush(out, start[i]);
        }
    }
    apush(out, 0);
    char *owned = xstrdup(out);
    afree(out);
    return owned;
}
static double bound(reader *r, TSNode node)
{
    bool negative = is(node, "unary_expression");
    TSNode digits = negative ? field(node, "operand") : node;
    char *spelling = text(r, digits);
    double value = strtod(spelling, NULL);
    free(spelling);
    return negative ? -value : value;
}
static void range(reader *r, TSNode node, script_component_field *out)
{
    out->min = bound(r, field(node, "min"));
    out->max = bound(r, field(node, "max"));
    if (!isfinite(out->min) || !isfinite(out->max) || !(out->min < out->max)) {
        report(r, node, "A range goes from a smaller finite number to a larger one");
    }
}
static bool type_known(const reader *r, const char *spelling)
{
    return !(script_builtin_type(spelling).flags & SCRIPT_TYPE_ERROR) ||
           script_catalog_find_type(r->catalog, spelling) != -1;
}
/* Unit terms, '/' and powers, with ^2 and ^3 written as superscripts. */
static char *unit(reader *r, TSNode node)
{
    char *out = NULL;
    for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
        TSNode part = ts_node_child(node, i);
        if (ts_node_is_extra(part)) {
            continue;
        }
        const char *append = NULL;
        char *spelling = text(r, part);
        if (is(part, "identifier") && type_known(r, spelling)) {
            report(r, part, "'%s' is a type, not a unit", spelling);
        } else if (is(part, "number")) {
            append = !strcmp(spelling, "2") ? "²"
                   : !strcmp(spelling, "3") ? "³"
                                            : NULL;
            if (!append) {
                report(r, part, "A unit's power is ^2 or ^3");
            }
        }
        if (!is(part, "^")) {
            for (const char *c = append ? append : spelling; *c; c++) {
                apush(out, *c);
            }
        }
        free(spelling);
    }
    apush(out, 0);
    char *owned = xstrdup(out);
    afree(out);
    return owned;
}
static bool pointer_syntax(TSNode node)
{
    if (is(node, "pointer_type")) {
        return true;
    }
    for (uint32_t i = 0; i < ts_node_named_child_count(node); i++) {
        if (pointer_syntax(ts_node_named_child(node, i))) {
            return true;
        }
    }
    return false;
}
static bool pointer_fields(const script_catalog *catalog, script_type type, int depth)
{
    if (type.storage == PIN_POINTER || type.storage == PIN_QUERY || depth >= 64) {
        return true;
    }
    const script_catalog_type *record = script_catalog_type_by_id(catalog, type.nominal);
    for (int i = 0; record && i < record->field_count; i++) {
        if (pointer_fields(catalog, record->fields[i].type, depth + 1)) {
            return true;
        }
    }
    return false;
}
static void type(reader *r, TSNode node, script_component_field *out)
{
    if (pointer_syntax(node)) {
        report(r, node, "ECS component fields cannot own managed pointers");
        return;
    }
    if (is(node, "measured_type")) {
        type(r, field(node, "base"), out);
        if (out->unit || !out->type ||
            !script_type_numeric(script_builtin_type(out->type))) {
            report(r, node, "A measured field requires a numeric storage type");
            return;
        }
        out->unit = unit(r, field(node, "unit"));
        return;
    }
    if (is(node, "measure")) {
        out->type = xstrdup("number");
        TSNode limits = field(node, "range"), measured = field(node, "unit");
        if (!ts_node_is_null(limits)) {
            range(r, limits, out);
        }
        out->unit = ts_node_is_null(measured) ? NULL : unit(r, measured);
        return;
    }
    TSNode type_name = field(node, "name");
    bool union_type = is(node, "union_type");
    for (uint32_t i = 0; i < ts_node_named_child_count(node); i++) {
        union_type |= is(ts_node_named_child(node, i), "union_type");
    }
    if (union_type) {
        report(r, node, "ECS component fields cannot own union payloads");
        return;
    }
    if (ts_node_named_child_count(node) != 1 || ts_node_child_count(node) != 1) {
        report(r, node, "A component field holds a number, a scalar or a struct");
        return;
    }
    out->type = text(r, type_name);
    script_type builtin = script_builtin_type(out->type);
    int named = script_catalog_find_type(r->catalog, out->type);
    if (builtin.storage == PIN_UNION ||
        (named >= 0 && r->catalog->types[named].type.storage == PIN_UNION)) {
        report(r, node, "ECS component fields cannot own union payloads");
        return;
    }
    if (builtin.storage == PIN_POINTER || builtin.storage == PIN_QUERY ||
        (named >= 0 && pointer_fields(r->catalog, r->catalog->types[named].type, 0))) {
        report(r, node, "ECS component fields cannot own managed pointers");
        return;
    }
    if (type_known(r, out->type)) {
        return;
    }
    TSNode word = ts_node_named_child(type_name, 0);
    if (ts_node_named_child_count(type_name) == 1 && is(word, "identifier") &&
        script_unit_shorthand(out->type)) {
        free(out->type);
        out->type = xstrdup("number");
        out->unit = text(r, word);
        return;
    }
    report(r, node, "Unknown type or unit '%s'", out->type);
}
static void fields(reader *r, TSNode body, script_component *out)
{
    for (uint32_t i = 0; i < ts_node_named_child_count(body); i++) {
        TSNode node = ts_node_named_child(body, i);
        /* Comments, and errors the syntax already reports. */
        if (!is(node, "component_field")) {
            continue;
        }
        script_component_field f = { .name = name(r, field(node, "name")),
                                     .description = description(r, node),
                                     .range = script_node_range(node) };
        type(r, field(node, "type"), &f);
        for (int j = 0; f.name && j < acount(out->fields); j++) {
            if (out->fields[j].name && !strcmp(out->fields[j].name, f.name)) {
                report(r, node, "Field '%s' is declared twice", f.name);
            }
        }
        apush(out->fields, f);
    }
    if (!acount(out->fields) && !r->failed) {
        report(r, body, "A component declares at least one field");
    }
}
script_component *script_component_read(
    const script_syntax *syntax,
    const script_catalog *catalog,
    script_diagnostic **diagnostics
)
{
    reader r = { syntax, catalog, diagnostics, syntax->diagnostic_count > 0 };
    for (int i = 0; i < syntax->diagnostic_count; i++) {
        apush(*diagnostics, syntax->diagnostics[i]);
    }
    if (syntax->diagnostic_count) {
        return NULL;
    }
    TSNode root = ts_tree_root_node(syntax->tree), declaration = { 0 };
    for (uint32_t i = 0; i < ts_node_named_child_count(root); i++) {
        TSNode node = ts_node_named_child(root, i);
        if (ts_node_is_extra(node)) {
            continue;
        }
        if (!is(node, "component") || !ts_node_is_null(declaration)) {
            report(&r, node, "A component source declares one component");
            continue;
        }
        declaration = node;
    }
    if (ts_node_is_null(declaration)) {
        if (!r.failed) {
            report(&r, root, "Source declares no component");
        }
        return NULL;
    }
    script_component *out = xcalloc(1, sizeof *out);
    out->name = name(&r, field(declaration, "name"));
    out->description = description(&r, declaration);
    out->range = script_node_range(declaration);
    fields(&r, field(declaration, "body"), out);
    if (r.failed) {
        script_component_free(out);
        return NULL;
    }
    return out;
}
void script_component_free(script_component *component)
{
    if (!component) {
        return;
    }
    for (int i = 0; i < acount(component->fields); i++) {
        script_component_field *f = &component->fields[i];
        free(f->name);
        free(f->type);
        free(f->unit);
        free(f->description);
    }
    afree(component->fields);
    free(component->name);
    free(component->description);
    free(component);
}
