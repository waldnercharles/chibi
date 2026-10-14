#include "runtime/color_value.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static bool fail(char *error, size_t size, const char *message)
{
    if (size) {
        snprintf(error, size, "%s", message);
    }
    return false;
}
static float clamp(float value)
{
    return fminf(fmaxf(value, 0), 1);
}
static float linear(float value)
{
    return value <= .04045f ? value / 12.92f : powf((value + .055f) / 1.055f, 2.4f);
}
static float srgb(float value)
{
    return value <= .0031308f ? value * 12.92f : 1.055f * powf(value, 1 / 2.4f) - .055f;
}
bool color_value_to_hex(const float rgba[4], char text[10], char *error, size_t size)
{
    uint32_t packed = 0;
    for (int i = 0; i < 4; i++) {
        if (!isfinite(rgba[i])) {
            return fail(error, size, "Color components must be finite");
        }
        float value = clamp(rgba[i]);
        if (i < 3) {
            value = srgb(value);
        }
        packed = (packed << 8) | (uint32_t)lroundf(clamp(value) * 255);
    }
    snprintf(text, 10, "#%08x", packed);
    if (size) {
        error[0] = 0;
    }
    return true;
}
bool color_value_packed_from_hex(const char *text, uint32_t *out, char *error, size_t size)
{
    if (!text) {
        return fail(error, size, "Choose a six- or eight-digit hexadecimal color");
    }
    const char *digits = text[0] == '#' ? text + 1 : text;
    size_t length = strnlen(digits, 9);
    if (length != 6 && length != 8) {
        return fail(error, size, "Choose a six- or eight-digit hexadecimal color");
    }
    uint32_t packed = 0;
    for (size_t i = 0; i < length; i++) {
        int digit = digits[i] >= '0' && digits[i] <= '9' ? digits[i] - '0'
                  : digits[i] >= 'a' && digits[i] <= 'f' ? digits[i] - 'a' + 10
                  : digits[i] >= 'A' && digits[i] <= 'F' ? digits[i] - 'A' + 10
                                                         : -1;
        if (digit < 0) {
            return fail(error, size, "Color contains a non-hexadecimal digit");
        }
        packed = (packed << 4) | (uint32_t)digit;
    }
    if (length == 6) {
        packed = (packed << 8) | 255;
    }
    *out = packed;
    if (size) {
        error[0] = 0;
    }
    return true;
}
bool color_value_packed_to_hex(double packed, char text[10], char *error, size_t size)
{
    if (!isfinite(packed) || packed < 0 || packed > UINT32_MAX ||
        trunc(packed) != packed) {
        return fail(error, size, "Packed color must be an integer from zero through 4294967295");
    }
    snprintf(text, 10, "#%08x", (uint32_t)packed);
    if (size) {
        error[0] = 0;
    }
    return true;
}
bool color_value_from_hex(const char *text, float rgba[4], char *error, size_t size)
{
    uint32_t packed;
    if (!color_value_packed_from_hex(text, &packed, error, size)) {
        return false;
    }
    float result[4];
    for (int i = 0; i < 4; i++) {
        float value = ((packed >> (24 - i * 8)) & 255) / 255.f;
        result[i] = i < 3 ? linear(value) : value;
    }
    memcpy(rgba, result, sizeof result);
    if (size) {
        error[0] = 0;
    }
    return true;
}
