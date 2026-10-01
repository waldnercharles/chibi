#include "graph/compiler/behavior_expression_wasm.h"

#include "graph/model/behavior_asset.h"
#include "graph/runtime/instance.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <limits.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static atomic_uint_fast64_t next_handle = 1;
static atomic_uint_fast64_t worker_cache_epoch = 1;
static atomic_uint requested_modules;
static atomic_uint executed_expressions;
static atomic_uint bytecode_fallbacks;
static atomic_uint compile_failures;
static atomic_uint released_modules;
static atomic_uint eligible_expressions;

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#ifdef __EMSCRIPTEN_PTHREADS__
#include <emscripten/threading.h>
#endif

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

static uint64_t next_module_handle(void)
{
    uint_fast64_t current = atomic_load_explicit(&next_handle, memory_order_relaxed);
    for (;;) {
        if (current >= UINT64_MAX) {
            return 0;
        }
        if (atomic_compare_exchange_weak_explicit(&next_handle, &current, current + 1, memory_order_relaxed, memory_order_relaxed)) {
            return (uint64_t)current;
        }
    }
}

/* Generated modules have no imports. Main-thread compilation stays async;
 * workers synchronously compile a bounded copy on their first execution. */
// clang-format off
EM_JS(int, expression_wasm_apply, (int action,
                                   unsigned handle_low,
                                   unsigned handle_high,
                                   int block,
                                   const double *arguments,
                                   int argument_count,
                                   double *result,
                                   unsigned epoch_low,
                                   unsigned epoch_high,
                                   const unsigned char *bytes,
                                   int byte_count), {
    const idHigh = handle_high >>> 0;
    const idLow = handle_low >>> 0;
    let entry;
    if (typeof ENVIRONMENT_IS_PTHREAD !== 'undefined' && ENVIRONMENT_IS_PTHREAD) {
        if (action === 0) return 0;
        entry = globalThis['__chibiBehaviorExpressionWasmWorkerGet'](
            handle_low,
            handle_high,
            epoch_low,
            epoch_high,
            bytes,
            byte_count
        );
    } else {
        const key = '__chibiBehaviorExpressionWasm';
        let registry = globalThis[key];
        if (!registry) {
            registry = new Map();
            globalThis[key] = registry;
        }
        const modules = registry.get(idHigh);
        entry = modules && modules.get(idLow);
        if (!entry) {
            entry = globalThis['__chibiBehaviorExpressionWasmMainBegin'](
                registry,
                idHigh,
                idLow,
                bytes,
                byte_count
            );
        }
    }
    if (action === 0 || entry.state <= 0) return entry.state;
    try {
        const first = (arguments >>> 0) / Float64Array.BYTES_PER_ELEMENT;
        const fn = entry.functions[block];
        if (!fn) {
            entry.state = -1;
            entry.functions = null;
            return -1;
        }
        let value;
        switch (argument_count) {
            case 0: value = fn(); break;
            case 1: value = fn(HEAPF64[first]); break;
            case 2: value = fn(HEAPF64[first], HEAPF64[first + 1]); break;
            case 3: value = fn(HEAPF64[first], HEAPF64[first + 1], HEAPF64[first + 2]); break;
            case 4: value = fn(HEAPF64[first], HEAPF64[first + 1], HEAPF64[first + 2], HEAPF64[first + 3]); break;
            case 5: value = fn(HEAPF64[first], HEAPF64[first + 1], HEAPF64[first + 2], HEAPF64[first + 3], HEAPF64[first + 4]); break;
            case 6: value = fn(HEAPF64[first], HEAPF64[first + 1], HEAPF64[first + 2], HEAPF64[first + 3], HEAPF64[first + 4], HEAPF64[first + 5]); break;
            case 7: value = fn(HEAPF64[first], HEAPF64[first + 1], HEAPF64[first + 2], HEAPF64[first + 3], HEAPF64[first + 4], HEAPF64[first + 5], HEAPF64[first + 6]); break;
            case 8: value = fn(HEAPF64[first], HEAPF64[first + 1], HEAPF64[first + 2], HEAPF64[first + 3], HEAPF64[first + 4], HEAPF64[first + 5], HEAPF64[first + 6], HEAPF64[first + 7]); break;
            default: return -1;
        }
        HEAPF64[(result >>> 0) / Float64Array.BYTES_PER_ELEMENT] = value;
        return 1;
    } catch (_) {
        entry.state = -1;
        entry.functions = null;
        return -1;
    }
})
// clang-format on

// clang-format off
EM_JS(void, expression_wasm_registry_drop, (unsigned handle_low, unsigned handle_high), {
    const registry = globalThis['__chibiBehaviorExpressionWasm'];
    if (registry) {
        const high = handle_high >>> 0;
        const low = handle_low >>> 0;
        const modules = registry.get(high);
        if (modules) {
            modules.delete(low);
            if (!modules.size) registry.delete(high);
        }
    }
})
// clang-format on

// clang-format off
EM_JS(int, expression_wasm_registry_size, (void), {
    if (typeof ENVIRONMENT_IS_PTHREAD !== 'undefined' && ENVIRONMENT_IS_PTHREAD) {
        const cache = globalThis['__chibiBehaviorExpressionWasmWorkerCache'];
        return cache ? cache.entries.length : 0;
    }
    const registry = globalThis['__chibiBehaviorExpressionWasm'];
    if (!registry) return 0;
    let count = 0;
    for (const modules of registry.values()) count += modules.size;
    return count;
})
// clang-format on

// clang-format off
EM_JS(void, expression_wasm_dispatch_setup, (void), {
    globalThis['__chibiBehaviorExpressionWasmMainBegin'] = (registry, high, low, source, length) => {
        let modules = registry.get(high);
        if (!modules) {
            modules = new Map();
            registry.set(high, modules);
        }
        const existing = modules.get(low);
        if (existing) return existing;
        const entry = { state: 0, functions: null };
        modules.set(low, entry);
        const copy = HEAPU8.slice(source >>> 0, (source >>> 0) + length);
        WebAssembly.compile(copy)
            .then((compiled) => WebAssembly.instantiate(compiled, {}))
            .then((instance) => {
                if (registry.get(high) === modules && modules.get(low) === entry) {
                    entry.functions = [];
                    for (const [name, fn] of Object.entries(instance.exports)) {
                        entry.functions[Number(name.slice(6))] = fn;
                    }
                    entry.state = 1;
                }
            })
            .catch(() => {
                if (registry.get(high) === modules && modules.get(low) === entry) entry.state = -1;
            });
        return entry;
    };
})
// clang-format on

// clang-format off
EM_JS(void, expression_wasm_worker_cache_setup, (void), {
    globalThis['__chibiBehaviorExpressionWasmWorkerGet'] = (
        handle_low, handle_high, epoch_low, epoch_high, bytes, byte_count
    ) => {
        const idHigh = handle_high >>> 0;
        const idLow = handle_low >>> 0;
        const retiredHigh = epoch_high >>> 0;
        const retiredLow = epoch_low >>> 0;
        let cache = globalThis['__chibiBehaviorExpressionWasmWorkerCache'];
        if (!cache) {
            cache = { epochHigh: retiredHigh, epochLow: retiredLow, entries: [], last: null };
            globalThis['__chibiBehaviorExpressionWasmWorkerCache'] = cache;
        } else if (cache.epochHigh !== retiredHigh || cache.epochLow !== retiredLow) {
            cache.entries.length = 0;
            cache.last = null;
            cache.epochHigh = retiredHigh;
            cache.epochLow = retiredLow;
        }
        let entry = cache.last;
        if (!entry || entry.idHigh !== idHigh || entry.idLow !== idLow) {
            const entries = cache.entries;
            let index = 0;
            for (; index < entries.length; index++) {
                if (entries[index].idHigh === idHigh && entries[index].idLow === idLow) break;
            }
            if (index < entries.length) {
                entry = entries[index];
                for (let i = index; i < entries.length - 1; i++) entries[i] = entries[i + 1];
                entries.length--;
            } else {
                entry = { idHigh, idLow, state: -1, functions: null };
                try {
                    const copy = HEAPU8.slice(bytes >>> 0, (bytes >>> 0) + byte_count);
                    const instance = new WebAssembly.Instance(new WebAssembly.Module(copy), {});
                    entry.functions = [];
                    for (const [name, fn] of Object.entries(instance.exports)) {
                        entry.functions[Number(name.slice(6))] = fn;
                    }
                    entry.state = 1;
                } catch (_) {}
            }
            entries.push(entry);
            if (entries.length > 16) entries.shift();
            cache.last = entry;
        }
        return entry;
    };
})
// clang-format on

static void expression_wasm_dispatch_initialize(void)
{
    static _Thread_local bool initialized;
    if (initialized) {
        return;
    }
    expression_wasm_dispatch_setup();
    expression_wasm_worker_cache_setup();
    initialized = true;
}

typedef enum
{
    VALUE_LITERAL,
    VALUE_VARIABLE,
    VALUE_BINARY,
} value_kind;

typedef struct
{
    value_kind kind;
    pin_type type;
    pin_value literal;
    int variable;
    int parameter;
    int left, right;
    int opcode;
    int local;
} expression_value;

typedef struct
{
    const node *node;
    int pin;
    bool output;
    int value;
} expression_slot;

typedef struct
{
    expression_value *values;
    expression_slot *slots;
    int *parameters;
    const behavior_asset *asset;
    int result;
    int operations;
    pin_type type;
} expression_builder;

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

struct behavior_expression_wasm
{
    uint64_t handle;
    unsigned char *bytes;
    size_t byte_count;
    int expression_count;
    int block_count;
    atomic_bool failure_reported;
    wasm_function *expressions; /* Indexed by graph IR block. */
};

static void byte(wasm_buffer *buffer, unsigned value)
{
    if (buffer->count == buffer->capacity) {
        buffer->capacity = buffer->capacity ? buffer->capacity * 2 : 32;
        buffer->data = xrealloc(buffer->data, buffer->capacity);
    }
    buffer->data[buffer->count++] = (unsigned char)value;
}

static void bytes(wasm_buffer *buffer, const void *source, size_t count)
{
    if (!count) {
        return;
    }
    while (buffer->count + count > buffer->capacity) {
        buffer->capacity = buffer->capacity ? buffer->capacity * 2 : 32;
    }
    buffer->data = xrealloc(buffer->data, buffer->capacity);
    memcpy(buffer->data + buffer->count, source, count);
    buffer->count += count;
}

static void u32(wasm_buffer *buffer, unsigned value)
{
    do {
        unsigned next = value & 0x7f;
        value >>= 7;
        byte(buffer, next | (value ? 0x80 : 0));
    } while (value);
}

static void section(wasm_buffer *module, unsigned id, const wasm_buffer *payload)
{
    byte(module, id);
    u32(module, (unsigned)payload->count);
    bytes(module, payload->data, payload->count);
}

static void name(wasm_buffer *buffer, const char *text)
{
    size_t size = strlen(text);
    u32(buffer, (unsigned)size);
    bytes(buffer, text, size);
}

static bool real(const pin_spec *pin)
{
    return pin->container == PIN_CONTAINER_SINGLE &&
           (pin->type == PIN_F32 || pin->type == PIN_F64);
}

static int find(const expression_builder *builder, const node *n, int pin, bool output)
{
    for (int i = acount(builder->slots) - 1; i >= 0; i--) {
        const expression_slot *slot = &builder->slots[i];
        if (slot->node == n && slot->pin == pin && slot->output == output) {
            return slot->value;
        }
    }
    return -1;
}

static int add_value(expression_builder *builder, expression_value value)
{
    apush(builder->values, value);
    return acount(builder->values) - 1;
}

static int variable(expression_builder *builder, uint64_t id, const pin_spec *spec)
{
    const behavior_asset *asset = builder->asset;
    for (int i = 0; i < acount(asset->variables); i++) {
        const behavior_variable *v = &asset->variables[i];
        if (v->id != id || v->type != spec->type || v->container != PIN_CONTAINER_SINGLE) {
            continue;
        }
        int parameter = -1;
        for (int j = 0; j < acount(builder->parameters); j++) {
            if (builder->parameters[j] == i) {
                parameter = j;
                break;
            }
        }
        if (parameter < 0) {
            apush(builder->parameters, i);
            parameter = acount(builder->parameters) - 1;
        }
        return add_value(
            builder,
            (expression_value){ .kind = VALUE_VARIABLE,
                                .type = v->type,
                                .variable = i,
                                .parameter = parameter,
                                .local = -1 }
        );
    }
    return -1;
}

static int bind(expression_builder *builder, const behavior_asset *asset, const pin_spec *spec)
{
    if (!real(spec)) {
        return -1;
    }
    if (!builder->type) {
        builder->type = spec->type;
    }
    if (builder->type != spec->type) {
        return -1;
    }
    if (!spec->binding) {
        if (spec->default_value.type != spec->type) {
            return -1;
        }
        return add_value(
            builder,
            (expression_value){ .kind = VALUE_LITERAL,
                                .type = spec->type,
                                .literal = spec->default_value,
                                .local = -1 }
        );
    }
    const pin_binding *binding = spec->binding;
    if (binding->kind != PIN_BINDING_VARIABLE ||
        (binding->source_asset && strcmp(binding->source_asset, asset->name))) {
        return -1;
    }
    return variable(builder, binding->variable_id, spec);
}

static int evaluate(expression_builder *builder, const behavior_asset *asset, const node *n)
{
    if (n->disabled || n->desc->flow || n->output_count != 1 ||
        !real(&n->output_pins[0])) {
        return -1;
    }
    if (!builder->type) {
        builder->type = n->output_pins[0].type;
    }
    if (builder->type != n->output_pins[0].type) {
        return -1;
    }
    if (n->desc->pure_kind == NODE_PURE_GET) {
        return variable(builder, n->reference_id, &n->output_pins[0]);
    }
    if (n->desc->pure_kind == NODE_PURE_CONST) {
        const pin_spec *spec = &n->output_pins[0];
        if (spec->default_value.type != spec->type) {
            return -1;
        }
        return add_value(
            builder,
            (expression_value){ .kind = VALUE_LITERAL,
                                .type = spec->type,
                                .literal = spec->default_value,
                                .local = -1 }
        );
    }
    if (n->desc->pure_kind == NODE_PURE_COPY && n->input_count == 1 &&
        real(&n->input_pins[0]) && n->input_pins[0].type == n->output_pins[0].type) {
        return find(builder, n, 0, false);
    }
    if (n->desc->pure_kind != NODE_PURE_CALL || n->input_count != 2 ||
        !real(&n->input_pins[0]) || !real(&n->input_pins[1]) ||
        n->input_pins[0].type != n->output_pins[0].type ||
        n->input_pins[1].type != n->output_pins[0].type) {
        return -1;
    }
    int opcode;
    switch (n->desc->jit_kind) {
        case NODE_JIT_ADD:
            opcode = n->output_pins[0].type == PIN_F32 ? 0x92 : 0xa0;
            break;
        case NODE_JIT_SUB:
            opcode = n->output_pins[0].type == PIN_F32 ? 0x93 : 0xa1;
            break;
        case NODE_JIT_MUL:
            opcode = n->output_pins[0].type == PIN_F32 ? 0x94 : 0xa2;
            break;
        case NODE_JIT_DIV:
            opcode = n->output_pins[0].type == PIN_F32 ? 0x95 : 0xa3;
            break;
        default: return -1;
    }
    int left = find(builder, n, 0, false), right = find(builder, n, 1, false);
    if (left < 0 || right < 0) {
        return -1;
    }
    builder->operations++;
    return add_value(
        builder,
        (expression_value){ .kind = VALUE_BINARY,
                            .type = n->output_pins[0].type,
                            .left = left,
                            .right = right,
                            .opcode = opcode,
                            .local = -1 }
    );
}

static bool lower(
    expression_builder *builder,
    const graph_ir *ir,
    const graph_ir_block *block,
    const behavior_asset *asset
)
{
    if (block->output) {
        return false;
    }
    builder->asset = asset;
    int result = -1;
    for (int pc = block->first; pc < block->first + block->count; pc++) {
        const graph_ir_behavior_instruction *instruction = &ir->behavior[pc];
        if (instruction->op == GRAPH_IR_MEMO_BRANCH) {
            continue;
        }
        bool output = instruction->op == GRAPH_IR_EVALUATE;
        int pin = output ? 0 : instruction->pin;
        if (instruction->op == GRAPH_IR_EVALUATE) {
            result = evaluate(builder, asset, instruction->node);
        } else if (instruction->op == GRAPH_IR_BIND_INPUT) {
            result = bind(builder, asset, &instruction->node->input_pins[pin]);
        } else if (instruction->op == GRAPH_IR_READ_SLOT) {
            const pin_spec *to = &instruction->node->input_pins[pin];
            const pin_spec *from = &instruction->source->output_pins[instruction->source_pin];
            if (instruction->field || instruction->conversion || !real(to) ||
                to->type != from->type ||
                (builder->type && builder->type != to->type)) {
                return false;
            }
            builder->type = to->type;
            result = find(builder, instruction->source, instruction->source_pin, true);
        }
        if (result < 0) {
            return false;
        }
        apush(builder->slots, ((expression_slot){ instruction->node, pin, output, result }));
    }
    builder->result = result;
    return result >= 0 && builder->type != 0;
}

static unsigned value_type(pin_type type)
{
    return type == PIN_F32 ? 0x7d : 0x7c;
}

static void emit_value(wasm_buffer *body, expression_builder *builder, int index)
{
    const expression_value *value = &builder->values[index];
    if (value->kind == VALUE_VARIABLE) {
        byte(body, 0x20); /* local.get */
        u32(body, (unsigned)value->parameter);
        return;
    }
    if (value->kind == VALUE_LITERAL) {
        if (value->type == PIN_F32) {
            uint32_t bits;
            memcpy(&bits, &value->literal.f, sizeof bits);
            byte(body, 0x43); /* f32.const */
            for (int i = 0; i < 4; i++) {
                byte(body, bits >> (i * 8));
            }
        } else {
            uint64_t bits;
            memcpy(&bits, &value->literal.d, sizeof bits);
            byte(body, 0x44); /* f64.const */
            for (int i = 0; i < 8; i++) {
                byte(body, (unsigned)(bits >> (i * 8)));
            }
        }
        return;
    }
    assert(value->local >= 0);
    byte(body, 0x20);
    u32(body, (unsigned)value->local);
}

static bool build_body(
    wasm_function *function,
    const graph_ir *ir,
    const graph_ir_block *block,
    const behavior_asset *asset
)
{
    expression_builder builder = { 0 };
    if (!lower(&builder, ir, block, asset) || builder.operations < WASM_MIN_OPERATIONS ||
        builder.operations > WASM_MAX_OPERATIONS ||
        acount(builder.parameters) > WASM_MAX_PARAMETERS ||
        builder.operations < acount(builder.parameters) * WASM_OPERATIONS_PER_PARAMETER) {
        afree(builder.values);
        afree(builder.slots);
        afree(builder.parameters);
        return false;
    }
    builder.type = builder.values[builder.result].type;
    int next_local = acount(builder.parameters);
    for (int i = 0; i < acount(builder.values); i++) {
        if (builder.values[i].kind == VALUE_BINARY) {
            builder.values[i].local = next_local++;
        }
    }
    assert(next_local == acount(builder.parameters) + builder.operations);
    if (builder.operations) {
        u32(&function->body, 1); /* one local declaration group */
        u32(&function->body, (unsigned)builder.operations);
        byte(&function->body, value_type(builder.type));
    } else {
        u32(&function->body, 0);
    }
    for (int i = 0; i < acount(builder.values); i++) {
        const expression_value *value = &builder.values[i];
        if (value->kind != VALUE_BINARY) {
            continue;
        }
        emit_value(&function->body, &builder, value->left);
        emit_value(&function->body, &builder, value->right);
        byte(&function->body, (unsigned)value->opcode);
        byte(&function->body, 0x21); /* local.set */
        u32(&function->body, (unsigned)value->local);
    }
    emit_value(&function->body, &builder, builder.result);
    byte(&function->body, 0x0b); /* end */
    function->parameters = builder.parameters;
    function->parameter_count = acount(builder.parameters);
    function->type = builder.type;
    function->block = -1; /* The caller assigns the IR block index. */
    afree(builder.values);
    afree(builder.slots);
    return function->body.count < WASM_MAX_MODULE_BYTES;
}

static bool linear_operation(const behavior_expression_wasm_instruction *instruction)
{
    return instruction->op >= BEHAVIOR_WASM_ADD &&
           instruction->op <= BEHAVIOR_WASM_DIV &&
           (instruction->type == PIN_F32 || instruction->type == PIN_F64);
}

static int slot_parameter(wasm_function *function, int slot, int *parameter_epoch, int *parameter_indices, int epoch)
{
    if (parameter_epoch[slot] == epoch) {
        return parameter_indices[slot];
    }
    if (function->parameter_count == WASM_MAX_PARAMETERS) {
        return -1;
    }
    apush(function->parameters, slot);
    parameter_epoch[slot] = epoch;
    parameter_indices[slot] = function->parameter_count++;
    return parameter_indices[slot];
}

static bool build_linear_body(
    wasm_function *function,
    const behavior_expression_wasm_instruction *instructions,
    int first,
    int end,
    int slot_count,
    int *slot_epoch,
    int *slot_operation,
    int *parameter_epoch,
    int *parameter_indices,
    int epoch
)
{
    int operation_count = end - first;
    for (int pc = first; pc < end; pc++) {
        const behavior_expression_wasm_instruction *instruction = &instructions[pc];
        if (instruction->output < 0 || instruction->output >= slot_count) {
            return false;
        }
        slot_epoch[instruction->output] = epoch;
        slot_operation[instruction->output] = pc - first;
    }
    for (int pc = first; pc < end; pc++) {
        const behavior_expression_wasm_instruction *instruction = &instructions[pc];
        int inputs[2] = { instruction->left, instruction->right };
        for (int operand = 0; operand < 2; operand++) {
            if (!(operand ? instruction->right_slot : instruction->left_slot)) {
                return false;
            }
            int slot = inputs[operand];
            if (slot < 0 || slot >= slot_count) {
                return false;
            }
            if (slot_epoch[slot] == epoch) {
                if (slot_operation[slot] >= pc - first) {
                    return false;
                }
                continue;
            }
            if (slot_parameter(function, slot, parameter_epoch, parameter_indices, epoch) <
                0) {
                return false;
            }
        }
    }
    if (function->parameter_count > WASM_MAX_PARAMETERS ||
        operation_count < function->parameter_count * WASM_OPERATIONS_PER_PARAMETER) {
        return false;
    }
    function->slot_parameters = true;
    function->type = instructions[first].type;
    function->block = -1;    /* The caller assigns the region index. */
    u32(&function->body, 1); /* one local declaration group */
    u32(&function->body, (unsigned)operation_count);
    byte(&function->body, value_type(function->type));
    for (int pc = first; pc < end; pc++) {
        const behavior_expression_wasm_instruction *instruction = &instructions[pc];
        int inputs[2] = { instruction->left, instruction->right };
        for (int operand = 0; operand < 2; operand++) {
            int slot = inputs[operand];
            byte(&function->body, 0x20); /* local.get */
            u32(&function->body,
                slot_epoch[slot] == epoch
                    ? (unsigned)(function->parameter_count + slot_operation[slot])
                    : (unsigned)parameter_indices[slot]);
        }
        byte(
            &function->body,
            instruction->type == PIN_F32
                ? (unsigned[]){ 0x92, 0x93, 0x94, 0x95 }[instruction->op - BEHAVIOR_WASM_ADD]
                : (unsigned[]){ 0xa0, 0xa1, 0xa2, 0xa3 }[instruction->op - BEHAVIOR_WASM_ADD]
        );
        byte(&function->body, 0x21); /* local.set */
        u32(&function->body, (unsigned)(function->parameter_count + pc - first));
    }
    byte(&function->body, 0x20); /* local.get of the region result */
    u32(&function->body, (unsigned)(function->parameter_count + operation_count - 1));
    byte(&function->body, 0x0b); /* end */
    return function->body.count < WASM_MAX_MODULE_BYTES;
}

static void free_functions(wasm_function *functions)
{
    for (int i = 0; i < acount(functions); i++) {
        afree(functions[i].parameters);
        free(functions[i].body.data);
    }
    afree(functions);
}

static bool build_module(wasm_function *functions, wasm_buffer *module);

static behavior_expression_wasm *publish_functions(wasm_function *functions, int function_count, int block_count)
{
    if (!function_count) {
        free_functions(functions);
        return NULL;
    }
    wasm_buffer encoded = { 0 };
    if (!build_module(functions, &encoded)) {
        free(encoded.data);
        free_functions(functions);
        atomic_fetch_add_explicit(&compile_failures, 1, memory_order_relaxed);
        return NULL;
    }
    uint64_t handle = next_module_handle();
    if (!handle) {
        free(encoded.data);
        free_functions(functions);
        atomic_fetch_add_explicit(&compile_failures, 1, memory_order_relaxed);
        return NULL;
    }
    behavior_expression_wasm *module = xcalloc(1, sizeof *module);
    module->bytes = encoded.data;
    module->byte_count = encoded.count;
    module->expressions = xcalloc((size_t)block_count, sizeof *module->expressions);
    module->expression_count = function_count;
    module->block_count = block_count;
    atomic_init(&module->failure_reported, false);
    for (int i = 0; i < function_count; i++) {
        assert(functions[i].block >= 0 && functions[i].block < block_count);
        module->expressions[functions[i].block] = functions[i];
        functions[i].parameters = NULL;
        functions[i].body.data = NULL;
    }
    module->handle = handle;
    uint64_t epoch = atomic_load_explicit(&worker_cache_epoch, memory_order_acquire);
    expression_wasm_dispatch_initialize();
    atomic_fetch_add_explicit(&requested_modules, 1, memory_order_relaxed);
    atomic_fetch_add_explicit(&eligible_expressions, (unsigned)module->expression_count, memory_order_relaxed);
    expression_wasm_apply(
        0,
        (unsigned)module->handle,
        (unsigned)(module->handle >> 32),
        0,
        NULL,
        0,
        NULL,
        (unsigned)epoch,
        (unsigned)(epoch >> 32),
        module->bytes,
        (int)module->byte_count
    );
    free_functions(functions);
    return module;
}

static bool build_module(wasm_function *functions, wasm_buffer *module)
{
    static const unsigned char header[] = { 0x00, 0x61, 0x73, 0x6d,
                                            0x01, 0x00, 0x00, 0x00 };
    bytes(module, header, sizeof header);

    wasm_buffer payload = { 0 };
    u32(&payload, (unsigned)acount(functions));
    for (int i = 0; i < acount(functions); i++) {
        byte(&payload, 0x60);
        u32(&payload, (unsigned)functions[i].parameter_count);
        for (int j = 0; j < functions[i].parameter_count; j++) {
            byte(&payload, value_type(functions[i].type));
        }
        u32(&payload, 1);
        byte(&payload, value_type(functions[i].type));
    }
    section(module, 1, &payload);
    free(payload.data);

    payload = (wasm_buffer){ 0 };
    u32(&payload, (unsigned)acount(functions));
    for (int i = 0; i < acount(functions); i++) {
        u32(&payload, (unsigned)i);
    }
    section(module, 3, &payload);
    free(payload.data);

    payload = (wasm_buffer){ 0 };
    u32(&payload, (unsigned)acount(functions));
    for (int i = 0; i < acount(functions); i++) {
        char export_name[32];
        snprintf(export_name, sizeof export_name, "block_%d", functions[i].block);
        name(&payload, export_name);
        byte(&payload, 0); /* function export */
        u32(&payload, (unsigned)i);
    }
    section(module, 7, &payload);
    free(payload.data);

    payload = (wasm_buffer){ 0 };
    u32(&payload, (unsigned)acount(functions));
    for (int i = 0; i < acount(functions); i++) {
        wasm_buffer *body = &functions[i].body;
        wasm_buffer sized = { 0 };
        u32(&sized, (unsigned)body->count);
        bytes(&sized, body->data, body->count);
        bytes(&payload, sized.data, sized.count);
        free(sized.data);
    }
    section(module, 10, &payload);
    free(payload.data);
    return module->count <= WASM_MAX_MODULE_BYTES;
}

behavior_expression_wasm *behavior_expression_wasm_compile(const graph_ir *ir, const behavior_asset *asset)
{
    wasm_function *functions = NULL;
    int candidate_instructions = 0;
    int body_bytes = 0;
    for (int block = 0; block < acount(ir->blocks); block++) {
        const graph_ir_block *source = &ir->blocks[block];
        if (source->output || source->count < WASM_MIN_OPERATIONS ||
            source->count > WASM_MAX_GRAPH_BLOCK_INSTRUCTIONS) {
            continue;
        }
        if (acount(functions) >= WASM_MAX_GRAPH_FUNCTIONS ||
            candidate_instructions + source->count >
                WASM_MAX_GRAPH_CANDIDATE_INSTRUCTIONS) {
            break;
        }
        candidate_instructions += source->count;
        wasm_function candidate = { 0 };
        if (!build_body(&candidate, ir, source, asset)) {
            afree(candidate.parameters);
            free(candidate.body.data);
            continue;
        }
        if (body_bytes + (int)candidate.body.count > WASM_MAX_GRAPH_BODY_BYTES) {
            afree(candidate.parameters);
            free(candidate.body.data);
            break;
        }
        body_bytes += (int)candidate.body.count;
        candidate.block = block;
        apush(functions, candidate);
    }
    return publish_functions(functions, acount(functions), acount(ir->blocks));
}

behavior_expression_wasm *behavior_expression_wasm_compile_linear(
    const behavior_expression_wasm_instruction *instructions,
    int instruction_count,
    int slot_count,
    const pin_type *slot_types,
    behavior_expression_wasm_region **out_regions,
    int *out_region_count
)
{
    assert(out_regions && out_region_count);
    *out_regions = NULL;
    *out_region_count = 0;
    if (!instructions || instruction_count <= 0 || slot_count <= 0 || !slot_types) {
        return NULL;
    }

    int *use_count = xcalloc((size_t)slot_count, sizeof *use_count);
    int *inside_count = xcalloc((size_t)slot_count, sizeof *inside_count);
    int *slot_epoch = xcalloc((size_t)slot_count, sizeof *slot_epoch);
    int *slot_operation = xcalloc((size_t)slot_count, sizeof *slot_operation);
    int *parameter_epoch = xcalloc((size_t)slot_count, sizeof *parameter_epoch);
    int *parameter_indices = xcalloc((size_t)slot_count, sizeof *parameter_indices);
    bool *control_entries = xcalloc((size_t)instruction_count, sizeof *control_entries);
    for (int pc = 0; pc < instruction_count; pc++) {
        const behavior_expression_wasm_instruction *instruction = &instructions[pc];
        if (instruction->control_transfer && instruction->control_target >= 0 &&
            instruction->control_target < instruction_count) {
            control_entries[instruction->control_target] = true;
        }
        if (instruction->left_slot && instruction->left >= 0 &&
            instruction->left < slot_count) {
            use_count[instruction->left]++;
        }
        if (instruction->right_slot && instruction->right >= 0 &&
            instruction->right < slot_count) {
            use_count[instruction->right]++;
        }
        if (instruction->third_slot && instruction->third >= 0 &&
            instruction->third < slot_count) {
            use_count[instruction->third]++;
        }
    }

    wasm_function *functions = NULL;
    behavior_expression_wasm_region *regions = NULL;
    int epoch = 1;
    int compiled_operations = 0;
    for (int first = 0; first < instruction_count;) {
        if (!linear_operation(&instructions[first])) {
            first++;
            continue;
        }
        int end = first + 1;
        while (end < instruction_count && linear_operation(&instructions[end]) &&
               instructions[end].type == instructions[first].type) {
            end++;
        }
        int count = end - first;
        if (count < WASM_MIN_OPERATIONS || count > WASM_MAX_OPERATIONS ||
            epoch == INT_MAX) {
            first = end;
            continue;
        }
        if (acount(functions) >= WASM_MAX_LINEAR_FUNCTIONS ||
            compiled_operations + count > WASM_MAX_LINEAR_OPERATIONS) {
            break;
        }
        epoch++;
        bool valid = true;
        for (int pc = first + 1; pc < end; pc++) {
            if (control_entries[pc]) {
                valid = false;
                break;
            }
        }
        if (!valid) {
            first = end;
            continue;
        }
        for (int pc = first; pc < end; pc++) {
            const behavior_expression_wasm_instruction *instruction = &instructions[pc];
            int output = instruction->output;
            if (output < 0 || output >= slot_count || slot_epoch[output] == epoch) {
                valid = false;
                break;
            }
            slot_epoch[output] = epoch;
            slot_operation[output] = pc - first;
            inside_count[output] = 0;
        }
        for (int pc = first; valid && pc < end; pc++) {
            const behavior_expression_wasm_instruction *instruction = &instructions[pc];
            int inputs[2] = { instruction->left, instruction->right };
            for (int operand = 0; operand < 2; operand++) {
                bool is_slot = operand ? instruction->right_slot : instruction->left_slot;
                int slot = inputs[operand];
                if (!is_slot || slot < 0 || slot >= slot_count ||
                    slot_types[slot] != instruction->type) {
                    valid = false;
                    break;
                }
                if (slot_epoch[slot] == epoch) {
                    if (slot_operation[slot] >= pc - first) {
                        valid = false;
                        break;
                    }
                    inside_count[slot]++;
                }
            }
        }
        int result_slot = valid ? instructions[end - 1].output : -1;
        for (int pc = first; valid && pc < end - 1; pc++) {
            int output = instructions[pc].output;
            if (!inside_count[output] || inside_count[output] != use_count[output]) {
                valid = false;
            }
        }
        if (valid && (!use_count[result_slot] ||
                      slot_types[result_slot] != instructions[first].type)) {
            valid = false;
        }
        if (valid) {
            wasm_function candidate = { 0 };
            valid = build_linear_body(
                &candidate,
                instructions,
                first,
                end,
                slot_count,
                slot_epoch,
                slot_operation,
                parameter_epoch,
                parameter_indices,
                epoch
            );
            if (valid) {
                int function = acount(functions);
                candidate.block = function;
                apush(functions, candidate);
                apush(
                    regions,
                    ((behavior_expression_wasm_region){ .first = first,
                                                        .end = end,
                                                        .function = function,
                                                        .result_slot = result_slot })
                );
                compiled_operations += count;
            } else {
                afree(candidate.parameters);
                free(candidate.body.data);
            }
        }
        first = end;
    }
    free(use_count);
    free(inside_count);
    free(slot_epoch);
    free(slot_operation);
    free(parameter_epoch);
    free(parameter_indices);
    free(control_entries);
    behavior_expression_wasm
        *module = publish_functions(functions, acount(functions), acount(regions));
    if (!module) {
        afree(regions);
        return NULL;
    }
    *out_regions = regions;
    *out_region_count = acount(regions);
    return module;
}

void behavior_expression_wasm_free(behavior_expression_wasm *module)
{
    if (!module) {
        return;
    }
    atomic_fetch_add_explicit(&worker_cache_epoch, 1, memory_order_acq_rel);
#ifdef __EMSCRIPTEN_PTHREADS__
    if (emscripten_is_main_runtime_thread()) {
        expression_wasm_registry_drop(
            (unsigned)module->handle,
            (unsigned)(module->handle >> 32)
        );
    } else {
        emscripten_async_run_in_main_runtime_thread(
            EM_FUNC_SIG_VII,
            expression_wasm_registry_drop,
            (unsigned)module->handle,
            (unsigned)(module->handle >> 32)
        );
    }
#else
    expression_wasm_registry_drop((unsigned)module->handle, (unsigned)(module->handle >> 32));
#endif
    for (int i = 0; i < module->block_count; i++) {
        afree(module->expressions[i].parameters);
        free(module->expressions[i].body.data);
    }
    free(module->expressions);
    free(module->bytes);
    atomic_fetch_add_explicit(&released_modules, 1, memory_order_relaxed);
    free(module);
}

int behavior_expression_wasm_count(const behavior_expression_wasm *module)
{
    return module ? module->expression_count : 0;
}

int behavior_expression_wasm_registry_size()
{
    return expression_wasm_registry_size();
}

bool behavior_expression_wasm_run(
    const behavior_expression_wasm *module,
    int block,
    const behavior_instance *instance,
    pin_value *out
)
{
    if (!module || block < 0 || block >= module->block_count ||
        !module->expressions[block].body.data || !instance || !out) {
        return false;
    }
    double arguments[WASM_MAX_PARAMETERS] = { 0 };
    const wasm_function *function = &module->expressions[block];
    if (function->slot_parameters || function->parameter_count > WASM_MAX_PARAMETERS) {
        return false;
    }
    expression_wasm_dispatch_initialize();
    for (int i = 0; i < function->parameter_count; i++) {
        int variable = function->parameters[i];
        assert(variable >= 0 && variable < instance->variable_count);
        pin_value value = instance->variables[variable];
        arguments[i] = function->type == PIN_F32 ? (double)value.f : value.d;
    }
    double result = 0;
    uint64_t epoch = atomic_load_explicit(&worker_cache_epoch, memory_order_acquire);
    int status = expression_wasm_apply(
        1,
        (unsigned)module->handle,
        (unsigned)(module->handle >> 32),
        block,
        arguments,
        function->parameter_count,
        &result,
        (unsigned)epoch,
        (unsigned)(epoch >> 32),
        module->bytes,
        (int)module->byte_count
    );
    if (status != 1) {
        atomic_fetch_add_explicit(&bytecode_fallbacks, 1, memory_order_relaxed);
        if (status < 0 &&
            !atomic_exchange_explicit((atomic_bool *)&module->failure_reported, true, memory_order_relaxed)) {
            atomic_fetch_add_explicit(&compile_failures, 1, memory_order_relaxed);
        }
        return false;
    }
    if (function->type == PIN_F32) {
        out->f = (float)result;
    } else {
        out->d = result;
    }
    atomic_fetch_add_explicit(&executed_expressions, 1, memory_order_relaxed);
    return true;
}

bool behavior_expression_wasm_run_slots(
    const behavior_expression_wasm *module,
    int block,
    const pin_value *slots,
    pin_value *out
)
{
    if (!module || block < 0 || block >= module->block_count ||
        !module->expressions[block].body.data || !slots || !out) {
        return false;
    }
    const wasm_function *function = &module->expressions[block];
    if (!function->slot_parameters || function->parameter_count > WASM_MAX_PARAMETERS) {
        return false;
    }
    expression_wasm_dispatch_initialize();
    double arguments[WASM_MAX_PARAMETERS] = { 0 };
    for (int i = 0; i < function->parameter_count; i++) {
        pin_value value = slots[function->parameters[i]];
        arguments[i] = function->type == PIN_F32 ? (double)value.f : value.d;
    }
    double result = 0;
    uint64_t epoch = atomic_load_explicit(&worker_cache_epoch, memory_order_acquire);
    int status = expression_wasm_apply(
        1,
        (unsigned)module->handle,
        (unsigned)(module->handle >> 32),
        block,
        arguments,
        function->parameter_count,
        &result,
        (unsigned)epoch,
        (unsigned)(epoch >> 32),
        module->bytes,
        (int)module->byte_count
    );
    if (status != 1) {
        atomic_fetch_add_explicit(&bytecode_fallbacks, 1, memory_order_relaxed);
        if (status < 0 &&
            !atomic_exchange_explicit((atomic_bool *)&module->failure_reported, true, memory_order_relaxed)) {
            atomic_fetch_add_explicit(&compile_failures, 1, memory_order_relaxed);
        }
        return false;
    }
    if (function->type == PIN_F32) {
        out->f = (float)result;
    } else {
        out->d = result;
    }
    atomic_fetch_add_explicit(&executed_expressions, 1, memory_order_relaxed);
    return true;
}

#else

struct behavior_expression_wasm
{
    int unused;
};

behavior_expression_wasm *behavior_expression_wasm_compile(const graph_ir *ir, const behavior_asset *asset)
{
    (void)ir;
    (void)asset;
    return NULL;
}

behavior_expression_wasm *behavior_expression_wasm_compile_linear(
    const behavior_expression_wasm_instruction *instructions,
    int instruction_count,
    int slot_count,
    const pin_type *slot_types,
    behavior_expression_wasm_region **regions,
    int *region_count
)
{
    (void)instructions;
    (void)instruction_count;
    (void)slot_count;
    (void)slot_types;
    *regions = NULL;
    *region_count = 0;
    return NULL;
}

void behavior_expression_wasm_free(behavior_expression_wasm *module)
{
    free(module);
}

int behavior_expression_wasm_count(const behavior_expression_wasm *module)
{
    (void)module;
    return 0;
}

int behavior_expression_wasm_registry_size()
{
    return 0;
}

bool behavior_expression_wasm_run(
    const behavior_expression_wasm *module,
    int block,
    const behavior_instance *instance,
    pin_value *out
)
{
    (void)module;
    (void)block;
    (void)instance;
    (void)out;
    return false;
}

bool behavior_expression_wasm_run_slots(
    const behavior_expression_wasm *module,
    int function,
    const pin_value *slots,
    pin_value *out
)
{
    (void)module;
    (void)function;
    (void)slots;
    (void)out;
    return false;
}

#endif

void behavior_expression_wasm_get_stats(behavior_expression_wasm_stats *out)
{
    *out = (behavior_expression_wasm_stats){
        .modules_requested = atomic_load_explicit(&requested_modules, memory_order_relaxed),
        .expressions_executed = atomic_load_explicit(&executed_expressions, memory_order_relaxed),
        .bytecode_fallbacks = atomic_load_explicit(&bytecode_fallbacks, memory_order_relaxed),
        .compile_failures = atomic_load_explicit(&compile_failures, memory_order_relaxed),
        .modules_released = atomic_load_explicit(&released_modules, memory_order_relaxed),
        .expressions_eligible = atomic_load_explicit(&eligible_expressions, memory_order_relaxed),
    };
}
