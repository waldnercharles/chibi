#include "runtime/events.h"

#include <assert.h>
#include <string.h>

static const event_field TICK_PAYLOAD[] = {
    { .name = "dt", .type = PIN_F32, .type_name = "float", .unit = "s" },
};
static const event_field INPUT_PAYLOAD[] = {
    { .name = "Direction", .type = PIN_STRUCT, .type_name = "vec2" },
};
static const event_field CONTACT_PAYLOAD[] = {
    { .name = "Other", .type = PIN_HANDLE, .type_name = "ecs.entity" },
    { .name = "Point", .type = PIN_STRUCT, .type_name = "vec2" },
};
static const event_field CONTACT_3D_PAYLOAD[] = {
    { .name = "Other", .type = PIN_HANDLE, .type_name = "ecs.entity" },
    { .name = "Point", .type = PIN_STRUCT, .type_name = "vec3" },
};

const host_event_desc HOST_EVENTS[HOST_EVENT_COUNT] = {
    [HOST_EVENT_BEGIN] = { .name = "Begin", .keywords = "start spawn create init ready", .description = "Runs once when the behavior starts." },
    [HOST_EVENT_TICK] = { .name = "Tick",
                          .keywords = "update frame every loop delta "
                                      "time",
                          .description = "Runs every frame; dt is the "
                                         "time in seconds since the last "
                                         "one.",
                          .payload = TICK_PAYLOAD,
                          .payload_count = 1 },
    [HOST_EVENT_INPUT] = { .name = "Input",
                           .keywords = "move direction stick controls "
                                       "player",
                           .description = "Runs with the movement "
                                          "Direction for entities the "
                                          "player controls.",
                           .payload = INPUT_PAYLOAD,
                           .payload_count = 1 },
    [HOST_EVENT_CONTACT_BEGIN] = { .name = "Contact Begin",
                                   .keywords = "hit collide collision "
                                               "touch overlap enter",
                                   .description = "Runs when this entity "
                                                  "starts touching Other "
                                                  "at Point.",
                                   .payload = CONTACT_PAYLOAD,
                                   .payload_count = 2 },
    [HOST_EVENT_CONTACT_BEGIN_3D] = { .name = "Contact Begin 3D",
                                      .keywords = "hit collide collision "
                                                  "touch overlap enter "
                                                  "physics",
                                      .description = "Runs when this "
                                                     "entity's 3D "
                                                     "collider starts "
                                                     "touching Other's at "
                                                     "Point.",
                                      .payload = CONTACT_3D_PAYLOAD,
                                      .payload_count = 2 },
    [HOST_EVENT_CONTACT_END_3D] = { .name = "Contact End 3D",
                                    .keywords = "separate leave exit "
                                                "release collision "
                                                "physics",
                                    .description = "Runs when this "
                                                   "entity's 3D collider "
                                                   "stops touching "
                                                   "Other's.",
                                    .payload = CONTACT_3D_PAYLOAD,
                                    .payload_count = 1 },
    [HOST_EVENT_CONTACT_END] = { .name = "Contact End",
                                 .keywords = "separate leave exit "
                                             "release collision",
                                 .description = "Runs when this entity "
                                                "stops touching Other.",
                                 .payload = CONTACT_PAYLOAD,
                                 .payload_count = 1 },
};

const char *host_event_name(host_event event)
{
    assert(event >= 0 && event < HOST_EVENT_COUNT);
    return HOST_EVENTS[event].name;
}

bool host_event_is(const char *name, host_event event)
{
    return name && !strcmp(name, host_event_name(event));
}

bool host_event_reserved(const char *name)
{
    for (int i = 0; i < HOST_EVENT_COUNT; i++) {
        if (host_event_is(name, i)) {
            return true;
        }
    }
    return false;
}
