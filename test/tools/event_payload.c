#include "execution/status.h"
#include "runtime/behaviors/entities.h"
#include "support/xalloc.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/array.h"
#include "types/catalog.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    const char *name;
} event_label;
typedef struct
{
    event_label label;
    int id;
} event_row;
static const type_field LABEL_FIELDS[] = { { .name = "name",
                                             .type = PIN_STR,
                                             .type_name = "const char *",
                                             .offset = offsetof(event_label, name),
                                             .size = sizeof(const char *) } };
static const type_desc LABEL_TYPE = { .name = "EventPayloadLabel",
                                      .size = sizeof(event_label),
                                      .fields = LABEL_FIELDS,
                                      .field_count = 1 };
static const type_field ROW_FIELDS[] = { { .name = "label",
                                           .type = PIN_STRUCT,
                                           .type_name = "EventPayloadLabel",
                                           .offset = offsetof(event_row, label),
                                           .size = sizeof(event_label) },
                                         { .name = "id",
                                           .type = PIN_I32,
                                           .type_name = "int",
                                           .offset = offsetof(event_row, id),
                                           .size = sizeof(int) } };
static const type_desc ROW_TYPE = { .name = "EventPayloadRow",
                                    .size = sizeof(event_row),
                                    .fields = ROW_FIELDS,
                                    .field_count = 2 };
static const type_desc *const TYPES[] = { &LABEL_TYPE, &ROW_TYPE };
static const type_catalog_pack TYPE_PACK = { .types = TYPES, .type_count = 2 };
static entities *service;
static ecs_world *world;
static ecs_entity target;
static int delivered;
static bool owned_payload;
static bool append_next;
static int receive(void *, ecs_entity, const char *name, execution_arguments input)
{
    delivered++;
    if (!strcmp(name, "Selection")) {
        if (input.count != 2) {
            return FLOW_ERR_ARGUMENT;
        }
        const pin_array *rows = input.values[0].ptr, *labels = input.values[1].ptr;
        if (!rows || rows->count != 2 || !labels || labels->count != 1) {
            return FLOW_ERR_ARGUMENT;
        }
        const event_row *first = rows->items[0].ptr, *second = rows->items[1].ptr;
        owned_payload = first->id == 7 && second->id == 11 &&
                        !strcmp(first->label.name, "雪☃") &&
                        !strcmp(second->label.name, "café") &&
                        !strcmp(labels->items[0].str, "selected✓");
        if (!owned_payload) {
            return FLOW_ERR_ARGUMENT;
        }
        if (append_next) {
            return entities_request_event(service, target, "Next", input);
        }
    }
    if (!strcmp(name, "Next")) {
        const pin_array *rows = input.values[0].ptr;
        owned_payload = rows && rows->count == 2 &&
                        !strcmp(((event_row *)rows->items[0].ptr)->label.name, "雪☃");
        if (!owned_payload) {
            return FLOW_ERR_ARGUMENT;
        }
    }
    return FLOW_OK;
}
static int destroy(void *, const ecs_entity *, int)
{
    return FLOW_OK;
}
static int attach(void *, ecs_entity, const char *)
{
    return FLOW_OK;
}
static void setup()
{
    type_catalog_set_project(&TYPE_PACK);
    world = ecs_world_new();
    const entity_host host = { .destroy = destroy, .attach = attach, .dispatch = receive };
    service = entities_new(world, &host);
    target = ecs_world_create_entity(world);
    ecs_world_apply_commands(world);
    delivered = 0;
    owned_payload = false;
    append_next = false;
}
static void cleanup()
{
    entities_free(service);
    ecs_world_free(world);
    type_catalog_set_project(NULL);
}
TEST_CASE(event_arrays_own_nested_strings_after_original_release_and_requeue)
{
    char *first = xstrdup("雪☃"), *second = xstrdup("café"),
         *label = xstrdup("selected✓");
    event_row *records = xmalloc(2 * sizeof *records);
    records[0] = (event_row){ { first }, 7 };
    records[1] = (event_row){ { second }, 11 };
    pin_array *rows = pin_array_new(PIN_STRUCT, "EventPayloadRow", 2);
    pin_value row = { .type = PIN_STRUCT,
                      .type_name = "EventPayloadRow",
                      .ptr = &records[0] };
    REQUIRE(pin_array_push(rows, &row) == 0);
    row.ptr = &records[1];
    REQUIRE(pin_array_push(rows, &row) == 1);
    pin_array *labels = pin_array_new(PIN_STR, "const char *", 1);
    pin_value text = { .type = PIN_STR, .type_name = "const char *", .str = label };
    REQUIRE(pin_array_push(labels, &text) == 0);
    pin_value input[] = { { .type = PIN_STRUCT,
                            .type_name = "EventPayloadRow",
                            .container = PIN_CONTAINER_ARRAY,
                            .ptr = rows },
                          { .type = PIN_STR,
                            .type_name = "const char *",
                            .container = PIN_CONTAINER_ARRAY,
                            .ptr = labels } };
    append_next = true;
    REQUIRE(
        entities_request_event(service, target, "Selection", (execution_arguments){ input, 2 }) == FLOW_OK
    );
    records[0].id = -1;
    memset(first, 'x', strlen(first));
    memset(second, 'x', strlen(second));
    memset(label, 'x', strlen(label));
    pin_array_free(rows);
    pin_array_free(labels);
    free(records);
    free(first);
    free(second);
    free(label);
    REQUIRE(delivered == 0 && entities_pending_events(service) == 1);
    REQUIRE(entities_dispatch_events(service) == FLOW_OK);
    REQUIRE(delivered == 1 && owned_payload && entities_pending_events(service) == 1);
    REQUIRE(entities_dispatch_events(service) == FLOW_OK);
    REQUIRE(delivered == 2 && owned_payload && entities_pending_events(service) == 0);
    return true;
}
TEST_CASE(event_arrays_reject_task_tokens_and_malformed_shapes_without_queuing)
{
    pin_value task = { .type = PIN_TASK, .u64 = 42 };
    REQUIRE(
        entities_request_event(service, target, "Task", (execution_arguments){ &task, 1 }) == FLOW_ERR_ARGUMENT
    );
    pin_array malformed = { .element_type = PIN_I32,
                            .count = 1,
                            .capacity = 1,
                            .items = &task };
    pin_value array = { .type = PIN_I32, .container = PIN_CONTAINER_ARRAY, .ptr = &malformed };
    REQUIRE(
        entities_request_event(service, target, "WrongItem", (execution_arguments){ &array, 1 }) ==
        FLOW_ERR_ARGUMENT
    );
    malformed.count = -1;
    REQUIRE(
        entities_request_event(service, target, "Negative", (execution_arguments){ &array, 1 }) ==
        FLOW_ERR_ARGUMENT
    );
    malformed.count = 4097;
    malformed.capacity = 4097;
    REQUIRE(
        entities_request_event(service, target, "TooMany", (execution_arguments){ &array, 1 }) ==
        FLOW_ERR_ARGUMENT
    );
    REQUIRE(entities_pending_events(service) == 0);
    return true;
}
TEST_CASE(event_queue_stays_bounded_and_reset_releases_accounted_ownership)
{
    for (int i = 0; i < ENTITIES_EVENT_QUEUE_COUNT_MAX; i++) {
        REQUIRE(
            entities_request_event(service, target, "Count", (execution_arguments){ 0 }) == FLOW_OK
        );
    }
    REQUIRE(
        entities_request_event(service, target, "Overflow", (execution_arguments){ 0 }) == FLOW_ERR_STEP_BUDGET
    );
    REQUIRE(entities_dispatch_events(service) == FLOW_OK);
    REQUIRE(
        delivered == 64 &&
        entities_pending_events(service) == ENTITIES_EVENT_QUEUE_COUNT_MAX - 64
    );
    entities_reset(service);
    REQUIRE(entities_pending_events(service) == 0);
    REQUIRE(
        entities_request_event(service, target, "AfterReset", (execution_arguments){ 0 }) == FLOW_OK
    );
    REQUIRE(entities_dispatch_events(service) == FLOW_OK);
    REQUIRE(delivered == 65);
    return true;
}
void test_event_payload()
{
    pu_setup(setup, cleanup);
    RUN_TEST_CASE(event_arrays_own_nested_strings_after_original_release_and_requeue);
    RUN_TEST_CASE(event_arrays_reject_task_tokens_and_malformed_shapes_without_queuing);
    RUN_TEST_CASE(event_queue_stays_bounded_and_reset_releases_accounted_ownership);
    pu_clear_setup();
}
