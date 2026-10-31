#ifndef CHIBI_AUTHORING_SOURCE_NAVIGATION_H
#define CHIBI_AUTHORING_SOURCE_NAVIGATION_H

#include "authoring/source_declaration.h"
#include "language/semantic.h"

typedef struct authoring_navigation authoring_navigation;
typedef struct
{
    const char *path;
    const script_syntax *syntax;
    const script_analysis *analysis;
} authoring_document;
typedef enum
{
    AUTHORING_SYMBOL,
    AUTHORING_CALLABLE,
    AUTHORING_TYPE,
    AUTHORING_FIELD,
    AUTHORING_PROGRAM,
    AUTHORING_PROTOTYPE,
} authoring_target_kind;
typedef struct
{
    authoring_target_kind kind;
    int document; /* Local symbols use their document; catalog identities use -1. */
    uint64_t id, owner;
} authoring_target;
typedef struct
{
    int document;
    script_range range;
} authoring_location;
typedef enum
{
    AUTHORING_DEFINE,
    AUTHORING_READ,
    AUTHORING_WRITE,
    AUTHORING_CALL,
    AUTHORING_HANDLE,
} authoring_reference_kind;
typedef struct
{
    authoring_target target;
    authoring_location location;
    authoring_reference_kind kind;
    script_type type;
} authoring_reference;
typedef struct
{
    authoring_location location;
    int function; /* Owning resolved function, or -1 for declaration initializers. */
    authoring_target callee;
} authoring_call;

/* The index borrows immutable documents and catalog storage until free. It uses
 * resolved identities for meaning and CST fields only for precise name ranges.
 * Invalid drafts retain the facts that analysis could resolve. */
authoring_navigation *authoring_navigation_new(const authoring_document *, int count, const script_catalog *);
/* Appends exact declarative definition documents, borrowing their text/ranges.
 * These documents have NULL syntax and analysis. */
authoring_navigation *authoring_navigation_new_declarations(
    const authoring_document *,
    int count,
    const script_catalog *,
    const source_catalog_definition *,
    int definition_count
);
authoring_navigation *authoring_navigation_new_project(
    const authoring_document *,
    int count,
    const script_catalog *,
    const source_catalog_definition *,
    int definition_count,
    const source_catalog_reference *,
    int reference_count
);
void authoring_navigation_free(authoring_navigation *);
int authoring_navigation_document_count(const authoring_navigation *);
const authoring_document *authoring_navigation_document_at(const authoring_navigation *, int index);
const char *authoring_navigation_document_text(const authoring_navigation *, int document);
source_text_kind authoring_navigation_document_kind(const authoring_navigation *, int document);
const script_catalog *authoring_navigation_catalog(const authoring_navigation *);
/* Resolves either an authored template ID or a concrete specialization ID. */
const script_catalog_template *authoring_navigation_template(const authoring_navigation *, uint64_t);
bool authoring_target_equal(authoring_target, authoring_target);
const authoring_reference *authoring_navigation_at(const authoring_navigation *, int document, int offset);
const authoring_reference *authoring_navigation_definition(const authoring_navigation *, authoring_target);
int authoring_navigation_reference_count(const authoring_navigation *);
const authoring_reference *authoring_navigation_reference_at(const authoring_navigation *, int index);
int authoring_navigation_call_count(const authoring_navigation *);
const authoring_call *authoring_navigation_call_at(const authoring_navigation *, int index);
/* Normalize authored aliases to the callable they invoke or handle. Zero ID
 * means the target does not name a callable. Caller includes event handlers. */
authoring_target authoring_navigation_callable(const authoring_navigation *, authoring_target);
authoring_target authoring_navigation_caller(const authoring_navigation *, const authoring_call *);
/* Borrowed semantic/catalog names. NULL means the draft has no resolved name. */
const char *authoring_navigation_name(const authoring_navigation *, authoring_target);

#endif
