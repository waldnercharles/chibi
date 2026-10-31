#ifndef CHIBI_AUTHORING_SOURCE_DECLARATION_H
#define CHIBI_AUTHORING_SOURCE_DECLARATION_H
#include "language/semantic.h"

typedef enum
{
    SOURCE_TEXT_KDL,
    SOURCE_TEXT_RML,
    SOURCE_TEXT_CHIBI,
} source_text_kind;

typedef struct
{
    uint64_t id;
    script_capability_kind kind;
    const char *name, *path, *text;
    script_range range;
    source_text_kind text_kind;
    /* A project field definition, rather than a callable or prototype. */
    bool field;
    bool relationship;
    uint64_t owner;
} source_catalog_definition;
typedef enum
{
    SOURCE_REFERENCE_EVENT,
    SOURCE_REFERENCE_PROTOTYPE,
    SOURCE_REFERENCE_PROGRAM,
    SOURCE_REFERENCE_SYSTEM,
    SOURCE_REFERENCE_RELATIONSHIP,
} source_catalog_reference_kind;
typedef struct
{
    uint64_t id, owner;
    source_catalog_reference_kind kind;
    const char *name, *path, *text;
    script_range range;
    source_text_kind text_kind;
} source_catalog_reference;

#endif
