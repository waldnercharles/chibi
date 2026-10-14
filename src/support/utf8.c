#include "support/utf8.h"

#include <stdint.h>

bool utf8_text_valid(const char *text, size_t bytes)
{
    for (size_t i = 0; i < bytes;) {
        unsigned char c = (unsigned char)text[i++];
        if (c < 128) {
            if (!c) {
                return false;
            }
            continue;
        }
        int count = c >= 0xc2 && c <= 0xdf ? 1
                  : c >= 0xe0 && c <= 0xef ? 2
                  : c >= 0xf0 && c <= 0xf4 ? 3
                                           : -1;
        if (count < 0 || bytes - i < (size_t)count) {
            return false;
        }
        uint32_t value = c & (0x7fu >> (count + 1));
        for (int n = 0; n < count; n++) {
            unsigned char part = (unsigned char)text[i++];
            if ((part & 0xc0) != 0x80) {
                return false;
            }
            value = (value << 6) | (part & 63);
        }
        if ((count == 1 && value < 0x80) || (count == 2 && value < 0x800) ||
            (count == 3 && value < 0x10000) || value > 0x10ffff ||
            (value >= 0xd800 && value <= 0xdfff)) {
            return false;
        }
    }
    return true;
}
