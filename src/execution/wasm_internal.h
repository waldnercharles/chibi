#ifndef CHIBI_EXECUTION_WASM_INTERNAL_H
#define CHIBI_EXECUTION_WASM_INTERNAL_H
#include "execution/wasm.h"
#ifdef __EMSCRIPTEN__
enum
{
    /* Amortize the JS crossing; tiny regions lose to the existing Wasm VM. */
    WASM_MIN_OPERATIONS = 16,
    WASM_OPERATIONS_PER_PARAMETER = 4,
    WASM_MAX_OPERATIONS = 256,
    WASM_MAX_LINEAR_FUNCTIONS = 128,
    WASM_MAX_LINEAR_OPERATIONS = 4096,
    WASM_MAX_GRAPH_FUNCTIONS = 128,
    WASM_MAX_GRAPH_BLOCK_INSTRUCTIONS = 1024,
    WASM_MAX_GRAPH_CANDIDATE_INSTRUCTIONS = 8192,
    WASM_MAX_GRAPH_BODY_BYTES = 32768,
    WASM_MAX_PARAMETERS = 8,
    WASM_MAX_MODULE_BYTES = 65536,
};

typedef struct
{
    unsigned char *data;
    size_t count, capacity;
} wasm_buffer;

typedef struct
{
    int *parameters;
    int parameter_count;
    pin_type type;
    int block;
    bool slot_parameters;
    wasm_buffer body;
} wasm_function;

void execution_wasm_byte(wasm_buffer *buffer, unsigned value);
void execution_wasm_u32(wasm_buffer *buffer, unsigned value);
unsigned execution_wasm_value_type(pin_type type);
behavior_expression_wasm *execution_wasm_publish_functions(
    wasm_function *functions,
    int function_count,
    int block_count
);
#endif
bool execution_wasm_run_variables(
    const behavior_expression_wasm *module,
    int block,
    const pin_value *variables,
    int variable_count,
    pin_value *out
);
#endif
