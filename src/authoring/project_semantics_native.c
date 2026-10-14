#include "authoring/project_semantics_definition.h"
#include "execution/function.h"
#include "types/array.h"

#include <string.h>

static const char *copy(arena *storage, const char *text)
{
    if (!text) {
        text = "";
    }
    char *out = arena_alloc(storage, strlen(text) + 1);
    strcpy(out, text);
    return out;
}
static const char *run(
    const behavior_function_context *context,
    void *service,
    const pin_value *in,
    pin_value *out,
    int operation
)
{
    arena *storage = context->value_storage;
    char error[256] = { 0 };
    if (operation == 3) {
        project_diagnostic *rows = NULL;
        int count = 0, total = 0;
        project_semantics_diagnostics(service, in[0].i, in[1].i, storage, &rows, &count, &total, error, sizeof error);
        pin_array *array = pin_array_snapshot_allocate(storage, PIN_STRUCT, "ProjectDiagnostic", count);
        for (int i = 0; i < count; i++) {
            array->items[i] = (pin_value){ .type = PIN_STRUCT,
                                           .type_name = "ProjectDiagnostic",
                                           .ptr = &rows[i] };
        }
        out[0].ptr = array;
        out[1].i = total;
        out[2].str = copy(storage, error);
        return NULL;
    }
    if (operation == 2) {
        int changed[WORKSPACE_DOCUMENT_COUNT_MAX], count = 0;
        out[0].b = project_semantics_rename(
            service,
            in[0].i,
            in[1].i,
            in[2].i,
            in[3].str,
            changed,
            &count,
            error,
            sizeof error
        );
        pin_array *array = pin_array_snapshot_allocate(storage, PIN_I32, "int", count);
        for (int i = 0; i < count; i++) {
            array->items[i] = (pin_value){ .type = PIN_I32, .i = changed[i] };
        }
        out[1].ptr = array;
        out[2].str = copy(storage, error);
        return NULL;
    }
    int count = 0, total = 0;
    document_call *calls = NULL;
    project_reference *references = NULL;
    if (operation == 0) {
        project_semantics_calls(
            service,
            in[0].i,
            in[1].i,
            in[2].i,
            in[3].i,
            in[4].i,
            in[5].i,
            storage,
            &calls,
            &count,
            &total,
            error,
            sizeof error
        );
    } else {
        project_semantics_references(
            service,
            in[0].i,
            in[1].i,
            in[2].i,
            in[3].i,
            in[4].i,
            storage,
            &references,
            &count,
            &total,
            error,
            sizeof error
        );
    }
    const char *name = operation == 0 ? "DocumentCall" : "ProjectReference";
    pin_array *array = pin_array_snapshot_allocate(storage, PIN_STRUCT, name, count);
    for (int i = 0; i < count; i++) {
        array->items[i] = (pin_value){ .type = PIN_STRUCT,
                                       .type_name = name,
                                       .ptr = operation == 0
                                                ? (void *)&calls[i]
                                                : (void *)&references[i] };
    }
    out[0].ptr = array;
    out[1].i = total;
    out[2].str = copy(storage, error);
    return NULL;
}
#define CALLBACK(N)                                                            \
    static const char *call##N(                                                \
        const behavior_function_context *context,                              \
        void *service,                                                         \
        const pin_value *in,                                                   \
        pin_value *out                                                         \
    )                                                                          \
    {                                                                          \
        return run(context, service, in, out, N);                              \
    }
CALLBACK(0) CALLBACK(1) CALLBACK(2) CALLBACK(3)
#define CALL(N)                                                                \
    { .service = "project_semantics",                                          \
      .optional_service = true,                                                \
      .missing_service = "Project semantic service is unavailable",            \
      .context = call##N },
const node_host_call PROJECT_SEMANTICS_CALLS[4]={CALL(0) CALL(1) CALL(2) CALL(3)};
const node_access PROJECT_SEMANTICS_READ_ACCESS = { .flags = NODE_EFFECT_WORLD_READ };
const node_access PROJECT_SEMANTICS_WRITE_ACCESS = { .flags = NODE_EFFECT_HOST_SINK };
