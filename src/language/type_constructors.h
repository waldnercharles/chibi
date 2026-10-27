#ifndef CHIBI_LANGUAGE_TYPE_CONSTRUCTORS_H
#define CHIBI_LANGUAGE_TYPE_CONSTRUCTORS_H

#include "language/semantic.h"

typedef struct
{
    const script_catalog *catalog;
    const script_symbol *symbols;
    const script_function *functions;
    int function_count;
} script_constructor_facts;
typedef struct
{
    int catalog_type, variant; /* Original catalog row; both -1 for synthesized. */
    int member; /* Original member index, or -1 for a catalog variant. */
    uint64_t tag;
    script_type type;
} script_constructor_variant;
typedef struct
{
    int capability, parameter; /* Original capability result, or both -1. */
    int symbol;                /* Original authored result symbol, or -1. */
} script_constructor_field;
typedef struct
{
    script_type type; /* Returned type, distinct from a task's descriptor type. */
    bool publish;
    uint64_t id;
    const char *name;
    script_type definition_type;
    script_type element_type; /* Complete pointee metadata, including collection identity. */
    size_t size;
    bool internal;
    const script_constructor_variant *variants;
    int variant_count;
    const script_constructor_field *fields;
    int field_count;
    script_diagnostic diagnostics[1];
    int diagnostic_count;
} script_constructor_plan;

/* Raw borrowed catalogs/functions have valid counts, indices and type metadata.
 * Plans own names/selection arrays in storage, and select original metadata by
 * index. Apply materialization before retiring facts; no source preparation or
 * expression evaluation occurs. Cache hits return only type/diagnostics. */
script_constructor_plan script_constructor_union(
    arena *,
    const script_constructor_facts *,
    const script_type *members,
    int count,
    size_t pointer_size
);
script_constructor_plan script_constructor_task(
    arena *,
    const script_constructor_facts *,
    script_type result,
    uint64_t function
);
script_constructor_plan script_constructor_await(
    arena *,
    const script_constructor_facts *,
    script_type success,
    script_range,
    size_t pointer_size
);
/* The internal catalog row naming one collection's key and element types. Its
 * identity depends only on the container and both type tags; storage owns the
 * name. Callers publish it when the catalog lacks that identity. */
script_catalog_type script_constructor_collection(
    arena *,
    pin_container,
    script_type key,
    script_type value,
    size_t pointer_size
);
/* A pointer's nominal identity includes the complete pointee type tag. */
script_catalog_type script_constructor_pointer(arena *, script_type pointee, size_t pointer_size);
/* Pure ownership adapter: copies exactly the selected variants/fields. No
 * lookup, deduplication, type selection or publication policy is performed.
 * False means no publication or a failed metadata snapshot. Accepted output
 * owns names/default payloads in storage and may outlive all input facts. */
bool script_constructor_materialize(
    arena *,
    const script_constructor_facts *,
    const script_constructor_plan *,
    script_catalog_type *out
);

#endif
