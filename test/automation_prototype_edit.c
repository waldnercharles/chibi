#include "automation/prototype_edit.h"
#include "automation/prototype_instance.h"
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

static const char GUARD[] = "name \"Guard\"\n"
                            "components {\n"
                            "    GameTransform {\n"
                            "        x 5\n"
                            "        y 6\n"
                            "    }\n"
                            "    GameHealth {\n"
                            "        current 10\n"
                            "        maximum 100\n"
                            "    }\n"
                            "}\n";

static const char
    YARD[] = "version 1\n"
             "name \"Yard\"\n"
             "(array)entities {\n"
             "    - {\n"
             "        name \"Gate\"\n"
             "        prototype \"Guard\"\n"
             "        components {\n"
             "            GAME.GameTransform x=40.0\n"
             "            GAME.GameHealth current=30 maximum=120\n"
             "        }\n"
             "        (array)behaviors \"Patrol\"\n"
             "    }\n"
             "}\n";

static int from_instance(const char *fields_json, const char *name, char **text, yyjson_mut_doc **doc)
{
    yyjson_doc *fields = fields_json
                           ? yyjson_read(fields_json, strlen(fields_json), 0)
                           : NULL;
    *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *result = yyjson_mut_obj(*doc);
    yyjson_mut_doc_set_root(*doc, result);
    game_prototype_registry registry = {
        .components = GAME_REGISTRY_COMPONENTS,
        .component_count = GAME_REGISTRY_COMPONENT_COUNT,
    };
    automation_instance instance = { YARD,
                                     "Gate",
                                     GUARD,
                                     name,
                                     fields ? yyjson_doc_get_root(fields) : NULL };
    char error[256] = { 0 };
    int status = automation_prototype_from_instance(&instance, &registry, *doc, result, text, error, sizeof error);
    yyjson_doc_free(fields);
    return status;
}

/* GameHealth's `field`, or GameTransform's x for "x", of the prototype text. */
static int field_of(const char *text, const char *field)
{
    game_prototype_registry registry = {
        .components = GAME_REGISTRY_COMPONENTS,
        .component_count = GAME_REGISTRY_COMPONENT_COUNT,
    };
    char error[256];
    game_prototype *prototype = game_prototype_load(text, &registry, error, sizeof error);
    bool x = !strcmp(field, "x");
    pin_value value = { 0 };
    bool read = prototype &&
                game_prototype_read_field(prototype, x ? "GameTransform" : "GameHealth", field, &value) ==
                    0;
    game_prototype_free(prototype);
    return !read ? -1 : x ? (int)value.f : value.i;
}

/* buildPrototype's source_instance: an entity's overridden fields, all or
   those named, apply to its prototype, never its place; a field it does
   not override is refused; with a name, the whole tuned entity becomes a
   prototype that keeps its prototype's place and gains its behaviors. */
static void prototype_from_instance(void)
{
    char *text = NULL, *named = NULL, *refused = NULL, *made = NULL;
    yyjson_mut_doc *doc = NULL;
    char instance[16] = "";
    int all = from_instance(NULL, NULL, &text, &doc);
    yyjson_mut_val *applied = yyjson_mut_obj_get(yyjson_mut_doc_get_root(doc), "applied_fields");
    bool listed = yyjson_mut_arr_size(applied) == 2;
    yyjson_mut_doc_free(doc);
    int one = from_instance(
        "[{\"component\":\"GAME.GameHealth\",\"field\":\"maximum\"}]",
        NULL,
        &named,
        &doc
    );
    yyjson_mut_doc_free(doc);
    int bad = from_instance("[{\"component\":\"GameTransform\",\"field\":\"x\"}]", NULL, &refused, &doc);
    yyjson_mut_doc_free(doc);
    int copy = from_instance(NULL, "GateGuard", &made, &doc);
    yyjson_mut_doc_free(doc);
    int prototype = automation_instance_prototype(YARD, "Gate", instance, sizeof instance, NULL, 0);
    test_check(
        all == CHIBI_AUTOMATION_OK && listed && field_of(text, "current") == 30 &&
            field_of(text, "maximum") == 120 && field_of(text, "x") == 5 &&
            one == CHIBI_AUTOMATION_OK && field_of(named, "current") == 10 &&
            field_of(named, "maximum") == 120,
        "MCP source_instance applies the entity's overridden fields, all or "
        "those named, and never its place"
    );
    test_check(
        bad == CHIBI_AUTOMATION_INVALID_REQUEST && !refused &&
            copy == CHIBI_AUTOMATION_OK && strstr(made, "GateGuard") &&
            strstr(made, "Patrol") && field_of(made, "x") == 5 &&
            field_of(made, "current") == 30 &&
            prototype == CHIBI_AUTOMATION_OK && !strcmp(instance, "Guard"),
        "MCP source_instance refuses fields the entity does not override, and "
        "saves the tuned entity as a new prototype"
    );
    free(text);
    free(named);
    free(made);
}

void test_automation_prototype_edit(void)
{
    prototype_from_instance();
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
