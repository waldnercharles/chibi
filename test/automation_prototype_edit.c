#include "automation/prototype_edit.h"
#include "suite_internal.h"
#include "third_party/yyjson/yyjson.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

static int edit_request(const char *source, const char *request_json, char **edited, yyjson_mut_doc **response_doc)
{
    yyjson_doc *request_doc = yyjson_read(request_json, strlen(request_json), 0);
    if (!request_doc) {
        return CHIBI_AUTOMATION_INVALID_REQUEST;
    }
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *result = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, result);
    game_prototype_registry registry = {
        .components = GAME_REGISTRY_COMPONENTS,
        .component_count = GAME_REGISTRY_COMPONENT_COUNT,
    };
    char error[256] = { 0 };
    int status = automation_prototype_edit(
        source,
        &registry,
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

void test_automation_prototype_edit(void)
{
    const char *source = "name \"PrototypeEdit\"\n"
                         "components {\n"
                         "    GameTransform {\n"
                         "        x 1\n"
                         "        y 2\n"
                         "    }\n"
                         "}\n";
    char *edited = NULL;
    yyjson_mut_doc *response = NULL;
    int status = edit_request(
        source,
        "{\"operations\":["
        "{\"op\":\"add_component\",\"component\":\"GameMovement\","
        "\"fields\":{\"speed\":90}},"
        "{\"op\":\"set_field\",\"component\":\"GameTransform\","
        "\"field\":\"x\",\"value\":12.5},"
        "{\"op\":\"add_behavior\",\"behavior\":\"PhaseWardenBehavior\"}]}\n",
        &edited,
        &response
    );
    game_prototype_registry registry = {
        .components = GAME_REGISTRY_COMPONENTS,
        .component_count = GAME_REGISTRY_COMPONENT_COUNT,
    };
    char error[256] = { 0 };
    game_prototype *prototype = edited
                                  ? game_prototype_load(edited, &registry, error, sizeof error)
                                  : NULL;
    pin_value x = { 0 };
    pin_value speed = { 0 };
    bool semantic_edit = status == CHIBI_AUTOMATION_OK && prototype &&
                         game_prototype_read_field(prototype, "GameTransform", "x", &x) ==
                             0 &&
                         game_prototype_read_field(prototype, "GameMovement", "speed", &speed) ==
                             0 &&
                         x.type == PIN_F32 && x.f == 12.5f && speed.f == 90.0f &&
                         game_prototype_has_graph(prototype, "PhaseWardenBehavior") &&
                         yyjson_mut_obj_get(yyjson_mut_doc_get_root(response), "operation_count");
    test_check(semantic_edit, "MCP prototype edits apply reflected fields, defaults, and behavior attachments");
    game_prototype_free(prototype);
    yyjson_mut_doc_free(response);

    char *roundtrip = edited;
    edited = NULL;
    response = NULL;
    status = edit_request(
        roundtrip,
        "{\"operations\":["
        "{\"op\":\"remove_behavior\",\"behavior\":\"PhaseWardenBehavior\"},"
        "{\"op\":\"remove_component\",\"component\":\"GameMovement\"}]}\n",
        &edited,
        &response
    );
    prototype = edited ? game_prototype_load(edited, &registry, error, sizeof error)
                       : NULL;
    bool removal_edit = status == CHIBI_AUTOMATION_OK && prototype &&
                        !game_prototype_has_graph(prototype, "PhaseWardenBehavior") &&
                        game_prototype_read_field(prototype, "GameMovement", "speed", &speed) !=
                            0;
    test_check(removal_edit, "MCP prototype edits remove components and behavior attachments atomically");
    game_prototype_free(prototype);
    yyjson_mut_doc_free(response);
    free(edited);

    edited = NULL;
    response = NULL;
    status = edit_request(
        roundtrip,
        "{\"operations\":[{\"op\":\"set_field\",\"component\":"
        "\"GameTransform\","
        "\"field\":\"missing\",\"value\":1}]}\n",
        &edited,
        &response
    );
    bool rollback = status == CHIBI_AUTOMATION_VALIDATION_ERROR && !edited &&
                    yyjson_mut_obj_get(yyjson_mut_doc_get_root(response), "failed_operation");
    test_check(rollback, "MCP prototype edit batches reject unknown fields without publishing a partial clone");
    yyjson_mut_doc_free(response);
    free(edited);
    free(roundtrip);
}
