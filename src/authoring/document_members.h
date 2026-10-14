#ifndef CHIBI_DOCUMENT_MEMBERS_H
#define CHIBI_DOCUMENT_MEMBERS_H
#include "authoring/document_semantics.h"

typedef struct
{
    const char *kind, *id, *origin, *graph, *name, *type, *value;
    int storage, container, index;
    bool result, pure, editable, has_default;
} document_member;
typedef enum
{
    DOCUMENT_MEMBER_ADD_PIN,
    DOCUMENT_MEMBER_REMOVE_PIN,
    DOCUMENT_MEMBER_RENAME_PIN,
    DOCUMENT_MEMBER_MOVE_PIN,
    DOCUMENT_MEMBER_RETYPE_PIN,
    DOCUMENT_MEMBER_DEFAULT_PIN,
    DOCUMENT_MEMBER_PURE,
    DOCUMENT_MEMBER_ADD_VARIABLE,
    DOCUMENT_MEMBER_REMOVE_VARIABLE,
    DOCUMENT_MEMBER_DUPLICATE_VARIABLE,
    DOCUMENT_MEMBER_RENAME_VARIABLE,
    DOCUMENT_MEMBER_RETYPE_VARIABLE,
    DOCUMENT_MEMBER_DEFAULT_VARIABLE,
    DOCUMENT_MEMBER_GET,
    DOCUMENT_MEMBER_SET,
    DOCUMENT_MEMBER_CALL
} document_member_operation;
typedef struct
{
    int operation, storage, container, index, target;
    const char *graph, *id, *name, *type, *value;
    bool result, flag;
} document_member_edit;
/* One revision-bound snapshot, no execution or document mutation. Arena-owned
 * rows and strings. Type expressions are exact source spellings for source
 * declarations, native type names for declarative graph pins. Inventories are
 * bounded to4096 rows and256 pins per callable. */
bool document_members_inspect(
    const document_semantics *,
    uint64_t graph,
    arena *,
    document_member **members,
    int *,
    document_member **pins,
    int *,
    document_member **types,
    int *,
    char *,
    size_t
);
/* Pure validated full-text and optional source-layout candidate. Caller owns
 * both outputs and adopts them through its canonical document transactions. */
bool document_member_prepare(
    const document_semantics *,
    const document_member_edit *,
    char **text,
    char **metadata,
    char *,
    size_t
);
#endif
