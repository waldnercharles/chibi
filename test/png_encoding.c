#include "platform/cute.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum
{
    WIDTH = 160, /* 641-byte filtered rows: repeated rows reference beyond 256 bytes. */
    HEIGHT = 4,
    PNG_SIGNATURE_SIZE = 8,
    ZLIB_DEFLATE = 8,
    ZLIB_WINDOW_32K = 7
};

static uint32_t read32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static const uint8_t *idat(const uint8_t *png, int size)
{
    for (int at = PNG_SIGNATURE_SIZE; at + 8 <= size;) {
        uint32_t length = read32(png + at);
        if (!memcmp(png + at + 4, "IDAT", 4)) {
            assert(length >= 2 && at + 8 + (int)length <= size);
            return png + at + 8;
        }
        at += 12 + (int)length;
    }
    assert(!"engine PNG has no IDAT chunk");
    return NULL;
}

void test_png_encoding()
{
    CF_Pixel pixels[WIDTH * HEIGHT];
    for (int i = 0; i < WIDTH * HEIGHT; i++) {
        unsigned x = (unsigned)(i % WIDTH) * 2654435761u;
        pixels[i] = (CF_Pixel){
            .colors = { (uint8_t)(x >> 24), (uint8_t)(x >> 16), (uint8_t)(x >> 8), 255 }
        };
    }
    CF_Image image = { WIDTH, HEIGHT, pixels };
    void *data = NULL;
    int size = 0;
    assert(!cf_is_error(cf_image_save_png_to_memory(&image, &data, &size)));
    const uint8_t *zlib = idat(data, size);
    assert((zlib[0] & 15) == ZLIB_DEFLATE);
    assert(zlib[0] >> 4 == ZLIB_WINDOW_32K);
    assert((zlib[0] << 8 | zlib[1]) % 31 == 0);
    assert(!(zlib[1] & 0x20));

    CF_Image decoded = { 0 };
    assert(!cf_is_error(cf_image_load_png_from_memory(data, size, &decoded)));
    assert(decoded.w == WIDTH && decoded.h == HEIGHT);
    assert(!memcmp(decoded.pix, pixels, sizeof pixels));
    cf_image_free(&decoded);
    cf_free(data);
    puts("png encoding PASS: zlib header declares the encoder's 32K window and round-trips");
}
