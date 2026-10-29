#include "authoring/source_edit_internal.h"
#include "authoring/source_literal.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool fail(char *error, size_t size, const char *message)
{
    if (size) {
        snprintf(error, size, "%s", message);
    }
    return false;
}
static bool identifier(const char *name)
{
    if (!name || !(isalpha((unsigned char)*name) || *name == '_')) {
        return false;
    }
    for (const unsigned char *p = (const unsigned char *)name + 1; *p; p++) {
        if (!(isalnum(*p) || *p == '_')) {
            return false;
        }
    }
    return true;
}
static char *member_name(const script_catalog *catalog, const char *name, char *error, size_t size)
{
    (void)catalog;
    return authoring_source_identifier(name, error, size);
}
static const script_catalog_field *field(const script_catalog_type *type, uint64_t id)
{
    for (int i = 0; type && i < type->field_count; i++) {
        if (type->fields[i].id == id) {
            return &type->fields[i];
        }
    }
    return NULL;
}
static const source_presentation_origin *origin(const authoring_source_presentation *p, uint64_t id)
{
    for (int i = 0; i < authoring_source_presentation_origin_count(p); i++) {
        const source_presentation_origin *o = authoring_source_presentation_origin_at(p, i);
        if (o->identity == id) {
            return o;
        }
    }
    return NULL;
}
bool source_edit_binding(
    const script_analysis *a,
    const script_catalog *catalog,
    const authoring_source_presentation *p,
    const authoring_edit_request *r,
    source_edit_changes *changes,
    char *error,
    size_t size
)
{
    const source_presentation_origin *target = origin(p, r->origin);
    if (!target || target->kind != SOURCE_PRESENTATION_EXPRESSION) {
        return fail(error, size, "Select an explicit value input for this binding");
    }
    int symbol = -1;
    for (int i = 0; i < a->symbol_count; i++) {
        if (a->symbols[i].id == r->symbol) {
            symbol = i;
            break;
        }
    }
    const script_catalog_type *component = NULL;
    if (r->binding == AUTHORING_BIND_COMPONENT) {
        component = script_catalog_type_by_id(catalog, r->component);
        if (!component || !component->component || a->owner_symbol < 0) {
            return fail(error, size, "Choose a registered component on this source owner");
        }
        if (r->symbol && (symbol < 0 || !script_catalog_type_assignable(
                                            catalog,
                                            a->symbols[a->owner_symbol].type,
                                            a->symbols[symbol].type
                                        ))) {
            return fail(error, size, "Choose an entity source for this component binding");
        }
        symbol = r->symbol ? symbol : a->owner_symbol;
    } else if (r->binding == AUTHORING_BIND_GLOBAL) {
        bool global = false;
        for (int i = 0; i < catalog->global_count; i++) {
            global |= catalog->globals[i].id == r->symbol;
        }
        if (!global || symbol < 0 || a->symbols[symbol].kind != SCRIPT_GLOBAL) {
            return fail(error, size, "Choose a declared global capability binding");
        }
    } else if (r->binding != AUTHORING_BIND_SYMBOL) {
        return fail(error, size, "This binding intent is not represented by the source catalog");
    }
    if (symbol < 0 || !identifier(a->symbols[symbol].name)) {
        return fail(error, size, "Choose a resolved symbol with a usable source identifier");
    }
    source_edit_connection value = { .symbol = a->symbols[symbol].id,
                                     .type = a->symbols[symbol].type };
    if (a->symbols[symbol].initializer >= 0 ||
        (value.type.flags & SCRIPT_TYPE_RESULTS)) {
        for (int i = 0; i < authoring_source_presentation_origin_count(p); i++) {
            const source_presentation_origin *o = authoring_source_presentation_origin_at(p, i);
            if (o->kind == SOURCE_PRESENTATION_SYMBOL && o->record == symbol) {
                value = source_edit_producer(a, catalog, p, o->identity, r->producer_result, error, size);
                break;
            }
        }
        if (!value.text) {
            return fail(error, size, "Choose a retained declared value or payload result");
        }
    } else {
        value.text = xstrdup(a->symbols[symbol].name);
    }
    const script_catalog_type
        *aggregate = component
                       ? component
                       : script_catalog_type_by_id(catalog, value.type.nominal);
    const script_catalog_field *selected = r->member ? field(aggregate, r->member)
                                                     : NULL;
    if (r->member && !selected) {
        free(value.text);
        return fail(error, size, "Choose a reflected field on the selected binding type");
    }
    char *component_name = component
                             ? member_name(
                                   catalog,
                                   component->source_name ? component->source_name
                                                          : component->name,
                                   error,
                                   size
                               )
                             : NULL;
    char *selected_name = selected ? member_name(catalog, selected->name, error, size)
                                   : NULL;
    if ((component && !component_name) || (selected && !selected_name)) {
        free(component_name);
        free(selected_name);
        free(value.text);
        return false;
    }
    size_t bytes = strlen(value.text) +
                   (component_name ? strlen(component_name) + 1 : 0) +
                   (selected_name ? strlen(selected_name) + 1 : 0) + 1;
    char *expression = xmalloc(bytes);
    snprintf(
        expression,
        bytes,
        "%s%s%s%s%s",
        value.text,
        component_name ? "." : "",
        component_name ? component_name : "",
        selected_name ? "." : "",
        selected_name ? selected_name : ""
    );
    free(value.text);
    value.text = expression;
    free(component_name);
    free(selected_name);
    apush(
        changes->patches,
        ((text_edit){ target->range.start, target->range.end, xstrdup(value.text) })
    );
    changes->text = text_edits_apply(a->text, changes->patches, acount(changes->patches));
    changes->text = source_edit_check_connection(
        a,
        catalog,
        changes->text,
        &value,
        NULL,
        target->range.start,
        error,
        size
    );
    free(value.text);
    if (!changes->text) {
        source_edit_changes_free(changes);
        return false;
    }
    return true;
}
