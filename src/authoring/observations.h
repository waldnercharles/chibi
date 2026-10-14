#ifndef CHIBI_AUTHORING_OBSERVATIONS_H
#define CHIBI_AUTHORING_OBSERVATIONS_H
#include "support/arena.h"
#include "types/array.h"

#include <stdbool.h>
typedef struct authoring_observations authoring_observations;
typedef struct
{
    int sequence, line;
    double time;
    const char *program, *level, *text, *path;
    bool truncated;
} authoring_log_row;
typedef struct
{
    double time, frame_ms, behavior_ms, physics_ms, systems_ms, rendering_ms,
        gpu_ms, budget_ms, resolution;
    int steps, backlog;
    bool available, playing, lighting;
} authoring_timing;
typedef struct
{
    bool ok;
    int revision, total, dropped;
    const char *error;
} authoring_log_status;
/* Thread-confined copied observations, independent of UI and renderer. At most
 * 512 rows; bounded text is UTF-8 boundary truncated with an explicit flag.
 * Query pages <=256 copy into caller arena. Clear keeps monotonic identities. */
authoring_observations *authoring_observations_new(void);
void authoring_observations_free(authoring_observations *);
bool authoring_observations_append(
    authoring_observations *,
    double time,
    const char *program,
    const char *level,
    const char *text,
    const char *path,
    int line
);
void authoring_observations_clear(authoring_observations *);
authoring_log_status authoring_observations_query(
    const authoring_observations *,
    int after,
    int first,
    int count,
    const char *program,
    const char *level,
    const char *query,
    arena *,
    pin_array **rows
);
/* Host and target are separate. Available=false and gpu_ms=-1 describe absent
 * measurements; simulation time and host wall frame time are never substituted. */
void authoring_observations_timing(authoring_observations *, bool target, authoring_timing);
authoring_timing authoring_observations_read_timing(const authoring_observations *, bool target);
#endif
