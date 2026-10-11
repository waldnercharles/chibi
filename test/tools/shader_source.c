#include "authoring/shader_source.h"

#include "authoring/shader_prepare.h"
#include "render/shader_catalog_records.h"
#include "support/array.h"
#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char root[128], content[256], descriptor[512], source[512], other[512];
static shader_prepared_asset loaded;
static char error[4096];
static const char *const
    declaration = "version 1\nname \"Source.Cloud\"\nkind \"particle\"\n"
                  "source \"Cloud.glsl\"\nentry \"cloud\"\n"
                  "(array)includes \"Shared.glsl\"\n"
                  "parameters {\n"
                  " Spacing { type \"float\"; default 2; }\n"
                  " Shift { type \"vec2\"; (array)default 1 3; }\n"
                  " Tint { type \"vec3\"; (array)default 0.25 0.5 1; }\n"
                  "}\n";
static const char *const
    code = "#include \"Shared.glsl\"\n"
           "ChibiParticle cloud(ChibiParticleInput p, ChibiParameters u) {\n"
           " ChibiParticle result=chibi_particle();\n"
           " result.position=vec3(p.index*u.Spacing,u.Shift.y,p.time);\n"
           " result.color=vec4(tint(u.Tint),1.0);\n"
           " return result;\n}\n";

static void cleanup()
{
    shader_prepared_asset_free(&loaded);
    authoring_shader_catalog_clear();
    if (*root) {
        tool_remove_tree(root);
        *root = 0;
    }
}
static bool fixture()
{
    snprintf(root, sizeof root, "/tmp/chibi-shader-source-XXXXXX");
    REQUIRE(mkdtemp(root));
    snprintf(content, sizeof content, "%s/content", root);
    REQUIRE(!mkdir(content, 0700));
    char folder[512];
    snprintf(folder, sizeof folder, "%s/shaders", content);
    REQUIRE(!mkdir(folder, 0700));
    snprintf(descriptor, sizeof descriptor, "%s/shaders/Cloud.kdl", content);
    snprintf(source, sizeof source, "%s/shaders/Cloud.glsl", content);
    snprintf(other, sizeof other, "%s/shaders/Other.kdl", content);
    char include[512];
    snprintf(include, sizeof include, "%s/shaders/Shared.glsl", content);
    tool_write(descriptor, declaration);
    tool_write(source, code);
    tool_write(include, "vec3 tint(vec3 color) { return color; }\n");
    return true;
}

TEST_CASE(shader_source_metadata_binds_scalars_vectors_and_bundled_files)
{
    REQUIRE(fixture());
    REQUIRE(authoring_shader_source_load(content, descriptor, NULL, &loaded, error, sizeof error));
    REQUIRE(loaded.kind == SHADER_ASSET_PARTICLE_TEXT);
    REQUIRE(!strcmp(loaded.name, "Source.Cloud"));
    REQUIRE(!strcmp(loaded.text.path, "shaders/Cloud.glsl"));
    REQUIRE(!strcmp(loaded.text.entry, "cloud"));
    REQUIRE(acount(loaded.text.includes) == 1);
    REQUIRE(!strcmp(loaded.text.includes[0].path, "Shared.glsl"));
    REQUIRE(!strcmp(loaded.names[0], "Index") && loaded.widths[0] == 1);
    REQUIRE(!strcmp(loaded.names[1], "Count") && loaded.widths[1] == 1);
    REQUIRE(!strcmp(loaded.names[2], "Seed") && loaded.widths[2] == 1);
    REQUIRE(!strcmp(loaded.names[3], "Spacing") && loaded.widths[3] == 1);
    REQUIRE(loaded.defaults[3][0] == 2);
    REQUIRE(loaded.widths[4] == 2 && loaded.defaults[4][1] == 3);
    REQUIRE(loaded.widths[5] == 3 && loaded.defaults[5][2] == 1);
    REQUIRE(!loaded.names[6]);
    return true;
}

TEST_CASE(shader_source_rejects_invalid_metadata_and_project_escape_without_replacement)
{
    REQUIRE(fixture());
    REQUIRE(authoring_shader_source_load(content, descriptor, NULL, &loaded, error, sizeof error));
    const char *invalid[] = {
        "version 2\nname \"Wrong\"\nkind \"particle\"\nsource "
        "\"Cloud.glsl\"\nentry \"cloud\"\n",
        "version 4294967297\nname \"Wrong\"\nkind \"particle\"\nsource "
        "\"Cloud.glsl\"\nentry \"cloud\"\n",
        "version 1\nname \"Wrong\"\nkind \"material\"\nsource "
        "\"Cloud.glsl\"\nentry \"cloud\"\n",
        "version 1\nname \"Wrong\"\nkind \"particle\"\nsource "
        "\"Cloud.glsl\"\nentry \"bad-name\"\n",
        "version 1\nname \"Wrong\"\nkind \"particle\"\nsource "
        "\"Cloud.glsl\"\nentry \"cloud\"\nparameters { Index { type \"float\"; "
        "default 2; }; }\n",
        "version 1\nname \"Wrong\"\nkind \"particle\"\nsource "
        "\"Cloud.glsl\"\nentry \"cloud\"\nparameters { Tint { type \"vec3\"; "
        "(array)default 1 2; }; }\n",
    };
    for (int i = 0; i < (int)(sizeof invalid / sizeof *invalid); i++) {
        REQUIRE(
            !authoring_shader_source_load(content, descriptor, invalid[i], &loaded, error, sizeof error)
        );
        REQUIRE(!strcmp(loaded.name, "Source.Cloud"));
    }
    char outside[256], link[512];
    snprintf(outside, sizeof outside, "%s/Outside.glsl", root);
    tool_write(outside, code);
    snprintf(link, sizeof link, "%s/shaders/Escape.glsl", content);
    REQUIRE(!symlink(outside, link));
    REQUIRE(!authoring_shader_source_load(
        content,
        descriptor,
        "version 1\nname \"Wrong\"\nkind \"particle\"\nsource "
        "\"Escape.glsl\"\nentry \"cloud\"\n",
        &loaded,
        error,
        sizeof error
    ));
    REQUIRE(strstr(error, "inside project content"));
    REQUIRE(!strcmp(loaded.name, "Source.Cloud"));
    return true;
}

TEST_CASE(shader_source_compiler_publication_preserves_last_good_and_retires_removed_names)
{
    REQUIRE(fixture());
    bool prepared = authoring_shader_sources_prepare(content, true, error, sizeof error);
    if (!prepared) {
        fprintf(stderr, "shader source preparation: %s\n", error);
    }
    REQUIRE(prepared);
    int id = render_shader_find("Source.Cloud");
    REQUIRE(id && render_shader_is_particle(id));
    REQUIRE(render_shader_particle_text(id));
    unsigned revision = render_shader_revision();
    uint64_t generation = render_shader_generation(id);
    float values[32][4];
    shader_error diagnostic = { 0 };
    REQUIRE(
        render_shader_parameters_validate(id, "Spacing 7\n(array)Tint 1 0.5 0.25\n", &diagnostic)
    );
    render_shader_parameters(id, NULL, (ecs_entity){ 0 }, "Spacing 7\n(array)Tint 1 0.5 0.25\n", values);
    REQUIRE(values[3][0] == 7 && values[5][2] == .25f);
    REQUIRE(render_shader_revision() == revision);
    REQUIRE(authoring_shader_sources_prepare(content, true, error, sizeof error));
    REQUIRE(render_shader_revision() == revision);

    tool_write(source, "ChibiParticle cloud(ChibiParticleInput p, ChibiParameters u) { bad GLSL; }\n");
    REQUIRE(!authoring_shader_sources_prepare(content, true, error, sizeof error));
    REQUIRE(strstr(error, "Cloud.glsl"));
    REQUIRE(render_shader_revision() == revision);
    REQUIRE(render_shader_find("Source.Cloud") == id);
    REQUIRE(!strcmp(shader_catalog_assets[id - 1].text.code, code));
    REQUIRE(authoring_shader_source_load(content, descriptor, NULL, &loaded, error, sizeof error));
    REQUIRE(!authoring_shader_sources_publish(&loaded, 1, true, error, sizeof error));
    REQUIRE(render_shader_generation(id) == generation);

    tool_write(source, code);
    tool_write(other, declaration);
    REQUIRE(!authoring_shader_sources_prepare(content, true, error, sizeof error));
    REQUIRE(strstr(error, "Duplicate shader-source"));
    REQUIRE(render_shader_revision() == revision);
    REQUIRE(!unlink(other));
    tool_write(
        source,
        "#include \"Shared.glsl\"\n"
        "ChibiParticle cloud(ChibiParticleInput p, ChibiParameters u) {\n"
        " ChibiParticle result=chibi_particle();\n"
        " result.position=vec3(p.index*u.Spacing,u.Shift.y,p.time*2.0);\n"
        " result.color=vec4(tint(u.Tint),1.0);\n"
        " return result;\n}\n"
    );
    REQUIRE(authoring_shader_sources_prepare(content, true, error, sizeof error));
    REQUIRE(render_shader_find("Source.Cloud") == id);
    REQUIRE(render_shader_generation(id) != generation);
    generation = render_shader_generation(id);
    char include[512];
    snprintf(include, sizeof include, "%s/shaders/Shared.glsl", content);
    tool_write(include, "vec3 tint(vec3 color) { return color*0.5; }\n");
    REQUIRE(authoring_shader_sources_prepare(content, true, error, sizeof error));
    REQUIRE(render_shader_generation(id) != generation);
    REQUIRE(!strcmp(render_shader_parameter_name(id, 3), "Spacing"));
    REQUIRE(!strcmp(render_shader_parameter_name(id, 5), "Tint"));
    REQUIRE(
        render_shader_parameters_validate(id, "Spacing 7\n(array)Tint 1 0.5 0.25\n", &diagnostic)
    );
    render_shader_parameters(id, NULL, (ecs_entity){ 0 }, "Spacing 7\n(array)Tint 1 0.5 0.25\n", values);
    REQUIRE(values[3][0] == 7 && values[5][2] == .25f);
    REQUIRE(!unlink(descriptor));
    REQUIRE(authoring_shader_sources_prepare(content, true, error, sizeof error));
    REQUIRE(!render_shader_find("Source.Cloud"));
    REQUIRE(render_shader_revision() != revision);
    return true;
}

TEST_CASE(shader_source_publication_rejects_existing_graph_identity)
{
    REQUIRE(fixture());
    REQUIRE(authoring_shader_source_load(content, descriptor, NULL, &loaded, error, sizeof error));
    shader_asset graph = { .name = strdup("Source.Cloud"), .glsl = strdup("graph") };
    REQUIRE(graph.name && graph.glsl);
    apush(shader_catalog_assets, graph);
    unsigned revision = render_shader_revision();
    REQUIRE(!authoring_shader_sources_publish(&loaded, 1, true, error, sizeof error));
    REQUIRE(strstr(error, "source and graph names must be unique"));
    REQUIRE(shader_catalog_assets[0].kind == SHADER_ASSET_GRAPH);
    REQUIRE(!strcmp(shader_catalog_assets[0].glsl, "graph"));
    REQUIRE(render_shader_revision() == revision);
    return true;
}

void test_shader_sources()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(shader_source_metadata_binds_scalars_vectors_and_bundled_files);
    RUN_TEST_CASE(shader_source_rejects_invalid_metadata_and_project_escape_without_replacement);
    RUN_TEST_CASE(shader_source_compiler_publication_preserves_last_good_and_retires_removed_names);
    RUN_TEST_CASE(shader_source_publication_rejects_existing_graph_identity);
}
