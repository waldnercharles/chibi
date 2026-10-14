#ifndef CHIBI_AUTHORING_SOURCE_EDIT_H
#define CHIBI_AUTHORING_SOURCE_EDIT_H

#include "authoring/source_presentation.h"

typedef enum
{
    AUTHORING_EDIT_EXPRESSION,
    AUTHORING_EDIT_CONNECT,
    AUTHORING_EDIT_DELETE_STATEMENT,
    AUTHORING_EDIT_MOVE_BEFORE,
    AUTHORING_EDIT_MOVE_AFTER,
    AUTHORING_EDIT_VALUE,
    AUTHORING_EDIT_DELETE_SELECTION,
    AUTHORING_EDIT_INSERT_BEFORE,
    AUTHORING_EDIT_INSERT_AFTER,
    AUTHORING_EDIT_APPEND_BODY,
    AUTHORING_EDIT_APPEND_DECLARATIONS,
    AUTHORING_EDIT_DUPLICATE_SELECTION,
    AUTHORING_EDIT_REPLACE_BODY,
    AUTHORING_EDIT_REPLACE_DECLARATION,
    AUTHORING_EDIT_MOVE_INTO_BODY,
    AUTHORING_EDIT_WRAP_IF,
    AUTHORING_EDIT_WRAP_WHILE,
    AUTHORING_EDIT_WRAP_SEQUENCE,
    AUTHORING_EDIT_DISCONNECT,
    AUTHORING_EDIT_RECONNECT,
    AUTHORING_EDIT_CREATE_CAPABILITY,
    AUTHORING_EDIT_CREATE_FUNCTION,
    AUTHORING_EDIT_CREATE_MACRO,
    AUTHORING_EDIT_RENAME_DECLARATION,
    AUTHORING_EDIT_CREATE_EVENT,
    AUTHORING_EDIT_CREATE_EXPRESSION,
    AUTHORING_EDIT_SET_DISABLED,
    AUTHORING_EDIT_NAME_VALUE,
    AUTHORING_EDIT_SIGNATURE,
    AUTHORING_EDIT_BIND_INPUT,
    AUTHORING_EDIT_EXTRACT_FUNCTION,
    AUTHORING_EDIT_EXTRACT_MACRO,
    AUTHORING_EDIT_INLINE_CALL,
    /* symbol is a declared state ID. origin selects its executable body;
     * Get previous binds an input, and Set accepts producer or a typed value. */
    AUTHORING_EDIT_CREATE_VARIABLE_GET,
    AUTHORING_EDIT_CREATE_VARIABLE_SET,
    AUTHORING_EDIT_RETYPE_VARIABLE,
} authoring_edit_kind;
typedef enum
{
    AUTHORING_BIND_SYMBOL,
    AUTHORING_BIND_COMPONENT,
    AUTHORING_BIND_GLOBAL,
} authoring_binding_kind;
typedef struct
{
    authoring_edit_kind kind;
    uint64_t generation, origin, producer;
    int producer_result;
    const char *expression;
    const pin_value *value;
    const uint64_t *origins;
    int origin_count;
    const char *fragment;
    uint64_t previous, capability;
    const char *capability_name;
    const char *name, *reference, *field;
    bool disabled;
    const char *signature;
    authoring_binding_kind binding;
    uint64_t symbol, component, member;
} authoring_edit_request;
typedef struct
{
    char *text, *metadata;
} authoring_edit_result;

/* Exact source generation guard; presentation origins also identify the
 * semantic document. Requests and immutable inputs are borrowed for the call. */
uint64_t authoring_source_edit_generation(const authoring_source_presentation *);
/* Returns owned candidate source and optional layout after complete syntax and semantic validation.
 * Expression edits replace one resolved expression. Connections reference an
 * existing named result whenever evaluation has effects; unnamed effectful
 * producers are rejected rather than evaluated twice. Pure expressions may be
 * inlined only after lexical/type checking in the destination context.
 * Statement moves require siblings in one block/sequence, move attached comments,
 * and validate declaration order. No graph topology becomes executable source.
 * Failure clears result with a message and leaves all inputs untouched.
 * A caller still validates the complete project before recording one source
 * history entry; editor projection adoption is deferred to reconciliation. */
bool authoring_source_edit(
    const script_analysis *,
    const script_catalog *,
    const authoring_source_presentation *,
    const authoring_edit_request *,
    authoring_edit_result *,
    char *error,
    size_t error_size
);
void authoring_edit_result_free(authoring_edit_result *);
/* Owned complete lexical selection for source clipboard/paste. The view is
 * immutable; nested selected records are deduplicated, with attached comments.
 * Copying does not execute or create native graph nodes. */
char *authoring_source_clipboard(
    const authoring_source_presentation *,
    const uint64_t *origins,
    int count,
    char *error,
    size_t error_size
);

#endif
