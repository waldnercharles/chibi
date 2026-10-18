#include "authoring/studio_outliner_definition.h"

#include "execution/prepared_producer.h"
#include "runtime/arrays.h"
#include "runtime/text_format.h"
#include "support/array.h"
#include "types/array.h"
#include "types/record.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static const char *capture(
    const behavior_function_context *context,
    void *host,
    const pin_value *arguments,
    pin_value *outputs
)
{
    (void)arguments;
    const studio_outliner_service *service = host;
    if (!context->value_storage || !service->capture) {
        return "Outliner Capture requires activation storage and a bound "
               "adapter";
    }
    void *scene_backing = outputs[STUDIO_OUTLINER_SCENE].ptr;
    const char *error = service->capture(context, service->user, outputs);
    if (error) {
        return error;
    }
    for (int i = 0; i < STUDIO_OUTLINER_CAPTURE_OUTPUTS; i++) {
        pin_value *value = &outputs[i];
        if (i == STUDIO_OUTLINER_SCENE) {
            if (value->type != PIN_STRUCT || value->container != PIN_CONTAINER_SINGLE ||
                value->record || !value->type_name ||
                strcmp(value->type_name, STUDIO_OUTLINER_SCENE_TYPE) ||
                !value->ptr || value->ptr != scene_backing) {
                return "Outliner Capture scene differs from its declared "
                       "layout";
            }
            const studio_outliner_scene *scene = value->ptr;
            if (!arena_contains(context->value_storage, scene->filter) ||
                !arena_contains(context->value_storage, scene->scenePath)) {
                return "Outliner Capture scene text must be an owned snapshot";
            }
            continue;
        }
        const pin_array *array = value->ptr;
        pin_type type = i < 2                                    ? PIN_STRUCT
                      : i == STUDIO_OUTLINER_ATTACHMENT_ENTITIES ? PIN_HANDLE
                                                                 : PIN_I32;
        const char *name = i == 0 ? STUDIO_OUTLINER_ACTOR_TYPE
                         : i == 1 ? STUDIO_OUTLINER_PROTOTYPE_TYPE
                         : i == STUDIO_OUTLINER_ATTACHMENT_ENTITIES
                             ? "ecs.entity"
                             : NULL;
        if (value->type != type || value->container != PIN_CONTAINER_ARRAY ||
            value->record || !array || !arena_contains(context->value_storage, array) ||
            !pin_array_valid(array) || array->element_type != type || array->record ||
            (name && (!value->type_name || strcmp(value->type_name, name) ||
                      !array->type_name || strcmp(array->type_name, name)))) {
            return "Outliner Capture array differs from its declared layout";
        }
        if (array->count && !arena_contains(
                                context->value_storage,
                                array->stride ? array->data : (void *)array->items
                            )) {
            return "Outliner Capture array must be an owned snapshot";
        }
        for (int j = 0; type == PIN_STRUCT && j < array->count; j++) {
            pin_value item = pin_array_get(array, j);
            if (item.type != PIN_STRUCT || item.container != PIN_CONTAINER_SINGLE ||
                item.record || !item.type_name || strcmp(item.type_name, name) ||
                !item.ptr || !arena_contains(context->value_storage, item.ptr)) {
                return "Outliner Capture requires backed record elements";
            }
            if (i == STUDIO_OUTLINER_ACTORS) {
                const studio_outliner_actor *actor = item.ptr;
                if (!arena_contains(context->value_storage, actor->name) ||
                    !arena_contains(context->value_storage, actor->prototype)) {
                    return "Outliner Capture actor text must be an owned "
                           "snapshot";
                }
            }
        }
    }
    return NULL;
}
static const char *publish(
    const behavior_function_context *context,
    void *host,
    const pin_value *arguments,
    pin_value *outputs
)
{
    const studio_outliner_service *service = host;
    if (!studio_outliner_projection_shape(&arguments[0]) ||
        !studio_outliner_rows_shape(&arguments[1]) || !arguments[0].ptr ||
        !arguments[1].ptr || !service->publish) {
        return "Outliner Publish requires the exact authored Projection and "
               "Row array";
    }
    const pin_array *rows = arguments[1].ptr;
    if (!pin_array_valid(rows) || rows->element_type != PIN_STRUCT ||
        !pin_record_same(rows->record, arguments[1].record) ||
        !rows->type_name || strcmp(rows->type_name, arguments[1].type_name)) {
        return "Outliner Publish row array differs from its declared layout";
    }
    bool accepted = false;
    const char *error = service->publish(context, service->user, arguments, arguments + 1, &accepted);
    if (!error) {
        outputs[0].b = accepted;
    }
    return error;
}
const node_host_call STUDIO_OUTLINER_CAPTURE_CALL = {
    .service = STUDIO_OUTLINER_SERVICE,
    .missing_service = "Outliner Capture requires Studio",
    .context = capture,
};
const node_host_call STUDIO_OUTLINER_PUBLISH_CALL = {
    .service = STUDIO_OUTLINER_SERVICE,
    .missing_service = "Outliner Publish requires Studio",
    .context = publish,
};
/* Capture consumes a request credit even though its facts are read-only. */
const node_access STUDIO_OUTLINER_CAPTURE_ACCESS = { .flags = NODE_EFFECT_WORLD_READ |
                                                              NODE_EFFECT_HOST_SINK };
const node_access STUDIO_OUTLINER_PUBLISH_ACCESS = { .flags = NODE_EFFECT_HOST_SINK };

#define EXEC(NAME)                                                             \
    { .name = NAME, .kind = NATIVE_PARAMETER_EXEC, .storage = PIN_FLOW }
#define DATA(NAME, TYPE, NOMINAL, CONTAINER)                                   \
    { .name = NAME,                                                            \
      .kind = NATIVE_PARAMETER_DATA,                                           \
      .storage = TYPE,                                                         \
      .type_name = NOMINAL,                                                    \
      .container = CONTAINER }
static const native_parameter_definition CAPTURE_INPUTS[] = { EXEC("Exec") };
static const native_parameter_definition CAPTURE_OUTPUTS[] = {
    EXEC("Then"),
    DATA("Actors", PIN_STRUCT, STUDIO_OUTLINER_ACTOR_TYPE, PIN_CONTAINER_ARRAY),
    DATA("Prototypes", PIN_STRUCT, STUDIO_OUTLINER_PROTOTYPE_TYPE, PIN_CONTAINER_ARRAY),
    DATA("ComponentIcons", PIN_I32, "int", PIN_CONTAINER_ARRAY),
    DATA("FieldKinds", PIN_I32, "int", PIN_CONTAINER_ARRAY),
    DATA("AttachmentEntities", PIN_HANDLE, "ecs.entity", PIN_CONTAINER_ARRAY),
    DATA("Scene", PIN_STRUCT, STUDIO_OUTLINER_SCENE_TYPE, PIN_CONTAINER_SINGLE),
};
static const native_parameter_definition PUBLISH_INPUTS[] = {
    EXEC("Exec"),
    { .name = "Projection",
      .kind = NATIVE_PARAMETER_DATA,
      .storage = PIN_FLOW,
      .container = PIN_CONTAINER_SINGLE,
      .type_flags = SCRIPT_TYPE_UNTYPED,
      .generic_group = 1 },
    { .name = "Rows",
      .kind = NATIVE_PARAMETER_DATA,
      .storage = PIN_FLOW,
      .container = PIN_CONTAINER_ARRAY,
      .type_flags = SCRIPT_TYPE_UNTYPED,
      .generic_group = 2 },
};
static const native_parameter_definition PUBLISH_OUTPUTS[] = {
    EXEC("Then"),
    DATA("Accepted", PIN_BOOL, "bool", PIN_CONTAINER_SINGLE),
};
#undef EXEC
#undef DATA
static const native_capability_definition DEFINITIONS[] = {
    { .canonical_name = STUDIO_OUTLINER_CAPTURE_NAME,
      .source_name = "capture_outliner",
      .kind = SCRIPT_CAPABILITY_ACTION,
      .inputs = CAPTURE_INPUTS,
      .input_count = NATIVE_COUNT(CAPTURE_INPUTS),
      .outputs = CAPTURE_OUTPUTS,
      .output_count = NATIVE_COUNT(CAPTURE_OUTPUTS),
      .completion_output = 0 },
    { .canonical_name = STUDIO_OUTLINER_PUBLISH_NAME,
      .source_name = "publish_outliner",
      .kind = SCRIPT_CAPABILITY_ACTION,
      .inputs = PUBLISH_INPUTS,
      .input_count = NATIVE_COUNT(PUBLISH_INPUTS),
      .outputs = PUBLISH_OUTPUTS,
      .output_count = NATIVE_COUNT(PUBLISH_OUTPUTS),
      .completion_output = 0 },
};
static const native_definition_binding BINDINGS[] = {
    { .canonical_name = STUDIO_OUTLINER_CAPTURE_NAME,
      .implementation = { .call = &STUDIO_OUTLINER_CAPTURE_CALL,
                          .access = &STUDIO_OUTLINER_CAPTURE_ACCESS } },
    { .canonical_name = STUDIO_OUTLINER_PUBLISH_NAME,
      .implementation = { .call = &STUDIO_OUTLINER_PUBLISH_CALL,
                          .access = &STUDIO_OUTLINER_PUBLISH_ACCESS } },
};
const native_definition_pack STUDIO_OUTLINER_DEFINITION_PACK = {
    .definitions = DEFINITIONS,
    .definition_count = NATIVE_COUNT(DEFINITIONS),
    .bindings = BINDINGS,
    .binding_count = NATIVE_COUNT(BINDINGS),
};
const native_type_definition STUDIO_OUTLINER_TYPE_DEFINITIONS[3] = {
    { .layout = &STUDIO_OUTLINER_ACTOR_LAYOUT, .storage = PIN_STRUCT },
    { .layout = &STUDIO_OUTLINER_PROTOTYPE_LAYOUT, .storage = PIN_STRUCT },
    { .layout = &STUDIO_OUTLINER_SCENE_LAYOUT, .storage = PIN_STRUCT },
};
static native_capabilities *trusted_catalog(char *error, size_t size)
{
    native_type_catalog
        *types = native_type_catalog_new(STUDIO_OUTLINER_TYPE_DEFINITIONS, 3, error, size);
    if (!types) {
        return NULL;
    }
    uint64_t entity_id = script_catalog_id("type", "ecs.entity");
    script_catalog_type entity = {
        .id = entity_id,
        .name = "ecs.entity",
        .type = { .storage = PIN_HANDLE, .nominal = entity_id },
        .size = sizeof(uint64_t),
    };
    script_catalog base = { .types = &entity, .type_count = 1, .entity_type = entity_id };
    const native_capability_pack type_packs[] = {
        native_type_catalog_pack(types),
        { .catalog = &base },
    };
    const native_definition_pack packs[] = {
        STUDIO_OUTLINER_DEFINITION_PACK,
        NATIVE_ARRAY_DEFINITION_PACK,
        NATIVE_TEXT_DEFINITION_PACK,
    };
    native_capabilities *native = native_capabilities_from_definition_packs(
        type_packs,
        NATIVE_COUNT(type_packs),
        packs,
        NATIVE_COUNT(packs),
        error,
        size
    );
    native_type_catalog_free(types);
    return native;
}

static execution_producer_port port(const script_catalog *catalog, const script_catalog_parameter *parameter)
{
    const script_catalog_type
        *type = script_catalog_type_by_id(catalog, parameter->type.nominal);
    const char *name = type ? type->name
                     : parameter->type.storage == PIN_HANDLE &&
                               parameter->type.nominal ==
                                   script_catalog_id("type", "ecs.entity")
                         ? "ecs.entity"
                         : NULL;
    return (execution_producer_port){
        .shape = { .type = parameter->type.storage,
                   .container = parameter->type.container,
                   .type_name = name },
        .generic = (parameter->type.flags &
                    (SCRIPT_TYPE_UNTYPED | SCRIPT_TYPE_GENERIC)) != 0,
        .group = 0,
    };
}
bool studio_outliner_module_valid(const execution_module *module, char *error, size_t size)
{
    native_capabilities *native = trusted_catalog(error, size);
    if (!native) {
        return false;
    }
    const script_catalog *catalog = native_capabilities_catalog(native);
    /* IDs, executable ABIs, implementations and access contracts all originate
     * from this trusted engine table. The candidate supplies none of them. */
    static const char *const names[] = {
        STUDIO_OUTLINER_CAPTURE_NAME, STUDIO_OUTLINER_PUBLISH_NAME,
        NATIVE_ARRAY_MAKE_NAME,       NATIVE_ARRAY_GET_NAME,
        NATIVE_ARRAY_LENGTH_NAME,     NATIVE_ARRAY_ADD_NAME,
        NATIVE_FORMAT_TEXT_NAME,      NATIVE_TEXT_EQUAL_NAME,
        NATIVE_TEXT_BYTE_LENGTH_NAME, NATIVE_TEXT_BYTE_AT_NAME,
        NATIVE_TEXT_SLICE_NAME,       NATIVE_ENTITY_DECIMAL_NAME,
    };
    arena storage = { 0 };
    const pin_record *records[2];
    studio_outliner_records(&storage, &records[0], &records[1]);
    execution_producer_call calls[NATIVE_COUNT(names)] = { 0 };
    execution_producer_port inputs[NATIVE_COUNT(names)][16] = { 0 };
    execution_producer_port outputs[NATIVE_COUNT(names)][8] = { 0 };
    bool valid = true;
    for (int i = 0; i < NATIVE_COUNT(names); i++) {
        uint64_t id = script_catalog_id("capability", names[i]);
        const script_catalog_capability *cap = script_catalog_capability_by_id(catalog, id);
        uint64_t abi = cap ? script_catalog_capability_abi(catalog, cap) : 0;
        const native_capability_binding *binding = native_capabilities_find(native, id, abi);
        if (!cap || !binding || cap->parameter_count > 16 || cap->result_count > 8) {
            valid = false;
            if (size) {
                snprintf(error, size, "Missing trusted Outliner capability: %s", names[i]);
            }
            break;
        }
        calls[i] = (execution_producer_call){
            .id = id,
            .abi = abi,
            .call = binding->call,
            .access = binding->access,
            .inputs = inputs[i],
            .input_count = cap->parameter_count,
            .outputs = outputs[i],
            .output_count = cap->result_count,
            .variadic = cap->variadic,
            .variadic_optional = cap->variadic_optional,
            .backed_results = true,
        };
        uint64_t groups[16] = { 0 };
        int group_count = 0;
        for (int j = 0; j < cap->parameter_count; j++) {
            inputs[i][j] = port(catalog, &cap->parameters[j]);
            uint64_t group = cap->parameters[j].type.parameter;
            if (group) {
                int k = 0;
                while (k < group_count && groups[k] != group) {
                    k++;
                }
                if (k == group_count) {
                    groups[group_count++] = group;
                }
                inputs[i][j].group = k + 1;
            }
        }
        for (int j = 0; j < cap->result_count; j++) {
            outputs[i][j] = port(catalog, &cap->results[j]);
            uint64_t group = cap->results[j].type.parameter;
            if (group) {
                int k = 0;
                while (k < group_count && groups[k] != group) {
                    k++;
                }
                if (k == group_count) {
                    groups[group_count++] = group;
                }
                outputs[i][j].group = k + 1;
            }
        }
    }
    /* The generic native declaration deliberately has no authored nominal IDs.
     * Every prepared Publish site must nevertheless carry these exact layouts. */
    inputs[1][0] = (execution_producer_port){ .shape = {
                                                  .type = PIN_STRUCT,
                                                  .type_name = records[1]->type.name,
                                                  .record = records[1] } };
    inputs[1][1] = (execution_producer_port){
        .shape = { .type = PIN_STRUCT,
                   .container = PIN_CONTAINER_ARRAY,
                   .type_name = records[0]->type.name,
                   .record = records[0] }
    };
    static const execution_producer_field window[] = {
        { "First", { .type = PIN_I32 } },
        { "Count", { .type = PIN_I32 } },
    };
    static const execution_producer_field open[] = { { "Open", { .type = PIN_BOOL } } };
    static const execution_producer_field object[] = { { "Object", { .type = PIN_STR } } };
    execution_producer_event events[] = {
        { .name = "Begin" },
        { .name = "ShowRows", .fields = window, .field_count = 2 },
        { .name = "Refresh" },
        { .name = "FoldRoot" },
        { .name = "UnfoldRoot" },
        { .name = "ToggleRoot" },
        { .name = "RootClicked", .fields = open, .field_count = 1 },
        { .name = "RevealObject", .fields = object, .field_count = 1 },
    };
    for (int i = 0; i < NATIVE_COUNT(events); i++) {
        execution_producer_event *event = &events[i];
        event->id = script_catalog_id("event", event->name);
        script_catalog_parameter parameters[2] = { 0 };
        for (int j = 0; j < event->field_count; j++) {
            parameters[j] = (script_catalog_parameter){
                .name = event->fields[j].name,
                .type = { .storage = event->fields[j].shape.type }
            };
        }
        script_catalog_capability declaration = {
            .id = event->id,
            .name = event->name,
            .kind = SCRIPT_CAPABILITY_EVENT,
            .parameters = parameters,
            .parameter_count = event->field_count,
            /* Project KDL event declarations have no native completion port. */
        };
        if (i == 0 || (i >= 3 && i <= 5)) {
            /* Begin and the three named UI events have no completion port;
             * the remaining events are the existing project KDL declarations. */
            declaration.completion_outcome = -1;
        }
        event->abi = script_catalog_capability_abi(catalog, &declaration);
    }
    static const char *const state_names[] = { "First", "Count", "Collapsed", "ShownOpen" };
    execution_producer_state states[4] = { 0 };
    for (int i = 0; i < 4; i++) {
        char identity[96];
        snprintf(identity, sizeof identity, "behaviors/OutlinerRows.chibi:1:%s", state_names[i]);
        states[i] = (execution_producer_state){
            .id = script_catalog_id("symbol", identity),
            .initial = { .type = i < 2 ? PIN_I32 : PIN_BOOL, .b = i == 3 }
        };
    }
    const type_desc *const native_types[] = {
        &STUDIO_OUTLINER_ACTOR_LAYOUT,
        &STUDIO_OUTLINER_PROTOTYPE_LAYOUT,
        &STUDIO_OUTLINER_SCENE_LAYOUT,
    };
    execution_producer_contract contract = {
        .module = script_catalog_id("module", "behaviors/OutlinerRows.chibi"),
        .states = states,
        .state_count = 4,
        .events = events,
        .event_count = NATIVE_COUNT(events),
        .calls = calls,
        .call_count = NATIVE_COUNT(calls),
        .native_types = native_types,
        .native_type_count = NATIVE_COUNT(native_types),
        .required_records = records,
        .required_record_count = 2,
        .service = STUDIO_OUTLINER_SERVICE,
    };
    if (valid) {
        valid = execution_prepared_producer_valid(module, &contract, error, size);
    }
    arena_free(&storage);
    native_capabilities_free(native);
    return valid;
}
