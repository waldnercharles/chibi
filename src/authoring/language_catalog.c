#include "authoring/language_catalog.h"

#include "authoring/native_bindings.h"
#include "authoring/source_catalog_snapshot.h"
#include "capabilities/catalog_index.h"
#include "support/array.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct authoring_language_catalog
{
    int references;
    native_capabilities *native;
    arena storage;
    script_catalog view;
    script_catalog_type *types;
    script_catalog_capability *capabilities;
    script_catalog_global *globals;
    script_catalog_relationship *relationships;
    script_catalog_template *templates;
    script_catalog_specialization *specializations;
};
typedef struct
{
    catalog_index types, capabilities, globals, templates, specializations;
} catalog_indices;
static bool relationship(authoring_language_catalog *c, script_catalog_relationship value)
{
    if (!value.id || !value.name || !value.name[0] ||
        (value.traversable && !value.exclusive)) {
        return false;
    }
    bool child_of = !strcmp(value.name, "ChildOf");
    if ((value.id == SCRIPT_CHILD_OF || child_of) &&
        (value.id != SCRIPT_CHILD_OF || !child_of || !value.exclusive ||
         !value.traversable)) {
        return false;
    }
    for (int i = 0; i < acount(c->relationships); i++) {
        const script_catalog_relationship *prior = &c->relationships[i];
        if (prior->id == value.id || !strcmp(prior->name, value.name)) {
            return prior->id == value.id && !strcmp(prior->name, value.name) &&
                   prior->exclusive == value.exclusive &&
                   prior->traversable == value.traversable;
        }
    }
    apush(c->relationships, value);
    return true;
}
static bool append(authoring_language_catalog *c, const script_catalog *source, catalog_indices *indices)
{
    if (source->relationship_count < 0 ||
        (source->relationship_count && !source->relationships)) {
        return false;
    }
    script_catalog copy;
    if (!source_catalog_snapshot(&c->storage, source, &copy)) {
        return false;
    }
    for (int i = 0; i < copy.type_count; i++) {
        if (catalog_index_add(&indices->types, copy.types[i].id, acount(c->types)) >= 0) {
            return false;
        }
        apush(c->types, copy.types[i]);
    }
    for (int i = 0; i < copy.capability_count; i++) {
        if (catalog_index_add(&indices->capabilities, copy.capabilities[i].id, acount(c->capabilities)) >=
            0) {
            return false;
        }
        apush(c->capabilities, copy.capabilities[i]);
    }
    for (int i = 0; i < copy.global_count; i++) {
        if (catalog_index_add(&indices->globals, copy.globals[i].id, acount(c->globals)) >=
            0) {
            return false;
        }
        apush(c->globals, copy.globals[i]);
    }
    for (int i = 0; i < copy.relationship_count; i++) {
        if (!relationship(c, copy.relationships[i])) {
            return false;
        }
    }
    for (int i = 0; i < copy.template_count; i++) {
        if (catalog_index_add(&indices->templates, copy.templates[i].id, acount(c->templates)) >=
            0) {
            return false;
        }
        apush(c->templates, copy.templates[i]);
    }
    for (int i = 0; i < copy.specialization_count; i++) {
        if (catalog_index_add(
                &indices->specializations,
                copy.specializations[i].id,
                acount(c->specializations)
            ) >= 0) {
            return false;
        }
        apush(c->specializations, copy.specializations[i]);
    }
    return true;
}
authoring_language_catalog *authoring_language_catalog_new(
    const authoring_language_catalog_input *input,
    char *error,
    size_t size
)
{
    assert(!size || error);
    if (!input || !input->native) {
        if (size) {
            snprintf(error, size, "An explicit native capability table is required");
        }
        return NULL;
    }
    authoring_language_catalog *c = xcalloc(1, sizeof *c);
    c->references = 1;
    arena_init(&c->storage, 4096);
    c->native = native_capabilities_retain(input->native);
    const script_catalog *native = native_capabilities_catalog(c->native);
    c->view.entity_type = native->entity_type;
    c->view.tick_event = native->tick_event;
    const script_catalog *declared = script_catalog_project();
    catalog_indices indices = { 0 };
    bool appended = append(c, native, &indices) &&
                    (!input->project || append(c, input->project, &indices)) &&
                    (!declared || append(c, declared, &indices));
    catalog_index_free(&indices.types);
    catalog_index_free(&indices.capabilities);
    catalog_index_free(&indices.globals);
    catalog_index_free(&indices.templates);
    catalog_index_free(&indices.specializations);
    if (!appended) {
        if (size) {
            snprintf(error, size, "Conflicting or unretainable project declarations");
        }
        authoring_language_catalog_free(c);
        return NULL;
    }
    c->view.types = c->types;
    c->view.type_count = acount(c->types);
    c->view.capabilities = c->capabilities;
    c->view.capability_count = acount(c->capabilities);
    c->view.globals = c->globals;
    c->view.global_count = acount(c->globals);
    c->view.relationships = c->relationships;
    c->view.relationship_count = acount(c->relationships);
    c->view.templates = c->templates;
    c->view.template_count = acount(c->templates);
    c->view.specializations = c->specializations;
    c->view.specialization_count = acount(c->specializations);
    if (size) {
        error[0] = 0;
    }
    return c;
}
const script_catalog *authoring_language_catalog_view(const authoring_language_catalog *c)
{
    assert(c);
    return &c->view;
}
authoring_language_catalog *authoring_language_catalog_retain(authoring_language_catalog *c)
{
    assert(c && c->references > 0 && c->references < INT_MAX);
    c->references++;
    return c;
}
void authoring_language_catalog_free(authoring_language_catalog *c)
{
    if (!c) {
        return;
    }
    assert(c->references > 0);
    if (--c->references) {
        return;
    }
    afree(c->types);
    afree(c->capabilities);
    afree(c->globals);
    afree(c->relationships);
    afree(c->templates);
    afree(c->specializations);
    arena_free(&c->storage);
    native_capabilities_free(c->native);
    free(c);
}
script_lower_bindings authoring_language_bindings(const authoring_language_catalog *c)
{
    assert(c);
    return authoring_native_bindings(c->native);
}
