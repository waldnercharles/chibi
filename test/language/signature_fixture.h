#ifndef CHIBI_TEST_SIGNATURE_FIXTURE_H
#define CHIBI_TEST_SIGNATURE_FIXTURE_H

#include "compiler_fixture.h"
#include "language/signature_reader.h"

/* Mechanical raw tables only. Shared bounds are 64 types, 128 IDs, 32 symbols,
 * eight scopes/functions/catalog entries/variants/imports/exports and sixteen
 * uses/result indices. Each consumer admits its own smaller input workload. */
bool signature_fixture_environment(
    const compiler_fixture *,
    int backend,
    const script_signature_facts *,
    int scope_count,
    arena *,
    pin_value *out
);
/* Nodes include anonymous children/extras and incoming public TS field names.
 * The caller owns collection bounds and selects its target node independently. */
bool signature_fixture_tree(
    const compiler_fixture *,
    int backend,
    const compiler_fixture_node *,
    int count,
    const char *source,
    arena *,
    pin_value *out
);

#endif
