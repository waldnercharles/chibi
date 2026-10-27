#ifndef CHIBI_EXECUTION_NATIVE_H
#define CHIBI_EXECUTION_NATIVE_H
#include "execution/host.h"
#include "execution/module.h"
#include "support/arena.h"

/* Calls from the VM into natively compiled library functions. A native
 * library describes each function it exports in a manifest, and binding it to
 * the library's module lets a program's synchronous calls of those functions
 * run the native entry instead of a VM frame. Tasks, inspected calls and
 * every unbound function stay in the VM. */
#define EXECUTION_NATIVE_ABI 2u
/* The most parameters and results an entry takes: native_abi.ExportParameters,
 * and one result. The emitter exports no function with more. */
#define EXECUTION_NATIVE_MAX_PARAMETERS 8
#define EXECUTION_NATIVE_MAX_RESULTS 1
/* An export's parameter and result types. */
typedef enum
{
    EXECUTION_NATIVE_INT = 1,
    EXECUTION_NATIVE_BOOL,
    EXECUTION_NATIVE_I8,
    EXECUTION_NATIVE_U8,
    EXECUTION_NATIVE_I16,
    EXECUTION_NATIVE_U16,
    EXECUTION_NATIVE_U32,
    EXECUTION_NATIVE_I64,
    EXECUTION_NATIVE_U64,
    EXECUTION_NATIVE_NUMBER,
    EXECUTION_NATIVE_F64,
    /* Text, only as a value native code passes the runtime. */
    EXECUTION_NATIVE_TEXT,
} execution_native_type;
/* A value native code passes the runtime: a scalar as its type and the word
 * an entry would read, or text as its bytes and length, borrowed for the
 * call. native_abi.ValueType() is its LLVM type. */
typedef struct
{
    uint32_t type, length;
    uint64_t bits;
    const char *bytes;
} execution_native_value;
/* The hidden first parameter of every function of a library that calls the
 * runtime, as Odin's procedures take their context: the runtime's operations,
 * which native code calls through it, so a library leaves no symbol for its
 * host to define. native_abi.ContextType() is its LLVM type. */
typedef struct execution_native_context execution_native_context;
struct execution_native_context
{
    /* The text `format_text` gives `values`, the format and then its
     * arguments, as the VM formats them. It is valid until the call from the
     * VM returns. */
    void (*format_text)(
        execution_native_context *,
        const execution_native_value *values,
        int32_t count,
        pin_string *result
    );
};
/* An entry reads its arguments and writes its results as 64-bit words in
 * declaration order: an integer sign- or zero-extended from its width, a bool
 * as 0 or 1, and a number or f64 as its encoding's bits, a number's
 * zero-extended. It never suspends. */
typedef void (*execution_native_entry)(
    execution_native_context *,
    const uint64_t *arguments,
    uint64_t *results
);
/* `id` is the function's stable ID, which its prepared module records:
 * script_catalog_id("symbol", "<library path>:1:<name>:callable"). `name`,
 * which is required, only names it in diagnostics. */
typedef struct
{
    const char *name;
    uint64_t id;
    /* The parameters' types, then the results'. */
    const uint8_t *types;
    int32_t parameter_count, result_count;
    execution_native_entry entry;
} execution_native_export;
/* One library's exports. */
typedef struct
{
    uint32_t abi;
    int32_t count;
    const execution_native_export *exports;
} execution_native_manifest;

/* The VM's operations behind a context, which the host supplies: the
 * runtime defines them, and execution does not depend on it. */
typedef struct
{
    node_value_call_fn format_text;
} execution_native_runtime;
/* One call's context and the heap that holds what its operations return,
 * which free releases. */
typedef struct
{
    execution_native_context context;
    execution_native_runtime runtime;
    arena heap;
} execution_native_frame;
void execution_native_frame_init(execution_native_frame *, execution_native_runtime);
void execution_native_frame_free(execution_native_frame *);

typedef struct execution_natives execution_natives;
execution_natives *execution_natives_new(execution_native_runtime);
void execution_natives_free(execution_natives *);
/* Binds each export of `manifest` to the pure authored function of `library`
 * with its ID and exactly its scalar signature. Any export without such a
 * function, or a manifest of another ABI, fails with the reason and binds
 * nothing. Only calls of that module's functions run natively, so a host
 * binds the modules its programs run. The module and manifest are borrowed
 * for the table's lifetime. */
bool execution_natives_bind(
    execution_natives *,
    const execution_module *library,
    const execution_native_manifest *manifest,
    char *error,
    size_t size
);
/* The export bound to `function`, or NULL. */
const execution_native_export *execution_natives_find(
    const execution_natives *,
    const behavior_function *function
);
/* Runs `native`, which is bound to `function` in `natives`, on a call's
 * `count` arguments, filling omitted parameters with their defaults as a VM
 * frame does, and writes the first `result_count` of its results as the
 * function's declared result values. What the runtime allocates for the call
 * is freed when it returns. */
void execution_native_call(
    const execution_natives *natives,
    const execution_native_export *native,
    const behavior_function *function,
    const pin_value *arguments,
    int count,
    pin_value *results,
    int result_count
);
/* Every later activation of `program` and the programs it imports calls the
 * table's bound entries. The table outlives the program. Set it before any
 * instance is attached. A program that replaces it starts without one. */
void execution_program_set_natives(execution_program *, const execution_natives *);
#endif
