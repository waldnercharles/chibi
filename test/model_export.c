#include "assets/model_export.h"

#include "assets/project.h"
#include "authoring/mesh_prepare.h"
#include "automation/controller.h"
#include "graph/authoring/graph_edit.h"
#include "graph/document/serialize.h"
#include "support/array.h"
#include "support/file_io.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <yyjson.h>

static void write_source(const char *path, const char *text)
{
    FILE *file = fopen(path, "wb");
    assert(file);
    assert(fwrite(text, 1, strlen(text), file) == strlen(text));
    assert(!fclose(file));
}
static void remove_bundle(const char *path)
{
    const char *names[] = { "model.mesh.json", "model.obj", "model.mtl", "provenance.json" };
    for (int i = 0; i < 4; i++) {
        char file[512];
        snprintf(file, sizeof file, "%s/%s", path, names[i]);
        assert(!unlink(file));
    }
    assert(!rmdir(path));
}
/* Exercise the automation entry point, including its operation allowlist,
 * project resolution and contract discovery, rather than calling only service. */
static void public_export(const char *root, const char *source)
{
    char project[512], content[512], models[512], path[512], destination[512];
    snprintf(project, sizeof project, "%s/public", root);
    snprintf(content, sizeof content, "%s/content", project);
    snprintf(models, sizeof models, "%s/models", content);
    assert(!mkdir(project, 0700) && !mkdir(content, 0700) && !mkdir(models, 0700));
    snprintf(path, sizeof path, "%s/project.kdl", project);
    char manifest[128];
    snprintf(
        manifest,
        sizeof manifest,
        "version %d\n"
        "name \"Bake test\"\n"
        "content \"content\"\n",
        CHIBI_PROJECT_VERSION
    );
    write_source(path, manifest);
    snprintf(path, sizeof path, "%s/Ball.kdl", models);
    write_source(path, source);
    snprintf(destination, sizeof destination, "%s/bundle", project);
    char request[1536];
    snprintf(request, sizeof request, "{\"contract\":1,\"operation\":\"bake_model\",\"model\":\"Ball\",\"destination\":\"%s\",\"options\":{\"cell_size\":0.25}}", destination);
    int code;
    char *reply = chibi_automation_handle(request, project, &code);
    assert(reply && code == CHIBI_AUTOMATION_OK);
    yyjson_doc *doc = yyjson_read(reply, strlen(reply), 0);
    assert(doc && yyjson_get_bool(yyjson_obj_get(yyjson_doc_get_root(doc), "ok")));
    yyjson_doc_free(doc);
    free(reply);
    char *retained = file_read_text(path);
    assert(retained && !strcmp(retained, source));
    free(retained);
    reply = chibi_automation_handle(request, project, &code);
    assert(reply && code != CHIBI_AUTOMATION_OK && strstr(reply, "new directory"));
    free(reply);
    reply = chibi_automation_handle("{\"contract\":1,\"operation\":\"describe_contract\"}", NULL, &code);
    assert(reply && code == CHIBI_AUTOMATION_OK);
    doc = yyjson_read(reply, strlen(reply), 0);
    yyjson_val *operations = yyjson_obj_get(yyjson_obj_get(yyjson_doc_get_root(doc), "result"), "operations");
    bool discovered = false;
    for (size_t i = 0; i < yyjson_arr_size(operations); i++) {
        yyjson_val *operation = yyjson_arr_get(operations, i);
        if (strcmp(yyjson_get_str(yyjson_obj_get(operation, "name")), "bake_model")) {
            continue;
        }
        discovered = true;
        assert(!yyjson_get_bool(yyjson_obj_get(operation, "read_only")));
        assert(!yyjson_get_bool(yyjson_obj_get(operation, "destructive")));
    }
    assert(discovered);
    yyjson_doc_free(doc);
    free(reply);
    remove_bundle(destination);
    assert(!unlink(path));
    snprintf(path, sizeof path, "%s/project.kdl", project);
    assert(!unlink(path));
    assert(!rmdir(models) && !rmdir(content) && !rmdir(project));
}

void test_model_export()
{
    char root[] = "/tmp/chibi-model-bundle-XXXXXX";
    assert(mkdtemp(root));
    char folder[256], source[256], destination[256], second[256], error[512];
    snprintf(folder, sizeof folder, "%s/models", root);
    assert(!mkdir(folder, 0700));
    snprintf(source, sizeof source, "%s/models/Ball.kdl", root);
    snprintf(destination, sizeof destination, "%s/bundle", root);
    snprintf(second, sizeof second, "%s/second", root);
    behavior_asset *asset = behavior_asset_new("Ball");
    asset->domain = BEHAVIOR_DOMAIN_MODEL;
    graph *g = asset->graph;
    node *parameter = graph_add_node(g, &NODE_ModelParameter);
    assert(!graph_set_node_reference(g, parameter, 0, "Radius"));
    float defaults[] = { 1, .5f, 2 };
    for (int i = 0; i < 3; i++) {
        pin_value value = { .type = PIN_F32, .f = defaults[i] };
        assert(!graph_set_pin_default(g, parameter, false, i, &value));
    }
    node *sphere = graph_add_node(g, &NODE_ModelSphere);
    assert(!graph_connect_data(g, parameter, 0, sphere, 0));
    assert(!graph_connect_data(g, sphere, 0, model_graph_add_output(g), 0));
    char *saved = behavior_asset_save(asset);
    assert(saved);
    write_source(source, saved);
    model_export_options options = { .bake = model_bake_defaults() };
    options.bake.cell_size = .25f;
    model_export_summary summary = { 0 };
    assert(model_export_bundle(root, "Ball", destination, &options, &summary, error, sizeof error));
    assert(summary.triangles > 0 && summary.samples > 0);
    char path[512];
    snprintf(path, sizeof path, "%s/model.mesh.json", destination);
    char *text = file_read_text(path);
    render_mesh_asset mesh = { 0 };
    assert(text && render_mesh_asset_parse(text, &mesh, error, sizeof error));
    for (int i = 0; i < acount(mesh.vertices); i++) {
        CF_V3 p = mesh.vertices[i].position;
        assert(fabsf(sqrtf(p.x * p.x + p.y * p.y + p.z * p.z) - 1) < .1f);
    }
    free(text);
    snprintf(path, sizeof path, "%s/model.obj", destination);
    text = file_read_text(path);
    assert(text && strstr(text, "mtllib model.mtl") && strstr(text, "usemtl material_0"));
    free(text);
    snprintf(path, sizeof path, "%s/model.mtl", destination);
    text = file_read_text(path);
    assert(text && strstr(text, "newmtl material_0"));
    assert(strstr(text, "Neutral placeholders") && strstr(text, "Kd 0.7 0.7 0.7"));
    free(text);
    render_mesh_asset_free(&mesh);
    assert(!model_export_bundle(root, "Ball", destination, &options, NULL, error, sizeof error));
    assert(strstr(error, "new directory"));
    model_export_override override = { .asset = "Ball",
                                       .scope = "",
                                       .parameter_id = parameter->id,
                                       .width = 1,
                                       .value = { 1.5f } };
    options.overrides = &override;
    options.override_count = 1;
    assert(model_export_bundle(root, "Ball", second, &options, &summary, error, sizeof error));
    assert(fabsf(summary.bounds.max[0] - 1.5f) < .0001f);
    remove_bundle(second);
    override.value[0] = 3;
    assert(!model_export_bundle(root, "Ball", second, &options, NULL, error, sizeof error));
    struct stat st;
    assert(lstat(second, &st));
    override.value[0] = 1;
    options.bake.max_triangles = 1;
    assert(!model_export_bundle(root, "Ball", second, &options, NULL, error, sizeof error));
    assert(lstat(second, &st));
    options.bake = model_bake_defaults();
    write_source(source, "malformed {");
    assert(!model_export_bundle(root, "Ball", second, &options, NULL, error, sizeof error));
    text = file_read_text(source);
    assert(text && !strcmp(text, "malformed {"));
    free(text);
    write_source(source, saved);
    options.bake.cell_size = .25f;
    assert(model_export_bundle(root, "Ball", second, &options, NULL, error, sizeof error));
    snprintf(path, sizeof path, "%s/model.mesh.json", destination);
    char *first_mesh = file_read_text(path);
    snprintf(path, sizeof path, "%s/model.mesh.json", second);
    char *retry_mesh = file_read_text(path);
    assert(first_mesh && retry_mesh && !strcmp(first_mesh, retry_mesh));
    free(first_mesh);
    free(retry_mesh);
    text = file_read_text(source);
    assert(text && !strcmp(saved, text));
    free(text);
    remove_bundle(second);
    remove_bundle(destination);
    public_export(root, saved);
    free(saved);
    behavior_asset_free(asset);
    assert(!unlink(source));
    assert(!rmdir(folder));
    assert(!rmdir(root));
}
