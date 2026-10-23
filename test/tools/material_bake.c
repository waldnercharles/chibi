#include "assets/project.h"
#include "platform/cute.h"
#include "support/asset_document.h"
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
static const char *material[] = {
    "package asset\n"
    "\n"
    "import data \"game:assets\"\n"
    "\n"
    "pub Build :: () -> byte[] {\n"
    "    return data.Encode(\n"
    "        data.Object([\n"
    "            data.Field.Named(\"version\", data.Value.UInt(3)),\n"
    "            data.Field.Named(\"name\", data.Value.Text(\"Bake\")),\n"
    "            data.Field.Named(\"domain\", data.Value.Text(\"material\")),\n"
    "            data.Field.Named(\"id_counter\", data.Value.UInt(1000)),\n"
    "            data.Field.Named(\"graphs\", data.List([\n"
    "                data.Object([\n"
    "                    data.Field.Named(\"kind\", "
    "data.Value.Text(\"event\")),\n"
    "                    data.Field.Named(\"name\", data.Value.Text(\"\")),\n"
    "                    data.Field.Named(\"nodes\", data.List([\n"
    "                        data.Object([\n"
    "                            data.Field.Named(\"id\", "
    "data.Value.UInt(1)),\n"
    "                            data.Field.Named(\"type\", "
    "data.Value.Text(\"Shader Surface Output\"))\n"
    "                        ]),\n"
    "                        data.Object([\n"
    "                            data.Field.Named(\"id\", "
    "data.Value.UInt(100)),\n"
    "                            data.Field.Named(\"type\", "
    "data.Value.Text(\"Call Pure Function\")),\n"
    "                            data.Field.Named(\"reference_id\", "
    "data.Value.UInt(300)),\n"
    "                            data.Field.Named(\"reference_name\", "
    "data.Value.Text(\"Local\")),\n"
    "                            data.Field.Named(\"outputs\", data.Object([\n"
    "                                data.Field.Named(\"Result\", "
    "data.Object([\n"
    "                                    data.Field.Named(\"id\", "
    "data.Value.UInt(101)),\n"
    "                                    data.Field.Named(\"declaration_id\", "
    "data.Value.UInt(401)),\n"
    "                                    data.Field.Named(\"type\", "
    "data.Value.Text(\"float\"))\n"
    "                                ]))\n"
    "                            ]))\n"
    "                        ])\n"
    "                    ])),\n"
    "                    data.Field.Named(\"edges\", data.List([\n"
    "                        data.Object([\n"
    "                            data.Field.Named(\"from\", "
    "data.Value.Text(\"100.Result\")),\n"
    "                            data.Field.Named(\"to\", "
    "data.Value.Text(\"1.Roughness\"))\n"
    "                        ])\n"
    "                    ]))\n"
    "                ]),\n"
    "                data.Object([\n"
    "                    data.Field.Named(\"id\", data.Value.UInt(300)),\n"
    "                    data.Field.Named(\"kind\", "
    "data.Value.Text(\"function\")),\n"
    "                    data.Field.Named(\"name\", "
    "data.Value.Text(\"Local\")),\n"
    "                    data.Field.Named(\"entry_id\", "
    "data.Value.UInt(301)),\n"
    "                    data.Field.Named(\"return_id\", "
    "data.Value.UInt(400)),\n"
    "                    data.Field.Named(\"call_depth_limit\", "
    "data.Value.UInt(64)),\n"
    "                    data.Field.Named(\"nodes\", data.List([\n",
    "                        data.Object([\n"
    "                            data.Field.Named(\"id\", "
    "data.Value.UInt(301)),\n"
    "                            data.Field.Named(\"type\", "
    "data.Value.Text(\"Function Entry\"))\n"
    "                        ]),\n"
    "                        data.Object([\n"
    "                            data.Field.Named(\"id\", "
    "data.Value.UInt(400)),\n"
    "                            data.Field.Named(\"type\", "
    "data.Value.Text(\"Return\")),\n"
    "                            data.Field.Named(\"inputs\", data.List([\n"
    "                                data.Object([\n"
    "                                    data.Field.Named(\"id\", "
    "data.Value.UInt(401)),\n"
    "                                    data.Field.Named(\"name\", "
    "data.Value.Text(\"Result\")),\n"
    "                                    data.Field.Named(\"type\", "
    "data.Value.Text(\"float\")),\n"
    "                                    data.Field.Named(\"kind\", "
    "data.Value.Text(\"data\"))\n"
    "                                ])\n"
    "                            ]))\n"
    "                        ]),\n"
    "                        data.Object([\n"
    "                            data.Field.Named(\"id\", "
    "data.Value.UInt(500)),\n"
    "                            data.Field.Named(\"type\", "
    "data.Value.Text(\"Shader Parameter\")),\n"
    "                            data.Field.Named(\"reference_name\", "
    "data.Value.Text(\"Rough\")),\n"
    "                            data.Field.Named(\"outputs\", data.Object([\n"
    "                                data.Field.Named(\"Value\", "
    "data.Object([\n"
    "                                    data.Field.Named(\"default\", "
    "data.Value.Real(0.25))\n"
    "                                ]))\n"
    "                            ]))\n"
    "                        ])\n"
    "                    ])),\n"
    "                    data.Field.Named(\"edges\", data.List([\n"
    "                        data.Object([\n"
    "                            data.Field.Named(\"from\", "
    "data.Value.Text(\"500.Value\")),\n"
    "                            data.Field.Named(\"to\", "
    "data.Value.Text(\"400.Result\"))\n"
    "                        ])\n"
    "                    ]))\n"
    "                ])\n"
    "            ]))\n"
    "        ])\n"
    "    )\n"
    "}\n"
};
static const char *image_material =
    "package asset\n"
    "\n"
    "import data \"game:assets\"\n"
    "\n"
    "pub Build :: () -> byte[] {\n"
    "    return data.Encode(\n"
    "        data.Object([\n"
    "            data.Field.Named(\"version\", data.Value.UInt(3)),\n"
    "            data.Field.Named(\"name\", data.Value.Text(\"Image\")),\n"
    "            data.Field.Named(\"domain\", data.Value.Text(\"material\")),\n"
    "            data.Field.Named(\"id_counter\", data.Value.UInt(200)),\n"
    "            data.Field.Named(\"graphs\", data.List([\n"
    "                data.Object([\n"
    "                    data.Field.Named(\"kind\", "
    "data.Value.Text(\"event\")),\n"
    "                    data.Field.Named(\"name\", data.Value.Text(\"\")),\n"
    "                    data.Field.Named(\"nodes\", data.List([\n"
    "                        data.Object([\n"
    "                            data.Field.Named(\"id\", "
    "data.Value.UInt(1)),\n"
    "                            data.Field.Named(\"type\", "
    "data.Value.Text(\"Shader Surface Output\"))\n"
    "                        ]),\n"
    "                        data.Object([\n"
    "                            data.Field.Named(\"id\", "
    "data.Value.UInt(100)),\n"
    "                            data.Field.Named(\"type\", "
    "data.Value.Text(\"Shader Image Texture\")),\n"
    "                            data.Field.Named(\"inputs\", data.Object([\n"
    "                                data.Field.Named(\"Image\", "
    "data.Value.Text(\"content/images/Test.png\")),\n"
    "                                data.Field.Named(\"Color Space\", "
    "data.Value.UInt(1))\n"
    "                            ]))\n"
    "                        ])\n"
    "                    ])),\n"
    "                    data.Field.Named(\"edges\", data.List([\n"
    "                        data.Object([\n"
    "                            data.Field.Named(\"from\", "
    "data.Value.Text(\"100.Color\")),\n"
    "                            data.Field.Named(\"to\", "
    "data.Value.Text(\"1.Base Color\"))\n"
    "                        ])\n"
    "                    ]))\n"
    "                ])\n"
    "            ]))\n"
    "        ])\n"
    "    )\n"
    "}\n";

static void write_at(const char *name, const char *text)
{
    char path[256];
    snprintf(path, sizeof path, "%s/%s", root, name);
    tool_write(path, text);
}
static void write_many_images()
{
    av_doc *doc = av_doc_new();
    av_value *root_value = av_obj(doc);
    av_doc_set_root(doc, root_value);
    av_obj_add_uint(doc, root_value, "version", 3);
    av_obj_add_str(doc, root_value, "name", "TooMany");
    av_obj_add_str(doc, root_value, "domain", "material");
    av_obj_add_uint(doc, root_value, "id_counter", 10000);
    av_value *graph = av_arr_add_obj(doc, av_obj_add_arr(doc, root_value, "graphs"));
    av_obj_add_str(doc, graph, "kind", "event");
    av_obj_add_str(doc, graph, "name", "");
    av_value *nodes = av_obj_add_arr(doc, graph, "nodes");
    av_value *output = av_arr_add_obj(doc, nodes);
    av_obj_add_uint(doc, output, "id", 1);
    av_obj_add_str(doc, output, "type", "Shader Surface Output");
    for (int i = 0; i < 9; i++) {
        av_value *node = av_arr_add_obj(doc, nodes);
        av_obj_add_uint(doc, node, "id", 1000 + i * 100);
        av_obj_add_str(doc, node, "type", "Shader Image Texture");
        char image[128];
        snprintf(image, sizeof image, "content/images/missing%d.png", i);
        av_obj_add_str(doc, av_obj_add_obj(doc, node, "inputs"), "Image", image);
    }
    for (int i = 0; i < 8; i++) {
        av_value *node = av_arr_add_obj(doc, nodes);
        av_obj_add_uint(doc, node, "id", 3000 + i * 100);
        av_obj_add_str(doc, node, "type", "Shader Add");
        av_value *inputs = av_obj_add_obj(doc, node, "inputs"),
                 *outputs = av_obj_add_obj(doc, node, "outputs");
        const char *names[] = { "A", "B", "Value" };
        for (int j = 0; j < 3; j++) {
            av_value *port = av_obj_add_obj(doc, j < 2 ? inputs : outputs, names[j]);
            av_obj_add_str(doc, port, "type", "struct");
            av_obj_add_str(doc, port, "type_name", "ShaderVec3");
        }
    }
    av_value *edges = av_obj_add_arr(doc, graph, "edges"),
             *edge = av_arr_add_obj(doc, edges);
    av_obj_add_str(doc, edge, "from", "1000.Color");
    av_obj_add_str(doc, edge, "to", "3000.A");
    for (int i = 0; i < 8; i++) {
        char from[64], to[64];
        edge = av_arr_add_obj(doc, edges);
        snprintf(from, sizeof from, "%d.Color", 1100 + i * 100);
        snprintf(to, sizeof to, "%d.B", 3000 + i * 100);
        av_obj_add_str(doc, edge, "from", from);
        av_obj_add_str(doc, edge, "to", to);
        if (i < 7) {
            edge = av_arr_add_obj(doc, edges);
            snprintf(from, sizeof from, "%d.Value", 3000 + i * 100);
            snprintf(to, sizeof to, "%d.A", 3100 + i * 100);
            av_obj_add_str(doc, edge, "from", from);
            av_obj_add_str(doc, edge, "to", to);
        }
    }
    edge = av_arr_add_obj(doc, edges);
    av_obj_add_str(doc, edge, "from", "3700.Value");
    av_obj_add_str(doc, edge, "to", "1.Base Color");
    char *source = asset_document_write(root_value);
    assert(source);
    write_at("content/materials/TooMany.chibi", source);
    free(source);
    av_doc_free(doc);
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
    char manifest[16384];
    snprintf(
        manifest,
        sizeof manifest,
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"version\", data.Value.Int(%d)),\n"
        "            data.Field.Named(\"name\", data.Value.Text(\"Bake "
        "test\")),\n"
        "            data.Field.Named(\"content\", "
        "data.Value.Text(\"content\"))\n"
        "        ])\n"
        "    )\n"
        "}\n",
        CHIBI_PROJECT_VERSION
    );
    write_at("project.chibi", manifest);
    char material_source[8192];
    snprintf(material_source, sizeof material_source, "%s%s", material[0], material[1]);
    write_at("content/materials/Bake.chibi", material_source);
    write_at("content/materials/Image.chibi", image_material);
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
