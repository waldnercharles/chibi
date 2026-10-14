#include "authoring/workspace_imports_definition.h"

#include "execution/function.h"
#include "support/array.h"
#include "types/array.h"

#include <stddef.h>
#include <string.h>
typedef struct
{
    const char *key, *action;
} import_action_row;
static const type_field FIELDS[] = { { .name = "Key",
                                       .type = PIN_STR,
                                       .type_name = "const char *",
                                       .offset = offsetof(import_action_row, key),
                                       .size = sizeof(const char *) },
                                     { .name = "Action",
                                       .type = PIN_STR,
                                       .type_name = "const char *",
                                       .offset = offsetof(import_action_row, action),
                                       .size = sizeof(const char *) } };
static const type_desc ACTION = { .name = "WorkspaceImportAction",
                                  .size = sizeof(import_action_row),
                                  .fields = FIELDS,
                                  .field_count = 2 };
const native_type_definition WORKSPACE_IMPORTS_TYPES[1] = {
    { .layout = &ACTION, .storage = PIN_STRUCT }
};
static const type_desc *const TYPES[] = { &ACTION };
const type_catalog_pack WORKSPACE_IMPORTS_TYPE_PACK = { .types = TYPES,
                                                        .type_count = 1 };
static const char *copy(arena *a, const char *s)
{
    if (!s) {
        s = "";
    }
    char *out = arena_alloc(a, strlen(s) + 1);
    strcpy(out, s);
    return out;
}
static const char *prepare(const behavior_function_context *c, void *host, const pin_value *in, pin_value *out)
{
    char error[256] = { 0 };
    out[0]
        .i = workspace_import_prepare(host, in[0].str, in[1].str, in[2].b, in[3].b, error, sizeof error);
    out[1].str = copy(c->value_storage, error);
    return NULL;
}
static const char *read_plan(
    const behavior_function_context *c,
    void *host,
    const pin_value *in,
    pin_value *out
)
{
    workspace_import_view v = { 0 };
    bool valid = workspace_import_read(host, in[0].i, &v);
    arena *a = c->value_storage;
    out[0].b = valid;
    out[1].b = v.committed;
    out[2].str = copy(a, v.error);
    pin_array *warnings = pin_array_snapshot_allocate(
        a,
        PIN_STR,
        "const char *",
        valid ? acount(v.report->warnings) : 0
    );
    for (int i = 0; i < warnings->count; i++) {
        warnings->items[i] = (pin_value){ .type = PIN_STR,
                                          .str = copy(a, v.report->warnings[i]) };
    }
    out[3].ptr = warnings;
    pin_array *actions = pin_array_snapshot_allocate(a, PIN_STRUCT, "WorkspaceImportAction", v.action_count);
    static const char *const names[] = { "create", "update", "remove", "preserve" };
    for (int i = 0; i < v.action_count; i++) {
        import_action_row *row = arena_alloc(a, sizeof *row);
        *row = (import_action_row){ copy(a, v.actions[i].key),
                                    copy(a, names[v.actions[i].action]) };
        actions->items[i] = (pin_value){ .type = PIN_STRUCT,
                                         .type_name = "WorkspaceImportAction",
                                         .ptr = row };
    }
    out[4].ptr = actions;
    for (int i = 5; i < 9; i++) {
        out[i].i = 0;
    }
    if (valid) {
        out[5].i = v.report->vertices;
        out[6].i = v.report->triangles;
        out[7].i = v.report->materials;
        out[8].i = v.report->images;
    }
    return NULL;
}
static const char *apply(const behavior_function_context *c, void *host, const pin_value *in, pin_value *out)
{
    char error[256] = { 0 };
    out[0].b = workspace_import_apply(host, in[0].i, error, sizeof error);
    workspace_import_view v = { 0 };
    workspace_import_read(host, in[0].i, &v);
    out[1].b = v.committed;
    out[2].str = copy(c->value_storage, error);
    return NULL;
}
static const char *close_plan(
    const behavior_function_context *c,
    void *host,
    const pin_value *in,
    pin_value *out
)
{
    out[0].b = workspace_import_close(host, in[0].i);
    out[1].str = copy(c->value_storage, host ? (out[0].b ? "" : "Import plan is closed") : "Workspace imports are unavailable");
    return NULL;
}
#define E(N) { .name = N, .kind = NATIVE_PARAMETER_EXEC, .storage = PIN_FLOW }
#define P(N, T, S)                                                             \
    { .name = N, .kind = NATIVE_PARAMETER_DATA, .storage = T, .type_name = S }
#define S(N) P(N, PIN_STR, "const char *")
#define I(N) P(N, PIN_I32, "int")
#define B(N) P(N, PIN_BOOL, "bool")
static const native_parameter_definition
    PREPARE_IN[] = { E("Exec"), S("Source"), S("Name"), B("Reimport"), B("Strict") },
    PREPARE_OUT[] = { E("Then"), I("Plan"), S("Error") };
static const native_parameter_definition HANDLE_IN[] = { E("Exec"), I("Plan") };
static const native_parameter_definition READ_OUT[] = {
    E("Then"),
    B("Valid"),
    B("Committed"),
    S("Error"),
    { .name = "Warnings",
      .kind = NATIVE_PARAMETER_DATA,
      .storage = PIN_STR,
      .type_name = "const char *",
      .container = PIN_CONTAINER_ARRAY },
    { .name = "Actions",
      .kind = NATIVE_PARAMETER_DATA,
      .storage = PIN_STRUCT,
      .type_name = "WorkspaceImportAction",
      .container = PIN_CONTAINER_ARRAY },
    I("Vertices"),
    I("Triangles"),
    I("Materials"),
    I("Images")
};
static const native_parameter_definition APPLY_OUT[] = { E("Then"),
                                                         B("Valid"),
                                                         B("Committed"),
                                                         S("Error") },
                                         CLOSE_OUT[] = { E("Then"),
                                                         B("Closed"),
                                                         S("Error") };
#define D(N, SRC, IN, OUT)                                                     \
    { .canonical_name = N,                                                     \
      .source_name = SRC,                                                      \
      .kind = SCRIPT_CAPABILITY_ACTION,                                        \
      .inputs = IN,                                                            \
      .input_count = NATIVE_COUNT(IN),                                         \
      .outputs = OUT,                                                          \
      .output_count = NATIVE_COUNT(OUT),                                       \
      .completion_output = 0 }
static const native_capability_definition DEFINITIONS[] = {
    D("Prepare Workspace Model Import", "prepare_workspace_model_import", PREPARE_IN, PREPARE_OUT),
    D("Read Workspace Model Import", "read_workspace_model_import", HANDLE_IN, READ_OUT),
    D("Apply Workspace Model Import", "apply_workspace_model_import", HANDLE_IN, APPLY_OUT),
    D("Close Workspace Model Import", "close_workspace_model_import", HANDLE_IN, CLOSE_OUT)
};
#define CALL(N, F)                                                             \
    static const node_host_call N = {                                          \
        .service = "workspace_imports",                                        \
        .optional_service = true,                                              \
        .missing_service = "Workspace imports are unavailable",                \
        .context = F                                                           \
    }
CALL(PREPARE, prepare);
CALL(READ, read_plan);
CALL(APPLY, apply);
CALL(CLOSE, close_plan);
static const node_access READ_ACCESS = { .flags = NODE_EFFECT_WORLD_READ },
                         WRITE_ACCESS = { .flags = NODE_EFFECT_HOST_SINK };
static const native_definition_binding BINDINGS[] = {
    { .canonical_name = "Prepare Workspace Model Import",
      .implementation = { .call = &PREPARE, .access = &READ_ACCESS } },
    { .canonical_name = "Read Workspace Model Import",
      .implementation = { .call = &READ, .access = &READ_ACCESS } },
    { .canonical_name = "Apply Workspace Model Import",
      .implementation = { .call = &APPLY, .access = &WRITE_ACCESS } },
    { .canonical_name = "Close Workspace Model Import",
      .implementation = { .call = &CLOSE, .access = &WRITE_ACCESS } }
};
const native_definition_pack WORKSPACE_IMPORTS_DEFINITION_PACK = {
    .definitions = DEFINITIONS,
    .definition_count = 4,
    .bindings = BINDINGS,
    .binding_count = 4,
    .layouts = TYPES,
    .layout_count = 1
};
