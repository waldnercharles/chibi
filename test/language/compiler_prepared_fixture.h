#ifndef CHIBI_TEST_COMPILER_PREPARED_FIXTURE_H
#define CHIBI_TEST_COMPILER_PREPARED_FIXTURE_H

#include "runtime/native_definitions.h"

#define COMPILER_PREPARED_FIXTURE_SERVICE "compiler_prepared_fixture"
typedef struct
{
    /* Resolve Plan.Ready from project:scalar_lowering export metadata after
     * bootstrap preparation. Keep immutable while a driver invocation runs. */
    uint64_t ready_tag;
} compiler_prepared_fixture_service;

/* Test-only opt-in native catalog. Authored signature:
 * compiler_prepared_scalar(plan: T, module: uint64, function: uint64,
 *                          entry: string, result_storage: int)
 *     -> (bytes: byte[], diagnostic: string)
 * T is native generic transport, validated by scalar_lowering_decode.
 * Required service: COMPILER_PREPARED_FIXTURE_SERVICE, pointing to the above.
 * A bridge rejection returns empty bytes and a nonempty diagnostic; success
 * returns nonempty inner-module bytes and an empty diagnostic. Both results
 * belong to invocation value_storage. The callback retains no input/service.
 * This pack must never be installed in the runtime-only compiler_host.
 * Tests may copy this single definition/binding to interpose call counting. */
extern const native_definition_pack COMPILER_PREPARED_FIXTURE_DEFINITION_PACK;

#endif
