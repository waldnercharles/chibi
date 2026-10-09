#include "host/native_catalog.h"

#include "runtime/events.h"
#include "support/array.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_PACKS 64

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
    for (int i = 0; i < GAME_HOST_EVENT_COUNT; i++) {
        const game_host_event_desc *event = &GAME_HOST_EVENTS[i];
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
                                             .completion_output = -1 })
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
    }
    return out;
}
