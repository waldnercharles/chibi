#ifndef CHIBI_LANGUAGE_RECORD_MEMBERS_H
#define CHIBI_LANGUAGE_RECORD_MEMBERS_H

#include "language/signature_reader.h"

typedef struct
{
    int symbol, definition; /* Both -1 when this declaration is not eligible. */
    const script_diagnostic *diagnostics;
    int diagnostic_count;
} script_record_selection;
typedef struct
{
    script_catalog_field field; /* Default value and physical offset are unset. */
    script_signature_reading annotation;
    TSNode default_node; /* Borrowed original syntax; null when absent. */
    bool admissible; /* Field shape/duplicate policy only, not default validity. */
    const script_diagnostic *diagnostics; /* Name, annotation, field order. */
    int diagnostic_count;
} script_record_member;

/* Raw lexical/catalog facts; catalog row order is significant. Selection uses
 * the original authored-record and document/start guards, not extra symbol-kind
 * or storage checks. The returned indices refer to the supplied tables. */
script_record_selection script_record_members_select(
    arena *,
    const script_signature_facts *,
    TSNode declaration,
    const char *source,
    int scope,
    int document
);

/* Discovers exactly one non-extra record member. Caller supplies every earlier
 * field name in source order, including invalid/duplicate fields. This call
 * stops BEFORE analyzing its default. Consume the combined diagnostics once,
 * apply annotation publications/uses immediately, then process the original
 * default separately. Only then call again with REFRESHED live facts and the
 * extended prior-name list. Do not batch/hoist reads across default boundaries.
 * No callback evaluates a default, decides eligibility or supplies validity.
 * All output text/metadata is arena-owned except the explicitly borrowed
 * default TSNode; keep its syntax owner alive until that node is consumed.
 * No layout, final record publication, scalar proof or folding occurs here. */
script_record_member script_record_members_read(
    arena *,
    const script_signature_facts *,
    TSNode member,
    const char *source,
    int scope,
    int document,
    const char *const *previous_names,
    int previous_count
);

#endif
