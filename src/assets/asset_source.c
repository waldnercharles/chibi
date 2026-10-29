#include "assets/asset_source.h"

#include "ecs/component_catalog.h"
#include "graph/model/registry.h"
#include "support/array.h"
#include "support/asset_text.h"
#include "support/text_edit.h"
#include "support/text_search.h"
#include "support/xalloc.h"

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static bool word(char c)
{
    return c && !isspace((unsigned char)c) && !strchr("{}[]();,=\"+*/!<>:&|", c);
}

void asset_source_position(const asset_source *s, int offset, int *line, int *column)
{
    *line = *column = 1;
    for (int i = 0; i < offset && s->text[i]; i++) {
        if (s->text[i] == '\n') {
            (*line)++;
            *column = 1;
        } else {
            (*column)++;
        }
    }
}

static void diagnostic(asset_source *s, av_source range, const char *message)
{
    asset_source_diagnostic d = { .start = range.start == SIZE_MAX ? 0
                                                                   : (int)range.start,
                                  .end = range.end == SIZE_MAX ? 0 : (int)range.end };
    asset_source_position(s, d.start, &d.line, &d.column);
    snprintf(d.message, sizeof d.message, "%s", message);
    apush(s->diagnostics, d);
}

/* Highlight incomplete documents independently of their semantic parse. */
static void kdl_lex(asset_source *s)
{
    const char *t = s->text;
    for (int i = 0; t[i];) {
        if (isspace((unsigned char)t[i])) {
            i++;
            continue;
        }
        int start = i;
        const char *cls = "tok-name";
        if (t[i] == '/' && t[i + 1] == '/') {
            i += 2;
            while (t[i] && t[i] != '\n') {
                i++;
            }
            cls = "tok-comment";
        } else if (t[i] == '/' && t[i + 1] == '*') {
            i += 2;
            int depth = 1;
            while (t[i] && depth) {
                if (t[i] == '/' && t[i + 1] == '*') {
                    depth++;
                    i += 2;
                } else if (t[i] == '*' && t[i + 1] == '/') {
                    depth--;
                    i += 2;
                } else {
                    i++;
                }
            }
            cls = "tok-comment";
        } else if (t[i] == '"' || t[i] == '#') {
            int hashes = 0;
            while (t[i] == '#') {
                hashes++;
                i++;
            }
            if (t[i] == '"') {
                bool multiline = t[i + 1] == '"' && t[i + 2] == '"';
                int quotes = multiline ? 3 : 1;
                i += quotes;
                while (t[i]) {
                    if (!hashes && t[i] == '\\' && t[i + 1]) {
                        i += 2;
                        continue;
                    }
                    bool end = true;
                    for (int q = 0; q < quotes; q++) {
                        if (t[i + q] != '"') {
                            end = false;
                            break;
                        }
                    }
                    if (end) {
                        for (int h = 0; h < hashes; h++) {
                            if (t[i + quotes + h] != '#') {
                                end = false;
                                break;
                            }
                        }
                    }
                    if (end) {
                        i += quotes + hashes;
                        break;
                    }
                    i++;
                }
                cls = "tok-string";
            } else {
                while (word(t[i])) {
                    i++;
                }
                cls = "tok-keyword";
            }
        } else if (strchr("{}();=", t[i])) {
            i++;
            cls = "tok-punctuation";
        } else {
            i++;
            while (word(t[i])) {
                i++;
            }
            if (isdigit((unsigned char)t[start]) ||
                ((t[start] == '-' || t[start] == '+') &&
                 isdigit((unsigned char)t[start + 1]))) {
                cls = "tok-number";
            }
        }
        apush(s->spans, ((asset_source_span){ start, i, cls }));
    }
}

static void lex(asset_source *s)
{
    kdl_lex(s);
}

/* Asset identities resolve through composed component descriptors or an exact
 * reflection-pack name, never an arbitrary prefix on a short type name. */
static const type_desc *reflected_type_named(const char *name)
{
    const type_desc *type = type_catalog_find(name);
    if (type) {
        return type;
    }
    for (int i = 0; i < registry_node_count(); i++) {
        const ecs_component_desc *component = registry_node(i)->component;
        if (component && component->key && !strcmp(component->key, name)) {
            return component->type;
        }
    }
    for (int i = 0; i < registry_pack_count(); i++) {
        const registry_pack *pack = registry_pack_at(i);
        size_t prefix = strlen(pack->name);
        if (strncmp(name, pack->name, prefix) || name[prefix] != '.') {
            continue;
        }
        for (int t = 0; t < pack->type_count; t++) {
            if (!strcmp(name + prefix + 1, pack->types[t]->name)) {
                return pack->types[t];
            }
        }
    }
    return NULL;
}

static const type_field *field_named(const type_desc *type, const char *name)
{
    for (int i = 0; type && i < type->field_count; i++) {
        if (!strcmp(type->fields[i].name, name)) {
            return &type->fields[i];
        }
    }
    return NULL;
}

static bool valid_field(const type_field *field, const av_value *v)
{
    if (av_is_null(v)) {
        return true; /* A sparse override may remove a field. */
    }
    if (field->type == PIN_BOOL) {
        return av_is_bool(v);
    }
    if (field->type == PIN_STR || field->type == PIN_NAME) {
        return av_is_str(v);
    }
    if (field->type == PIN_STRUCT) {
        return av_is_obj(v);
    }
    if (field->type == PIN_ENUM) {
        const enum_desc *e = type_catalog_enum_find(field->type_name);
        if (!e) {
            return av_is_int(v) || av_is_str(v);
        }
        for (int i = 0; i < e->value_count; i++) {
            if (av_is_int(v) && av_get_int(v) == e->values[i].value) {
                return true;
            }
            if (av_is_str(v) && (!strcmp(av_get_str(v), e->values[i].name) ||
                                 (e->values[i].label &&
                                  !strcmp(av_get_str(v), e->values[i].label)))) {
                return true;
            }
        }
        return false;
    }
    if (field->type >= PIN_I8 && field->type <= PIN_U64) {
        return av_is_int(v);
    }
    if (field->type == PIN_F32 || field->type == PIN_F64) {
        return av_is_num(v);
    }
    return true;
}

static void validate(asset_source *s, const av_value *value, const type_desc *type)
{
    if (av_is_arr(value)) {
        for (size_t i = 0; i < av_arr_size(value); i++) {
            validate(s, av_arr_get(value, i), NULL);
        }
        return;
    }
    if (!av_is_obj(value)) {
        return;
    }
    for (size_t i = 0; i < av_obj_size(value); i++) {
        const char *name = av_get_str(av_obj_key_at(value, i));
        const av_value *child = av_obj_value_at(value, i);
        const type_field *field = field_named(type, name);
        char error[256];
        if (type && !field) {
            snprintf(error, sizeof error, "Unknown field '%s' of %s", name, type->name);
            diagnostic(s, av_get_source(child), error);
        } else if (field && !valid_field(field, child)) {
            snprintf(
                error,
                sizeof error,
                "Invalid value for %s.%s (%s)",
                type->name,
                name,
                type_field_type_name(field)
            );
            diagnostic(s, av_get_source(child), error);
        }
        const type_desc *nested = field && field->type == PIN_STRUCT
                                    ? reflected_type_named(field->type_name)
                                    : reflected_type_named(name);
        validate(s, child, nested);
    }
}

asset_source *asset_source_parse(const char *text)
{
    asset_source *s = xcalloc(1, sizeof *s);
    s->text = xstrdup(text);
    lex(s);
    char error[256] = "";
    s->document = av_doc_read(text, error, sizeof error);
    if (s->document) {
        validate(s, av_doc_root(s->document), NULL);
        return s;
    }
    int line = 1, column = 1;
    if (sscanf(error, "line %d, column %d", &line, &column) != 2) {
        (void)sscanf(error, "%d:%d", &line, &column);
    }
    int at = 0;
    for (int n = 1; n < line && text[at]; n++) {
        while (text[at] && text[at] != '\n') {
            at++;
        }
        if (text[at]) {
            at++;
        }
    }
    for (int n = 1; n < column && text[at] && text[at] != '\n'; n++) {
        at++;
    }
    diagnostic(s, (av_source){ .start = (size_t)at, .end = (size_t)(at + (text[at] != 0)) }, error);
    return s;
}

void asset_source_free(asset_source *s)
{
    if (!s) {
        return;
    }
    free(s->text);
    av_doc_free(s->document);
    afree(s->spans);
    afree(s->diagnostics);
    free(s);
}

static const av_value *value_at(const av_value *value, int at, const char **name)
{
    for (size_t i = 0, n = av_is_obj(value) ? av_obj_size(value) : av_arr_size(value);
         i < n;
         i++) {
        const av_value *child = av_is_obj(value) ? av_obj_value_at(value, i)
                                                 : av_arr_get(value, i);
        av_source range = av_get_source(child);
        if ((range.start != SIZE_MAX && at >= (int)range.start &&
             at < (int)range.end) ||
            (range.name_start != SIZE_MAX && at >= (int)range.name_start &&
             at < (int)range.name_end)) {
            const av_value *nested = av_is_ctn(child) ? value_at(child, at, name)
                                                      : NULL;
            if (nested) {
                return nested;
            }
            *name = av_is_obj(value) ? av_get_str(av_obj_key_at(value, i)) : NULL;
            return child;
        }
    }
    return NULL;
}

const av_value *asset_source_value_at(const asset_source *s, int at, const char **name)
{
    *name = NULL;
    return s->document ? value_at(av_doc_root(s->document), at, name) : NULL;
}

static void offer(
    asset_source_completions *out,
    const char *prefix,
    const char *label,
    const char *detail,
    const char *insert,
    code_completion_kind kind
)
{
    int score = text_search_match(prefix, label, NULL, 0);
    if (score < 0) {
        return;
    }
    for (int i = 0; i < acount(out->items); i++) {
        if (!strcmp(out->items[i].label, label)) {
            return;
        }
    }
    apush(
        out->items,
        ((asset_source_completion){ .label = xstrdup(label),
                                    .detail = detail,
                                    .insert = insert ? xstrdup(insert) : NULL,
                                    .kind = kind,
                                    .score = score })
    );
}

static char *identifier_text(const char *name)
{
    bool bare = isalpha((unsigned char)name[0]) || name[0] == '_';
    for (const char *p = name; *p && bare; p++) {
        bare = isalnum((unsigned char)*p) || strchr("_.-", *p);
    }
    if (bare) {
        return xstrdup(name);
    }
    av_doc *doc = av_doc_new();
    char *text = av_value_write(av_str(doc, name));
    av_doc_free(doc);
    return text;
}

static void offer_name(
    asset_source_completions *out,
    const char *prefix,
    const char *name,
    const char *detail,
    code_completion_kind kind
)
{
    char *insert = identifier_text(name);
    offer(out, prefix, name, detail, !strcmp(insert, name) ? NULL : insert, kind);
    free(insert);
}

/* Find the enclosing reflected type even while a new property is incomplete. */
typedef struct
{
    const type_desc *type;
    int node, block;
} completion_scope;

/* Scope recovery depends on braces and assignments, so it also works when a
 * value, terminator or closing brace has not been typed yet. */
static completion_scope scope_at(const asset_source *s, int at)
{
    completion_scope stack[128];
    for (int i = 0; i < 128; i++) {
        stack[i] = (completion_scope){ .node = -1, .block = -1 };
    }
    int depth = 0;
    completion_scope current = stack[0];
    bool node_start = true, annotation = false;
    int consumed = 0;
    for (int i = 0; i < acount(s->spans); i++) {
        asset_source_span span = s->spans[i];
        int previous = i ? s->spans[i - 1].end : 0;
        for (int gap = previous; gap < span.start && gap < at; gap++) {
            if (s->text[gap] == '\n') {
                current = stack[depth];
                node_start = true;
                break;
            }
        }
        if (span.start >= at) {
            break;
        }
        consumed = span.end;
        char first = s->text[span.start];
        if (first == '{') {
            if (depth < 127) {
                current.block = i;
                stack[++depth] = current;
            }
            node_start = true;
            continue;
        }
        if (first == '}') {
            if (depth) {
                depth--;
            }
            current = stack[depth];
            node_start = false;
            continue;
        }
        if (first == ';') {
            current = stack[depth];
            node_start = true;
            continue;
        }
        if (first == '(') {
            annotation = true;
            continue;
        }
        if (first == ')') {
            annotation = false;
            continue;
        }
        if (annotation || !node_start || !strcmp(span.cls, "tok-comment")) {
            continue;
        }
        if (strcmp(span.cls, "tok-name") && strcmp(span.cls, "tok-string")) {
            continue;
        }
        char name[256];
        int n = span.end - span.start;
        if (n >= (int)sizeof name) {
            continue;
        }
        memcpy(name, s->text + span.start, (size_t)n);
        name[n] = 0;
        av_doc *literal = !strcmp(span.cls, "tok-string")
                            ? av_value_read(name, NULL, 0)
                            : NULL;
        const char *key = literal ? av_get_str(av_doc_root(literal)) : name;
        const type_desc *type = key ? reflected_type_named(key) : NULL;
        if (type) {
            current = (completion_scope){ .type = type, .node = i, .block = -1 };
        } else {
            const type_field *field = key ? field_named(current.type, key) : NULL;
            if (field && field->type == PIN_STRUCT) {
                current = (completion_scope){ .type = reflected_type_named(field->type_name),
                                              .node = i,
                                              .block = -1 };
            } else if (!current.type) {
                current.node = i;
                current.block = -1;
            }
        }
        av_doc_free(literal);
        node_start = false;
    }
    /* A draft may end in whitespace with no next token to expose a newline.
     * Finish that gap so a completed nested field returns to its parent. */
    for (int gap = consumed; gap < at; gap++) {
        if (s->text[gap] == '\n') {
            current = stack[depth];
            break;
        }
    }
    return current;
}

static const type_desc *context(const asset_source *s, int at)
{
    return scope_at(s, at).type;
}

static bool property_present(const asset_source *s, completion_scope scope, const char *field, int editing)
{
    int first = scope.block >= 0 ? scope.block + 1 : scope.node + 1;
    int depth = 0;
    bool node_start = true, annotation = false;
    for (int i = first; i < acount(s->spans); i++) {
        asset_source_span span = s->spans[i];
        int previous = i ? s->spans[i - 1].end : 0;
        for (int gap = previous; gap < span.start; gap++) {
            if (s->text[gap] != '\n') {
                continue;
            }
            if (scope.block < 0 && scope.node >= 0) {
                return false;
            }
            node_start = true;
        }
        char token = s->text[span.start];
        if (token == '{') {
            depth++;
            continue;
        }
        if (token == '}') {
            if (!depth) {
                return false;
            }
            depth--;
            node_start = false;
            continue;
        }
        if (depth || !strcmp(span.cls, "tok-comment")) {
            continue;
        }
        if (token == ';') {
            if (scope.block < 0 && scope.node >= 0) {
                return false;
            }
            node_start = true;
            continue;
        }
        if (token == '(') {
            annotation = true;
            continue;
        }
        if (token == ')') {
            annotation = false;
            continue;
        }
        if (annotation) {
            continue;
        }
        bool property = i + 1 < acount(s->spans) &&
                        s->text[s->spans[i + 1].start] == '=';
        bool child = scope.block >= 0 || scope.node < 0;
        if ((!child || !node_start) && !property) {
            continue;
        }
        if (strcmp(span.cls, "tok-name") && strcmp(span.cls, "tok-string")) {
            continue;
        }
        node_start = false;
        if (editing >= span.start && editing < span.end) {
            continue;
        }
        int n = span.end - span.start;
        if (!strcmp(span.cls, "tok-name")) {
            if ((int)strlen(field) == n &&
                !strncmp(s->text + span.start, field, (size_t)n)) {
                return true;
            }
            continue;
        }
        char *literal = xmalloc((size_t)n + 1);
        memcpy(literal, s->text + span.start, (size_t)n);
        literal[n] = 0;
        av_doc *doc = av_value_read(literal, NULL, 0);
        bool matches = doc && av_is_str(av_doc_root(doc)) &&
                       !strcmp(av_get_str(av_doc_root(doc)), field);
        av_doc_free(doc);
        free(literal);
        if (matches) {
            return true;
        }
    }
    return false;
}

static void kdl_complete(
    const asset_source *s,
    int at,
    const char *path,
    const char *const *names,
    int name_count,
    asset_source_completions *out
)
{
    memset(out, 0, sizeof *out);
    int length = (int)strlen(s->text);
    if (at < 0 || at > length) {
        return;
    }
    out->end = at;
    out->start = at;
    while (out->start && word(s->text[out->start - 1])) {
        out->start--;
    }
    for (int i = 0; i < acount(s->spans); i++) {
        asset_source_span span = s->spans[i];
        if (span.start < at && at < span.end &&
            !strcmp(span.cls, "tok-string") && s->text[span.start] == '"') {
            out->start = span.start + 1;
            break;
        }
    }
    char *prefix = xmalloc((size_t)(at - out->start + 1));
    memcpy(prefix, s->text + out->start, (size_t)(at - out->start));
    prefix[at - out->start] = 0;
    completion_scope scope = scope_at(s, out->start);
    const type_desc *type = scope.type;
    char field_name[128] = "";
    bool quoted = out->start > 0 && s->text[out->start - 1] == '"';
    bool value_position = quoted;
    bool assigned = false;
    int before = out->start - (quoted ? 1 : 0);
    while (before && (s->text[before - 1] == ' ' || s->text[before - 1] == '\t')) {
        before--;
    }
    if (before && s->text[before - 1] == '=') {
        value_position = true;
        assigned = true;
        before--;
    } else if (before && before < out->start && word(s->text[before - 1])) {
        value_position = true;
    }
    while (before && (s->text[before - 1] == ' ' || s->text[before - 1] == '\t')) {
        before--;
    }
    int begin = before;
    while (begin && word(s->text[begin - 1])) {
        begin--;
    }
    int n = before - begin;
    if (n > 0 && n < (int)sizeof field_name) {
        memcpy(field_name, s->text + begin, (size_t)n);
        field_name[n] = 0;
    }
    const type_field *field = field_named(type, field_name);
    if (type && !field && !assigned && !quoted) {
        value_position = false;
    }
    if (value_position) {
        if (field && field->type == PIN_BOOL) {
            offer(out, prefix, "#true", "bool", NULL, CODE_COMPLETE_KEYWORD);
            offer(out, prefix, "#false", "bool", NULL, CODE_COMPLETE_KEYWORD);
        }
        const enum_desc *e = field && field->type == PIN_ENUM
                               ? type_catalog_enum_find(field->type_name)
                               : NULL;
        for (int i = 0; e && i < e->value_count; i++) {
            const char *label = e->values[i].label ? e->values[i].label
                                                   : e->values[i].name;
            av_doc *doc = av_doc_new();
            char *insert = quoted ? xstrdup(label)
                                  : av_value_write(av_str(doc, label));
            offer(out, prefix, label, e->name, insert, CODE_COMPLETE_VALUE);
            free(insert);
            av_doc_free(doc);
        }
        if (!field || field->type == PIN_STR || field->type == PIN_NAME ||
            field->type == PIN_HANDLE) {
            for (int i = 0; i < name_count; i++) {
                av_doc *doc = av_doc_new();
                char *insert = quoted ? xstrdup(names[i])
                                      : av_value_write(av_str(doc, names[i]));
                offer(out, prefix, names[i], "asset", insert, CODE_COMPLETE_ASSET);
                free(insert);
                av_doc_free(doc);
            }
        }
        if (!field && !strcmp(field_name, "type")) {
            for (int i = 0; i < registry_node_count(); i++) {
                const char *label = registry_node(i)->name;
                av_doc *doc = av_doc_new();
                char *insert = quoted ? xstrdup(label)
                                      : av_value_write(av_str(doc, label));
                offer(out, prefix, label, "graph node", insert, CODE_COMPLETE_FUNCTION);
                free(insert);
                av_doc_free(doc);
            }
        }
    } else if (type) {
        for (int i = 0; i < type->field_count; i++) {
            if (property_present(s, scope, type->fields[i].name, *prefix ? out->start : -1)) {
                continue;
            }
            offer(out, prefix, type->fields[i].name, type->fields[i].type_name, NULL, CODE_COMPLETE_FIELD);
        }
    } else {
        static const char *const common[] = {
            "name",        "version",  "components", "behaviors",
            "includes",    "entities", "nodes",      "edges",
            "variables",   "events",   "inputs",     "outputs",
            "perspective", "physics",  "ui",         "identity"
        };
        for (size_t i = 0; i < sizeof common / sizeof *common; i++) {
            if (!property_present(s, scope, common[i], *prefix ? out->start : -1)) {
                offer(out, prefix, common[i], "asset field", NULL, CODE_COMPLETE_FIELD);
            }
        }
        for (int i = 0; i < type_catalog_count(); i++) {
            offer_name(out, prefix, type_catalog_at(i)->name, "component type", CODE_COMPLETE_TYPE);
        }
        for (int p = 0; p < registry_pack_count(); p++) {
            const registry_pack *pack = registry_pack_at(p);
            for (int i = 0; i < pack->type_count; i++) {
                char qualified[256];
                snprintf(
                    qualified,
                    sizeof qualified,
                    "%s.%s",
                    pack->name,
                    pack->types[i]->name
                );
                offer_name(out, prefix, qualified, "component type", CODE_COMPLETE_TYPE);
            }
        }
        for (int i = 0; i < registry_node_count(); i++) {
            const ecs_component_desc *component = registry_node(i)->component;
            if (component && component->key && strcmp(component->key, component->name)) {
                offer_name(out, prefix, component->key, "component type", CODE_COMPLETE_TYPE);
            }
            offer_name(out, prefix, registry_node(i)->name, "graph node", CODE_COMPLETE_FUNCTION);
        }
    }
    for (int i = 1; i < acount(out->items); i++) {
        asset_source_completion item = out->items[i];
        int at = i;
        while (at > 0 && out->items[at - 1].score < item.score) {
            out->items[at] = out->items[at - 1];
            at--;
        }
        out->items[at] = item;
    }
    (void)path;
    free(prefix);
}

void asset_source_complete(
    const asset_source *s,
    int at,
    const char *path,
    const char *const *names,
    int name_count,
    asset_source_completions *out
)
{
    memset(out, 0, sizeof *out);
    if (at < 0 || at > (int)strlen(s->text)) {
        return;
    }
    for (int i = 0; i < acount(s->spans); i++) {
        asset_source_span span = s->spans[i];
        if (!strcmp(span.cls, "tok-comment") && span.start <= at &&
            (at < span.end || (at == span.end && s->text[span.start + 1] == '/'))) {
            return;
        }
    }
    kdl_complete(s, at, path, names, name_count, out);
}

void asset_source_completions_free(asset_source_completions *out)
{
    for (int i = 0; i < acount(out->items); i++) {
        free(out->items[i].label);
        free(out->items[i].insert);
    }
    afree(out->items);
    memset(out, 0, sizeof *out);
}

char *asset_source_hover(const asset_source *s, int at)
{
    const char *name;
    const av_value *v = asset_source_value_at(s, at, &name);
    if (!v || !name) {
        return NULL;
    }
    const type_desc *type = reflected_type_named(name);
    const type_field *field = field_named(context(s, at), name);
    char text[512];
    if (type) {
        snprintf(text, sizeof text, "%s\n%d reflected fields", name, type->field_count);
    } else if (field) {
        snprintf(
            text,
            sizeof text,
            "%s: %s\n%s",
            name,
            type_field_type_name(field),
            field->description ? field->description : ""
        );
    } else {
        snprintf(text, sizeof text, "%s\nasset field", name);
    }
    return xstrdup(text);
}

static char *node_text(const char *name, const av_value *value)
{
    av_doc *doc = av_doc_new();
    av_value *root = av_obj(doc);
    av_doc_set_root(doc, root);
    av_obj_add_val(doc, root, name, av_clone(doc, value));
    char *text = av_doc_write(doc);
    av_doc_free(doc);
    return text;
}

static char *scalar_text(const av_value *value)
{
    char *node = node_text("value", value);
    if (!node) {
        return NULL;
    }
    char *start = node + strlen("value");
    while (*start == ' ' || *start == '\t') {
        start++;
    }
    size_t n = strlen(start);
    while (n && (start[n - 1] == '\n' || start[n - 1] == '\r' || start[n - 1] == ';')) {
        n--;
    }
    char *result = xmalloc(n + 1);
    memcpy(result, start, n);
    result[n] = 0;
    free(node);
    return result;
}

static char *indent_node(const char *text, const char *source, size_t at)
{
    size_t line = at;
    while (line && source[line - 1] != '\n') {
        line--;
    }
    size_t indent = 0;
    while (line + indent < at &&
           (source[line + indent] == ' ' || source[line + indent] == '\t')) {
        indent++;
    }
    size_t lines = 0, n = strlen(text);
    while (n && (text[n - 1] == '\n' || text[n - 1] == '\r')) {
        n--;
    }
    for (size_t i = 0; i < n; i++) {
        lines += text[i] == '\n';
    }
    char *out = xmalloc(n + lines * indent + 1);
    size_t write = 0;
    for (size_t i = 0; i < n; i++) {
        out[write++] = text[i];
        if (text[i] == '\n') {
            memcpy(out + write, source + line, indent);
            write += indent;
        }
    }
    out[write] = 0;
    return out;
}

/* Container spans include trailing trivia. Locate the last syntax token so
 * inserting a property never lands inside a trailing line comment. */
static size_t content_end(const char *text, size_t start, size_t end)
{
    asset_source tokens = { .text = (char *)text };
    lex(&tokens);
    size_t last = start;
    for (int i = 0; i < acount(tokens.spans); i++) {
        asset_source_span span = tokens.spans[i];
        if ((size_t)span.start >= start && (size_t)span.end <= end &&
            strcmp(span.cls, "tok-comment")) {
            last = (size_t)span.end;
        }
    }
    afree(tokens.spans);
    return last;
}

static bool replace_node(
    const char *text,
    const av_value *old,
    const av_value *value,
    const char *name,
    text_edit **edits
)
{
    av_source range = av_get_source(old);
    if (range.start == SIZE_MAX || range.end == SIZE_MAX || range.end > strlen(text)) {
        return false;
    }
    char *replacement;
    int start = (int)range.start;
    size_t end = av_is_ctn(old) ? content_end(text, range.start, range.end)
                                : range.end;
    if (!av_is_ctn(old) && !av_is_ctn(value)) {
        replacement = scalar_text(value);
    } else {
        if (!name || range.name_start == SIZE_MAX) {
            return false;
        }
        start = (int)(av_is_ctn(old) ? range.start : range.name_start);
        char *node = node_text(name, value);
        replacement = node ? indent_node(node, text, (size_t)start) : NULL;
        free(node);
    }
    if (!replacement) {
        return false;
    }
    apush(*edits, ((text_edit){ start, (int)end, replacement }));
    return true;
}

static bool collect_edits(
    const char *text,
    const av_value *old,
    const av_value *value,
    const char *name,
    text_edit **edits
)
{
    if (!av_is_ctn(old) && av_equals(old, value)) {
        if (!av_is_num(value)) {
            return true;
        }
        av_source range = av_get_source(old);
        char *literal = scalar_text(value);
        bool same = literal && range.start != SIZE_MAX &&
                    range.end != SIZE_MAX && range.end <= strlen(text) &&
                    range.end - range.start == strlen(literal) &&
                    !memcmp(text + range.start, literal, strlen(literal));
        free(literal);
        if (same) {
            return true;
        }
    }
    if (av_is_obj(old) && av_is_obj(value)) {
        av_source container = av_get_source(old);
        size_t length = strlen(text);
        bool root = !name;
        size_t ending = root ? length : container.end;
        if (!root && ending != SIZE_MAX) {
            ending = content_end(text, container.start, ending);
        }
        bool children = root ||
                        (ending != SIZE_MAX && ending && text[ending - 1] == '}');
        if (!children) {
            for (size_t i = 0; i < av_obj_size(value); i++) {
                if (av_is_ctn(av_obj_value_at(value, i))) {
                    return replace_node(text, old, value, name, edits);
                }
            }
        }
        int removed = -1, added = -1, removals = 0, additions = 0;
        for (size_t i = 0; i < av_obj_size(old); i++) {
            if (!av_obj_get(value, av_get_str(av_obj_key_at(old, i)))) {
                removed = (int)i;
                removals++;
            }
        }
        for (size_t i = 0; i < av_obj_size(value); i++) {
            if (!av_obj_get(old, av_get_str(av_obj_key_at(value, i)))) {
                added = (int)i;
                additions++;
            }
        }
        bool rename = removals == 1 && additions == 1 && removed == added;
        if (rename) {
            const av_value *a = av_obj_value_at(old, (size_t)removed);
            const av_value *b = av_obj_value_at(value, (size_t)added);
            const char *key = av_get_str(av_obj_key_at(value, (size_t)added));
            av_source location = av_get_source(a);
            if (location.name_start == SIZE_MAX || location.name_end == SIZE_MAX) {
                return false;
            }
            int before = acount(*edits);
            if (!collect_edits(text, a, b, key, edits)) {
                return false;
            }
            bool rewritten = false;
            for (int i = before; i < acount(*edits); i++) {
                rewritten |= (*edits)[i].start <= (int)location.name_start &&
                             (*edits)[i].end >= (int)location.name_end;
            }
            if (!rewritten) {
                char *name_text = identifier_text(key);
                apush(
                    *edits,
                    ((text_edit){ (int)location.name_start, (int)location.name_end, name_text })
                );
            }
        }
        for (size_t i = 0; i < av_obj_size(old); i++) {
            const char *key = av_get_str(av_obj_key_at(old, i));
            const av_value *a = av_obj_value_at(old, i);
            const av_value *b = av_obj_get(value, key);
            if (rename && (int)i == removed) {
                continue;
            }
            if (b) {
                if (!collect_edits(text, a, b, key, edits)) {
                    return false;
                }
                continue;
            }
            av_source range = av_get_source(a);
            if (range.name_start == SIZE_MAX || range.end == SIZE_MAX) {
                return false;
            }
            size_t start = av_is_ctn(a) ? range.start : range.name_start;
            if (children) {
                size_t line = start;
                while (line && text[line - 1] != '\n') {
                    line--;
                }
                size_t first = line;
                while (first < start && isspace((unsigned char)text[first])) {
                    first++;
                }
                if (text[first] == '(') {
                    start = first;
                }
            }
            size_t end = av_is_ctn(a) ? content_end(text, range.start, range.end)
                                      : range.end;
            apush(*edits, ((text_edit){ (int)start, (int)end, xstrdup("") }));
        }
        char *insert = xstrdup("");
        for (size_t i = 0; i < av_obj_size(value); i++) {
            const char *key = av_get_str(av_obj_key_at(value, i));
            const av_value *b = av_obj_value_at(value, i);
            if (av_obj_get(old, key) || (rename && (int)i == added)) {
                continue;
            }
            if (!children && av_is_ctn(b)) {
                free(insert);
                return false;
            }
            char *node = children ? node_text(key, b) : scalar_text(b);
            if (!node) {
                free(insert);
                return false;
            }
            size_t n = strlen(insert), extra = strlen(node) + strlen(key) + 8;
            insert = xrealloc(insert, n + extra + 1);
            if (children) {
                snprintf(
                    insert + n,
                    extra + 1,
                    "%s%s",
                    n || !length || text[length - 1] == '\n' || !root ? "" : "\n",
                    node
                );
            } else {
                snprintf(insert + n, extra + 1, " %s=%s", key, node);
            }
            free(node);
        }
        if (*insert) {
            size_t at = ending;
            if (at == SIZE_MAX || at > length) {
                free(insert);
                return false;
            }
            if (children && !root) {
                at--;
            }
            if (children && !root) {
                char *indented = indent_node(insert, text, container.name_start);
                free(insert);
                size_t n = strlen(indented);
                insert = xmalloc(n + 7);
                snprintf(insert, n + 7, "\n    %s\n", indented);
                free(indented);
            }
            apush(*edits, ((text_edit){ (int)at, (int)at, insert }));
        } else {
            free(insert);
        }
        return true;
    }
    if (av_is_arr(old) && av_is_arr(value)) {
        av_source range = av_get_source(old);
        size_t end = content_end(text, range.start, range.end);
        bool children = end && text[end - 1] == '}';
        for (size_t i = 0; !children && i < av_arr_size(value); i++) {
            if (av_is_ctn(av_arr_get(value, i))) {
                return replace_node(text, old, value, name, edits);
            }
        }
        size_t common = av_arr_size(old) < av_arr_size(value) ? av_arr_size(old)
                                                              : av_arr_size(value);
        for (size_t i = 0; i < common; i++) {
            if (!collect_edits(text, av_arr_get(old, i), av_arr_get(value, i), "-", edits)) {
                return false;
            }
        }
        for (size_t i = common; i < av_arr_size(old); i++) {
            const av_value *child = av_arr_get(old, i);
            av_source location = av_get_source(child);
            size_t start = children
                             ? (av_is_ctn(child) ? location.start : location.name_start)
                             : location.start;
            size_t finish = av_is_ctn(child)
                              ? content_end(text, location.start, location.end)
                              : location.end;
            if (start == SIZE_MAX || finish == SIZE_MAX) {
                return false;
            }
            apush(*edits, ((text_edit){ (int)start, (int)finish, xstrdup("") }));
        }
        char *insert = xstrdup("");
        for (size_t i = common; i < av_arr_size(value); i++) {
            const av_value *child = av_arr_get(value, i);
            char *piece = children ? node_text("-", child) : scalar_text(child);
            if (!piece) {
                free(insert);
                return false;
            }
            size_t n = strlen(insert), size = strlen(piece) + 8;
            insert = xrealloc(insert, n + size + 1);
            snprintf(insert + n, size + 1, children ? "\n    %s" : " %s", piece);
            free(piece);
        }
        if (*insert) {
            size_t at = children ? end - 1 : end;
            if (children) {
                char *indented = indent_node(insert, text, range.name_start);
                free(insert);
                insert = indented;
            }
            apush(*edits, ((text_edit){ (int)at, (int)at, insert }));
        } else {
            free(insert);
        }
        return true;
    }
    return replace_node(text, old, value, name, edits);
}

/* Whether the KDL writer emits exactly `text` for its values: no comments
 * or hand formatting to preserve. */
static bool written_canonically(const av_doc *doc, const char *text)
{
    char *written = av_doc_write(doc);
    bool same = written && !strcmp(written, text);
    free(written);
    return same;
}

char *asset_source_reconcile(const char *text, const av_value *value, char *error, size_t size)
{
    av_doc *old = av_doc_read(text, error, size);
    if (!old) {
        return NULL;
    }
    char *canonical = av_value_write(value);
    if (canonical && written_canonically(old, text)) {
        av_doc_free(old);
        return canonical;
    }
    /* Float32 literals are parsed as doubles until a reflected loader reads
     * them. Compare the desired disk values at that same boundary. */
    av_doc *target = canonical ? av_doc_read(canonical, error, size) : NULL;
    free(canonical);
    if (!target) {
        av_doc_free(old);
        return NULL;
    }
    value = av_doc_root(target);
    text_edit *edits = NULL;
    bool ok = collect_edits(text, av_doc_root(old), value, NULL, &edits);
    char *result = ok ? text_edits_apply(text, edits, acount(edits)) : NULL;
    if (result) {
        av_doc *check = av_doc_read(result, error, size);
        if (!check || !av_equals(av_doc_root(check), value)) {
            free(result);
            result = NULL;
        }
        av_doc_free(check);
    }
    if (!result && error && size) {
        snprintf(error, size, "Cannot preserve KDL source for this edit");
    }
    for (int i = 0; i < acount(edits); i++) {
        free((char *)edits[i].text);
    }
    afree(edits);
    av_doc_free(target);
    av_doc_free(old);
    return result;
}
