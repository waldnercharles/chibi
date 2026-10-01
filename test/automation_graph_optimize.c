#include "automation/graph_optimize.h"
#include "suite_internal.h"
#include "third_party/yyjson/yyjson.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

static int optimize_request(const char *source, const char *request_json, char **edited, yyjson_mut_doc **response_doc)
{
    yyjson_doc *request_doc = yyjson_read(request_json, strlen(request_json), 0);
    if (!request_doc) {
        return CHIBI_AUTOMATION_INVALID_REQUEST;
    }
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *result = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, result);
    char error[256] = { 0 };
    int status = automation_graph_optimize(
        source,
        yyjson_doc_get_root(request_doc),
        doc,
        result,
        edited,
        error,
        sizeof error
    );
    yyjson_doc_free(request_doc);
    *response_doc = doc;
    return status;
}

static node *node_named(graph *graph_value, const char *name)
{
    for (int i = 0; i < acount(graph_value->nodes); i++) {
        if (!strcmp(graph_value->nodes[i]->desc->name, name)) {
            return graph_value->nodes[i];
        }
    }
    return NULL;
}

void test_automation_graph_optimize(void)
{
    behavior_asset *asset = behavior_asset_new("OptimizeBehavior");
    node *getter = graph_add_node(asset->graph, registry_node_by_name("Get_GameMovement"));
    node *target = graph_add_node(asset->graph, registry_node_by_name("Set_GameMovement"));
    bool connected = getter && target &&
                     graph_connect_data(asset->graph, getter, 0, target, 1) == 0;
    char *source = connected ? behavior_asset_save(asset) : NULL;
    behavior_asset_free(asset);

    char *edited = NULL;
    yyjson_mut_doc *response = NULL;
    int status = source
                   ? optimize_request(source, "{\"dry_run\":true,\"local_aliases\":false}", &edited, &response)
                   : CHIBI_AUTOMATION_VALIDATION_ERROR;
    yyjson_mut_val *root = response ? yyjson_mut_doc_get_root(response) : NULL;
    bool folded = status == CHIBI_AUTOMATION_OK && edited && root &&
                  yyjson_mut_get_int(yyjson_mut_obj_get(root, "folded_inputs")) == 1 &&
                  yyjson_mut_get_int(yyjson_mut_obj_get(root, "removed_nodes")) == 1;
    /* The helper is deliberately IO-free; the native controller adds publish
     * and dry-run state around its transformation report. */
    behavior_asset *optimized = edited ? behavior_asset_load(edited) : NULL;
    node *optimized_target = optimized
                               ? node_named(optimized->graph, "Set_GameMovement")
                               : NULL;
    const pin_binding *binding = optimized_target
                                   ? graph_pin_binding(optimized->graph, optimized_target, 0, 1)
                                   : NULL;
    folded = folded && optimized && binding &&
             binding->kind == PIN_BINDING_COMPONENT && binding->component_key &&
             !strcmp(binding->component_key, GAME_COMPONENT_GameMovement.key);
    char *expected = edited ? xstrdup(edited) : NULL;
    test_check(folded, "MCP graph optimization folds safe component getters and reports the transformation");
    behavior_asset_free(optimized);
    yyjson_mut_doc_free(response);
    free(edited);

    edited = NULL;
    response = NULL;
    status = optimize_request(source, "{\"dry_run\":true,\"local_aliases\":false}", &edited, &response);
    bool repeatable = status == CHIBI_AUTOMATION_OK && edited && expected &&
                      !strcmp(expected, edited);
    test_check(repeatable, "MCP graph optimization is deterministic and never mutates its source clone");
    yyjson_mut_doc_free(response);
    free(edited);
    free(expected);
    free(source);
}
