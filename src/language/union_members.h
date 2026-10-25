#ifndef CHIBI_LANGUAGE_UNION_MEMBERS_H
#define CHIBI_LANGUAGE_UNION_MEMBERS_H

#include "language/signature_reader.h"

typedef struct
{
    bool append;
    script_catalog_type definition;
} script_union_member_update;
typedef struct
{
    const script_union_member_update *updates;
    int update_count;
    const script_diagnostic *diagnostics;
    int diagnostic_count;
    const script_use *uses;
    int use_count;
} script_union_member_result;

typedef struct
{
    int root;  /* -1 on success; otherwise the first failed root index. */
    int error; /* 0 success, 1 owned cycle, 2 nesting depth. */
} script_union_member_validation;

/* Raw catalog metadata discovers edges here. States has catalog->type_count
 * entries (0 unseen, 1 visiting, 2 done), initially zero or a previous returned
 * state. Roots retain valid scalar type metadata; containers remain relevant
 * facts, not a caller-supplied edge filter. Stops at the first failed root. */
script_union_member_validation script_union_members_validate(
    const script_catalog *,
    const script_type *roots,
    int count,
    unsigned char *states
);

/* Populates all unions directly in body, then validates their owned payload
 * graph. Nominal headers already exist. Catalog/symbol counts and tree facts
 * retain analyzer invariants. Constructor append and union replacement updates
 * occur in source order and affect later reads immediately. Replacements apply
 * to every matching catalog/own-type ID. Output metadata is deeply arena-owned;
 * input facts/tree/source need only survive the call. No initializer/body
 * expression is analyzed, including syntactic payload defaults. */
script_union_member_result script_union_members(
    arena *,
    const script_signature_facts *,
    TSNode body,
    const char *source,
    int scope,
    int document
);

#endif
