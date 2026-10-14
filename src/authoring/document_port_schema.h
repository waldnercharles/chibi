#ifndef CHIBI_DOCUMENT_PORT_SCHEMA_H
#define CHIBI_DOCUMENT_PORT_SCHEMA_H
#include "authoring/document_semantics.h"

typedef struct
{
    const char *path, *label, *type, *unit, *value;
    int storage, container;
    double min, max;
    bool range, range_contract, color, editable;
} document_port_field;
typedef struct
{
    const char *path, *label, *name, *value;
} document_port_choice;
/* Exact pin identity and metadata from real descriptors only. Scalar paths
 * are dot-separated reflected field names; empty path denotes the whole pin.
 * Source expressions remain source text, never evaluated to obtain defaults.
 * Rows and strings are owned by storage. Count is limited to256. */
bool document_port_schema(
    const document_semantics *,
    uint64_t graph,
    uint64_t node,
    uint64_t pin,
    bool output,
    int first,
    int count,
    arena *,
    document_port_field **,
    int *fields,
    document_port_choice **,
    int *choices,
    int *total,
    char *,
    size_t
);
/* Pure declarative aggregate edit. Value is a canonical typed literal (quote
 * strings); empty Path replaces a complete aggregate atomically. Source
 * expression programs use prepare_document_graph instead.
 * The unchanged siblings come from the exact owned snapshot. */
bool document_port_prepare_field(
    workspace_documents *,
    int document,
    const document_semantics *,
    uint64_t graph,
    uint64_t node,
    uint64_t pin,
    bool output,
    const char *path,
    const char *value,
    char **text,
    char *,
    size_t
);
#endif
