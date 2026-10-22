#include "authoring/source_prototype_edit.h"

#include "authoring/asset_compiler_host.h"
#include "authoring/source_asset.h"
#include "authoring/source_literal.h"
#include "support/arena.h"
#include "support/array.h"
#include "support/asset_document.h"
#include "support/xalloc.h"
#include "types/catalog.h"
#include "types/text.h"
#include "types/value_document.h"

#include <assert.h>
#include <math.h>
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
    int values;
    arena storage;
} context;

static bool fail(context *c, const char *message)
{
    if (c->size) {
        snprintf(c->error, c->size, "%s", message);
    }
    return false;
}
static bool text_type(context *c, pin_type expected, pin_value *value)
{
    if (expected == PIN_NAME && value->type == PIN_STR) {
        const char *text;
        const char *error = pin_text_cstring(&c->storage, *value, &text, NULL);
        if (error) {
            return fail(c, error);
        }
        value->type = PIN_NAME;
        value->str = text;
    } else if (expected == PIN_STR && value->type == PIN_NAME) {
        const char *text = value->str ? value->str : "";
        const char *type_name = value->type_name;
        const char *error = pin_value_text(&c->storage, value, text, strlen(text));
        if (error) {
            return fail(c, error);
        }
        value->type_name = type_name;
    }
    return true;
}
static const script_catalog_type *component_type(context *c, const ecs_component_desc *desc)
{
    for (int i = 0; i < c->catalog->type_count; i++) {
        if (c->catalog->types[i].component_id == desc->id) {
            return &c->catalog->types[i];
        }
    }
    int index = desc->type ? script_catalog_find_type(c->catalog, desc->type->name)
                           : -1;
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

static size_t field_size(const type_field *field)
{
    switch (field->type) {
        case PIN_BOOL: return sizeof(bool);
        case PIN_I8:
        case PIN_U8: return 1;
        case PIN_I16:
        case PIN_U16: return 2;
        case PIN_I32:
        case PIN_U32:
        case PIN_F32: return 4;
        case PIN_I64:
        case PIN_U64:
        case PIN_F64:
        case PIN_HANDLE: return 8;
        case PIN_NAME:
        case PIN_STR: return sizeof(char *);
        case PIN_ENUM: return field->size <= sizeof(int) ? field->size : 0;
        case PIN_STRUCT: {
            const type_desc *type = type_catalog_find(field->type_name);
            return type ? type->size : 0;
        }
        default: return 0;
    }
}

static bool field_fits(const type_desc *type, const type_field *field)
{
    size_t width = field_size(field);
    return width && field->size == width && field->offset <= type->size &&
           width <= type->size - field->offset;
}

static bool write_path(context *c, void *data, const type_desc *type, const char *path, pin_value value, int depth)
{
    if (!type || depth >= EDIT_MAX_DEPTH) {
        return false;
    }
    const char *dot = strchr(path, '.');
    size_t size = dot ? (size_t)(dot - path) : strlen(path);
    for (int i = 0; i < type->field_count; i++) {
        const type_field *f = &type->fields[i];
        if (strlen(f->name) != size || strncmp(f->name, path, size)) {
            continue;
        }
        if (!field_fits(type, f)) {
            return false;
        }
        if (!dot) {
            bool text = (f->type == PIN_NAME || f->type == PIN_STR) &&
                        (value.type == PIN_NAME || value.type == PIN_STR);
            if ((!text && value.type != f->type) ||
                value.container != PIN_CONTAINER_SINGLE ||
                (f->type == PIN_STRUCT && !value.ptr)) {
                return false;
            }
            if (f->type == PIN_STRUCT) {
                const type_desc *payload = value.type_name
                                             ? type_catalog_find(value.type_name)
                                             : NULL;
                if (!payload || payload->size != f->size) {
                    return false;
                }
            }
            return text_type(c, f->type, &value) &&
                   !pin_write_field(&c->storage, data, f, value);
        }
        return f->type == PIN_STRUCT && dot[1] &&
               write_path(
                   c,
                   (char *)data + f->offset,
                   type_catalog_find(f->type_name),
                   dot + 1,
                   value,
                   depth + 1
               );
    }
    return false;
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
        bool parameter = op->kind == AUTHORING_PROTOTYPE_SET_PARAMETER;
        bool field_edit = op->kind == AUTHORING_PROTOTYPE_SET_FIELD ||
                          op->kind == AUTHORING_PROTOTYPE_RESET_FIELD_OVERRIDE;
        if (op->kind < AUTHORING_PROTOTYPE_SET_FIELD ||
            op->kind > AUTHORING_PROTOTYPE_RESET_FIELD_OVERRIDE ||
            (behavior ? !op->behavior || !*op->behavior
             : parameter ? false
                         : !op->component ||
                               !prototype_registry_find(c->registry, op->component)) ||
            ((field_edit || parameter) &&
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
            bool was_parameter = previous->kind == AUTHORING_PROTOTYPE_SET_PARAMETER;
            if (parameter && was_parameter && !strcmp(op->field, previous->field)) {
                return fail(c, "Conflicting prototype parameter operations");
            }
            bool was_field = previous->kind == AUTHORING_PROTOTYPE_SET_FIELD ||
                             previous->kind == AUTHORING_PROTOTYPE_RESET_FIELD_OVERRIDE;
            if (!behavior && !was_behavior && !parameter && !was_parameter &&
                prototype_registry_find(c->registry, op->component) ==
                    prototype_registry_find(c->registry, previous->component) &&
                (!field_edit || !was_field || overlaps(op->field, previous->field))) {
                return fail(c, "Overlapping prototype component operations");
            }
        }
    }
    return true;
}

static char *reconcile(const char *source, const av_value *value, char *error, size_t size)
{
    av_doc *old = asset_document_read(source, error, size);
    asset_compiler_diagnostic diagnostic = { 0 };
    char *result = old
                     ? asset_source_reconcile_compiled(source, av_doc_root(old), value, NULL, &diagnostic)
                     : NULL;
    if (!result && old && size) {
        snprintf(error, size, "%s", diagnostic.message);
    }
    av_doc_free(old);
    return result;
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

static av_value *object(av_doc *doc, av_value *parent, const char *key)
{
    av_value *value = av_obj_get(parent, key);
    if (!av_is_obj(value)) {
        value = av_obj(doc);
        av_obj_set(doc, parent, key, value);
    }
    return value;
}

/* These checks concern native payload layout and lifetimes. Source spelling and
 * patch policy remain in the authored facade. Never render a C source literal. */
static av_value *project_value(context *c, av_doc *doc, script_type expected, const pin_value *value, int depth)
{
    pin_value normalized = *value;
    if (!text_type(c, expected.storage, &normalized)) {
        return NULL;
    }
    value = &normalized;
    bool text = (expected.storage == PIN_NAME || expected.storage == PIN_STR) &&
                (value->type == PIN_NAME || value->type == PIN_STR);
    if (depth >= EDIT_MAX_DEPTH || ++c->values > 16384 ||
        expected.flags & (SCRIPT_TYPE_ERROR | SCRIPT_TYPE_REFERENCE |
                          SCRIPT_TYPE_RESULTS | SCRIPT_TYPE_UNTYPED) ||
        expected.container != PIN_CONTAINER_SINGLE ||
        value->container != PIN_CONTAINER_SINGLE ||
        (!text && expected.storage != value->type) || value->record) {
        fail(c, "Prototype edit value does not match its native reflected type");
        return NULL;
    }
    if (value->type == PIN_HANDLE) {
        if (value->u64) {
            fail(c, "Runtime handles cannot be authored as entity references");
            return NULL;
        }
        return av_null(doc);
    }
    script_reference_kind reference = script_reference_kind_for_type(expected);
    if (reference == SCRIPT_REFERENCE_PROTOTYPE &&
        (!value->str ||
         script_catalog_find_capability(c->catalog, value->str, SCRIPT_CAPABILITY_PROTOTYPE) <
             0)) {
        fail(c, "Prototype reference must resolve to a declared prototype");
        return NULL;
    }
    if (reference == SCRIPT_REFERENCE_COMPONENT) {
        int index = value->str ? script_catalog_find_type(c->catalog, value->str)
                               : -1;
        if (index < 0 || !c->catalog->types[index].component) {
            fail(c, "Component reference must resolve to a declared component");
            return NULL;
        }
    }
    if (reference == SCRIPT_REFERENCE_RELATIONSHIP) {
        bool known = value->u64 == SCRIPT_CHILD_OF;
        for (int i = 0; !known && i < c->catalog->relationship_count; i++) {
            known = c->catalog->relationships[i].id == value->u64;
        }
        if (!known) {
            fail(c, "Relationship reference must resolve to a declared relationship");
            return NULL;
        }
    }
    const script_catalog_type *declared = NULL;
    if (expected.nominal && reference == SCRIPT_REFERENCE_NONE) {
        declared = script_catalog_type_by_id(c->catalog, expected.nominal);
        int index = value->type_name
                      ? script_catalog_find_type(c->catalog, value->type_name)
                      : -1;
        if (!declared || index < 0 ||
            !script_catalog_type_assignable(
                c->catalog,
                expected,
                c->catalog->types[index].type
            ) ||
            (value->type == PIN_STRUCT &&
             declared->size != c->catalog->types[index].size)) {
            fail(c, "Prototype edit nominal type does not match its native layout");
            return NULL;
        }
    }
    if (value->type == PIN_STRUCT) {
        const type_desc *native = value->type_name
                                    ? type_catalog_find(value->type_name)
                                    : NULL;
        if (!declared || !native || !value->ptr || native->size != declared->size ||
            native->size > AUTHORING_PROTOTYPE_EDIT_MAX_BYTES ||
            native->field_count < 0 || native->field_count != declared->field_count ||
            (native->field_count && !native->fields)) {
            fail(c, "Prototype edit struct has an invalid native payload layout");
            return NULL;
        }
        av_value *out = av_obj(doc);
        for (int i = 0; i < native->field_count; i++) {
            const type_field *field = &native->fields[i];
            const script_catalog_field
                *contract = named_field(declared, field->name, strlen(field->name));
            if (!contract || contract->offset != field->offset ||
                contract->type.storage != field->type || !field_fits(native, field)) {
                fail(c, "Prototype edit field exceeds its native reflected layout");
                return NULL;
            }
            pin_value member;
            const char *error = pin_read_field(&c->storage, value->ptr, field, &member);
            if (error) {
                fail(c, error);
                return NULL;
            }
            av_value *projected = project_value(c, doc, contract->type, &member, depth + 1);
            if (!projected) {
                return NULL;
            }
            av_obj_set(
                doc,
                out,
                field->name,
                field->color ? av_color(doc, member.u) : projected
            );
        }
        return out;
    }
    if (value->type == PIN_FLOW || value->type == PIN_TASK ||
        value->type == PIN_UNION || (value->type == PIN_F32 && !isfinite(value->f)) ||
        (value->type == PIN_F64 && !isfinite(value->d))) {
        fail(c, "Prototype edit requires a finite native scalar value");
        return NULL;
    }
    av_value *out = pin_value_to_document(doc, value);
    if (!out) {
        fail(c, "Prototype edit value cannot cross the native document boundary");
    }
    return out;
}

static av_value *project(context *c, av_doc *doc, script_type expected, const pin_value *value)
{
    return project_value(c, doc, expected, value, 0);
}

static bool has_behavior(const prototype *model, const char *name)
{
    for (int i = 0; i < acount(model->graphs); i++) {
        if (!strcmp(model->graphs[i], name)) {
            return true;
        }
    }
    return false;
}

static bool materialize(
    context *c,
    av_doc *doc,
    const prototype *model,
    const authoring_prototype_edit *op,
    bool *changed
)
{
    const av_value *root = av_doc_root(doc), *includes = av_obj_get(root, "includes");
    if (!av_arr_size(includes)) {
        return true;
    }
    av_doc *base_doc = av_doc_new();
    av_value *base_root = av_obj(base_doc);
    av_doc_set_root(base_doc, base_root);
    av_obj_add_str(base_doc, base_root, "name", model->name);
    av_obj_set(base_doc, base_root, "includes", av_clone(base_doc, includes));
    prototype *base = prototype_load_value(base_root, c->registry, c->error, c->size);
    av_doc_free(base_doc);
    if (!base) {
        return false;
    }
    bool inherited = op->kind == AUTHORING_PROTOTYPE_REMOVE_COMPONENT
                       ? find_component(
                             base,
                             prototype_registry_find(c->registry, op->component)->name
                         ) != NULL
                       : has_behavior(base, op->behavior);
    prototype_free(base);
    if (!inherited) {
        return true;
    }
    prototype standalone = *model;
    standalone.authored = false;
    av_value *value = prototype_to_value(&standalone);
    if (!value) {
        return fail(c, "Cannot project the resolved prototype for this edit");
    }
    av_doc_set_root(doc, av_clone(doc, value));
    av_value_free(value);
    *changed = true;
    return true;
}

static char *materialized_source(context *c, const char *source, const av_value *value)
{
    asset_compiler_diagnostic diagnostic = { 0 };
    av_doc *columns = asset_source_value_columns(value, &diagnostic);
    av_doc *request = av_doc_new();
    av_value *args = av_arr(request);
    av_doc_set_root(request, args);
    av_arr_add_str(request, args, source);
    for (size_t i = 0; columns && i < 8; i++) {
        av_arr_append(args, av_clone(request, av_arr_get(av_doc_root(columns), i)));
    }
    char *next = columns ? asset_source_invoke_bytes(
                               "project:asset_compiler:1:Materialize:callable",
                               args,
                               NULL,
                               &diagnostic
                           )
                         : NULL;
    if (!next) {
        fail(c, diagnostic.message);
    }
    av_doc_free(columns);
    av_doc_free(request);
    return next;
}

static char *parameter_source(context *c, const char *source, const authoring_prototype_edit *op)
{
    const pin_value *value = &op->value;
    int kind = 0;
    int64_t signed_value = 0;
    uint64_t unsigned_value = 0;
    double real = 0;
    bool boolean = false;
    const char *text = "";
    if (value->container != PIN_CONTAINER_SINGLE || value->record) {
        fail(c, "Parameters require native scalar values");
        return NULL;
    }
    switch (value->type) {
        case PIN_BOOL:
            kind = 1;
            boolean = value->b;
            break;
        case PIN_I32:
            kind = 2;
            signed_value = value->i;
            break;
        case PIN_I64:
            kind = 2;
            signed_value = value->i64;
            break;
        case PIN_U32:
            kind = 3;
            unsigned_value = value->u;
            break;
        case PIN_U64:
            kind = 3;
            unsigned_value = value->u64;
            break;
        case PIN_F64:
            kind = 4;
            real = value->d;
            break;
        case PIN_STR:
            kind = 5;
            {
                const char *error = pin_text_cstring(&c->storage, *value, &text, NULL);
                if (error) {
                    fail(c, error);
                    return NULL;
                }
            }
            break;
        default:
            fail(c, "Parameters require bool, integer, number or string values");
            return NULL;
    }
    if ((kind == 4 && !isfinite(real)) || (kind == 5 && !text)) {
        fail(c, "Parameter value is not a finite scalar literal");
        return NULL;
    }
    av_doc *request = av_doc_new();
    av_value *args = av_arr(request);
    av_doc_set_root(request, args);
    av_arr_add_str(request, args, source);
    av_arr_add_str(request, args, op->field);
    av_arr_append(args, av_sint(request, kind));
    av_arr_add_str(request, args, text);
    av_arr_append(args, av_sint(request, signed_value));
    av_arr_append(args, av_uint(request, unsigned_value));
    av_arr_append(args, av_real(request, real));
    av_arr_append(args, av_bool(request, boolean));
    asset_compiler_diagnostic diagnostic = { 0 };
    char *next = asset_source_invoke_bytes(
        "project:asset_compiler:1:SetParameter:callable",
        args,
        NULL,
        &diagnostic
    );
    if (!next) {
        fail(c, diagnostic.message);
    }
    av_doc_free(request);
    return next;
}

static bool apply(context *c, av_doc *doc, const prototype *model, const authoring_prototype_edit *op)
{
    av_value *root = av_doc_root(doc);
    if (op->kind == AUTHORING_PROTOTYPE_ATTACH_BEHAVIOR ||
        op->kind == AUTHORING_PROTOTYPE_DETACH_BEHAVIOR) {
        if (op->kind == AUTHORING_PROTOTYPE_ATTACH_BEHAVIOR &&
            has_behavior(model, op->behavior)) {
            return fail(c, "Prototype already contains this behavior");
        }
        av_value *behaviors = av_obj_get(root, "behaviors");
        bool found = false;
        for (size_t i = 0; i < av_arr_size(behaviors);) {
            const char *name = av_get_str(av_arr_get(behaviors, i));
            if (name && !strcmp(name, op->behavior)) {
                found = true;
                if (op->kind == AUTHORING_PROTOTYPE_DETACH_BEHAVIOR) {
                    av_arr_remove(behaviors, i);
                    continue;
                }
            }
            i++;
        }
        if (op->kind == AUTHORING_PROTOTYPE_DETACH_BEHAVIOR) {
            return found || fail(c, "Prototype does not contain this local behavior");
        }
        if (found) {
            return fail(c, "Prototype already contains this behavior");
        }
        if (!av_is_arr(behaviors)) {
            behaviors = av_arr(doc);
            av_obj_set(doc, root, "behaviors", behaviors);
        }
        av_arr_append(behaviors, av_str(doc, op->behavior));
        return true;
    }
    const ecs_component_desc *desc = prototype_registry_find(c->registry, op->component);
    if (!desc || desc->derived_from) {
        return fail(c, "Computed components cannot be authored");
    }
    av_value *components = object(doc, root, "components");
    const prototype_component *current = find_component(model, desc->name);
    if (op->kind == AUTHORING_PROTOTYPE_ADD_COMPONENT) {
        if (current) {
            return fail(c, "Prototype already contains this component");
        }
        const script_catalog_type *type = component_type(c, desc);
        if (!type) {
            return fail(c, "Component has no reflected source type");
        }
        av_value *value = project(c, doc, type->type, &op->value);
        return value && av_obj_set(doc, components, desc->name, value);
    }
    if (!current) {
        return fail(c, "Prototype does not contain this component");
    }
    if (op->kind == AUTHORING_PROTOTYPE_REMOVE_COMPONENT) {
        bool removed = av_obj_remove_key(components, desc->name);
        av_obj_remove_key(av_obj_get(root, "overrides"), desc->name);
        return removed || fail(c, "Prototype does not contain this local component");
    }
    if (op->kind == AUTHORING_PROTOTYPE_RESET_FIELD_OVERRIDE) {
        if (strchr(op->field, '.')) {
            return fail(c, "Reset the complete native record override");
        }
        av_value *overrides = av_obj_get(root, "overrides");
        av_value *component = av_obj_get(overrides, desc->name);
        return av_obj_remove_key(component, op->field) ||
               fail(c, "Field has no explicit override");
    }
    const script_catalog_type *type = component_type(c, desc);
    if (!type) {
        return fail(c, "Component has no reflected source type");
    }
    const script_catalog_field *leaf = field_path(c, type, op->field);
    if (!leaf) {
        return fail(c, "Prototype edit names an invalid reflected field");
    }
    av_value *value = project(c, doc, leaf->type, &op->value);
    if (!value) {
        return false;
    }
    const char *dot = strchr(op->field, '.');
    av_value *component = object(doc, components, desc->name);
    if (!dot) {
        return av_obj_set(doc, component, op->field, value);
    }
    /* Native records replace a whole reflected field. Copy resolved siblings
     * before changing a leaf, including when its parent was inherited. */
    size_t length = (size_t)(dot - op->field);
    char *top = xmalloc(length + 1);
    memcpy(top, op->field, length);
    top[length] = 0;
    if (!desc->type || desc->element_size != desc->type->size ||
        desc->element_size > AUTHORING_PROTOTYPE_EDIT_MAX_BYTES || !current->data) {
        free(top);
        return fail(c, "Component has an invalid native payload layout");
    }
    void *copy = xmalloc(desc->element_size);
    memcpy(copy, current->data, desc->element_size);
    pin_value whole;
    bool ok = write_path(c, copy, desc->type, op->field, op->value, 0) &&
              !pin_read_field_path(&c->storage, copy, desc->type, top, &whole);
    const script_catalog_field *outer = named_field(type, top, length);
    if (!ok || !outer) {
        free(copy);
        free(top);
        return fail(c, "Invalid native record field edit");
    }
    av_value *record = project(c, doc, outer->type, &whole);
    ok = record && av_obj_set(doc, component, top, record);
    free(copy);
    free(top);
    return ok;
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
    if (count < 0 || count > AUTHORING_PROTOTYPE_EDIT_MAX_OPERATIONS ||
        (count && (!operations || !catalog)) ||
        strlen(source) > AUTHORING_PROTOTYPE_EDIT_MAX_BYTES) {
        return fail(&c, "Invalid prototype source edit bounds or catalog");
    }
    if (!validate_operations(&c, operations, count)) {
        return false;
    }
    char *current = xstrdup(source);
    bool materialized = false;
    for (int i = 0; i < count; i++) {
        av_doc *doc = authoring_prototype_source_read(current, registry, error, size);
        prototype *model = doc ? prototype_load_value(av_doc_root(doc), registry, error, size)
                               : NULL;
        const authoring_prototype_edit *op = &operations[i];
        bool flattened = false;
        bool valid = model != NULL;
        if (valid && (op->kind == AUTHORING_PROTOTYPE_REMOVE_COMPONENT ||
                      op->kind == AUTHORING_PROTOTYPE_DETACH_BEHAVIOR)) {
            valid = materialize(&c, doc, model, op, &flattened);
        }
        char *next = NULL;
        if (valid && op->kind == AUTHORING_PROTOTYPE_SET_PARAMETER) {
            next = parameter_source(&c, current, op);
        } else if (valid && apply(&c, doc, model, op)) {
            next = flattened ? materialized_source(&c, current, av_doc_root(doc))
                             : reconcile(current, av_doc_root(doc), error, size);
        }
        materialized |= flattened;
        prototype_free(model);
        av_doc_free(doc);
        if (!next) {
            free(current);
            arena_free(&c.storage);
            return false;
        }
        free(current);
        current = next;
        if (strlen(current) > AUTHORING_PROTOTYPE_EDIT_MAX_BYTES) {
            free(current);
            arena_free(&c.storage);
            return fail(&c, "Prototype source edit exceeds its output byte limit");
        }
    }
    av_doc *doc = authoring_prototype_source_read(current, registry, error, size);
    prototype *value = doc ? prototype_load_value(av_doc_root(doc), registry, error, size)
                           : NULL;
    av_doc_free(doc);
    if (!value) {
        free(current);
        arena_free(&c.storage);
        return false;
    }
    result->source = current;
    result->value = value;
    result->changed = strcmp(source, current) != 0;
    result->materialized = materialized;
    arena_free(&c.storage);
    return true;
}

void authoring_prototype_edit_result_free(authoring_prototype_edit_result *result)
{
    free(result->source);
    prototype_free(result->value);
    *result = (authoring_prototype_edit_result){ 0 };
}
