#ifndef CHIBI_BEHAVIOR_EXPRESSION_WASM_H
#define CHIBI_BEHAVIOR_EXPRESSION_WASM_H

#include "graph/compiler/graph_ir.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct behavior_asset behavior_asset;
typedef struct behavior_instance behavior_instance;
typedef struct behavior_expression_wasm behavior_expression_wasm;

typedef enum
{
    BEHAVIOR_WASM_NONE,
    BEHAVIOR_WASM_ADD,
    BEHAVIOR_WASM_SUB,
    BEHAVIOR_WASM_MUL,
    BEHAVIOR_WASM_DIV,
} behavior_expression_wasm_op;

typedef struct
{
    behavior_expression_wasm_op op;
    pin_type type;
    int output, left, right;
    int third;
    bool left_slot, right_slot, third_slot;
    int control_target;
    bool control_transfer;
} behavior_expression_wasm_instruction;

typedef struct
{
    int first, end, function, result_slot;
} behavior_expression_wasm_region;

typedef struct
{
    uint32_t modules_requested;
    uint32_t expressions_executed;
    uint32_t bytecode_fallbacks;
    uint32_t compile_failures;
    uint32_t modules_released;
    uint32_t expressions_eligible;
} behavior_expression_wasm_stats;

/* Emits one small, self-contained module for the eligible numeric input blocks
 * in a graph. Browser compilation is asynchronous; callers keep using bytecode
 * until an export is ready. NULL means the graph has no profitable region. */
behavior_expression_wasm *behavior_expression_wasm_compile(const graph_ir *ir, const behavior_asset *asset);
/* Emits bounded straight-line numeric regions from a compiled behavior VM.
 * The returned regions reference the same instruction indices and keep all
 * intermediate slots private to the region. */
behavior_expression_wasm *behavior_expression_wasm_compile_linear(
    const behavior_expression_wasm_instruction *instructions,
    int instruction_count,
    int slot_count,
    const pin_type *slot_types,
    behavior_expression_wasm_region **regions,
    int *region_count
);
void behavior_expression_wasm_free(behavior_expression_wasm *module);
int behavior_expression_wasm_count(const behavior_expression_wasm *module);
/* Returns false for unsupported blocks, pending compilation, or a failed
 * optional optimization. The caller then evaluates the original IR bytecode. */
bool behavior_expression_wasm_run(
    const behavior_expression_wasm *module,
    int block,
    const behavior_instance *instance,
    pin_value *out
);
/* VM counterpart: parameters are read from scalar slots instead of instance
 * variables, allowing the module to replace a supported numeric instruction
 * region during ordinary whole-function execution. */
bool behavior_expression_wasm_run_slots(
    const behavior_expression_wasm *module,
    int function,
    const pin_value *slots,
    pin_value *out
);
void behavior_expression_wasm_get_stats(behavior_expression_wasm_stats *out);
/* Main-thread smoke diagnostic for retained JS modules. */
int behavior_expression_wasm_registry_size();

#endif
