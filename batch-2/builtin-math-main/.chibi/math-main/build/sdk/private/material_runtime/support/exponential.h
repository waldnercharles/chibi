#ifndef CHIBI_EXPONENTIAL_H
#define CHIBI_EXPONENTIAL_H

/*
 * Single-precision e^x approximation adapted from musl expf.c and
 * exp2f_data.c.
 *
 * Copyright (c) 2017-2018, Arm Limited.
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <math.h>
#include <stdint.h>
#include <string.h>

#if defined(__GNUC__) && !defined(__clang__)
__attribute__((optimize("fp-contract=off")))
#endif
static inline float deterministic_expf(float x)
{
#if defined(__clang__)
#pragma clang fp contract(off)
#endif
    static const uint64_t table[32] = {
        0x3ff0000000000000, 0x3fefd9b0d3158574, 0x3fefb5586cf9890f,
        0x3fef9301d0125b51, 0x3fef72b83c7d517b, 0x3fef54873168b9aa,
        0x3fef387a6e756238, 0x3fef1e9df51fdee1, 0x3fef06fe0a31b715,
        0x3feef1a7373aa9cb, 0x3feedea64c123422, 0x3feece086061892d,
        0x3feebfdad5362a27, 0x3feeb42b569d4f82, 0x3feeab07dd485429,
        0x3feea47eb03a5585, 0x3feea09e667f3bcd, 0x3fee9f75e8ec5f74,
        0x3feea11473eb0187, 0x3feea589994cce13, 0x3feeace5422aa0db,
        0x3feeb737b0cdc5e5, 0x3feec49182a3f090, 0x3feed503b23e255d,
        0x3feee89f995ad3ad, 0x3feeff76f2fb5e47, 0x3fef199bdd85529c,
        0x3fef3720dcef9069, 0x3fef5818dcfba487, 0x3fef7c97337b9b5f,
        0x3fefa4afa2a490da, 0x3fefd0765b6e4540,
    };
    uint32_t bits;
    memcpy(&bits, &x, sizeof bits);
    uint32_t top = bits >> 20;
    uint32_t absolute_top = top & 0x7ff;
    if (absolute_top >= (0x42b00000u >> 20)) {
        if (bits == 0xff800000u) {
            return 0;
        }
        if (absolute_top >= (0x7f800000u >> 20)) {
            return x + x;
        }
        if (x > 0x1.62e42ep6f) {
            return INFINITY;
        }
        if (x < -0x1.9fe368p6f) {
            return 0;
        }
    }

    double z = 0x1.71547652b82fep+5 * (double)x;
    double rounded = z + 0x1.8p+52;
    uint64_t integer;
    memcpy(&integer, &rounded, sizeof integer);
    rounded -= 0x1.8p+52;
    double remainder = z - rounded;
    uint64_t scale_bits = table[integer % 32];
    scale_bits += integer << (52 - 5);
    double scale;
    memcpy(&scale, &scale_bits, sizeof scale);

    double polynomial = 0x1.c6af84b912394p-20 * remainder + 0x1.ebfce50fac4f3p-13;
    double remainder_squared = remainder * remainder;
    double result = 0x1.62e42ff0c52d6p-6 * remainder + 1;
    result = polynomial * remainder_squared + result;
    result = result * scale;
    return (float)result;
}

#endif
