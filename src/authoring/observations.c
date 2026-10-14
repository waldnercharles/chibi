#include "authoring/observations.h"

#include "support/xalloc.h"

#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#define ROWS 512
typedef struct
{
    authoring_log_row row;
    char program[256], level[32], text[1537], path[1025];
} stored_row;
struct authoring_observations
{
    stored_row rows[ROWS];
    int first, count, sequence, revision, dropped;
    authoring_timing host, target;
};
authoring_observations *authoring_observations_new(void)
{
    authoring_observations *s = xcalloc(1, sizeof *s);
    s->revision = 1;
    s->host.gpu_ms = s->target.gpu_ms = -1;
    return s;
}
void authoring_observations_free(authoring_observations *s)
{
    free(s);
}
static bool copy(char *to, size_t capacity, const char *from)
{
    if (!from) {
        from = "";
    }
    size_t n = strnlen(from, capacity);
    bool truncated = n == capacity;
    if (truncated) {
        n = capacity - 1;
        while (n && ((unsigned char)from[n] & 0xc0) == 0x80) {
            n--;
        }
    }
    memcpy(to, from, n);
    to[n] = 0;
    return truncated;
}
bool authoring_observations_append(
    authoring_observations *s,
    double time,
    const char *program,
    const char *level,
    const char *text,
    const char *path,
    int line
)
{
    if (!s || !isfinite(time) || time < 0 || line < 0 ||
        s->sequence == INT_MAX || s->revision == INT_MAX) {
        return false;
    }
    int index = (s->first + s->count) % ROWS;
    if (s->count == ROWS) {
        s->first = (s->first + 1) % ROWS;
        if (s->dropped < INT_MAX) {
            s->dropped++;
        }
    } else {
        s->count++;
    }
    stored_row *r = &s->rows[index];
    bool cut = copy(r->program, sizeof r->program, program);
    cut |= copy(r->level, sizeof r->level, level);
    cut |= copy(r->text, sizeof r->text, text);
    cut |= copy(r->path, sizeof r->path, path);
    r->row = (authoring_log_row){ .sequence = ++s->sequence,
                                  .time = time,
                                  .program = r->program,
                                  .level = r->level,
                                  .text = r->text,
                                  .path = r->path,
                                  .line = line,
                                  .truncated = cut };
    s->revision++;
    return true;
}
void authoring_observations_clear(authoring_observations *s)
{
    if (!s || s->revision == INT_MAX) {
        return;
    }
    s->first = s->count = s->dropped = 0;
    s->revision++;
}
static bool contains(const char *text, const char *query)
{
    if (!*query) {
        return true;
    }
    for (; *text; text++) {
        const unsigned char *a = (const unsigned char *)text,
                            *b = (const unsigned char *)query;
        while (*a && *b && tolower(*a) == tolower(*b)) {
            a++;
            b++;
        }
        if (!*b) {
            return true;
        }
    }
    return false;
}
static char *snapshot(arena *a, const char *text)
{
    size_t n = strlen(text) + 1;
    char *out = arena_alloc(a, n);
    memcpy(out, text, n);
    return out;
}
authoring_log_status authoring_observations_query(
    const authoring_observations *s,
    int after,
    int first,
    int count,
    const char *program,
    const char *level,
    const char *query,
    arena *a,
    pin_array **rows
)
{
    *rows = NULL;
    if (!s || !a || after < 0 || first < 0 || count < 0 || count > 256 ||
        !program || !level || !query || strnlen(program, 257) > 256 ||
        strnlen(level, 33) > 32 || strnlen(query, 257) > 256) {
        return (authoring_log_status){
            .error = "Invalid observation query; Count must be 0..256"
        };
    }
    int selected[ROWS], total = 0;
    for (int i = 0; i < s->count; i++) {
        int index = (s->first + i) % ROWS;
        const authoring_log_row *r = &s->rows[index].row;
        if (r->sequence <= after || (*program && strcmp(program, r->program)) ||
            (*level && strcmp(level, r->level)) || !contains(r->text, query)) {
            continue;
        }
        selected[total++] = index;
    }
    int n = first < total ? total - first : 0;
    if (n > count) {
        n = count;
    }
    *rows = pin_array_snapshot_allocate(a, PIN_STRUCT, "AuthoringLogRow", n);
    for (int i = 0; i < n; i++) {
        authoring_log_row *r = arena_alloc(a, sizeof *r);
        *r = s->rows[selected[first + i]].row;
        r->program = snapshot(a, r->program);
        r->level = snapshot(a, r->level);
        r->text = snapshot(a, r->text);
        r->path = snapshot(a, r->path);
        (*rows)->items[i] = (pin_value){ .type = PIN_STRUCT,
                                         .type_name = "AuthoringLogRow",
                                         .ptr = r };
    }
    return (authoring_log_status){ .ok = true,
                                   .revision = s->revision,
                                   .total = total,
                                   .dropped = s->dropped,
                                   .error = "" };
}
void authoring_observations_timing(authoring_observations *s, bool target, authoring_timing value)
{
    if (!s) {
        return;
    }
    double fields[] = { value.time,       value.frame_ms,   value.behavior_ms,
                        value.physics_ms, value.systems_ms, value.rendering_ms,
                        value.gpu_ms,     value.budget_ms,  value.resolution };
    for (int i = 0; i < 9; i++) {
        if (!isfinite(fields[i])) {
            return;
        }
    }
    if (value.time < 0 || value.frame_ms < 0 || value.behavior_ms < 0 ||
        value.physics_ms < 0 || value.systems_ms < 0 ||
        value.rendering_ms < 0 || value.gpu_ms < -1 || value.budget_ms < 0 ||
        value.resolution < 0 || value.steps < 0 || value.backlog < 0) {
        return;
    }
    if (target) {
        s->target = value;
    } else {
        s->host = value;
    }
}
authoring_timing authoring_observations_read_timing(const authoring_observations *s, bool target)
{
    return s ? (target ? s->target : s->host) : (authoring_timing){ .gpu_ms = -1 };
}
