#ifndef CHIBI_UI_REPLAY_RUNTIME_H
#define CHIBI_UI_REPLAY_RUNTIME_H
#include "host/ui_replay.h"
struct engine_viewport;
struct ui_world;
struct scene_instance;
typedef struct ui_replay_session ui_replay_session;
/* Consumes replay on success. Reads only explicit bounded artifact/package inputs. */
ui_replay_session *ui_replay_session_new(ui_replay *, const char *artifact, char *error, size_t);
bool ui_replay_session_input(ui_replay_session *, int frame, struct engine_viewport *);
bool ui_replay_session_check(
    ui_replay_session *,
    int frame,
    const struct ui_world *,
    const struct scene_instance *,
    struct engine_viewport *
);
bool ui_replay_session_finished(const ui_replay_session *);
const char *ui_replay_session_error(const ui_replay_session *);
bool ui_replay_session_write(
    ui_replay_session *,
    const char *path,
    const char *capture,
    bool complete,
    const char *backend,
    const char *behavior,
    int width,
    int height,
    double fixed_dt
);
void ui_replay_session_free(ui_replay_session *);
/* Regular-file bytes bounded before allocation; owned, NUL-terminated. */
char *ui_replay_read_file(const char *, size_t maximum, size_t *bytes);
#endif
