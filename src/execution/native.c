#include "execution/native.h"

#include "execution/definition.h"
#include "execution/function_builder.h"
#include "execution/scalar.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "types/text.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* native_abi.ValueType() and ContextType() spell these layouts. */
_Static_assert(offsetof(execution_native_value, type) == 0, "value type");
_Static_assert(offsetof(execution_native_value, length) == 4, "value length");
_Static_assert(offsetof(execution_native_value, bits) == 8, "value bits");
_Static_assert(offsetof(execution_native_value, bytes) == 16, "value bytes");
_Static_assert(sizeof(execution_native_value) == 24, "value size");
_Static_assert(offsetof(execution_native_context, format_text) == 0, "context format_text");
_Static_assert(offsetof(execution_native_context, failure) == sizeof(void *), "context failure");
_Static_assert(sizeof(execution_native_context) == 2 * sizeof(void *), "context size");
/* And FailureType() these. */
_Static_assert(offsetof(execution_native_failure, function) == 0, "failure function");
_Static_assert(offsetof(execution_native_failure, start) == 8, "failure start");
_Static_assert(offsetof(execution_native_failure, end) == 12, "failure end");
_Static_assert(sizeof(execution_native_failure) == 16, "failure size");

typedef struct
{
    const behavior_function *function;
    const execution_native_export *native;
    const execution_module *library;
} native_binding;
/* Bindings sorted by function address. */
struct execution_natives
{
    native_binding *bindings;
    execution_native_runtime runtime;
};

execution_natives *execution_natives_new(execution_native_runtime runtime)
{
    assert(runtime.format_text);
    execution_natives *natives = xcalloc(1, sizeof(execution_natives));
    natives->runtime = runtime;
    return natives;
}
void execution_natives_free(execution_natives *natives)
{
    if (!natives) {
        return;
    }
    afree(natives->bindings);
    free(natives);
}

static pin_type pin_of(int type)
{
    static const pin_type PINS[] = {
        [EXECUTION_NATIVE_INT] = PIN_I32, [EXECUTION_NATIVE_BOOL] = PIN_BOOL,
        [EXECUTION_NATIVE_I8] = PIN_I8,   [EXECUTION_NATIVE_U8] = PIN_U8,
        [EXECUTION_NATIVE_I16] = PIN_I16, [EXECUTION_NATIVE_U16] = PIN_U16,
        [EXECUTION_NATIVE_U32] = PIN_U32, [EXECUTION_NATIVE_I64] = PIN_I64,
        [EXECUTION_NATIVE_U64] = PIN_U64, [EXECUTION_NATIVE_NUMBER] = PIN_F32,
        [EXECUTION_NATIVE_F64] = PIN_F64,
    };
    return type >= EXECUTION_NATIVE_INT && type <= EXECUTION_NATIVE_F64
             ? PINS[type]
             : PIN_FLOW;
}
static bool scalar(pin_value value, int type)
{
    return value.type == pin_of(type) && value.container == PIN_CONTAINER_SINGLE &&
           !value.type_name && !value.record;
}
/* Whether `native` has exactly the signature of `function`. */
static bool same_signature(const execution_native_export *native, const behavior_function *function)
{
    int parameters, results;
    const pin_value *arguments = behavior_function_arguments(function, &parameters);
    const pin_value *defaults = behavior_function_results(function, &results);
    if (native->parameter_count != parameters || native->result_count != results) {
        return false;
    }
    for (int p = 0; p < parameters; p++) {
        if (function->argument_kinds[p] != BEHAVIOR_VALUE ||
            !scalar(arguments[p], native->types[p])) {
            return false;
        }
    }
    for (int r = 0; r < results; r++) {
        if (function->result_kinds[r] != BEHAVIOR_VALUE ||
            !scalar(defaults[r], native->types[parameters + r])) {
            return false;
        }
    }
    return true;
}
/* Why `native` cannot bind `function`, or NULL. */
static const char *mismatch(const execution_native_export *native, const behavior_function *function)
{
    if (!function || !same_signature(native, function)) {
        return "has no pure function of its signature in the library";
    }
    if (native->parameter_count > EXECUTION_NATIVE_MAX_PARAMETERS ||
        native->result_count > EXECUTION_NATIVE_MAX_RESULTS) {
        return "has more parameters or results than a native entry takes";
    }
    return NULL;
}
/* The pure authored function of `library` with ID `id`, or NULL. */
static const behavior_function *library_function(const execution_module *library, uint64_t id)
{
    for (int i = 0; i < execution_module_function_count(library); i++) {
        execution_module_function_info info = execution_module_function_at(library, i);
        if (info.kind == EXECUTION_FUNCTION_AUTHORED && info.id == id) {
            return info.effects ? NULL : info.body;
        }
    }
    return NULL;
}
static int binding_at(const execution_natives *natives, const behavior_function *function)
{
    int low = 0, high = acount(natives->bindings);
    while (low < high) {
        int middle = low + (high - low) / 2;
        if ((uintptr_t)natives->bindings[middle].function < (uintptr_t)function) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    return low;
}
bool execution_natives_bind(
    execution_natives *natives,
    const execution_module *library,
    const execution_native_manifest *manifest,
    char *error,
    size_t size
)
{
    if (manifest->abi != EXECUTION_NATIVE_ABI) {
        snprintf(error, size, "native library ABI %u differs from %u", manifest->abi, EXECUTION_NATIVE_ABI);
        return false;
    }
    const behavior_function **functions = NULL;
    for (int i = 0; i < manifest->count; i++) {
        const execution_native_export *native = &manifest->exports[i];
        assert(native->name);
        const behavior_function *function = library_function(library, native->id);
        const char *reason = mismatch(native, function);
        for (int j = 0; !reason && j < i; j++) {
            reason = functions[j] == function ? "is exported twice" : NULL;
        }
        if (!reason && execution_natives_find(natives, function)) {
            reason = "is already bound";
        }
        if (reason) {
            snprintf(error, size, "native export %s %s", native->name, reason);
            afree(functions);
            return false;
        }
        apush(functions, function);
    }
    for (int i = 0; i < manifest->count; i++) {
        int at = binding_at(natives, functions[i]);
        native_binding binding = { functions[i], &manifest->exports[i], library };
        apush(natives->bindings, binding);
        memmove(
            &natives->bindings[at + 1],
            &natives->bindings[at],
            (size_t)(acount(natives->bindings) - 1 - at) * sizeof *natives->bindings
        );
        natives->bindings[at] = binding;
    }
    afree(functions);
    return true;
}
const execution_native_export *execution_natives_find(
    const execution_natives *natives,
    const behavior_function *function
)
{
    int at = binding_at(natives, function);
    return at < acount(natives->bindings) && natives->bindings[at].function == function
             ? natives->bindings[at].native
             : NULL;
}

static uint64_t word(pin_value value)
{
    uint64_t bits = 0;
    switch (value.type) {
        case PIN_F32: {
            uint32_t narrow;
            memcpy(&narrow, &value.f, sizeof narrow);
            return narrow;
        }
        case PIN_F64: memcpy(&bits, &value.d, sizeof bits); return bits;
        default: return behavior_scalar_bits(value);
    }
}
static pin_value from_word(pin_type type, uint64_t bits)
{
    pin_value value = { .type = type };
    switch (type) {
        case PIN_BOOL: value.b = bits != 0; return value;
        case PIN_F32: {
            uint32_t narrow = (uint32_t)bits;
            memcpy(&value.f, &narrow, sizeof narrow);
            return value;
        }
        case PIN_F64: memcpy(&value.d, &bits, sizeof bits); return value;
        default: return behavior_scalar_from_bits(type, bits);
    }
}
/* A runtime value as the pin_value the VM would pass, its text in `text`. */
static pin_value runtime_value(const execution_native_value *value, pin_string *text)
{
    if (value->type == EXECUTION_NATIVE_TEXT) {
        *text = (pin_string){ value->bytes, value->length };
        return (pin_value){ .type = PIN_STR, .text = text };
    }
    pin_type type = pin_of((int)value->type);
    assert(type != PIN_FLOW);
    return from_word(type, value->bits);
}
/* Native code reports only failed asserts so far, so a failure to format
 * ends the program with its message, as an index out of range does. */
static void format_text(
    execution_native_context *context,
    const execution_native_value *values,
    int32_t count,
    pin_string *result
)
{
    assert(count >= 1);
    execution_native_frame *frame = (execution_native_frame *)context;
    pin_value *inputs = arena_alloc(&frame->heap, sizeof *inputs * (size_t)count);
    pin_string *texts = arena_alloc(&frame->heap, sizeof *texts * (size_t)count);
    for (int i = 0; i < count; i++) {
        inputs[i] = runtime_value(&values[i], &texts[i]);
    }
    pin_value text;
    const char *error = frame->runtime.format_text(&frame->heap, inputs, count, &text);
    if (error) {
        fprintf(stderr, "native format_text: %s\n", error);
        abort();
    }
    *result = pin_text(text);
}
void execution_native_frame_init(execution_native_frame *frame, execution_native_runtime runtime)
{
    *frame = (execution_native_frame){ .context = { format_text }, .runtime = runtime };
}
void execution_native_frame_free(execution_native_frame *frame)
{
    arena_free(&frame->heap);
}
/* The VM's assert whose call `failure` names in `library`. Native code names
 * only asserts of the functions it was compiled from. */
static execution_native_assert assert_at(const execution_module *library, const execution_native_failure *failure)
{
    const behavior_function *function = library_function(library, failure->function);
    assert(function);
    for (int pc = 0; pc < acount(function->instructions); pc++) {
        const behavior_function_source *source = behavior_function_instruction_source(function, pc);
        if (function->instructions[pc].op == BF_ASSERT && source &&
            source->start == failure->start && source->end == failure->end) {
            return (execution_native_assert){ function, pc };
        }
    }
    fprintf(stderr, "native failure names no assert of its function\n");
    abort();
}
bool execution_native_call(
    const execution_natives *natives,
    const execution_native_export *native,
    const behavior_function *function,
    const pin_value *arguments,
    int count,
    pin_value *results,
    int result_count,
    execution_native_assert *failed
)
{
    int parameters, declared_results;
    const pin_value *declared = behavior_function_arguments(function, &parameters);
    const pin_value *defaults = behavior_function_results(function, &declared_results);
    assert(parameters == native->parameter_count && declared_results == native->result_count);
    uint64_t in[EXECUTION_NATIVE_MAX_PARAMETERS] = { 0 },
             out[EXECUTION_NATIVE_MAX_RESULTS] = { 0 };
    for (int p = 0; p < parameters; p++) {
        in[p] = word(behavior_function_argument(arguments, count, p, declared[p]));
    }
    execution_native_frame frame;
    execution_native_frame_init(&frame, natives->runtime);
    native->entry(&frame.context, in, out);
    const execution_native_failure *failure = frame.context.failure;
    execution_native_frame_free(&frame);
    if (failure) {
        *failed = assert_at(natives->bindings[binding_at(natives, function)].library, failure);
        return false;
    }
    for (int r = 0; r < result_count && r < declared_results; r++) {
        results[r] = from_word(defaults[r].type, out[r]);
    }
    return true;
}
