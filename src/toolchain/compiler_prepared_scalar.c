#include "toolchain/compiler_prepared_scalar.h"

#include "execution/definition.h"
#include "execution/module_builder.h"
#include "execution/prepared.h"
#include "execution/scalar_lowering_plan.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool fail(char *error, size_t size, const char *message)
{
    snprintf(error, size, "%s", message);
    return false;
}
static bool entry_valid(const char *entry)
{
    if (!entry || !entry[0]) {
        return false;
    }
    for (int i = 1; i <= COMPILER_PREPARED_SCALAR_MAX_ENTRY; i++) {
        if (!entry[i]) {
            return true;
        }
    }
    return false;
}
bool compiler_prepared_scalar_write(
    const pin_value *value,
    uint64_t ready_tag,
    const compiler_prepared_scalar_declaration *declaration,
    unsigned char **bytes,
    size_t *length,
    char *error,
    size_t size
)
{
    assert(value && declaration && bytes && length && error && size);
    *bytes = NULL;
    *length = 0;
    error[0] = 0;
    if (!ready_tag || !declaration->module || !declaration->function ||
        !entry_valid(declaration->entry) ||
        (declaration->result != PIN_BOOL && declaration->result != PIN_I32)) {
        return fail(error, size, "invalid scalar callable declaration");
    }
    scalar_lowering_builder holder = { 0 };
    if (!scalar_lowering_decode(value, ready_tag, &holder, error, size)) {
        return false;
    }
    const behavior_function *definition = behavior_function_builder_definition(
        holder.builder
    );
    if (holder.variable_count || definition->argument_count ||
        definition->variable_count || definition->result_count != 1 ||
        definition->result_defaults[0].type != declaration->result ||
        definition->result_defaults[0].container != PIN_CONTAINER_SINGLE ||
        definition->result_kinds[0] != BEHAVIOR_VALUE) {
        scalar_lowering_builder_free(&holder);
        return fail(error, size, "scalar callable requires no arguments/state and one declared result");
    }
    behavior_function *body = scalar_lowering_finish(&holder, error, size);
    scalar_lowering_builder_free(&holder);
    if (!body) {
        return false;
    }
    /* The authored evaluator already writes context.results and reaches
     * BF_RETURN. Only its module role changes; no instruction is rewritten. */
    execution_module_plan *plan = execution_module_plan_new();
    plan->id = declaration->module;
    plan->valid = true;
    execution_module_function function = { .id = declaration->function,
                                           .kind = EXECUTION_FUNCTION_AUTHORED,
                                           .entry = xstrdup(declaration->entry),
                                           .body = body };
    apush(plan->functions, function);
    plan->function_count = 1;
    execution_module *module = execution_module_finish(&plan, BEHAVIOR_BYTECODE, error, size);
    if (!module) {
        execution_module_plan_free(plan);
        return false;
    }
    bool written = execution_prepared_write(module, bytes, length, error, size);
    execution_module_free(module);
    if (!written) {
        return false;
    }
    if (!*length || *length > COMPILER_PREPARED_SCALAR_MAX_BYTES) {
        free(*bytes);
        *bytes = NULL;
        *length = 0;
        return fail(error, size, "scalar prepared module exceeds bounded output storage");
    }
    return true;
}
