#include "editor/commands/internal.h"
#include "editor/commands/model.h"
#include "editor/ui/command_transport.h"
#include "test/suite_internal.h"

#include <yyjson.h>

static yyjson_val *test_json_root(const char *json, yyjson_doc **document)
{
    *document = yyjson_read(json, strlen(json), 0);
    return *document ? yyjson_doc_get_root(*document) : NULL;
}

static const char *last_user_message(yyjson_val *body)
{
    yyjson_val *messages = yyjson_obj_get(body, "messages");
    for (int i = (int)yyjson_arr_size(messages) - 1; i >= 0; i--) {
        yyjson_val *message = yyjson_arr_get(messages, i);
        if (!strcmp(command_string(message, "role"), "user")) {
            return command_string(message, "content");
        }
    }
    return "";
}

void test_editor_command_model()
{
    char previous_host[256];
    char previous_model[128];
    bool previous_schema = editor_command_model_json_schema();
    char error[256];
    editor_command_model_get_settings(previous_host, sizeof previous_host, previous_model, sizeof previous_model);
    test_check(
        editor_command_model_set_settings("http://127.0.0.1:19999/v1", "test-model", error, sizeof error),
        "command model accepts an OpenAI-compatible host"
    );

    char transport_error[128] = { 0 };
    char *plain_body = editor_command_response_body(
        "HTTP/1.1 200 OK\r\nContent-Length: 11\r\n\r\n{\"ok\":true}",
        transport_error,
        sizeof transport_error
    );
    test_check(plain_body && !strcmp(plain_body, "{\"ok\":true}"), "command transport reads a fixed HTTP body");
    free(plain_body);
    char *chunked_body = editor_command_response_body(
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
        "7\r\n{\"ok\":t\r\n4\r\nrue}\r\n0\r\n\r\n",
        transport_error,
        sizeof transport_error
    );
    test_check(chunked_body && !strcmp(chunked_body, "{\"ok\":true}"), "command transport decodes chunked model responses");
    free(chunked_body);

    const char *packet = "{\"command\":\"make it green\",\"catalog\":{"
                         "\"revision\":\"r1\",\"document\":\"doc\","
                         "\"objects\":[{\"id\":\"object:1\",\"name\":\"Thing\","
                         "\"kind\":\"prototype\",\"fields\":[{\"id\":\"color\","
                         "\"type\":\"color\",\"value\":287454020}]}],"
                         "\"capabilities\":[\"set\"],\"panels\":[]}}";
    char *request = editor_command_model_prepare(packet, error, sizeof error);
    test_check(request != NULL, "command model builds a provider request");
    if (request) {
        yyjson_doc *request_doc = NULL;
        yyjson_val *request_root = test_json_root(request, &request_doc);
        const char *url = yyjson_get_str(yyjson_obj_get(request_root, "url"));
        const char *body = yyjson_get_str(yyjson_obj_get(request_root, "body"));
        test_check(
            url && !strcmp(url, "http://127.0.0.1:19999/v1/chat/completions"),
            "command model appends the chat completions endpoint"
        );
        test_check(
            body && strstr(body, "\"tools\"") && strstr(body, "submit_plan") &&
                strstr(body, "tool_choice"),
            "command model requires the named structured proposal tool"
        );
        test_check(body && !strstr(body, "response_format"), "command model does not mix JSON mode with function output");
        test_check(body && strstr(body, "make it green"), "command model sends the observed request");
        yyjson_doc_free(request_doc);
    }
    editor_command_model_set_json_schema(true);
    char *schema_request = editor_command_model_prepare(packet, error, sizeof error);
    yyjson_doc *schema_doc = NULL;
    yyjson_val *schema_root = schema_request
                                ? test_json_root(schema_request, &schema_doc)
                                : NULL;
    const char *schema_body = command_string(schema_root, "body");
    test_check(
        strstr(schema_body, "\"response_format\"") &&
            strstr(schema_body, "\"json_schema\"") &&
            !strstr(schema_body, "\"tools\"") &&
            !strstr(schema_body, "\"reasoning_effort\""),
        "OpenAI-compatible JSON schema mode omits tool calls"
    );
    yyjson_doc_free(schema_doc);
    free(schema_request);
    editor_command_model_set_json_schema(false);
    test_check(
        editor_command_model_set_settings("http://127.0.0.1:19999/api", "test-model", error, sizeof error),
        "command model accepts the native Ollama API host"
    );
    const char *focused_packet =
        "{\"command\":\"Change Scout's color to blue\",\"catalog\":{"
        "\"revision\":\"r1\",\"document\":\"scene\","
        "\"capabilities\":[\"set\"],\"panels\":[],\"objects\":["
        "{\"id\":\"entity:1\",\"name\":\"Scout\",\"kind\":\"entity\","
        "\"prototype\":\"Ship\"},"
        "{\"id\":\"/project/content/prototypes/"
        "Ship.json\",\"name\":\"Ship\",\"kind\":\"prototype\","
        "\"fields\":[{\"id\":\"GAME.GameAppearance.color\",\"name\":\"color\","
        "\"label\":\"Appearance "
        "Color\",\"type\":\"color\",\"value\":4294967295}]},"
        "{\"id\":\"/project/content/prototypes/"
        "Other.json\",\"name\":\"Other\",\"kind\":\"prototype\",\"fields\":[]}"
        "]}}";
    char *native_request = editor_command_model_prepare(focused_packet, error, sizeof error);
    test_check(native_request != NULL, "command model builds an Ollama schema request");
    if (native_request) {
        yyjson_doc *native_doc = NULL;
        yyjson_val *native_root = test_json_root(native_request, &native_doc);
        const char *url = yyjson_get_str(yyjson_obj_get(native_root, "url"));
        const char *body = yyjson_get_str(yyjson_obj_get(native_root, "body"));
        yyjson_doc *native_body_doc = NULL;
        yyjson_val *native_body_root = body ? test_json_root(body, &native_body_doc)
                                            : NULL;
        test_check(url && !strcmp(url, "http://127.0.0.1:19999/api/chat"), "command model appends the native Ollama chat endpoint");
        test_check(
            body && strstr(body, "\"format\"") && !strstr(body, "\"tools\""),
            "command model uses native schema output without tools"
        );
        test_check(
            body && strstr(body, "Scout") && strstr(body, "#0000FF"),
            "native Ollama prompt demonstrates a supported color edit"
        );
        test_check(
            body && strstr(body, "/project/content/prototypes/Ship.json") &&
                strstr(body, "Appearance Color") && !strstr(body, "Other.json"),
            "native Ollama prompt focuses the named entity and its prototype"
        );
        test_check(
            native_body_root &&
                yyjson_get_str(yyjson_obj_get(native_body_root, "think")) &&
                !strcmp(yyjson_get_str(yyjson_obj_get(native_body_root, "think")), "low"),
            "native Ollama requests low reasoning effort for editor commands"
        );
        test_check(
            native_body_root &&
                !strcmp(command_string(native_body_root, "keep_alive"), "30m"),
            "native Ollama keeps GPT-OSS resident for command iteration"
        );
        yyjson_doc_free(native_body_doc);
        yyjson_doc_free(native_doc);
    }
    free(native_request);

    test_check(
        editor_command_model_set_settings("http://127.0.0.1:11435/api", "gpt-oss:20b", error, sizeof error),
        "command model selects GPT-OSS"
    );
    char *fast_request = editor_command_model_prepare(focused_packet, error, sizeof error);
    yyjson_doc *fast_doc = NULL;
    yyjson_val *fast_root = fast_request ? test_json_root(fast_request, &fast_doc)
                                         : NULL;
    const char *fast_response = command_string(fast_root, "local_response");
    test_check(
        *fast_response && !yyjson_obj_get(fast_root, "url"),
        "GPT-OSS uses a catalog-grounded local choice for a clear property edit"
    );
    char *fast_result = editor_command_model_result(focused_packet, fast_response, error, sizeof error);
    yyjson_doc *fast_result_doc = NULL;
    yyjson_val *fast_result_root = fast_result
                                     ? test_json_root(fast_result, &fast_result_doc)
                                     : NULL;
    test_check(
        !strcmp(command_string(fast_result_root, "status"), "continue") &&
            !strcmp(command_string(fast_result_root, "provider"), "native"),
        "GPT-OSS local choice binds a native editor target without calling Laya"
    );
    yyjson_doc_free(fast_result_doc);
    yyjson_doc_free(fast_doc);
    free(fast_result);
    free(fast_request);
    editor_command_model_set_settings("http://127.0.0.1:19999/api", "test-model", error, sizeof error);

    const char *scope_packet =
        "{\"command\":\"Set Scout's color to #42C6A5FF\",\"catalog\":{"
        "\"revision\":\"r1\",\"document\":\"scene\",\"objects\":["
        "{\"id\":\"entity:1\",\"name\":\"Scout\",\"kind\":\"entity\","
        "\"prototype\":\"Ship\"},"
        "{\"id\":\"prototype:ship\",\"name\":\"Ship\",\"kind\":\"prototype\","
        "\"fields\":[{\"id\":\"GAME.GameAppearance.color\",\"name\":\"color\","
        "\"type\":\"color\",\"value\":4294967295}]}]}}";
    const char
        *scope_response = "{\"choices\":[{\"message\":{\"content\":\"{"
                          "\\\"decision\\\":\\\"act\\\","
                          "\\\"operations\\\":[{\\\"op\\\":\\\"set\\\","
                          "\\\"target\\\":\\\"entity:1\\\","
                          "\\\"field\\\":\\\"GAME.GameAppearance.color\\\","
                          "\\\"value\\\":\\\"#FFFFFF\\\"}]}\"}}]}";
    char *scope_result = editor_command_model_result(scope_packet, scope_response, error, sizeof error);
    yyjson_doc *scope_doc = NULL;
    yyjson_val *scope_root = scope_result ? test_json_root(scope_result, &scope_doc)
                                          : NULL;
    yyjson_val *scope_operation = yyjson_arr_get(yyjson_obj_get(scope_root, "operations"), 0);
    test_check(
        !strcmp(command_string(scope_operation, "target"), "prototype:ship") &&
            yyjson_get_uint(yyjson_obj_get(scope_operation, "value")) == 0x42c6a5ffu,
        "command model resolves scene names to prototype edits and copies an "
        "explicit RGBA literal"
    );
    yyjson_doc_free(scope_doc);
    free(scope_result);

    const char *semantic_packet =
        "{\"command\":\"Increase the size of the particles\",\"catalog\":{"
        "\"revision\":\"r1\",\"document\":\"scene\","
        "\"capabilities\":[\"set\"],\"panels\":[],\"objects\":["
        "{\"id\":\"prototype:burst\",\"name\":\"Burst\","
        "\"kind\":\"prototype\",\"components\":[\"GameParticleEmitter\"],"
        "\"fields\":[{\"id\":\"particle.size\",\"name\":\"size\","
        "\"label\":\"Particle Size\",\"component\":\"GameParticleEmitter\","
        "\"type\":\"number\",\"value\":2}]},"
        "{\"id\":\"prototype:wall\",\"name\":\"Wall\","
        "\"kind\":\"prototype\",\"components\":[\"GameCollider\"],"
        "\"fields\":[{\"id\":\"wall.width\",\"name\":\"width\","
        "\"label\":\"Wall Width\",\"component\":\"GameCollider\","
        "\"type\":\"number\",\"value\":20}]}]}}";
    char *semantic_request = editor_command_model_prepare(semantic_packet, error, sizeof error);
    yyjson_doc *semantic_request_doc = NULL, *semantic_body_doc = NULL;
    yyjson_val *semantic_request_root = semantic_request
                                          ? test_json_root(semantic_request, &semantic_request_doc)
                                          : NULL;
    const char *semantic_body = semantic_request_root
                                  ? command_string(semantic_request_root, "body")
                                  : "";
    yyjson_val *semantic_body_root = *semantic_body
                                       ? test_json_root(semantic_body, &semantic_body_doc)
                                       : NULL;
    const char *semantic_prompt = semantic_body_root
                                    ? last_user_message(semantic_body_root)
                                    : "";
    test_check(
        strstr(semantic_prompt, "Particle Size") && !strstr(semantic_prompt, "Wall Width"),
        "native Ollama context selects semantically relevant fields"
    );
    yyjson_doc_free(semantic_body_doc);
    yyjson_doc_free(semantic_request_doc);
    free(semantic_request);
    editor_command_model_set_settings("http://127.0.0.1:19999/v1", "test-model", error, sizeof error);

    const char *provider_response =
        "{\"choices\":[{\"message\":{\"content\":"
        "\"{\\\"decision\\\":\\\"act\\\",\\\"operations\\\":[{"
        "\\\"op\\\":\\\"set\\\",\\\"target\\\":\\\"object:1\\\","
        "\\\"field\\\":\\\"color\\\",\\\"value\\\":\\\"#00ff00\\\","
        "\\\"alpha\\\":12,\\\"destination\\\":\\\"\\\","
        "\\\"pin\\\":\\\"\\\",\\\"result\\\":\\\"\\\","
        "\\\"result_target\\\":\\\"\\\"}],\\\"question\\\":\\\"\\\","
        "\\\"choices\\\":[],\\\"unsupported\\\":\\\"\\\"}\"}}]}";
    char *result = editor_command_model_result(packet, provider_response, error, sizeof error);
    test_check(result != NULL, "command model parses a provider proposal");
    if (result) {
        yyjson_doc *result_doc = NULL;
        yyjson_val *result_root = test_json_root(result, &result_doc);
        yyjson_val *operation = yyjson_arr_get(yyjson_obj_get(result_root, "operations"), 0);
        test_check(
            !strcmp(command_string(result_root, "provider"), "test-model") &&
                !yyjson_obj_get(result_root, "resolver"),
            "command model identifies the selected provider without a sidecar "
            "resolver"
        );
        test_check(
            yyjson_get_uint(yyjson_obj_get(operation, "value")) == 0x00ff000cu,
            "command model normalizes color values before execution"
        );
        yyjson_doc_free(result_doc);
    }

    const char *rgba_response =
        "{\"choices\":[{\"message\":{\"content\":"
        "\"{\\\"decision\\\":\\\"act\\\",\\\"operations\\\":[{"
        "\\\"op\\\":\\\"set\\\",\\\"target\\\":\\\"object:1\\\","
        "\\\"field\\\":\\\"color\\\",\\\"value\\\":\\\"#42C6A5FF\\\","
        "\\\"alpha\\\":null}],\\\"question\\\":\\\"\\\","
        "\\\"choices\\\":[],\\\"unsupported\\\":\\\"\\\"}\"}}]}";
    free(result);
    result = editor_command_model_result(packet, rgba_response, error, sizeof error);
    if (result) {
        yyjson_doc *result_doc = NULL;
        yyjson_val *root = test_json_root(result, &result_doc);
        yyjson_val *operation = yyjson_arr_get(yyjson_obj_get(root, "operations"), 0);
        test_check(
            yyjson_get_uint(yyjson_obj_get(operation, "value")) == 0x42c6a5ffu,
            "command model accepts an explicit eight-digit RGBA value"
        );
        yyjson_doc_free(result_doc);
    } else {
        test_check(false, "command model accepts an explicit eight-digit RGBA value");
    }

    const char *ollama_response =
        "{\"total_duration\":250000000,\"load_duration\":10000000,"
        "\"prompt_eval_count\":200,\"prompt_eval_cached_count\":120,"
        "\"prompt_eval_duration\":50000000,\"eval_count\":25,"
        "\"eval_duration\":190000000,\"message\":{\"role\":\"assistant\","
        "\"content\":"
        "\"{\\\"decision\\\":\\\"unsupported\\\","
        "\\\"operations\\\":[],"
        "\\\"question\\\":\\\"\\\",\\\"choices\\\":[],"
        "\\\"unsupported\\\":\\\"No matching field\\\"}\"}}";
    free(result);
    result = editor_command_model_result(packet, ollama_response, error, sizeof error);
    test_check(result != NULL, "command model parses the native Ollama response envelope");
    if (result) {
        yyjson_doc *result_doc = NULL;
        yyjson_val *result_root = test_json_root(result, &result_doc);
        test_check(
            !strcmp(command_string(result_root, "status"), "unsupported"),
            "native Ollama proposals pass through shared command validation"
        );
        yyjson_val *metrics = yyjson_obj_get(result_root, "provider_metrics");
        test_check(
            yyjson_get_uint(yyjson_obj_get(metrics, "total_ms")) == 250 &&
                yyjson_get_uint(yyjson_obj_get(metrics, "load_ms")) == 10 &&
                yyjson_get_uint(yyjson_obj_get(metrics, "prompt_tokens")) == 200 &&
                yyjson_get_uint(yyjson_obj_get(metrics, "cached_prompt_tokens")) == 120 &&
                yyjson_get_uint(yyjson_obj_get(metrics, "output_tokens")) == 25,
            "native Ollama results retain provider timing and token metrics"
        );
        yyjson_doc_free(result_doc);
    }

    yyjson_mut_doc *tool_doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *tool_root = yyjson_mut_obj(tool_doc);
    yyjson_mut_doc_set_root(tool_doc, tool_root);
    yyjson_mut_val *tool_choices = yyjson_mut_obj_add_arr(tool_doc, tool_root, "choices");
    yyjson_mut_val *tool_choice = yyjson_mut_arr_add_obj(tool_doc, tool_choices);
    yyjson_mut_obj_add_str(tool_doc, tool_choice, "finish_reason", "tool_calls");
    yyjson_mut_val *tool_message = yyjson_mut_obj_add_obj(tool_doc, tool_choice, "message");
    yyjson_mut_obj_add_null(tool_doc, tool_message, "content");
    yyjson_mut_val *tool_calls = yyjson_mut_obj_add_arr(tool_doc, tool_message, "tool_calls");
    yyjson_mut_val *tool_call = yyjson_mut_arr_add_obj(tool_doc, tool_calls);
    yyjson_mut_obj_add_str(tool_doc, tool_call, "type", "function");
    yyjson_mut_val *function = yyjson_mut_obj_add_obj(tool_doc, tool_call, "function");
    yyjson_mut_obj_add_str(tool_doc, function, "name", "submit_plan");
    yyjson_mut_obj_add_str(
        tool_doc,
        function,
        "arguments",
        "{\"decision\":\"act\",\"operations\":[{\"op\":\"set\","
        "\"target\":\"object:1\",\"field\":\"color\",\"value\":\"#0000ff\","
        "\"alpha\":null}],\"question\":\"\",\"choices\":[],\"unsupported\":"
        "\"\"}"
    );
    char *tool_call_response = yyjson_mut_write(tool_doc, 0, NULL);
    yyjson_mut_doc_free(tool_doc);
    free(result);
    result = editor_command_model_result(packet, tool_call_response, error, sizeof error);
    test_check(result != NULL, "command model parses GPT-OSS structured tool-call output");
    if (result) {
        yyjson_doc *result_doc = NULL;
        yyjson_val *result_root = test_json_root(result, &result_doc);
        yyjson_val *operation = yyjson_arr_get(yyjson_obj_get(result_root, "operations"), 0);
        test_check(yyjson_get_uint(yyjson_obj_get(operation, "value")) == 0x0000ff44u, "command model preserves RGB and existing alpha from tool-call output");
        yyjson_doc_free(result_doc);
    }

    const char *unexpected_tool_response = "{\"choices\":[{\"message\":{"
                                           "\"content\":null,\"tool_calls\":[{"
                                           "\"type\":\"function\",\"function\":"
                                           "{\"name\":\"bash\","
                                           "\"arguments\":\"{}\"}}]}}]}";
    free(result);
    result = editor_command_model_result(packet, unexpected_tool_response, error, sizeof error);
    test_check(!result, "command model rejects tools outside its named proposal function");

    const char *question_response = "{\"choices\":[{\"message\":{\"content\":"
                                    "\"{\\\"decision\\\":\\\"ask\\\","
                                    "\\\"operations\\\":[],"
                                    "\\\"question\\\":\\\"Which object?\\\","
                                    "\\\"choices\\\":[\\\"object:1\\\"],"
                                    "\\\"unsupported\\\":\\\"\\\"}\"}}]}";
    free(result);
    result = editor_command_model_result(packet, question_response, error, sizeof error);
    test_check(result != NULL, "command model preserves clarification proposals");
    if (result) {
        yyjson_doc *result_doc = NULL;
        yyjson_val *result_root = test_json_root(result, &result_doc);
        yyjson_val *choice = yyjson_arr_get(yyjson_obj_get(result_root, "choices"), 0);
        test_check(
            !strcmp(command_string(result_root, "status"), "needs_clarification") &&
                !strcmp(command_string(choice, "id"), "object:1"),
            "command model validates clarification choices against the catalog"
        );
        yyjson_doc_free(result_doc);
    }

    free(result);
    free(request);
    free(tool_call_response);
    editor_command_model_set_settings(previous_host, previous_model, error, sizeof error);
    editor_command_model_set_json_schema(previous_schema);
}
