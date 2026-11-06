#include "assets/project.h"
#include "platform/cute.h"
#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <yyjson.h>

static const char *program;
static char root[128];
static yyjson_doc *replies[9];
static const char *material =
    "version 3\nname \"Bake\"\ndomain \"material\"\nid_counter 1000\n"
    "(array)graphs {\n - {\n kind \"event\"\n name \"\"\n (array)nodes {\n"
    " - {\n id 1\n type \"Shader Surface Output\"\n }\n"
    " - {\n id 100\n type \"Call Pure Function\"\n reference_id 300\n "
    "reference_name \"Local\"\n outputs {\n Result id=101 declaration_id=401 "
    "type=\"number\"\n }\n }\n"
    " }\n (array)edges {\n - from=\"100.Result\" to=\"1.Roughness\"\n }\n }\n"
    " - {\n id 300\n kind \"function\"\n name \"Local\"\n entry_id 301\n "
    "return_id 400\n call_depth_limit 64\n"
    " (array)nodes {\n - {\n id 301\n type \"Function Entry\"\n }\n"
    " - {\n id 400\n type \"Return\"\n (array)inputs {\n - id=401 "
    "name=\"Result\" type=\"number\" kind=\"data\"\n }\n }\n"
    " - {\n id 500\n type \"Shader Parameter\"\n reference_name \"Rough\"\n "
    "outputs {\n Value default=0.25\n }\n }\n"
    " }\n (array)edges {\n - from=\"500.Value\" to=\"400.Result\"\n }\n }\n}\n";
static const char *image_material =
    "version 3\nname \"Image\"\ndomain \"material\"\nid_counter 200\n"
    "(array)graphs {\n - {\n kind \"event\"\n name \"\"\n (array)nodes {\n"
    " - {\n id 1\n type \"Shader Surface Output\"\n }\n"
    " - {\n id 100\n type \"Shader Image Texture\"\n inputs {\n Image "
    "\"content/images/Test.png\"\n \"Color Space\" 1\n }\n }\n"
    " }\n (array)edges {\n - from=\"100.Color\" to=\"1.Base Color\"\n }\n "
    "}\n}\n";

static void write_at(const char *name, const char *text)
{
    char path[256];
    snprintf(path, sizeof path, "%s/%s", root, name);
    tool_write(path, text);
}
static void write_many_images()
{
    char path[256];
    snprintf(path, sizeof path, "%s/content/materials/TooMany.kdl", root);
    FILE *file = fopen(path, "wb");
    assert(file);
    fputs("version 3\nname \"TooMany\"\ndomain \"material\"\nid_counter 10000\n(array)graphs {\n - {\n kind \"event\"\n name \"\"\n (array)nodes {\n - {\n id 1\n type \"Shader Surface Output\"\n }\n", file);
    for (int i = 0; i < 9; i++) {
        fprintf(
            file,
            " - {\n id %d\n type \"Shader Image Texture\"\n inputs {\n Image "
            "\"content/images/missing%d.png\"\n }\n }\n",
            1000 + i * 100,
            i
        );
    }
    for (int i = 0; i < 8; i++) {
        fprintf(
            file,
            " - {\n id %d\n type \"Shader Add\"\n inputs {\n A type=\"struct\" "
            "type_name=\"ShaderVec3\"\n B type=\"struct\" "
            "type_name=\"ShaderVec3\"\n }\n outputs {\n Value type=\"struct\" "
            "type_name=\"ShaderVec3\"\n }\n }\n",
            3000 + i * 100
        );
    }
    fputs(" }\n (array)edges {\n - from=\"1000.Color\" to=\"3000.A\"\n", file);
    for (int i = 0; i < 8; i++) {
        fprintf(file, " - from=\"%d.Color\" to=\"%d.B\"\n", 1100 + i * 100, 3000 + i * 100);
        if (i < 7) {
            fprintf(file, " - from=\"%d.Value\" to=\"%d.A\"\n", 3000 + i * 100, 3100 + i * 100);
        }
    }
    fputs(" - from=\"3700.Value\" to=\"1.Base Color\"\n }\n }\n}\n", file);
    assert(!fclose(file));
}
static yyjson_val *value(int i)
{
    return yyjson_doc_get_root(replies[i]);
}
static yyjson_val *channel(int i)
{
    return yyjson_arr_get(yyjson_obj_get(yyjson_obj_get(value(i), "result"), "channels"), 0);
}
static void teardown()
{
    for (int i = 0; i < 9; i++) {
        yyjson_doc_free(replies[i]);
        replies[i] = NULL;
    }
    if (root[0]) {
        tool_remove_tree(root);
    }
    root[0] = 0;
}
TEST_CASE(offline_material_bake_contract)
{
    snprintf(root, sizeof root, "/tmp/chibi-material-bake-XXXXXX");
    REQUIRE(mkdtemp(root));
    char path[256];
    const char *dirs[] = { "content", "content/materials", "content/images" };
    for (int i = 0; i < 3; i++) {
        snprintf(path, sizeof path, "%s/%s", root, dirs[i]);
        REQUIRE(!mkdir(path, 0700));
    }
    char manifest[160];
    snprintf(manifest, sizeof manifest, "version %d\nname \"Bake test\"\ncontent \"content\"\n", CHIBI_PROJECT_VERSION);
    write_at("project.kdl", manifest);
    write_at("content/materials/Bake.kdl", material);
    write_at("content/materials/Image.kdl", image_material);
    write_many_images();
    CF_Pixel pixel = { .r = 255, .g = 64, .b = 0, .a = 255 };
    CF_Image image = { 1, 1, &pixel };
    void *png = NULL;
    int png_size = 0;
    REQUIRE(!cf_is_error(cf_image_save_png_to_memory(&image, &png, &png_size)));
    snprintf(path, sizeof path, "%s/content/images/Test.png", root);
    FILE *output = fopen(path, "wb");
    REQUIRE(output);
    REQUIRE(fwrite(png, 1, (size_t)png_size, output) == (size_t)png_size);
    cf_free(png);
    REQUIRE(!fclose(output));
    write_at(
        "requests.jsonl",
        "{\"contract\":1,\"operation\":\"bake_material\",\"kind\":\"material\","
        "\"name\":\"Bake\",\"width\":2,\"height\":2,\"channel\":\"roughness\"}"
        "\n"
        "{\"contract\":1,\"operation\":\"bake_material\",\"kind\":\"material\","
        "\"name\":\"Bake\",\"width\":2,\"height\":2,\"channel\":\"roughness\"}"
        "\n"
        "{\"contract\":1,\"operation\":\"bake_material\",\"kind\":\"material\","
        "\"name\":\"Bake\",\"width\":2,\"height\":2,\"channel\":\"roughness\","
        "\"overrides\":[{\"asset\":\"Bake\",\"scope\":\"\",\"parameter_id\":"
        "500,"
        "\"value\":[0.75]}]}\n"
        "{\"contract\":1,\"operation\":\"bake_material\",\"kind\":\"material\","
        "\"name\":\"Bake\",\"uv_max\":[0,0]}\n"
        "{\"contract\":1,\"operation\":\"bake_material\",\"kind\":\"material\","
        "\"name\":\"Bake\",\"channel\":\"absent\"}\n"
        "{\"contract\":1,\"operation\":\"bake_material\",\"kind\":\"material\","
        "\"name\":\"Image\",\"width\":2,\"height\":2,\"channel\":\"base_"
        "color\"}\n"
        "{\"contract\":1,\"operation\":\"bake_material\",\"kind\":\"material\","
        "\"name\":\"Bake\",\"width\":129}\n"
        "{\"contract\":1,\"operation\":\"bake_material\",\"kind\":\"material\","
        "\"name\":\"Bake\",\"overrides\":[{\"asset\":\"Bake\",\"scope\":"
        "\"unsupported\",\"parameter_id\":500,\"value\":[0.75]}]}\n"

        "{\"contract\":1,\"operation\":\"bake_material\",\"kind\":\"material\","
        "\"name\":\"Bake\",\"width\":1,\"height\":1}\n"
    );
    char log[256];
    snprintf(log, sizeof log, "%s/output", root);
    const char *args[] = { "/bin/sh",
                           "-c",
                           "exec \"$0\" --project-root . < requests.jsonl",
                           program,
                           NULL };
    tool_run(args, root, log);
    char *text = tool_read(log);
    REQUIRE(text);
    int count = 0;
    for (const char *line = text; *line;) {
        size_t length = strcspn(line, "\n");
        if (!strncmp(line, "{\"contract\"", 11)) {
            REQUIRE(count < 9);
            replies[count++] = yyjson_read(line, length, 0);
        }
        line += length + (line[length] == '\n');
    }
    if (count != 9 || !channel(0)) {
        fputs(text, stderr);
    }
    free(text);
    REQUIRE(count == 9);
    REQUIRE(yyjson_is_obj(channel(0)));
    yyjson_val *parameter = yyjson_arr_get(
        yyjson_obj_get(yyjson_obj_get(value(0), "result"), "parameters"),
        0
    );
    REQUIRE(yyjson_get_uint(yyjson_obj_get(parameter, "parameter_id")) == 500);
    REQUIRE(!strcmp(yyjson_get_str(yyjson_obj_get(parameter, "asset")), "Bake"));
    REQUIRE(!strcmp(yyjson_get_str(yyjson_obj_get(parameter, "scope")), ""));
    REQUIRE(yyjson_arr_size(yyjson_obj_get(channel(0), "data")) == 4);
    REQUIRE(yyjson_get_num(yyjson_arr_get(yyjson_obj_get(channel(0), "data"), 0)) == .25);
    REQUIRE(!strcmp(
        yyjson_get_str(yyjson_obj_get(channel(0), "hash")),
        yyjson_get_str(yyjson_obj_get(channel(1), "hash"))
    ));
    REQUIRE(yyjson_get_num(yyjson_arr_get(yyjson_obj_get(channel(2), "data"), 0)) == .75);
    REQUIRE(strcmp(
        yyjson_get_str(yyjson_obj_get(channel(0), "hash")),
        yyjson_get_str(yyjson_obj_get(channel(2), "hash"))
    ));
    REQUIRE(yyjson_obj_get(value(3), "error"));
    REQUIRE(yyjson_obj_get(value(4), "error"));
    REQUIRE(yyjson_arr_size(yyjson_obj_get(channel(5), "data")) == 12);
    REQUIRE(yyjson_get_num(yyjson_arr_get(yyjson_obj_get(channel(5), "data"), 0)) == 1.0);
    REQUIRE(yyjson_get_num(yyjson_arr_get(yyjson_obj_get(channel(5), "data"), 2)) == 0.0);
    REQUIRE(yyjson_obj_get(value(6), "error"));
    REQUIRE(yyjson_obj_get(value(7), "error"));
    REQUIRE(yyjson_arr_size(yyjson_obj_get(yyjson_obj_get(value(8), "result"), "channels")) == 9);
    char root_hash[17], pixel_hash[17], output_hash[17];
    yyjson_val *image_result = yyjson_obj_get(value(5), "result");
    snprintf(root_hash, sizeof root_hash, "%s", yyjson_get_str(yyjson_obj_get(image_result, "hash")));
    snprintf(
        pixel_hash,
        sizeof pixel_hash,
        "%s",
        yyjson_get_str(yyjson_obj_get(
            yyjson_arr_get(yyjson_obj_get(image_result, "image_dependencies"), 0),
            "pixel_hash"
        ))
    );
    snprintf(output_hash, sizeof output_hash, "%s", yyjson_get_str(yyjson_obj_get(channel(5), "hash")));
    REQUIRE(yyjson_get_str(yyjson_obj_get(image_result, "linked_graph_hash")));
    for (int i = 0; i < count; i++) {
        yyjson_doc_free(replies[i]);
        replies[i] = NULL;
    }
    pixel.r = 0;
    pixel.g = 255;
    png = NULL;
    REQUIRE(!cf_is_error(cf_image_save_png_to_memory(&image, &png, &png_size)));
    output = fopen(path, "wb");
    REQUIRE(output);
    REQUIRE(fwrite(png, 1, (size_t)png_size, output) == (size_t)png_size);
    cf_free(png);
    REQUIRE(!fclose(output));
    write_at("requests.jsonl", "{\"contract\":1,\"operation\":\"bake_material\",\"kind\":\"material\",\"name\":\"Image\",\"width\":2,\"height\":2,\"channel\":\"base_color\"}\n");
    REQUIRE(tool_run(args, root, log) == 0);
    text = tool_read(log);
    REQUIRE(text);
    replies[0] = yyjson_read(text, strlen(text), 0);
    free(text);
    REQUIRE(replies[0]);
    image_result = yyjson_obj_get(value(0), "result");
    REQUIRE(!strcmp(root_hash, yyjson_get_str(yyjson_obj_get(image_result, "hash"))));
    REQUIRE(strcmp(
        pixel_hash,
        yyjson_get_str(
            yyjson_obj_get(yyjson_arr_get(yyjson_obj_get(image_result, "image_dependencies"), 0), "pixel_hash")
        )
    ));
    REQUIRE(strcmp(output_hash, yyjson_get_str(yyjson_obj_get(channel(0), "hash"))));
    REQUIRE(yyjson_get_num(yyjson_arr_get(yyjson_obj_get(channel(0), "data"), 0)) == 0.0);
    yyjson_doc_free(replies[0]);
    replies[0] = NULL;
    REQUIRE(!unlink(path));
    write_at(
        "requests.jsonl",
        "{\"contract\":1,\"operation\":\"bake_material\",\"kind\":\"material\","
        "\"name\":\"Image\",\"width\":2,\"height\":2}\n"
        "{\"contract\":1,\"operation\":\"bake_material\",\"kind\":\"material\","
        "\"name\":\"TooMany\",\"width\":1,\"height\":1}\n"
    );
    tool_run(args, root, log);
    text = tool_read(log);
    REQUIRE(text && strstr(text, "\"error\"") && strstr(text, "content/images/Test.png"));
    REQUIRE(strstr(text, "exceeds 8 distinct image dependency keys"));
    free(text);
    return true;
}
void test_tool_material_bake(const char *binary)
{
    program = binary;
    pu_setup(NULL, teardown);
    RUN_TEST_CASE(offline_material_bake_contract);
    pu_setup(NULL, NULL);
}
