#include "support/sha256.h"

#include <inttypes.h>
#include <stdio.h>
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
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

/* Every rotation uses a uint32_t local and a fixed count from 1 through 31. */
#define ROTATE(value, bits) (((value) >> (bits)) | ((value) << (32 - (bits))))

static void compress(uint32_t state[8], const unsigned char *block)
{
    uint32_t w[64];
    for (int i = 0; i < 16; i++) {
        w[i] = (uint32_t)block[4 * i] << 24 | (uint32_t)block[4 * i + 1] << 16 |
               (uint32_t)block[4 * i + 2] << 8 | block[4 * i + 3];
    }
    for (int i = 16; i < 64; i++) {
        uint32_t x = w[i - 15], y = w[i - 2];
        uint32_t s0 = ROTATE(x, 7) ^ ROTATE(x, 18) ^ x >> 3;
        uint32_t s1 = ROTATE(y, 17) ^ ROTATE(y, 19) ^ y >> 10;
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
    for (int i = 0; i < 64; i++) {
        uint32_t t1 = h + (ROTATE(e, 6) ^ ROTATE(e, 11) ^ ROTATE(e, 25)) +
                      ((e & f) ^ (~e & g)) + ROUND[i] + w[i];
        uint32_t t2 = (ROTATE(a, 2) ^ ROTATE(a, 13) ^ ROTATE(a, 22)) +
                      ((a & b) ^ (a & c) ^ (b & c));
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
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

void sha256_begin(sha256 *hash)
{
    *hash = (sha256){
        .state = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 }
    };
}

void sha256_add(sha256 *hash, const void *data, size_t size)
{
    const unsigned char *bytes = data;
    while (size) {
        size_t used = hash->length % 64, copy = 64 - used < size ? 64 - used : size;
        memcpy(hash->block + used, bytes, copy);
        hash->length += copy;
        bytes += copy;
        size -= copy;
        if (used + copy == 64) {
            compress(hash->state, hash->block);
        }
    }
}

void sha256_end(sha256 *hash, char out[65])
{
    uint64_t bits = hash->length * 8;
    unsigned char padding[72] = { 0x80 };
    size_t used = hash->length % 64, size = used < 56 ? 56 - used : 120 - used;
    for (int i = 0; i < 8; i++) {
        padding[size + i] = (unsigned char)(bits >> (56 - 8 * i));
    }
    sha256_add(hash, padding, size + 8);
    for (int i = 0; i < 8; i++) {
        snprintf(out + 8 * i, 9, "%08" PRIx32, hash->state[i]);
    }
}

void sha256_hex(const void *data, size_t size, char out[65])
{
    sha256 hash;
    sha256_begin(&hash);
    sha256_add(&hash, data, size);
    sha256_end(&hash, out);
}
