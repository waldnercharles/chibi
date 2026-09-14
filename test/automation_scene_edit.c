#include "automation/scene_edit.h"
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
    char error[256] = { 0 };
    int status = automation_scene_edit(
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

void test_automation_scene_edit(void)
{
    const char *source = "{\"version\":1,\"name\":\"SceneEdit\","
                         "\"presentation\":\"2d\","
                         "\"entities\":[]}";
    char *edited = NULL;
    yyjson_mut_doc *response = NULL;
    int status = edit_request(
        source,
        "{\"operations\":["
        "{\"op\":\"set_presentation\",\"presentation\":\"3d\"},"
        "{\"op\":\"add_entity\",\"name\":\"Warden\","
        "\"prototype\":\"PhaseWarden\","
        "\"components\":{\"game.GameTransform\":{\"x\":3,\"y\":4}},"
        "\"retained_components\":{\"game.GameTransform\":{\"x\":5,\"y\":6}},"
        "\"behaviors\":[\"PhaseWardenBehavior\"]},"
        "{\"op\":\"add_behavior\",\"entity\":\"Warden\","
        "\"behavior\":\"CombatFeedback\"},"
        "{\"op\":\"set_entity_components\",\"entity\":\"Warden\","
        "\"components\":{\"game.GameTransform\":{\"x\":9,\"y\":10}}},"
        "{\"op\":\"set_entity_behaviors\",\"entity\":\"Warden\","
        "\"behaviors\":[\"CombatFeedback\"]}]} ",
        &edited,
        &response
    );
    char error[256] = { 0 };
    game_scene_document *scene = edited
                                   ? game_scene_document_load(edited, error, sizeof error)
                                   : NULL;
    game_scene_entity *entity = scene && acount(scene->entities) == 1
                                  ? &scene->entities[0]
                                  : NULL;
    bool semantic_edit = status == CHIBI_AUTOMATION_OK && entity &&
                         scene->perspective && !strcmp(entity->name, "Warden") &&
                         !strcmp(entity->prototype, "PhaseWarden") &&
                         acount(entity->behaviors) == 1 &&
                         !strcmp(entity->behaviors[0], "CombatFeedback") &&
                         strstr(entity->components_json, "\"x\": 9") &&
                         strstr(entity->retained_json, "\"x\": 5") &&
                         yyjson_mut_get_uint(
                             yyjson_mut_obj_get(yyjson_mut_doc_get_root(response), "operation_count")
                         ) == 5;
    test_check(semantic_edit, "MCP scene edits compose entities, overrides, retained values, and behaviors semantically");
    game_scene_document_free(scene);
    yyjson_mut_doc_free(response);

    char *removed = NULL;
    response = NULL;
    status = edit_request(
        edited,
        "{\"operations\":[{\"op\":\"remove_entity\",\"entity\":\"Warden\"}]}",
        &removed,
        &response
    );
    scene = removed ? game_scene_document_load(removed, error, sizeof error) : NULL;
    bool removal = status == CHIBI_AUTOMATION_OK && scene &&
                   acount(scene->entities) == 0;
    test_check(removal, "MCP scene edits remove entities by semantic name");
    game_scene_document_free(scene);
    yyjson_mut_doc_free(response);
    free(removed);
    free(edited);

    edited = NULL;
    response = NULL;
    status = edit_request(
        source,
        "{\"operations\":[{\"op\":\"set_entity_components\","
        "\"entity\":\"Missing\",\"components\":{}}]}",
        &edited,
        &response
    );
    bool rollback = status == CHIBI_AUTOMATION_VALIDATION_ERROR && !edited &&
                    yyjson_mut_obj_get(yyjson_mut_doc_get_root(response), "failed_operation");
    test_check(rollback, "MCP scene edit batches reject missing entities without a partial result");
    yyjson_mut_doc_free(response);
    free(edited);
}
