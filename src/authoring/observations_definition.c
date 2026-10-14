#include "authoring/observations_definition.h"

#include "execution/function.h"

#include <assert.h>
#include <stddef.h>
#include <string.h>
#define COUNT(A) ((int)(sizeof A / sizeof *A))
#define FIELD(T, N, P, S)                                                      \
    { .name = #N,                                                              \
      .type = P,                                                               \
      .type_name = S,                                                          \
      .offset = offsetof(T, N),                                                \
      .size = sizeof(((T *)0)->N) }
#define I(T, N) FIELD(T, N, PIN_I32, "int")
#define D(T, N) FIELD(T, N, PIN_F64, "double")
#define S(T, N) FIELD(T, N, PIN_STR, "const char *")
#define B(T, N) FIELD(T, N, PIN_BOOL, "bool")
static const type_field LOG_FIELDS[] = {
    I(authoring_log_row, sequence), I(authoring_log_row, line),
    D(authoring_log_row, time),     S(authoring_log_row, program),
    S(authoring_log_row, level),    S(authoring_log_row, text),
    S(authoring_log_row, path),     B(authoring_log_row, truncated)
};
static const type_field TIMING_FIELDS[] = {
    D(authoring_timing, time),        D(authoring_timing, frame_ms),
    D(authoring_timing, behavior_ms), D(authoring_timing, physics_ms),
    D(authoring_timing, systems_ms),  D(authoring_timing, rendering_ms),
    D(authoring_timing, gpu_ms),      D(authoring_timing, budget_ms),
    D(authoring_timing, resolution),  I(authoring_timing, steps),
    I(authoring_timing, backlog),     B(authoring_timing, available),
    B(authoring_timing, playing),     B(authoring_timing, lighting)
};
const type_desc AUTHORING_LOG_ROW_LAYOUT = { .name = "AuthoringLogRow",
                                             .size = sizeof(authoring_log_row),
                                             .fields = LOG_FIELDS,
                                             .field_count = COUNT(LOG_FIELDS) };
const type_desc AUTHORING_TIMING_LAYOUT = { .name = "AuthoringTiming",
                                            .size = sizeof(authoring_timing),
                                            .fields = TIMING_FIELDS,
                                            .field_count = COUNT(TIMING_FIELDS) };
const native_type_definition AUTHORING_OBSERVATIONS_TYPES[2] = {
    { .layout = &AUTHORING_LOG_ROW_LAYOUT, .storage = PIN_STRUCT },
    { .layout = &AUTHORING_TIMING_LAYOUT, .storage = PIN_STRUCT }
};
static const type_desc *const TYPES[] = { &AUTHORING_LOG_ROW_LAYOUT,
                                          &AUTHORING_TIMING_LAYOUT };
const type_catalog_pack AUTHORING_OBSERVATIONS_TYPE_PACK = { .types = TYPES,
                                                             .type_count = COUNT(TYPES) };
#define PORT(N, P, T, C, V)                                                    \
    { .name = N,                                                               \
      .kind = NATIVE_PARAMETER_DATA,                                           \
      .storage = P,                                                            \
      .type_name = T,                                                          \
      .container = C,                                                          \
      .has_default = true,                                                     \
      .default_value = V }
#define INT(N, V)                                                              \
    PORT(N, PIN_I32, "int", PIN_CONTAINER_SINGLE, ((pin_value){ .type = PIN_I32, .i = V }))
#define TEXT(N)                                                                \
    PORT(N, PIN_STR, "const char *", PIN_CONTAINER_SINGLE, ((pin_value){ .type = PIN_STR, .str = "" }))
#define DOUBLE(N)                                                              \
    PORT(N, PIN_F64, "double", PIN_CONTAINER_SINGLE, ((pin_value){ .type = PIN_F64 }))
#define BOOL(N)                                                                \
    PORT(N, PIN_BOOL, "bool", PIN_CONTAINER_SINGLE, ((pin_value){ .type = PIN_BOOL }))
#define RECORD(N, T)                                                           \
    PORT(N, PIN_STRUCT, T, PIN_CONTAINER_SINGLE, ((pin_value){ .type = PIN_STRUCT }))
#define EXEC(N)                                                                \
    { .name = N, .kind = NATIVE_PARAMETER_EXEC, .storage = PIN_FLOW }
static const native_parameter_definition QUERY_IN[] = {
    EXEC("Exec"),    INT("After", 0), INT("First", 0), INT("Count", 64),
    TEXT("Program"), TEXT("Level"),   TEXT("Query")
};
static const native_parameter_definition QUERY_OUT[] = {
    EXEC("Then"),
    BOOL("Ok"),
    TEXT("Error"),
    INT("Revision", 0),
    INT("Total", 0),
    INT("Dropped", 0),
    PORT(
        "Rows",
        PIN_STRUCT,
        "AuthoringLogRow",
        PIN_CONTAINER_ARRAY,
        ((pin_value){ .type = PIN_STRUCT, .container = PIN_CONTAINER_ARRAY })
    )
};
static const native_parameter_definition APPEND_IN[] = {
    EXEC("Exec"), DOUBLE("Time"), TEXT("Program"), TEXT("Level"),
    TEXT("Text"), TEXT("Path"),   INT("Line", 0)
};
static const native_parameter_definition APPEND_OUT[] = { EXEC("Then"), BOOL("Ok") };
static const native_parameter_definition NONE_IN[] = { EXEC("Exec") },
                                         NONE_OUT[] = { EXEC("Then") };
static const native_parameter_definition TIMING_OUT[] = {
    EXEC("Then"),
    RECORD("Host", "AuthoringTiming"),
    RECORD("Target", "AuthoringTiming")
};
#define DEF(N, S, I, O, D)                                                     \
    { .canonical_name = N,                                                     \
      .source_name = S,                                                        \
      .kind = SCRIPT_CAPABILITY_ACTION,                                        \
      .category = "Authoring",                                                 \
      .description = D,                                                        \
      .inputs = I,                                                             \
      .input_count = COUNT(I),                                                 \
      .outputs = O,                                                            \
      .output_count = COUNT(O),                                                \
      .completion_output = 0,                                                  \
      .effects = SCRIPT_EFFECT_HOST }
static const native_capability_definition DEFS[] = {
    DEF("Query Authoring Log", "query_authoring_log", QUERY_IN, QUERY_OUT, "Copies bounded observed logs. Sequence is monotonic across Clear; Count<=256. Program/Level exact, Query ASCII case-insensitive. Dropped and per-row Truncated are explicit."),
    DEF("Append Authoring Log", "append_authoring_log", APPEND_IN, APPEND_OUT, "Records authored operation feedback in the same bounded log stream. Does not print, save, execute or modify a document."),
    DEF("Clear Authoring Log",
        "clear_authoring_log",
        NONE_IN,
        NONE_OUT,
        "Clears retained observations without reusing log sequence identities."),
    DEF("Query Authoring Timing", "query_authoring_timing", NONE_IN, TIMING_OUT, "Reads latest actual host and target measurements separately. Unavailable values remain explicit; negative GPU time means no timestamps. Caller owns sampling, history and presentation.")
};
static char *string(arena *a, const char *s)
{
    size_t n = strlen(s) + 1;
    char *r = arena_alloc(a, n);
    memcpy(r, s, n);
    return r;
}
static const char *query(
    const behavior_function_context *ctx,
    void *service,
    const pin_value *in,
    pin_value *out
)
{
    pin_array *rows = NULL;
    authoring_log_status r = authoring_observations_query(
        service,
        in[0].i,
        in[1].i,
        in[2].i,
        in[3].str,
        in[4].str,
        in[5].str,
        ctx->value_storage,
        &rows
    );
    out[0].b = r.ok;
    out[1].str = string(ctx->value_storage, r.error);
    out[2].i = r.revision;
    out[3].i = r.total;
    out[4].i = r.dropped;
    out[5].ptr = rows;
    return NULL;
}
static const char *append(
    const behavior_function_context *ctx,
    void *service,
    const pin_value *in,
    pin_value *out
)
{
    (void)ctx;
    out[0].b = authoring_observations_append(
        service,
        in[0].d,
        in[1].str,
        in[2].str,
        in[3].str,
        in[4].str,
        in[5].i
    );
    return NULL;
}
static const char *clear(
    const behavior_function_context *ctx,
    void *service,
    const pin_value *in,
    pin_value *out
)
{
    (void)ctx;
    (void)in;
    (void)out;
    authoring_observations_clear(service);
    return NULL;
}
static const char *timing(
    const behavior_function_context *ctx,
    void *service,
    const pin_value *in,
    pin_value *out
)
{
    (void)ctx;
    (void)in;
    assert(out[0].ptr && out[1].ptr);
    authoring_timing h = authoring_observations_read_timing(service, false),
                     t = authoring_observations_read_timing(service, true);
    memcpy(out[0].ptr, &h, sizeof h);
    memcpy(out[1].ptr, &t, sizeof t);
    return NULL;
}
static const node_host_call
    QUERY = { .service = "authoring_observations",
              .missing_service = "Authoring observations unavailable",
              .context = query },
    APPEND = { .service = "authoring_observations",
               .missing_service = "Authoring observations unavailable",
               .context = append },
    CLEAR = { .service = "authoring_observations",
              .missing_service = "Authoring observations unavailable",
              .context = clear },
    TIMING = { .service = "authoring_observations",
               .missing_service = "Authoring observations unavailable",
               .context = timing };
static const node_access READ = { .flags = NODE_EFFECT_WORLD_READ },
                         WRITE = { .flags = NODE_EFFECT_HOST_SINK };
#define BIND(N, C, A)                                                          \
    {                                                                          \
        .canonical_name = N, .implementation = {.call = &C, .access = &A }     \
    }
static const native_definition_binding BINDS[] = {
    BIND("Query Authoring Log", QUERY, READ),
    BIND("Append Authoring Log", APPEND, WRITE),
    BIND("Clear Authoring Log", CLEAR, WRITE),
    BIND("Query Authoring Timing", TIMING, READ)
};
const native_definition_pack AUTHORING_OBSERVATIONS_DEFINITION_PACK = {
    .definitions = DEFS,
    .definition_count = COUNT(DEFS),
    .bindings = BINDS,
    .binding_count = COUNT(BINDS),
    .layouts = TYPES,
    .layout_count = COUNT(TYPES)
};
