#include "automation/graph_edit.h"
#include "graph/document/serialize.h"
#include "graph/model/behavior_asset.h"
#include "suite_internal.h"
#include "third_party/yyjson/yyjson.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int edit_request(
    const char *source,
    const char *kind,
    const char *name,
    const behavior_asset *const *libraries,
    int library_count,
    const char *request_json,
    char **edited,
    yyjson_mut_doc **response_doc
)
{
    yyjson_doc *request_doc = yyjson_read(request_json, strlen(request_json), 0);
    if (!request_doc) {
        return CHIBI_AUTOMATION_INVALID_REQUEST;
    }
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *result = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, result);
    char error[256] = { 0 };
    int status = automation_graph_edit(
        source,
        kind,
        name,
        libraries,
        library_count,
        yyjson_doc_get_root(request_doc),
        doc,
        result,
        edited,
        error,
        sizeof error
    );
    yyjson_doc_free(request_doc);
    if (response_doc) {
        *response_doc = doc;
    } else {
        yyjson_mut_doc_free(doc);
    }
    return status;
}

static node *find_node_type(graph *graph, const char *type)
{
    for (int i = 0; i < acount(graph->nodes); i++) {
        if (!strcmp(graph->nodes[i]->desc->name, type)) {
            return graph->nodes[i];
        }
    }
    return NULL;
}

static int find_pin(const node *n, const char *name, bool output)
{
    const pin_spec *pins = output ? n->output_pins : n->input_pins;
    int count = output ? n->output_count : n->input_count;
    for (int i = 0; i < count; i++) {
        if (pins[i].name && !strcmp(pins[i].name, name)) {
            return i;
        }
    }
    return -1;
}

static bool result_has_node_ref(yyjson_mut_doc *doc, const char *name)
{
    yyjson_mut_val *root = yyjson_mut_doc_get_root(doc);
    yyjson_mut_val *nodes = root ? yyjson_mut_obj_get(root, "nodes") : NULL;
    if (!nodes || !yyjson_mut_is_arr(nodes)) {
        return false;
    }
    yyjson_mut_arr_iter iterator;
    yyjson_mut_arr_iter_init(nodes, &iterator);
    yyjson_mut_val *entry;
    while ((entry = yyjson_mut_arr_iter_next(&iterator))) {
        yyjson_mut_val *ref = yyjson_mut_obj_get(entry, "ref");
        if (ref && yyjson_mut_is_str(ref) && !strcmp(yyjson_mut_get_str(ref), name)) {
            return true;
        }
    }
    return false;
}

void test_automation_graph_edit(void)
{
    behavior_asset *empty = behavior_asset_new("GraphEditBehavior");
    char *source = behavior_asset_save_json(empty);
    behavior_asset_free(empty);
    char *edited = NULL;
    yyjson_mut_doc *response = NULL;
    int status = edit_request(
        source,
        "behavior",
        "GraphEditBehavior",
        NULL,
        0,
        "{\"operations\":[{\"op\":\"add_node\",\"ref\":\"value\",\"type\":"
        "\"Integer\",\"inputs\":{\"Value\":7}}]}",
        &edited,
        &response
    );
    bool behavior_ok = status == CHIBI_AUTOMATION_OK && edited &&
                       strstr(edited, "\"type\": \"Integer\"") &&
                       yyjson_mut_obj_get(yyjson_mut_doc_get_root(response), "nodes");
    test_check(behavior_ok, "MCP graph edits add typed behavior nodes and return refs");
    yyjson_mut_doc_free(response);
    free(edited);

    edited = NULL;
    status = edit_request(
        source,
        "behavior",
        "GraphEditBehavior",
        NULL,
        0,
        "{\"operations\":[{\"op\":\"add_node\",\"ref\":\"compare\",\"type\":"
        "\"Equal\"},"
        "{\"op\":\"set_pin\",\"node\":\"compare\",\"pin\":\"A\",\"type\":"
        "\"uint\"}]}",
        &edited,
        NULL
    );
    test_check(status != CHIBI_AUTOMATION_OK && !edited, "MCP rejects retyping a fixed native pin before a runtime assertion");
    free(edited);

    edited = NULL;
    response = NULL;
    status = edit_request(
        source,
        "behavior",
        "GraphEditBehavior",
        NULL,
        0,
        "{\"operations\":[{\"op\":\"add_node\",\"ref\":\"expression\","
        "\"type\":\"Behavior Expression\",\"source\":\"x >= 0\"},"
        "{\"op\":\"set_expression\",\"node\":\"expression\","
        "\"source\":\"x < 10\"}]}",
        &edited,
        &response
    );
    behavior_asset *expression_result = edited ? behavior_asset_load_json(edited)
                                               : NULL;
    node *expression = expression_result
                         ? find_node_type(expression_result->graph, "Behavior Expression")
                         : NULL;
    graph *expression_graph = expression_result && expression
                                ? behavior_asset_find_graph(
                                      expression_result,
                                      expression->reference_id
                                  )
                                : NULL;
    bool expression_edit = status == CHIBI_AUTOMATION_OK && expression_graph &&
                           expression_graph->expression_mode == GRAPH_EXPRESSION_BRANCH &&
                           find_pin(expression, "Exec", false) >= 0 &&
                           find_pin(expression, "True", true) >= 0 &&
                           find_pin(expression, "False", true) >= 0 &&
                           find_pin(expression, "Result", true) >= 0;
    test_check(expression_edit, "MCP graph edits add and update Behavior Expression nodes");
    behavior_asset_free(expression_result);
    yyjson_mut_doc_free(response);
    free(edited);

    edited = NULL;
    status = edit_request(
        source,
        "behavior",
        "GraphEditBehavior",
        NULL,
        0,
        "{\"operations\":[{\"op\":\"add_node\",\"ref\":\"value\",\"type\":"
        "\"Integer\",\"inputs\":{\"Value\":7}},"
        "{\"op\":\"add_node\",\"ref\":\"first\",\"type\":\"Add\"},"
        "{\"op\":\"add_node\",\"ref\":\"second\",\"type\":\"Add\"},"
        "{\"op\":\"connect\",\"from\":\"value\",\"from_pin\":\"Value\","
        "\"to\":\"first\",\"to_pin\":\"A\"},"
        "{\"op\":\"create_local\",\"node\":\"value\",\"pin\":\"Value\","
        "\"name\":\"Seven\"},"
        "{\"op\":\"connect\",\"from\":\"value\",\"from_pin\":\"Value\","
        "\"to\":\"second\",\"to_pin\":\"A\"}]}",
        &edited,
        NULL
    );
    behavior_asset *named = edited ? behavior_asset_load_json(edited) : NULL;
    int reads = 0;
    for (int i = 0; named && i < acount(named->graph->nodes); i++) {
        const pin_binding *b = named->graph->nodes[i]->input_count
                                 ? named->graph->nodes[i]->input_pins[0].binding
                                 : NULL;
        reads += b && b->kind == PIN_BINDING_LOCAL;
    }
    test_check(status == CHIBI_AUTOMATION_OK && reads == 2, "MCP connections from a named output read it by name");
    behavior_asset_free(named);
    free(edited);

    edited = NULL;
    response = NULL;
    status = edit_request(
        source,
        "behavior",
        "GraphEditBehavior",
        NULL,
        0,
        "{\"description\":\"Enemy movement graph\","
        "\"owner_type\":\"ecs.entity\","
        "\"graph_description\":\"Runs once per fixed frame\","
        "\"operations\":[]}",
        &edited,
        &response
    );
    behavior_asset *metadata = edited ? behavior_asset_load_json(edited) : NULL;
    bool metadata_ok = status == CHIBI_AUTOMATION_OK && metadata &&
                       !strcmp(metadata->description, "Enemy movement graph") &&
                       !strcmp(metadata->owner_type_name, "ecs.entity") &&
                       !strcmp(metadata->graph->description, "Runs once per fixed frame");
    test_check(metadata_ok, "MCP graph edits preserve asset and selected graph metadata");
    behavior_asset_free(metadata);
    yyjson_mut_doc_free(response);
    free(edited);

    edited = NULL;
    response = NULL;
    status = edit_request(
        NULL,
        "material",
        "InvalidMaterial",
        NULL,
        0,
        "{\"graph_name\":\"missing\",\"operations\":[]}",
        &edited,
        &response
    );
    bool invalid_new_material = status == CHIBI_AUTOMATION_INVALID_REQUEST &&
                                !edited &&
                                yyjson_mut_obj_get(yyjson_mut_doc_get_root(response), "failed_operation");
    test_check(invalid_new_material, "MCP new asset failures clean up the initialized shader output");
    yyjson_mut_doc_free(response);

    edited = NULL;
    response = NULL;
    status = edit_request(source, "material", "GraphEditBehavior", NULL, 0, "{\"operations\":[]}", &edited, &response);
    bool domain_mismatch = status == CHIBI_AUTOMATION_INVALID_REQUEST && !edited;
    test_check(domain_mismatch, "MCP graph edits reject a source asset from another family");
    yyjson_mut_doc_free(response);

    edited = NULL;
    response = NULL;
    status = edit_request(
        source,
        "behavior",
        "GraphEditBehavior",
        NULL,
        0,
        "{\"operations\":[{\"op\":\"add_node\",\"ref\":\"first\","
        "\"type\":\"Integer\"},{\"op\":\"add_node\",\"ref\":\"second\","
        "\"type\":\"Integer\"},{\"op\":\"connect\",\"from\":\"first\","
        "\"from_pin\":\"Value\",\"to\":\"second\",\"to_pin\":\"Value\"},"
        "{\"op\":\"set_node\",\"node\":\"first\",\"presentation\":\"expanded\"}"
        "]}",
        &edited,
        &response
    );
    behavior_asset *arranged_asset = edited ? behavior_asset_load_json(edited) : NULL;
    node *arranged_first = NULL;
    node *arranged_second = NULL;
    if (arranged_asset) {
        for (int i = 0; i < acount(arranged_asset->graph->nodes); i++) {
            node *candidate = arranged_asset->graph->nodes[i];
            if (strcmp(candidate->desc->name, "Integer")) {
                continue;
            }
            if (!arranged_first) {
                arranged_first = candidate;
            } else {
                arranged_second = candidate;
                break;
            }
        }
    }
    bool layout_enabled = status == CHIBI_AUTOMATION_OK && arranged_first &&
                          arranged_second &&
                          (arranged_first->x != arranged_second->x ||
                           arranged_first->y != arranged_second->y);
    test_check(layout_enabled, "MCP graph edits arrange authored nodes by default");
    behavior_asset_free(arranged_asset);
    yyjson_mut_doc_free(response);
    free(edited);

    edited = NULL;
    response = NULL;
    status = edit_request(
        source,
        "behavior",
        "GraphEditBehavior",
        NULL,
        0,
        "{\"operations\":[{\"op\":\"add_node\",\"ref\":\"note\",\"type\":"
        "\"Integer\"},"
        "{\"op\":\"set_comment\",\"node\":\"note\",\"comment\":\"Starting "
        "health\"},"
        "{\"op\":\"set_comment\",\"node\":\"note\",\"hidden\":true}]}",
        &edited,
        &response
    );
    behavior_asset *commented = edited ? behavior_asset_load_json(edited) : NULL;
    node *note = commented ? find_node_type(commented->graph, "Integer") : NULL;
    test_check(
        status == CHIBI_AUTOMATION_OK && note && note->comment &&
            !strcmp(note->comment, "Starting health") && note->comment_hidden,
        "MCP set_comment writes a node comment and hides it separately"
    );
    behavior_asset_free(commented);
    yyjson_mut_doc_free(response);
    free(edited);

    edited = NULL;
    response = NULL;
    status = edit_request(
        source,
        "behavior",
        "GraphEditBehavior",
        NULL,
        0,
        "{\"operations\":[{\"op\":\"add_node\",\"ref\":\"note\",\"type\":"
        "\"Integer\"},"
        "{\"op\":\"set_comment\",\"node\":\"note\"}]}",
        &edited,
        &response
    );
    test_check(status != CHIBI_AUTOMATION_OK && !edited, "MCP set_comment needs a comment or hidden");
    yyjson_mut_doc_free(response);
    free(edited);

    behavior_asset *function_asset = behavior_asset_new("FunctionVariables");
    behavior_asset_set_owner_type(function_asset, "ecs.entity");
    behavior_asset_add_variable(function_asset, "items", PIN_F32, NULL, PIN_CONTAINER_ARRAY, NULL, "Test");
    behavior_asset_add_function(function_asset, "Work", false);
    char *function_source = behavior_asset_save_json(function_asset);
    behavior_asset_free(function_asset);
    edited = NULL;
    response = NULL;
    status = edit_request(
        function_source,
        "behavior",
        "FunctionVariables",
        NULL,
        0,
        "{\"graph_name\":\"Work\",\"operations\":["
        "{\"op\":\"add_variable_node\",\"ref\":\"items_get\",\"kind\":\"get\","
        "\"variable\":1},"
        "{\"op\":\"add_self_node\",\"ref\":\"owner\"}]}",
        &edited,
        &response
    );
    behavior_asset *function_result = edited ? behavior_asset_load_json(edited) : NULL;
    graph *work = function_result
                    ? behavior_asset_find_graph_by_name(function_result, "Work")
                    : NULL;
    node *get = work ? find_node_type(work, "Get Variable") : NULL;
    node *self = work ? find_node_type(work, "Self") : NULL;
    bool function_nodes = status == CHIBI_AUTOMATION_OK && get && self &&
                          get->reference_id == 1 &&
                          get->output_pins[0].container == PIN_CONTAINER_ARRAY &&
                          self->output_pins[0].type == PIN_HANDLE &&
                          self->output_pins[0].type_name &&
                          !strcmp(self->output_pins[0].type_name, "ecs.entity");
    test_check(function_nodes, "MCP variable and Self nodes target the selected function graph");
    behavior_asset_free(function_result);
    yyjson_mut_doc_free(response);
    free(function_source);
    free(edited);

    char variable_request[4096] = "{\"operations\":[";
    for (int i = 0; i < 20; i++) {
        char operation[192];
        snprintf(
            operation,
            sizeof operation,
            "%s{\"op\":\"add_variable\",\"name\":\"v%d\",\"type\":\"float\"%s}",
            i ? "," : "",
            i,
            i ? "" : ",\"ref\":\"first\""
        );
        strcat(variable_request, operation);
    }
    strcat(variable_request, ",{\"op\":\"set_variable\",\"variable\":\"first\",\"name\":\"renamed\"}]}");
    edited = NULL;
    response = NULL;
    status = edit_request(source, "behavior", "GraphEditBehavior", NULL, 0, variable_request, &edited, &response);
    behavior_asset *variable_result = edited ? behavior_asset_load_json(edited) : NULL;
    bool variable_refs = status == CHIBI_AUTOMATION_OK && variable_result &&
                         behavior_asset_find_variable_by_name(variable_result, "renamed");
    test_check(variable_refs, "MCP variable refs survive variable-array growth");
    behavior_asset_free(variable_result);
    yyjson_mut_doc_free(response);
    free(edited);

    edited = NULL;
    response = NULL;
    status = edit_request(
        NULL,
        "material",
        "GraphEditMaterial",
        NULL,
        0,
        "{\"operations\":["
        "{\"op\":\"add_node\",\"ref\":\"color\",\"type\":\"Shader Vector3\","
        "\"inputs\":{\"X\":1,\"Y\":0.25,\"Z\":0}},"
        "{\"op\":\"connect\",\"from\":\"color\",\"from_pin\":\"Value\","
        "\"to\":\"output\",\"to_pin\":\"Base Color\",\"kind\":\"data\"},"
        "{\"op\":\"add_node\",\"ref\":\"ramp\",\"type\":\"Shader Color Ramp\"},"
        "{\"op\":\"set_ramp\",\"node\":\"ramp\",\"stops\":["
        "{\"position\":0,\"color\":[0,0,0,1]},"
        "{\"position\":1,\"color\":[1,0.5,0,1]}]}]}",
        &edited,
        &response
    );
    bool shader_ok = status == CHIBI_AUTOMATION_OK && edited &&
                     strstr(edited, "GraphEditMaterial") &&
                     strstr(edited, "Shader Color Ramp");
    test_check(shader_ok, "MCP graph edits initialize, connect, and configure shader nodes");
    yyjson_mut_doc_free(response);
    free(edited);

    edited = NULL;
    response = NULL;
    status = edit_request(
        NULL,
        "material",
        "GraphEditConstantMaterial",
        NULL,
        0,
        "{\"operations\":["
        "{\"op\":\"add_node\",\"ref\":\"constant\",\"type\":\"Shader "
        "Constant\"},"
        "{\"op\":\"set_output\",\"node\":\"constant\",\"pin\":\"Value\","
        "\"type\":\"ShaderVec3\",\"value\":{\"x\":0.1,\"y\":0.2,\"z\":0.3}},"
        "{\"op\":\"connect\",\"from\":\"constant\",\"from_pin\":\"Value\","
        "\"to\":\"output\",\"to_pin\":\"Base Color\",\"kind\":\"data\"}]}",
        &edited,
        &response
    );
    behavior_asset *constant_material = edited ? behavior_asset_load_json(edited)
                                               : NULL;
    node *constant = constant_material
                       ? find_node_type(constant_material->graph, "Shader Constant")
                       : NULL;
    bool constant_ok = status == CHIBI_AUTOMATION_OK && constant &&
                       constant->output_pins[0].type == PIN_STRUCT &&
                       constant->output_pins[0].type_name &&
                       !strcmp(constant->output_pins[0].type_name, "ShaderVec3");
    test_check(constant_ok, "MCP shader edits set typed values on constant outputs");
    behavior_asset_free(constant_material);
    yyjson_mut_doc_free(response);
    free(edited);

    edited = NULL;
    response = NULL;
    status = edit_request(
        NULL,
        "material",
        "GraphEditTypedWildcardMaterial",
        NULL,
        0,
        "{\"operations\":["
        "{\"op\":\"add_node\",\"ref\":\"value\",\"type\":\"Shader Parameter\"},"
        "{\"op\":\"add_node\",\"ref\":\"multiply\",\"type\":\"Shader "
        "Multiply\"},"
        "{\"op\":\"connect\",\"from\":\"value\",\"from_pin\":\"Value\","
        "\"to\":\"multiply\",\"to_pin\":\"A\",\"type\":\"ShaderVec3\",\"kind\":"
        "\"data\"},"
        "{\"op\":\"connect\",\"from\":\"multiply\",\"from_pin\":\"Value\","
        "\"to\":\"output\",\"to_pin\":\"Base Color\",\"kind\":\"data\"}]}",
        &edited,
        &response
    );
    behavior_asset *typed_wildcard = edited ? behavior_asset_load_json(edited) : NULL;
    node *typed_value = typed_wildcard
                          ? find_node_type(typed_wildcard->graph, "Shader Parameter")
                          : NULL;
    node *typed_multiply = typed_wildcard
                             ? find_node_type(typed_wildcard->graph, "Shader Multiply")
                             : NULL;
    bool typed_wildcard_ok = status == CHIBI_AUTOMATION_OK && typed_value &&
                             typed_multiply &&
                             typed_value->output_pins[0].type == PIN_STRUCT &&
                             typed_value->output_pins[0].type_name &&
                             !strcmp(typed_value->output_pins[0].type_name, "ShaderVec3") &&
                             typed_multiply->input_pins[0].type == PIN_STRUCT &&
                             typed_multiply->input_pins[0].type_name &&
                             !strcmp(typed_multiply->input_pins[0].type_name, "ShaderVec3");
    test_check(typed_wildcard_ok, "MCP shader connections accept explicit wildcard types");
    behavior_asset_free(typed_wildcard);
    yyjson_mut_doc_free(response);
    free(edited);

    edited = NULL;
    response = NULL;
    status = edit_request(
        NULL,
        "shape",
        "GraphEditShape",
        NULL,
        0,
        "{\"operations\":[{\"op\":\"set_bounds\",\"value\":[24,18]}]}",
        &edited,
        &response
    );
    behavior_asset *shape_asset = edited ? behavior_asset_load_json(edited) : NULL;
    bool shape_ok = status == CHIBI_AUTOMATION_OK && shape_asset &&
                    shape_asset->bounds[0] == 24 && shape_asset->bounds[1] == 18;
    test_check(shape_ok, "MCP shape creation authors valid half extents");
    behavior_asset_free(shape_asset);
    yyjson_mut_doc_free(response);
    free(edited);

    char *library_json = NULL;
    response = NULL;
    status = edit_request(
        NULL,
        "shader-library",
        "ShaderLib",
        NULL,
        0,
        "{\"operations\":[{\"op\":\"add_graph\",\"ref\":\"math\","
        "\"name\":\"Math\",\"kind\":\"function\",\"pure\":true},"
        "{\"op\":\"add_pin\",\"node\":\"math.entry\",\"side\":\"output\","
        "\"name\":\"Value\",\"kind\":\"data\",\"type\":\"float\"},"
        "{\"op\":\"add_pin\",\"node\":\"math.return\",\"side\":\"input\","
        "\"name\":\"Result\",\"kind\":\"data\",\"type\":\"float\"},"
        "{\"op\":\"connect\",\"from\":\"math.entry\",\"from_pin\":\"Value\","
        "\"to\":\"math.return\",\"to_pin\":\"Result\",\"kind\":\"data\"}]}",
        &library_json,
        &response
    );
    bool library_ok = status == CHIBI_AUTOMATION_OK && library_json &&
                      strstr(library_json, "\"domain\": \"shader-library\"") &&
                      strstr(library_json, "\"name\": \"Math\"") &&
                      result_has_node_ref(response, "math.entry") &&
                      result_has_node_ref(response, "math.return");
    test_check(library_ok, "MCP graph edits create a valid shader function library");
    yyjson_mut_doc_free(response);

    behavior_asset *library = library_json ? behavior_asset_load_json(library_json)
                                           : NULL;
    graph *math = library ? behavior_asset_find_graph_by_name(library, "Math") : NULL;
    char invalid_library_request[1024];
    snprintf(
        invalid_library_request,
        sizeof invalid_library_request,
        "{\"graph_name\":\"Math\",\"operations\":["
        "{\"op\":\"add_node\",\"ref\":\"gradient\","
        "\"type\":\"Shader Gradient\"},"
        "{\"op\":\"connect\",\"from\":\"gradient\","
        "\"from_pin\":\"Value\",\"to\":%llu,\"to_pin\":\"Result\","
        "\"kind\":\"data\"},"
        "{\"op\":\"set_input\",\"node\":\"gradient\","
        "\"pin\":\"Type\",\"value\":99}]}\n",
        (unsigned long long)(math ? math->return_id : 0)
    );
    edited = NULL;
    response = NULL;
    status = library ? edit_request(
                           library_json,
                           "shader-library",
                           "ShaderLib",
                           NULL,
                           0,
                           invalid_library_request,
                           &edited,
                           &response
                       )
                     : CHIBI_AUTOMATION_VALIDATION_ERROR;
    bool invalid_library_setting = status == CHIBI_AUTOMATION_VALIDATION_ERROR &&
                                   !edited;
    test_check(invalid_library_setting, "MCP shader libraries reject invalid shader settings before publication");
    yyjson_mut_doc_free(response);
    free(edited);

    const behavior_asset *libraries[] = { library };
    edited = NULL;
    response = NULL;
    status = library ? edit_request(
                           NULL,
                           "material",
                           "LibraryConsumer",
                           libraries,
                           1,
                           "{\"operations\":[{\"op\":\"add_node\",\"ref\":"
                           "\"math_call\","
                           "\"type\":\"Call Pure Function\",\"reference_id\":1,"
                           "\"reference_name\":\"Math\",\"reference_asset\":"
                           "\"ShaderLib\"}]}",
                           &edited,
                           &response
                       )
                     : CHIBI_AUTOMATION_VALIDATION_ERROR;
    bool library_call_ok = status == CHIBI_AUTOMATION_OK && edited &&
                           strstr(edited, "LibraryConsumer");
    test_check(library_call_ok, "MCP shader consumers validate linked library calls before save");
    yyjson_mut_doc_free(response);
    free(edited);
    behavior_asset_free(library);
    free(library_json);

    edited = NULL;
    response = NULL;
    status = edit_request(
        source,
        "behavior",
        "GraphEditBehavior",
        NULL,
        0,
        "{\"operations\":[{\"op\":\"add_node\",\"ref\":\"value\",\"type\":"
        "\"Integer\"},"
        "{\"op\":\"connect\",\"from\":\"value\",\"from_pin\":\"Value\","
        "\"to\":999999,\"to_pin\":0,\"kind\":\"data\"}]}",
        &edited,
        &response
    );
    bool rollback_ok = status == CHIBI_AUTOMATION_INVALID_REQUEST && !edited &&
                       yyjson_mut_obj_get(yyjson_mut_doc_get_root(response), "failed_operation");
    test_check(rollback_ok, "MCP graph edit batches reject invalid operations atomically");
    yyjson_mut_doc_free(response);
    free(source);
}
