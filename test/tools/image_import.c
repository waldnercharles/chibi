#include "authoring/image_import.h"

#include "authoring/image_prepare.h"
#include "authoring/shader_prepare.h"
#include "authoring/visual_prepare.h"
#include "graph/shader/shader_graph.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "test/graph/packs.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/catalog.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static image_import_plan plans[3];
static shader_image_view decoded[3];
static unsigned char *encoded, *pixels;
static size_t encoded_size;
static player_asset *assets;
static behavior_asset *material;
static render_image_loader previous;
static char error[256];
static unsigned char resident[] = { 255, 0, 0, 255 };

static bool load(void *context, const char *key, shader_image_view *out, char *message, size_t size)
{
    (void)context;
    (void)key;
    (void)message;
    (void)size;
    unsigned char *copy = xmalloc(4);
    memcpy(copy, resident, 4);
    *out = (shader_image_view){ copy, 1, 1 };
    return true;
}
static void setup()
{
    previous = render_images_loader();
    render_images_set_loader((render_image_loader){ .read = load });
    memset(error, 0, sizeof error);
    resident[0] = 255;
}
static void teardown()
{
    for (int i = 0; i < 3; i++) {
        image_import_free(&plans[i]);
        free((void *)decoded[i].pixels);
        decoded[i] = (shader_image_view){ 0 };
    }
    free(encoded);
    free(pixels);
    encoded = pixels = NULL;
    authoring_visual_prepared_free(assets);
    assets = NULL;
    behavior_asset_free(material);
    material = NULL;
    authoring_shader_catalog_clear();
    render_images_set_loader(previous);
}
static bool encode(const unsigned char *data, int width, int height)
{
    free(encoded);
    encoded = NULL;
    return image_prepare_png_encode(
        &(shader_image_view){ data, width, height },
        &encoded,
        &encoded_size,
        error,
        sizeof error
    );
}
static bool prepare(int index, int width, int height, image_import_semantic semantic)
{
    return image_import_prepare(
        encoded,
        encoded_size,
        (image_import_options){ width, height, semantic },
        &plans[index],
        error,
        sizeof error
    );
}
static bool decode_plan(int index)
{
    return image_prepare_decode(plans[index].png, plans[index].png_bytes, &decoded[index], error, sizeof error) !=
           NULL;
}
/* Small independent PNG fixtures: two RGB scanlines, valid PNG CRCs and zlib
 * Adler checksum, with explicit RFC 1951 stored blocks (no encoder dependency). */
static const unsigned char stored_rows[] = { 0, 255, 0, 0, 0, 0, 255, 0 };
static void big32(unsigned char *out, uint32_t value)
{
    for (int i = 0; i < 4; i++) {
        out[i] = (unsigned char)(value >> (24 - i * 8));
    }
}
static size_t png_chunk(unsigned char *out, const char *kind, const void *data, size_t length)
{
    big32(out, (uint32_t)length);
    memcpy(out + 4, kind, 4);
    if (length) {
        memcpy(out + 8, data, length);
    }
    uint32_t crc = UINT32_MAX;
    for (size_t i = 4; i < length + 8; i++) {
        crc ^= out[i];
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc >> 1) ^ (crc & 1 ? UINT32_C(0xedb88320) : 0);
        }
    }
    big32(out + length + 8, ~crc);
    return length + 12;
}
static size_t stored_block(unsigned char *out, bool final, const unsigned char *data, int declared, int available)
{
    out[0] = final;
    out[1] = (unsigned char)declared;
    out[2] = (unsigned char)(declared >> 8);
    out[3] = (unsigned char)~declared;
    out[4] = (unsigned char)(~declared >> 8);
    if (available) {
        memcpy(out + 5, data, (size_t)available);
    }
    return (size_t)available + 5;
}
static void stored_fixture(const unsigned char *deflate, size_t length)
{
    assert(length < 128);
    unsigned char zlib[136] = { 0x78, 0x01 };
    memcpy(zlib + 2, deflate, length);
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < sizeof stored_rows; i++) {
        a = (a + stored_rows[i]) % 65521;
        b = (b + a) % 65521;
    }
    big32(zlib + length + 2, (b << 16) | a);
    unsigned char header[13] = { 0, 0, 0, 1, 0, 0, 0, 2, 8, 2, 0, 0, 0 };
    free(encoded);
    encoded = xmalloc(256);
    memcpy(encoded, "\211PNG\r\n\032\n", 8);
    encoded_size = 8;
    encoded_size += png_chunk(encoded + encoded_size, "IHDR", header, sizeof header);
    /* Split within the zlib stream to cover IDAT concatenation as well. */
    encoded_size += png_chunk(encoded + encoded_size, "IDAT", zlib, 3);
    encoded_size += png_chunk(encoded + encoded_size, "IDAT", zlib + 3, length + 3);
    encoded_size += png_chunk(encoded + encoded_size, "IEND", NULL, 0);
}
TEST_CASE(png_stored_blocks_advance_across_prefetched_and_partial_words)
{
    unsigned char blocks[64];
    size_t length = stored_block(blocks, false, NULL, 0, 0);
    length += stored_block(blocks + length, false, stored_rows, 1, 1);
    length += stored_block(blocks + length, false, stored_rows + 1, 3, 3);
    length += stored_block(blocks + length, true, stored_rows + 4, 4, 4);
    stored_fixture(blocks, length);
    REQUIRE(prepare(0, 1, 2, IMAGE_IMPORT_SCALAR) && decode_plan(0));
    const unsigned char expected[] = { 255, 0, 0, 255, 0, 255, 0, 255 };
    REQUIRE(!memcmp(decoded[0].pixels, expected, sizeof expected));
    /* An empty fixed-code block ends at bit 10. The following final stored
     * block header/alignment ends at bit 16, then its LEN/NLEN follow. */
    blocks[0] = 2;
    blocks[1] = 4;
    unsigned char final[16];
    length = stored_block(final, true, stored_rows, 8, 8);
    memcpy(blocks + 2, final + 1, length - 1);
    stored_fixture(blocks, length + 1);
    REQUIRE(prepare(1, 1, 2, IMAGE_IMPORT_SCALAR) && decode_plan(1));
    REQUIRE(!memcmp(decoded[1].pixels, expected, sizeof expected));
    return true;
}
TEST_CASE(png_stored_blocks_reject_truncation_and_scanline_overflow)
{
    unsigned char blocks[64] = { 1, 8 };
    stored_fixture(blocks, 2); /* Missing LEN/NLEN. */
    REQUIRE(!prepare(0, 1, 2, IMAGE_IMPORT_SCALAR));
    size_t length = stored_block(blocks, true, stored_rows, 8, 7);
    stored_fixture(blocks, length); /* Declared payload is truncated. */
    REQUIRE(!prepare(0, 1, 2, IMAGE_IMPORT_SCALAR));
    const unsigned char overflow[] = { 0, 255, 0, 0, 0, 0, 255, 0, 0 };
    length = stored_block(blocks, true, overflow, 9, 9);
    stored_fixture(blocks, length); /* RGB offset must not inflate capacity. */
    REQUIRE(!prepare(0, 1, 2, IMAGE_IMPORT_SCALAR));
    length = stored_block(blocks, true, stored_rows, 7, 7);
    stored_fixture(blocks, length); /* Final block leaves one scanline byte absent. */
    REQUIRE(!prepare(0, 1, 2, IMAGE_IMPORT_SCALAR));
    length = stored_block(blocks, false, stored_rows, 8, 8);
    stored_fixture(blocks, length); /* Missing next block after non-final data. */
    REQUIRE(!prepare(0, 1, 2, IMAGE_IMPORT_SCALAR));
    REQUIRE(!plans[0].png);
    return true;
}
TEST_CASE(image_semantics_preserve_color_coverage_and_normal_direction)
{
    const unsigned char black_white[] = { 0, 0, 0, 255, 255, 255, 255, 255 };
    REQUIRE(encode(black_white, 2, 1));
    uint64_t source_hash = player_asset_fingerprint(encoded, encoded_size);
    REQUIRE(prepare(0, 1, 1, IMAGE_IMPORT_COLOR));
    REQUIRE(prepare(1, 1, 1, IMAGE_IMPORT_SCALAR));
    REQUIRE(decode_plan(0) && decode_plan(1));
    REQUIRE(decoded[0].pixels[0] == 188 && decoded[1].pixels[0] == 128);
    REQUIRE(plans[0].source_hash == source_hash && plans[1].source_hash == source_hash);
    REQUIRE(plans[0].output_hash != plans[1].output_hash);
    REQUIRE(player_asset_fingerprint(encoded, encoded_size) == source_hash);
    REQUIRE(plans[0].prepared_bytes > plans[0].pixel_bytes);
    const unsigned char normals[] = { 255, 128, 128, 255, 128, 255, 128, 255 };
    REQUIRE(encode(normals, 2, 1));
    REQUIRE(prepare(2, 1, 1, IMAGE_IMPORT_NORMAL) && decode_plan(2));
    REQUIRE(abs((int)decoded[2].pixels[0] - 218) <= 1);
    REQUIRE(abs((int)decoded[2].pixels[1] - 218) <= 1);
    REQUIRE(abs((int)decoded[2].pixels[2] - 128) <= 1);
    return true;
}
TEST_CASE(image_import_is_deterministic_and_refuses_unsupported_source_precision)
{
    const unsigned char transparent[] = { 255, 0, 0, 255, 0, 0, 255, 0 };
    REQUIRE(encode(transparent, 2, 1));
    REQUIRE(prepare(0, 1, 1, IMAGE_IMPORT_COLOR));
    REQUIRE(prepare(1, 1, 1, IMAGE_IMPORT_COLOR));
    REQUIRE(plans[0].png_bytes == plans[1].png_bytes);
    REQUIRE(!memcmp(plans[0].png, plans[1].png, plans[0].png_bytes));
    REQUIRE(decode_plan(0));
    REQUIRE(
        decoded[0].pixels[0] == 255 && decoded[0].pixels[2] == 0 &&
        decoded[0].pixels[3] == 128
    );
    unsigned char *kept = plans[0].png;
    encoded[24] = 16;
    REQUIRE(!prepare(0, 1, 1, IMAGE_IMPORT_SCALAR));
    REQUIRE(strstr(error, "bit depth") && strstr(error, "8-bit"));
    REQUIRE(plans[0].png == kept);
    encoded[24] = 8;
    REQUIRE(
        !image_import_prepare_fit(encoded, encoded_size, 512, IMAGE_IMPORT_COLOR, &plans[2], error, sizeof error)
    );
    REQUIRE(
        image_import_prepare_fit(encoded, encoded_size, 1024, IMAGE_IMPORT_SCALAR, &plans[2], error, sizeof error)
    );
    REQUIRE(plans[2].width == 2 && plans[2].height == 1); /* No upscale. */
    return true;
}
TEST_CASE(image_resolution_choices_preserve_aspect)
{
    size_t count = (size_t)1536 * 768 * 4;
    pixels = xmalloc(count);
    memset(pixels, 255, count);
    REQUIRE(encode(pixels, 1536, 768));
    REQUIRE(
        image_import_prepare_fit(encoded, encoded_size, 1024, IMAGE_IMPORT_COLOR, &plans[0], error, sizeof error)
    );
    REQUIRE(plans[0].source_width == 1536 && plans[0].source_height == 768);
    REQUIRE(plans[0].width == 1024 && plans[0].height == 512);
    REQUIRE(plans[0].pixel_bytes == (size_t)1024 * 512 * 4);
    return true;
}
TEST_CASE(visual_measure_matches_codec_totals_and_tracks_replacement_content)
{
    material = behavior_asset_new("Import measurement surface");
    material->domain = BEHAVIOR_DOMAIN_MATERIAL;
    REQUIRE(shader_graph_add_output(material->graph, false));
    shader_error diagnostic = { 0 };
    REQUIRE(render_shader_upsert(material, &diagnostic));
    REQUIRE(render_image_reload("content/current.png", error, sizeof error));
    REQUIRE(authoring_visual_prepare(NULL, 0, &assets, error, sizeof error));
    size_t total = 0;
    for (int i = 0; i < acount(assets); i++) {
        total += assets[i].length;
    }
    authoring_visual_budget before, after;
    REQUIRE(authoring_visual_measure(NULL, 0, NULL, 0, &before, error, sizeof error));
    REQUIRE(
        before.current_bytes == total && before.proposed_bytes == total &&
        before.fits
    );
    REQUIRE(before.current_count == acount(assets));
    REQUIRE(before.current[PLAYER_ASSET_SHADER].count == 1);
    REQUIRE(before.current[PLAYER_ASSET_PARTS].count == 1);
    REQUIRE(before.current[PLAYER_ASSET_SHADER_VALUES].count == 1);
    REQUIRE(before.current_pixel_bytes == 4 && before.current_hash == before.proposed_hash);
    resident[0] = 127;
    REQUIRE(render_image_reload("content/current.png", error, sizeof error));
    REQUIRE(authoring_visual_measure(NULL, 0, NULL, 0, &after, error, sizeof error));
    REQUIRE(
        after.current_bytes == before.current_bytes &&
        after.current_hash != before.current_hash
    );
    REQUIRE(encode(resident, 1, 1) && prepare(0, 2, 2, IMAGE_IMPORT_SCALAR));
    image_import_proposal proposal = { "content/current.png", &plans[0] };
    REQUIRE(authoring_visual_measure(NULL, 0, &proposal, 1, &before, error, sizeof error));
    REQUIRE(before.proposed_count == after.current_count && before.proposed_pixel_bytes == 16);
    REQUIRE(
        before.proposed_bytes ==
        after.current_bytes - after.current[PLAYER_ASSET_IMAGE].bytes + plans[0].prepared_bytes
    );
    image_import_proposal duplicate[] = { proposal, proposal };
    REQUIRE(!authoring_visual_measure(NULL, 0, duplicate, 2, &before, error, sizeof error));
    REQUIRE(strstr(error, "duplicated"));
    return true;
}
TEST_CASE(four_2k_images_exceed_budget_before_other_visual_assets)
{
    REQUIRE(encode(resident, 1, 1));
    REQUIRE(prepare(0, 2048, 2048, IMAGE_IMPORT_SCALAR));
    image_import_proposal proposals[] = { { "content/a.png", &plans[0] },
                                          { "content/b.png", &plans[0] },
                                          { "content/c.png", &plans[0] },
                                          { "content/d.png", &plans[0] } };
    authoring_visual_budget budget;
    REQUIRE(authoring_visual_measure(NULL, 0, proposals, 4, &budget, error, sizeof error));
    REQUIRE(!budget.fits && budget.proposed_pixel_bytes == budget.limit_bytes);
    REQUIRE(budget.proposed[PLAYER_ASSET_IMAGE].bytes == plans[0].prepared_bytes * 4);
    REQUIRE(budget.proposed_bytes > budget.limit_bytes);
    REQUIRE(budget.proposed_bytes == budget.current_bytes + plans[0].prepared_bytes * 4);
    return true;
}
void test_image_import()
{
    type_catalog_pack types = { .types = SHADER_PACK.types,
                                .type_count = SHADER_PACK.type_count,
                                .enums = SHADER_PACK.enums,
                                .enum_count = SHADER_PACK.enum_count };
    type_catalog_set_project(&types);
    test_graph_packs_install();
    pu_setup(setup, teardown);
    RUN_TEST_CASE(png_stored_blocks_advance_across_prefetched_and_partial_words);
    RUN_TEST_CASE(png_stored_blocks_reject_truncation_and_scanline_overflow);
    RUN_TEST_CASE(image_semantics_preserve_color_coverage_and_normal_direction);
    RUN_TEST_CASE(image_import_is_deterministic_and_refuses_unsupported_source_precision);
    RUN_TEST_CASE(image_resolution_choices_preserve_aspect);
    RUN_TEST_CASE(visual_measure_matches_codec_totals_and_tracks_replacement_content);
    RUN_TEST_CASE(four_2k_images_exceed_budget_before_other_visual_assets);
    pu_clear_setup();
    test_graph_packs_restore();
    type_catalog_set_project(NULL);
}
