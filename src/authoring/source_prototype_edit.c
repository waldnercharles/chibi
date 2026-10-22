#include "authoring/source_prototype_edit.h"

#include "authoring/source_asset.h"
#include "authoring/source_literal.h"
#include "language/syntax.h"
#include "support/arena.h"
#include "support/array.h"
#include "support/text_edit.h"
#include "support/xalloc.h"
#include "types/catalog.h"
#include "types/text.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EDIT_MAX_DEPTH 64

typedef struct
{
    const prototype_registry *registry;
    const script_catalog *catalog;
    char *error;
    size_t size;
    arena storage;
} context;

typedef struct
{
    char *source;
    av_doc *doc;
    prototype *value;
    script_syntax *syntax;
} snapshot;

static bool fail(context *c, const char *message)
{
    if (c->size) {
        snprintf(c->error, c->size, "%s", message);
    }
    return false;
}
static TSNode field(TSNode node, const char *name)
{
    return ts_node_child_by_field_name(node, name, (uint32_t)strlen(name));
}
static bool is(TSNode node, const char *kind)
{
    return !ts_node_is_null(node) && !strcmp(ts_node_type(node), kind);
}
static TSNode declaration(const snapshot *s)
{
    TSNode root = ts_tree_root_node(s->syntax->tree);
    for (uint32_t i = 0; i < ts_node_named_child_count(root); i++) {
        TSNode node = ts_node_named_child(root, i);
        if (is(node, "prototype_declaration")) {
            return node;
        }
    }
    return (TSNode){ 0 };
}
static void snapshot_free(snapshot *s)
{
    free(s->source);
    av_doc_free(s->doc);
    prototype_free(s->value);
    script_syntax_free(s->syntax);
    *s = (snapshot){ 0 };
}
static bool prepare(context *c, char *source, snapshot *s)
{
    *s = (snapshot){ .source = source };
    if (strlen(source) > AUTHORING_PROTOTYPE_EDIT_MAX_BYTES) {
        return fail(c, "Prototype source exceeds edit limit");
    }
    s->doc = authoring_prototype_source_read(source, c->registry, c->error, c->size);
    if (!s->doc) {
        return false;
    }
    s->value = prototype_load_value(av_doc_root(s->doc), c->registry, c->error, c->size);
    if (!s->value) {
        return false;
    }
    s->syntax = script_parse(source);
    return true;
}
static bool append(context *c, char **out, const char *text)
{
    size_t size = strlen(text);
    if (size > AUTHORING_PROTOTYPE_EDIT_MAX_BYTES - (size_t)acount(*out)) {
        return fail(c, "Prototype source edit exceeds output limit");
    }
    if (!size) {
        return true;
    }
    int at = acount(*out);
    asetlen(*out, at + (int)size);
    memcpy(*out + at, text, size);
    return true;
}
static char *finish(char *text)
{
    int length = acount(text);
    char *out = xmalloc((size_t)length + 1);
    if (length) {
        memcpy(out, text, (size_t)length);
    }
    out[length] = 0;
    afree(text);
    return out;
}
static bool symbol(context *c, char **out, const char *name)
{
    char *text = authoring_source_identifier(name, c->error, c->size);
    bool ok = text && append(c, out, text);
    free(text);
    return ok;
}
static bool quoted(context *c, char **out, const char *text)
{
    pin_value value;
    const char *error = pin_value_text(&c->storage, &value, text, strlen(text));
    if (error) {
        return fail(c, error);
    }
    char *literal = authoring_source_literal(
        c->catalog,
        (script_type){ .storage = PIN_STR },
        &value,
        c->error,
        c->size
    );
    bool ok = literal && append(c, out, literal);
    free(literal);
    return ok;
}
static const script_catalog_type *component_type(context *c, const ecs_component_desc *desc)
{
    for (int i = 0; i < c->catalog->type_count; i++) {
        if (c->catalog->types[i].component_id == desc->id) {
            return &c->catalog->types[i];
        }
    }
    int index = script_catalog_find_type(c->catalog, desc->type->name);
    return index >= 0 ? &c->catalog->types[index] : NULL;
}
static const script_catalog_field *named_field(const script_catalog_type *type, const char *name, size_t size)
{
    for (int i = 0; type && i < type->field_count; i++) {
        if (strlen(type->fields[i].name) == size &&
            !strncmp(type->fields[i].name, name, size)) {
            return &type->fields[i];
        }
    }
    return NULL;
}
static const script_catalog_field *field_path(context *c, const script_catalog_type *type, const char *path)
{
    for (int depth = 0; depth < EDIT_MAX_DEPTH; depth++) {
        const char *dot = strchr(path, '.');
        const script_catalog_field
            *found = named_field(type, path, dot ? (size_t)(dot - path) : strlen(path));
        if (!found || !dot) {
            return found;
        }
        if (found->type.storage != PIN_STRUCT ||
            found->type.container != PIN_CONTAINER_SINGLE || !dot[1]) {
            return NULL;
        }
        type = script_catalog_type_by_id(c->catalog, found->type.nominal);
        path = dot + 1;
    }
    return NULL;
}
/* Native defaults may contain a null entity slot. A nonzero runtime ID is never
 * a source reference. Ordinary leaves use the shared checked literal writer. */
static bool typed(context *c, char **out, script_type expected, const pin_value *value, int depth)
{
    if (depth > EDIT_MAX_DEPTH) {
        return fail(c, "Prototype field nesting exceeds edit limit");
    }
    if (expected.storage == PIN_HANDLE && expected.container == PIN_CONTAINER_SINGLE) {
        return value->type == PIN_HANDLE &&
                       value->container == PIN_CONTAINER_SINGLE && !value->u64
                 ? append(c, out, "null")
                 : fail(c, "Runtime handles cannot be written as prototype source references");
    }
    if (expected.storage != PIN_STRUCT || expected.container != PIN_CONTAINER_SINGLE) {
        char *literal = authoring_source_literal(c->catalog, expected, value, c->error, c->size);
        bool ok = literal && append(c, out, literal);
        free(literal);
        return ok;
    }
    const script_catalog_type
        *type = script_catalog_type_by_id(c->catalog, expected.nominal);
    int actual = value->type_name
                   ? script_catalog_find_type(c->catalog, value->type_name)
                   : -1;
    if (!type || value->type != PIN_STRUCT || value->container != PIN_CONTAINER_SINGLE ||
        !value->ptr || actual < 0 || c->catalog->types[actual].size != type->size ||
        !script_catalog_type_assignable(
            c->catalog,
            expected,
            c->catalog->types[actual].type
        )) {
        return fail(c, "Prototype record does not match its reflected payload layout");
    }
    const type_desc *native = type_catalog_find(value->type_name);
    if (!native || native->size != type->size ||
        native->field_count != type->field_count) {
        return fail(c, "Prototype record has no matching native reflection");
    }
    if (!append(c, out, "{")) {
        return false;
    }
    int written = 0;
    for (int i = 0; i < native->field_count; i++) {
        const type_field *nf = &native->fields[i];
        const script_catalog_field *sf = named_field(type, nf->name, strlen(nf->name));
        if (!sf || nf->offset > native->size || nf->size > native->size - nf->offset) {
            return fail(c, "Prototype record field exceeds its reflected payload");
        }
        pin_value item;
        const char *error = pin_read_field(&c->storage, value->ptr, nf, &item);
        if (error) {
            return fail(c, error);
        }
        /* Omitted nullable strings retain zero initialization. The static
         * loader accepts string literals, not null, for authored strings. */
        if ((item.type == PIN_NAME && !item.str) ||
            (item.type == PIN_STR && !item.text)) {
            continue;
        }
        if ((written++ && !append(c, out, ", ")) || !symbol(c, out, nf->name) ||
            !append(c, out, ": ") || !typed(c, out, sf->type, &item, depth + 1)) {
            return false;
        }
    }
    return append(c, out, "}");
}
/* This is the static prototype schema, not a generic language formatter. The
 * context distinguishes symbolic references from ordinary strings. */
typedef enum
{
    VALUE_DATA,
    VALUE_SYMBOL,
    VALUE_COMPONENTS,
    VALUE_RECORD,
    VALUE_CHILDREN,
    VALUE_CHILD,
    VALUE_LINKS
} value_context;
static bool emit(context *, char **, const av_value *, value_context, const type_desc *, int);
static bool emit(context *c, char **out, const av_value *v, value_context kind, const type_desc *type, int depth)
{
    if (depth > EDIT_MAX_DEPTH) {
        return fail(c, "Prototype materialization exceeds nesting limit");
    }
    if (av_is_arr(v)) {
        if (!append(c, out, "[")) {
            return false;
        }
        for (size_t i = 0; i < av_arr_size(v); i++) {
            if ((i && !append(c, out, ", ")) ||
                !emit(c, out, av_arr_get(v, i), kind == VALUE_CHILDREN ? VALUE_CHILD : kind, type, depth + 1)) {
                return false;
            }
        }
        return append(c, out, "]");
    }
    if (av_is_obj(v)) {
        if (!append(c, out, "{")) {
            return false;
        }
        for (size_t i = 0; i < av_obj_size(v); i++) {
            const char *key = av_get_str(av_obj_key_at(v, i));
            const char *name = key;
            const type_desc *nested = NULL;
            value_context next = VALUE_DATA;
            if (kind == VALUE_COMPONENTS) {
                const ecs_component_desc *desc = prototype_registry_find(c->registry, key);
                if (!desc) {
                    return fail(c, "Materialized child names an unavailable component");
                }
                name = desc->name;
                nested = desc->type;
                next = VALUE_RECORD;
            } else if (kind == VALUE_RECORD) {
                const type_field *f = pin_type_field_path(type, key);
                if (!f) {
                    return fail(c, "Materialized component has an unknown reflected field");
                }
                next = f->type == PIN_HANDLE ? VALUE_SYMBOL : VALUE_DATA;
                if (f->type == PIN_STRUCT) {
                    nested = type_catalog_find(f->type_name);
                    next = VALUE_RECORD;
                    if (!nested) {
                        return fail(c, "Materialized record has no reflected type");
                    }
                }
            } else if (kind == VALUE_CHILD) {
                next = !strcmp(key, "prototype") || !strcmp(key, "behaviors")
                         ? VALUE_SYMBOL
                     : !strcmp(key, "children") ? VALUE_CHILDREN
                     : !strcmp(key, "components") || !strcmp(key, "retained_components")
                         ? VALUE_COMPONENTS
                         : VALUE_DATA;
            } else if (kind == VALUE_LINKS) {
                next = VALUE_SYMBOL;
            }
            if ((i && !append(c, out, ", ")) || !symbol(c, out, name) ||
                !append(c, out, ": ") ||
                !emit(c, out, av_obj_value_at(v, i), next, nested, depth + 1)) {
                return false;
            }
        }
        return append(c, out, "}");
    }
    if (av_is_str(v)) {
        return kind == VALUE_SYMBOL ? symbol(c, out, av_get_str(v))
                                    : quoted(c, out, av_get_str(v));
    }
    if (av_is_null(v)) {
        return append(c, out, "null");
    }
    pin_value value = { 0 };
    switch (av_get_type(v)) {
        case AV_BOOL:
            value.type = PIN_BOOL;
            value.b = av_get_bool(v);
            break;
        case AV_SINT:
            value.type = PIN_I64;
            value.i64 = av_get_sint(v);
            break;
        case AV_UINT:
            value.type = PIN_U64;
            value.u64 = av_get_uint(v);
            break;
        case AV_REAL:
            value.type = PIN_F64;
            value.d = av_get_num(v);
            break;
        default: return fail(c, "Unsupported prototype materialization value");
    }
    return typed(c, out, (script_type){ .storage = value.type }, &value, depth + 1);
}
static TSNode value_node(const snapshot *s, const av_value *v)
{
    av_source at = av_get_source(v);
    return ts_node_named_descendant_for_byte_range(
        ts_tree_root_node(s->syntax->tree),
        (uint32_t)at.start,
        (uint32_t)at.end
    );
}
static char *replace(context *c, const snapshot *s, int start, int end, const char *text)
{
    size_t length = strlen(s->source);
    if (start < 0 || end < start || (size_t)end > length ||
        strlen(text) > AUTHORING_PROTOTYPE_EDIT_MAX_BYTES -
                           (length - (size_t)(end - start))) {
        fail(c, "Prototype source replacement exceeds edit limit");
        return NULL;
    }
    text_edit edit = { start, end, text };
    return text_edits_apply(s->source, &edit, 1);
}
/* Insert before the closing delimiter, after a syntax-owned comma if needed.
 * Comments remain untouched; no textual punctuation inference is used. */
static char *insert(context *c, const snapshot *s, TSNode container, const char *text, bool separated)
{
    uint32_t count = ts_node_child_count(container);
    if (!count) {
        fail(c, "Prototype source has no insertion boundary");
        return NULL;
    }
    TSNode close = ts_node_child(container, count - 1), last = { 0 };
    for (uint32_t i = 1; i + 1 < count; i++) {
        TSNode child = ts_node_child(container, i);
        if (!is(child, "comment")) {
            last = child;
        }
    }
    char *addition = NULL;
    bool ok = (!separated || ts_node_is_null(last) || is(last, ",") ||
               append(c, &addition, ",")) &&
              append(c, &addition, "\n    ") && append(c, &addition, text) &&
              append(c, &addition, "\n");
    char *literal = finish(addition);
    char *result = ok ? replace(c, s, (int)ts_node_start_byte(close), (int)ts_node_start_byte(close), literal)
                      : NULL;
    free(literal);
    return result;
}
static char *set_member(context *c, const snapshot *s, av_value *object, const char *name, const char *literal)
{
    av_value *existing = av_obj_get(object, name);
    if (existing) {
        av_source at = av_get_source(existing);
        return replace(c, s, (int)at.start, (int)at.end, literal);
    }
    char *text = NULL;
    bool ok = symbol(c, &text, name) && append(c, &text, ": ") &&
              append(c, &text, literal);
    char *addition = finish(text);
    char *result = ok ? insert(c, s, value_node(s, object), addition, true) : NULL;
    free(addition);
    return result;
}
static char *set_property(context *c, const snapshot *s, const char *name, const char *literal)
{
    av_value *root = av_doc_root(s->doc), *existing = av_obj_get(root, name);
    if (existing) {
        av_source at = av_get_source(existing);
        return replace(c, s, (int)at.start, (int)at.end, literal);
    }
    char *text = NULL;
    bool ok = symbol(c, &text, name) && append(c, &text, ": ") &&
              append(c, &text, literal) && append(c, &text, ";");
    char *addition = finish(text);
    char *result = ok ? insert(c, s, field(declaration(s), "body"), addition, false)
                      : NULL;
    free(addition);
    return result;
}
static char *add_component(context *c, const snapshot *s, const char *name, const char *literal)
{
    char *text = NULL;
    bool ok = symbol(c, &text, name) && append(c, &text, " ") &&
              append(c, &text, literal);
    char *addition = finish(text);
    char *result = ok ? insert(c, s, field(declaration(s), "body"), addition, false)
                      : NULL;
    free(addition);
    return result;
}
static bool adopt(context *c, snapshot *s, char *source)
{
    snapshot next = { 0 };
    if (!source || !prepare(c, source, &next)) {
        snapshot_free(&next);
        return false;
    }
    snapshot_free(s);
    *s = next;
    return true;
}
static const prototype_component *find_component(const prototype *value, const char *name)
{
    for (int i = 0; i < acount(value->components); i++) {
        if (!strcmp(value->components[i].desc->name, name)) {
            return &value->components[i];
        }
    }
    return NULL;
}
static bool same_model(const prototype *a, const prototype *b)
{
    av_value *av = prototype_to_value(a), *bv = prototype_to_value(b);
    bool same = av_equals(av, bv) && acount(a->graphs) == acount(b->graphs);
    for (int i = 0; same && i < acount(a->graphs); i++) {
        same = !strcmp(a->graphs[i], b->graphs[i]);
    }
    av_value_free(av);
    av_value_free(bv);
    return same;
}
static bool push_edit(context *c, text_edit **edits, int start, int end, char *text)
{
    if (!text) {
        return false;
    }
    size_t added = strlen(text);
    for (int i = 0; i < acount(*edits); i++) {
        added += strlen((*edits)[i].text);
    }
    if (added > AUTHORING_PROTOTYPE_EDIT_MAX_BYTES) {
        free(text);
        return fail(c, "Materialized prototype exceeds edit limit");
    }
    apush(*edits, ((text_edit){ start, end, text }));
    return true;
}
static void edits_free(text_edit *edits)
{
    for (int i = 0; i < acount(edits); i++) {
        free((char *)edits[i].text);
    }
    afree(edits);
}
static bool missing_fields(
    context *c,
    const snapshot *s,
    av_value *local,
    const av_value *resolved,
    const type_desc *type,
    text_edit **edits,
    int depth
)
{
    if (!type || depth > EDIT_MAX_DEPTH) {
        return fail(c, "Materialized fields have no supported reflected layout");
    }
    char *addition = NULL;
    for (size_t i = 0; i < av_obj_size(resolved); i++) {
        const char *key = av_get_str(av_obj_key_at(resolved, i));
        av_value *present = av_obj_get(local, key),
                 *wanted = av_obj_value_at(resolved, i);
        const type_field *f = pin_type_field_path(type, key);
        if (!f) {
            afree(addition);
            return fail(c, "Materialized prototype has an unknown field");
        }
        if (present) {
            if (f->type == PIN_STRUCT && av_is_obj(present) && av_is_obj(wanted) &&
                !missing_fields(c, s, present, wanted, type_catalog_find(f->type_name), edits, depth + 1)) {
                afree(addition);
                return false;
            }
            continue;
        }
        bool ok = (!acount(addition) || append(c, &addition, ", ")) &&
                  symbol(c, &addition, key) && append(c, &addition, ": ") &&
                  emit(
                      c,
                      &addition,
                      wanted,
                      f->type == PIN_HANDLE   ? VALUE_SYMBOL
                      : f->type == PIN_STRUCT ? VALUE_RECORD
                                              : VALUE_DATA,
                      f->type == PIN_STRUCT ? type_catalog_find(f->type_name) : NULL,
                      depth + 1
                  );
        if (!ok) {
            afree(addition);
            return false;
        }
    }
    if (!acount(addition)) {
        afree(addition);
        return true;
    }
    TSNode node = value_node(s, local),
           close = ts_node_child(node, ts_node_child_count(node) - 1), last = { 0 };
    for (uint32_t i = 1; i + 1 < ts_node_child_count(node); i++) {
        TSNode child = ts_node_child(node, i);
        if (!is(child, "comment")) {
            last = child;
        }
    }
    char *text = NULL, *members = finish(addition);
    bool ok = (ts_node_is_null(last) || is(last, ",") || append(c, &text, ",")) &&
              append(c, &text, "\n    ") && append(c, &text, members) &&
              append(c, &text, "\n");
    free(members);
    char *literal = finish(text);
    if (!ok) {
        free(literal);
        return false;
    }
    return push_edit(c, edits, (int)ts_node_start_byte(close), (int)ts_node_start_byte(close), literal);
}
static bool materialize(context *c, snapshot *s)
{
    av_value *resolved = prototype_to_value(s->value);
    if (av_obj_get(resolved, "detached_components")) {
        av_value_free(resolved);
        return fail(c, "Detached inherited component storage has no static source representation");
    }
    text_edit *edits = NULL;
    char *addition = NULL;
    TSNode decl = declaration(s), bases = field(decl, "bases");
    bool ok = push_edit(
        c,
        &edits,
        (int)ts_node_start_byte(bases),
        (int)ts_node_end_byte(bases),
        xstrdup("")
    );
    av_value *locals = av_obj_get(av_doc_root(s->doc), "components"),
             *components = av_obj_get(resolved, "components");
    for (size_t i = 0; ok && i < av_obj_size(components); i++) {
        const ecs_component_desc *desc = prototype_registry_find(
            c->registry,
            av_get_str(av_obj_key_at(components, i))
        );
        av_value *local = av_obj_get(locals, desc->name),
                 *wanted = av_obj_value_at(components, i);
        if (local) {
            ok = missing_fields(c, s, local, wanted, desc->type, &edits, 0);
            continue;
        }
        ok = append(c, &addition, "\n    ") &&
             symbol(c, &addition, desc->name) && append(c, &addition, " ") &&
             emit(c, &addition, wanted, VALUE_RECORD, desc->type, 0) &&
             append(c, &addition, "\n");
    }
    /* prototype_to_value deliberately sorts behaviors for disk output. Preserve
     * current execution order here, including duplicate authored entries. */
    av_doc *graphs_doc = av_doc_new();
    av_value *graphs = av_arr(graphs_doc);
    av_doc_set_root(graphs_doc, graphs);
    for (int i = 0; i < acount(s->value->graphs); i++) {
        av_arr_add_str(graphs_doc, graphs, s->value->graphs[i]);
    }
    const char *keys[] = { "children", "links", "behaviors" };
    value_context kinds[] = { VALUE_CHILDREN, VALUE_LINKS, VALUE_SYMBOL };
    for (int i = 0; ok && i < 3; i++) {
        av_value *local = av_obj_get(av_doc_root(s->doc), keys[i]);
        const av_value *wanted = i == 2 ? graphs : av_obj_get(resolved, keys[i]);
        if (!wanted || av_equals(local, wanted)) {
            continue;
        }
        char *text = NULL;
        ok = emit(c, &text, wanted, kinds[i], NULL, 0);
        char *literal = finish(text);
        if (ok && local) {
            av_source at = av_get_source(local);
            ok = push_edit(c, &edits, (int)at.start, (int)at.end, literal);
            literal = NULL;
        } else if (ok) {
            ok = append(c, &addition, "\n    ") &&
                 append(c, &addition, keys[i]) && append(c, &addition, ": ") &&
                 append(c, &addition, literal) && append(c, &addition, ";\n");
        }
        free(literal);
    }
    av_doc_free(graphs_doc);
    av_value_free(resolved);
    if (ok && acount(addition)) {
        TSNode body = field(decl, "body"),
               close = ts_node_child(body, ts_node_child_count(body) - 1);
        ok = push_edit(
            c,
            &edits,
            (int)ts_node_start_byte(close),
            (int)ts_node_start_byte(close),
            finish(addition)
        );
        addition = NULL;
    }
    afree(addition);
    char *source = ok ? text_edits_apply(s->source, edits, acount(edits)) : NULL;
    edits_free(edits);
    snapshot next = { 0 };
    ok = source && prepare(c, source, &next);
    if (ok && !same_model(s->value, next.value)) {
        ok = fail(c, "Inherited composition cannot be materialized without changing its meaning");
    }
    if (!ok) {
        snapshot_free(&next);
        return false;
    }
    snapshot_free(s);
    *s = next;
    return true;
}
static prototype *inherited(context *c, const snapshot *s)
{
    av_doc *doc = av_doc_clone_value(av_doc_root(s->doc));
    av_value *root = av_doc_root(doc);
    av_obj_remove_key(root, "components");
    av_obj_remove_key(root, "behaviors");
    av_obj_remove_key(root, "children");
    av_obj_remove_key(root, "links");
    prototype *value = prototype_load_value(root, c->registry, c->error, c->size);
    av_doc_free(doc);
    return value;
}
static char *remove_component(context *c, const snapshot *s, av_value *component)
{
    TSNode node = value_node(s, component);
    while (!ts_node_is_null(node) && !is(node, "prototype_component")) {
        node = ts_node_parent(node);
    }
    if (ts_node_is_null(node)) {
        fail(c, "Prototype component has no source declaration");
        return NULL;
    }
    return replace(c, s, (int)ts_node_start_byte(node), (int)ts_node_end_byte(node), "");
}
static char *remove_behavior(context *c, const snapshot *s, av_value *array, const char *name)
{
    TSNode node = value_node(s, array);
    text_edit *edits = NULL;
    size_t index = 0;
    bool removed = false;
    for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
        TSNode child = ts_node_child(node, i);
        if (is(child, "comment")) {
            continue;
        }
        if (ts_node_is_named(child)) {
            if (index >= av_arr_size(array)) {
                afree(edits);
                fail(c, "Behavior array has inconsistent source items");
                return NULL;
            }
            removed = !strcmp(av_get_str(av_arr_get(array, index++)), name);
        } else if (!is(child, ",")) {
            continue;
        }
        if (removed) {
            apush(edits, ((text_edit){ (int)ts_node_start_byte(child), (int)ts_node_end_byte(child), "" }));
        }
    }
    /* A comma belongs to its preceding value, so each deleted value/comma is
     * visited once. A surviving last value may retain a legal trailing comma.
     * Comments and surviving values stay byte-for-byte in their original order. */
    if (index != av_arr_size(array) || !acount(edits)) {
        afree(edits);
        fail(c, "Behavior array has no matching source items");
        return NULL;
    }
    char *source = text_edits_apply(s->source, edits, acount(edits));
    afree(edits);
    return source;
}
static bool write_path(arena *storage, void *data, const type_desc *type, const char *path, pin_value value)
{
    const char *dot = strchr(path, '.');
    size_t size = dot ? (size_t)(dot - path) : strlen(path);
    for (int i = 0; type && i < type->field_count; i++) {
        const type_field *f = &type->fields[i];
        if (strlen(f->name) != size || strncmp(f->name, path, size)) {
            continue;
        }
        if (!dot) {
            return !pin_write_field(storage, data, f, value);
        }
        return f->type == PIN_STRUCT && write_path(
                                            storage,
                                            (char *)data + f->offset,
                                            type_catalog_find(f->type_name),
                                            dot + 1,
                                            value
                                        );
    }
    return false;
}
static char *set_field(
    context *c,
    const snapshot *s,
    const ecs_component_desc *desc,
    const authoring_prototype_edit *op
)
{
    const script_catalog_type *type = component_type(c, desc);
    const script_catalog_field *leaf = type ? field_path(c, type, op->field) : NULL;
    if (!leaf) {
        fail(c, "Prototype edit names an unknown reflected field");
        return NULL;
    }
    char *text = NULL;
    if (!typed(c, &text, leaf->type, &op->value, 0)) {
        afree(text);
        return NULL;
    }
    char *literal = finish(text);
    const prototype_component *current = find_component(s->value, desc->name);
    pin_value previous;
    char *previous_text = NULL;
    if (leaf->type.storage != PIN_HANDLE &&
        !pin_read_field_path(&c->storage, current->data, desc->type, op->field, &previous) &&
        typed(c, &previous_text, leaf->type, &previous, 0)) {
        char *previous_literal = finish(previous_text);
        bool unchanged = !strcmp(previous_literal, literal);
        free(previous_literal);
        if (unchanged) {
            free(literal);
            return xstrdup(s->source);
        }
    } else {
        afree(previous_text);
        if (c->size) {
            c->error[0] = 0;
        }
    }
    av_value *components = av_obj_get(av_doc_root(s->doc), "components"),
             *local = av_obj_get(components, desc->name);
    char *path = xstrdup(op->field), *part = path, *dot = strchr(part, '.');
    av_value *parent = local;
    bool missing_record = false;
    while (dot) {
        *dot = 0;
        parent = av_obj_get(parent, part);
        if (!av_is_obj(parent)) {
            missing_record = true;
            break;
        }
        part = dot + 1;
        dot = strchr(part, '.');
    }
    char *result = NULL;
    if (missing_record) {
        /* The language replaces an entire nested record. Author its resolved
         * siblings when creating the override, as existing Properties does. */
        const prototype_component *component = find_component(s->value, desc->name);
        void *copy = xmalloc(desc->element_size);
        memcpy(copy, component->data, desc->element_size);
        const char *first_dot = strchr(op->field, '.');
        size_t length = (size_t)(first_dot - op->field);
        char *top = xmalloc(length + 1);
        memcpy(top, op->field, length);
        top[length] = 0;
        pin_value whole;
        const script_catalog_field *top_field = named_field(type, top, length);
        bool ok = write_path(&c->storage, copy, desc->type, op->field, op->value) &&
                  !pin_read_field_path(&c->storage, copy, desc->type, top, &whole);
        text = NULL;
        ok = ok && typed(c, &text, top_field->type, &whole, 0);
        free(literal);
        literal = finish(text);
        if (ok && local) {
            result = set_member(c, s, local, top, literal);
        } else if (ok) {
            text = NULL;
            ok = append(c, &text, "{") && symbol(c, &text, top) &&
                 append(c, &text, ": ") && append(c, &text, literal) &&
                 append(c, &text, "}");
            char *record = finish(text);
            result = ok ? add_component(c, s, desc->name, record) : NULL;
            free(record);
        }
        free(top);
        free(copy);
    } else if (parent) {
        result = set_member(c, s, parent, part, literal);
    } else {
        text = NULL;
        bool ok = append(c, &text, "{") && symbol(c, &text, op->field) &&
                  append(c, &text, ": ") && append(c, &text, literal) &&
                  append(c, &text, "}");
        char *record = finish(text);
        result = ok ? add_component(c, s, desc->name, record) : NULL;
        free(record);
    }
    free(literal);
    free(path);
    return result;
}
static bool overlaps(const char *a, const char *b)
{
    size_t an = strlen(a), bn = strlen(b), n = an < bn ? an : bn;
    return !strncmp(a, b, n) && (an == bn || a[n] == '.' || b[n] == '.');
}
static bool validate_operations(context *c, const authoring_prototype_edit *ops, int count)
{
    for (int i = 0; i < count; i++) {
        const authoring_prototype_edit *op = &ops[i];
        bool behavior = op->kind == AUTHORING_PROTOTYPE_ATTACH_BEHAVIOR ||
                        op->kind == AUTHORING_PROTOTYPE_DETACH_BEHAVIOR;
        if (op->kind < AUTHORING_PROTOTYPE_SET_FIELD ||
            op->kind > AUTHORING_PROTOTYPE_DETACH_BEHAVIOR ||
            (behavior ? !op->behavior || !*op->behavior
                      : !op->component ||
                            !prototype_registry_find(c->registry, op->component)) ||
            (op->kind == AUTHORING_PROTOTYPE_SET_FIELD &&
             (!op->field || !*op->field || strlen(op->field) > 4096))) {
            return fail(c, "Invalid prototype source edit operation");
        }
        if (behavior) {
            char *name = authoring_source_identifier(op->behavior, c->error, c->size);
            bool exact = name && !strcmp(name, op->behavior);
            free(name);
            if (!exact) {
                return fail(c, "Behavior reference must use its exact source symbol");
            }
        }
        for (int j = 0; j < i; j++) {
            const authoring_prototype_edit *previous = &ops[j];
            bool was_behavior = previous->kind == AUTHORING_PROTOTYPE_ATTACH_BEHAVIOR ||
                                previous->kind == AUTHORING_PROTOTYPE_DETACH_BEHAVIOR;
            if (behavior && was_behavior && !strcmp(op->behavior, previous->behavior)) {
                return fail(c, "Conflicting prototype behavior operations");
            }
            if (!behavior && !was_behavior &&
                prototype_registry_find(c->registry, op->component) ==
                    prototype_registry_find(c->registry, previous->component) &&
                (op->kind != AUTHORING_PROTOTYPE_SET_FIELD ||
                 previous->kind != AUTHORING_PROTOTYPE_SET_FIELD ||
                 overlaps(op->field, previous->field))) {
                return fail(c, "Overlapping prototype component operations");
            }
        }
    }
    return true;
}
static bool apply(context *c, snapshot *s, const authoring_prototype_edit *op, bool *materialized)
{
    const ecs_component_desc *desc = op->component
                                       ? prototype_registry_find(c->registry, op->component)
                                       : NULL;
    if (desc && desc->derived_from) {
        return fail(c, "Computed components cannot be authored");
    }
    av_value *components = av_obj_get(av_doc_root(s->doc), "components");
    char *source = NULL;
    switch (op->kind) {
        case AUTHORING_PROTOTYPE_SET_FIELD:
            if (!find_component(s->value, desc->name)) {
                return fail(c, "Cannot edit a field of an absent component");
            }
            source = set_field(c, s, desc, op);
            break;
        case AUTHORING_PROTOTYPE_ADD_COMPONENT: {
            if (find_component(s->value, desc->name)) {
                return fail(c, "Prototype already contains this component");
            }
            const script_catalog_type *type = component_type(c, desc);
            if (!type) {
                return fail(c, "Component has no reflected source type");
            }
            char *text = NULL;
            bool ok = typed(c, &text, type->type, &op->value, 0);
            char *literal = finish(text);
            source = ok ? add_component(c, s, desc->name, literal) : NULL;
            free(literal);
            break;
        }
        case AUTHORING_PROTOTYPE_REMOVE_COMPONENT: {
            if (!find_component(s->value, desc->name)) {
                return fail(c, "Prototype does not contain this component");
            }
            prototype *base = inherited(c, s);
            if (!base) {
                return false;
            }
            bool inherited_member = find_component(base, desc->name) != NULL;
            prototype_free(base);
            if (inherited_member) {
                if (!materialize(c, s)) {
                    return false;
                }
                *materialized = true;
                components = av_obj_get(av_doc_root(s->doc), "components");
            }
            source = remove_component(c, s, av_obj_get(components, desc->name));
            break;
        }
        case AUTHORING_PROTOTYPE_ATTACH_BEHAVIOR: {
            if (prototype_has_graph(s->value, op->behavior)) {
                return fail(c, "Prototype already has this behavior");
            }
            char *text = NULL;
            bool ok = symbol(c, &text, op->behavior);
            char *literal = finish(text);
            av_value *behaviors = av_obj_get(av_doc_root(s->doc), "behaviors");
            if (ok && behaviors) {
                source = insert(c, s, value_node(s, behaviors), literal, true);
            } else if (ok) {
                text = NULL;
                ok = append(c, &text, "[") && append(c, &text, literal) &&
                     append(c, &text, "]");
                char *array = finish(text);
                source = ok ? set_property(c, s, "behaviors", array) : NULL;
                free(array);
            }
            free(literal);
            break;
        }
        case AUTHORING_PROTOTYPE_DETACH_BEHAVIOR: {
            if (!prototype_has_graph(s->value, op->behavior)) {
                return fail(c, "Prototype does not have this behavior");
            }
            prototype *base = inherited(c, s);
            if (!base) {
                return false;
            }
            bool inherited_member = prototype_has_graph(base, op->behavior);
            prototype_free(base);
            if (inherited_member) {
                if (!materialize(c, s)) {
                    return false;
                }
                *materialized = true;
            }
            av_value *behaviors = av_obj_get(av_doc_root(s->doc), "behaviors");
            source = remove_behavior(c, s, behaviors, op->behavior);
            break;
        }
    }
    return source && adopt(c, s, source);
}
bool authoring_prototype_source_edit(
    const char *source,
    const prototype_registry *registry,
    const script_catalog *catalog,
    const authoring_prototype_edit *operations,
    int count,
    authoring_prototype_edit_result *result,
    char *error,
    size_t size
)
{
    assert(source && registry && result && (!size || error));
    *result = (authoring_prototype_edit_result){ 0 };
    if (size) {
        error[0] = 0;
    }
    context c = { .registry = registry, .catalog = catalog, .error = error, .size = size };
    if (count && !catalog) {
        bool failed = fail(&c, "Prototype source edits require a reflected script catalog");
        arena_free(&c.storage);
        return failed;
    }
    if (count < 0 || count > AUTHORING_PROTOTYPE_EDIT_MAX_OPERATIONS ||
        (count && !operations)) {
        bool failed = fail(&c, "Prototype source edit exceeds operation limit");
        arena_free(&c.storage);
        return failed;
    }
    if (!validate_operations(&c, operations, count)) {
        arena_free(&c.storage);
        return false;
    }
    snapshot s = { 0 };
    if (!prepare(&c, xstrdup(source), &s)) {
        snapshot_free(&s);
        arena_free(&c.storage);
        return false;
    }
    bool materialized = false;
    for (int i = 0; i < count; i++) {
        if (!apply(&c, &s, &operations[i], &materialized)) {
            snapshot_free(&s);
            arena_free(&c.storage);
            return false;
        }
    }
    result->source = s.source;
    result->value = s.value;
    result->changed = strcmp(source, s.source) != 0;
    result->materialized = materialized;
    s.source = NULL;
    s.value = NULL;
    snapshot_free(&s);
    arena_free(&c.storage);
    return true;
}
void authoring_prototype_edit_result_free(authoring_prototype_edit_result *result)
{
    free(result->source);
    prototype_free(result->value);
    *result = (authoring_prototype_edit_result){ 0 };
}
