#include "platform/cute.h"
#include "third_party/pico_headers/pico_unit.h"

#include <stdint.h>
#include <string.h>

/* Screenshots save through Cute's PNG encoder. Its zlib header must declare
 * the window the encoder actually references, or strict decoders reject it. */

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

/* The first IDAT chunk's data, or NULL. */
static const uint8_t *idat(const uint8_t *png, int size)
{
    for (int at = PNG_SIGNATURE_SIZE; at + 8 <= size;) {
        uint32_t length = read32(png + at);
        if (!memcmp(png + at + 4, "IDAT", 4)) {
            return length >= 2 && at + 8 + (int)length <= size ? png + at + 8 : NULL;
        }
        at += 12 + (int)length;
    }
    return NULL;
}

TEST_CASE(png_zlib_header_declares_the_32k_window_and_round_trips)
{
    static CF_Pixel pixels[WIDTH * HEIGHT];
    for (int i = 0; i < WIDTH * HEIGHT; i++) {
        unsigned x = (unsigned)(i % WIDTH) * 2654435761u;
        pixels[i] = (CF_Pixel){
            .colors = { (uint8_t)(x >> 24), (uint8_t)(x >> 16), (uint8_t)(x >> 8), 255 }
        };
    }
    CF_Image image = { WIDTH, HEIGHT, pixels };
    void *data = NULL;
    int size = 0;
    REQUIRE(!cf_is_error(cf_image_save_png_to_memory(&image, &data, &size)));
    const uint8_t *zlib = idat(data, size);
    CF_Image decoded = { 0 };
    bool loaded = !cf_is_error(cf_image_load_png_from_memory(data, size, &decoded));
    bool same = loaded && decoded.w == WIDTH && decoded.h == HEIGHT &&
                !memcmp(decoded.pix, pixels, sizeof pixels);
    bool header = zlib && (zlib[0] & 15) == ZLIB_DEFLATE &&
                  zlib[0] >> 4 == ZLIB_WINDOW_32K &&
                  (zlib[0] << 8 | zlib[1]) % 31 == 0 && !(zlib[1] & 0x20);
    if (loaded) {
        cf_image_free(&decoded);
    }
    cf_free(data);
    REQUIRE(header);
    REQUIRE(same);
    return true;
}

void test_png_encoding()
{
    RUN_TEST_CASE(png_zlib_header_declares_the_32k_window_and_round_trips);
}
