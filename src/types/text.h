#ifndef CHIBI_TEXT_H
#define CHIBI_TEXT_H
#include "types/value.h"

#include <string.h>

/* PIN_STR holds a descriptor pointer; PIN_NAME remains a C string. A null
 * descriptor denotes empty text. Every descriptor's bytes[length] is readable;
 * owned constructors append zero, while a bounded borrowed slice may not. */
pin_string pin_text(pin_value);
static inline bool pin_text_equal(pin_string a, pin_string b)
{
    return a.length == b.length && (!a.length || !memcmp(a.bytes, b.bytes, a.length));
}
bool pin_text_valid_utf8(pin_string);
/* Copies bytes into retained immutable backing. NULL storage selects a heap
 * descriptor released by pin_value_destroy; arena descriptors expire with
 * storage. Constructs plain text with no nominal type name. Wrappers carrying a
 * nominal identity must restore that header. Errors leave out unchanged. No
 * UTF-8 validation is implied. */
const char *pin_value_text(arena *storage, pin_value *out, const char *bytes, size_t length);
/* Shares owned immutable bytes, copying borrowed bytes before retaining them.
 * Descriptors have independent lifetimes, including across arena reset/free.
 * Empty values have no backing allocation. Returns plain text; errors leave
 * out unchanged. */
const char *pin_value_text_share(arena *, pin_value *out, pin_value source);
/* Copies precisely the source bytes into independent backing. */
const char *pin_value_text_clone(arena *, pin_value *out, pin_value source);
/* A checked byte window, counting length bytes from start. A nested slice
 * retains the original backing directly. Empty slices retain no backing.
 * An invalid window leaves out unchanged. */
const char *pin_value_text_slice(arena *, pin_value *out, pin_value source, size_t start, size_t length);
/* What an arena holding nonempty text of this length accounts for it: its
 * descriptor and the shared backing it retains. */
size_t pin_text_footprint(size_t length);
/* Releases a heap descriptor and its retained backing. */
void pin_text_destroy(const pin_string *);
/* Reject embedded NUL before exposing C text. Reuse a terminating buffer or
 * copy into storage. With NULL storage, a nonterminated input is heap-copied
 * and the caller frees that buffer. Failure leaves out unchanged. nul_offset
 * receives the first NUL offset, or SIZE_MAX when none was found. */
const char *pin_text_cstring(arena *, pin_value, const char **out, size_t *nul_offset);
#endif
