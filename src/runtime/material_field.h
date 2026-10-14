#pragma once
#include "runtime/fields/program.h"

typedef struct material_fields material_fields;
typedef struct material_field_session material_field_session;
material_fields *material_fields_new();
void material_fields_free(material_fields *fields);
/* Takes ownership of an immutable validated program. Replacement advances
 * the revision and invalidates prior borrowed baking sessions. */
bool material_field_register(material_fields *, const char *name, field_program *);
bool material_field_register_budget(material_fields *, const char *, field_program *, size_t budget);
/* Borrowed immutable generation until replacement or catalog free. */
const field_program *material_field_program(const material_fields *, const char *name);
int material_field_count(const material_fields *);
/* Retained owner, array capacities, argument arenas, names and programs. */
size_t material_fields_storage_bytes(const material_fields *);
const char *material_field_name_at(const material_fields *, int);
unsigned material_field_revision(const material_fields *fields, const char *name);
typedef struct
{
    const char *field, *key;
    field_context context;
} material_field_arguments;
/* Named argument records are prepared by the authoring/package host. Both key
 * and typed values are copied; no text interpretation occurs in the runtime.
 * Borrowed records end at replacement or catalog free. Empty keys use defaults. */
bool material_field_register_arguments(
    material_fields *,
    const char *field,
    const char *key,
    field_context,
    char *error,
    size_t size
);
bool material_field_register_arguments_budget(
    material_fields *,
    const char *,
    const char *,
    field_context,
    size_t budget,
    char *error,
    size_t size
);
bool material_field_context(
    const material_fields *,
    const char *field,
    const char *key,
    unsigned seed,
    field_context *,
    char *error,
    size_t size
);
int material_field_argument_count(const material_fields *);
material_field_arguments material_field_argument_at(const material_fields *, int);
bool material_field_bake(
    const material_fields *fields,
    const char *name,
    field_context context,
    field_grid grid,
    float *out,
    const task_system *tasks,
    char *error,
    size_t size
);
/* Rows [first, first + count) only, as field_program_bake_rows. */
bool material_field_bake_rows(
    const material_fields *fields,
    const char *name,
    field_context context,
    field_grid grid,
    int first,
    int count,
    float *out,
    const task_system *tasks,
    char *error,
    size_t size
);
/* A session owns bound scalar parameters and prepared evaluator workspace while
 * borrowing one catalog revision. Calls are serialized; a revision change makes
 * baking fail so the stream can be recreated without mixing tile versions. */
material_field_session *material_field_session_new(
    const material_fields *fields,
    const char *name,
    field_context context,
    const task_system *tasks,
    char *error,
    size_t size
);
void material_field_session_free(material_field_session *session);
bool material_field_session_matches(const material_field_session *session, const material_fields *fields);
bool material_field_session_bake_rows(
    material_field_session *session,
    const material_fields *fields,
    field_grid grid,
    int first,
    int count,
    float *out,
    const task_system *tasks,
    char *error,
    size_t size
);
