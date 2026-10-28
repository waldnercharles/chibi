#include "types/text.h"

#include "support/arena.h"
#include "support/xalloc.h"
#include "types/copy_internal.h"

#include <assert.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

struct pin_text_owner
{
    atomic_size_t references;
    uint32_t length;
    char bytes[];
};

static void release_owner(void *resource)
{
    pin_text_owner *owner = resource;
    size_t before = atomic_fetch_sub_explicit(&owner->references, 1, memory_order_acq_rel);
    assert(before);
    if (before == 1) {
        free(owner);
    }
}

static const char *store_descriptor(arena *storage, pin_value *out, pin_string text)
{
    pin_string *descriptor = storage ? arena_alloc(storage, sizeof *descriptor)
                                     : xmalloc(sizeof *descriptor);
    if (!descriptor) {
        release_owner(text.owner);
        return "String allocation failed";
    }
    *descriptor = text;
    if (storage) {
        arena_set_cleanup(
            storage,
            descriptor,
            release_owner,
            text.owner,
            sizeof *text.owner + (size_t)text.owner->length + 1
        );
    }
    *out = (pin_value){ .type = PIN_STR, .text = descriptor };
    return NULL;
}

pin_string pin_text(pin_value value)
{
    assert(value.type == PIN_STR || value.type == PIN_NAME);
    if (value.type == PIN_NAME) {
        const char *bytes = value.str ? value.str : "";
        size_t length = strlen(bytes);
        assert(length <= UINT32_MAX);
        return (pin_string){ .bytes = bytes, .length = (uint32_t)length };
    }
    return value.text ? *value.text : (pin_string){ .bytes = "" };
}
bool pin_text_valid_utf8(pin_string text)
{
    uint32_t at = 0;
    while (at < text.length) {
        unsigned char first = (unsigned char)text.bytes[at++];
        if (first < 0x80) {
            continue;
        }
        unsigned remaining;
        uint32_t scalar, minimum;
        if (first >= 0xC2 && first <= 0xDF) {
            remaining = 1;
            scalar = first & 0x1F;
            minimum = 0x80;
        } else if (first >= 0xE0 && first <= 0xEF) {
            remaining = 2;
            scalar = first & 0x0F;
            minimum = 0x800;
        } else if (first >= 0xF0 && first <= 0xF4) {
            remaining = 3;
            scalar = first & 7;
            minimum = 0x10000;
        } else {
            return false;
        }
        if (remaining > text.length - at) {
            return false;
        }
        while (remaining--) {
            unsigned char byte = (unsigned char)text.bytes[at++];
            if ((byte & 0xC0) != 0x80) {
                return false;
            }
            scalar = (scalar << 6) | (byte & 0x3F);
        }
        if (scalar < minimum || scalar > 0x10FFFF ||
            (scalar >= 0xD800 && scalar <= 0xDFFF)) {
            return false;
        }
    }
    return true;
}
const char *pin_value_text(arena *storage, pin_value *out, const char *bytes, size_t length)
{
    if (!out || (!bytes && length) || length > UINT32_MAX ||
        length > SIZE_MAX - sizeof(pin_text_owner) - 1) {
        return "Invalid string length or data";
    }
    if (!length) {
        *out = (pin_value){ .type = PIN_STR };
        return NULL;
    }
    pin_text_owner *owner = xmalloc(sizeof *owner + length + 1);
    atomic_init(&owner->references, 1);
    owner->length = (uint32_t)length;
    memcpy(owner->bytes, bytes, length);
    owner->bytes[length] = 0;
    pin_copy_record(PIN_COPY_TEXT, length + 1);
    return store_descriptor(
        storage,
        out,
        (pin_string){ .bytes = owner->bytes, .length = (uint32_t)length, .owner = owner }
    );
}
const char *pin_value_text_slice(arena *storage, pin_value *out, pin_value source, size_t start, size_t length)
{
    if (!out || source.container != PIN_CONTAINER_SINGLE ||
        (source.type != PIN_STR && source.type != PIN_NAME)) {
        return "Invalid string slice";
    }
    pin_string text = pin_text(source);
    if (start > text.length || length > text.length - start) {
        return "String slice is outside its bounds";
    }
    if (!length) {
        *out = (pin_value){ .type = PIN_STR };
        return NULL;
    }
    /* Descriptors are immutable and traced, so a whole copy into the arena
     * that already holds one shares it. */
    if (storage && source.type == PIN_STR && length == text.length &&
        arena_contains(storage, source.text)) {
        *out = (pin_value){ .type = PIN_STR, .text = source.text };
        return NULL;
    }
    if (!text.owner) {
        return pin_value_text(storage, out, text.bytes + start, length);
    }
    [[maybe_unused]] size_t before = atomic_fetch_add_explicit(&text.owner->references, 1, memory_order_relaxed);
    assert(before && before < SIZE_MAX);
    text.bytes += start;
    text.length = (uint32_t)length;
    return store_descriptor(storage, out, text);
}
const char *pin_value_text_share(arena *storage, pin_value *out, pin_value source)
{
    if (source.container != PIN_CONTAINER_SINGLE ||
        (source.type != PIN_STR && source.type != PIN_NAME)) {
        return "Invalid string value";
    }
    return pin_value_text_slice(storage, out, source, 0, pin_text(source).length);
}
const char *pin_value_text_clone(arena *storage, pin_value *out, pin_value source)
{
    if (source.container != PIN_CONTAINER_SINGLE ||
        (source.type != PIN_STR && source.type != PIN_NAME)) {
        return "Invalid string value";
    }
    pin_string text = pin_text(source);
    return pin_value_text(storage, out, text.bytes, text.length);
}
size_t pin_text_footprint(size_t length)
{
    return sizeof(pin_string) + sizeof(pin_text_owner) + length + 1;
}
void pin_text_destroy(const pin_string *text)
{
    if (!text) {
        return;
    }
    if (text->owner) {
        release_owner(text->owner);
    }
    free((void *)text);
}
const char *pin_text_cstring(arena *storage, pin_value value, const char **out, size_t *nul_offset)
{
    if (!out || value.container != PIN_CONTAINER_SINGLE ||
        (value.type != PIN_STR && value.type != PIN_NAME)) {
        return "Invalid C string conversion";
    }
    pin_string text = pin_text(value);
    const char *nul = text.length ? memchr(text.bytes, 0, text.length) : NULL;
    if (nul_offset) {
        *nul_offset = nul ? (size_t)(nul - text.bytes) : SIZE_MAX;
    }
    if (nul) {
        return "String contains embedded NUL";
    }
    if (!text.bytes[text.length]) {
        *out = text.bytes;
        return NULL;
    }
    char *copy = storage ? arena_alloc(storage, (size_t)text.length + 1)
                         : xmalloc((size_t)text.length + 1);
    if (!copy) {
        return "C string allocation failed";
    }
    memcpy(copy, text.bytes, text.length);
    copy[text.length] = 0;
    *out = copy;
    return NULL;
}
