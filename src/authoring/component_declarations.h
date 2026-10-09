#pragma once
#include "runtime/declared_components.h"

/* Reads one component per .kdl file in content/components, in name order:
 *
 *     name "Health"
 *     description "Hit points."
 *     (array)fields {
 *         - name="current" type="int"
 *         - name="maximum" type="int" description="Full health."
 *     }
 *
 * A project that declares none gets an empty set, which still replaces the
 * sets of other projects. A name may not shadow a type the native catalog
 * already offers. */
bool authoring_component_declarations_load(
    const char *content,
    const script_catalog *native,
    declared_components **out,
    char *error,
    size_t size
);
