#include "authoring/workspace_file_history_definition.h"

#include "execution/function.h"
#include "types/array.h"

#include <string.h>
static const char *copy(arena *a, const char *s)
{
    if (!s) {
        s = "";
    }
    char *r = arena_alloc(a, strlen(s) + 1);
    strcpy(r, s);
    return r;
}
static void result(arena *a, workspace_file_history_result r, pin_value *out)
{
    out[0].b = r.ok;
    out[1].i = r.token;
    out[2].str = copy(a, r.state);
    out[3].str = copy(a, r.error);
    out[4].i = r.changed;
}
static const char *delete_call(
    const behavior_function_context *c,
    void *host,
    const pin_value *in,
    pin_value *out
)
{
    const pin_array *rows = in[0].ptr;
    const char *paths[WORKSPACE_FILE_HISTORY_FILES_MAX];
    workspace_file_history_result r = { false,
                                        0,
                                        0,
                                        "unavailable",
                                        host ? "Deletion requires 1 to 64 paths"
                                             : "File history unavailable" };
    if (host && rows && rows->count > 0 &&
        rows->count <= WORKSPACE_FILE_HISTORY_FILES_MAX) {
        for (int i = 0; i < rows->count; i++) {
            paths[i] = rows->items[i].str;
        }
        r = workspace_files_delete(host, paths, rows->count);
    }
    result(c->value_storage, r, out);
    return NULL;
}
#define TRANSITION(N, F)                                                                   \
    static const char *                                                                    \
    N(const behavior_function_context *c, void *host, const pin_value *in, pin_value *out) \
    {                                                                                      \
        result(c->value_storage, F(host, in[0].i), out);                                   \
        return NULL;                                                                       \
    }
TRANSITION(restore_call, workspace_files_restore)
TRANSITION(redo_call, workspace_files_redo)
TRANSITION(forget_call, workspace_files_forget)
static pin_array *strings(arena *a, const char *const *text, int count)
{
    pin_array *rows = pin_array_snapshot_allocate(a, PIN_STR, "const char *", count);
    for (int i = 0; i < count; i++) {
        rows->items[i] = (pin_value){ .type = PIN_STR, .str = copy(a, text[i]) };
    }
    return rows;
}
static const char *read_call(
    const behavior_function_context *c,
    void *host,
    const pin_value *in,
    pin_value *out
)
{
    workspace_file_history_view v = { 0 };
    bool valid = workspace_file_history_read(host, in[0].i, &v);
    arena *a = c->value_storage;
    out[0].b = valid;
    out[1].str = copy(
        a,
        valid  ? v.result.state
        : host ? "closed"
               : "unavailable"
    );
    out[2].str = copy(
        a,
        valid  ? v.result.error
        : host ? "File history token is closed"
               : "File history unavailable"
    );
    out[3].ptr = strings(a, v.paths, v.count);
    out[4].ptr = strings(a, v.recovery, v.count);
    out[5].i = (int)v.bytes;
    out[6].b = v.can_undo;
    out[7].b = v.can_redo;
    return NULL;
}
#define E(N) { .name = N, .kind = NATIVE_PARAMETER_EXEC, .storage = PIN_FLOW }
#define P(N, T, S)                                                             \
    { .name = N, .kind = NATIVE_PARAMETER_DATA, .storage = T, .type_name = S }
#define S(N) P(N, PIN_STR, "const char *")
#define I(N) P(N, PIN_I32, "int")
#define B(N) P(N, PIN_BOOL, "bool")
#define A(N)                                                                   \
    { .name = N,                                                               \
      .kind = NATIVE_PARAMETER_DATA,                                           \
      .storage = PIN_STR,                                                      \
      .type_name = "const char *",                                             \
      .container = PIN_CONTAINER_ARRAY }
static const native_parameter_definition DELETE_IN[] = { E("Exec"), A("Paths") };
static const native_parameter_definition HANDLE_IN[] = { E("Exec"), I("Token") },
                                         READ_IN[] = { I("Token") };
static const native_parameter_definition ACTION_OUT[] = {
    E("Then"), B("Ok"), I("Token"), S("State"), S("Error"), I("Changed")
};
static const native_parameter_definition READ_OUT[] = {
    B("Valid"),    S("State"), S("Error"),   A("Paths"),
    A("Recovery"), I("Bytes"), B("CanUndo"), B("CanRedo")
};
#define D(N, SRC, IN)                                                          \
    { .canonical_name = N,                                                     \
      .source_name = SRC,                                                      \
      .kind = SCRIPT_CAPABILITY_ACTION,                                        \
      .inputs = IN,                                                            \
      .input_count = NATIVE_COUNT(IN),                                         \
      .outputs = ACTION_OUT,                                                   \
      .output_count = NATIVE_COUNT(ACTION_OUT),                                \
      .completion_output = 0 }
static const native_capability_definition DEFINITIONS[] = {
    D("Delete Workspace Files", "delete_workspace_files", DELETE_IN),
    D("Restore Workspace Files", "restore_workspace_files", HANDLE_IN),
    D("Redo Workspace File Deletion", "redo_workspace_file_deletion", HANDLE_IN),
    D("Forget Workspace File History", "forget_workspace_file_history", HANDLE_IN),
    { .canonical_name = "Read Workspace File History",
      .source_name = "read_workspace_file_history",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .pure = true,
      .inputs = READ_IN,
      .input_count = 1,
      .outputs = READ_OUT,
      .output_count = 8,
      .completion_output = -1 }
};
#define CALL(N, F)                                                             \
    static const node_host_call N = {                                          \
        .service = "workspace_file_history",                                   \
        .optional_service = true,                                              \
        .missing_service = "Workspace file history unavailable",               \
        .context = F                                                           \
    }
CALL(DELETE, delete_call);
CALL(RESTORE, restore_call);
CALL(REDO, redo_call);
CALL(FORGET, forget_call);
CALL(READ, read_call);
static const node_access READ_ACCESS = { .flags = NODE_EFFECT_WORLD_READ },
                         WRITE_ACCESS = { .flags = NODE_EFFECT_HOST_SINK };
static const native_definition_binding BINDINGS[] = {
    { .canonical_name = "Delete Workspace Files",
      .implementation = { .call = &DELETE, .access = &WRITE_ACCESS } },
    { .canonical_name = "Restore Workspace Files",
      .implementation = { .call = &RESTORE, .access = &WRITE_ACCESS } },
    { .canonical_name = "Redo Workspace File Deletion",
      .implementation = { .call = &REDO, .access = &WRITE_ACCESS } },
    { .canonical_name = "Forget Workspace File History",
      .implementation = { .call = &FORGET, .access = &WRITE_ACCESS } },
    { .canonical_name = "Read Workspace File History",
      .implementation = { .call = &READ, .access = &READ_ACCESS } }
};
const native_definition_pack WORKSPACE_FILE_HISTORY_DEFINITION_PACK = {
    .definitions = DEFINITIONS,
    .definition_count = 5,
    .bindings = BINDINGS,
    .binding_count = 5
};
