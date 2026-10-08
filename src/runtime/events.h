#ifndef CHIBI_GAME_EVENTS_H
#define CHIBI_GAME_EVENTS_H

#include "types/value.h"

typedef struct
{
    const char *name;
    pin_type type;
    const char *type_name;
    pin_container container;
} game_event_field;

typedef enum
{
    GAME_HOST_EVENT_BEGIN,
    GAME_HOST_EVENT_TICK,
    GAME_HOST_EVENT_FIRE,
    GAME_HOST_EVENT_DASH,
    GAME_HOST_EVENT_INPUT,
    GAME_HOST_EVENT_CONTACT_BEGIN,
    GAME_HOST_EVENT_PRESSED_CHANGED,
    GAME_HOST_EVENT_COUNT,
} game_host_event;

/* An event the engine dispatches: its name, words for and a sentence on when
 * it fires, and the payload its Event node outputs after Then. */
typedef struct
{
    const char *name;
    const char *keywords;
    const char *description;
    const game_event_field *payload;
    int payload_count;
    const char *receiver_type;
    /* Reflected native field sampled by a receiver-bound subscription. */
    const char *receiver_field;
} game_host_event_desc;

extern const game_host_event_desc GAME_HOST_EVENTS[GAME_HOST_EVENT_COUNT];
const char *game_host_event_name(game_host_event event);
bool game_host_event_is(const char *name, game_host_event event);

bool game_host_event_reserved(const char *name);

#endif
