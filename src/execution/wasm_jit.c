#include "execution/wasm_jit.h"

#include "execution/definition.h"
#include "execution/machine.h"
#include "execution/runner.h"
#include "execution/status.h"
#include "execution/wasm_emit.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <limits.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

#define ERROR_SIZE 256

struct execution_wasm_jit
{
    unsigned char *bytes;
    size_t size;
    uint64_t id;
    atomic_uint references;
    atomic_bool retired;
    _Atomic(struct execution_wasm_jit *) inspection;
};

static atomic_uint_fast64_t compilations;
static atomic_uint_fast64_t executions;
static atomic_uint_fast64_t compile_failures;
static atomic_uint_fast64_t live_entries;
static _Thread_local char last_error[ERROR_SIZE];

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

EM_JS_DEPS(wasm_jit, "$wasmMemory,$wasmTable,$addFunction,$removeFunction,$setWasmTableEntry,$stringToUTF8");

static atomic_uint_fast64_t next_id = 1;
static atomic_uint_fast64_t retired_epoch = 1;

typedef int (*wasm_entry)(execution_machine *);
typedef struct
{
    execution_wasm_jit *jit;
    wasm_entry entry;
} cached_entry;

/* Table entries belong to this JS realm. Never publish their indices in shared
 * artifact storage: a worker installs its own instance and table entry. */
static _Thread_local cached_entry *entries;
static _Thread_local uint64_t cache_epoch;

// clang-format off
EM_JS(int, install_entry, (const unsigned char *bytes, size_t size,
                          uintptr_t step, char *error, size_t error_size), {
    try {
        const copy = HEAPU8.slice(bytes >>> 0, (bytes >>> 0) + size);
        const module = new WebAssembly.Module(copy);
        const instance = new WebAssembly.Instance(module, {
            env: { memory: wasmMemory, step: wasmTable.get(step) }
        });
        if (typeof instance.exports.run !== 'function') {
            throw new Error('Generated Wasm module has no run entry');
        }
        return addFunction(instance.exports.run, 'ii');
    } catch (failure) {
        stringToUTF8(String(failure), error, error_size);
        return 0;
    }
})

EM_JS(void, remove_entry, (uintptr_t entry), {
    removeFunction(entry);
    setWasmTableEntry(entry, null);
})
// clang-format on

static void retain(execution_wasm_jit *jit)
{
    unsigned before = atomic_fetch_add_explicit(&jit->references, 1, memory_order_relaxed);
    assert(before && before < UINT_MAX);
    (void)before;
}

static void release(execution_wasm_jit *jit)
{
    unsigned before = atomic_fetch_sub_explicit(&jit->references, 1, memory_order_acq_rel);
    assert(before);
    if (before != 1) {
        return;
    }
    free(jit->bytes);
    free(jit);
}

static void drop_entry(cached_entry entry)
{
    remove_entry((uintptr_t)entry.entry);
    atomic_fetch_sub_explicit(&live_entries, 1, memory_order_relaxed);
    release(entry.jit);
}

static void collect_entries()
{
    uint64_t epoch = atomic_load_explicit(&retired_epoch, memory_order_acquire);
    if (cache_epoch == epoch) {
        return;
    }
    int kept = 0;
    for (int i = 0; i < acount(entries); i++) {
        if (atomic_load_explicit(&entries[i].jit->retired, memory_order_acquire)) {
            drop_entry(entries[i]);
        } else {
            entries[kept++] = entries[i];
        }
    }
    asetlen(entries, kept);
    if (!kept) {
        afree(entries);
    }
    cache_epoch = epoch;
}

#ifdef __EMSCRIPTEN_PTHREADS__
#include <pthread.h>

static pthread_once_t cleanup_once = PTHREAD_ONCE_INIT;
static pthread_key_t cleanup_key;
static _Thread_local bool cleanup_registered;

static void thread_cache_free(void *)
{
    for (int i = 0; i < acount(entries); i++) {
        drop_entry(entries[i]);
    }
    afree(entries);
    cache_epoch = 0;
    cleanup_registered = false;
}

static void create_cleanup_key()
{
    int status = pthread_key_create(&cleanup_key, thread_cache_free);
    assert(status == 0);
    (void)status;
}

static void register_cleanup()
{
    if (cleanup_registered) {
        return;
    }
    int status = pthread_once(&cleanup_once, create_cleanup_key);
    assert(status == 0);
    (void)status;
    status = pthread_setspecific(cleanup_key, (void *)1);
    assert(status == 0);
    (void)status;
    cleanup_registered = true;
}
#endif

static wasm_entry entry_for(execution_wasm_jit *jit)
{
    assert(!atomic_load_explicit(&jit->retired, memory_order_acquire));
    collect_entries();
    int low = 0, high = acount(entries);
    while (low < high) {
        int middle = low + (high - low) / 2;
        if (entries[middle].jit->id < jit->id) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    if (low < acount(entries) && entries[low].jit->id == jit->id) {
        return entries[low].entry;
    }
    last_error[0] = '\0';
    int index = install_entry(jit->bytes, jit->size, (uintptr_t)execution_machine_step, last_error, sizeof last_error);
    if (!index) {
        atomic_fetch_add_explicit(&compile_failures, 1, memory_order_relaxed);
        return NULL;
    }
#ifdef __EMSCRIPTEN_PTHREADS__
    register_cleanup();
#endif
    wasm_entry entry = (wasm_entry)(uintptr_t)index;
    retain(jit);
    apush(entries, ((cached_entry){ 0 }));
    for (int i = acount(entries) - 1; i > low; i--) {
        entries[i] = entries[i - 1];
    }
    entries[low] = (cached_entry){ .jit = jit, .entry = entry };
    atomic_fetch_add_explicit(&compilations, 1, memory_order_relaxed);
    atomic_fetch_add_explicit(&live_entries, 1, memory_order_relaxed);
    return entry;
}

static uint64_t allocate_id()
{
    uint_fast64_t id = atomic_load_explicit(&next_id, memory_order_relaxed);
    for (;;) {
        if (id == UINT64_MAX) {
            snprintf(last_error, sizeof last_error, "Wasm module identity exhausted");
            return 0;
        }
        if (atomic_compare_exchange_weak_explicit(&next_id, &id, id + 1, memory_order_relaxed, memory_order_relaxed)) {
            return (uint64_t)id;
        }
    }
}

static execution_wasm_jit *compile_entry(const behavior_function *f, bool inspected)
{
    execution_wasm_jit *jit = xcalloc(1, sizeof *jit);
    atomic_init(&jit->references, 1);
    atomic_init(&jit->retired, false);
    atomic_init(&jit->inspection, NULL);
    jit->id = allocate_id();
    if (!jit->id ||
        !execution_wasm_emit(f, inspected, &jit->bytes, &jit->size, last_error, sizeof last_error)) {
        atomic_fetch_add_explicit(&compile_failures, 1, memory_order_relaxed);
        release(jit);
        return NULL;
    }
    return jit;
}

static execution_wasm_jit *inspection_for(const behavior_function *f)
{
    execution_wasm_jit *normal = f->artifact->wasm_jit;
    execution_wasm_jit *inspection = atomic_load_explicit(&normal->inspection, memory_order_acquire);
    if (inspection) {
        return inspection;
    }
    execution_wasm_jit *candidate = compile_entry(f, true);
    if (!candidate) {
        return NULL;
    }
    if (atomic_compare_exchange_strong_explicit(
            &normal->inspection,
            &inspection,
            candidate,
            memory_order_acq_rel,
            memory_order_acquire
        )) {
        return candidate;
    }
    /* The losing bytes were never installed in a thread's table. */
    release(candidate);
    return inspection;
}
#endif

bool behavior_function_compile_wasm(behavior_function *f)
{
    assert(f && f->artifact);
    if (f->artifact->wasm_jit) {
        return true;
    }
    last_error[0] = '\0';
#ifdef __EMSCRIPTEN__
    execution_wasm_jit *jit = compile_entry(f, false);
    if (!jit) {
        return false;
    }
    if (!entry_for(jit)) {
        release(jit);
        return false;
    }
    f->artifact->wasm_jit = jit;
    return true;
#else
    snprintf(last_error, sizeof last_error, "Wasm JIT requires the browser runtime");
    return false;
#endif
}

int behavior_function_run_wasm(const behavior_function *f, behavior_function_context *ctx)
{
    assert(f && f->artifact && ctx);
#ifdef __EMSCRIPTEN__
    if (!f->artifact->wasm_jit) {
        snprintf(last_error, sizeof last_error, "Wasm function was not compiled");
        ctx->error_message = last_error;
        return FLOW_ERR_COMPILE;
    }
    bool inspected = behavior_function_inspection_required(f, ctx);
    execution_wasm_jit *jit = inspected ? inspection_for(f) : f->artifact->wasm_jit;
    wasm_entry entry = jit ? entry_for(jit) : NULL;
    if (!entry) {
        ctx->error_message = last_error;
        return FLOW_ERR_COMPILE;
    }
    execution_machine machine;
    execution_machine_begin(&machine, f, ctx, false);
    /* Both emitted entries keep canonical PCs, including continuation leaders.
     * Helpers read exactly the immutable stream used to emit this entry. */
    if (!inspected && f->runtime_body) {
        machine.instructions = f->runtime_body;
    }
    atomic_fetch_add_explicit(&executions, 1, memory_order_relaxed);
    behavior_run_record(inspected ? EXECUTION_RUN_INSPECTED_WASM : EXECUTION_RUN_WASM);
    int result = entry(&machine);
    execution_machine_end(&machine, result);
    return result;
#else
    (void)f;
    snprintf(last_error, sizeof last_error, "Wasm JIT requires the browser runtime");
    ctx->error_message = last_error;
    return FLOW_ERR_COMPILE;
#endif
}

void execution_wasm_jit_free(execution_wasm_jit *jit)
{
    if (!jit) {
        return;
    }
#ifdef __EMSCRIPTEN__
    execution_wasm_jit_free(
        atomic_exchange_explicit(&jit->inspection, NULL, memory_order_acq_rel)
    );
    /* Only retired generations are collected. Idle threads retain their cache
     * references until the next call or their pthread cleanup destructor. */
    atomic_store_explicit(&jit->retired, true, memory_order_release);
    atomic_fetch_add_explicit(&retired_epoch, 1, memory_order_release);
    collect_entries();
    release(jit);
#else
    free(jit->bytes);
    free(jit);
#endif
}

void execution_wasm_jit_get_stats(execution_wasm_jit_stats *stats)
{
    assert(stats);
    *stats = (execution_wasm_jit_stats){
        .compilations = atomic_load_explicit(&compilations, memory_order_relaxed),
        .executions = atomic_load_explicit(&executions, memory_order_relaxed),
        .compile_failures = atomic_load_explicit(&compile_failures, memory_order_relaxed),
        .live_entries = atomic_load_explicit(&live_entries, memory_order_relaxed),
    };
}

const char *execution_wasm_jit_error()
{
    return last_error;
}
