#include "host/browser_messages_definition.h"

#include "execution/function.h"
#include "host/browser_messages.h"

#include <assert.h>

static pin_value text(const behavior_function_context *ctx, const char *value)
{
    return (pin_value){ .type = PIN_STR,
                        .type_name = "const char *",
                        .str = ctx->copy_string(ctx->value_storage, value ? value : "") };
}
static const char *available(void *service, const pin_value *, pin_value *out)
{
    out[0] = (pin_value){ .type = PIN_BOOL,
                          .type_name = "bool",
                          .b = browser_messages_available(service) };
    return NULL;
}
static const char *request(
    const behavior_function_context *ctx,
    void *service,
    const pin_value *in,
    pin_value *out
)
{
    int handle;
    char error[256];
    bool ok = browser_messages_request(
        service,
        in[0].str,
        in[1].str,
        in[2].str,
        in[3].str,
        in[4].str,
        in[5].i,
        &handle,
        error,
        sizeof error
    );
    out[0] = (pin_value){ .type = PIN_BOOL, .type_name = "bool", .b = ok };
    out[1] = (pin_value){ .type = PIN_I32, .type_name = "int", .i = handle };
    out[2] = text(ctx, error);
    return NULL;
}
static const char *receive(
    const behavior_function_context *ctx,
    void *service,
    const pin_value *in,
    pin_value *out
)
{
    browser_message result = browser_messages_receive(service, in[0].str);
    out[0] = (pin_value){ .type = PIN_BOOL, .type_name = "bool", .b = result.available };
    out[1] = (pin_value){ .type = PIN_I32, .type_name = "int", .i = result.request };
    out[2] = (pin_value){ .type = PIN_I32, .type_name = "int", .i = result.source };
    out[3] = text(ctx, result.kind);
    out[4] = text(ctx, result.text);
    return NULL;
}
static const char *query(
    const behavior_function_context *ctx,
    void *service,
    const pin_value *in,
    pin_value *out
)
{
    json_query_result result = { 0 };
    char error[256];
    bool ok = browser_messages_query(service, in[0].i, in[1].str, &result, error, sizeof error);
    out[0] = (pin_value){ .type = PIN_BOOL, .type_name = "bool", .b = ok };
    out[1] = (pin_value){ .type = PIN_BOOL, .type_name = "bool", .b = result.found };
    out[2] = text(ctx, result.kind);
    out[3] = text(ctx, result.string);
    out[4] = (pin_value){ .type = PIN_F64, .type_name = "double", .d = result.number };
    out[5] = (pin_value){ .type = PIN_BOOL, .type_name = "bool", .b = result.boolean };
    out[6] = text(ctx, result.json);
    out[7] = (pin_value){ .type = PIN_I32, .type_name = "int", .i = result.count };
    out[8] = text(ctx, error);
    json_query_result_free(&result);
    return NULL;
}
static const char *release(void *service, const pin_value *in, pin_value *out)
{
    out[0] = (pin_value){ .type = PIN_BOOL,
                          .type_name = "bool",
                          .b = browser_messages_release(service, in[0].i) };
    return NULL;
}
#define DATA(NAME, STORAGE, TYPE)                                              \
    {                                                                          \
        .name = NAME, .kind = NATIVE_PARAMETER_DATA, .storage = STORAGE,       \
        .type_name = TYPE, .has_default = true, .default_value = {             \
            .type = STORAGE                                                    \
        }                                                                      \
    }
#define TEXT(NAME)                                                             \
    { .name = NAME,                                                            \
      .kind = NATIVE_PARAMETER_DATA,                                           \
      .storage = PIN_STR,                                                      \
      .type_name = "const char *",                                             \
      .has_default = true,                                                     \
      .default_value = { .type = PIN_STR, .str = "" } }
#define EXEC(NAME)                                                             \
    { .name = NAME, .kind = NATIVE_PARAMETER_EXEC, .storage = PIN_FLOW }
static const native_parameter_definition NONE[] = { EXEC("Exec") };
static const native_parameter_definition CHANNEL[] = { EXEC("Exec"), TEXT("Channel") };
static const native_parameter_definition SOURCE[] = { EXEC("Exec"),
                                                      DATA("Source", PIN_I32, "int") };
static const native_parameter_definition REQUEST[] = { EXEC("Exec"),
                                                       TEXT("Channel"),
                                                       TEXT("Operation"),
                                                       TEXT("Value"),
                                                       TEXT("Json"),
                                                       TEXT("Headers"),
                                                       DATA("Source", PIN_I32, "int") };
static const native_parameter_definition QUERY[] = { EXEC("Exec"),
                                                     DATA("Source", PIN_I32, "int"),
                                                     TEXT("Pointer") };
static const native_parameter_definition AVAILABLE_OUT[] = {
    EXEC("Then"),
    DATA("Available", PIN_BOOL, "bool")
};
static const native_parameter_definition RELEASE_OUT[] = {
    EXEC("Then"),
    DATA("Released", PIN_BOOL, "bool")
};
static const native_parameter_definition REQUEST_OUT[] = { EXEC("Then"),
                                                           DATA("Ok", PIN_BOOL, "bool"),
                                                           DATA("Request", PIN_I32, "int"),
                                                           TEXT("Error") };
static const native_parameter_definition RECEIVE_OUT[] = {
    EXEC("Then"),
    DATA("Available", PIN_BOOL, "bool"),
    DATA("Request", PIN_I32, "int"),
    DATA("Source", PIN_I32, "int"),
    TEXT("Kind"),
    TEXT("Text")
};
static const native_parameter_definition QUERY_OUT[] = {
    EXEC("Then"),
    DATA("Ok", PIN_BOOL, "bool"),
    DATA("Found", PIN_BOOL, "bool"),
    TEXT("Kind"),
    TEXT("String"),
    DATA("Number", PIN_F64, "double"),
    DATA("Boolean", PIN_BOOL, "bool"),
    TEXT("Json"),
    DATA("Count", PIN_I32, "int"),
    TEXT("Error")
};
#define DEF(NAME, SOURCE, IN, OUT, DESCRIPTION)                                \
    { .canonical_name = NAME,                                                  \
      .source_name = SOURCE,                                                   \
      .kind = SCRIPT_CAPABILITY_ACTION,                                        \
      .category = "Host/Browser Messages",                                     \
      .description = DESCRIPTION,                                              \
      .inputs = IN,                                                            \
      .input_count = NATIVE_COUNT(IN),                                         \
      .outputs = OUT,                                                          \
      .output_count = NATIVE_COUNT(OUT),                                       \
      .completion_output = 0,                                                  \
      .effects = SCRIPT_EFFECT_HOST }
static const native_capability_definition DEFINITIONS[] = {
    DEF("Browser Messages Available", "browser_messages_available", NONE, AVAILABLE_OUT, "Reports an installed browser transport adapter; native hosts report false."),
    DEF("Request Browser Message", "browser_message_request", REQUEST, REQUEST_OUT, "Fixed browser transport/HTTP/archive operations. Small metadata is bounded to64 KiB; large attachments remain owned by opaque Source handles."),
    DEF("Receive Browser Message", "browser_message_receive", CHANNEL, RECEIVE_OUT, "Takes one copied metadata message from the bounded channel queue. Release Source when finished; overflow requires application resynchronization."),
    DEF("Query Browser Message", "browser_message_query", QUERY, QUERY_OUT, "Queries retained raw attachment JSON by RFC6901 pointer without exposing the entire attachment to authored state. Selected strings are bounded to64 KiB; larger container Json is omitted while Kind/Count remain available."),
    DEF("Release Browser Message", "browser_message_release", SOURCE, RELEASE_OUT, "Releases an owned opaque browser attachment. A stale/foreign handle returns false.")
};
#define CALL(NAME, FIELD, FUNCTION)                                            \
    static const node_host_call NAME = {                                       \
        .service = "browser_messages",                                         \
        .missing_service = "Browser message service unavailable",              \
        .FIELD = FUNCTION                                                      \
    }
CALL(AVAILABLE_CALL, invoke, available);
CALL(REQUEST_CALL, context, request);
CALL(RECEIVE_CALL, context, receive);
CALL(QUERY_CALL, context, query);
CALL(RELEASE_CALL, invoke, release);
static const node_access ACCESS = { .flags = NODE_EFFECT_HOST_SINK };
#define BIND(NAME, CALL)                                                       \
    {                                                                          \
        .canonical_name = NAME, .implementation = {                            \
            .call = &CALL,                                                     \
            .access = &ACCESS                                                  \
        }                                                                      \
    }
static const native_definition_binding BINDINGS[] = {
    BIND("Browser Messages Available", AVAILABLE_CALL),
    BIND("Request Browser Message", REQUEST_CALL),
    BIND("Receive Browser Message", RECEIVE_CALL),
    BIND("Query Browser Message", QUERY_CALL),
    BIND("Release Browser Message", RELEASE_CALL)
};
const native_definition_pack BROWSER_MESSAGES_DEFINITION_PACK = {
    .definitions = DEFINITIONS,
    .definition_count = NATIVE_COUNT(DEFINITIONS),
    .bindings = BINDINGS,
    .binding_count = NATIVE_COUNT(BINDINGS)
};
