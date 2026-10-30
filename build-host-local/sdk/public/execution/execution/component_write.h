#ifndef CHIBI_EXECUTION_COMPONENT_WRITE_H
#define CHIBI_EXECUTION_COMPONENT_WRITE_H

#include "ecs/execution.h"
#include "execution/function.h"
#include "execution/instruction.h"
#include "execution/scalar.h"

#include <string.h>

/* The verifier proves the scalar operands and byte range. Pointer arguments
 * preserve full-width handles and scalar bits on 32-bit backends. Reacquire the
 * current world only after both operands have been evaluated; never keep row
 * storage across publication. A failing store may already have side effects. */
static inline const char *behavior_component_write(
    behavior_function_context *context,
    const behavior_function_instruction *instruction,
    const void *owner,
    const void *scalar
)
{
    uint64_t entity;
    memcpy(&entity, owner, sizeof entity);
    const ecs_execution_view *world = context->world;
    uint32_t row = world ? ecs_execution_entity(world, entity) : 0;
    const ecs_execution_column
        *column = ecs_execution_component(world, row, instruction->value.u64);
    if (!column || column->layout != instruction->layout ||
        column->stride != instruction->offset) {
        return "Referenced storage is unavailable";
    }
    pin_value value = { .type = instruction->type };
    memcpy(&value.d, scalar, behavior_scalar_size(instruction->type));
    return !context->store_component || context->store_component(
                                            context,
                                            entity,
                                            instruction->value.u64,
                                            instruction->layout,
                                            (size_t)instruction->target,
                                            (pin_value){ .type = instruction->type },
                                            value
                                        )
             ? "Cannot retain referenced component write"
             : NULL;
}
#endif
