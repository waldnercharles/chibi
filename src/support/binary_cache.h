#ifndef CHIBI_BINARY_CACHE_H
#define CHIBI_BINARY_CACHE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BINARY_CACHE_KEY_BYTES 160
#define BINARY_CACHE_HEADER_BYTES 144
#define BINARY_CACHE_SCAN_LIMIT 4096

typedef struct
{
    char key[BINARY_CACHE_KEY_BYTES + 1];
    uint64_t bytes, stamp;
} binary_cache_entry;
/* Explicit disposable directory. Keys are bounded filename components. Reads
 * verify framing, key binding and SHA256 before returning malloc-owned bytes.
 * Missing/corrupt/unavailable storage returns NULL and length zero.
 * Windows currently reports unavailable storage for every operation; callers
 * continue without persistent caching. */
unsigned char *binary_cache_read(const char *directory, const char *key, size_t limit, size_t *length);
bool binary_cache_remove(const char *directory, const char *key);
/* Nonblocking publication lock; creates parent directories. Negative means
 * unavailable. Hold through list, policy-directed removals and atomic write. */
int binary_cache_lock(const char *directory);
void binary_cache_unlock(int descriptor);
/* Metadata is sorted by key. Returned entries are malloc-owned. Scans reject
 * beyond the fixed bound; orphan transaction temporaries are removed. */
bool binary_cache_list(const char *directory, binary_cache_entry **entries, int *count);
bool binary_cache_write(const char *directory, const char *key, const void *bytes, size_t length, size_t limit);
#endif
