#ifndef CHIBI_TEST_COMPILER_FIXTURE_H
#define CHIBI_TEST_COMPILER_FIXTURE_H

#include "authoring/native_bindings.h"
#include "authoring/source_package_prepare.h"
#include "execution/managed.h"
#include "runtime/native_definitions.h"
#include "types/array.h"
#include "types/record.h"

/* Test-only mechanics shared by authored compiler differentials. Inputs use
 * /compiler as project root and /sdk/core as explicit core collection root.
 * Start zero-initialized; free also releases partial failed preparation. */
typedef struct
{
    /* Diagnostic opt-in: disables readonly argument borrowing for this fixture. */
    bool observe_failures;
    /* Calls run in collectable storage, as program activations do. */
    bool collectable;
    native_capabilities *native;
    authoring_packages *owners[2];
    behavior_managed_heap *managed;
} compiler_fixture;
typedef struct
{
    TSNode node;
    const char *field; /* Public cursor field name, NULL when absent. */
    int parent, first_child, next_sibling;
} compiler_fixture_node;

/* pin_value_snapshot_to_arena that boxes every packed array, as call results
 * are, so differentials read items whatever the runtime packed. */
int compiler_fixture_snapshot(arena *, pin_value *out, const pin_value *value);
/* Boxes every packed array reachable from value in place, allocating in storage. */
void compiler_fixture_box(arena *, const pin_value *);
/* pin_union_value with boxed payload arrays, for inputs the tests read back. */
int compiler_fixture_union(arena *, uint64_t tag, const pin_value *, int count, pin_value *out);
bool compiler_fixture_prepare(compiler_fixture *, const source_package_input *, int count);
/* Additional private primitives are opt-in; ordinary fixtures keep their catalog. */
bool compiler_fixture_prepare_packs(
    compiler_fixture *,
    const source_package_input *,
    int count,
    const native_definition_pack *additional,
    int additional_count
);
enum
{
    COMPILER_FIXTURE_BYTECODE = 1u << 0,
    COMPILER_FIXTURE_COMPILED = 1u << 1,
    COMPILER_FIXTURE_BOTH = COMPILER_FIXTURE_BYTECODE | COMPILER_FIXTURE_COMPILED
};
/* Prepare only the requested owners. Unselected owners stay NULL; selected
 * owners still take independent snapshots and live only until fixture_free.
 * The ordinary prepare helpers always prepare both backends. */
bool compiler_fixture_prepare_backends(
    compiler_fixture *,
    unsigned backends,
    const source_package_input *,
    int count,
    const native_definition_pack *additional,
    int additional_count
);
void test_language_compiler_fixture();
void compiler_fixture_free(compiler_fixture *);
/* Discover and run every authored @test in each prepared owner on that owner's
 * backend, with private invocation storage, automatic pure-test scheduling and
 * ordered diagnostics. CHIBI_TEST_WORKERS bounds its pool independently of C
 * case workers. Without a pool capability it runs serially. Returns false for
 * no tests or any failure; call only outside a parallel C case scope. */
bool compiler_fixture_tests(const compiler_fixture *);
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
 * The caller owns arguments and must retire their arena before comparison.
 * Managed pointer results retain identity in the fixture-owned heap, which
 * lives until compiler_fixture_free; detached storage does not own pointees. */
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
/* The data functions `catalog` links, which take no flow, outcomes or
 * suspension, as Compile and CollectScalar take them: out[0] holds a Native
 * row of `identity` for each, out[1] its Port rows and out[2] its ID and ABI.
 * `effects` receives each one's effect mask. Allocates in `input`. */
bool compiler_fixture_natives(
    const compiler_fixture *,
    int backend,
    const char *identity,
    const script_catalog *catalog,
    arena *input,
    pin_value out[3],
    pin_value *effects
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
