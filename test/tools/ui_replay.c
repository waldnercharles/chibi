#include "host/ui_replay.h"

#include "third_party/pico_headers/pico_unit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
TEST_CASE(ui_replay_owns_utf8_input_and_keeps_assertion_types)
{
    char *source = strdup("{\"version\":1,\"frames\":8,\"events\":[{\"frame\":2,\"type\":\"click\",\"selector\":\"#name\"},{\"frame\":3,\"type\":\"text\",\"text\":\"界\"}],\"assertions\":[{\"frame\":6,\"type\":\"data\",\"entity\":\"Application\",\"name\":\"open\",\"equals\":true},{\"frame\":7,\"type\":\"data\",\"entity\":\"Application\",\"name\":\"text\",\"equals\":\"true\"}]}");
    char error[256];
    ui_replay *replay = ui_replay_parse(source, strlen(source), error, sizeof error);
    memset(source, 'x', strlen(source));
    free(source);
    REQUIRE(
        replay && replay->frames == 8 && replay->event_count == 2 &&
        replay->assertion_count == 2
    );
    REQUIRE(!strcmp(replay->events[1].text, "界"));
    REQUIRE(replay->events[0].x == .5 && replay->events[0].y == .5);
    REQUIRE(yyjson_is_bool(replay->assertions[0].expected));
    REQUIRE(yyjson_is_str(replay->assertions[1].expected));
    REQUIRE(strlen(replay->request_sha256) == 64);
    ui_replay_free(replay);
    return true;
}
TEST_CASE(ui_replay_refuses_ambiguous_structure_and_invalid_intents)
{
    static const char *const cases[] = {
        "{\"version\":1,\"version\":1,\"frames\":1,\"events\":[],"
        "\"assertions\":[]}",
        "{\"version\":2,\"frames\":1,\"events\":[],\"assertions\":[]}",
        "{\"version\":1,\"frames\":61,\"events\":[],\"assertions\":[]}",
        "{\"version\":1,\"frames\":4,\"events\":[{\"frame\":2,\"type\":"
        "\"text\",\"text\":\"a\"},{\"frame\":1,\"type\":\"text\",\"text\":"
        "\"b\"}],\"assertions\":[]}",
        "{\"version\":1,\"frames\":4,\"events\":[{\"frame\":1,\"type\":"
        "\"invoke\",\"selector\":\"#button\"}],\"assertions\":[]}",
        "{\"version\":1,\"frames\":4,\"events\":[{\"frame\":1,\"type\":"
        "\"click\",\"selector\":\"#button\",\"x\":1.1}],\"assertions\":[]}",
        "{\"version\":1,\"frames\":4,\"events\":[{\"frame\":1,\"type\":\"key-"
        "down\",\"key\":\"F13\"}],\"assertions\":[]}",
        "{\"version\":1,\"frames\":4,\"events\":[{\"frame\":1,\"type\":\"key-"
        "down\",\"key\":\"A\",\"modifiers\":[\"Mod\",\"Mod\"]}],\"assertions\":"
        "[]}",
        "{\"version\":1,\"frames\":4,\"events\":[],\"assertions\":[{\"frame\":"
        "1,\"type\":\"visible\",\"selector\":\"#button\",\"equals\":\"true\"}]}"
    };
    for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
        char error[256];
        ui_replay *replay = ui_replay_parse(cases[i], strlen(cases[i]), error, sizeof error);
        REQUIRE(!replay && error[0]);
    }
    return true;
}
TEST_CASE(ui_replay_bounds_bytes_and_combined_records)
{
    char error[256];
    char *oversized = malloc(UI_REPLAY_BYTES_MAX + 1);
    REQUIRE(oversized);
    memset(oversized, ' ', UI_REPLAY_BYTES_MAX + 1);
    REQUIRE(!ui_replay_parse(oversized, UI_REPLAY_BYTES_MAX + 1, error, sizeof error));
    free(oversized);
    char *records = malloc(65536);
    REQUIRE(records);
    size_t at = (size_t)sprintf(records, "{\"version\":1,\"frames\":1,\"events\":[");
    for (int i = 0; i < UI_REPLAY_RECORDS_MAX; i++) {
        at += (size_t)sprintf(
            records + at,
            "%s{\"frame\":0,\"type\":\"text\",\"text\":\"a\"}",
            i ? "," : ""
        );
    }
    at += (size_t)sprintf(records + at, "],\"assertions\":[{\"frame\":0,\"type\":\"visible\",\"selector\":\"#button\",\"equals\":true}]}");
    REQUIRE(!ui_replay_parse(records, at, error, sizeof error));
    REQUIRE(strstr(error, "256combined"));
    free(records);
    return true;
}
void test_ui_replay()
{
    RUN_TEST_CASE(ui_replay_owns_utf8_input_and_keeps_assertion_types);
    RUN_TEST_CASE(ui_replay_refuses_ambiguous_structure_and_invalid_intents);
    RUN_TEST_CASE(ui_replay_bounds_bytes_and_combined_records);
}
