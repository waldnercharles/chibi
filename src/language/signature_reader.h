#ifndef CHIBI_LANGUAGE_SIGNATURE_READER_H
#define CHIBI_LANGUAGE_SIGNATURE_READER_H

#include "language/named_binding.h"
#include "language/type_constructors.h"

#include <tree_sitter/api.h>

typedef struct
{
    script_named_facts named;
    const script_function *functions;
    int function_count;
    size_t pointer_size;
} script_signature_facts;
typedef struct
{
    script_type type;
    const char *unit; /* NULL differs from a present, empty unit. */
    const script_diagnostic *diagnostics;
    int diagnostic_count;
    const script_use *uses;
    int use_count;
    const script_constructor_plan *publications;
    int publication_count;
    const script_catalog_type *types; /* Newly published definitions, in order. */
    int type_count;
} script_signature_reading;

/* Source and TSNode belong to one document. Raw tables/counts and tree indices
 * retain analyzer invariants. Reading owns all new output in storage; input
 * tables remain borrowed during the call. Nested constructors immediately
 * extend the local catalog/own-type/use views. Publication indices refer to
 * the original catalog followed by preceding publications, in order. */
script_signature_reading script_signature_read(
    arena *storage,
    const script_signature_facts *,
    TSNode,
    const char *source,
    int scope,
    int document,
    bool signature
);

#endif
