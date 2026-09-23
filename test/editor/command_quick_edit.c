#include "editor/commands/quick_edit.h"
#include "test/suite_internal.h"

#include <yyjson.h>

static const char *const catalog_json =
    "{\"revision\":\"revision-1\",\"document\":\"scene:test\","
    "\"capabilities\":[\"set\"],\"objects\":["
    "{\"id\":\"entity:scene:4\",\"name\":\"Ship\",\"kind\":\"entity\","
    "\"prototype\":\"Player\",\"fields\":["
    "{\"id\":\"GAME.GameAppearance.color\",\"name\":\"color\","
    "\"label\":\"Appearance Color\",\"component\":\"GameAppearance\","
    "\"component_key\":\"GAME.GameAppearance\",\"type\":\"color\","
    "\"integer\":false,\"value\":4294967295}]},"
    "{\"id\":\"prototype:player\",\"name\":\"Player\",\"kind\":\"prototype\","
    "\"fields\":["
    "{\"id\":\"GAME.GameAppearance.color\",\"name\":\"color\","
    "\"label\":\"Appearance Color\",\"component\":\"GameAppearance\","
    "\"component_key\":\"GAME.GameAppearance\",\"type\":\"color\","
    "\"integer\":false,\"value\":4294967295},"
    "{\"id\":\"GAME.GameAppearance.width\",\"name\":\"width\","
    "\"label\":\"Appearance Width\",\"component\":\"GameAppearance\","
    "\"component_key\":\"GAME.GameAppearance\",\"type\":\"number\","
    "\"integer\":false,\"value\":48},"
    "{\"id\":\"GAME.GameCollider.shape\",\"name\":\"shape\","
    "\"label\":\"Collider Shape\",\"component\":\"GameCollider\","
    "\"component_key\":\"GAME.GameCollider\",\"type\":\"choice\",\"value\":1,"
    "\"choices\":[{\"label\":\"Circle\",\"value\":0},"
    "{\"label\":\"Box\",\"value\":1}]},"
    "{\"id\":\"GAME.GameCollider.width\",\"name\":\"width\","
    "\"label\":\"Collider Width\",\"component\":\"GameCollider\","
    "\"component_key\":\"GAME.GameCollider\",\"type\":\"number\","
    "\"integer\":false,\"value\":18},"
    "{\"id\":\"GAME.GameParticleEmitter.size\",\"name\":\"size\","
    "\"label\":\"Particle Size\",\"component\":\"GameParticleEmitter\","
    "\"component_key\":\"GAME.GameParticleEmitter\",\"type\":\"number\","
    "\"integer\":false,\"value\":2},"
    "{\"id\":\"GAME.GameBody.base_speed\",\"name\":\"base_speed\","
    "\"label\":\"Base Speed\",\"component\":\"GameBody\","
    "\"component_key\":\"GAME.GameBody\",\"type\":\"number\","
    "\"integer\":false,\"value\":3},"
    "{\"id\":\"GAME.GameDrive.max_speed\",\"name\":\"max_speed\","
    "\"label\":\"Maximum Speed\",\"component\":\"GameDrive\","
    "\"component_key\":\"GAME.GameDrive\",\"type\":\"number\","
    "\"integer\":false,\"value\":7}]}]}";

static char *packet(const char *command)
{
    yyjson_doc *catalog_doc = yyjson_read(catalog_json, strlen(catalog_json), 0);
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_strcpy(doc, root, "command", command);
    yyjson_mut_obj_add_val(doc, root, "catalog", yyjson_val_mut_copy(doc, yyjson_doc_get_root(catalog_doc)));
    char *json = yyjson_mut_write(doc, 0, NULL);
    yyjson_mut_doc_free(doc);
    yyjson_doc_free(catalog_doc);
    return json;
}

static yyjson_val *result_root(char *result, yyjson_doc **doc)
{
    *doc = result ? yyjson_read(result, strlen(result), 0) : NULL;
    return *doc ? yyjson_doc_get_root(*doc) : NULL;
}

static const char *string_value(yyjson_val *object, const char *key)
{
    const char *value = yyjson_get_str(yyjson_obj_get(object, key));
    return value ? value : "";
}

static void expect_value(
    const char *command,
    const char *target,
    const char *field,
    double expected,
    const char *message
)
{
    char *request = packet(command);
    char *result = editor_command_quick_edit(request);
    yyjson_doc *doc = NULL;
    yyjson_val *root = result_root(result, &doc);
    yyjson_val *operation = yyjson_arr_get(yyjson_obj_get(root, "operations"), 0);
    bool ok = root && operation &&
              !strcmp(string_value(root, "status"), "needs_review") &&
              !strcmp(string_value(root, "provider"), "quick_edit") &&
              !strcmp(string_value(root, "revision"), "revision-1") &&
              !strcmp(string_value(operation, "target"), target) &&
              !strcmp(string_value(operation, "field"), field) &&
              fabs(yyjson_get_num(yyjson_obj_get(operation, "value")) - expected) <
                  0.00001;
    test_check(ok, message);
    yyjson_doc_free(doc);
    free(result);
    free(request);
}

static void expect_abstain(const char *command, const char *message)
{
    char *request = packet(command);
    char *result = editor_command_quick_edit(request);
    test_check(result == NULL, message);
    free(result);
    free(request);
}

void test_editor_command_quick_edit()
{
    expect_value("Change Ship's color to blue", "prototype:player", "GAME.GameAppearance.color", 0x0000ffff, "quick edit scopes a named instance's color change to its prototype by default");
    expect_value(
        "Change only this instance Ship's color to #12345678",
        "entity:scene:4",
        "GAME.GameAppearance.color",
        0x12345678,
        "quick edit keeps an explicitly scoped instance and preserves exact "
        "RGBA"
    );
    expect_value(
        "Change Ship copy's color to blue",
        "entity:scene:4",
        "GAME.GameAppearance.color",
        0x0000ffff,
        "quick edit keeps copy-scoped edits on the instance"
    );
    expect_value(
        "Change Ship's hitbox to Circle",
        "prototype:player",
        "GAME.GameCollider.shape",
        0,
        "quick edit copies the observed collider choice value"
    );
    expect_value(
        "Change Player's hitbox to a circle",
        "prototype:player",
        "GAME.GameCollider.shape",
        0,
        "quick edit accepts an article before an exact collider choice"
    );
    expect_value("Set Ship's visual width to 80", "prototype:player", "GAME.GameAppearance.width", 80, "quick edit selects the observed appearance width rather than collider width");
    expect_value("Increase Ship's particle size by 3", "prototype:player", "GAME.GameParticleEmitter.size", 5, "quick edit computes an explicit relative numeric change from the catalog value");
    expect_value(
        "Set Ship's particle size to 12",
        "prototype:player",
        "GAME.GameParticleEmitter.size",
        12,
        "quick edit accepts an explicit numeric value"
    );

    expect_abstain("Increase Ship's particle size by one quarter", "quick edit defers vague fractional amounts");
    expect_abstain("Reduce Ship's particle size by 25 percent", "quick edit defers percentage changes");
    expect_abstain(
        "Reduce Ship's particle size by eight pixels",
        "quick edit defers spelled-out quantities and unit-bearing changes"
    );
    expect_abstain("Change Ship's hitbox from Box to Circle, without changing appearance width", "quick edit defers compound edits and exclusions");
    expect_abstain("Set Ship's box hitbox height field to 28", "quick edit does not mistake a collider dimension for its shape");
    expect_abstain("Set Ship's shape to Circle", "quick edit does not confuse collider and visual shape");
    expect_abstain("Set Ship's width to 80", "quick edit defers width requests without explicit visual scope");
    expect_abstain("Change every Enemy instance's color to blue", "quick edit defers multi-instance scope");
    expect_abstain("Change color to blue", "quick edit defers an unscoped target");
    expect_abstain("Increase Ship's speed by 2", "quick edit defers when multiple observed numeric fields match");
    expect_abstain(
        "Set Ship enum to 0 (Circle), not appearance color",
        "quick edit does not infer a property from an enum ordinal"
    );
}
