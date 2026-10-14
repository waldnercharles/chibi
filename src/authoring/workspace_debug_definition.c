#include "authoring/workspace_debug_definition.h"

#include <stddef.h>

_Static_assert(ECS_QUERY_VARS == 8, "WorkspaceDebugRow query reflection must cover every ECS tuple variable");
#define FIELD(N, K, T)                                                         \
    { .name = #N,                                                              \
      .type = K,                                                               \
      .type_name = T,                                                          \
      .offset = offsetof(workspace_debug_row, N),                              \
      .size = sizeof(((workspace_debug_row *)0)->N) }
#define S(N) FIELD(N, PIN_STR, "const char *")
#define U(N) FIELD(N, PIN_U64, "uint64")
#define I(N) FIELD(N, PIN_I32, "int")
#define B(N) FIELD(N, PIN_BOOL, "bool")
#define Q(N)                                                                           \
    { .name = "query" #N,                                                              \
      .type = PIN_U64,                                                                 \
      .type_name = "uint64",                                                           \
      .offset = offsetof(workspace_debug_row, query_variables) + N * sizeof(uint64_t), \
      .size = sizeof(uint64_t) }
static const type_field FIELDS[] = {
    S(prototype), FIELD(creation, PIN_I64, "int64"),
    I(object),    Q(0),
    Q(1),         Q(2),
    Q(3),         Q(4),
    Q(5),         Q(6),
    Q(7),         S(source),
    S(kind),      S(name),
    S(text),      S(type),
    U(owner),     U(module),
    U(function),  U(occurrence),
    U(run),       U(step),
    I(index),     I(depth),
    I(start),     I(end),
    I(line),      I(column),
    I(status),    B(enabled),
    B(available), B(retired),
    B(output),    U(generation)
};
const type_desc WORKSPACE_DEBUG_ROW_LAYOUT = { .name = "WorkspaceDebugRow",
                                               .size = sizeof(workspace_debug_row),
                                               .fields = FIELDS,
                                               .field_count = NATIVE_COUNT(FIELDS) };
static const type_field ACTIVITY_FIELDS[] = {
    { .name = "source",
      .type = PIN_STR,
      .type_name = "const char *",
      .offset = offsetof(workspace_debug_activity, source),
      .size = sizeof(((workspace_debug_activity *)0)->source) },
    { .name = "generation",
      .type = PIN_U64,
      .type_name = "uint64",
      .offset = offsetof(workspace_debug_activity, generation),
      .size = sizeof(((workspace_debug_activity *)0)->generation) },
    { .name = "owner",
      .type = PIN_U64,
      .type_name = "uint64",
      .offset = offsetof(workspace_debug_activity, owner),
      .size = sizeof(((workspace_debug_activity *)0)->owner) },
    { .name = "module",
      .type = PIN_U64,
      .type_name = "uint64",
      .offset = offsetof(workspace_debug_activity, module),
      .size = sizeof(((workspace_debug_activity *)0)->module) },
    { .name = "function",
      .type = PIN_U64,
      .type_name = "uint64",
      .offset = offsetof(workspace_debug_activity, function),
      .size = sizeof(((workspace_debug_activity *)0)->function) },
    { .name = "occurrence",
      .type = PIN_U64,
      .type_name = "uint64",
      .offset = offsetof(workspace_debug_activity, occurrence),
      .size = sizeof(((workspace_debug_activity *)0)->occurrence) },
    { .name = "from_module",
      .type = PIN_U64,
      .type_name = "uint64",
      .offset = offsetof(workspace_debug_activity, from_module),
      .size = sizeof(((workspace_debug_activity *)0)->from_module) },
    { .name = "from_function",
      .type = PIN_U64,
      .type_name = "uint64",
      .offset = offsetof(workspace_debug_activity, from_function),
      .size = sizeof(((workspace_debug_activity *)0)->from_function) },
    { .name = "from_occurrence",
      .type = PIN_U64,
      .type_name = "uint64",
      .offset = offsetof(workspace_debug_activity, from_occurrence),
      .size = sizeof(((workspace_debug_activity *)0)->from_occurrence) },
    { .name = "count",
      .type = PIN_U64,
      .type_name = "uint64",
      .offset = offsetof(workspace_debug_activity, count),
      .size = sizeof(((workspace_debug_activity *)0)->count) },
    { .name = "query0",
      .type = PIN_U64,
      .type_name = "uint64",
      .offset = offsetof(workspace_debug_activity, query_variables) + 0 * sizeof(uint64_t),
      .size = sizeof(uint64_t) },
    { .name = "query1",
      .type = PIN_U64,
      .type_name = "uint64",
      .offset = offsetof(workspace_debug_activity, query_variables) + 1 * sizeof(uint64_t),
      .size = sizeof(uint64_t) },
    { .name = "query2",
      .type = PIN_U64,
      .type_name = "uint64",
      .offset = offsetof(workspace_debug_activity, query_variables) + 2 * sizeof(uint64_t),
      .size = sizeof(uint64_t) },
    { .name = "query3",
      .type = PIN_U64,
      .type_name = "uint64",
      .offset = offsetof(workspace_debug_activity, query_variables) + 3 * sizeof(uint64_t),
      .size = sizeof(uint64_t) },
    { .name = "query4",
      .type = PIN_U64,
      .type_name = "uint64",
      .offset = offsetof(workspace_debug_activity, query_variables) + 4 * sizeof(uint64_t),
      .size = sizeof(uint64_t) },
    { .name = "query5",
      .type = PIN_U64,
      .type_name = "uint64",
      .offset = offsetof(workspace_debug_activity, query_variables) + 5 * sizeof(uint64_t),
      .size = sizeof(uint64_t) },
    { .name = "query6",
      .type = PIN_U64,
      .type_name = "uint64",
      .offset = offsetof(workspace_debug_activity, query_variables) + 6 * sizeof(uint64_t),
      .size = sizeof(uint64_t) },
    { .name = "query7",
      .type = PIN_U64,
      .type_name = "uint64",
      .offset = offsetof(workspace_debug_activity, query_variables) + 7 * sizeof(uint64_t),
      .size = sizeof(uint64_t) },
};
const type_desc WORKSPACE_DEBUG_ACTIVITY_LAYOUT = {
    .name = "WorkspaceDebugActivity",
    .size = sizeof(workspace_debug_activity),
    .fields = ACTIVITY_FIELDS,
    .field_count = NATIVE_COUNT(ACTIVITY_FIELDS)
};
const native_type_definition WORKSPACE_DEBUG_TYPES[2] = {
    { .layout = &WORKSPACE_DEBUG_ROW_LAYOUT, .storage = PIN_STRUCT },
    { .layout = &WORKSPACE_DEBUG_ACTIVITY_LAYOUT, .storage = PIN_STRUCT }
};
#undef S
#undef U
#undef I
#undef B
#define P(N, K, T)                                                             \
    { .name = N, .kind = NATIVE_PARAMETER_DATA, .storage = K, .type_name = T }
#define E(N) { .name = N, .kind = NATIVE_PARAMETER_EXEC, .storage = PIN_FLOW }
#define S(N) P(N, PIN_STR, "const char *")
#define U(N) P(N, PIN_U64, "uint64")
#define I(N) P(N, PIN_I32, "int")
#define B(N) P(N, PIN_BOOL, "bool")
#define R(N) P(N, PIN_STRUCT, "WorkspaceDebugRow")
#define A(N)                                                                   \
    { .name = N,                                                               \
      .kind = NATIVE_PARAMETER_DATA,                                           \
      .storage = PIN_STRUCT,                                                   \
      .type_name = "WorkspaceDebugRow",                                        \
      .container = PIN_CONTAINER_ARRAY }
static const native_parameter_definition ACTIVITY_IN[] = { E("Exec"),
                                                           I("Handle"),
                                                           U("Generation"),
                                                           U("Owner") };
static const native_parameter_definition ACTIVITY_OUT[] = {
    E("Then"),
    B("Valid"),
    { .name = "Rows",
      .kind = NATIVE_PARAMETER_DATA,
      .storage = PIN_STRUCT,
      .type_name = "WorkspaceDebugActivity",
      .container = PIN_CONTAINER_ARRAY },
    U("Dropped"),
    B("Retired"),
    S("Error")
};
static const native_parameter_definition QUERY_IN[] = { E("Exec"), I("Session") },
                                         QUERY_OUT[] = { E("Then"), A("Rows"), S("Error") };
static const native_parameter_definition OPEN_IN[] = { E("Exec"),
                                                       I("Session"),
                                                       S("Name"),
                                                       U("Owner") },
                                         OPEN_OUT[] = { E("Then"), I("Handle"), S("Error") };
static const native_parameter_definition READ_IN[] = { E("Exec"), I("Handle") }, READ_OUT[] = {
    E("Then"),   B("Valid"),  R("Cursor"), A("Frames"),
    A("Values"), A("Points"), S("Error")
};
static const native_parameter_definition
    CONTROL_IN[] = { E("Exec"),   I("Handle"),   S("Operation"),
                     U("Module"), U("Function"), U("Occurrence"),
                     B("Enabled") },
    CONTROL_OUT[] = { E("Then"), B("Valid"), S("Error") };
static const native_parameter_definition CLOSE_IN[] = { E("Exec"), I("Handle") },
                                         CLOSE_OUT[] = { E("Then"), B("Valid") };
static const native_parameter_definition
    SOURCE_IN[] = { E("Exec"), I("Handle"), S("Asset") },
    SOURCE_OUT[] = { E("Then"), B("Valid"), S("Path"), S("Text"), S("Error") };
static const native_parameter_definition PROTOTYPES_IN[] = { E("Exec"),
                                                             I("Session"),
                                                             I("First"),
                                                             I("Count"),
                                                             S("Query") };
static const native_parameter_definition PROTOTYPES_OUT[] = { E("Then"),
                                                              A("Rows"),
                                                              I("Total"),
                                                              S("Error") };
static const native_parameter_definition SPAWNS_IN[] = { E("Exec"),
                                                         I("Session"),
                                                         S("Prototype"),
                                                         I("Object"),
                                                         P("After", PIN_I64, "int64") };
static const native_parameter_definition SPAWNS_OUT[] = { E("Then"), A("Rows"), S("Error") };
#define D(N, SRC, IN, OUT)                                                     \
    { .canonical_name = N,                                                     \
      .source_name = SRC,                                                      \
      .kind = SCRIPT_CAPABILITY_ACTION,                                        \
      .inputs = IN,                                                            \
      .input_count = NATIVE_COUNT(IN),                                         \
      .outputs = OUT,                                                          \
      .output_count = NATIVE_COUNT(OUT),                                       \
      .completion_output = 0 }
const native_capability_definition WORKSPACE_DEBUG_DEFINITIONS[9] = {
    D("Read Debug Source", "read_debug_source", SOURCE_IN, SOURCE_OUT),
    D("Query Debug Targets", "query_debug_targets", QUERY_IN, QUERY_OUT),
    D("Open Debug Target", "open_debug_target", OPEN_IN, OPEN_OUT),
    D("Inspect Debug Target", "inspect_debug_target", READ_IN, READ_OUT),
    D("Control Debug Target", "control_debug_target", CONTROL_IN, CONTROL_OUT),
    D("Close Debug Target", "close_debug_target", CLOSE_IN, CLOSE_OUT),
    D("Inspect Debug Activity", "inspect_debug_activity", ACTIVITY_IN, ACTIVITY_OUT),
    D("Query Debug Prototypes", "query_debug_prototypes", PROTOTYPES_IN, PROTOTYPES_OUT),
    D("Query Debug Spawns", "query_debug_spawns", SPAWNS_IN, SPAWNS_OUT)
};
