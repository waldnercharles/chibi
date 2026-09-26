#include "graph/document/problems.h"

#include "assets/project.h"
#include "automation/controller.h"
#include "support/asset_text.h"
#include "test/suite_internal.h"

#include <assert.h>
#include <sys/stat.h>

static bool problem_at(const catalog_problem *p, const behavior_asset *asset, uint64_t graph_id, uint64_t node_id)
{
    return p->asset == asset && p->graph_id == graph_id &&
           p->node_id == node_id && p->severity == CMPL_WARNING;
}

/* Writes an asset's YAML to content/<folder>/<name>.yaml. */
static void write_asset(const char *root, const char *folder, const behavior_asset *asset)
{
    char path[256];
    snprintf(path, sizeof path, "%s/content/%s", root, folder);
    mkdir(path, 0755);
    snprintf(path, sizeof path, "%s/content/%s/%s.yaml", root, folder, asset->name);
    char *json = behavior_asset_save_json(asset);
    char error[256];
    char *yaml = asset_text_to_disk(json, error, sizeof error);
    assert(yaml);
    FILE *file = fopen(path, "w");
    assert(file);
    fputs(yaml, file);
    fclose(file);
    free(yaml);
    free(json);
}

/* An asset's error count in the MCP's asset catalog, or -1 when it is absent. */
static int catalog_errors(const char *root, const char *name)
{
    char request[256];
    snprintf(request, sizeof request, "{\"contract\":1,\"operation\":\"catalog\",\"project_root\":\"%s\"}", root);
    int exit_code = 0;
    char *response = chibi_automation_handle(request, ".", &exit_code);
    yyjson_doc *doc = yyjson_read(response, strlen(response), 0);
    yyjson_val *assets = yyjson_obj_get(yyjson_obj_get(yyjson_doc_get_root(doc), "result"), "assets");
    int errors = -1;
    size_t i, count;
    yyjson_val *asset;
    yyjson_arr_foreach(assets, i, count, asset)
    {
        if (!strcmp(yyjson_get_str(yyjson_obj_get(asset, "name")), name)) {
            errors = (int)yyjson_get_int(yyjson_obj_get(asset, "errors"));
        }
    }
    yyjson_doc_free(doc);
    free(response);
    return errors;
}

/* The MCP's catalog validates graph assets linked with the project's
   libraries, and validates them again when a library changes. */
static void catalog_links_libraries(const behavior_asset *library, const behavior_asset *caller)
{
    char root[] = "/tmp/chibi-problems-XXXXXX";
    assert(mkdtemp(root));
    char path[256];
    snprintf(path, sizeof path, "%s/" CHIBI_PROJECT_MANIFEST, root);
    FILE *file = fopen(path, "w");
    assert(file);
    fprintf(file, "version: %d\ncontent: content\n", CHIBI_PROJECT_VERSION);
    fclose(file);
    snprintf(path, sizeof path, "%s/content", root);
    assert(mkdir(path, 0755) == 0);
    write_asset(root, "libraries", library);
    write_asset(root, "behaviors", caller);
    int linked = catalog_errors(root, "CheckedCaller");
    behavior_asset *emptied = behavior_asset_new(library->name);
    emptied->domain = BEHAVIOR_DOMAIN_LIBRARY;
    write_asset(root, "libraries", emptied);
    behavior_asset_free(emptied);
    test_check(linked == 0 && catalog_errors(root, "CheckedCaller") == 1, "problems: the asset catalog links libraries and rechecks callers when one changes");
    const char *const files[] = { "content/libraries/CheckedMath.yaml",
                                  "content/behaviors/CheckedCaller.yaml",
                                  "content/libraries",
                                  "content/behaviors",
                                  "content",
                                  CHIBI_PROJECT_MANIFEST };
    for (int i = 0; i < (int)(sizeof files / sizeof *files); i++) {
        snprintf(path, sizeof path, "%s/%s", root, files[i]);
        assert(!remove(path));
    }
    assert(!rmdir(root));
}

/* A project's problems: every asset validated linked with the others, each
   problem once, where it was authored, by asset in catalog order. */
void run_graph_problems_checks()
{
    behavior_asset *library = behavior_asset_new("CheckedMath");
    library->domain = BEHAVIOR_DOMAIN_LIBRARY;
    graph *value = behavior_asset_add_function(library, "Value", 1);
    node *muted = graph_add_node(value, registry_node_by_name("Real"));
    graph_set_node_disabled(value, muted, true);
    behavior_asset *caller = behavior_asset_new("CheckedCaller");
    assert(behavior_asset_add_function_call_node(caller, value));
    node *idle = graph_add_node(caller->graph, registry_node_by_name("Real"));
    graph_set_node_disabled(caller->graph, idle, true);

    const behavior_asset *catalog[] = { caller, library };
    catalog_problem *problems = behavior_catalog_problems(catalog, 2);
    test_check(
        acount(problems) == 2 &&
            problem_at(&problems[0], caller, caller->graph->graph_id, idle->id) &&
            problem_at(&problems[1], library, value->graph_id, muted->id),
        "problems: a library's problem shows once, under the library, after "
        "the caller's own"
    );
    afree(problems);

    const behavior_asset *alone[] = { caller };
    problems = behavior_catalog_problems(alone, 1);
    test_check(
        acount(problems) == 1 && problems[0].asset == caller &&
            problems[0].severity == CMPL_ERROR && problems[0].node_id == 0 &&
            strstr(problems[0].message, "CheckedMath"),
        "problems: a call into a missing library is one error on the caller"
    );
    afree(problems);

    compile_report report;
    compile_report_init(&report);
    behavior_asset *linked = behavior_asset_validate(caller, catalog, 2, &report);
    test_check(
        linked && report.error_count == 0 && report.warning_count == 2,
        "problems: validating an asset compiles it with the libraries it calls"
    );
    behavior_asset_free(linked);
    compile_report_free(&report);
    catalog_links_libraries(library, caller);
    behavior_asset_free(caller);
    behavior_asset_free(library);
}
