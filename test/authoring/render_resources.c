#include "render/shader_catalog.h"
#include "render/shader_images.h"
#include "render/shader_publish.h"
#include "runtime/shaders/prepared.h"
#include "runtime/ui/documents.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "third_party/pico_headers/pico_unit.h"

#include <stdlib.h>
#include <string.h>

/* Fixed CPU snapshots only: no app/window, texture upload, draw, files, world
 * or source parsing. Cute's default pixel allocator delegates to malloc/free. */
static game_player_resource_host host;
static shader_prepared_asset producer;
static game_ui_document document;
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
    game_ui_document_free(&document);
    for (int i = 0; i < 3; i++) {
        free(bytes[i]);
        bytes[i] = NULL;
    }
}
static bool inputs(bool replacement, game_player_asset records[3])
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
    apush(
        document.resources,
        ((game_ui_resource){ .kind = GAME_UI_RESOURCE_IMAGE, .key = xstrdup(pixel_key) })
    );
    char error[128];
    REQUIRE(
        game_ui_document_prepared_write(&document, &bytes[2], &lengths[2], error, sizeof error)
    );
    shader_prepared_asset_free(&producer);
    game_ui_document_free(&document);
    memset(pixels, 0, sizeof pixels);
    const game_player_asset_kind kinds[] = { GAME_PLAYER_ASSET_SHADER,
                                             GAME_PLAYER_ASSET_IMAGE,
                                             GAME_PLAYER_ASSET_UI };
    const char *const names[] = { "OwnedShader", pixel_key, "OwnedPage" };
    for (int i = 0; i < 3; i++) {
        REQUIRE(lengths[i] > 0 && lengths[i] < 8192);
        records[i] = (game_player_asset){
            .name = names[i],
            .kind = kinds[i],
            .data = bytes[i],
            .length = lengths[i],
            .content_hash = game_player_asset_fingerprint(bytes[i], lengths[i])
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
    const game_ui_document *page = game_ui_documents_find(game_ui_documents_current(), "OwnedPage");
    REQUIRE(page && !strcmp(page->markup, replacement ? "<img id='new'/>" : "<img id='old'/>"));
    return true;
}
TEST_CASE(prepared_renderer_resources_publish_atomically_and_retain_retired_tokens)
{
    host = render_prepared_resources(NULL);
    shader_prepared_asset_init(&producer);
    REQUIRE(
        !render_image_source_count() &&
        !game_ui_documents_count(game_ui_documents_current())
    );
    REQUIRE(!render_shader_find("OwnedShader"));
    game_player_asset records[3];
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
    const game_ui_document *old_page = game_ui_documents_find(game_ui_documents_current(), "OwnedPage");
    shader_image_source resource = { .key = (char *)pixel_key,
                                     .color_space = SHADER_IMAGE_LINEAR,
                                     .filter = SHADER_IMAGE_NEAREST,
                                     .wrap = SHADER_IMAGE_CLAMP };
    shader_image_view old_pixels;
    REQUIRE(render_image_resolve(NULL, &resource, &old_pixels));
    REQUIRE(inputs(true, records));
    unsigned revision = render_shader_revision();
    game_player_asset bad[3];
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
    game_player_asset missing[] = { records[0], records[2] };
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
    REQUIRE(
        !game_ui_documents_active() &&
        !game_ui_documents_count(game_ui_documents_current())
    );
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
void test_render_resources()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(prepared_renderer_resources_publish_atomically_and_retain_retired_tokens);
    RUN_TEST_CASE(sprite_images_join_the_program_set_and_sample_by_variant);
    pu_clear_setup();
}
