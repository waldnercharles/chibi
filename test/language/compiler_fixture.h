#ifndef CHIBI_TEST_COMPILER_FIXTURE_H
#define CHIBI_TEST_COMPILER_FIXTURE_H

#include "authoring/native_bindings.h"
#include "authoring/source_package_prepare.h"
#include "types/record.h"

/* Test-only mechanics shared by authored compiler differentials. Inputs use
 * /compiler as project root and /sdk/core as explicit core collection root.
 * Start zero-initialized; free also releases partial failed preparation. */
typedef struct
{
    native_capabilities *native;
    authoring_packages *owners[2];
} compiler_fixture;
typedef struct
{
    TSNode node;
    const char *field; /* Public cursor field name, NULL when absent. */
    int parent, first_child, next_sibling;
} compiler_fixture_node;

bool compiler_fixture_prepare(compiler_fixture *, const source_package_input *, int count);
void compiler_fixture_free(compiler_fixture *);
const authoring_package *compiler_fixture_package(
    const compiler_fixture *,
    int backend,
    const char *identity
);
uint64_t compiler_fixture_export(
    const compiler_fixture *,
    int backend,
    const char *identity,
    const char *name
);
const pin_record *compiler_fixture_record(
    const compiler_fixture *,
    int backend,
    const char *identity,
    const char *name,
    arena *
);
/* Exactly one result; its type is seeded from exported signature metadata.
 * Snapshots into detached before destroying producer context/activation/storage.
 * The caller owns arguments and must retire their arena before comparison. */
bool compiler_fixture_call(
    const compiler_fixture *,
    int backend,
    const char *identity,
    const char *name,
    const pin_value *arguments,
    int count,
    int budget,
    arena *detached,
    pin_value *out
);
bool compiler_fixture_nodes(TSNode root, compiler_fixture_node *, int capacity, int depth_cap, int *count);
/* Node schema is always taken from project:syntax_diagnostics. Field names,
 * when requested, are an aligned string array, with empty text for no field. */
bool compiler_fixture_pack_nodes(
    const compiler_fixture *,
    int backend,
    const compiler_fixture_node *,
    int count,
    arena *input,
    pin_value *nodes,
    pin_value *fields
);

#endif
