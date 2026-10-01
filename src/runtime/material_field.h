#pragma once
#include "graph/compiler/field_program.h"

typedef struct material_fields material_fields;
typedef struct material_field_session material_field_session;
material_fields *material_fields_new();
void material_fields_free(material_fields *fields);
/* Compiles atomically; a rejected edit retains the last valid program. */
bool material_field_compile(material_fields *fields, const behavior_asset *asset, char *error, size_t size);
unsigned material_field_revision(const material_fields *fields, const char *name);
bool material_field_bake(
    const material_fields *fields,
    const char *name,
    const char *parameters,
    unsigned seed,
    field_grid grid,
    float *out,
    const game_task_system *tasks,
    char *error,
    size_t size
);
/* Rows [first, first + count) only, as field_program_bake_rows. */
bool material_field_bake_rows(
    const material_fields *fields,
    const char *name,
    const char *parameters,
    unsigned seed,
    field_grid grid,
    int first,
    int count,
    float *out,
    const game_task_system *tasks,
    char *error,
    size_t size
);
/* A session owns parsed parameters and prepared evaluator workspace while
 * borrowing one catalog revision. Calls are serialized; a revision change makes
 * baking fail so the stream can be recreated without mixing tile versions. */
material_field_session *material_field_session_new(
    const material_fields *fields,
    const char *name,
    const char *parameters,
    unsigned seed,
    const game_task_system *tasks,
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
    const game_task_system *tasks,
    char *error,
    size_t size
);
