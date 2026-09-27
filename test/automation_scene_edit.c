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

static void rename_entities()
{
    const char
        *source = "{\"version\":1,\"name\":\"Rename\",\"entities\":["
                  "{\"name\":\"First\",\"prototype\":\"Point\","
                  "\"components\":{\"GAME.GameTransform\":{\"x\":3}},"
                  "\"retained_components\":{\"GAME.GameTransform\":{\"x\":5}},"
                  "\"behaviors\":[\"Pulse\"]},"
                  "{\"name\":\"Second\",\"prototype\":\"Point\"}]}";
    char *edited = NULL;
    yyjson_mut_doc *response = NULL;
    int status = edit_request(
        source,
        "{\"operations\":[{\"op\":\"rename_entity\",\"entity\":\"First\","
        "\"name\":\"Hero\"},"
        "{\"op\":\"rename_entity\",\"entity\":\"Hero\",\"name\":\"Hero\"},"
        "{\"op\":\"add_behavior\",\"entity\":\"Hero\",\"behavior\":\"Move\"}]}",
        &edited,
        &response
    );
    char error[256] = { 0 };
    game_scene_document *scene = edited
                                   ? game_scene_document_load(edited, error, sizeof error)
                                   : NULL;
    test_check(
        status == CHIBI_AUTOMATION_OK && scene &&
            game_scene_document_entity_index(scene, "First") == -1 &&
            game_scene_document_entity_index(scene, "Hero") == 0 &&
            game_scene_document_entity_index(scene, "Second") == 1 &&
            !strcmp(scene->entities[0].prototype, "Point") &&
            strstr(scene->entities[0].components_json, "\"x\": 3") &&
            strstr(scene->entities[0].retained_json, "\"x\": 5") &&
            acount(scene->entities[0].behaviors) == 2 &&
            !strcmp(scene->entities[0].behaviors[0], "Pulse") &&
            !strcmp(scene->entities[0].behaviors[1], "Move"),
        "MCP rename preserves object data and order, accepts its own name, and "
        "supports later edits by the new name"
    );
    game_scene_document_free(scene);
    free(edited);
    yyjson_mut_doc_free(response);

    const struct
    {
        const char *operation;
        const char *error;
        int status;
    } invalid[] = {
        { "{\"op\":\"rename_entity\",\"entity\":\"Hero\",\"name\":\"Second\"}",
          "Another object",
          CHIBI_AUTOMATION_VALIDATION_ERROR },
        { "{\"op\":\"rename_entity\",\"entity\":\"Hero\",\"name\":\"\"}",
          "needs a name",
          CHIBI_AUTOMATION_VALIDATION_ERROR },
        { "{\"op\":\"rename_entity\",\"entity\":\"First\",\"name\":\"Other\"}",
          "does not exist",
          CHIBI_AUTOMATION_VALIDATION_ERROR },
        { "{\"op\":\"rename_entity\",\"entity\":\"Hero\"}",
          "name must be",
          CHIBI_AUTOMATION_INVALID_REQUEST },
        { "{\"op\":\"rename_entity\",\"entity\":\"Hero\",\"name\":4}",
          "name must be",
          CHIBI_AUTOMATION_INVALID_REQUEST },
        { "{\"op\":\"rename_entity\",\"entity\":\"Hero\",\"name\":"
          "\"Other\\u0000Hidden\"}",
          "name must be",
          CHIBI_AUTOMATION_INVALID_REQUEST },
    };
    for (int i = 0; i < (int)(sizeof invalid / sizeof *invalid); i++) {
        char request[512];
        snprintf(
            request,
            sizeof request,
            "{\"operations\":[{\"op\":\"rename_entity\",\"entity\":\"First\","
            "\"name\":\"Hero\"},%s]}",
            invalid[i].operation
        );
        edited = NULL;
        status = edit_request(source, request, &edited, &response);
        yyjson_mut_val *result = yyjson_mut_doc_get_root(response);
        const char *message = yyjson_mut_get_str(yyjson_mut_obj_get(result, "error"));
        test_check(
            status == invalid[i].status && !edited && message &&
                strstr(message, invalid[i].error) &&
                yyjson_mut_get_int(yyjson_mut_obj_get(result, "failed_operation")) == 1,
            "MCP invalid rename rejects the entire batch with the failing "
            "operation and reason"
        );
        free(edited);
        yyjson_mut_doc_free(response);
    }
}

void test_automation_scene_edit(void)
{
    rename_entities();
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
        "\"components\":{\"GAME.GameTransform\":{\"x\":3,\"y\":4}},"
        "\"retained_components\":{\"GAME.GameTransform\":{\"x\":5,\"y\":6}},"
        "\"behaviors\":[\"PhaseWardenBehavior\"]},"
        "{\"op\":\"add_behavior\",\"entity\":\"Warden\","
        "\"behavior\":\"CombatFeedback\"},"
        "{\"op\":\"set_entity_components\",\"entity\":\"Warden\","
        "\"components\":{\"GAME.GameTransform\":{\"x\":9,\"y\":10}}},"
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
