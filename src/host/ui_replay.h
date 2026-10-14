#ifndef CHIBI_UI_REPLAY_H
#define CHIBI_UI_REPLAY_H
#include <stdbool.h>
#include <stddef.h>
#include <yyjson.h>
enum
{
    UI_REPLAY_BYTES_MAX = 256 * 1024,
    UI_REPLAY_RECORDS_MAX = 256,
    UI_REPLAY_FRAMES_MAX = 60
};
typedef enum
{
    UI_REPLAY_CLICK,
    UI_REPLAY_MOVE,
    UI_REPLAY_DOWN,
    UI_REPLAY_UP,
    UI_REPLAY_WHEEL,
    UI_REPLAY_KEY_DOWN,
    UI_REPLAY_KEY_UP,
    UI_REPLAY_TEXT
} ui_replay_event_kind;
typedef struct
{
    int frame;
    ui_replay_event_kind kind;
    const char *selector, *key, *text;
    double x, y, delta;
    unsigned modifiers; /* Ctrl1 Shift2 Alt4 Meta8; Mod128 resolves on host. */
} ui_replay_event;
typedef struct
{
    int frame;
    bool data;
    const char *selector, *entity, *name;
    yyjson_val *expected; /* Scalar JSON; borrowed from the owned replay. */
} ui_replay_assertion;
typedef struct
{
    yyjson_doc *document;
    char request_sha256[65];
    int frames, event_count, assertion_count;
    ui_replay_event events[UI_REPLAY_RECORDS_MAX];
    ui_replay_assertion assertions[UI_REPLAY_RECORDS_MAX];
} ui_replay;
/* Strict UTF-8 JSON, duplicate-key refusal, monotonic frame records. Owns all
 * strings until free. Does not read files, create a renderer or dispatch input. */
ui_replay *ui_replay_parse(const char *, size_t, char *error, size_t);
void ui_replay_free(ui_replay *);
#endif
