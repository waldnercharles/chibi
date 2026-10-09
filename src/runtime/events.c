#include "runtime/events.h"

#include <assert.h>
#include <string.h>

static const game_event_field TICK_PAYLOAD[] = {
    { .name = "DeltaSeconds", .type = PIN_F32, .type_name = "float" },
};
static const game_event_field INPUT_PAYLOAD[] = {
    { .name = "Direction", .type = PIN_STRUCT, .type_name = "CF_V2" },
};
static const game_event_field CONTACT_PAYLOAD[] = {
    { .name = "Other", .type = PIN_HANDLE, .type_name = "ecs.entity" },
    { .name = "Point", .type = PIN_STRUCT, .type_name = "CF_V2" },
};

const game_host_event_desc GAME_HOST_EVENTS[GAME_HOST_EVENT_COUNT] = {
    [GAME_HOST_EVENT_BEGIN] = { .name = "Begin", .keywords = "start spawn create init ready", .description = "Runs once when the behavior starts." },
    [GAME_HOST_EVENT_TICK] = { .name = "Tick",
                               .keywords = "update frame every loop delta "
                                           "time",
                               .description = "Runs every frame; DeltaSeconds "
                                              "is the time since the last "
                                              "one.",
                               .payload = TICK_PAYLOAD,
                               .payload_count = 1 },
    [GAME_HOST_EVENT_INPUT] = { .name = "Input",
                                .keywords = "move direction stick controls "
                                            "player",
                                .description = "Runs with the movement "
                                               "Direction for entities the "
                                               "player controls.",
                                .payload = INPUT_PAYLOAD,
                                .payload_count = 1 },
    [GAME_HOST_EVENT_CONTACT_BEGIN] = { .name = "Contact Begin",
                                        .keywords = "hit collide collision "
                                                    "touch overlap enter",
                                        .description = "Runs when this entity "
                                                       "starts touching Other "
                                                       "at Point.",
                                        .payload = CONTACT_PAYLOAD,
                                        .payload_count = 2 },
};

const char *game_host_event_name(game_host_event event)
{
    assert(event >= 0 && event < GAME_HOST_EVENT_COUNT);
    return GAME_HOST_EVENTS[event].name;
}

bool game_host_event_is(const char *name, game_host_event event)
{
    return name && !strcmp(name, game_host_event_name(event));
}

bool game_host_event_reserved(const char *name)
{
    for (int i = 0; i < GAME_HOST_EVENT_COUNT; i++) {
        if (game_host_event_is(name, i)) {
            return true;
        }
    }
    return false;
}
