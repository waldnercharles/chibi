#include "types/text.h"

#include "support/arena.h"
#include "support/xalloc.h"
#include "types/copy_internal.h"

#include <assert.h>
#include <string.h>

pin_string pin_text(pin_value value)
{
    assert(value.type == PIN_STR || value.type == PIN_NAME);
    if (value.type == PIN_NAME) {
        const char *bytes = value.str ? value.str : "";
        size_t length = strlen(bytes);
        assert(length <= UINT32_MAX);
        return (pin_string){ bytes, (uint32_t)length };
    }
    return value.text ? *value.text : (pin_string){ "", 0 };
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
        length > SIZE_MAX - sizeof(pin_string) - 1) {
        return "Invalid string length or data";
    }
    size_t size = sizeof(pin_string) + length + 1;
    pin_string *text = storage ? arena_alloc(storage, size) : xmalloc(size);
    if (!text) {
        return "String allocation failed";
    }
    char *data = (char *)(text + 1);
    if (length) {
        memcpy(data, bytes, length);
    }
    data[length] = 0;
    *text = (pin_string){ data, (uint32_t)length };
    *out = (pin_value){ .type = PIN_STR, .text = text };
    pin_copy_record(PIN_COPY_TEXT, length + 1);
    return NULL;
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
