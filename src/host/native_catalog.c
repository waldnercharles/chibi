#include "host/native_catalog.h"

#include "runtime/events.h"
#include "support/array.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_PACKS 64

static bool receiver_present(
    const char *name,
    const native_capability_pack *native,
    int native_count,
    const native_definition_pack *definitions,
    int definition_count
)
{
    if (!name) {
        return true;
    }
    uint64_t id = script_catalog_id("type", name);
    for (int i = 0; i < native_count; i++) {
        const script_catalog *catalog = native[i].catalog;
        if (catalog && script_catalog_type_by_id(catalog, id)) {
            return true;
        }
    }
    for (int i = 0; i < definition_count; i++) {
        if (script_catalog_type_by_id(&definitions[i].base, id)) {
            return true;
        }
    }
    return false;
}
native_capabilities *host_native_catalog_new(
    const native_capability_pack *native,
    int native_count,
    const native_definition_pack *definitions,
    int definition_count,
    char *error,
    size_t size
)
{
    assert(!size || error);
    if (native_count < 0 || definition_count < 0 || native_count > MAX_PACKS ||
        definition_count > MAX_PACKS || (native_count && !native) ||
        (definition_count && !definitions)) {
        if (size) {
            snprintf(error, size, "Invalid application capability selection");
        }
        return NULL;
    }
    native_capability_definition *events = NULL;
    native_parameter_definition *payloads[GAME_HOST_EVENT_COUNT] = { 0 };
    char *observed[GAME_HOST_EVENT_COUNT] = { 0 };
    for (int i = 0; i < GAME_HOST_EVENT_COUNT; i++) {
        const game_host_event_desc *event = &GAME_HOST_EVENTS[i];
        if (!receiver_present(event->receiver_type, native, native_count, definitions, definition_count)) {
            continue;
        }
        if (event->receiver_field) {
            assert(event->receiver_type);
            size_t bytes = strlen(event->receiver_type) +
                           strlen(event->receiver_field) + 2;
            observed[i] = xmalloc(bytes);
            snprintf(observed[i], bytes, "%s.%s", event->receiver_type, event->receiver_field);
        }
        for (int j = 0; j < event->payload_count; j++) {
            const game_event_field *field = &event->payload[j];
            apush(
                payloads[i],
                ((native_parameter_definition){ .name = field->name,
                                                .kind = NATIVE_PARAMETER_DATA,
                                                .storage = field->type,
                                                .container = field->container,
                                                .type_name = field->type_name })
            );
        }
        apush(
            events,
            ((native_capability_definition){ .canonical_name = event->name,
                                             .kind = SCRIPT_CAPABILITY_EVENT,
                                             .inputs = payloads[i],
                                             .input_count = acount(payloads[i]),
                                             .completion_output = -1,
                                             .receiver_type = event->receiver_type,
                                             .component_type = event->receiver_type,
                                             .component_field = observed[i] })
        );
    }
    native_definition_pack *selected = NULL;
    for (int i = 0; i < definition_count; i++) {
        apush(selected, definitions[i]);
    }
    apush(
        selected,
        ((native_definition_pack){
            .base = { .entity_type = script_catalog_id("type", "ecs.entity"),
                      .tick_event = script_catalog_id(
                          "event",
                          GAME_HOST_EVENTS[GAME_HOST_EVENT_TICK].name
                      ) },
            .definitions = events,
            .definition_count = acount(events) })
    );
    native_capabilities *out = native_capabilities_from_definition_packs(
        native,
        native_count,
        selected,
        acount(selected),
        error,
        size
    );
    afree(selected);
    afree(events);
    for (int i = 0; i < GAME_HOST_EVENT_COUNT; i++) {
        afree(payloads[i]);
        free(observed[i]);
    }
    return out;
}
