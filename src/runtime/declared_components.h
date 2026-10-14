#pragma once
#include "capabilities/catalog.h"
#include "ecs/component_catalog.h"
#include "execution/prepared.h"
#include "types/value_wire.h"

/* A component type a project declares in content/components instead of C. */
typedef struct
{
    /* A scalar script type (bool, int, float, double, number, string, entity,
     * ...) or a struct in the final target's type catalog, such as CF_V2. */
    const char *name, *type, *description;
    /* Numeric unit and inclusive stored-value range; equal means unbounded. */
    const char *unit;
    double min, max;
} declared_field;
typedef struct
{
    const char *name, *description;
    const declared_field *fields;
    int field_count;
} declared_component;
typedef struct declared_components declared_components;

/* Lays out each component's fields in order with natural alignment and builds
 * its type descriptor, ECS descriptor and script type. Inputs are copied.
 * The newest set alive is the loaded project's: its types are the process
 * type and script catalogs' project slots, and freeing it restores the next
 * newest. Rows start zeroed, then owning publication clamps range contracts. */
declared_components *declared_components_new(const declared_component *, int count, char *error, size_t size);
/* Whether a field may have the type `spelling`. */
bool declared_components_field_type(const char *spelling);
declared_components *declared_components_retain(declared_components *);
void declared_components_free(declared_components *);
ecs_component_catalog declared_components_ecs(const declared_components *);
const script_catalog *declared_components_catalog(const declared_components *);
/* The components a project's assets can name: the native registry, then the
 * live set's. */
ecs_component_catalog project_components();
/* Writes names, field types and range contracts, or rebuilds a set into *out.
 * Descriptions and units stay with the source. An empty set reads as NULL. */
void declared_components_wire(execution_wire *, declared_components **out);

/* Resolves declared types, then defers every request to base. */
typedef struct
{
    const declared_components *components;
    const execution_prepared_linker *base;
} declared_components_linking;
execution_prepared_linker declared_components_linker(const declared_components_linking *);
