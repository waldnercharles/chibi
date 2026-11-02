#include "authoring/asset_compiler_wasm.h"

#include <assert.h>
#include <limits.h>
#include <setjmp.h>
#include <stdalign.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The compiler's internal context ABI, native_abi.ContextType/TextType/ValueType.
 * Its allocator always receives i64, independently of C's size_t. */
typedef struct wasm_context wasm_context;
typedef struct
{
    const char *bytes;
    uint32_t length;
    void *owner;
} wasm_text;
typedef struct
{
    uint32_t type, length;
    uint64_t bits;
    const char *bytes;
} wasm_value;
struct wasm_context
{
    void (*format)(wasm_context *, const wasm_value *, int32_t, wasm_text *);
    const void *failure;
    void *(*allocate)(wasm_context *, uint64_t);
    void (*index_failed)(wasm_context *, int32_t, int32_t);
    void (*release)(wasm_context *, void *);
};
_Static_assert(sizeof(void *) == 4 && sizeof(wasm_text) == 12, "Wasm32 text ABI");
_Static_assert(sizeof(wasm_value) == 24 && sizeof(wasm_context) == 20, "Wasm32 context ABI");

typedef struct allocation allocation;
struct allocation
{
    allocation *previous, *next;
    max_align_t alignment;
    unsigned char bytes[];
};
typedef struct
{
    wasm_context context;
    jmp_buf stopped;
    allocation *allocations;
    const char *request;
    size_t request_length;
    const asset_compiler_options *options;
    asset_compiler_diagnostic *diagnostic;
    uint64_t remaining;
    bool bounded;
    unsigned char *output;
    size_t output_length;
} compile;

/* A process hook, one synchronous compiler per browser thread. The context
 * and longjmp destination live on the heap, so setjmp cannot invalidate them. */
static _Thread_local compile *active;
#ifdef CHIBI_ASSET_COMPILER_WASM_TEST
static _Thread_local size_t live_allocations;
size_t asset_compiler_wasm_test_allocations(void)
{
    return live_allocations;
}
#endif
extern int32_t chibi_scalar(wasm_context *);
extern void chibi_format_text(wasm_context *, const wasm_value *, int32_t, wasm_text *);

static void fail(asset_compiler_diagnostic *diagnostic, const char *message)
{
    snprintf(diagnostic->message, sizeof diagnostic->message, "%s", message);
}
/* Keep longjmp in C functions that carry Emscripten's EH attributes. Joint LTO
 * must not move it into authored LLVM functions with no EH attribute. */
__attribute__((noinline)) _Noreturn void chibi_native_failure(int32_t kind)
{
    assert(active);
    if (kind == 5) {
        fail(active->diagnostic, "The package compile exceeded its step budget");
    } else {
        fail(active->diagnostic, "The native package compiler failed");
    }
    longjmp(active->stopped, kind == 5 ? 125 : 134);
}
__attribute__((noinline)) void chibi_native_step(void)
{
    assert(active);
    if (active->options && active->options->cancelled &&
        active->options->cancelled(active->options->user)) {
        fail(active->diagnostic, "The package compile was cancelled");
        longjmp(active->stopped, 126);
    }
    if (active->bounded) {
        if (!active->remaining) {
            chibi_native_failure(5);
        }
        active->remaining--;
    }
}
int32_t chibi_native_scope_begin(uint64_t steps)
{
    assert(active);
    active->remaining = steps;
    active->bounded = true;
    return 0;
}
int32_t chibi_native_scope_end(void)
{
    assert(active);
    active->bounded = false;
    return 0;
}
void *chibi_allocate(wasm_context *context, uint64_t size)
{
    compile *call = (compile *)context;
    if (size > SIZE_MAX - sizeof(allocation)) {
        abort();
    }
    allocation *item = calloc(1, sizeof *item + (size_t)size);
    if (!item) {
        abort();
    }
    item->next = call->allocations;
    if (item->next) {
        item->next->previous = item;
    }
    call->allocations = item;
#ifdef CHIBI_ASSET_COMPILER_WASM_TEST
    live_allocations++;
#endif
    return item->bytes;
}
static void release(wasm_context *context, void *bytes)
{
    if (!bytes) {
        return;
    }
    compile *call = (compile *)context;
    allocation *item = (allocation *)((unsigned char *)bytes -
                                      offsetof(allocation, bytes));
    if (item->previous) {
        item->previous->next = item->next;
    } else {
        call->allocations = item->next;
    }
    if (item->next) {
        item->next->previous = item->previous;
    }
#ifdef CHIBI_ASSET_COMPILER_WASM_TEST
    assert(live_allocations);
    live_allocations--;
#endif
    free(item);
}
static _Noreturn void index_failed(wasm_context *context, int32_t index, int32_t length)
{
    (void)context;
    (void)index;
    (void)length;
    chibi_native_failure(1);
}
int32_t chibi_asset_request_length(void)
{
    assert(active && active->request_length <= INT_MAX);
    return (int32_t)active->request_length;
}
int32_t chibi_asset_request_copy(unsigned char *buffer, int32_t count)
{
    assert(active && count >= 0 && (size_t)count == active->request_length);
    if (count) {
        memcpy(buffer, active->request, (size_t)count);
    }
    return 0;
}
uint64_t chibi_asset_budget(void)
{
    assert(active);
    return active->options && active->options->compile_budget
             ? active->options->compile_budget
             : UINT64_C(4000000000);
}
int32_t chibi_asset_publish(const unsigned char *bytes, int32_t count)
{
    assert(active && count >= 0 && !active->output);
    if ((uint32_t)count > ASSET_COMPILER_MAX_OUTPUT_BYTES) {
        fail(active->diagnostic, "The package compiler output exceeds its byte limit");
        return 2;
    }
    active->output = malloc((size_t)count ? (size_t)count : 1);
    if (!active->output) {
        abort();
    }
    if (count) {
        memcpy(active->output, bytes, (size_t)count);
    }
    active->output_length = (size_t)count;
    return 0;
}
int32_t chibi_asset_rejected(int32_t document, int32_t line, int32_t column, const char *message)
{
    assert(active);
    active->diagnostic->document = document;
    active->diagnostic->line = line;
    active->diagnostic->column = column;
    fail(active->diagnostic, message);
    return 0;
}
unsigned char *asset_compiler_wasm_compile(
    const char *request,
    size_t request_length,
    size_t *length,
    const asset_compiler_options *options,
    asset_compiler_diagnostic *diagnostic
)
{
    *length = 0;
    *diagnostic = (asset_compiler_diagnostic){ .document = -1 };
    if (!request || request_length > ASSET_COMPILER_MAX_REQUEST_BYTES) {
        fail(diagnostic, request ? "The package compiler request exceeds its byte limit" : "The package compiler request is missing");
        return NULL;
    }
    if (active) {
        fail(diagnostic, "The package compiler is already running on this thread");
        return NULL;
    }
    compile *call = calloc(1, sizeof *call);
    if (!call) {
        abort();
    }
    call->context = (wasm_context){ .format = chibi_format_text,
                                    .allocate = chibi_allocate,
                                    .index_failed = index_failed,
                                    .release = release };
    call->request = request;
    call->request_length = request_length;
    call->options = options;
    call->diagnostic = diagnostic;
    active = call;
    int status = setjmp(call->stopped);
    if (!status) {
        chibi_native_step();
        status = chibi_scalar(&call->context);
        if (call->context.failure) {
            status = 134;
        }
    }
    while (call->allocations) {
        release(&call->context, call->allocations->bytes);
    }
    unsigned char *output = call->output;
    if (status) {
        free(output);
        output = NULL;
        if (!diagnostic->message[0]) {
            fail(diagnostic, "The package compiler rejected its request");
        }
    } else {
        *length = call->output_length;
    }
    active = NULL;
    free(call);
    return output;
}
