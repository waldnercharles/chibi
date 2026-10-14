#include "authoring/observations_definition.h"
#include "third_party/pico_headers/pico_unit.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
static authoring_observations *s;
static arena a;
static void setup(void)
{
    s = authoring_observations_new();
}
static void cleanup(void)
{
    authoring_observations_free(s);
    arena_free(&a);
}
static authoring_log_status query(
    int after,
    int first,
    int count,
    const char *program,
    const char *level,
    const char *text,
    pin_array **rows
)
{
    return authoring_observations_query(s, after, first, count, program, level, text, &a, rows);
}
TEST_CASE(observations_copy_filter_and_preserve_sequence_after_clear)
{
    char text[] = "Café ready";
    REQUIRE(authoring_observations_append(s, 1, "Player", "info", text, "content/Player.chibi", 12));
    text[0] = 'x';
    REQUIRE(authoring_observations_append(s, 2, "Physics", "error", "contact failed", "", 0));
    pin_array *rows;
    authoring_log_status result = query(0, 0, 64, "Player", "info", "READY", &rows);
    REQUIRE(result.ok && result.total == 1 && rows->count == 1);
    authoring_log_row *r = rows->items[0].ptr;
    REQUIRE(!strcmp(r->text, "Café ready") && r->line == 12 && r->sequence == 1 && !r->truncated);
    const char *retained = r->text;
    authoring_observations_clear(s);
    REQUIRE(authoring_observations_append(s, 3, "Player", "info", "next", "", 0));
    result = query(0, 0, 64, "", "", "", &rows);
    REQUIRE(
        result.ok && result.total == 1 &&
        ((authoring_log_row *)rows->items[0].ptr)->sequence == 3
    );
    REQUIRE(!strcmp(retained, "Café ready"));
    result = query(3, 0, 64, "", "", "", &rows);
    REQUIRE(result.ok && result.total == 0 && rows->count == 0);
    return true;
}
TEST_CASE(observations_bound_retention_pages_and_utf8)
{
    for (int i = 0; i < 515; i++) {
        REQUIRE(authoring_observations_append(s, i, "", "info", "kept", "", 0));
    }
    char text[1542];
    memset(text, 'a', 1535);
    memcpy(text + 1535, "é€", 6);
    REQUIRE(authoring_observations_append(s, 516, "", "info", text, "", 0));
    pin_array *rows;
    authoring_log_status r = query(0, 511, 1, "", "", "", &rows);
    REQUIRE(r.ok && r.total == 512 && r.dropped == 4 && rows->count == 1);
    authoring_log_row *last = rows->items[0].ptr;
    REQUIRE(last->truncated && strlen(last->text) == 1535 && last->sequence == 516);
    r = query(0, 0, 257, "", "", "", &rows);
    REQUIRE(!r.ok && rows == NULL);
    REQUIRE(!authoring_observations_append(s, NAN, "", "", "", "", 0));
    REQUIRE(!authoring_observations_append(s, -1, "", "", "", "", 0));
    return true;
}
TEST_CASE(observations_separate_actual_and_unavailable_timings)
{
    authoring_timing unavailable = authoring_observations_read_timing(s, true);
    REQUIRE(!unavailable.available && unavailable.gpu_ms == -1);
    authoring_timing host = { .available = true, .time = 7, .frame_ms = 8, .gpu_ms = -1 };
    authoring_observations_timing(s, false, host);
    REQUIRE(authoring_observations_read_timing(s, false).frame_ms == 8);
    REQUIRE(!authoring_observations_read_timing(s, true).available);
    authoring_timing target = { .available = true,
                                .playing = true,
                                .time = 2,
                                .behavior_ms = 3,
                                .gpu_ms = 4,
                                .lighting = true,
                                .budget_ms = 16.7 };
    authoring_observations_timing(s, true, target);
    REQUIRE(authoring_observations_read_timing(s, true).behavior_ms == 3);
    target.behavior_ms = NAN;
    authoring_observations_timing(s, true, target);
    REQUIRE(authoring_observations_read_timing(s, true).behavior_ms == 3);
    REQUIRE(authoring_observations_read_timing(s, false).frame_ms == 8);
    return true;
}
void test_authoring_observations(void)
{
    pu_setup(setup, cleanup);
    RUN_TEST_CASE(observations_copy_filter_and_preserve_sequence_after_clear);
    RUN_TEST_CASE(observations_bound_retention_pages_and_utf8);
    RUN_TEST_CASE(observations_separate_actual_and_unavailable_timings);
    pu_clear_setup();
}
