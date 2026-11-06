#ifndef CHIBI_EXECUTION_SCALAR_LOWERING_PLAN_H
#define CHIBI_EXECUTION_SCALAR_LOWERING_PLAN_H
#include "execution/function_builder.h"
#include "support/arena.h"

/* Private schema 1. This adapter receives an authored plan, never typed IR or
 * source text. Its only opcode selection is a one-to-one wire-name translation.
 * The holder owns origin strings until finish snapshots the definition. */
typedef struct
{
    arena storage;
    behavior_function_builder *builder;
    uint64_t variable_ids[2];
    int variable_count;
} scalar_lowering_builder;
enum
{
    SCALAR_LOWERING_OPCODES = 29
};
/* The instruction a wire opcode names, for producers that write prepared
 * modules from the plan themselves. */
behavior_function_op scalar_lowering_opcode(int wire);
/* Start with a zeroed holder. On rejection, holder remains empty. The caller
 * obtains ready_tag from the trusted scalar_lowering.Plan export metadata. */
bool scalar_lowering_decode(const pin_value *, uint64_t ready_tag, scalar_lowering_builder *, char *, size_t);
void scalar_lowering_builder_free(scalar_lowering_builder *);
/* Calls the existing builder verifier, optimizer, finalizer/liveness/rewriter
 * and parallel proof. These are declared native compiler dependencies. */
behavior_function *scalar_lowering_finish(scalar_lowering_builder *, char *, size_t);
#endif
