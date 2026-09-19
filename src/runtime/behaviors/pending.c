#include "runtime/behaviors/pending.h"

#include "support/xalloc.h"

#include <assert.h>
#include <stddef.h>

void behavior_pending_reserve(behavior_pending *pending, int attachments)
{
    int count = (attachments + 63) / 64;
    if (count <= pending->word_count) {
        return;
    }
    int capacity = pending->word_count ? pending->word_count * 2 : 4;
    if (capacity < count) {
        capacity = count;
    }
    pending->words = xrealloc(pending->words, (size_t)capacity * sizeof *pending->words);
    for (int i = pending->word_count; i < capacity; i++) {
        atomic_init(&pending->words[i], 0);
    }
    pending->word_count = capacity;
}
void behavior_pending_reset(behavior_pending *pending)
{
    for (int i = 0; i < pending->word_count; i++) {
        atomic_store_explicit(&pending->words[i], 0, memory_order_relaxed);
    }
    atomic_store_explicit(&pending->count, 0, memory_order_relaxed);
}
void behavior_pending_truncate(behavior_pending *pending, int attachments)
{
    assert(attachments >= 0);
    int word = attachments / 64;
    if (word >= pending->word_count) {
        return;
    }
    uint64_t keep = (UINT64_C(1) << (attachments % 64)) - 1;
    uint64_t old = atomic_fetch_and_explicit(&pending->words[word], keep, memory_order_relaxed);
    int removed = __builtin_popcountll(old & ~keep);
    for (word++; word < pending->word_count; word++) {
        old = atomic_exchange_explicit(&pending->words[word], 0, memory_order_relaxed);
        removed += __builtin_popcountll(old);
    }
    atomic_fetch_sub_explicit(&pending->count, removed, memory_order_relaxed);
}
void behavior_pending_set(behavior_pending *pending, int index, bool present)
{
    assert(index >= 0 && index / 64 < pending->word_count);
    uint64_t bit = UINT64_C(1) << (index % 64);
    _Atomic uint64_t *word = &pending->words[index / 64];
    uint64_t old = present
                     ? atomic_fetch_or_explicit(word, bit, memory_order_relaxed)
                     : atomic_fetch_and_explicit(word, ~bit, memory_order_relaxed);
    if (((old & bit) != 0) != present) {
        atomic_fetch_add_explicit(&pending->count, present ? 1 : -1, memory_order_relaxed);
    }
}
int behavior_pending_next(const behavior_pending *pending, int start)
{
    assert(start >= 0);
    if (!atomic_load_explicit(&pending->count, memory_order_relaxed)) {
        return -1;
    }
    for (int word = start / 64; word < pending->word_count; word++) {
        uint64_t bits = atomic_load_explicit(&pending->words[word], memory_order_relaxed);
        if (word == start / 64) {
            bits &= UINT64_MAX << (start % 64);
        }
        if (bits) {
            return word * 64 + __builtin_ctzll(bits);
        }
    }
    return -1;
}
