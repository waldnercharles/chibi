#ifndef CHIBI_LANGUAGE_NOMINAL_BINDING_H
#define CHIBI_LANGUAGE_NOMINAL_BINDING_H

#include "language/lexical_binding.h"

typedef enum
{
    SCRIPT_NOMINAL_RECORD,
    SCRIPT_NOMINAL_UNION
} script_nominal_kind;
typedef struct
{
    script_lexical_context lexical;
    script_program_kind program_kind;
    const char *program_name;
    size_t pointer_size; /* Target ABI fact, currently sizeof(void *). */
} script_nominal_context;
typedef struct
{
    script_nominal_kind kind;
    const char *name;
    script_range range, name_range; /* Whole declaration, then name token. */
} script_nominal_request;
typedef struct
{
    bool declare, publish; /* Placement rejection declares nothing. */
    int existing; /* -1 appends; otherwise reuse without changing its type yet. */
    uint64_t id; /* Effective symbol ID, including an existing declaration. */
    script_type declaration_type; /* Applied only when appending a symbol. */
    script_type type; /* Published type, valid only when publish is true. */
    const char *name, *source_name; /* Owned by the supplied arena when published. */
    bool authored_record;
    size_t size;
    script_diagnostic diagnostics[3];
    int diagnostic_count;
} script_nominal_declaration;

/* Placement is checked before decoding a quoted declaration name. */
bool script_nominal_allowed(script_nominal_context, script_nominal_kind);

/* TS-free extraction of the existing record/union header passes. Facts/strings
 * are borrowed, with lexical indices and declaration kinds already valid.
 * A placement-rejected request may have a NULL name.
 * This neither appends symbols nor publishes a catalog entry. It preserves the
 * distinct legacy origin/storage guards and lexical diagnostic order. Output
 * names belong to storage; all other output is copied by value. */
script_nominal_declaration script_nominal_declare(
    arena *storage,
    const script_lexical_facts *,
    script_nominal_context,
    script_nominal_request
);

#endif
