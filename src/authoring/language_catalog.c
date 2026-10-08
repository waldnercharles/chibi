#include "authoring/language_catalog.h"

#include "authoring/native_bindings.h"
#include "authoring/source_catalog_snapshot.h"
#include "support/array.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

struct authoring_language_catalog
{
    int references;
    native_capabilities *native;
    arena storage;
    script_catalog view;
    script_catalog_type *types;
    script_catalog_capability *capabilities;
    script_catalog_global *globals;
};
static bool append(authoring_language_catalog *c, const script_catalog *source)
{
    script_catalog copy;
    if (!source_catalog_snapshot(&c->storage, source, &copy)) {
        return false;
    }
    for (int i = 0; i < copy.type_count; i++) {
        for (int j = 0; j < acount(c->types); j++) {
            if (c->types[j].id == copy.types[i].id) {
                return false;
            }
        }
        apush(c->types, copy.types[i]);
    }
    for (int i = 0; i < copy.capability_count; i++) {
        for (int j = 0; j < acount(c->capabilities); j++) {
            if (c->capabilities[j].id == copy.capabilities[i].id) {
                return false;
            }
        }
        apush(c->capabilities, copy.capabilities[i]);
    }
    for (int i = 0; i < copy.global_count; i++) {
        for (int j = 0; j < acount(c->globals); j++) {
            if (c->globals[j].id == copy.globals[i].id) {
                return false;
            }
        }
        apush(c->globals, copy.globals[i]);
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
    if (!append(c, native) || (input->project && !append(c, input->project))) {
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
    arena_free(&c->storage);
    native_capabilities_free(c->native);
    free(c);
}
script_lower_bindings authoring_language_bindings(const authoring_language_catalog *c)
{
    assert(c);
    return authoring_native_bindings(c->native);
}
