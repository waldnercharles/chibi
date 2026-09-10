#include "automation/controller.h"
#include "suite_internal.h"
#include "third_party/yyjson/yyjson.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

static bool response_ok(const char *response, const char *id)
{
    yyjson_doc *doc = yyjson_read(response, strlen(response), 0);
    if (!doc) {
        return false;
    }
    yyjson_val *root = yyjson_doc_get_root(doc);
    yyjson_val *response_id = yyjson_obj_get(root, "id");
    yyjson_val *ok = yyjson_obj_get(root, "ok");
    bool valid = yyjson_is_str(response_id) &&
                 !strcmp(yyjson_get_str(response_id), id) &&
                 yyjson_is_bool(ok) && yyjson_get_bool(ok);
    yyjson_doc_free(doc);
    return valid;
}

static bool response_result_field(const char *response, const char *id, const char *field)
{
    yyjson_doc *doc = yyjson_read(response, strlen(response), 0);
    if (!doc) {
        return false;
    }
    yyjson_val *root = yyjson_doc_get_root(doc);
    yyjson_val *result = yyjson_obj_get(root, "result");
    bool valid = response_ok(response, id) && result && yyjson_obj_get(result, field);
    yyjson_doc_free(doc);
    return valid;
}

static bool response_error(const char *response, const char *code)
{
    yyjson_doc *doc = yyjson_read(response, strlen(response), 0);
    if (!doc) {
        return false;
    }
    yyjson_val *root = yyjson_doc_get_root(doc);
    yyjson_val *ok = yyjson_obj_get(root, "ok");
    yyjson_val *error = yyjson_obj_get(root, "error");
    yyjson_val *actual = error ? yyjson_obj_get(error, "code") : NULL;
    bool valid = yyjson_is_bool(ok) && !yyjson_get_bool(ok) &&
                 yyjson_is_str(actual) && !strcmp(yyjson_get_str(actual), code);
    yyjson_doc_free(doc);
    return valid;
}

void run_automation_checks(void)
{
    chibi_ctl_context context = { .root = "." };
    int status = -1;
    char *response = chibi_ctl_handle(
        &context,
        "{\"version\":1,\"id\":\"status\",\"method\":\"project.status\"}",
        &status
    );
    bool status_ok = response && status == CHIBI_CTL_OK &&
                     response_result_field(response, "status", "project") &&
                     strstr(response, "chibi") &&
                     strstr(response, "reflection_node_count");
    test_check(status_ok, "automation status reports the reflected project");
    free(response);

    response = chibi_ctl_handle(&context, "{\"version\":1,\"id\":\"list\",\"method\":\"assets.list\"}", &status);
    bool list_ok = response && status == CHIBI_CTL_OK &&
                   response_result_field(response, "list", "assets");
    test_check(list_ok, "automation lists authored JSON assets");
    free(response);

    response = chibi_ctl_handle(
        &context,
        "{\"version\":1,\"id\":\"read\",\"method\":\"asset.read\",\"params\":{"
        "\"path\":\"content/behaviors/ArenaShipMotionBehavior.json\"}}",
        &status
    );
    bool read_ok = response && status == CHIBI_CTL_OK &&
                   response_result_field(response, "read", "asset");
    test_check(read_ok, "automation reads an authored asset as JSON");
    free(response);

    response = chibi_ctl_handle(
        &context,
        "{\"version\":1,\"id\":\"validate\",\"method\":\"asset.validate\","
        "\"params\":{\"path\":\"content/behaviors/"
        "ArenaShipMotionBehavior.json\"}}",
        &status
    );
    bool validate_ok = response && status == CHIBI_CTL_OK &&
                       response_result_field(response, "validate", "valid") &&
                       strstr(response, "\"valid\":true");
    test_check(validate_ok, "automation validates a behavior through the compiler");
    free(response);

    response = chibi_ctl_handle(
        &context,
        "{\"version\":1,\"id\":\"missing\",\"method\":\"asset.read\","
        "\"params\":{\"path\":\"content/nope.json\"}}",
        &status
    );
    test_check(
        response && status == CHIBI_CTL_NOT_FOUND &&
            response_error(response, "asset_not_found"),
        "automation reports missing assets with a stable error"
    );
    free(response);

    response = chibi_ctl_handle(&context, "not json", &status);
    test_check(
        response && status == CHIBI_CTL_INVALID_REQUEST &&
            response_error(response, "invalid_json"),
        "automation reports malformed requests with a stable error"
    );
    free(response);
}
