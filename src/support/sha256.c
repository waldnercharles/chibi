#include "support/sha256.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

static const uint32_t ROUND[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
    0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};
static uint32_t rotate(uint32_t value, int bits)
{
    return (value >> bits) | (value << (32 - bits));
}
static void block(uint32_t state[8], const unsigned char bytes[64])
{
    uint32_t words[64];
    for (int i = 0; i < 16; i++) {
        words[i] = ((uint32_t)bytes[4 * i] << 24) |
                   ((uint32_t)bytes[4 * i + 1] << 16) |
                   ((uint32_t)bytes[4 * i + 2] << 8) | bytes[4 * i + 3];
    }
    for (int i = 16; i < 64; i++) {
        uint32_t x = words[i - 15], y = words[i - 2];
        uint32_t a = rotate(x, 7) ^ rotate(x, 18) ^ (x >> 3);
        uint32_t b = rotate(y, 17) ^ rotate(y, 19) ^ (y >> 10);
        words[i] = words[i - 16] + a + words[i - 7] + b;
    }
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
    for (int i = 0; i < 64; i++) {
        uint32_t choice = (e & f) ^ (~e & g);
        uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        uint32_t upper = rotate(e, 6) ^ rotate(e, 11) ^ rotate(e, 25);
        uint32_t lower = rotate(a, 2) ^ rotate(a, 13) ^ rotate(a, 22);
        uint32_t first = h + upper + choice + ROUND[i] + words[i];
        uint32_t second = lower + majority;
        h = g;
        g = f;
        f = e;
        e = d + first;
        d = c;
        c = b;
        b = a;
        a = first + second;
    }
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}
void sha256(const void *data, size_t size, unsigned char digest[32])
{
    assert(data || !size);
    assert(digest);
    uint32_t state[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                          0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
    const unsigned char *bytes = data;
    uint64_t bits = (uint64_t)size << 3;
    while (size >= 64) {
        block(state, bytes);
        bytes += 64;
        size -= 64;
    }
    unsigned char tail[128] = { 0 };
    if (size) {
        memcpy(tail, bytes, size);
    }
    tail[size] = 0x80;
    size_t length = size < 56 ? 64 : 128;
    for (int i = 0; i < 8; i++) {
        tail[length - 1 - (size_t)i] = (unsigned char)(bits >> (8 * i));
    }
    block(state, tail);
    if (length == 128) {
        block(state, tail + 64);
    }
    for (int i = 0; i < 32; i++) {
        digest[i] = (unsigned char)(state[i / 4] >> (24 - 8 * (i % 4)));
    }
}
void sha256_hex(const void *data, size_t size, char hex[65])
{
    assert(hex);
    static const char DIGITS[] = "0123456789abcdef";
    unsigned char digest[32];
    sha256(data, size, digest);
    for (int i = 0; i < 32; i++) {
        hex[2 * i] = DIGITS[digest[i] >> 4];
        hex[2 * i + 1] = DIGITS[digest[i] & 15];
    }
    hex[64] = 0;
}
