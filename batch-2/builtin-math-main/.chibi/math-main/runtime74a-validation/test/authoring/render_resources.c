#include "render/gpu_timing.h"
#include "render/shader_catalog.h"
#include "render/shader_images.h"
#include "render/shader_publish.h"
#include "runtime/component_catalog.h"
#include "runtime/images/sky.h"
#include "runtime/scenes/scene.h"
#include "runtime/shaders/prepared.h"
#include "runtime/ui/documents.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Fixed CPU snapshots only: no app/window, texture upload, draw, files, world
 * or source parsing. Cute's default pixel allocator delegates to malloc/free. */
static player_resource_host host;
static shader_prepared_asset producer;
static prepared_ui_document document;
static unsigned char *bytes[3];
static size_t lengths[3];
static void *pending, *old_generation, *current_generation;
static const char *const pixel_key = "content/textures/pixel.png";
static void cleanup()
{
    if (pending) {
        host.discard(host.context, pending);
    }
    if (old_generation) {
        host.retire(host.context, old_generation);
    }
    if (current_generation) {
        host.retire(host.context, current_generation);
    }
    pending = old_generation = current_generation = NULL;
    shader_prepared_asset_free(&producer);
    prepared_ui_document_free(&document);
    for (int i = 0; i < 3; i++) {
        free(bytes[i]);
        bytes[i] = NULL;
    }
}
static bool inputs(bool replacement, player_asset records[3])
{
    for (int i = 0; i < 3; i++) {
        free(bytes[i]);
        bytes[i] = NULL;
    }
    producer.name = xstrdup("OwnedShader");
    producer.names[0] = xstrdup(replacement ? "NewTint" : "OldTint");
    producer.widths[0] = 3;
    producer.defaults[0][0] = .25f;
    producer.defaults[0][1] = .5f;
    producer.defaults[0][2] = .75f;
    shader_graph *g = &producer.graph;
    int uv = shader_graph_add(g, SHADER_CONSTANT, 2);
    int image = shader_graph_add(g, SHADER_IMAGE_TEXTURE, 3);
    shader_image_source resource = { .key = (char *)pixel_key,
                                     .color_space = SHADER_IMAGE_LINEAR,
                                     .filter = SHADER_IMAGE_NEAREST,
                                     .wrap = SHADER_IMAGE_CLAMP };
    g->nodes[image].image = shader_graph_add_image(g, &resource);
    g->nodes[image].input[0] = uv;
    int tint = shader_graph_add(g, SHADER_PARAMETER, 3);
    g->nodes[tint].parameter = 0;
    g->output[0] = image;
    g->output[1] = tint;
    g->output_count = 2;
    shader_error shader_error = { 0 };
    REQUIRE(shader_asset_prepared_write(&producer, &bytes[0], &lengths[0], &shader_error));
    unsigned char pixels[4] = { replacement ? 0 : 255, 0, replacement ? 255 : 0, 255 };
    shader_image_view view = { .pixels = pixels, .width = 1, .height = 1 };
    REQUIRE(render_image_pixels_prepared_write(&view, &bytes[1], &lengths[1], &shader_error));
    document.name = xstrdup("OwnedPage");
    document.markup = xstrdup(replacement ? "<img id='new'/>" : "<img id='old'/>");
    document.style = xstrdup("img { width: 1px; }");
    document.body = xstrdup("class='owned'");
    apush(document.resources, ((ui_resource){ .kind = UI_RESOURCE_IMAGE, .key = xstrdup(pixel_key) }));
    char error[128];
    REQUIRE(ui_document_prepared_write(&document, &bytes[2], &lengths[2], error, sizeof error));
    shader_prepared_asset_free(&producer);
    prepared_ui_document_free(&document);
    memset(pixels, 0, sizeof pixels);
    const player_asset_kind kinds[] = { PLAYER_ASSET_SHADER,
                                        PLAYER_ASSET_IMAGE,
                                        PLAYER_ASSET_UI };
    const char *const names[] = { "OwnedShader", pixel_key, "OwnedPage" };
    for (int i = 0; i < 3; i++) {
        REQUIRE(lengths[i] >= 16 && lengths[i] < 8192);
        records[i] = (player_asset){
            .name = names[i],
            .kind = kinds[i],
            .data = bytes[i],
            .length = lengths[i],
            .content_hash = player_asset_fingerprint(bytes[i], lengths[i])
        };
    }
    return true;
}
static bool sample(bool replacement)
{
    int index = render_shader_find("OwnedShader");
    REQUIRE(index > 0 && render_shader_parameter_width(index, 0) == 3);
    REQUIRE(!strcmp(render_shader_parameter_name(index, 0), replacement ? "NewTint" : "OldTint"));
    float parameters[SHADER_PARAMETER_MAX][4] = { { .25f, .5f, .75f } };
    float values[SHADER_OUTPUT_MAX][4];
    shader_sample_context context = { 0 };
    shader_error error = { 0 };
    REQUIRE(render_shader_sample(index, &context, parameters, values, &error));
    REQUIRE(
        values[0][0] == (replacement ? 0 : 1) && values[0][1] == 0 &&
        values[0][2] == (replacement ? 1 : 0)
    );
    REQUIRE(values[1][0] == .25f && values[1][1] == .5f && values[1][2] == .75f);
    const prepared_ui_document *page = ui_documents_find(ui_documents_current(), "OwnedPage");
    REQUIRE(page && !strcmp(page->markup, replacement ? "<img id='new'/>" : "<img id='old'/>"));
    return true;
}
TEST_CASE(prepared_renderer_resources_publish_atomically_and_retain_retired_tokens)
{
    host = render_prepared_resources(NULL);
    shader_prepared_asset_init(&producer);
    REQUIRE(!render_image_source_count() && !ui_documents_count(ui_documents_current()));
    REQUIRE(!render_shader_find("OwnedShader"));
    player_asset records[3];
    REQUIRE(inputs(false, records));
    char error[128] = { 0 };
    size_t remaining = 2 * 1024 * 1024;
    pending = host.stage(host.context, records, 3, &remaining, error, sizeof error);
    REQUIRE(pending && remaining < 2 * 1024 * 1024);
    REQUIRE(!render_shader_find("OwnedShader") && !render_image_source_count());
    host.commit(host.context, pending, NULL);
    current_generation = pending;
    pending = NULL;
    REQUIRE(sample(false));
    const char *old_name = render_shader_parameter_name(render_shader_find("OwnedShader"), 0);
    const prepared_ui_document *old_page = ui_documents_find(ui_documents_current(), "OwnedPage");
    shader_image_source resource = { .key = (char *)pixel_key,
                                     .color_space = SHADER_IMAGE_LINEAR,
                                     .filter = SHADER_IMAGE_NEAREST,
                                     .wrap = SHADER_IMAGE_CLAMP };
    shader_image_view old_pixels;
    REQUIRE(render_image_resolve(NULL, &resource, &old_pixels));
    REQUIRE(inputs(true, records));
    unsigned revision = render_shader_revision();
    player_asset bad[3];
    memcpy(bad, records, sizeof bad);
    bad[1].content_hash ^= UINT64_C(1);
    remaining = 2 * 1024 * 1024;
    REQUIRE(!host.stage(host.context, bad, 3, &remaining, error, sizeof error));
    REQUIRE(remaining == 2 * 1024 * 1024 && error[0] && render_shader_revision() == revision);
    memcpy(bad, records, sizeof bad);
    bad[1].length--;
    bad[1].content_hash = 0;
    REQUIRE(!host.stage(host.context, bad, 3, &remaining, error, sizeof error));
    REQUIRE(remaining == 2 * 1024 * 1024 && render_shader_revision() == revision);
    player_asset missing[] = { records[0], records[2] };
    REQUIRE(!host.stage(host.context, missing, 2, &remaining, error, sizeof error));
    REQUIRE(remaining == 2 * 1024 * 1024 && render_shader_revision() == revision);
    memcpy(bad, records, sizeof bad);
    bad[0].name = "WrongShader";
    REQUIRE(!host.stage(host.context, bad, 3, &remaining, error, sizeof error));
    REQUIRE(remaining == 2 * 1024 * 1024 && render_shader_revision() == revision);
    remaining = 1;
    REQUIRE(!host.stage(host.context, records, 3, &remaining, error, sizeof error));
    REQUIRE(remaining == 1 && render_shader_revision() == revision);
    REQUIRE(sample(false));
    remaining = 2 * 1024 * 1024;
    pending = host.stage(host.context, records, 3, &remaining, error, sizeof error);
    REQUIRE(pending);
    host.discard(host.context, pending);
    pending = NULL;
    REQUIRE(sample(false) && render_shader_revision() == revision);
    remaining = 2 * 1024 * 1024;
    pending = host.stage(host.context, records, 3, &remaining, error, sizeof error);
    REQUIRE(pending);
    for (int i = 0; i < 3; i++) {
        free(bytes[i]);
        bytes[i] = NULL;
    }
    host.commit(host.context, pending, NULL);
    old_generation = current_generation;
    current_generation = pending;
    pending = NULL;
    REQUIRE(sample(true));
    REQUIRE(
        !strcmp(old_name, "OldTint") && !strcmp(old_page->markup, "<img id='old'/>") &&
        !strcmp(old_page->body, "class='owned'")
    );
    REQUIRE(
        old_pixels.width == 1 && old_pixels.height == 1 &&
        old_pixels.pixels[0] == 255 && old_pixels.pixels[2] == 0
    );
    host.retire(host.context, old_generation);
    old_generation = NULL;
    REQUIRE(sample(true));
    host.retire(host.context, current_generation);
    current_generation = NULL;
    REQUIRE(!render_shader_find("OwnedShader") && !render_image_source_count());
    REQUIRE(!ui_documents_active() && !ui_documents_count(ui_documents_current()));
    return true;
}
/* A sprite's image variant joins a program's sorted image set, and the
 * program samples it by that variant. */
TEST_CASE(sprite_images_join_the_program_set_and_sample_by_variant)
{
    render_shader_set set = { 0 };
    shader_error error = { 0 };
    REQUIRE(render_shader_set_add_image(&set, 7, &error));
    REQUIRE(render_shader_set_add_image(&set, 3, &error));
    REQUIRE(render_shader_set_add_image(&set, 7, &error));
    REQUIRE(set.count == 2 && set.images[0] == 3 && set.images[1] == 7);
    char *glsl = render_shader_glsl(&set, 2, 0);
    REQUIRE(strstr(glsl, "if(id==3) return textureLod(sg_image_0,uv,0.0);"));
    REQUIRE(strstr(glsl, "if(id==7) return textureLod(sg_image_1,uv,0.0);"));
    free(glsl);
    for (int i = set.count; i < RENDER_SHADER_IMAGE_MAX; i++) {
        REQUIRE(render_shader_set_add_image(&set, 10 + i, &error));
    }
    REQUIRE(!render_shader_set_add_image(&set, 99, &error) && error.message[0]);
    REQUIRE(set.count == RENDER_SHADER_IMAGE_MAX);
    return true;
}
/* A sky `width` texels wide whose every texel holds `rgbe`. */
static render_sky uniform_sky(int width, const unsigned char rgbe[4])
{
    render_sky sky = { .width = width,
                       .height = width / 2,
                       .texels = xmalloc((size_t)width * width / 2 * 4) };
    for (int i = 0; i < sky.width * sky.height; i++) {
        memcpy(sky.texels + i * 4, rgbe, 4);
    }
    return sky;
}
/* Whether every texel of a 4 x 4 face holds `rgb` as halves. */
static bool uniform_face(const render_sky *sky, int face, const uint16_t rgb[3])
{
    uint16_t texels[4 * 4 * 4];
    render_sky_face(sky, face, 4, texels);
    for (int i = 0; i < 4 * 4; i++) {
        const uint16_t *t = texels + i * 4;
        REQUIRE(t[0] == rgb[0] && t[1] == rgb[1] && t[2] == rgb[2] && t[3] == 0x3c00);
    }
    return true;
}
/* The direction texel (x, y) of a 64 x 32 sky looks along. Row 0 looks up,
 * the middle column along -Z and the column three quarters across along +X. */
static void sky_ray(int x, int y, float ray[3])
{
    float elevation = (.5f - (y + .5f) / 32) * 3.14159265f;
    float azimuth = ((x + .5f) / 64 - .5f) * 2 * 3.14159265f;
    ray[0] = cosf(elevation) * sinf(azimuth);
    ray[1] = sinf(elevation);
    ray[2] = -cosf(elevation) * cosf(azimuth);
}
/* A 64 x 32 sky holding one half in red where a texel looks toward +X, green
 * toward +Y and blue toward +Z. */
static render_sky octant_sky()
{
    render_sky sky = { .width = 64, .height = 32, .texels = xmalloc(64 * 32 * 4) };
    for (int y = 0; y < sky.height; y++) {
        for (int x = 0; x < sky.width; x++) {
            float ray[3];
            sky_ray(x, y, ray);
            unsigned char *t = sky.texels + (y * sky.width + x) * 4;
            for (int c = 0; c < 3; c++) {
                t[c] = ray[c] > 0 ? 128 : 0;
            }
            t[3] = 128;
        }
    }
    return sky;
}
/* Unit axes whose squared cosine with each texel's direction the quadratic
 * sky holds, one per channel. Together they reach every second-band term, the
 * last with unequal weights on xy, yz and xz. */
static const float quadratic_axes[3][3] = { { 0, 1, 0 },
                                            { 1, 0, 0 },
                                            { .26726124f, .53452248f, .80178373f } };
/* A 64 x 32 sky whose channel c holds (a_c . d)^2 toward direction d. */
static render_sky quadratic_sky()
{
    render_sky sky = { .width = 64, .height = 32, .texels = xmalloc(64 * 32 * 4) };
    for (int y = 0; y < sky.height; y++) {
        for (int x = 0; x < sky.width; x++) {
            float ray[3];
            sky_ray(x, y, ray);
            unsigned char *t = sky.texels + (y * sky.width + x) * 4;
            for (int c = 0; c < 3; c++) {
                const float *a = quadratic_axes[c];
                float cosine = a[0] * ray[0] + a[1] * ray[1] + a[2] * ray[2];
                /* Mantissa over 256 under exponent 128, capped at 255. */
                t[c] = (unsigned char)fminf(roundf(cosine * cosine * 256), 255);
            }
            t[3] = 128;
        }
    }
    return sky;
}
/* The cube-map face and texel that direction `d` selects in faces `size`
 * texels wide, by the GPU's face selection table: the major axis picks the
 * face, then s and t run along the axes the table assigns it. */
static int select_face(const float d[3], int size, int *s, int *t)
{
    float x = fabsf(d[0]), y = fabsf(d[1]), z = fabsf(d[2]);
    float sc, tc, major;
    int face;
    if (x >= y && x >= z) {
        face = d[0] > 0 ? 0 : 1;
        sc = d[0] > 0 ? -d[2] : d[2];
        tc = -d[1];
        major = x;
    } else if (y >= z) {
        face = d[1] > 0 ? 2 : 3;
        sc = d[0];
        tc = d[1] > 0 ? d[2] : -d[2];
        major = y;
    } else {
        face = d[2] > 0 ? 4 : 5;
        sc = d[2] > 0 ? d[0] : -d[0];
        tc = -d[1];
        major = z;
    }
    *s = (int)((sc / major + 1) / 2 * size);
    *t = (int)((tc / major + 1) / 2 * size);
    return face;
}
/* Directions toward each face, leaning into each of its four corners, find
 * the octant they look into at the texel the GPU would read for them. */
TEST_CASE(sky_faces_follow_the_gpu_cube_map_layout)
{
    render_sky sky = octant_sky();
    REQUIRE(render_sky_face_size(&sky) == 16);
    static uint16_t faces[6][16 * 16 * 4];
    for (int face = 0; face < 6; face++) {
        render_sky_face(&sky, face, 16, faces[face]);
    }
    render_sky_free(&sky);
    for (int axis = 0; axis < 3; axis++) {
        for (int sign = -1; sign <= 1; sign += 2) {
            for (int corner = 0; corner < 4; corner++) {
                float d[3];
                d[axis] = (float)sign;
                d[(axis + 1) % 3] = corner & 1 ? .6f : -.6f;
                d[(axis + 2) % 3] = corner & 2 ? .6f : -.6f;
                int s, t, face = select_face(d, 16, &s, &t);
                REQUIRE(face == axis * 2 + (sign < 0));
                const uint16_t *texel = faces[face] + (t * 16 + s) * 4;
                for (int c = 0; c < 3; c++) {
                    REQUIRE(texel[c] == (d[c] > 0 ? 0x3800 : 0));
                }
            }
        }
    }
    return true;
}
/* RGBE texels become exact halves: 128 x 2^(129 - 136) is one, and the
 * mantissas halve from there. Past the half range saturates, and a zero
 * exponent is black. */
TEST_CASE(sky_faces_hold_texels_as_halves)
{
    render_sky sky = uniform_sky(16, (const unsigned char[4]){ 128, 64, 32, 129 });
    REQUIRE(uniform_face(&sky, 0, (const uint16_t[3]){ 0x3c00, 0x3800, 0x3400 }));
    render_sky_free(&sky);
    sky = uniform_sky(16, (const unsigned char[4]){ 255, 1, 0, 145 });
    REQUIRE(uniform_face(&sky, 3, (const uint16_t[3]){ 0x7bff, 0x6000, 0 }));
    render_sky_free(&sky);
    sky = uniform_sky(16, (const unsigned char[4]){ 255, 255, 255, 0 });
    REQUIRE(uniform_face(&sky, 5, (const uint16_t[3]){ 0 }));
    render_sky_free(&sky);
    return true;
}
/* Irradiance in channel c toward unit direction d, from coefficients as
 * sky.h spells them out. */
static float irradiance(const float coefficients[9][3], const float d[3], int c)
{
    float x = d[0], y = d[1], z = d[2];
    float terms[9] = {
        1, x, y, z, x * y, y * z, 3 * y * y - 1, x * z, x * x - z * z
    };
    float sum = 0;
    for (int i = 0; i < 9; i++) {
        sum += coefficients[i][c] * terms[i];
    }
    return sum;
}
/* An even sky of radiance L gives pi L toward every direction. The octant sky
 * holds 0.5 in each channel over the half of the sky its axis points into, so
 * channel c receives pi 0.5 (1 + d_c) / 2: the cosine integral over that half.
 * Radiance (a . d)^2 lies within the first three bands, which the nine terms
 * carry exactly, and gives pi (1 + (a . d)^2) / 4. */
TEST_CASE(sky_irradiance_integrates_the_cosine_lobe)
{
    static const float pi = 3.14159265f;
    float coefficients[3][9][3];
    render_sky sky = uniform_sky(64, (const unsigned char[4]){ 128, 64, 32, 128 });
    render_sky_irradiance(&sky, coefficients[0]);
    render_sky_free(&sky);
    sky = octant_sky();
    render_sky_irradiance(&sky, coefficients[1]);
    render_sky_free(&sky);
    sky = quadratic_sky();
    render_sky_irradiance(&sky, coefficients[2]);
    render_sky_free(&sky);
    int checked = 0;
    for (int i = 0; i < 27; i++) {
        float d[3] = { (float)(i % 3 - 1), (float)(i / 3 % 3 - 1), (float)(i / 9 - 1) };
        float length = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        if (!length) {
            continue;
        }
        for (int c = 0; c < 3; c++) {
            d[c] /= length;
        }
        for (int c = 0; c < 3; c++) {
            float even = pi * ldexpf(1, -1 - c), half = pi * .5f * (1 + d[c]) / 2;
            REQUIRE(fabsf(irradiance(coefficients[0], d, c) - even) < even * .005f);
            REQUIRE(fabsf(irradiance(coefficients[1], d, c) - half) < pi * .5f * .01f);
            const float *a = quadratic_axes[c];
            float cosine = a[0] * d[0] + a[1] * d[1] + a[2] * d[2];
            float quadratic = pi * (1 + cosine * cosine) / 4;
            REQUIRE(fabsf(irradiance(coefficients[2], d, c) - quadratic) < pi * .5f * .01f);
        }
        checked++;
    }
    REQUIRE(checked == 26);
    return true;
}
/* Radiance past the half range saturates in the faces, so the brightest RGBE
 * sky lights as an even sky of 65504 rather than with infinite coefficients. */
TEST_CASE(sky_irradiance_saturates_with_the_faces)
{
    const float pi = 3.14159265f, saturated = pi * 65504;
    float coefficients[9][3];
    render_sky sky = uniform_sky(64, (const unsigned char[4]){ 255, 255, 255, 255 });
    render_sky_irradiance(&sky, coefficients);
    render_sky_free(&sky);
    const float up[3] = { 0, 1, 0 }, side[3] = { 1, 0, 0 };
    for (int c = 0; c < 3; c++) {
        REQUIRE(fabsf(irradiance(coefficients, up, c) - saturated) < saturated * .005f);
        REQUIRE(fabsf(irradiance(coefficients, side, c) - saturated) < saturated * .005f);
    }
    return true;
}
static float from_half(uint16_t h)
{
    int exponent = h >> 10 & 0x1f, mantissa = h & 0x3ff;
    float value = exponent ? ldexpf((float)(1024 + mantissa), exponent - 25)
                           : ldexpf((float)mantissa, -24);
    return h & 0x8000 ? -value : value;
}
/* The specular reflectance of a GGX surface under even white light, split
 * into the part that scales F0 and the part added to it, by integrating the
 * lobe over a fine grid of light directions. */
static void reference_split_sum(float nv, float roughness, double result[2])
{
    enum
    {
        THETAS = 256,
        PHIS = 512
    };
    static const double pi = 3.14159265358979;
    double alpha = roughness * roughness, a2 = alpha * alpha;
    double view[3] = { sqrt(1 - nv * nv), 0, nv };
    result[0] = result[1] = 0;
    for (int i = 0; i < THETAS; i++) {
        double theta = (i + .5) * pi / 2 / THETAS, nl = cos(theta);
        for (int j = 0; j < PHIS; j++) {
            double phi = (j + .5) * 2 * pi / PHIS;
            double light[3] = { sin(theta) * cos(phi), sin(theta) * sin(phi), nl };
            double h[3], length = 0;
            for (int c = 0; c < 3; c++) {
                h[c] = light[c] + view[c];
                length += h[c] * h[c];
            }
            double nh = h[2] / sqrt(length);
            double vh = (h[0] * view[0] + h[2] * view[2]) / sqrt(length);
            double d = nh * nh * (a2 - 1) + 1;
            double distribution = a2 / (pi * d * d);
            double visibility = .5 / (nl * sqrt(nv * nv * (1 - a2) + a2) +
                                      nv * sqrt(nl * nl * (1 - a2) + a2));
            double weight = distribution * visibility * nl * sin(theta) *
                            (pi / 2 / THETAS) * (2 * pi / PHIS);
            double fresnel = pow(1 - vh, 5);
            result[0] += (1 - fresnel) * weight;
            result[1] += fresnel * weight;
        }
    }
}
/* The table matches a direct integration of the lobe at its texel centers,
 * and a near-mirror surface reflects all light with Schlick's Fresnel at
 * n.v. */
TEST_CASE(split_sum_table_integrates_the_specular_lobe)
{
    enum
    {
        SIZE = 32
    };
    static uint16_t texels[SIZE * SIZE * 2];
    render_sky_split_sum(SIZE, texels);
    static const int points[][2] = { { 15, 15 }, { 28, 20 }, { 6, 28 }, { 20, 10 } };
    for (int i = 0; i < 4; i++) {
        float nv = (points[i][0] + .5f) / SIZE, roughness = (points[i][1] + .5f) / SIZE;
        double expected[2];
        reference_split_sum(nv, roughness, expected);
        const uint16_t *texel = texels + (points[i][1] * SIZE + points[i][0]) * 2;
        REQUIRE(fabs(from_half(texel[0]) - expected[0]) < .02);
        REQUIRE(fabs(from_half(texel[1]) - expected[1]) < .01);
    }
    for (int i = 4; i < SIZE; i++) {
        float nv = (i + .5f) / SIZE, fresnel = powf(1 - nv, 5);
        float scale = from_half(texels[i * 2]), bias = from_half(texels[i * 2 + 1]);
        REQUIRE(fabsf(scale + bias - 1) < .02f);
        REQUIRE(fabsf(bias - fresnel) < .01f);
    }
    return true;
}
static const char *const sky_key = "content/skies/field.hdr";
static scene *environment_scene(const char *sky)
{
    Environment environment = environment_defaults();
    environment.sky = sky;
    scene_component component = { &COMPONENT_Environment, &environment };
    scene_object object = { .name = "Environment",
                            .components = &component,
                            .component_count = 1 };
    scene_spec spec = { .name = "Sky",
                        .physics = physics_defaults(),
                        .objects = &object,
                        .object_count = 1 };
    char error[128];
    scene *scene = scene_prepare(&spec, error, sizeof error);
    assert(scene);
    return scene;
}
TEST_CASE(prepared_skies_publish_with_their_generation)
{
    host = render_prepared_resources(NULL);
    render_sky sky = uniform_sky(16, (const unsigned char[4]){ 128, 64, 32, 129 });
    char error[128] = { 0 };
    REQUIRE(render_sky_prepared_write(&sky, &bytes[0], &lengths[0], error, sizeof error));
    player_asset record = {
        .name = sky_key,
        .kind = PLAYER_ASSET_SKY,
        .data = bytes[0],
        .length = lengths[0],
        .content_hash = player_asset_fingerprint(bytes[0], lengths[0])
    };
    size_t remaining = 2 * 1024 * 1024;
    /* Staging charges the texels; a byte less stages nothing. */
    pending = host.stage(host.context, &record, 1, &remaining, error, sizeof error);
    REQUIRE(pending && remaining < 2 * 1024 * 1024 - 16 * 8 * 4);
    host.discard(host.context, pending);
    pending = NULL;
    size_t tight = 2 * 1024 * 1024 - remaining - 1;
    REQUIRE(!host.stage(host.context, &record, 1, &tight, error, sizeof error));
    REQUIRE(strstr(error, "Prepared sky byte count or ownership budget differs"));
    REQUIRE(tight == 2 * 1024 * 1024 - remaining - 1);
    /* Truncated, mislabeled or duplicated skies stage nothing. */
    remaining = 2 * 1024 * 1024;
    player_asset bad = record;
    bad.length--;
    bad.content_hash = 0;
    REQUIRE(!host.stage(host.context, &bad, 1, &remaining, error, sizeof error));
    REQUIRE(strstr(error, "truncated"));
    bad = record;
    bad.name = "content/../field.hdr";
    REQUIRE(!host.stage(host.context, &bad, 1, &remaining, error, sizeof error));
    REQUIRE(strstr(error, "without parent traversal"));
    REQUIRE(
        !host.stage(host.context, (player_asset[]){ record, record }, 2, &remaining, error, sizeof error)
    );
    REQUIRE(remaining == 2 * 1024 * 1024);
    pending = host.stage(host.context, &record, 1, &remaining, error, sizeof error);
    REQUIRE(pending);
    REQUIRE(!render_sky_find(sky_key));
    /* Scenes may name only skies their generation carries. */
    scene *named = environment_scene(sky_key),
          *other = environment_scene("content/skies/dusk.hdr");
    REQUIRE(host.scene(host.context, pending, named, error, sizeof error));
    REQUIRE(!host.scene(host.context, pending, other, error, sizeof error));
    REQUIRE(strstr(error, "sky is absent"));
    scene_free(named);
    scene_free(other);
    host.commit(host.context, pending, NULL);
    current_generation = pending;
    pending = NULL;
    const render_sky *resident = render_sky_find(sky_key);
    REQUIRE(
        resident && resident->width == 16 && resident->height == 8 && resident->identity
    );
    REQUIRE(!memcmp(resident->texels, sky.texels, 16 * 8 * 4));
    render_sky_free(&sky);
    host.retire(host.context, current_generation);
    current_generation = NULL;
    REQUIRE(!render_sky_find(sky_key));
    return true;
}

/* Discarding drops every span not yet taken, wherever the ring starts, so
 * none is taken later and the span starts over empty. */
TEST_CASE(discarded_gpu_spans_leave_nothing_to_take)
{
    render_gpu_span span = { .pending = { [0] = { 3, 4 }, [7] = { 1, 2 } },
                             .head = 7,
                             .count = 2,
                             .first = 3 };
    render_gpu_span_discard(&span);
    REQUIRE(!span.count && !span.head && !span.first);
    double ms;
    REQUIRE(!render_gpu_span_take(&span, &ms));
    return true;
}

void test_render_resources()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(prepared_renderer_resources_publish_atomically_and_retain_retired_tokens);
    RUN_TEST_CASE(sprite_images_join_the_program_set_and_sample_by_variant);
    RUN_TEST_CASE(sky_faces_follow_the_gpu_cube_map_layout);
    RUN_TEST_CASE(sky_faces_hold_texels_as_halves);
    RUN_TEST_CASE(sky_irradiance_integrates_the_cosine_lobe);
    RUN_TEST_CASE(sky_irradiance_saturates_with_the_faces);
    RUN_TEST_CASE(split_sum_table_integrates_the_specular_lobe);
    RUN_TEST_CASE(prepared_skies_publish_with_their_generation);
    RUN_TEST_CASE(discarded_gpu_spans_leave_nothing_to_take);
    pu_clear_setup();
}
