#include "graph/compiler/behavior_program.h"
#include "graph/runtime/update_internal.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdlib.h>

/* Bound both schedule search work and code growth. Longer/nonrepeating
 * stretches still execute their prepared compiled bodies in order. */
enum
{
    SYSTEM_MAX_PERIOD = 8,
    SYSTEM_REBUILD_LOOKBACK = SYSTEM_MAX_PERIOD * 2
};
typedef struct
{
    const behavior_function *function;
    uint64_t asset, revision;
} behavior_update_key;
struct behavior_update_kernel
{
    behavior_update_key keys[SYSTEM_MAX_PERIOD];
    int count;
    void *code;
    bool used;
};
static behavior_update_key key(const behavior_update_row *row)
{
    return (behavior_update_key){ row->binding->function,
                                  row->binding->asset->identity,
                                  behavior_program_revision(row->binding->asset->program) };
}
static bool eligible(const behavior_update_row *row)
{
    return row->binding && row->binding->system_code &&
           row->instance->backend == BEHAVIOR_SLJIT;
}
static bool same(const behavior_update_row *a, const behavior_update_row *b)
{
    return eligible(b) && a->binding->function == b->binding->function;
}
static void *kernel(behavior_update_kernel ***kernels, const behavior_update_row *rows, int count)
{
    behavior_update_key keys[SYSTEM_MAX_PERIOD];
    const behavior_function *functions[SYSTEM_MAX_PERIOD];
    for (int i = 0; i < count; i++) {
        keys[i] = key(&rows[i]);
        functions[i] = keys[i].function;
    }
    for (int i = 0; i < acount(*kernels); i++) {
        behavior_update_kernel *k = (*kernels)[i];
        if (k->count != count) {
            continue;
        }
        int j = 0;
        while (j < count && k->keys[j].function == keys[j].function &&
               k->keys[j].asset == keys[j].asset &&
               k->keys[j].revision == keys[j].revision) {
            j++;
        }
        if (j == count) {
            k->used = true;
            return k->code;
        }
    }
    behavior_update_kernel *k = xcalloc(1, sizeof *k);
    k->count = count;
    for (int i = 0; i < count; i++) {
        k->keys[i] = keys[i];
    }
    k->code = behavior_function_compile_sequence(functions, count);
    k->used = true;
    apush(*kernels, k);
    return k->code;
}
void behavior_update_system_spans(
    const behavior_update_row *rows,
    int count,
    behavior_update_span **spans,
    behavior_update_kernel ***kernels
)
{
    behavior_update_system_spans_incremental(rows, count, 0, spans, kernels);
}

static int rebuild_begin(const behavior_update_span *spans, int span_count, int dirty_row)
{
    int begin = dirty_row - SYSTEM_REBUILD_LOOKBACK;
    if (begin < 0) {
        begin = 0;
    }
    for (int i = 0; i < span_count; i++) {
        if (spans[i].end <= begin) {
            continue;
        }
        begin = spans[i].begin;
        break;
    }
    return begin;
}

static void mark_kernel_used(behavior_update_kernel **kernels, void *code)
{
    for (int i = 0; i < acount(kernels); i++) {
        if (kernels[i]->code == code) {
            kernels[i]->used = true;
            return;
        }
    }
}

int behavior_update_system_spans_incremental(
    const behavior_update_row *rows,
    int count,
    int dirty_row,
    behavior_update_span **spans,
    behavior_update_kernel ***kernels
)
{
    assert(dirty_row >= 0 && dirty_row <= count);
    int old_span_count = acount(*spans);
    int begin = rebuild_begin(*spans, old_span_count, dirty_row);
    int retained = 0;
    while (retained < old_span_count && (*spans)[retained].end <= begin) {
        retained++;
    }
    for (int i = 0; i < acount(*kernels); i++) {
        (*kernels)[i]->used = false;
    }
    for (int i = 0; i < retained; i++) {
        mark_kernel_used(*kernels, (*spans)[i].code);
    }
    asetlen(*spans, retained);
    for (int at = begin; at < count;) {
        if (!eligible(&rows[at])) {
            at++;
            continue;
        }
        int best = at + 1, period = 0;
        for (int p = 1; p <= SYSTEM_MAX_PERIOD && p <= (count - at) / 2; p++) {
            int end = at + p;
            if (!same(&rows[at], &rows[end])) {
                continue;
            }
            int j = 0;
            while (j < p && eligible(&rows[at + j]) &&
                   same(&rows[at + j], &rows[end + j])) {
                j++;
            }
            if (j != p) {
                continue;
            }
            end += p;
            while (end < count && same(&rows[at + (end - at) % p], &rows[end])) {
                end++;
            }
            best = end;
            period = p;
            break;
        }
        if (period) {
            void *code = period == 1 ? rows[at].binding->system_code
                                     : kernel(kernels, rows + at, period);
            if (code) {
                apush(*spans, ((behavior_update_span){ at, best, period, code }));
            }
        }
        at = best;
    }
    for (int i = acount(*kernels) - 1; i >= 0; i--) {
        behavior_update_kernel *k = (*kernels)[i];
        if (k->used) {
            continue;
        }
        behavior_update_free_code(k->code);
        free(k);
        adel(*kernels, i);
    }
    return retained;
}
void behavior_update_free_kernels(behavior_update_kernel **kernels)
{
    for (int i = 0; i < acount(kernels); i++) {
        behavior_update_free_code(kernels[i]->code);
        free(kernels[i]);
    }
    afree(kernels);
}
