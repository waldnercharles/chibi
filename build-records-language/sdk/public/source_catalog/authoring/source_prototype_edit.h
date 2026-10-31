#ifndef CHIBI_AUTHORING_SOURCE_PROTOTYPE_EDIT_H
#define CHIBI_AUTHORING_SOURCE_PROTOTYPE_EDIT_H

#include "assets/prototype.h"
#include "capabilities/catalog.h"

/* A request is one authoring/history gesture; callers must not split a rejected
 * batch into partial edits. These limits bound planning and serialization. */
#define AUTHORING_PROTOTYPE_EDIT_MAX_OPERATIONS 1024
#define AUTHORING_PROTOTYPE_EDIT_MAX_BYTES (1024 * 1024)

typedef enum
{
    AUTHORING_PROTOTYPE_SET_FIELD,
    AUTHORING_PROTOTYPE_ADD_COMPONENT,
    AUTHORING_PROTOTYPE_REMOVE_COMPONENT,
    AUTHORING_PROTOTYPE_ATTACH_BEHAVIOR,
    /* Remove every occurrence of the requested behavior symbol. */
    AUTHORING_PROTOTYPE_DETACH_BEHAVIOR
} authoring_prototype_edit_kind;

typedef struct
{
    authoring_prototype_edit_kind kind;
    const char *component;
    /* Reflected field names, optionally separated by dots. */
    const char *field;
    /* SET_FIELD: leaf value. ADD_COMPONENT: complete PIN_STRUCT creation
     * payload, including the consumer's contextual native defaults. Borrowed. */
    pin_value value;
    const char *behavior;
} authoring_prototype_edit;

typedef struct
{
    char *source;
    prototype *value;
    bool changed;
    /* Removing an inherited member requires a standalone resolved prototype;
     * the language has no inherited-member suppression syntax. */
    bool materialized;
} authoring_prototype_edit_result;

/* Pure, bounded edit of one static prototype. Input strings, payloads,
 * registry and catalog are borrowed and never changed. Conflicting operations,
 * invalid input, unrepresentable values or invalid final composition fail with
 * an empty result. Successful source and model are owned by the result.
 *
 * With count zero, returns an owned validated source/model without editing;
 * catalog may be NULL for this prepare-only operation.
 *
 * Runtime handles are never emitted as symbols. Existing symbolic links
 * survive ordinary edits and structural materialization.
 *
 * Nested fields follow the existing language's whole-record override rule.
 * When an ancestor record is omitted, author its resolved siblings as well
 * to retain their current values; this does not introduce per-axis inheritance.
 *
 * The caller owns source/dependency revision checks and editor history.
 * Registry descriptors and include context must outlive the returned model. */
bool authoring_prototype_source_edit(
    const char *,
    const prototype_registry *,
    const script_catalog *,
    const authoring_prototype_edit *,
    int count,
    authoring_prototype_edit_result *,
    char *error,
    size_t
);
void authoring_prototype_edit_result_free(authoring_prototype_edit_result *);

#endif
