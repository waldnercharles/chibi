#ifndef CHIBI_COLOR_VALUE_H
#define CHIBI_COLOR_VALUE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* Hex is sRGB RGB and straight linear alpha. Encoding clamps finite HDR/negative
 * values to the display gamut; it never changes the caller's numeric color.
 * Six-digit input implies opaque alpha. Failed decoding preserves output. */
bool color_value_to_hex(const float rgba[4], char text[10], char *error, size_t);
bool color_value_from_hex(const char *text, float rgba[4], char *error, size_t);
/* Packed colors preserve exact sRGB/straight-alpha bytes as 0xRRGGBBAA.
 * Encoding accepts only finite integral numbers in [0, UINT32_MAX]. Decoding
 * accepts six/eight hex digits (optional #), implies alpha FF for six, and
 * preserves the output on refusal. No linear-light conversion is performed. */
bool color_value_packed_to_hex(double packed, char text[10], char *error, size_t);
bool color_value_packed_from_hex(const char *text, uint32_t *packed, char *error, size_t);
#endif
