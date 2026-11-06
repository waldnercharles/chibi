#ifndef CHIBI_LANGUAGE_CALLABLE_HEADERS_H
#define CHIBI_LANGUAGE_CALLABLE_HEADERS_H

#include "language/signature_reader.h"

/* A callable parameter has an unmodified function signature. Parentheses do
 * not change it; arrays, optional values and pointers require stored values. */
TSNode script_callable_type_node(TSNode);
bool script_callable_type_annotation(TSNode);

typedef struct
{
    script_signature_facts signature;
    int scope_count;
} script_callable_facts;
typedef struct
{
    int scope, function, document;
    uint64_t specialization; /* Nonzero while analyzing one generic specialization. */
    bool instantiating; /* Compile-time parameters are already bound. */
} script_callable_context;
enum
{
    SCRIPT_CALLABLE_TYPE = 1u << 0,
    SCRIPT_CALLABLE_UNIT = 1u << 1,
    SCRIPT_CALLABLE_RANGE = 1u << 2,
    SCRIPT_CALLABLE_FUNCTION = 1u << 3,
    SCRIPT_CALLABLE_CAPABILITY = 1u << 4,
    SCRIPT_CALLABLE_OUTCOME = 1u << 5,
    SCRIPT_CALLABLE_APPEND = 1u << 6
};
typedef struct
{
    int index;
    unsigned fields;
    script_symbol symbol;
} script_callable_symbol_change;
typedef struct
{
    int index;
    bool append;
    script_function function;
} script_callable_function_change;
typedef struct
{
    int symbol, document;
    TSNode node;
} script_callable_initializer;
typedef struct
{
    int function, document;
    TSNode node;
} script_callable_body;
typedef struct
{
    int symbol, function; /* Function is -1 when the package-origin guard rejects. */
    const script_callable_symbol_change *symbols;
    int symbol_count;
    const script_scope *scopes; /* Appended at the input scope_count. */
    int scope_count;
    const script_callable_function_change *functions;
    int function_count;
    const script_callable_initializer *initializers;
    int initializer_count;
    const script_callable_body *bodies;
    int body_count;
    const script_diagnostic *diagnostics;
    int diagnostic_count;
    const script_use *uses;
    int use_count;
    const script_catalog_type *types;
    int type_count;
} script_callable_header;

/* Sequential header registration only. Raw tables/tree retain analyzer
 * invariants; input function memberships are support/array vectors. No
 * body/default/effect evaluation occurs. Changed row names/units,
 * memberships and metadata are arena-owned; pending TSNodes borrow the source
 * syntax. Symbol changes are in index order, with appended rows marked APPEND.
 * Only masked fields of existing rows change. Native application duplicates
 * names/units into the analyzer's heap ownership; function membership arrays
 * are flat counted spans, NOT support/array vectors, and must be transferred. */
script_callable_header script_callable_register(
    arena *,
    const script_callable_facts *,
    TSNode declaration,
    const char *source,
    script_callable_context
);
/* Shared initialization/type policy for the analyzer's non-header callers. */
script_function script_callable_function(int parent, TSNode, int document, script_function_kind, int symbol);
/* The `T` of `declaration`'s parameter `index` when it is a scoped pointer,
 * a `^T` whose address never outlives the call so it aliases its argument;
 * otherwise a null node. */
TSNode script_scoped_pointer_target(const char *source, TSNode declaration, uint32_t index);
script_type script_callable_result_type(uint64_t function, const script_type *results, int count);

#endif
