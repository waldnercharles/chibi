#ifndef CHIBI_RUNTIME_EVENTS_H
#define CHIBI_RUNTIME_EVENTS_H

#include "types/value.h"

typedef struct
{
    const char *name;
    pin_type type;
    const char *type_name;
    pin_container container;
    const char *unit; /* Optional erased numeric dimension, such as "s". */
} event_field;

typedef enum
{
    HOST_EVENT_BEGIN,
    HOST_EVENT_TICK,
    HOST_EVENT_INPUT,
    HOST_EVENT_CONTACT_BEGIN,
    HOST_EVENT_CONTACT_BEGIN_3D,
    HOST_EVENT_CONTACT_END_3D,
    HOST_EVENT_CONTACT_END,
    HOST_EVENT_COUNT,
} host_event;

/* An event the engine dispatches: its name, words for and a sentence on when
 * it fires, and the payload its Event node outputs after Then. */
typedef struct
{
    const char *name;
    const char *keywords;
    const char *description;
    const event_field *payload;
    int payload_count;
} host_event_desc;

extern const host_event_desc HOST_EVENTS[HOST_EVENT_COUNT];
const char *host_event_name(host_event event);
bool host_event_is(const char *name, host_event event);

bool host_event_reserved(const char *name);

#endif
